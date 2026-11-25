// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_transfer.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/saves/save_commit.h"
#include "tests/fake_gb_saves.h"
#include "tests/fake_saves.h"
#include <cstdio>
#include <cstdlib>

namespace pk = pktool::pokemon;
namespace sv = pktool::saves;
namespace br = pk::pccs_bridge;
void pktool::test::reportFailure(const char* file,int line,const char* expr) {
    std::fprintf(stderr,"%s:%d: %s\n",file,line,expr); std::exit(1);
}
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main() {
    auto s=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
    auto d=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
    REQUIRE(s && d);
    pk::create4::Trainer trainer;
    trainer.name="ASH"; trainer.tid=12345; trainer.version=41; trainer.language=2;
    std::array<uint8_t,pk::entity::kMaxPartySize> a{},b{},c{};
    pk::create4::encode(pk::create4::newPokemon(trainer,155,5,255),a.data());
    pk::create4::encode(pk::create4::newPokemon(trainer,158,5,258),b.data());
    pk::create4::encode(pk::create4::newPokemon(trainer,152,5,252),c.data());
    a[1]=b[1]=c[1]=0;
    REQUIRE(s->writeBoxSlot(0,0,b.data()));
    REQUIRE(s->writeBoxSlot(0,1,a.data()));
    REQUIRE(s->writeBoxSlot(0,2,c.data()));
    REQUIRE(s->writeBoxSlot(1,0,b.data()));
    REQUIRE(s->appendParty(c.data()));
    s->refreshChecksums(); d->refreshChecksums();
    const auto originalS=s->data(),originalD=d->data();
    auto staged=br::stageCrystalToRuby(originalS,originalD,{0,1},{0,5});
    if(!staged.ok) std::fprintf(stderr,"Stage refused: %s\n",staged.error.c_str());
    REQUIRE(staged.ok);
    REQUIRE(s->data()==originalS && d->data()==originalD);
    auto sr=sv::loadSaveFile(staged.sourceAfter),dr=sv::loadSaveFile(staged.destinationAfter);
    REQUIRE(sr && dr && sv::validateSave(*sr) && sv::validateSave(*dr));
    std::array<uint8_t,pk::entity::kMaxPartySize> read{};
    REQUIRE(sr->readBoxSlot(0,0,read.data())==sv::SlotState::Valid && read[0]==158);
    REQUIRE(sr->readBoxSlot(0,1,read.data())==sv::SlotState::Valid && read[0]==152);
    REQUIRE(sr->readBoxSlot(0,2,read.data())==sv::SlotState::Empty);
    REQUIRE(sr->readBoxSlot(1,0,read.data())==sv::SlotState::Valid && read[0]==158);
    REQUIRE(sr->partyCount()==1 && sr->readPartySlot(0,read.data())==sv::SlotState::Valid && read[0]==152);
    REQUIRE(dr->readBoxSlot(0,5,read.data())==sv::SlotState::Valid);
    REQUIRE(std::equal(staged.converted.begin(),staged.converted.end(),read.begin()));
    REQUIRE(pk::pk3::toPokemon(read.data(),80).species==155);
    auto occupied=br::stageCrystalToRuby(originalS,staged.destinationAfter,{0,1},{0,5});
    REQUIRE(!occupied.ok && occupied.sourceAfter.empty() && occupied.destinationAfter.empty());
    REQUIRE(!br::stageCrystalToRuby(originalS,originalD,{0,19},{0,5}).ok);
    REQUIRE(!br::stageCrystalToRuby(originalS,originalD,{-1,0},{0,5}).ok);
    REQUIRE(!br::stageCrystalToRuby(originalS,originalD,{0,1},{14,0}).ok);
    REQUIRE(!br::stageCrystalToRuby(fakegb::blankGen2(fakegb::kGen2Gs),originalD,{0,1},{0,5}).ok);
    REQUIRE(!br::stageCrystalToRuby(originalS,fake::blankGen3(sv::Game::Emerald),{0,1},{0,5}).ok);
    REQUIRE(!br::stageCrystalToRuby(std::vector<uint8_t>{1,2,3},originalD,{0,1},{0,5}).ok);
    std::puts("PASS: staged Crystal to Ruby/Sapphire save round-trip; source compaction, exact destination, party and other boxes preserved; rejection cases passed");
    return 0;
}
