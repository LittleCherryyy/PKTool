// SPDX-License-Identifier: GPL-3.0-or-later
#include "transfer_journal.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/saves/save_commit.h"
#include "core/io/file_io.h"
#include "tests/fake_gb_saves.h"
#include "tests/fake_saves.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace pk=pktool::pokemon;
namespace sv=pktool::saves;
namespace br=pk::pccs_bridge;
namespace io=pktool::io;
namespace fs=std::filesystem;
void pktool::test::reportFailure(const char* f,int l,const char* e) {
    std::fprintf(stderr,"%s:%d: %s\n",f,l,e); std::exit(1);
}
#define REQUIRE(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
bool writeFixture(const std::string& path,const std::vector<uint8_t>& bytes) {
    return io::safeWriteFile(path,bytes)==io::WriteResult::Ok;
}
bool exact(const std::string& p,const std::vector<uint8_t>& b) {
    std::vector<uint8_t> a;return io::readFile(p,a)&&a==b;
}
int main() {
    auto s=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
    auto d=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
    REQUIRE(s&&d);
    pk::create4::Trainer tr;tr.name="ASH";tr.tid=12345;tr.version=41;tr.language=2;
    std::array<uint8_t,pk::entity::kMaxPartySize> e{};
    pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),e.data());e[1]=0;
    REQUIRE(s->writeBoxSlot(0,0,e.data()));
    s->refreshChecksums();d->refreshChecksums();
    const auto sb=s->data(),db=d->data();
    char root[]="/tmp/vpt005-XXXXXX";REQUIRE(mkdtemp(root)!=nullptr);
    struct Cleanup { const char* p; ~Cleanup(){std::error_code ec;fs::remove_all(p,ec);} } cleanup{root};
    int n=0;
    for(auto stop:{br::StopPoint::None,br::StopPoint::DestinationMoved,
                   br::StopPoint::DestinationWritten,br::StopPoint::SourceMoved}) {
        std::string folder=std::string(root)+"/"+std::to_string(n++);
        REQUIRE(fs::create_directory(folder));
        auto a=folder+"/crystal.sav",b=folder+"/ruby.sav",j=folder+"/transfer.vpt";
        REQUIRE(writeFixture(a,sb)&&writeFixture(b,db));
        auto prep=br::prepareJournal(j,a,b,{0,0},{0,0});
        if(prep.status!=br::JournalStatus::Complete) std::fprintf(stderr,"%s\n",prep.message.c_str());
        REQUIRE(prep.status==br::JournalStatus::Complete);
        REQUIRE(exact(a,sb)&&exact(b,db));
        REQUIRE(br::prepareJournal(j,a,b,{0,0},{0,0}).status==br::JournalStatus::Conflict);
        auto first=br::resumeJournal(j,a,b,stop);
        if(stop==br::StopPoint::None) REQUIRE(first.status==br::JournalStatus::Complete);
        else {
            REQUIRE(first.status==br::JournalStatus::Interrupted);
            if(stop!=br::StopPoint::SourceMoved) REQUIRE(exact(a,sb));
            if(stop==br::StopPoint::DestinationMoved) REQUIRE(!io::fileExists(b)&&exact(b+".vpt-old",db));
            if(stop==br::StopPoint::SourceMoved) REQUIRE(!io::fileExists(a)&&exact(a+".vpt-old",sb));
        }
        auto finish=br::resumeJournal(j,a,b);
        if(finish.status!=br::JournalStatus::Complete) std::fprintf(stderr,"%s\n",finish.message.c_str());
        REQUIRE(finish.status==br::JournalStatus::Complete);
        REQUIRE(exact(a+".vpt-old",sb)&&exact(b+".vpt-old",db));
        std::vector<uint8_t> sa,da;REQUIRE(io::readFile(a,sa)&&io::readFile(b,da));
        auto sr=sv::loadSaveFile(sa),dr=sv::loadSaveFile(da);
        REQUIRE(sr&&dr&&sv::validateSave(*sr)&&sv::validateSave(*dr));
        REQUIRE(sr->readBoxSlot(0,0,e.data())==sv::SlotState::Empty);
        REQUIRE(dr->readBoxSlot(0,0,e.data())==sv::SlotState::Valid);
        REQUIRE(pk::pk3::toPokemon(e.data(),80).species==155);
        REQUIRE(br::resumeJournal(j,a,b).status==br::JournalStatus::Complete);
        REQUIRE(exact(a,sa)&&exact(b,da));
        std::printf("PASS interruption case %d\n",n);
    }
    std::string folder=std::string(root)+"/conflict";REQUIRE(fs::create_directory(folder));
    auto a=folder+"/crystal.sav",b=folder+"/ruby.sav",j=folder+"/transfer.vpt";
    REQUIRE(writeFixture(a,sb)&&writeFixture(b,db));
    REQUIRE(br::prepareJournal(j,a,b,{0,0},{0,0}).status==br::JournalStatus::Complete);
    REQUIRE(br::resumeJournal(j,b,a).status==br::JournalStatus::Conflict);
    auto changed=db;changed.back()^=1;
    REQUIRE(writeFixture(b,changed));
    REQUIRE(br::resumeJournal(j,a,b).status==br::JournalStatus::Conflict);
    REQUIRE(exact(a,sb)&&exact(b,changed));
    REQUIRE(writeFixture(b,db));
    std::vector<uint8_t> corrupt;REQUIRE(io::readFile(j,corrupt));corrupt.back()^=1;
    REQUIRE(writeFixture(j,corrupt));
    REQUIRE(br::resumeJournal(j,a,b).status==br::JournalStatus::InvalidJournal);
    REQUIRE(exact(a,sb)&&exact(b,db));
    std::puts("PASS: four completion/recovery cases, path mismatch, edited destination, corrupt journal and idempotence");
    return 0;
}
