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
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Completed);
        REQUIRE(!app.openSave(a));
        REQUIRE(!same(a,sb)&&!same(b,db));
    }
    // Restart application before archiving; ordinary editor must stay locked.
    {
        ui::App app(cfg);
        REQUIRE(std::string(app.screenName())=="TransferRecovery");
        press(app,ui::kKeyB);REQUIRE(std::string(app.screenName())=="TransferRecovery");
        REQUIRE(!app.openSave(b));
        press(app,ui::kKeyX);
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
            press(app,ui::kKeyY);press(app,ui::kKeyA);
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
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            REQUIRE(br::inspectActiveJournal(bj).state==br::StartupState::Completed);
            REQUIRE(!same(ga,bs)&&!same(eb,bd));
        }
        {
            ui::App app(bc);
            REQUIRE(std::string(app.screenName())=="TransferRecovery");
            press(app,ui::kKeyX);
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
    // Verify the startup UI refuses a partially created journal too.
    REQUIRE(put(j+".new",{1,2,3}));
    ui::App locked(cfg);REQUIRE(std::string(locked.screenName())=="TransferRecovery");
    press(locked,ui::kKeyX);REQUIRE(std::string(locked.screenName())=="TransferRecovery");
    REQUIRE(same(a,sa)&&same(b,da));
    std::puts("PASS: end-to-end UI selection, cancel, two-step commit, restart lock, archive, destination and source validation");
    return 0;
}
