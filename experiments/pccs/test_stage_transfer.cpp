// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_transfer.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk1.h"
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
    {
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        auto em=sv::loadSaveFile(fake::blankGen3(sv::Game::Emerald));
        REQUIRE(gs&&em);
        trainer.version=39;
        std::vector<br::TransferPair> pairs;
        for(int i=0;i<10;++i) {
            auto mon=pk::create4::newPokemon(trainer,155,5,255+i);mon.heldItem=0;
            pk::create4::encode(mon,read.data());
            REQUIRE(gs->writeBoxSlot(0,i,read.data()));
            pairs.push_back({{0,i},{7,i}});
        }
        gs->refreshChecksums();em->refreshChecksums();
        const auto before=gs->data(),dest=em->data();
        {
            br::BatchStage cancelled(before,dest,pairs,2);
            REQUIRE(!cancelled.step());REQUIRE(!cancelled.step());
            REQUIRE(cancelled.completed()==1);
            REQUIRE(!cancelled.take().ok);
        }
        br::BatchStage batch(before,dest,pairs,2);
        REQUIRE(!batch.step());
        for(size_t i=0;i<10;++i) {
            REQUIRE(!batch.step());
            REQUIRE(batch.completed()==i+1&&batch.entries().size()==i+1);
        }
        REQUIRE(batch.step());
        auto out=batch.take();REQUIRE(out.ok&&out.convertedBatch.size()==10);
        REQUIRE(gs->data()==before&&em->data()==dest);
        auto final=sv::loadSaveFile(out.destinationAfter);REQUIRE(final&&sv::validateSave(*final));
        for(int i=0;i<10;++i) REQUIRE(final->readBoxSlot(7,i,read.data())==sv::SlotState::Valid);
        trainer.version=41;
        std::puts("PASS: cooperative ten-Pokemon Gold/Silver to Emerald, cancel and exact stage");
    }
    {
        auto jp=sv::loadSaveFile(fakegb::blankGen1(true,false));
        REQUIRE(jp&&jp->boxSlotCount()==30);
        pk::create4::Trainer t;t.name="アッシュ";t.tid=12345;t.version=35;t.language=1;
        std::vector<br::TransferPair> pairs;
        for(int i=0;i<30;++i) {
            auto mon=pk::create4::newPokemon(t,1,5,255+i);
            mon.nickname="フシギダネ";pk::create4::encode(mon,read.data());
            REQUIRE(jp->writeBoxSlot(0,i,read.data()));pairs.push_back({{0,i},{0,i}});
        }
        jp->refreshChecksums();
        pktool::bank::Bank bank;
        auto empty=bank.serialize();
        auto result=br::stageGbBatch(jp->data(),empty,pairs,1);
        if(!result.ok) std::fprintf(stderr,"Bank batch: %s\n",result.error.c_str());
        REQUIRE(result.ok&&result.convertedBatch.size()==30);
        REQUIRE(br::loadTransferBank(result.destinationAfter,bank)&&bank.count()==30);
        for(int i=0;i<30;++i) REQUIRE(bank.format(0,i)==3);
        auto src=sv::loadSaveFile(result.sourceAfter);
        REQUIRE(src&&sv::validateSave(*src)&&src->readBoxSlot(0,0,read.data())==sv::SlotState::Empty);
        REQUIRE(!br::stageGbBatch(jp->data(),result.destinationAfter,pairs,1).ok);
    }
    for(auto game:{sv::Game::RubySapphire,sv::Game::Emerald,sv::Game::FireRedLeafGreen}) {
        auto dest=fake::blankGen3(game);
        auto batch=br::stageGbBatch(originalS,dest,{{{0,0},{0,3}},{{0,2},{0,8}},{{1,0},{1,0}}});
        if(!batch.ok) std::fprintf(stderr,"Batch failed: %s\n",batch.error.c_str());
        REQUIRE(batch.ok && batch.convertedBatch.size()==3);
        auto src=sv::loadSaveFile(batch.sourceAfter), dst=sv::loadSaveFile(batch.destinationAfter);
        REQUIRE(src&&dst&&sv::validateSave(*src)&&sv::validateSave(*dst));
        REQUIRE(src->readBoxSlot(0,0,read.data())==sv::SlotState::Valid && read[0]==155);
        REQUIRE(src->readBoxSlot(0,1,read.data())==sv::SlotState::Empty);
        REQUIRE(src->readBoxSlot(1,0,read.data())==sv::SlotState::Empty);
        REQUIRE(dst->readBoxSlot(0,3,read.data())==sv::SlotState::Valid && pk::pk3::toPokemon(read.data(),80).species==158);
        REQUIRE(dst->readBoxSlot(0,8,read.data())==sv::SlotState::Valid && pk::pk3::toPokemon(read.data(),80).species==152);
        REQUIRE(dst->readBoxSlot(1,0,read.data())==sv::SlotState::Valid && pk::pk3::toPokemon(read.data(),80).species==158);
        auto rejected=br::stageGbBatch(originalS,dest,{{{0,0},{0,3}},{{0,0},{0,8}}});
        REQUIRE(!rejected.ok && rejected.sourceAfter.empty() && rejected.destinationAfter.empty());
        rejected=br::stageGbBatch(originalS,dest,{{{0,0},{0,3}},{{0,19},{0,8}}});
        REQUIRE(!rejected.ok && rejected.sourceAfter.empty() && rejected.destinationAfter.empty());
        rejected=br::stageGbBatch(originalS,dest,{{{0,0},{0,3}},{{0,2},{0,3}}});
        REQUIRE(!rejected.ok && rejected.sourceAfter.empty() && rejected.destinationAfter.empty());
        for(uint8_t version:{35,38,39}) {
            auto gb=sv::loadSaveFile(version==39?fakegb::blankGen2(fakegb::kGen2Gs):fakegb::blankGen1(false,version==38));
            REQUIRE(gb);
            trainer.version=version;
            const uint16_t species=version==39?155:version==38?25:1;
            auto mon=pk::create4::newPokemon(trainer,species,5,100+species);
            mon.heldItem=0;
            std::array<uint8_t,pk::entity::kMaxPartySize> raw{};
            pk::create4::encode(mon,raw.data());
            REQUIRE(gb->writeBoxSlot(0,0,raw.data()));
            gb->refreshChecksums();
            auto cross=br::stageGbBatch(gb->data(),dest,{{{0,0},{0,0}}});
            if(!cross.ok) {std::fprintf(stderr,"Version %u: %s\n",version,cross.error.c_str());for(auto& n:cross.notes)std::fprintf(stderr,"%s\n",n.c_str());}
            REQUIRE(cross.ok);
            auto out=sv::loadSaveFile(cross.destinationAfter), gone=sv::loadSaveFile(cross.sourceAfter);
            REQUIRE(out&&gone&&gone->readBoxSlot(0,0,read.data())==sv::SlotState::Empty);
            REQUIRE(out->readBoxSlot(0,0,read.data())==sv::SlotState::Valid && pk::pk3::toPokemon(read.data(),80).species==species);
        }
    }
    // Held items are returned to the source Bag atomically; every entry
    // is reviewed even when the middle entry is blocked.
    for(auto setup:{fakegb::kGen2Gs,fakegb::kGen2Crystal,fakegb::kGen2CrystalJpn}) {
        auto bagSave=sv::loadSaveFile(fakegb::blankGen2(setup));REQUIRE(bagSave);
        int pocket=-1;const uint16_t item=0x12; // Potion
        for(int p=0;p<4;++p) if(bagSave->pocketAllows(p,item)) {pocket=p;break;}
        REQUIRE(pocket>=0&&bagSave->setPocket(pocket,{{item,5}}));
        trainer.version=bagSave->gameVersion();trainer.language=setup.japanese?1:2;
        trainer.name=setup.japanese?"アッシュ":"ASH";
        auto mon=pk::create4::newPokemon(trainer,155,5,255);
        if(setup.japanese) mon.nickname="ヒノアラシ";
        std::array<uint8_t,pk::entity::kMaxPartySize> held{};
        pk::create4::encode(mon,held.data());held[1]=item;
        REQUIRE(bagSave->writeBoxSlot(0,0,held.data()));
        REQUIRE(bagSave->writeBoxSlot(0,1,held.data()));
        held[1]=0;REQUIRE(bagSave->writeBoxSlot(0,2,held.data()));
        bagSave->refreshChecksums();
        auto original=bagSave->data();
        auto batch=br::stageGbBatch(original,originalD,{{{0,0},{0,0}},{{0,1},{0,1}},{{0,2},{0,2}}});
        if(!batch.ok) std::fprintf(stderr,"Bag regression: %s\n",batch.error.c_str());
        REQUIRE(batch.ok&&batch.entries.size()==3&&batch.entries[0].ready&&batch.entries[0].returnedItem==item);
        auto after=sv::loadSaveFile(batch.sourceAfter);REQUIRE(after&&sv::validateSave(*after));
        REQUIRE(after->readPocket(pocket).size()==1&&after->readPocket(pocket)[0].count==7);
        REQUIRE(bagSave->data()==original);
        REQUIRE(pk::pk3::toPokemon(batch.converted.data(),80).heldItem==0);
        REQUIRE(bagSave->setPocket(pocket,{{item,static_cast<uint16_t>(bagSave->itemMaxCount(pocket,item)-1)}}));
        bagSave->refreshChecksums();original=bagSave->data();
        auto refused=br::stageGbBatch(original,originalD,{{{0,0},{0,0}},{{0,1},{0,1}},{{0,2},{0,2}}});
        REQUIRE(!refused.ok&&refused.entries.size()==3&&refused.entries[0].ready&&!refused.entries[1].ready&&refused.entries[2].ready);
        REQUIRE(refused.sourceAfter.empty()&&refused.destinationAfter.empty()&&bagSave->data()==original);
        auto subset=br::stageGbBatch(original,originalD,{{{0,0},{0,0}},{{0,2},{0,2}}});
        REQUIRE(subset.ok);
        auto left=sv::loadSaveFile(subset.sourceAfter);
        REQUIRE(left&&left->readBoxSlot(0,0,read.data())==sv::SlotState::Valid&&read[1]==item);
        REQUIRE(left->readPocket(pocket)[0].count==bagSave->itemMaxCount(pocket,item));
    }
    // Reproduce the exact non-terminating upstream condition with actual
    // Gen I/II save layouts and the real LEGAL converter, including Red -> Emerald.
    for(uint8_t generation:{1,2}) {
        for(uint16_t exp:{0,254,255,256,257,65535}) {
            auto gb=sv::loadSaveFile(generation==1?fakegb::blankGen1(false,false):fakegb::blankGen2(fakegb::kGen2Gs));
            auto emerald=sv::loadSaveFile(fake::blankGen3(sv::Game::Emerald));
            REQUIRE(gb&&emerald);
            pk::create4::Trainer t;
            t.name="ASH";t.tid=12345;t.version=gb->gameVersion();t.language=2;
            auto mon=pk::create4::newPokemon(t,generation==1?1:155,5,255);
            mon.heldItem=0;
            std::array<uint8_t,pk::entity::kMaxPartySize> raw{};
            pk::create4::encode(mon,raw.data());
            const auto offset=generation==1?pk::pk1::offset::kStatExp:pk::pk2::offset::kStatExp;
            for(size_t i=0;i<5;++i) {
                raw[offset+2*i]=static_cast<uint8_t>(exp>>8);
                raw[offset+2*i+1]=static_cast<uint8_t>(exp);
            }
            REQUIRE(gb->writeBoxSlot(0,0,raw.data()));gb->refreshChecksums();
            const auto before=gb->data(),targetBefore=emerald->data();
            auto stage=br::stageGbBatch(before,targetBefore,{{{0,0},{0,0}}});
            if(!stage.ok) {
                std::fprintf(stderr,"StatExp %u Gen %u: %s\n",exp,generation,stage.error.c_str());
                for(const auto& n:stage.notes) std::fprintf(stderr,"%s\n",n.c_str());
            }
            REQUIRE(stage.ok&&gb->data()==before&&emerald->data()==targetBefore);
            auto dest=sv::loadSaveFile(stage.destinationAfter),src=sv::loadSaveFile(stage.sourceAfter);
            REQUIRE(dest&&src&&sv::validateSave(*dest)&&sv::validateSave(*src));
            REQUIRE(src->readBoxSlot(0,0,read.data())==sv::SlotState::Empty);
            REQUIRE(dest->readBoxSlot(0,0,read.data())==sv::SlotState::Valid);
            const auto p=pk::pk3::toPokemon(read.data(),80);
            REQUIRE(p.species==mon.species);
            unsigned sum=0;
            for(auto ev:p.evs) {
                sum+=ev;
                if(exp==255||exp==254||exp==256||exp==257) REQUIRE(ev==16);
                if(exp==0) REQUIRE(ev==0);
            }
            REQUIRE(sum<=510);
        }
    }
    std::puts("PASS: Gen I/II Stat Exp 0,254,255,256,257,65535 -> Emerald; exact EV regression");
    // Same western save geometry, language explicitly supplied by user.
    for(uint8_t lang:{2,3,4,5,7}) {
        auto multilingual=br::stageGbBatch(originalS,originalD,{{{0,1},{0,5}}},lang);
        if(!multilingual.ok) {std::fprintf(stderr,"Language %u: %s\n",lang,multilingual.error.c_str());for(auto& n:multilingual.notes)std::fprintf(stderr,"%s\n",n.c_str());}
        REQUIRE(multilingual.ok);
        REQUIRE(pk::pk3::toPokemon(multilingual.converted.data(),80).language==lang);
    }
    for(uint8_t version:{35,41}) {
        auto jp=sv::loadSaveFile(version==35?fakegb::blankGen1(true,false):fakegb::blankGen2(fakegb::kGen2CrystalJpn));
        REQUIRE(jp&&jp->language()==1);
        trainer.version=version;trainer.language=1;trainer.name="アッシュ";
        auto mon=pk::create4::newPokemon(trainer,version==35?1:155,5,255);
        mon.heldItem=0;mon.nickname=version==35?"フシギダネ":"ヒノアラシ";
        std::array<uint8_t,pk::entity::kMaxPartySize> raw{};
        pk::create4::encode(mon,raw.data());
        REQUIRE(jp->writeBoxSlot(0,0,raw.data()));jp->refreshChecksums();
        auto stagedJp=br::stageGbBatch(jp->data(),originalD,{{{0,0},{0,0}}},1);
        if(!stagedJp.ok) {std::fprintf(stderr,"Japanese %u: %s\n",version,stagedJp.error.c_str());for(auto& n:stagedJp.notes)std::fprintf(stderr,"%s\n",n.c_str());}
        REQUIRE(stagedJp.ok);
        auto target=pk::pk3::toPokemon(stagedJp.converted.data(),80);
        REQUIRE(target.language==1&&target.nickname==mon.nickname&&target.trainer.name==trainer.name);
        REQUIRE(!br::stageGbBatch(jp->data(),originalD,{{{0,0},{0,0}}},2).ok);
    }
    REQUIRE(!br::stageGbBatch(originalS,originalD,{{{0,1},{0,5}}},1).ok);
    REQUIRE(!br::stageGbBatch(originalS,originalD,{{{0,1},{0,5}}},8).ok);
    REQUIRE(!br::stageCrystalToRuby(std::vector<uint8_t>{1,2,3},originalD,{0,1},{0,5}).ok);
    // Native Bank source: mixed formats, per-entity languages and exact
    // preservation of other Bank slots, including held items on refusal.
    {
        pktool::bank::Bank bank;
        auto g1=sv::loadSaveFile(fakegb::blankGen1(false,false));
        auto g2=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        REQUIRE(g1&&g2);
        pk::create4::Trainer t;t.name="ASH";t.tid=12345;t.version=g1->gameVersion();t.language=2;
        std::array<uint8_t,pk::entity::kMaxPartySize> native{};
        pk::create4::encode(pk::create4::newPokemon(t,1,5,255),native.data());
        REQUIRE(bank.set(0,0,1,native.data()));
        t.version=g2->gameVersion();t.language=3;t.name="ÉRIC";
        auto mon=pk::create4::newPokemon(t,155,5,255);mon.nickname="ÉCLAIR";mon.heldItem=0;
        pk::create4::encode(mon,native.data());native[1]=0;
        REQUIRE(bank.set(0,1,2,native.data()));
        native[1]=1;REQUIRE(bank.set(0,2,2,native.data()));
        REQUIRE(bank.setBoxName(0,"Classic"));
        const auto initial=bank.serialize();
        auto refused=br::stageGbBatch(initial,originalD,{{{0,2},{0,0}}});
        REQUIRE(!refused.ok&&refused.sourceAfter.empty()&&refused.destinationAfter.empty());
        auto batch=br::stageGbBatch(initial,originalD,{{{0,0},{0,0}},{{0,1},{0,1}}});
        REQUIRE(batch.ok&&batch.convertedBatch.size()==2);
        REQUIRE(pk::pk3::toPokemon(batch.convertedBatch[0].data(),80).language==2);
        REQUIRE(pk::pk3::toPokemon(batch.convertedBatch[1].data(),80).language==3);
        pktool::bank::Bank after;
        REQUIRE(br::loadTransferBank(batch.sourceAfter,after)&&after.count()==1&&after.format(0,2)==2);
        REQUIRE(after.entity(0,2)[1]==1&&after.boxName(0)=="Classic"&&bank.serialize()==initial);
        REQUIRE(!br::stageGbBatch(initial,initial,{{{0,0},{0,4}}}).ok);
    }
    // Optional host-only audit corpus: no ROMs, no real player saves.
    // Run all six source languages, both generations, and three destination
    // save families. Retain conversion refusals in the report rather than
    // silently replacing names or weakening identity checks.
    if(const char* audit=std::getenv("VPT_PK3_AUDIT_DIR")) {
        for(uint8_t lang:{1,2,3,4,5,7}) for(uint8_t gen:{1,2}) {
            auto gb=sv::loadSaveFile(gen==1?fakegb::blankGen1(lang==1,false):
                fakegb::blankGen2(lang==1?fakegb::kGen2CrystalJpn:fakegb::kGen2Gs));
            REQUIRE(gb);
            pk::create4::Trainer t;t.name=lang==1?"アッシュ":lang==3?"ÉRIC":lang==5?"JÖRG":lang==7?"PEÑA":"ASH";
            t.tid=12345;t.version=gb->gameVersion();t.language=lang;
            auto mon=pk::create4::newPokemon(t,gen==1?1:155,5,255);
            mon.heldItem=0;
            if(lang==1) mon.nickname=gen==1?"フシギダネ":"ヒノアラシ";
            else mon.nickname=lang==3?"ÉCLAIR":lang==5?"BÄR":lang==7?"NIÑO":"BUDDY";
            std::array<uint8_t,pk::entity::kMaxPartySize> raw{};
            pk::create4::encode(mon,raw.data());
            REQUIRE(gb->writeBoxSlot(0,0,raw.data()));gb->refreshChecksums();
            int family=0;
            for(auto game:{sv::Game::RubySapphire,sv::Game::Emerald,sv::Game::FireRedLeafGreen}) {
                const auto name=std::string(audit)+"/g"+std::to_string(gen)+"-lang"+std::to_string(lang)+"-dest"+std::to_string(++family);
                auto staged=br::stageGbBatch(gb->data(),fake::blankGen3(game),{{{0,0},{0,0}}},lang);
                if(!staged.ok) {
                    auto* out=std::fopen((name+".refused.txt").c_str(),"w");REQUIRE(out);
                    std::fprintf(out,"%s\n",staged.error.c_str());
                    for(const auto& n:staged.notes) std::fprintf(out,"%s\n",n.c_str());
                    REQUIRE(std::fclose(out)==0);continue;
                }
                auto* out=std::fopen((name+".pk3").c_str(),"wb");REQUIRE(out);
                REQUIRE(std::fwrite(staged.converted.data(),1,80,out)==80);
                REQUIRE(std::fclose(out)==0);
            }
        }
        std::puts("PASS: generated 36 synthetic locale/destination audit cases (including explicit refusals)");
    }
    std::puts("PASS: staged Crystal to Ruby/Sapphire save round-trip; source compaction, exact destination, party and other boxes preserved; rejection cases passed");
    return 0;
}
