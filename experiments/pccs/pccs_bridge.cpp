// SPDX-License-Identifier: GPL-3.0-or-later
#include "pccs_bridge.h"
#include "pccs_preflight.h"
#include "pccs_policy.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/legality/legality.h"
// External PCCS headers from pinned upstream; not bundled.
#include <Gen2Pokemon.h>
#include <Gen3Pokemon.h>
namespace pktool::pokemon::pccs_bridge {
static_assert(pk2::kEntitySize == 72 && pk2::kOtOffset == 48 &&
              pk2::kNicknameOffset == 59 && pk2::kLanguageOffset == 70 &&
              pk2::kEggOffset == 71 && pk2::offset::kLevel == 31);
static_assert(pk3::kStoredSize == 80);
Preview previewCrystalToRuby(const uint8_t* entity, std::size_t size) {
    Preview result;
    switch (preflight(entity, size)) {
    case Preflight::Ready: break;
    case Preflight::InvalidSize: result.error = "Invalid PK2 entity size"; return result;
    case Preflight::UnsupportedLanguage: result.error = "English saves only in VPT001"; return result;
    case Preflight::Egg: result.error = "Hatch the egg first"; return result;
    case Preflight::InvalidSpecies: result.error = "Invalid Generation II species"; return result;
    case Preflight::HeldItem: result.error = "Remove the held item first"; return result;
    case Preflight::Mythical: result.error = "Mythical event conversion is not validated"; return result;
    case Preflight::InvalidLevel: result.error = "Invalid source level"; return result;
    }
    const auto original = pk2::toPokemon(entity, size);
    const auto sourceReport = pktool::legality::checkGen2(original);
    if (sourceReport.overall() != pktool::legality::Severity::Valid) {
        result.notes.push_back("Species: "+std::to_string(original.species)+" Lv"+std::to_string(original.level));
        result.notes.push_back("Met: "+std::to_string(original.met.metLocation)+" Lv"+std::to_string(original.met.metLevel)+" time "+std::to_string(original.met.encounterType));
        for (const auto& finding : sourceReport.findings)
            if(finding.severity != legality::Severity::Valid)
                result.notes.push_back(std::string(finding.severity==legality::Severity::Invalid?"ERROR: ":"WARN: ")+finding.text);
        for (const auto& finding : sourceReport.findings)
            if(finding.severity == legality::Severity::Valid) result.notes.push_back(finding.text);
        result.error = "Source legality cannot be confirmed";
        return result;
    }
    ::PokemonTables table;
    ::Gen2Pokemon source(::ENGLISH);
    source.loadData(::ENGLISH, entity, entity + pk2::kNicknameOffset,
                    entity + pk2::kOtOffset, entity[pk2::offset::kSpecies]);
    ::Gen3Pokemon converted;
    if (!source.convertToGen3(&table, &converted, ::LEGAL, false)) {
        result.error = "PCCS LEGAL conversion refused";
        return result;
    }
    // Newly converted PCCS data is plaintext with a stale checksum.
    // PCCS detects encryption by comparing the checksum, so initialize it
    // before serialization, following upstream examples/convertPKs/main.cpp.
    converted.setChecksum(converted.calculateChecksum());
    auto bytes = converted.outputByteArray(false, true); // canonical GAEM
    // Independent PKTool at-rest encoding/decoding must round-trip.
    auto roundtrip = bytes;
    pk3::encrypt(roundtrip.data());
    pk3::decrypt(roundtrip.data());
    if (roundtrip != bytes) {
        result.error = "PK3 encryption round trip failed";
        return result;
    }
    if (pk3::isEmptySlot(bytes.data()) || (bytes[pk3::offset::kFlags] & 1) ||
        pk3::storedChecksum(bytes.data()) != pk3::calcChecksum(bytes.data())) {
        result.error = "Invalid converted PK3 structure or checksum";
        return result;
    }
    const auto target = pk3::toPokemon(bytes.data(), bytes.size());
    if (target.species != original.species || target.isEgg ||
        target.shiny != original.shiny || target.gender != original.gender ||
        target.trainer.tid != original.trainer.tid || target.trainer.name != original.trainer.name ||
        target.trainer.gender != original.trainer.gender || target.language != original.language ||
        target.nickname != original.nickname || target.form != original.form) {
        result.error = "Conversion changes protected identity fields";
        return result;
    }
    const auto targetReport = pktool::legality::checkGen3(target);
    for (const auto& finding : targetReport.findings) result.notes.push_back(finding.text);
    if (!acceptsFreshLegalOutput(target, targetReport)) {
        result.error = "Destination legality cannot be confirmed";
        return result;
    }
    if (targetReport.overall() == legality::Severity::Warning) {
        result.notes.push_back("Expected PCCS SID=0 warning retained; Gen III has a SID, Gen II does not.");
    }
    result.data = bytes;
    result.notes.push_back("PCCS LEGAL: unofficial conversion; not an official Nintendo transfer");
    result.ok = true;
    return result;
}
}
