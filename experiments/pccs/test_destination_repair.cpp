// SPDX-License-Identifier: GPL-3.0-or-later
// White-box tests of the private destination repair proposal. This test
// includes the implementation on purpose: it checks candidates without
// exposing an unsafe "force validity" public API to the homebrew.
#include "experiments/pccs/pccs_bridge.cpp"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/legality/legality.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <array>

namespace pk=pktool::pokemon;
namespace br=pktool::pokemon::pccs_bridge;
namespace legal=pktool::legality;
#define REQUIRE(x) do { if(!(x)) {std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} }while(0)

int main() {
    pk::create4::Trainer tr;
    tr.name="ASH";tr.tid=12345;tr.version=41;tr.language=2;
    auto sourceModel=pk::create4::newPokemon(tr,155,5,255);
    sourceModel.heldItem=0;
    std::array<uint8_t,pk::entity::kMaxPartySize> sourceEntity{};
    pk::create4::encode(sourceModel,sourceEntity.data());
    const auto source=pk::pk2::toPokemon(sourceEntity.data(),pk::pk2::kEntitySize);
    auto accepted=br::previewCrystalToRuby(sourceEntity.data(),pk::pk2::kEntitySize);
    REQUIRE(accepted.ok&&!accepted.repairAvailable);
    const auto raw=accepted.data;
    const auto target=pk::pk3::toPokemon(raw.data(),raw.size());

    // Invalid destination Ball: the source, metadata and all visible
    // identity traits remain intact. The tool must preview that exact fix.
    auto wrong=target;wrong.met.ball=0;
    auto wrongBytes=raw;
    pk::pk3::applyPokemon(wrong,wrongBytes.data());
    REQUIRE(legal::checkGen3(pk::pk3::toPokemon(wrongBytes.data(),80)).overall()==legal::Severity::Invalid);
    auto fix=br::proposeDestinationRepair(source,pk::pk3::toPokemon(wrongBytes.data(),80),
                       wrongBytes,false,sourceEntity.data()+pk::pk2::kOtOffset,
                       sourceEntity.data()+pk::pk2::kNicknameOffset);
    REQUIRE(fix.available&&!fix.changes.empty());
    REQUIRE(wrongBytes!=fix.data&&raw==fix.data); // exact original Poke Ball restored
    REQUIRE(legal::checkGen3(pk::pk3::toPokemon(fix.data.data(),80)).overall()!=legal::Severity::Invalid);
    // Removing a single illegal Gen III move must be disclosed rather than
    // deleting it from the source save or silently omitting it.
    auto moveWrong=target;
    moveWrong.moves[3]=354; // Psycho Boost: not learned by Cyndaquil
    moveWrong.pp[3]=5;
    auto moveBytes=raw;
    pk::pk3::applyPokemon(moveWrong,moveBytes.data());
    REQUIRE(legal::checkGen3(pk::pk3::toPokemon(moveBytes.data(),80)).overall()==legal::Severity::Invalid);
    const auto before=moveBytes;
    auto moveFix=br::proposeDestinationRepair(source,pk::pk3::toPokemon(moveBytes.data(),80),
                       moveBytes,false,sourceEntity.data()+pk::pk2::kOtOffset,
                       sourceEntity.data()+pk::pk2::kNicknameOffset);
    REQUIRE(moveFix.available&&!moveFix.changes.empty());
    REQUIRE(before==moveBytes); // never edit the input
    const auto recovered=pk::pk3::toPokemon(moveFix.data.data(),80);
    REQUIRE(recovered.moves[3]==0 && recovered.species==target.species);
    REQUIRE(recovered.trainer.tid==target.trainer.tid && recovered.nickname==target.nickname);
    REQUIRE(br::acceptsFreshLegalOutput(recovered,legal::checkGen3(recovered)));
    bool explained=false;
    for(const auto& change:moveFix.changes)
        explained|=change.find("Remove incompatible move")!=std::string::npos;
    REQUIRE(explained);

    // A destination 'repair' that would have to change the species, level
    // or source identity must be refused, not misrepresented as legal.
    auto impossible=target;
    impossible.level=1; impossible.exp=0;
    auto invalid=raw;
    pk::pk3::applyPokemon(impossible,invalid.data());
    const auto impossibleFix=br::proposeDestinationRepair(source,pk::pk3::toPokemon(invalid.data(),80),
                      invalid,false,sourceEntity.data()+pk::pk2::kOtOffset,
                      sourceEntity.data()+pk::pk2::kNicknameOffset);
    REQUIRE(!impossibleFix.available);

    if(const char* dir=std::getenv("VPT_PK3_AUDIT_DIR")) {
        for(const auto& output: {std::make_pair("vpt038-dest-ball.pk3",&fix.data),
                                 std::make_pair("vpt038-dest-move.pk3",&moveFix.data)}) {
            const auto file=std::string(dir)+"/"+output.first;
            auto* fp=std::fopen(file.c_str(),"wb");
            REQUIRE(fp!=nullptr && std::fwrite(output.second->data(),1,80,fp)==80);
            REQUIRE(std::fclose(fp)==0);
        }
    }
    std::puts("PASS: destination repair requires identified actual mismatch, protected identity, fully validated PK3 and disclosed move changes");
    return 0;
}
