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
#include "core/text/gen4_text.h"
#include "core/text/gen12_text.h"
#include <algorithm>
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/data/moves.h"
// External PCCS headers from pinned upstream; not bundled.
#include <Gen2Pokemon.h>
#include <Gen3Pokemon.h>
namespace pktool::pokemon::pccs_bridge {
static_assert(pk2::kEntitySize == 72 && pk2::kOtOffset == 48 &&
              pk2::kNicknameOffset == 59 && pk2::kLanguageOffset == 70 &&
              pk2::kEggOffset == 71 && pk2::offset::kLevel == 31);
static_assert(pk3::kStoredSize == 80);
namespace {
struct DestinationRepair {
    bool available=false;
    std::array<uint8_t,80> data{};
    std::vector<std::string> changes;
};
int invalidCount(const legality::Report& report) {
    int n=0;
    for(const auto& f:report.findings)
        if(f.severity==legality::Severity::Invalid) ++n;
    return n;
}
bool sameVisibleIdentity(const Pokemon& a,const Pokemon& b) {
    // Game Boy has no PID, nature, SID or IVs in the Gen III sense.
    // Keep all traits that DO have direct meaning, plus the PCCS-synthesized
    // Gen III SID and origin label, unless the user explicitly changes games.
    return a.species==b.species && a.level==b.level &&
           a.format==b.format && a.nickname==b.nickname &&
           a.trainer.name==b.trainer.name &&
           a.trainer.tid==b.trainer.tid &&
           a.trainer.sid==b.trainer.sid &&
           a.trainer.gender==b.trainer.gender &&
           a.shiny==b.shiny && a.gender==b.gender &&
           a.form==b.form && a.language==b.language &&
           a.originGame==b.originGame && a.isEgg==b.isEgg &&
           a.nature==b.nature;
}
void noteUnlistedChanges(const Pokemon& before,const Pokemon& after,
                         std::vector<std::string>& changes) {
    if(before.pid!=after.pid && std::none_of(changes.begin(),changes.end(),[](const std::string& c) {
        return c.find("PID")!=std::string::npos;
    })) changes.push_back("PID regenerated (nature, gender and shiny retained)");
    if(before.ability!=after.ability)
        changes.push_back("Ability adjusted to match species and PID");
    if(before.heldItem!=after.heldItem)
        changes.push_back("Destination held item removed");
    if(before.met.ball!=after.met.ball)
        changes.push_back("Destination Poke Ball adjusted");
    if(before.met.metLevel!=after.met.metLevel || before.met.metLocation!=after.met.metLocation)
        changes.push_back("Gen III met location/level normalized");
    for(int i=0;i<4;++i) {
        if(before.pp[i]!=after.pp[i] || before.ppUps[i]!=after.ppUps[i])
            changes.push_back("Move "+std::to_string(i+1)+" PP / PP Ups adjusted");
    }
}
DestinationRepair proposeDestinationRepair(const Pokemon& source,const Pokemon& originalTarget,
                                          const std::array<uint8_t,80>& raw,
                                          bool tradeToken,const uint8_t* rawOt,
                                          const uint8_t* rawNickname) {
    DestinationRepair out;
    if(source.isEgg || source.species==151 || source.species==251 ||
       originalTarget.isEgg || originalTarget.species==0) return out;
    // Use the existing Gen III encounter/PID normalization rather than
    // editing the independent validity report or bypassing PCCS.
    auto repaired=legality::legalizeGen3(originalTarget,180);
    auto proposed=repaired.pokemon;
    if(!sameVisibleIdentity(originalTarget,proposed)) return out;
    auto changes=std::move(repaired.changes);
    // Some perfectly legitimate Gen II moves cannot be learned by a Gen III
    // specimen of the same species. Only offer removal of a move if it
    // demonstrably reduces the number of Gen III Invalid findings. Never
    // remove the last move. Every removal is shown before explicit approval.
    int errors=invalidCount(legality::checkGen3(proposed));
    if(errors>0) for(int index=0;index<4;++index) {
        if(!proposed.moves[index]) continue;
        Pokemon trial=proposed;
        const auto oldMove=trial.moves[index];
        trial.moves[index]=0;trial.pp[index]=0;trial.ppUps[index]=0;
        bool hasOther=false;
        for(int j=0;j<4;++j) hasOther|=trial.moves[j]!=0;
        if(!hasOther) continue;
        const int now=invalidCount(legality::checkGen3(trial));
        if(now<errors) {
            changes.push_back("Remove incompatible move: "+std::string(data::moveName(oldMove)));
            proposed=std::move(trial);
            errors=now;
        }
    }
    if(!sameVisibleIdentity(originalTarget,proposed)) return out;
    if(!acceptsFreshLegalOutput(proposed,legality::checkGen3(proposed))) return out;
    if(proposed==originalTarget || changes.empty()) return out;
    noteUnlistedChanges(originalTarget,proposed,changes);
    if(changes.empty()) return out;
    out.data=raw;
    pk3::applyPokemon(proposed,out.data.data());
    if(pk3::isEmptySlot(out.data.data()) ||
       (out.data[pk3::offset::kFlags]&1) ||
       pk3::storedChecksum(out.data.data())!=pk3::calcChecksum(out.data.data())) return {};
    const auto decoded=pk3::toPokemon(out.data.data(),out.data.size());
    if(!sameVisibleIdentity(originalTarget,decoded) ||
       !acceptsFreshLegalOutput(decoded,legality::checkGen3(decoded))) return {};
    std::vector<std::string> identityNotes;
    if(!checkProtectedIdentity(source,decoded,tradeToken,identityNotes,rawOt,rawNickname)) return {};
    auto copy=out.data;pk3::encrypt(copy.data());pk3::decrypt(copy.data());
    if(copy!=out.data) return {};
    out.available=true;out.changes=std::move(changes);
    return out;
}
} // namespace
Preview previewGbToGba(const uint8_t* entity, std::size_t size, uint8_t generation,
                       bool allowDestinationRepair) {
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
    // PCCS Gen I FR/DE charset differs from the actual GB character map.
    // Correct only exact, independently decoded names; do not transliterate
    // unknown glyphs, truncate, alter trade OT tokens or relax identity.
    const bool tradeToken=entity[otOffset]==0x5D&&entity[otOffset+1]==0x50;
    const auto rewriteName=[&](size_t sourceOffset,size_t targetOffset,size_t max,size_t width,
                               const std::string& expected) {
        const auto* raw=entity+sourceOffset;
        for(size_t i=0;i<=max;++i) {
            if(raw[i]==0x50) break;
            if(data::charGb(generation,original.language,raw[i])==0xffff) return false;
        }
        std::array<uint8_t,10> encoded{};
        encoded.fill(text::kGen3Terminator);
        text::encodeGen3(expected,encoded.data(),width,max,original.language);
        if(text::decodeGen3(encoded.data(),width,original.language)!=expected) return false;
        std::copy_n(encoded.begin(),width,bytes.begin()+targetOffset);
        return true;
    };
    if(generation==1&&(original.language==3||original.language==5)) {
        if(!tradeToken&&text::decodeGen3(bytes.data()+pk3::offset::kOtName,7,original.language)!=original.trainer.name) {
            if(!rewriteName(otOffset,pk3::offset::kOtName,7,7,original.trainer.name)) {
                result.error="Cannot preserve localized OT";return result;
            }
            result.notes.push_back("Gen I localized OT encoded without changing identity");
        }
        if(text::decodeGen3(bytes.data()+pk3::offset::kNickname,10,original.language)!=original.nickname) {
            if(!rewriteName(nickOffset,pk3::offset::kNickname,10,10,original.nickname)) {
                result.error="Cannot preserve localized nickname";return result;
            }
            result.notes.push_back("Gen I localized nickname encoded without changing identity");
        }
    }
    if(original.language==1) {
        // Japanese logical names are at most five characters. Keep their
        // exact text and ensure physical PK3 fields end with FF padding.
        if(!tradeToken&&!rewriteName(otOffset,pk3::offset::kOtName,5,7,original.trainer.name)) {
            result.error="Cannot preserve Japanese OT";return result;
        }
        if(!rewriteName(nickOffset,pk3::offset::kNickname,5,10,original.nickname)) {
            result.error="Cannot preserve Japanese nickname";return result;
        }
        // Japanese GBA saves copy six bytes of trainer text (five
        // characters + FF) and a zero byte, unlike international OT fields.
        // PKHeX MiscVerifierG3.VerifyTrashJPN checks this exact pattern.
        if(bytes[pk3::offset::kOtName+5]!=text::kGen3Terminator) {
            result.error="Japanese OT terminator missing";return result;
        }
        bytes[pk3::offset::kOtName+6]=0;
        result.notes.push_back("Japanese PK3 OT uses FF then zero padding");
    }
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
    // The first finding previously displayed in Review could be a VALID
    // informational line ("Hatched from an egg"), hiding the real blocker.
    // Put actual errors first, followed by warnings and valid observations.
    for(const auto severity:{legality::Severity::Invalid,
                             legality::Severity::Warning,legality::Severity::Valid})
        for(const auto& finding:targetReport.findings)
            if(finding.severity==severity)
                result.notes.push_back(std::string(
                    severity==legality::Severity::Invalid?"ERROR: ":
                    severity==legality::Severity::Warning?"WARN: ":"INFO: ")+finding.text);
    if (!acceptsFreshLegalOutput(target, targetReport)) {
        const auto candidate=proposeDestinationRepair(original,target,bytes,tradeOt,
                                                      entity+otOffset,entity+nickOffset);
        result.repairAvailable=candidate.available;
        result.repairChanges=candidate.changes;
        if(!candidate.available || !allowDestinationRepair) {
            result.error = "Destination legality cannot be confirmed";
            return result;
        }
        bytes=candidate.data;
        result.repaired=true;
        for(const auto& change:candidate.changes)
            result.notes.push_back("REPAIR: "+change);
        result.notes.push_back("Destination repair is optional and does not change source save");
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
