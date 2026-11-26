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
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Clear);
    REQUIRE(br::inspectActiveJournal(std::string(root)+"/missing/active.vpt").state==br::StartupState::Clear);
    REQUIRE(put(j+".new",{1,2,3}));
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Blocked);
    REQUIRE(std::remove((j+".new").c_str())==0);
    REQUIRE(put(j,{1,2,3}));
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Blocked);
    REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Conflict);
    REQUIRE(std::remove(j.c_str())==0);
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
    auto pending=br::inspectActiveJournal(j);
    REQUIRE(pending.state==br::StartupState::Pending && pending.source==a && pending.destination==b);
    REQUIRE(put(b,changed));
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Blocked);
    REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Conflict);
    REQUIRE(put(b,db));
    REQUIRE(review->persist(j).status==br::JournalStatus::Conflict);
    REQUIRE(br::resumeJournal(j,a,b,br::StopPoint::DestinationWritten).status==br::JournalStatus::Interrupted);
    std::vector<uint8_t> bytes;REQUIRE(io::readFile(b,bytes));
    auto dr=sv::loadSaveFile(bytes);
    REQUIRE(dr&&sv::validateSave(*dr)&&dr->readBoxSlot(0,2,e.data())==sv::SlotState::Valid);
    REQUIRE(std::equal(exact.begin(),exact.end(),e.begin()));
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Pending);
    REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Complete);
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Completed);
    REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Complete);
    REQUIRE(io::readFile(a,bytes));auto sr=sv::loadSaveFile(bytes);
    REQUIRE(sr&&sv::validateSave(*sr)&&sr->readBoxSlot(0,0,e.data())==sv::SlotState::Empty);
    REQUIRE(!review->originalsMatch());
    REQUIRE(br::archiveActiveJournal(j,true).status==br::JournalStatus::Interrupted);
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Completed);
    REQUIRE(!io::fileExists(a+".vpt-old")&&io::fileExists(b+".vpt-old"));
    REQUIRE(br::archiveActiveJournal(j).status==br::JournalStatus::Complete);
    REQUIRE(br::inspectActiveJournal(j).state==br::StartupState::Clear);
    REQUIRE(!io::fileExists(b+".vpt-old")&&!io::fileExists(j));
    int archives=0;
    for(auto& file:fs::directory_iterator(root))
        if(file.path().filename().string().find("transfer.vpt.archive-")==0) ++archives;
    REQUIRE(archives==1);
    for(auto& file:fs::directory_iterator(root)) {
        if(file.path().filename().string().find("transfer.vpt.archive-")!=0) continue;
        br::TransferHistory h;
        REQUIRE(br::readTransferHistory(file.path().string(),h));
        REQUIRE(h.pokemon.size()==1&&h.pokemon[0].species==155&&h.source==a&&h.destination==b);
    }
    // Bank destination uses the very same reviewed journal and recovery.
    {
        auto bankPath=std::string(root)+"/bank.pkb";
        pktool::bank::Bank bank;
        const auto initial=bank.serialize();
        REQUIRE(put(a,sb)&&put(bankPath,initial));
        auto reviewed=br::ReviewedTransfer::create(a,bankPath,sb,initial,{0,0},{0,0},err,notes);
        REQUIRE(reviewed&&reviewed->persist(j).status==br::JournalStatus::Complete);
        REQUIRE(br::resumeJournal(j,a,bankPath,br::StopPoint::DestinationWritten).status==br::JournalStatus::Interrupted);
        REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Complete);
        REQUIRE(br::archiveActiveJournal(j).status==br::JournalStatus::Complete);
        REQUIRE(bank.load(bankPath)==pktool::bank::Bank::LoadResult::Ok&&bank.format(0,0)==3);
    }
    {
        // Withdrawal of the last native Bank Pokemon changes only its slot,
        // the canonical version and CRC; recover after writing GBA first.
        auto bp=std::string(root)+"/native-bank.pkb";
        pktool::bank::Bank bank;
        REQUIRE(bank.set(0,0,2,e.data()));
        // e currently holds the earlier converted PK3. Rebuild a true PK2.
        pk::create4::Trainer t;t.name="ASH";t.tid=12345;t.version=41;t.language=2;
        pk::create4::encode(pk::create4::newPokemon(t,155,5,255),e.data());e[1]=0;
        REQUIRE(bank.set(0,0,2,e.data()));
        const auto before=bank.serialize();
        REQUIRE(put(bp,before)&&put(b,db));
        auto reviewed=br::ReviewedTransfer::create(bp,b,before,db,{0,0},{0,0},err,notes);
        REQUIRE(reviewed&&reviewed->persist(j).status==br::JournalStatus::Complete);
        REQUIRE(br::resumeJournal(j,bp,b,br::StopPoint::DestinationWritten).status==br::JournalStatus::Interrupted);
        REQUIRE(br::recoverActiveJournal(j).status==br::JournalStatus::Complete);
        REQUIRE(br::archiveActiveJournal(j).status==br::JournalStatus::Complete);
        REQUIRE(bank.load(bp)==pktool::bank::Bank::LoadResult::Ok&&bank.count()==0);
    }
    std::puts("PASS: archive cleanup interruption and resume");
    std::puts("PASS: startup gate clear, partial, corrupt, pending, conflict, recovered and complete; exact reviewed bytes persisted");
    return 0;
}
