// SPDX-License-Identifier: GPL-3.0-or-later
#include "transfer_journal.h"
#include "core/io/file_io.h"
#if defined(ARM9) || defined(VPT_FAT_TEST)
#define VPT_FAT_STORAGE 1
#include <sys/stat.h>
#else
#include <filesystem>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>
#include <limits>
namespace pktool::pokemon::pccs_bridge {
namespace {
#ifndef VPT_FAT_STORAGE
namespace fs=std::filesystem;
#endif
using Bytes=std::vector<uint8_t>;
using Status=JournalStatus;
constexpr size_t maxSave=160*1024, maxJournal=4*maxSave+8192;
struct Record { std::string source,dest; Bytes sb,db,sa,da; };
JournalResult result(Status s,const char* m) { return {s,m}; }
#ifdef VPT_FAT_STORAGE
// Only the DSi internal SD mount is supported. Restrict paths to unambiguous
// ASCII and UTF-8 Latin-1 letters; reject relative paths, traversal, malformed UTF-8,
// DOS short-name aliases,
// trailing dots/spaces and other mounts. No FAT symlinks are supported.
bool exists(const std::string& p) {
    struct stat st{};
    errno=0;
    return stat(p.c_str(),&st)==0 || errno!=ENOENT;
}
bool regular(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(),&st)==0 && S_ISREG(st.st_mode);
}
std::string normalized(const std::string& p) {
    if(p.size()<5 || p.size()>240 || p.compare(0,4,"sd:/")!=0) return {};
    size_t start=4;
    for(size_t i=4;i<=p.size();++i) {
        if(i==p.size() || p[i]=='/') {
            if(i==start || i-start>220 || p[start]=='.' || p[i-1]=='.' ||
               p[start]==' ' || p[i-1]==' ') return {};
            start=i+1;
        } else {
            const auto c=static_cast<unsigned char>(p[i]);
            if(c==0xc3 && i+1<p.size()) {
                const auto tail=static_cast<unsigned char>(p[i+1]);
                if(tail<0x80 || tail>0xbf || tail==0x97 || tail==0xb7) return {};
                ++i;
            } else if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
                 c==' '||c=='_'||c=='-'||c=='.'||c=='('||c==')'||c=='['||c==']')) return {};
        }
    }
    struct stat st{};
    const auto slash=p.find_last_of('/');
    auto parent=p.substr(0,slash==3?4:slash);
    if(stat(parent.c_str(),&st)!=0 || !S_ISDIR(st.st_mode)) return {};
    return p;
}
std::string pathKey(std::string p) {
    for(size_t i=0;i<p.size();++i) {
        auto c=static_cast<unsigned char>(p[i]);
        if(c>='A'&&c<='Z') p[i]=char(c-'A'+'a');
        else if(c==0xc3 && i+1<p.size()) {
            auto tail=static_cast<unsigned char>(p[++i]);
            if(tail>=0x80 && tail<=0x9e && tail!=0x97) p[i]=char(tail+0x20);
        }
    }
    return p;
}
#else
bool exists(const std::string& p) {
    std::error_code ec;
    auto t=fs::symlink_status(p,ec).type();
    // Anything except a confirmed missing entry is a conflict, including a
    // dangling symlink or an inaccessible path.
    return t!=fs::file_type::not_found;
}
bool regular(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(fs::symlink_status(p,ec)) && !ec;
}
std::string normalized(const std::string& p) {
    std::error_code ec;
    auto a=fs::absolute(p,ec);
    if(ec || a.filename().empty() || a.filename()=="." || a.filename()=="..") return {};
    auto parent=fs::canonical(a.parent_path(),ec);
    return ec?std::string(): (parent/a.filename()).string();
}
std::string pathKey(const std::string& p) { return p; }
#endif
bool distinct(const std::string& j,const std::string& s,const std::string& d) {
    if(j.empty()||s.empty()||d.empty()) return false;
    std::vector<std::string> paths{j,j+".new",s,s+".vpt-old",s+".vpt-new",d,d+".vpt-old",d+".vpt-new"};
    for(auto& p:paths) p=pathKey(p);
    std::sort(paths.begin(),paths.end());
    return std::adjacent_find(paths.begin(),paths.end())==paths.end();
}
bool readBounded(const std::string& p,Bytes& out,size_t limit) {
    if(!regular(p)) return false;
    long n=io::fileSize(p);
    return n>0 && static_cast<size_t>(n)<=limit && io::readFile(p,out) &&
           out.size()==static_cast<size_t>(n);
}
bool exact(const std::string& p,const Bytes& b) {
    if(!regular(p) || io::fileSize(p)!=static_cast<long>(b.size())) return false;
    io::FileReader f;
    if(!f.open(p) || f.size()!=b.size()) return false;
    std::array<uint8_t,4096> chunk{};
    for(size_t off=0;off<b.size();off+=chunk.size()) {
        auto n=std::min(chunk.size(),b.size()-off);
        if(!f.read(off,n,chunk.data()) || !std::equal(chunk.begin(),chunk.begin()+n,b.begin()+off))
            return false;
    }
    return true;
}
#ifdef VPT_FAT_STORAGE
// libfat flushes its cache on successful rename and fsync of modified files.
// It has no directory-fsync contract. This is a compile/runtime candidate,
// not proof of persistence through sudden power loss on a physical SD card.
bool syncParent(const std::string&) { return true; }
bool syncFile(const std::string& p) {
    int fd=open(p.c_str(),O_RDWR);
    if(fd<0) return false;
    bool ok=fsync(fd)==0;
    return close(fd)==0 && ok;
}
constexpr int createFlags=O_WRONLY|O_CREAT|O_EXCL;
#else
bool syncParent(const std::string& p) {
    int fd=open(fs::path(p).parent_path().c_str(),O_RDONLY|O_DIRECTORY);
    if(fd<0) return false;
    bool ok=fsync(fd)==0;
    return close(fd)==0 && ok;
}
bool syncFile(const std::string& p) {
    int fd=open(p.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) return false;
    bool ok=fsync(fd)==0;
    return close(fd)==0 && ok;
}
constexpr int createFlags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW;
#endif
// Exclusive create, fsync, close, exact readback, directory sync. No silent
// truncation or deletion of a previous file even if an IO error occurs.
bool writeNew(const std::string& p,const Bytes& b) {
    int fd=open(p.c_str(),createFlags,0600);
    if(fd<0) return false;
    size_t done=0;
    while(done<b.size()) {
        ssize_t n=write(fd,b.data()+done,b.size()-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) break;
        done+=static_cast<size_t>(n);
    }
    bool ok=done==b.size() && fsync(fd)==0;
    ok=close(fd)==0 && ok;
    return ok && exact(p,b) && syncParent(p);
}
bool moveNew(const std::string& a,const std::string& b) {
    if(exists(b) || !regular(a) || std::rename(a.c_str(),b.c_str())!=0) return false;
#ifdef VPT_FAT_STORAGE
    return syncFile(b);
#else
    return syncParent(a) && (fs::path(a).parent_path()==fs::path(b).parent_path() || syncParent(b));
#endif
}
uint32_t crc(const uint8_t* p,size_t n) {
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<n;++i) {
        c^=p[i];
        for(int b=0;b<8;++b) c=(c>>1)^((c&1)?0xedb88320u:0u);
    }
    return c^0xffffffffu;
}
void word(Bytes& b,uint32_t x) { for(int i=0;i<4;++i) b.push_back(uint8_t(x>>(8*i))); }
void field(Bytes& b,const uint8_t* p,size_t n) { word(b,uint32_t(n)); b.insert(b.end(),p,p+n); }
Bytes encode(const Record& r) {
    Bytes b{'V','P','T','J','0','0','0','1'};
    b.reserve(36+r.source.size()+r.dest.size()+r.sb.size()+r.db.size()+r.sa.size()+r.da.size());
    field(b,reinterpret_cast<const uint8_t*>(r.source.data()),r.source.size());
    field(b,reinterpret_cast<const uint8_t*>(r.dest.data()),r.dest.size());
    for(auto p:{&r.sb,&r.db,&r.sa,&r.da}) field(b,p->data(),p->size());
    word(b,crc(b.data(),b.size()));
    return b;
}
bool getWord(const Bytes& b,size_t& pos,uint32_t& n) {
    if(pos>b.size() || b.size()-pos<4) return false;
    n=0; for(int i=0;i<4;++i) n|=uint32_t(b[pos++])<<(8*i);
    return true;
}
bool getField(const Bytes& b,size_t& pos,Bytes& out,size_t limit) {
    uint32_t n=0;
    if(!getWord(b,pos,n)||n==0||n>limit||n>b.size()-pos) return false;
    out.assign(b.begin()+pos,b.begin()+pos+n); pos+=n; return true;
}
bool decode(const Bytes& b,Record& r) {
    if(b.size()<12 || std::string(b.begin(),b.begin()+8)!="VPTJ0001") return false;
    size_t end=b.size()-4,p=end; uint32_t stored=0;
    if(!getWord(b,p,stored)||stored!=crc(b.data(),end)) return false;
    p=8; Bytes s,d;
    if(!getField(b,p,s,4096)||!getField(b,p,d,4096)) return false;
    r.source.assign(s.begin(),s.end()); r.dest.assign(d.begin(),d.end());
    if(r.source.find('\0')!=std::string::npos||r.dest.find('\0')!=std::string::npos) return false;
    for(auto x:{&r.sb,&r.db,&r.sa,&r.da}) if(!getField(b,p,*x,maxSave)) return false;
    return p==end && r.sb.size()==r.sa.size() && r.db.size()==r.da.size() && r.sb!=r.sa && r.db!=r.da;
}
// Deterministic name from the record payload, not its appended CRC.
std::string archivePath(const std::string& j,const Bytes& bytes) {
    return j+".archive-"+std::to_string(crc(bytes.data(),bytes.size()-4));
}
bool archiveDistinct(const std::string& j,const Record& r,const std::string& a) {
    auto key=pathKey(a);
    for(const auto& p:{j,j+".new",r.source,r.source+".vpt-old",r.source+".vpt-new",
                      r.dest,r.dest+".vpt-old",r.dest+".vpt-new"})
        if(key==pathKey(p)) return false;
    return normalized(a)==a;
}
bool archiveReady(const std::string& j,const Bytes& bytes,const Record& r) {
    auto a=archivePath(j,bytes);
    return archiveDistinct(j,r,a)&&exact(a,bytes);
}
bool completedImages(const Record& r,bool archived) {
    if(!exact(r.source,r.sa)||!exact(r.dest,r.da)||
       exists(r.source+".vpt-new")||exists(r.dest+".vpt-new")) return false;
    for(const auto& p:{std::make_pair(r.source+".vpt-old",&r.sb),
                       std::make_pair(r.dest+".vpt-old",&r.db)}) {
        if(exists(p.first)) {if(!exact(p.first,*p.second)) return false;}
        else if(!archived) return false;
    }
    return true;
}
enum class State { Before, Moved, After, Conflict };
State state(const std::string& path,const Bytes& before,const Bytes& after) {
    const auto old=path+".vpt-old", next=path+".vpt-new";
    bool haveOld=exists(old), haveNext=exists(next);
    if(haveOld && !exact(old,before)) return State::Conflict;
    if(haveNext && !exact(next,after)) return State::Conflict;
    if(exact(path,before) && !haveOld) return State::Before;
    if(!exists(path) && haveOld && haveNext) return State::Moved;
    if(exact(path,after) && haveOld && !haveNext) return State::After;
    return State::Conflict;
}
JournalResult apply(const std::string& path,const Bytes& before,const Bytes& after, bool interrupt) {
    auto st=state(path,before,after);
    if(st==State::Conflict) return result(Status::Conflict,"Save or sidecar differs from journal");
    if(st==State::After) return result(Status::Complete,"Already written");
    const auto old=path+".vpt-old", next=path+".vpt-new";
    if(st==State::Before) {
        if(!exists(next) && !writeNew(next,after)) return result(Status::IoError,"Cannot stage replacement");
        if(!exact(next,after)||!syncFile(next)||!syncParent(next)||!exact(path,before)||!syncFile(path)||!moveNew(path,old))
            return result(Status::IoError,"Cannot retain original");
        if(interrupt) return result(Status::Interrupted,"Stopped after original rename");
    }
    if(state(path,before,after)!=State::Moved||!moveNew(next,path)||
       state(path,before,after)!=State::After||!syncFile(path)||!syncParent(path))
        return result(Status::IoError,"Cannot verify replacement");
    return result(Status::Complete,"Verified replacement");
}
}
std::unique_ptr<ReviewedTransfer> ReviewedTransfer::create(
    const std::string& source,const std::string& destination,
    const Bytes& expectedSource,const Bytes& expectedDestination,
    BoxSlot from,BoxSlot to,std::string& error,std::vector<std::string>& notes) {
    return createBatch(source,destination,expectedSource,expectedDestination,{{from,to}},error,notes);
}
std::unique_ptr<ReviewedTransfer> ReviewedTransfer::createBatch(
    const std::string& source,const std::string& destination,
    const Bytes& expectedSource,const Bytes& expectedDestination,
    const std::vector<TransferPair>& pairs,std::string& error,std::vector<std::string>& notes,uint8_t sourceLanguage,std::vector<EntryReview>* entries) {
    ReviewedBatch batch(source,destination,expectedSource,expectedDestination,pairs,sourceLanguage);
    while(!batch.step()) {}
    return batch.take(error,notes,entries);
}
ReviewedBatch::ReviewedBatch(const std::string& source,const std::string& destination,
    const Bytes& expectedSource,const Bytes& expectedDestination,
    const std::vector<TransferPair>& pairs,uint8_t sourceLanguage)
    :source_(normalized(source)),destination_(normalized(destination)),sb_(expectedSource),
     db_(expectedDestination),batch_(sb_,db_,pairs,sourceLanguage){}
size_t ReviewedBatch::completed() const{return batch_.completed();}
size_t ReviewedBatch::total() const{return batch_.total();}
const std::vector<EntryReview>& ReviewedBatch::entries() const{return done_?staged_.entries:batch_.entries();}
bool ReviewedBatch::step() {
    if(done_) return true;
    if(!checked_) {
        const auto& s=source_;const auto& d=destination_;
    if(s.empty()) {error_="Unsupported source save path";done_=true;return true;}
    if(d.empty()) {error_="Unsupported destination save path";done_=true;return true;}
    if(pathKey(s)==pathKey(d)) {error_="Source and destination alias";done_=true;return true;}
    if(sb_.empty()||db_.empty()||
       sb_.size()>maxSave||db_.size()>maxSave) {
        error_="Unsupported save size";done_=true;return true;
    }
    for(const auto& p:{s+".vpt-old",s+".vpt-new",d+".vpt-old",d+".vpt-new"}) {
        if(exists(p)) {error_="Existing sidecar or stat failure: "+p;done_=true;return true;}
    }
    if(!exact(s,sb_)||!exact(d,db_)) {
        error_="Selected save changed; restart preview"; done_=true;return true;
    }

        checked_=true;return false;
    }
    if(!batch_.step()) return false;
    staged_=batch_.take();
    if(!staged_.ok) error_=staged_.error;
    else if(!exact(source_,sb_)||!exact(destination_,db_)) error_="Save changed during conversion";
    done_=true;return true;
}
std::unique_ptr<ReviewedTransfer> ReviewedBatch::take(std::string& error,std::vector<std::string>& notes,
                                                      std::vector<EntryReview>* entries) {
    error=done_?error_:"Batch not complete";
    notes=staged_.notes;if(entries) *entries=std::move(staged_.entries);
    if(!done_||!error.empty()||!staged_.ok) return nullptr;
    std::unique_ptr<ReviewedTransfer> reviewed(new ReviewedTransfer);
    reviewed->source_=source_;reviewed->destination_=destination_;
    reviewed->sourceBefore_=sb_;reviewed->destinationBefore_=db_;
    reviewed->staged_=std::move(staged_);
    return reviewed;
}
bool ReviewedTransfer::originalsMatch() const {
    return exact(source_,sourceBefore_)&&exact(destination_,destinationBefore_);
}
JournalResult ReviewedTransfer::persist(const std::string& journal) const {
    auto j=normalized(journal);
    if(!distinct(j,source_,destination_)||exists(j)||exists(j+".new")||
       exists(source_+".vpt-old")||exists(source_+".vpt-new")||
       exists(destination_+".vpt-old")||exists(destination_+".vpt-new")||!originalsMatch())
        return result(Status::Conflict,"Reviewed files changed or transaction conflict");
    Record r{source_,destination_,sourceBefore_,destinationBefore_,staged_.sourceAfter,staged_.destinationAfter};
    auto bytes=encode(r);
    if(bytes.size()>maxJournal||!originalsMatch()||!writeNew(j+".new",bytes)||
       !moveNew(j+".new",j)||!exact(j,bytes))
        return result(Status::IoError,"Cannot persist and verify reviewed journal");
    return result(Status::Complete,"Reviewed journal prepared; originals untouched");
}
JournalResult prepareJournal(const std::string& journal,const std::string& source,
                             const std::string& destination,BoxSlot from,BoxSlot to) {
    auto j=normalized(journal),s=normalized(source),d=normalized(destination);
    if(!distinct(j,s,d)||exists(j)||exists(j+".new"))
        return result(Status::Conflict,"Existing transaction or path conflict");
    Bytes sb,db;
    if(!readBounded(s,sb,maxSave)||!readBounded(d,db,maxSave))
        return result(Status::IoError,"Cannot read original saves");
    std::string error;std::vector<std::string> notes;
    auto reviewed=ReviewedTransfer::create(s,d,sb,db,from,to,error,notes);
    if(!reviewed) return {Status::Conflict,error};
    return reviewed->persist(j);
}
JournalResult resumeJournal(const std::string& journal,const std::string& source,
                            const std::string& destination,StopPoint stop) {
    auto j=normalized(journal),s=normalized(source),d=normalized(destination);
    if(!distinct(j,s,d)) return result(Status::Conflict,"Path conflict");
    Bytes bytes; Record r;
    if(!readBounded(j,bytes,maxJournal)||!decode(bytes,r))
        return result(Status::InvalidJournal,"Missing or corrupt journal");
    if(r.source!=s||r.dest!=d) return result(Status::Conflict,"Journal path mismatch");
    auto ss=state(s,r.sb,r.sa),ds=state(d,r.db,r.da);
    if(ss==State::Conflict||ds==State::Conflict||
       (ss!=State::Before && ds!=State::After))
        return result(Status::Conflict,"Unexpected save state; no writes");
    // Journal must be durable before either original is renamed.
    if(!syncFile(j)||!syncParent(j)) return result(Status::IoError,"Cannot sync journal");
    auto dest=apply(d,r.db,r.da,stop==StopPoint::DestinationMoved);
    if(dest.status!=Status::Complete) return dest;
    if(!exact(d,r.da)||!syncFile(d)||!syncParent(d))
        return result(Status::IoError,"Destination not durable");
    if(stop==StopPoint::DestinationWritten) return result(Status::Interrupted,"Stopped before source removal");
    auto src=apply(s,r.sb,r.sa,stop==StopPoint::SourceMoved);
    if(src.status!=Status::Complete) return src;
    if(!exact(s,r.sa)||!exact(d,r.da)) return result(Status::Conflict,"Final readback mismatch");
    // Retain immutable journal and original sidecars. A future UI must archive
    // them and provide explicit recovery before a new transaction.
    return result(Status::Complete,"Both saves verified; original backups retained");
}
StartupReview inspectActiveJournal(const std::string& journal) {
    StartupReview out;
    if(journal.empty()) {out.message="Invalid active journal path";return out;}
    // The application supplies a fixed, absolute internal SD path. A missing
    // journal directory on a fresh installation is normal; other stat errors
    // are deliberately treated as present by exists().
    if(!exists(journal) && !exists(journal+".new")) {
        out.state=StartupState::Clear;out.message="No active transfer";return out;
    }
    auto j=normalized(journal);
    if(j.empty()||exists(j+".new")) {
        out.message="Incomplete journal creation; retain all files";return out;
    }
    Bytes bytes;Record r;
    if(!readBounded(j,bytes,maxJournal)||!decode(bytes,r)) {
        out.message="Active journal unreadable or corrupt";return out;
    }
    auto s=normalized(r.source),d=normalized(r.dest);
    if(!distinct(j,s,d)||s!=r.source||d!=r.dest) {
        out.message="Invalid recorded save paths";return out;
    }
    out.source=s;out.destination=d;
    // Archive cleanup may already have removed one or both original sidecars.
    // Only a verified exact archive permits that completed state.
    if(completedImages(r,archiveReady(j,bytes,r))) {
        out.state=StartupState::Completed;
        out.message="Transfer complete; archive required";
        return out;
    }
    auto ss=state(s,r.sb,r.sa),ds=state(d,r.db,r.da);
    if(ss==State::Conflict||ds==State::Conflict||
       (ss!=State::Before&&ds!=State::After)) {
        out.message="Save or sidecar conflict; no writes allowed";return out;
    }
    if(ss==State::After&&ds==State::After) {
        out.state=StartupState::Completed;
        out.message="Transfer complete; archive required";
    } else {
        out.state=StartupState::Pending;
        out.message="Transfer pending; recovery required";
    }
    return out;
}
JournalResult recoverActiveJournal(const std::string& journal) {
    auto review=inspectActiveJournal(journal);
    if(review.state==StartupState::Completed)
        return result(Status::Complete,"Transfer already complete");
    if(review.state!=StartupState::Pending)
        return {Status::Conflict,review.message};
    return resumeJournal(journal,review.source,review.destination);
}

JournalResult archiveActiveJournal(const std::string& journal,bool stopAfterFirstSidecar) {
    auto j=normalized(journal);
    Bytes bytes;Record r;
    if(j.empty()||exists(j+".new")||!readBounded(j,bytes,maxJournal)||!decode(bytes,r))
        return result(Status::InvalidJournal,"Missing or corrupt active journal");
    if(!distinct(j,r.source,r.dest)||normalized(r.source)!=r.source||normalized(r.dest)!=r.dest)
        return result(Status::Conflict,"Invalid archive paths");
    auto a=archivePath(j,bytes);
    if(!archiveDistinct(j,r,a)) return result(Status::Conflict,"Archive path collision");
    bool archived=archiveReady(j,bytes,r);
    if(!completedImages(r,archived)) return result(Status::Conflict,"Transfer not complete or save changed");
    if(exists(a)&&!archived) return result(Status::Conflict,"Archive differs; keep all files");
    if(!archived&&!writeNew(a,bytes)) return result(Status::IoError,"Cannot create archive");
    if(!archiveReady(j,bytes,r)||!syncFile(a)||!syncParent(a))
        return result(Status::IoError,"Archive verification failed");
    // Independent archive copy contains both full original images. Only remove
    // old sidecars after validating that copy. Do not rename the active record
    // over an archive: FAT rename is not atomic across power loss.
    int n=0;
    for(const auto& p:{std::make_pair(r.source+".vpt-old",&r.sb),
                       std::make_pair(r.dest+".vpt-old",&r.db)}) {
        if(exists(p.first)) {
            if(!completedImages(r,true)||!exact(p.first,*p.second)||
               std::remove(p.first.c_str())!=0||exists(p.first)||!syncParent(p.first))
                return result(Status::IoError,"Cannot release original sidecar");
        }
        if(++n==1&&stopAfterFirstSidecar)
            return result(Status::Interrupted,"Stopped during archive cleanup");
    }
    if(!completedImages(r,true)||!archiveReady(j,bytes,r)||
       !exact(j,bytes)||std::remove(j.c_str())!=0||exists(j)||!syncParent(j))
        return result(Status::IoError,"Cannot release active journal");
    return result(Status::Complete,"Transfer archived; full originals retained");
}

}
