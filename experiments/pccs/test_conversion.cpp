// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic fixtures only. No save files, no bank writes.
#include "pccs_bridge.h"
#include "pccs_policy.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/legality/legality.h"
#include <Gen2Pokemon.h>
#include <Gen3Pokemon.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace pk = pktool::pokemon;
namespace legal = pktool::legality;
namespace bridge = pk::pccs_bridge;

int main() {
    // All 28 Gen III Unown forms must be decoded from the PID, not default 0.
    for (unsigned form = 0; form < 28; ++form) {
        std::array<uint8_t, pk::pk3::kStoredSize> raw{};
        raw[0] = form & 3; raw[1] = (form >> 2) & 3;
        raw[2] = (form >> 4) & 3; raw[3] = (form >> 6) & 3;
        raw[pk::pk3::offset::kSpecies] = 201;
        raw[pk::pk3::offset::kLanguage] = 2;
        if (pk::pk3::toPokemon(raw.data(),raw.size()).form != form) return 21;
    }
    pk::Pokemon before, after;
    before.language = after.language = 2;
    before.trainer.name = "*"; after.trainer.name = "TRAINER";
    std::vector<std::string> notes;
    if (!bridge::checkProtectedIdentity(before,after,true,notes) || notes.empty()) return 22;
    notes.clear();
    if (bridge::checkProtectedIdentity(before,after,false,notes) || notes.empty()) return 23;
    after.shiny = true; after.trainer.tid = 123;
    notes.clear();
    if (bridge::checkProtectedIdentity(before,after,true,notes) || notes.size() != 3) return 24;
    after.shiny = false; after.trainer.tid = 0; after.trainer.name = "OTHER";
    notes.clear();
    if (bridge::checkProtectedIdentity(before,after,true,notes)) return 25;
    std::puts("PASS: all Unown forms and exact identity diagnostics");
    // Regression: allow only the exact non-invalid, expected PCCS warning.
    pk::Pokemon policyPokemon;
    policyPokemon.format = 3;
    policyPokemon.species = 155;
    policyPokemon.originGame = legal::version::kFireRed;
    policyPokemon.trainer.tid = 12345;
    policyPokemon.trainer.sid = 0;
    policyPokemon.pid = 0x12345678;
    legal::Report report;
    report.findings.push_back({legal::Severity::Warning, "SID is 0"});
    if (!bridge::acceptsFreshLegalOutput(policyPokemon, report)) return 10;
    auto other = report;
    other.findings.push_back({legal::Severity::Warning, "Unverified encounter"});
    if (bridge::acceptsFreshLegalOutput(policyPokemon, other)) return 11;
    other = report;
    other.findings.push_back({legal::Severity::Invalid, "Invalid moves"});
    if (bridge::acceptsFreshLegalOutput(policyPokemon, other)) return 12;
    auto wrong = policyPokemon;
    wrong.trainer.sid = 5;
    if (bridge::acceptsFreshLegalOutput(wrong, report)) return 13;
    wrong = policyPokemon;
    wrong.shiny = true;
    if (bridge::acceptsFreshLegalOutput(wrong, report)) return 14;
    wrong = policyPokemon;
    wrong.pid = wrong.trainer.tid;
    if (bridge::acceptsFreshLegalOutput(wrong, report)) return 15;
    wrong = policyPokemon;
    wrong.originGame = legal::version::kRuby;
    if (bridge::acceptsFreshLegalOutput(wrong, report)) return 16;
    wrong = policyPokemon;
    wrong.trainer.tid = 0;
    if (bridge::acceptsFreshLegalOutput(wrong, report)) return 17;
    std::puts("PASS: eight warning policy assertions");
    pk::create4::Trainer trainer;
    trainer.name = "ASH";
    trainer.tid = 12345;
    trainer.version = legal::version::kCrystal;
    trainer.language = 2;
    int passed = 0;
    int failed = 0;
    for (uint16_t species : {155, 158, 152}) {
        auto model = pk::create4::newPokemon(trainer, species, 5, 100 + species);
        model.heldItem = 0;
        std::array<uint8_t, pk::entity::kMaxPartySize> entity{};
        pk::create4::encode(model, entity.data());
        const auto saved = entity;
        const auto source = pk::pk2::toPokemon(entity.data(), pk::pk2::kEntitySize);
        std::printf("Species %u: source severity %u\n", species, unsigned(legal::checkGen2(source).overall()));
        // Direct upstream call checks the external API and serialization independently
        // of the policy wrapper. Never inject a raw conversion into a save.
        ::PokemonTables table;
        ::Gen2Pokemon input(::ENGLISH);
        input.loadData(::ENGLISH, entity.data(), entity.data() + pk::pk2::kNicknameOffset,
                       entity.data() + pk::pk2::kOtOffset, entity[0]);
        ::Gen3Pokemon output;
        if (!input.convertToGen3(&table, &output, ::LEGAL, false)) {
            std::puts("FAIL: upstream conversion refused synthetic fixture");
            ++failed;
            continue;
        }
        output.setChecksum(output.calculateChecksum());
        // Test both PCCS serialization paths independently. Copy before either
        // serialization because outputByteArray mutates its internal layout.
        ::Gen3Pokemon encryptedCopy(output);
        auto raw = output.outputByteArray(false, true);
        auto encrypted = encryptedCopy.outputByteArray(true, false);
        pk::pk3::decrypt(encrypted.data());
        if (raw != encrypted) {
            std::puts("FAIL: upstream canonical and encrypted serialization differ");
            ++failed;
            continue;
        }
        const auto decoded = pk::pk3::toPokemon(raw.data(), raw.size());
        if (pk::pk3::storedChecksum(raw.data()) != pk::pk3::calcChecksum(raw.data()) ||
            decoded.species != species || decoded.trainer.tid != source.trainer.tid) {
            std::printf("FAIL: upstream checksum or identity: checksum %04x vs %04x, species %u vs %u, TID %u vs %u\n",
                        pk::pk3::storedChecksum(raw.data()), pk::pk3::calcChecksum(raw.data()),
                        decoded.species, species, decoded.trainer.tid, source.trainer.tid);
            ++failed;
            continue;
        }
        const auto report = legal::checkGen3(decoded);
        std::printf("Upstream result: origin %u level %u severity %u\n", decoded.originGame,
                    decoded.level, unsigned(report.overall()));
        for (const auto& finding : report.findings)
            if (finding.severity != legal::Severity::Valid) std::printf("  %s\n", finding.text.c_str());
        const auto preview = bridge::previewCrystalToRuby(entity.data(), pk::pk2::kEntitySize);
        if (entity != saved) {
            std::puts("FAIL: preview modified source entity");
            ++failed;
            continue;
        }
        if (!preview.ok) {
            std::printf("FAIL: strict preview refused fixture: %s\n", preview.error.c_str());
            for (const auto& note : preview.notes) std::printf("  %s\n", note.c_str());
            ++failed;
            continue;
        }
        auto target = pk::pk3::toPokemon(preview.data.data(), preview.data.size());
        if (target.species != species || !bridge::acceptsFreshLegalOutput(target, legal::checkGen3(target))) {
            std::puts("FAIL: preview returned non-valid conversion");
            ++failed;
            continue;
        }
        std::printf("PASS: strict read-only conversion; checker severity %u (warnings retained)\n", unsigned(legal::checkGen3(target).overall()));
        ++passed;
        auto egg = entity;
        egg[pk::pk2::kEggOffset] = 1;
        if (bridge::previewCrystalToRuby(egg.data(), pk::pk2::kEntitySize).ok) ++failed;
    }
    std::printf("VPT002: %d accepted / %d failures\n", passed, failed);
    return passed == 3 && failed == 0 ? 0 : 1;
}
