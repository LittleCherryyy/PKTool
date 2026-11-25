// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic fixtures only. No save files, no bank writes.
#include "pccs_bridge.h"
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
        auto raw = output.outputByteArray(true, false);
        pk::pk3::decrypt(raw.data());
        const auto decoded = pk::pk3::toPokemon(raw.data(), raw.size());
        if (pk::pk3::storedChecksum(raw.data()) != pk::pk3::calcChecksum(raw.data()) ||
            decoded.species != species || decoded.trainer.tid != source.trainer.tid) {
            std::puts("FAIL: upstream output checksum or identity");
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
        if (target.species != species || legal::checkGen3(target).overall() != legal::Severity::Valid) {
            std::puts("FAIL: preview returned non-valid conversion");
            ++failed;
            continue;
        }
        std::puts("PASS: strict read-only conversion");
        ++passed;
        auto egg = entity;
        egg[pk::pk2::kEggOffset] = 1;
        if (bridge::previewCrystalToRuby(egg.data(), pk::pk2::kEntitySize).ok) ++failed;
    }
    std::printf("VPT002: %d accepted / %d failures\n", passed, failed);
    return passed == 3 && failed == 0 ? 0 : 1;
}
