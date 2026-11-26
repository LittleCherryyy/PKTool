// SPDX-License-Identifier: GPL-3.0-or-later
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/saves/save_commit.h"
#include "core/io/file_io.h"
#include "tests/fake_gb_saves.h"
#include "tests/fake_saves.h"
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace pk=pktool::pokemon;
namespace sv=pktool::saves;
namespace ui=pktool::ui;
namespace gfx=pktool::gfx;
namespace io=pktool::io;
namespace br=pk::pccs_bridge;
namespace fs=std::filesystem;
void pktool::test::reportFailure(const char* f,int l,const char* e) {
    std::fprintf(stderr,"%s:%d: %s\n",f,l,e);std::exit(1);
}
#define REQUIRE(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
bool put(const std::string& p,const std::vector<uint8_t>& b){return io::safeWriteFile(p,b)==io::WriteResult::Ok;}
bool same(const std::string& p,const std::vector<uint8_t>& b){std::vector<uint8_t> a;return io::readFile(p,a)&&a==b;}
bool press(ui::App& app,uint32_t key) {
    static std::array<uint16_t,256*192> top{},bottom{};
    bool running=app.update({key,key,false,0,0});
    app.update({});
    for(int i=0;i<10000&&app.transporterBusy();++i) app.update({});
    gfx::Canvas t(top.data()),b(bottom.data());app.draw(t,b);
    return running;
}
int main() {
    auto s=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
    auto d=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
    REQUIRE(s&&d);
    pk::create4::Trainer tr;tr.name="ASH";tr.tid=12345;tr.version=41;tr.language=2;
    std::array<uint8_t,pk::entity::kMaxPartySize> raw{};
    pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),raw.data());raw[1]=0;
    REQUIRE(s->writeBoxSlot(0,0,raw.data()));
    s->refreshChecksums();d->refreshChecksums();
    auto sb=s->data(),db=d->data();
    char tmp[]="/tmp/vpt007-ui-XXXXXX";REQUIRE(mkdtemp(tmp)!=nullptr);
    struct Cleanup{const char* p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{tmp};
    std::string root=tmp,a=root+"/crystal.sav",b=root+"/ruby.sav";
    ui::Config cfg;cfg.scanRoot=root;cfg.dataDir=root+"/PKTOOL";cfg.transporterPreview=true;
    const auto j=cfg.dataDir+"/transporter/active.vpt";
    REQUIRE(put(a,sb)&&put(b,db));
    {
        ui::App app(cfg);
        REQUIRE(std::string(app.screenName())=="Home");
        press(app,ui::kKeyY);REQUIRE(std::string(app.screenName())=="Transporter");
        for(int i=0;i<4;++i) press(app,ui::kKeyA); // source, mon, dest, empty slot
        REQUIRE(same(a,sb)&&same(b,db));
        press(app,ui::kKeyStart); // arm confirmation
        REQUIRE(!io::fileExists(j)&&same(a,sb)&&same(b,db));
        press(app,ui::kKeyB); // cancel without writing
        REQUIRE(std::string(app.screenName())=="Home"&&!io::fileExists(j));
        press(app,ui::kKeyY);
        for(int i=0;i<4;++i) press(app,ui::kKeyA);
        press(app,ui::kKeyStart);
        press(app,ui::kKeyStart);
        REQUIRE(!io::fileExists(j)&&same(a,sb)&&same(b,db));
        press(app,ui::kKeyA); // skip visual link animation, commit next frame
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Completed);
        REQUIRE(!app.openSave(a));
        REQUIRE(!same(a,sb)&&!same(b,db));
    }
    // Restart application before archiving; ordinary editor must stay locked.
    {
        ui::App app(cfg);
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(!app.openSave(b));
        REQUIRE(press(app,ui::kKeyStart)); // completed: never exit the homebrew
        REQUIRE(std::string(app.screenName())=="Home");
        REQUIRE(std::string(app.screenName())=="Home");
        REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Clear);
    }
    std::vector<uint8_t> sa,da;REQUIRE(io::readFile(a,sa)&&io::readFile(b,da));
    auto sr=sv::loadSaveFile(sa),dr=sv::loadSaveFile(da);
    REQUIRE(sr&&dr&&sv::validateSave(*sr)&&sv::validateSave(*dr));
    REQUIRE(sr->readBoxSlot(0,0,raw.data())==sv::SlotState::Empty);
    REQUIRE(dr->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid);
    REQUIRE(pk::pk3::toPokemon(raw.data(),80).species==155);
    REQUIRE(!io::fileExists(a+".vpt-old")&&!io::fileExists(b+".vpt-old"));
    // Batch end-to-end UI on a separate pair of synthetic save files.
    {
        const auto batchRoot=root+"/batch";
        REQUIRE(io::makeDirectories(batchRoot));
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        auto em=sv::loadSaveFile(fake::blankGen3(sv::Game::Emerald));
        REQUIRE(gs&&em);
        tr.version=39;
        const uint16_t species[]={155,158,152};
        for(int i=0;i<3;++i) {
            auto mon=pk::create4::newPokemon(tr,species[i],5,100+species[i]);
            mon.heldItem=0;
            pk::create4::encode(mon,raw.data());
            REQUIRE(gs->writeBoxSlot(0,i,raw.data()));
        }
        gs->refreshChecksums();em->refreshChecksums();
        const auto bs=gs->data(),bd=em->data();
        const auto ga=batchRoot+"/gold.sav",eb=batchRoot+"/emerald.sav";
        ui::Config bc=cfg;bc.scanRoot=batchRoot;bc.dataDir=batchRoot+"/PKTOOL";
        const auto bj=bc.dataDir+"/transporter/active.vpt";
        REQUIRE(put(ga,bs)&&put(eb,bd));
        {
            ui::App app(bc);
            press(app,0); // render the central Home tile
            press(app,ui::kKeyRight);press(app,ui::kKeyA);
            REQUIRE(std::string(app.screenName())=="Transporter");
            press(app,ui::kKeyA);
            press(app,ui::kKeyX); // first source
            press(app,ui::kKeyDown);press(app,ui::kKeyDown);
            press(app,ui::kKeyX); // third source
            press(app,ui::kKeyA); // selected entries -> destination
            press(app,ui::kKeyA); // Emerald
            press(app,ui::kKeyDown);press(app,ui::kKeyA); // first destination slot 1
            REQUIRE(same(ga,bs)&&same(eb,bd)&&!io::fileExists(bj));
            press(app,ui::kKeyStart); // no writes until second START
            REQUIRE(same(ga,bs)&&same(eb,bd)&&!io::fileExists(bj));
            press(app,ui::kKeyStart);
            REQUIRE(!io::fileExists(bj)&&same(ga,bs)&&same(eb,bd));
            press(app,ui::kKeyA);
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            REQUIRE(br::inspectActiveJournal(bj).state==br::StartupState::Completed);
            REQUIRE(!same(ga,bs)&&!same(eb,bd));
        }
        {
            ui::App app(bc);
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            REQUIRE(press(app,ui::kKeyA)); // finish & return
            REQUIRE(std::string(app.screenName())=="Home");
            REQUIRE(br::inspectActiveJournal(bj).state==br::StartupState::Clear);
        }
        std::vector<uint8_t> ss,dd;
        REQUIRE(io::readFile(ga,ss)&&io::readFile(eb,dd));
        auto src=sv::loadSaveFile(ss),dst=sv::loadSaveFile(dd);
        REQUIRE(src&&dst&&sv::validateSave(*src)&&sv::validateSave(*dst));
        REQUIRE(src->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid && raw[0]==158);
        REQUIRE(src->readBoxSlot(0,1,raw.data())==sv::SlotState::Empty);
        REQUIRE(dst->readBoxSlot(0,0,raw.data())==sv::SlotState::Empty);
        REQUIRE(dst->readBoxSlot(0,1,raw.data())==sv::SlotState::Valid && pk::pk3::toPokemon(raw.data(),80).species==155);
        REQUIRE(dst->readBoxSlot(0,2,raw.data())==sv::SlotState::Valid && pk::pk3::toPokemon(raw.data(),80).species==152);
        REQUIRE(!io::fileExists(ga+".vpt-old")&&!io::fileExists(eb+".vpt-old"));
        std::puts("PASS: Gold to Emerald batch UI, exact order, source compaction, commit and restart/archive");
    }
    // VPT021: mixed review, explicit X ready-only, exact source Bag commit.
    {
        const auto mixedRoot=root+"/mixed";
        REQUIRE(io::makeDirectories(mixedRoot));
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        auto rb=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
        REQUIRE(gs&&rb);
        const uint16_t item=0x12;
        int pocket=-1;
        for(int p=0;p<4;++p) if(gs->pocketAllows(p,item)) {pocket=p;break;}
        REQUIRE(pocket>=0);
        const auto limit=static_cast<uint16_t>(gs->itemMaxCount(pocket,item));
        REQUIRE(gs->setPocket(pocket,{{item,static_cast<uint16_t>(limit-1)}}));
        tr.version=39;
        const uint16_t species[]={155,158,152};
        for(int i=0;i<3;++i) {
            auto mon=pk::create4::newPokemon(tr,species[i],5,100+species[i]);
            pk::create4::encode(mon,raw.data());raw[1]=i<2?item:0;
            REQUIRE(gs->writeBoxSlot(0,i,raw.data()));
        }
        gs->refreshChecksums();rb->refreshChecksums();
        const auto ms=gs->data(),md=rb->data();
        const auto ga=mixedRoot+"/silver.sav",eb=mixedRoot+"/ruby.sav";
        ui::Config mc=cfg;mc.scanRoot=mixedRoot;mc.dataDir=mixedRoot+"/PKTOOL";
        const auto mj=mc.dataDir+"/transporter/active.vpt";
        REQUIRE(put(ga,ms)&&put(eb,md));
        {
            ui::App app(mc);
            press(app,ui::kKeyY);press(app,ui::kKeyA);
            press(app,ui::kKeyX);press(app,ui::kKeyDown);press(app,ui::kKeyX);
            press(app,ui::kKeyDown);press(app,ui::kKeyX);
            press(app,ui::kKeyA);press(app,ui::kKeyA);press(app,ui::kKeyA);
            // Mixed results must never authorize the original batch.
            press(app,ui::kKeyStart);press(app,ui::kKeyStart);press(app,ui::kKeyA);
            REQUIRE(same(ga,ms)&&same(eb,md)&&!io::fileExists(mj));
            press(app,ui::kKeyX); // explicitly re-review the ready subset
            REQUIRE(same(ga,ms)&&same(eb,md)&&!io::fileExists(mj));
            press(app,ui::kKeyStart);press(app,ui::kKeyStart);
            REQUIRE(same(ga,ms)&&same(eb,md)&&!io::fileExists(mj));
            press(app,ui::kKeyA);
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            REQUIRE(br::inspectActiveJournal(mj).state==br::StartupState::Completed);
            REQUIRE(press(app,ui::kKeyA));
            REQUIRE(std::string(app.screenName())=="Home");
        }
        std::vector<uint8_t> ss,dd;
        REQUIRE(io::readFile(ga,ss)&&io::readFile(eb,dd));
        auto src=sv::loadSaveFile(ss),dst=sv::loadSaveFile(dd);
        REQUIRE(src&&dst&&sv::validateSave(*src)&&sv::validateSave(*dst));
        REQUIRE(src->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid && raw[0]==158&&raw[1]==item);
        REQUIRE(src->readBoxSlot(0,1,raw.data())==sv::SlotState::Empty);
        REQUIRE(src->readPocket(pocket).size()==1&&src->readPocket(pocket)[0].count==limit);
        REQUIRE(dst->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid&&pk::pk3::toPokemon(raw.data(),80).species==155);
        REQUIRE(pk::pk3::toPokemon(raw.data(),80).heldItem==0);
        REQUIRE(dst->readBoxSlot(0,1,raw.data())==sv::SlotState::Empty);
        REQUIRE(dst->readBoxSlot(0,2,raw.data())==sv::SlotState::Valid&&pk::pk3::toPokemon(raw.data(),80).species==152);
        std::puts("PASS: Silver to Ruby mixed review, explicit X subset, item returned to Bag, blocked Pokemon retained");
    }
    // A stale or empty source never drops the user out of the transporter.
    {
        const auto rejectRoot=root+"/reject";
        REQUIRE(io::makeDirectories(rejectRoot));
        const auto empty=rejectRoot+"/a-empty.sav", good=rejectRoot+"/b-crystal.sav";
        auto blank=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));REQUIRE(blank);
        REQUIRE(put(empty,blank->data())&&put(good,sb));
        ui::Config rc=cfg;rc.scanRoot=rejectRoot;rc.dataDir=rejectRoot+"/PKTOOL";
        ui::App app(rc);
        press(app,ui::kKeyY);
        REQUIRE(app.transporterRows().size()==2);
        press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Transporter"&&app.transporterRows().size()==2);
        press(app,ui::kKeyDown);press(app,ui::kKeyA);
        REQUIRE(app.transporterRows().size()==1);
        press(app,ui::kKeyB);
        REQUIRE(std::string(app.screenName())=="Transporter"&&app.transporterRows().size()==2);
        REQUIRE(put(good,{1,2,3}));
        press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Transporter"&&app.transporterRows().size()==2);
        press(app,ui::kKeyY);
        REQUIRE(app.transporterRows().size()==1);
    }
    // Capacity is displayed before opening a GBA save and an insufficient
    // save cannot advance; exactly two free slots fit a two-Pokemon batch.
    {
        const auto capRoot=root+"/capacity";
        REQUIRE(io::makeDirectories(capRoot));
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        auto rb=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
        REQUIRE(gs&&rb);
        tr.version=39;
        for(int i=0;i<2;++i) {
            pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),raw.data());raw[1]=0;
            REQUIRE(gs->writeBoxSlot(0,i,raw.data()));
        }
        gs->refreshChecksums();
        tr.version=2;
        pk::create4::encode(pk::create4::newPokemon(tr,25,5,255),raw.data());
        for(int i=0;i<418;++i) REQUIRE(rb->writeBoxSlot(i/30,i%30,raw.data()));
        rb->refreshChecksums();
        const auto source=capRoot+"/gold.sav", fit=capRoot+"/b-fit.sav", low=capRoot+"/a-low.sav";
        REQUIRE(put(source,gs->data())&&put(fit,rb->data()));
        REQUIRE(rb->writeBoxSlot(13,28,raw.data()));rb->refreshChecksums();
        REQUIRE(put(low,rb->data()));
        ui::Config cc=cfg;cc.scanRoot=capRoot;cc.dataDir=capRoot+"/PKTOOL";
        ui::App app(cc);
        press(app,ui::kKeyY);press(app,ui::kKeyA);
        press(app,ui::kKeyX);press(app,ui::kKeyDown);press(app,ui::kKeyX);press(app,ui::kKeyA);
        REQUIRE(app.transporterRows().size()==2);
        REQUIRE(app.transporterRows()[0].find("NO SPACE | 1 free")==0);
        REQUIRE(app.transporterRows()[1].find("READY | 2 free")==0);
        io::resetReadStats();press(app,ui::kKeyA);
        REQUIRE(app.transporterRows().size()==2&&io::readStats().opens==0);
        press(app,ui::kKeyDown);press(app,ui::kKeyA);
        REQUIRE(app.transporterRows().size()==1); // only first starting slot fits
        press(app,ui::kKeyB);
        REQUIRE(app.transporterRows().size()==2);
        REQUIRE(app.transporterRows()[1].find("READY | 2 free")==0);
        REQUIRE(!io::fileExists(cc.dataDir+"/transporter/active.vpt"));
    }
    // Verify the startup UI refuses a partially created journal too.
    REQUIRE(put(j+".new",{1,2,3}));
    ui::App locked(cfg);REQUIRE(std::string(locked.screenName())=="TransferRecovery");
    press(locked,ui::kKeyX);REQUIRE(std::string(locked.screenName())=="TransferRecovery");
    press(locked,ui::kKeyB);REQUIRE(std::string(locked.screenName())=="TransferRecovery");
    REQUIRE(same(a,sa)&&same(b,da));
    std::puts("PASS: end-to-end UI selection, cancel, two-step commit, restart lock, archive, destination and source validation");
    return 0;
}
