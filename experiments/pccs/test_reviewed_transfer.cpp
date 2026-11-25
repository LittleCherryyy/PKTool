// SPDX-License-Identifier: GPL-3.0-or-later
#include "transfer_journal.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
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
    std::fprintf(stderr,"%s:%d: %s\n",f,l,e);std::exit(1);
}
#define REQUIRE(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
bool put(const std::string& p,const std::vector<uint8_t>& b){return io::safeWriteFile(p,b)==io::WriteResult::Ok;}
int main(){
    auto s=sv::loadSaveFile(fakegb::blankGen2(fakegb::kGen2Crystal));
    auto d=sv::loadSaveFile(fake::blankGen3(sv::Game::RubySapphire));
    REQUIRE(s&&d);
    pk::create4::Trainer tr;tr.name="ASH";tr.tid=12345;tr.version=41;tr.language=2;
    std::array<uint8_t,pk::entity::kMaxPartySize> e{};
    pk::create4::encode(pk::create4::newPokemon(tr,155,5,255),e.data());e[1]=0;
    REQUIRE(s->writeBoxSlot(0,0,e.data()));
    s->refreshChecksums();d->refreshChecksums();
    auto sb=s->data(),db=d->data();
    char root[]="/tmp/vpt006c-XXXXXX";REQUIRE(mkdtemp(root)!=nullptr);
    struct Cleanup{const char* p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{root};
    std::string a=std::string(root)+"/crystal.sav",b=std::string(root)+"/ruby.sav",j=std::string(root)+"/transfer.vpt";
    REQUIRE(put(a,sb)&&put(b,db));
    std::string err;std::vector<std::string> notes;
    auto review=br::ReviewedTransfer::create(a,b,sb,db,{0,0},{0,2},err,notes);
    if(!review) std::fprintf(stderr,"Review refused: %s\n",err.c_str());
    REQUIRE(review&&review->originalsMatch());
    auto exact=review->converted();
    auto changed=db;changed.back()^=1;
    REQUIRE(put(b,changed)&&!review->originalsMatch());
    REQUIRE(review->persist(j).status==br::JournalStatus::Conflict&&!io::fileExists(j));
    REQUIRE(!br::ReviewedTransfer::create(a,b,sb,db,{0,0},{0,2},err,notes));
    REQUIRE(put(b,db)&&review->originalsMatch());
    auto changedSource=sb;changedSource.back()^=1;
    REQUIRE(put(a,changedSource)&&!review->originalsMatch());
    REQUIRE(review->persist(j).status==br::JournalStatus::Conflict&&!io::fileExists(j));
    REQUIRE(put(a,sb));
    REQUIRE(review->persist(j).status==br::JournalStatus::Complete);
    REQUIRE(review->originalsMatch());
    REQUIRE(review->persist(j).status==br::JournalStatus::Conflict);
    REQUIRE(br::resumeJournal(j,a,b,br::StopPoint::DestinationWritten).status==br::JournalStatus::Interrupted);
    std::vector<uint8_t> bytes;REQUIRE(io::readFile(b,bytes));
    auto dr=sv::loadSaveFile(bytes);
    REQUIRE(dr&&sv::validateSave(*dr)&&dr->readBoxSlot(0,2,e.data())==sv::SlotState::Valid);
    REQUIRE(std::equal(exact.begin(),exact.end(),e.begin()));
    REQUIRE(br::resumeJournal(j,a,b).status==br::JournalStatus::Complete);
    REQUIRE(io::readFile(a,bytes));auto sr=sv::loadSaveFile(bytes);
    REQUIRE(sr&&sv::validateSave(*sr)&&sr->readBoxSlot(0,0,e.data())==sv::SlotState::Empty);
    REQUIRE(!review->originalsMatch());
    std::puts("PASS: exact reviewed PK3 persisted without reconversion; both stale snapshots rejected");
    return 0;
}
