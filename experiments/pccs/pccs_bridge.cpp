// SPDX-License-Identifier: GPL-3.0-or-later
#include "pccs_bridge.h"
#include "pccs_preflight.h"
#include "pccs_policy.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/gb_common.h"
#include <Gen1Pokemon.h>
#include <memory>
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
Preview previewGbToGba(const uint8_t* entity, std::size_t size, uint8_t generation) {
    Preview result;
    if (generation != 1 && generation != 2) { result.error="Unsupported source generation"; return result; }
    if (generation == 1) {
        if (!entity || size != pk1::kEntitySize) { result.error="Invalid PK1 size"; return result; }
        if (!supportedLanguage(entity[pk1::kLanguageOffset])) { result.error="Unsupported language"; return result; }
        if (entity[0] >= 191 || !data::speciesFromGen1(entity[0]) ||
            data::speciesFromGen1(entity[0]) > 150) { result.error="Unsupported Gen I species or mythical"; return result; }
        if (!entity[pk1::offset::kBoxLevel] || entity[pk1::offset::kBoxLevel] > 100) {
            result.error="Invalid source level"; return result;
        }
    } else switch (preflight(entity, size)) {
    case Preflight::Ready: break;
    case Preflight::InvalidSize: result.error = "Invalid PK2 entity size"; return result;
    case Preflight::UnsupportedLanguage: result.error = "Unsupported source language"; return result;
    case Preflight::Egg: result.error = "Hatch the egg first"; return result;
    case Preflight::InvalidSpecies: result.error = "Invalid Generation II species"; return result;
    case Preflight::HeldItem: result.error = "Remove the held item first"; return result;
    case Preflight::Mythical: result.error = "Mythical event conversion is not validated"; return result;
    case Preflight::InvalidLevel: result.error = "Invalid source level"; return result;
    }
    auto original = generation==1 ? pk1::toPokemon(entity,size) : pk2::toPokemon(entity,size);
    const auto sourceReport = generation==1 ? legality::checkGen1(original) : legality::checkGen2(original);
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
    const auto sourceLanguage=static_cast<::Language>(original.language);
    // A Japanese name must terminate within its six-byte field. Avoid
    // reading beyond the logical field in the pinned converter.
    const auto validName=[&](size_t off, size_t max) {
        for(size_t i=0;i<=max;++i) if(entity[off+i]==0x50) return true;
        return false;
    };
    if(!validName(generation==1?pk1::kOtOffset:pk2::kOtOffset,original.language==1?5:7) ||
       !validName(generation==1?pk1::kNicknameOffset:pk2::kNicknameOffset,original.language==1?5:10)) {
        result.error="Invalid source name termination"; return result;
    }
    // The original GBA library assumes a generous stack. ARM9 does not.
    auto table=std::make_unique<::PokemonTables>();
    ::Gen3Pokemon converted;
    const auto otOffset = generation==1 ? pk1::kOtOffset : pk2::kOtOffset;
    const auto nickOffset = generation==1 ? pk1::kNicknameOffset : pk2::kNicknameOffset;
    bool convertedOk = false;
    if (generation==1) {
        // Gen I has no displayed gender/shiny flag; derive the expected
        // Gen II traits from its original DVs before checking the PK3.
        original.gender = gb::genderFromDvs(original.species,original.ivs);
        original.shiny = gb::isShinyDvs(original.ivs);
        ::Gen1Pokemon source(sourceLanguage);
        source.loadData(sourceLanguage,entity,entity+nickOffset,entity+otOffset,entity[0]);
        convertedOk = source.convertToGen3(table.get(),&converted,::LEGAL,false);
        result.notes.push_back("Gen I: gender and shiny derived from original DVs");
    } else {
        ::Gen2Pokemon source(sourceLanguage);
        source.loadData(sourceLanguage,entity,entity+nickOffset,entity+otOffset,entity[0]);
        convertedOk = source.convertToGen3(table.get(),&converted,::LEGAL,false);
    }
    if (!convertedOk) {
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
    // The GB codec displays the in-game trade OT control code as "*".
    // PCCS expands the exact two-byte English token to the displayed TRAINER.
    const bool tradeOt = entity[otOffset] == 0x5D &&
                         entity[otOffset + 1] == 0x50;
    if (!checkProtectedIdentity(original, target, tradeOt, result.notes,
                                entity + otOffset, entity + nickOffset)) {
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
Preview previewCrystalToRuby(const uint8_t* entity, std::size_t size) {
    return previewGbToGba(entity,size,2);
}
}
