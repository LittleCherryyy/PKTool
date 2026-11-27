// SPDX-License-Identifier: GPL-3.0-or-later
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk2.h"
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

std::vector<uint8_t> syntheticArtPack(uint16_t species,uint16_t color) {
    constexpr int side=16;
    const uint16_t count=species;
    const uint32_t header=8+4*(static_cast<uint32_t>(count)+2);
    const uint32_t body=8+side*side*2;
    std::vector<uint8_t> pack={'P','K','S','P',
        static_cast<uint8_t>(count),static_cast<uint8_t>(count>>8),0,0};
    for(uint32_t i=0;i<static_cast<uint32_t>(count)+2;++i) {
        const uint32_t offset=header+(i>species?body:0);
        for(int j=0;j<4;++j) pack.push_back(static_cast<uint8_t>(offset>>(8*j)));
    }
    pack.insert(pack.end(),{'P','K','S','1',side,0,side,0});
    for(int i=0;i<side*side;++i) {
        pack.push_back(static_cast<uint8_t>(color));
        pack.push_back(static_cast<uint8_t>(color>>8));
    }
    return pack;
}
size_t countColor(const std::array<uint16_t,256*192>& buffer,uint16_t color) {
    return static_cast<size_t>(std::count(buffer.begin(),buffer.end(),color));
}

bool pressRaw(ui::App& app,uint32_t key) {
    static std::array<uint16_t,256*192> top{},bottom{};
    bool running=app.update({key,key,false,0,0});
    app.update({});
    for(int i=0;i<10000&&app.transporterBusy();++i) app.update({});
    gfx::Canvas t(top.data()),b(bottom.data());app.draw(t,b);
    return running;
}
// Older flow tests accept the explicitly displayed default. New tests
// below use raw presses to exercise the actual confirmation screen.
bool press(ui::App& app,uint32_t key) {
    const bool running=pressRaw(app,key);
    if(key==ui::kKeyA&&app.transporterChoosingEdition()) pressRaw(app,ui::kKeyA);
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
    // VPT040: initial Home frame must be rendered BEFORE the cooperative SD
    // search; the catalog and bounded snapshots become available without
    // opening any menu or switching screens.
    {
        ui::App app(cfg);
        app.setAnimations(false); // exact RGB555 theme assertions, no fade blending
        REQUIRE(std::string(app.screenName())=="Home");
        std::array<uint16_t,256*192> top{},bottom{};
        gfx::Canvas tc(top.data()),bc(bottom.data());
        app.draw(tc,bc);
        REQUIRE(top[5*256+5]==gfx::rgb(228,63,76));
        for(int i=0;i<10000&&!app.startupPreloadReady();++i)
            REQUIRE(app.update({}));
        REQUIRE(app.startupCatalogReady()&&app.startupPreloadReady());
        REQUIRE(std::string(app.screenName())=="Home");
        REQUIRE(app.startupCachedFiles()>=2);
        REQUIRE(app.startupCachedBytes()<=384u*1024u);
        // Enter Transporter after the preloader finished. The old code
        // displayed an un-themed source selection while the first box
        // enumeration advanced; it must now show only a Unova loading panel.
        REQUIRE(app.update({ui::kKeyY,ui::kKeyY}));
        REQUIRE(std::string(app.screenName())=="Transporter");
        REQUIRE(app.update({ui::kKeyA,ui::kKeyA}));
        REQUIRE(app.update({})); // transfer source loaded, boxes will follow
        REQUIRE(app.transporterBusy());
        app.draw(tc,bc);
        REQUIRE(top[5*256+5]==gfx::rgb(228,63,76));
        REQUIRE(bottom[5*256+5]==gfx::rgb(228,63,76));
        for(int i=0;i<10000&&app.transporterBusy();++i)
            REQUIRE(app.update({}));
        REQUIRE(!app.transporterBusy());
        app.draw(tc,bc);
        REQUIRE(bottom[27*256+5]==gfx::rgb(228,63,76));
        REQUIRE(same(a,sb)&&same(b,db)); // nothing written by preloading
        std::puts("PASS: startup preload on Home, <=384 KiB cache, no old interface on first source open");
    }
    {
        ui::App app(cfg);
        REQUIRE(std::string(app.screenName())=="Home");
        press(app,ui::kKeyY);REQUIRE(std::string(app.screenName())=="Transporter");
        press(app,ui::kKeyA); // Crystal source -> Pokémon picklist
        {
            std::array<uint16_t,256*192> top{},bottom{};
            gfx::Canvas tc(top.data()),bc(bottom.data());
            app.draw(tc,bc);
            // The source Pokémon selector must use the Unova RGB555
            // selection cursor, not the old green-on-white GB screen.
            REQUIRE(bottom[27*256+5]==gfx::rgb(228,63,76));
            REQUIRE(top[40*256+5]==gfx::rgb(52,164,176));
        }
        press(app,ui::kKeyA); // Pokémon -> destination list
        press(app,ui::kKeyA); // destination save -> GBA slot grid
        {
            std::array<uint16_t,256*192> top{},bottom{};
            gfx::Canvas tc(top.data()),bc(bottom.data());
            app.draw(tc,bc);
            REQUIRE(bottom[13*256+8]==gfx::rgb(228,63,76));
            press(app,ui::kKeyRight); // horizontal moves one slot
            app.draw(tc,bc);
            REQUIRE(bottom[13*256+48]==gfx::rgb(228,63,76));
            press(app,ui::kKeyLeft); // return to first slot
        }
        press(app,ui::kKeyA); // accept first empty slot
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
    // VPT038: explicit repair is inspectable, cancellable and only moves
    // after a second PCCS review and the existing double-START commit.
    {
        const auto dir=root+"/repair-ui";
        REQUIRE(io::makeDirectories(dir));
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
        auto gba=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
        REQUIRE(gs&&gba);
        pk::create4::Trainer trainer;trainer.name="ASH";trainer.tid=12345;
        trainer.version=41;trainer.language=2;
        auto mon=pk::create4::newPokemon(trainer,155,5,255);mon.heldItem=0;
        pk::create4::encode(mon,raw.data());
        raw[pk::pk2::offset::kExp]=0;
        raw[pk::pk2::offset::kExp+1]=0;
        raw[pk::pk2::offset::kExp+2]=1; // Level / EXP mismatch
        REQUIRE(gs->writeBoxSlot(0,0,raw.data()));
        gs->refreshChecksums();gba->refreshChecksums();
        const auto before=gs->data(),dest=gba->data();
        const auto source=dir+"/crystal.sav",target=dir+"/ruby.sav";
        REQUIRE(put(source,before)&&put(target,dest));
        ui::Config repairConfig=cfg;
        repairConfig.scanRoot=dir;repairConfig.dataDir=dir+"/PKTOOL";
        const auto repairJournal=repairConfig.dataDir+"/transporter/active.vpt";
        ui::App app(repairConfig);
        press(app,ui::kKeyY);
        for(int i=0;i<4;++i) press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Transporter");
        REQUIRE(!app.transporterRepairConfirm());
        press(app,ui::kKeyY);
        REQUIRE(app.transporterRepairConfirm());
        REQUIRE(same(source,before)&&same(target,dest)&&!io::fileExists(repairJournal));
        press(app,ui::kKeyB);
        REQUIRE(!app.transporterRepairConfirm());
        REQUIRE(same(source,before)&&same(target,dest)&&!io::fileExists(repairJournal));
        press(app,ui::kKeyY);
        REQUIRE(app.transporterRepairConfirm());
        press(app,ui::kKeyA); // approve conversion-only repair + re-review
        REQUIRE(!app.transporterRepairConfirm());
        REQUIRE(same(source,before)&&same(target,dest)&&!io::fileExists(repairJournal));
        press(app,ui::kKeyStart);press(app,ui::kKeyStart);
        REQUIRE(same(source,before)&&same(target,dest)&&!io::fileExists(repairJournal));
        press(app,ui::kKeyA); // skip animation, execute the existing journal
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(br::inspectActiveJournal(repairJournal).state==br::StartupState::Completed);
        REQUIRE(!same(source,before)&&!same(target,dest));
        REQUIRE(press(app,ui::kKeyA)); // archive and return home
        REQUIRE(br::inspectActiveJournal(repairJournal).state==br::StartupState::Clear);
        auto src=sv::loadSaveFile(before);
        REQUIRE(src && src->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid);
        // The source's ORIGINAL broken entity is only retained in the backup,
        // never rewritten with the repair before or after the transfer.
        REQUIRE(raw[pk::pk2::offset::kExp+2]==1);
        std::puts("PASS: VPT038 repair preview cancellation, explicit approval, re-review, journal and archive");
    }
    // Restart application before archiving; ordinary editor must stay locked.
    {
        ui::App app(cfg);
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(!app.openSave(b));
        // Completed transfer stays on the final screen until A (or touch).
        // In particular START/B/X must not archive the journal by accident.
        for(uint32_t key : {ui::kKeyStart,ui::kKeyB,ui::kKeyX}) {
            REQUIRE(press(app,key));
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Completed);
        }
        REQUIRE(press(app,ui::kKeyA)); // one Continue action archives and returns home
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
            press(app,0); // render the Unova Home tiles
            press(app,ui::kKeyDown); // saves -> Bank
            press(app,ui::kKeyDown); // Bank -> featured Transporter
            press(app,ui::kKeyA);
            REQUIRE(std::string(app.screenName())=="Transporter");
            press(app,ui::kKeyA);
            press(app,ui::kKeyX); // first source
            press(app,ui::kKeyDown);press(app,ui::kKeyDown);
            press(app,ui::kKeyX); // third source
            press(app,ui::kKeyA); // selected entries -> destination
            press(app,ui::kKeyA); // Emerald
            press(app,ui::kKeyRight);press(app,ui::kKeyA); // grid: slot 1, right from slot 0
            REQUIRE(same(ga,bs)&&same(eb,bd)&&!io::fileExists(bj));
            press(app,ui::kKeyStart); // no writes until second START
            REQUIRE(same(ga,bs)&&same(eb,bd)&&!io::fileExists(bj));
            press(app,ui::kKeyStart);
            REQUIRE(!io::fileExists(bj)&&same(ga,bs)&&same(eb,bd));
            // Two Pokemon now have 480 frames of animation plus 60 final
            // frames. Verify the sequence stays read-only well beyond the old
            // 90-frame timeout, then naturally commits without a skip.
            for(int f=0;f<300;++f) app.update({});
            REQUIRE(std::string(app.screenName())=="Transporter");
            REQUIRE(!io::fileExists(bj)&&same(ga,bs)&&same(eb,bd));
            for(int f=0;f<260;++f) app.update({});
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
        press(app,ui::kKeyY); // history, not rescan
        press(app,ui::kKeyB); // back to the source list
        REQUIRE(app.transporterRows().size()==2);
        press(app,ui::kKeyB);
        io::resetReadStats();press(app,ui::kKeyY);
        REQUIRE(app.transporterRows().size()==2&&io::readStats().opens==0);
        ui::App fresh(rc);press(fresh,ui::kKeyY);
        REQUIRE(fresh.transporterRows().size()==1);
    }
    // The two entry points share one completed discovery, including an empty catalog.
    {
        const auto sessionRoot=root+"/session";
        REQUIRE(io::makeDirectories(sessionRoot));
        REQUIRE(put(sessionRoot+"/crystal.sav",sb)&&put(sessionRoot+"/ruby.sav",db));
        ui::Config sc=cfg;sc.scanRoot=sessionRoot;sc.dataDir=sessionRoot+"/PKTOOL";
        ui::App app(sc);press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Browser");
        press(app,ui::kKeyB);
        io::resetReadStats();press(app,ui::kKeyY);
        REQUIRE(app.transporterRows().size()==1&&io::readStats().opens==0);
        press(app,ui::kKeyB);
        io::resetReadStats();press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Browser"&&io::readStats().opens==0);
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
    // Whole-box selection and existing Bank destination through the UI.
    {
        const auto bankRoot=root+"/transport-bank";
        REQUIRE(io::makeDirectories(bankRoot));
        const auto source=bankRoot+"/crystal.sav";
        auto gb=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
        REQUIRE(gb);
        tr.version=41;tr.language=2;tr.name="ASH";
        for(int i=0;i<20;++i) {
            pk::create4::encode(pk::create4::newPokemon(tr,155,5,255+i),raw.data());raw[1]=0;
            REQUIRE(gb->writeBoxSlot(0,i,raw.data()));
        }
        gb->refreshChecksums();
        ui::Config bc=cfg;bc.scanRoot=bankRoot;bc.dataDir=bankRoot+"/PKTOOL";
        const auto dir=bc.dataDir+"/bank",bp=dir+"/bank.pkb",journal=bc.dataDir+"/transporter/active.vpt";
        pktool::bank::Bank target;
        REQUIRE(io::makeDirectories(dir)&&put(bp,target.serialize())&&put(source,gb->data()));
        ui::App app(bc);press(app,ui::kKeyY);press(app,ui::kKeyA);
        REQUIRE(app.transporterRows().size()==20);
        press(app,ui::kKeyY); // mark entire source box
        press(app,ui::kKeyA); // destination picker, no GBA files
        press(app,ui::kKeySelect); // existing Bank
        REQUIRE(app.transporterRows().size()==581);
        press(app,ui::kKeyA);
        press(app,ui::kKeyStart);press(app,ui::kKeyStart);
        REQUIRE(!io::fileExists(journal)&&same(source,gb->data()));
        press(app,ui::kKeyA); // skip animation
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        press(app,ui::kKeyR); // page through the result
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        press(app,ui::kKeyA);
        REQUIRE(std::string(app.screenName())=="Home");
        REQUIRE(target.load(bp)==pktool::bank::Bank::LoadResult::Ok&&target.count()==20);
        std::vector<uint8_t> srcBytes;
        REQUIRE(io::readFile(source,srcBytes));
        auto gone=sv::loadSaveFile(srcBytes);
        REQUIRE(gone&&sv::validateSave(*gone)&&gone->readBoxSlot(0,0,raw.data())==sv::SlotState::Empty);
        press(app,ui::kKeyY);press(app,ui::kKeyY); // source list, history
        REQUIRE(std::string(app.screenName())=="Transporter");
        press(app,ui::kKeyR);press(app,ui::kKeyB);
        REQUIRE(app.transporterRows().size()==1);
    }
    // Native Bank source is selectable without any GB/GBC save on disk.
    {
        const auto root2=root+"/native-bank";
        REQUIRE(io::makeDirectories(root2));
        ui::Config bc=cfg;bc.scanRoot=root2;bc.dataDir=root2+"/PKTOOL";
        const auto dir=bc.dataDir+"/bank",bp=dir+"/bank.pkb",journal=bc.dataDir+"/transporter/active.vpt";
        const auto gba=root2+"/ruby.sav";
        pktool::bank::Bank target;
        tr.version=41;tr.language=2;tr.name="ASH";
        pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),raw.data());raw[1]=0;
        REQUIRE(target.set(0,0,2,raw.data()));
        const auto before=target.serialize();
        REQUIRE(io::makeDirectories(dir)&&put(bp,before)&&put(gba,db));
        ui::App app(bc);press(app,ui::kKeyY);
        REQUIRE(app.transporterRows().empty());
        press(app,ui::kKeySelect);REQUIRE(app.transporterRows().size()==1);
        press(app,ui::kKeyA);REQUIRE(app.transporterRows().size()==1);
        press(app,ui::kKeyA);REQUIRE(app.transporterRows().size()==420);
        press(app,ui::kKeyA);press(app,ui::kKeyStart);press(app,ui::kKeyStart);
        REQUIRE(!io::fileExists(journal)&&same(bp,before)&&same(gba,db));
        press(app,ui::kKeyA);REQUIRE(std::string(app.screenName())=="TransferRecovery");
        press(app,ui::kKeyA);REQUIRE(std::string(app.screenName())=="Home");
        REQUIRE(target.load(bp)==pktool::bank::Bank::LoadResult::Ok&&target.count()==0);
        std::vector<uint8_t> output;REQUIRE(io::readFile(gba,output));
        auto dest=sv::loadSaveFile(output);
        REQUIRE(dest&&sv::validateSave(*dest)&&dest->readBoxSlot(0,0,raw.data())==sv::SlotState::Valid);
        REQUIRE(pk::pk3::toPokemon(raw.data(),80).species==155);
    }
    // Ambiguous save families must not guess from contained Pokemon.
    // Confirm Silver and Sapphire once, then reopen without an edition prompt.
    {
        namespace vis=pktool::transport_visual;
        const auto r=root+"/edition";
        REQUIRE(io::makeDirectories(r));
        auto gs=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Gs));
        REQUIRE(gs);
        tr.version=39;tr.language=2;
        pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),raw.data());raw[1]=0;
        REQUIRE(gs->writeBoxSlot(0,0,raw.data()));gs->refreshChecksums();
        REQUIRE(put(r+"/source.sav",gs->data())&&put(r+"/target.sav",db));
        ui::Config ec=cfg;ec.scanRoot=r;ec.dataDir=r+"/PKTOOL";
        ui::App app(ec);pressRaw(app,ui::kKeyY);pressRaw(app,ui::kKeyA);
        REQUIRE(app.transporterChoosingEdition()&&app.transporterRows().size()==2);
        pressRaw(app,ui::kKeyDown);pressRaw(app,ui::kKeyA);
        REQUIRE(app.transporterVisual(true).edition==vis::Edition::Silver);
        pressRaw(app,ui::kKeyA);pressRaw(app,ui::kKeyA);
        REQUIRE(app.transporterChoosingEdition());
        pressRaw(app,ui::kKeyDown);pressRaw(app,ui::kKeyA);
        REQUIRE(app.transporterVisual(false).edition==vis::Edition::Sapphire);
        REQUIRE(app.transporterRows().size()==420);
        pressRaw(app,ui::kKeyB);pressRaw(app,ui::kKeyA);
        REQUIRE(!app.transporterChoosingEdition()&&app.transporterVisual(false).edition==vis::Edition::Sapphire);
        pressRaw(app,ui::kKeyB);pressRaw(app,ui::kKeyB);pressRaw(app,ui::kKeyA);
        REQUIRE(!app.transporterChoosingEdition()&&app.transporterVisual(true).edition==vis::Edition::Silver);
        REQUIRE(same(r+"/source.sav",gs->data())&&same(r+"/target.sav",db));
        ui::App fresh(ec);pressRaw(fresh,ui::kKeyY);pressRaw(fresh,ui::kKeyA);
        REQUIRE(fresh.transporterChoosingEdition());
        pressRaw(fresh,ui::kKeyB);REQUIRE(!fresh.transporterChoosingEdition()&&fresh.transporterRows().size()==1);
    }
    // VPT034 end-to-end visual regression. The art packs use different solid
    // synthetic colors, so a pixel readback proves that the *actual* edition
    // sprite changes at the centre instead of a Pokeball proxy. Every frame
    // before the final commit must leave both files byte-for-byte unchanged.
    {
        const auto visualRoot=root+"/visual";
        const auto art=visualRoot+"/art";
        REQUIRE(io::makeDirectories(visualRoot));
        REQUIRE(io::makeDirectories(art+"/icons"));
        const auto source=visualRoot+"/crystal.sav",destination=visualRoot+"/ruby.sav";
        REQUIRE(put(source,sb)&&put(destination,db));
        REQUIRE(put(art+"/icons/25.bin",{'P','K','S','1',1,0,1,0,1,0x80}));
        constexpr uint16_t species=155;
        const uint16_t srcColor=gfx::rgb(255,0,0);
        const uint16_t dstColor=gfx::rgb(0,0,255);
        REQUIRE(put(art+"/transport-crystal.pak",syntheticArtPack(species,srcColor)));
        REQUIRE(put(art+"/transport-rs.pak",syntheticArtPack(species,dstColor)));
        ui::Config vc=cfg;vc.scanRoot=visualRoot;vc.dataDir=visualRoot+"/PKTOOL";
        vc.spriteDirs={art};
        const auto journal=vc.dataDir+"/transporter/active.vpt";
        ui::App app(vc);
        press(app,ui::kKeyY);
        for(int i=0;i<4;++i) press(app,ui::kKeyA);
        press(app,ui::kKeyStart);press(app,ui::kKeyStart);
        REQUIRE(std::string(app.screenName())=="Transporter");
        REQUIRE(!io::fileExists(journal)&&same(source,sb)&&same(destination,db));
        std::array<uint16_t,256*192> top{},bottom{};
        gfx::Canvas tc(top.data()),bc(bottom.data());
        app.draw(tc,bc);
        REQUIRE(countColor(top,srcColor)>=100);
        REQUIRE(countColor(top,dstColor)==0);
        // Draw the second cable half, past the 135-frame art transition.
        for(int i=0;i<175;++i) app.update({});
        app.draw(tc,bc);
        REQUIRE(countColor(top,dstColor)>=100);
        REQUIRE(countColor(top,srcColor)==0);
        REQUIRE(!io::fileExists(journal)&&same(source,sb)&&same(destination,db));
        press(app,ui::kKeyA);  // Skip remaining animation; commit exact review.
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(br::inspectActiveJournal(journal).state==br::StartupState::Completed);
        press(app,ui::kKeyA);  // Archive safely; the next test has no open journal.
        REQUIRE(br::inspectActiveJournal(journal).state==br::StartupState::Clear);
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
