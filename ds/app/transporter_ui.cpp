// SPDX-License-Identifier: GPL-3.0-or-later
// Optional PCCS preview and separately enabled experimental write flow.
#ifdef VPT_TRANSPORTER_PREVIEW
#include "ds/app/app.h"
#include "ds/app/ui_common.h"
#include "core/io/file_io.h"
#include "core/saves/save_commit.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/data/language.h"
#include "experiments/pccs/pccs_preflight.h"
#include <cctype>
#include "experiments/pccs/transfer_journal.h"
#include <array>
#include <algorithm>

namespace pktool::ui {
using namespace detail;
namespace visual=transport_visual;
namespace {
constexpr size_t kMaxSave = 160 * 1024;
struct ConsoleScreen {int x,y,w,h;};
ConsoleScreen drawLinkConsole(gfx::Canvas& c,visual::Console model,bool source) {
    const int x=source?9:164;
    const Color black=rgb(45,44,56),gray=rgb(171,175,162),purple=rgb(102,70,164);
    const Color blue=rgb(104,155,193),burgundy=rgb(141,43,82);
    auto dpad=[&](int cx,int cy){c.fillRect(cx-10,cy-3,21,7,black);c.fillRect(cx-3,cy-10,7,21,black);};
    if(model==visual::Console::Bank) {
        c.roundRect(x+8,39,66,102,kBorder,rgb(62,97,138),2);
        for(int i=0;i<3;++i) {
            c.roundRect(x+14,105+i*10,54,8,kBorder,kPanelDark);
            c.fillCircle(x+19,109+i*10,2,kGreen);
        }
        c.roundRect(x+13,47,56,55,kPanelDark,rgb(195,218,231));
        return {x+16,50,50,49};
    }
    if(model==visual::Console::DMG) {
        c.roundRect(x+6,29,70,123,kBorder,gray,2);
        c.roundRect(x+12,36,58,65,black,rgb(103,105,118));
        c.hline(x+16,41,50,burgundy);c.fillCircle(x+16,51,2,kRed);
        c.fillRect(x+20,45,45,49,rgb(181,202,133));
        dpad(x+24,117);c.fillCircle(x+60,110,5,burgundy);c.fillCircle(x+51,121,5,burgundy);
        c.fillRect(x+30,135,8,3,black);c.fillRect(x+42,135,8,3,black);
        for(int i=0;i<4;++i)c.vline(x+61+i*2,136,8,rgb(116,121,115));
        return {x+20,45,45,49};
    }
    if(model==visual::Console::GBCPurple) {
        c.roundRect(x+9,29,64,123,kBorder,purple,2);
        c.roundRect(x+14,35,54,61,black,black);
        c.fillRect(x+18,39,46,48,rgb(220,235,208));
        c.hline(x+21,91,9,kRed);c.hline(x+32,91,9,kYellow);c.hline(x+43,91,9,kGreen);
        dpad(x+26,113);c.fillCircle(x+60,109,5,black);c.fillCircle(x+52,120,5,black);
        c.fillRect(x+31,131,7,3,black);c.fillRect(x+41,131,7,3,black);
        for(int i=0;i<4;++i)c.hline(x+48,139+i*2,15,black);
        return {x+18,39,46,48};
    }
    if(model==visual::Console::GBASP) {
        // Open clamshell: screen in lid, hinge, separate control deck.
        c.roundRect(x+7,27,70,71,kBorder,blue,2);
        c.roundRect(x+12,33,60,59,black,black);
        c.fillRect(x+16,37,52,45,rgb(205,229,210));
        c.hline(x+25,86,35,blue);
        c.roundRect(x+5,97,74,53,kBorder,blue,2);
        c.fillRect(x+9,95,66,5,black);
        dpad(x+22,117);c.fillCircle(x+65,110,5,black);c.fillCircle(x+56,121,5,black);
        c.fillRect(x+32,137,8,3,black);c.fillRect(x+44,137,8,3,black);
        c.fillCircle(x+70,143,2,kGreen);
        return {x+16,37,52,45};
    }
    // AGB-001 is horizontal, never the clamshell.
    c.roundRect(x,54,87,63,kBorder,rgb(79,77,161),2);
    c.fillRect(x+4,54,12,3,gray);c.fillRect(x+71,54,12,3,gray);
    c.roundRect(x+18,59,51,51,black,black);
    c.fillRect(x+21,63,45,39,rgb(205,229,210));
    c.fillRect(x+6,80,12,4,black);c.fillRect(x+10,76,4,12,black);
    c.fillCircle(x+79,75,4,black);c.fillCircle(x+74,86,4,black);
    c.fillRect(x+6,98,6,2,black);c.fillRect(x+6,103,6,2,black);
    c.fillCircle(x+75,106,2,kGreen);
    return {x+21,63,45,39};
}
void transferBall(gfx::Canvas& c,int x,int y,int radius) {
    c.fillCircle(x,y,radius,kText);
    for(int dy=-radius;dy<0;++dy)
        for(int dx=-radius;dx<=radius;++dx)
            if(dx*dx+dy*dy<=radius*radius) c.pixel(x+dx,y+dy,kRed);
    c.circle(x,y,radius,kPanelDark);
    c.hline(x-radius,y,radius*2+1,kPanelDark);
    c.fillCircle(x,y,2,kText);c.circle(x,y,2,kPanelDark);
}
bool readImage(const std::string& path,std::vector<uint8_t>& bytes) {
    auto size=io::fileSize(path);
    return size>0 && static_cast<size_t>(size)<=kMaxSave &&
           io::readFile(path,bytes) && bytes.size()==static_cast<size_t>(size);
}
uint8_t languageHint(std::string path) {
    for(auto& c:path) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Do not override the save's Japanese/non-Japanese encoding layout
    // using filename hints. That would corrupt decoded character data.
    if(path.find("(france)")!=std::string::npos || path.find("(fr)")!=std::string::npos) return 3;
    if(path.find("(italy)")!=std::string::npos || path.find("(it)")!=std::string::npos) return 4;
    if(path.find("(germany)")!=std::string::npos || path.find("(de)")!=std::string::npos) return 5;
    if(path.find("(spain)")!=std::string::npos || path.find("(es)")!=std::string::npos) return 7;
    return 2;
}
// Gen I/II saves contain an identifiable JP/non-JP layout, but most do NOT
// encode a complete EU/US language/region tag. Use that layout and an optional
// explicit file label; do not claim an undecidable region was detected.
std::string sourceLanguageLabel(uint8_t language,std::string path) {
    if(language==1) return "Japanese";
    for(auto& c:path) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(language==2 && (path.find("(usa)")!=std::string::npos ||
                      path.find("(us)")!=std::string::npos))
        return "English (USA)";
    if(language==2 && (path.find("(europe)")!=std::string::npos ||
                      path.find("(eu)")!=std::string::npos))
        return "English (Europe)";
    return data::languageName(language);
}

bool destinationReady(const saves::SaveSummary& s,size_t needed) {
    return s.boxIntegrityOk&&pokemon::pccs_bridge::supportedLanguage(s.language)&&
           s.freeBoxSlots>=0&&static_cast<size_t>(s.freeBoxSlots)>=needed;
}
std::string destinationStatus(const saves::SaveSummary& s,size_t needed) {
    if(s.freeBoxSlots<0) return "UNKNOWN";
    if(!s.boxIntegrityOk||!pokemon::pccs_bridge::supportedLanguage(s.language)) return "INVALID";
    return destinationReady(s,needed)?"READY":s.freeBoxSlots==0?"FULL":"NO SPACE";
}
std::string destinationRow(const saves::SaveSummary& s,size_t needed) {
    return destinationStatus(s,needed)+" | "+std::to_string(std::max(0,s.freeBoxSlots))+" free | "+io::fileName(s.path);
}
std::string slotLabel(int box,int slot) {
    return "B"+twoDigits(box+1)+" S"+twoDigits(slot+1);
}
}
std::string App::activeTransferPath() const {
    std::string p=io::joinPath(io::joinPath(config_.dataDir,"transporter"),"active.vpt");
#ifdef ARM9
    if(!p.empty()&&p[0]=='/') p="sd:"+p;
#endif
    return p;
}
bool App::refreshTransferGate() {
    using pokemon::pccs_bridge::StartupState;
    transferStartup_=pokemon::pccs_bridge::inspectActiveJournal(activeTransferPath());
    if(transferStartup_.state==StartupState::Clear) {
        if(screen_==Screen::TransferRecovery) screen_=Screen::Home;
        return true;
    }
    screen_=Screen::TransferRecovery;
    redraw_=true;
    return false;
}
void App::drawTransferRecovery(Canvas& top,Canvas& bottom) {
    using pokemon::pccs_bridge::StartupState;
    const bool complete=transferStartup_.state==StartupState::Completed;
    drawChrome(top,"Poke Transporter",complete?"SUCCESS":"RECOVERY",complete?kGreen:kYellow);
    top.roundRect(5,kBodyTop+3,246,136,kBorder,kPanel);
    if(complete) {
        top.fillCircle(128,47,12,kGreen);
        for(int i=0;i<4;++i) top.pixel(121+i,46+i,kPanelDark);
        for(int i=0;i<8;++i) top.pixel(125+i,50-i,kPanelDark);
        top.text(kFontLarge,8,63,240,Align::Center,"Transfer Complete!",kGreen);
        top.text(kFontSmall,8,83,240,Align::Center,"Both saves verified",kText);
        marqueeActive_ |= drawMarquee(top,8,99,240,Align::Left,"From: "+io::fileName(transferStartup_.source),kMuted,frame_-marqueeStart_);
        marqueeActive_ |= drawMarquee(top,8,113,240,Align::Left,"To: "+io::fileName(transferStartup_.destination),kMuted,frame_-marqueeStart_);
        marqueeActive_ |= drawMarquee(top,8,139,240,Align::Center,transferStartup_.message,kText,frame_-marqueeStart_);
        bottom.clear(rgb(49,104,85));
        bottom.text(kFontSmall,0,2,256,Align::Center,transferStartup_.destination==bankPath()?"BANK - Transfer Complete":"GBA PC - Transfer Complete",kText);
        if(!transportResultMons_.empty()) {
            const size_t start=static_cast<size_t>(transportResultPage_)*10;
            for(size_t i=start;i<std::min(start+10,transportResultMons_.size());++i) {
                const int x=5+static_cast<int>((i-start)%5)*50,y=18+static_cast<int>((i-start)/5)*42;
                bottom.roundRect(x,y,46,40,kBorder,kPanel);
                drawTransportSprite(bottom,transportResultMons_[i],x+2,y+1,42,27,3,false,transportDestVisual_);
                bottom.text(kFontSmall,x,y+27,46,Align::Center,transportResultMons_[i].nickname,kText);
            }
        } else bottom.text(kFontSmall,0,53,256,Align::Center,"Original backups retained.",kText);
#ifdef VPT_TRANSPORTER_WRITE
        if(transportResultMons_.size()>10)
            bottom.text(kFontSmall,0,101,256,Align::Center,"L/R: "+std::to_string(transportResultPage_+1)+"/"+std::to_string((transportResultMons_.size()+9)/10),kText);
        bottom.roundRect(18,109,220,44,kCursor,kSelectedRow,2);
        bottom.text(kFontLarge,18,117,220,Align::Center,"Continue",kText);
        bottom.text(kFontSmall,18,134,220,Align::Center,"Press A to continue",kMuted);
        zones_.push_back({18,109,220,44,kKeyA});
        bottom.text(kFontSmall,0,162,256,Align::Center,"Archives transfer and returns to main menu.",kMuted);
#else
        bottom.text(kFontSmall,0,112,256,Align::Center,"Write-enabled build needed to finish.",kYellow);
#endif
        return;
    }
    top.text(kFontLarge,8,45,240,Align::Center,"Transfer locked",kYellow);
    marqueeActive_ |= drawMarquee(top,8,68,240,Align::Center,transferStartup_.message,kText,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,8,87,240,Align::Left,"From: "+transferStartup_.source,kMuted,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,8,101,240,Align::Left,"To: "+transferStartup_.destination,kMuted,frame_-marqueeStart_);
    top.text(kFontSmall,8,124,240,Align::Center,"Do not open either save in a game.",kYellow);
    top.text(kFontSmall,8,140,240,Align::Center,"Keep journal and sidecar files.",kMuted);
    bottom.clear(kBackground);
    bottom.text(kFontLarge,0,44,256,Align::Center,"Editor blocked",kText);
#ifdef VPT_TRANSPORTER_WRITE
    bottom.text(kFontSmall,0,70,256,Align::Center,
        transferStartup_.state==StartupState::Pending?"X: Resume recorded transfer":
        "Conflicting files: no writes",kYellow);
#else
    bottom.text(kFontSmall,0,70,256,Align::Center,"Read-only recovery inspection",kMuted);
#endif
    bottom.text(kFontSmall,0,102,256,Align::Center,"A: Recheck journal",kText);
    bottom.text(kFontSmall,0,120,256,Align::Center,"START: Exit",kText);
#ifdef VPT_TRANSPORTER_WRITE
    bottom.text(kFontSmall,0,148,256,Align::Center,"Experimental - disposable saves only",kYellow);
#else
    bottom.text(kFontSmall,0,148,256,Align::Center,"Recovery writes not enabled in this build",kYellow);
#endif
}
void App::runTransferRecovery() {
#ifdef VPT_TRANSPORTER_WRITE
    namespace br=pokemon::pccs_bridge;
    if(refreshTransferGate()) return;
    sprites_.clear();
    auto j=activeTransferPath();
    br::JournalResult r{br::JournalStatus::Conflict,"Conflicting transaction; no writes"};
    if(transferStartup_.state==br::StartupState::Pending)
        r=br::recoverActiveJournal(j);
    else if(transferStartup_.state==br::StartupState::Completed)
        r=br::archiveActiveJournal(j);
    const auto source=transferStartup_.source,destination=transferStartup_.destination;
    if(refreshTransferGate()) {
        if(r.status==br::JournalStatus::Complete) {
            // Archive has succeeded; refresh only the two known paths.
            if(source==bankPath()||source=="sd:"+bankPath()) bankLoaded_=false;
            else refreshSessionSave(source);
            if(destination==bankPath()||destination=="sd:"+bankPath()) bankLoaded_=false;
            else refreshSessionSave(destination);
            homeCursor_=4;
            transportRows_.clear();transportNotes_.clear();transportSelected_.clear();
        } else showMessage("Transporter",r.message);
    } else transferStartup_.message=r.message;
#endif
}
void App::commitTransporter() {
#ifdef VPT_TRANSPORTER_WRITE
    if(!transportReview_||!transportAccepted_||!transportConfirm_) return;
    transportConfirm_=false;
    if(!refreshTransferGate()) {transportReview_.reset();return;}
    if(!transportReview_->originalsMatch()) {
        transportMessage_="Save changed; restart preview";transportReview_.reset();transportAccepted_=false;return;
    }
    // The animation no longer needs sprites. Release up to 128 KiB of
    // decoded art, pack indices and all open NitroFS/FAT handles before
    // persisting large reviewed save images on the constrained ARM9 heap.
    // This never changes the already accepted review or staged save bytes.
    sprites_.clear();
    auto j=activeTransferPath();
    auto dir=j.substr(0,j.find_last_of('/'));
    if(!io::makeDirectories(dir)) {transportMessage_="Cannot create journal directory";return;}
    auto r=transportReview_->persist(j);
    // Drop the reviewed snapshots before loading the recovery journal.
    transportReview_.reset();transportAccepted_=false;
    if(r.status!=pokemon::pccs_bridge::JournalStatus::Complete) {
        if(refreshTransferGate()) transportMessage_=r.message;
        else transferStartup_.message=r.message;
        return;
    }
    // Recovery screen remains modal until explicit archive. No second PCCS call.
    runTransferRecovery();
#endif
}
void App::openTransporter() {
    if (!config_.storageReady) { showMessage("Transporter","SD card unavailable."); return; }
    if(!refreshTransferGate()) return;
    // Home only: release large cached state before loading two save images.
    if(save_ || bankDirty_ || !bankWithdrawn_.empty()) {
        showMessage("Transporter","Close the current save first.");
        return;
    }
    // Keep the <=128 KiB warmed artwork and pack indices until the reviewed
    // two-save transaction, where it is freed before allocating large buffers.
    transportSave_.reset();
    transportSourceBytes_.clear();transportDestBytes_.clear();transportReview_.reset();
    transportSource_.clear();transportDest_.clear();
    transportRows_.clear();transportNotes_.clear();transportPositions_.clear();
    transportSelected_.clear();transportMons_.clear();transportResultMons_.clear();
    transportEntries_.clear();transportReviewPairs_.clear();
    transportRepairIndices_.clear();transportRepairFocus_=0;transportRepairScroll_=0;
    transportBoxSpecies_.clear();transportAnimating_=false;
    transportMessage_.clear();transportAccepted_=false;transportConfirm_=false;
    screen_=Screen::Transporter;
    syncTransportCatalog();
    showTransportSaveList(true);
    if(!scanned_) startTransportScan(true);
}
void App::showTransportSaveList(bool source,const std::string& message,const std::string& focus) {
    transportConfirm_=false;transportAccepted_=false;transportBankDest_=false;
    transportDestVisual_={};
    transportSave_.reset();transportReview_.reset();
    std::vector<uint8_t>().swap(transportDestBytes_);
    transportDest_.clear();
    transportEntries_.clear();transportReviewPairs_.clear();transportResultMons_.clear();
    transportRepairIndices_.clear();transportRepairFocus_=0;transportRepairScroll_=0;
    transportRows_.clear();transportNotes_.clear();transportPositions_.clear();
    transportMons_.clear();transportBoxSpecies_.clear();transportLoadBox_=-1;
    if(source) {
        transportBankSource_=false;transportSourceVisual_={};
        std::vector<uint8_t>().swap(transportSourceBytes_);
        transportSource_.clear();transportSelected_.clear();
    }
    transportSaves_.clear();
    for(const auto& save:transportCatalog_) {
        const auto gen=saves::gameGeneration(save.game);
        if(source?(gen==1||gen==2):gen==3) {
            transportSaves_.push_back(save);
            transportRows_.push_back(source?io::fileName(save.path)+" | "+save.trainer:
                destinationRow(save,transportSelected_.size()));
        }
    }
    transportPhase_=source?TransportPhase::Source:TransportPhase::Destination;
    transportCursor_=transportScroll_=0;
    if(!focus.empty()) for(size_t i=0;i<transportSaves_.size();++i) {
        if(transportSaves_[i].path==focus||("sd:"+transportSaves_[i].path)==focus) {
            transportCursor_=static_cast<int>(i);break;
        }
    }
    transportMessage_=message;redraw_=true;
}
void App::syncTransportCatalog() {
    transportCatalog_.clear();
    for(const auto& save:saveFiles_) if(saves::gameGeneration(save.game)<=3)
        transportCatalog_.push_back(save);
}
void App::refreshSessionSave(const std::string& path) {
    if(!scanned_||path.empty()) return;
    const auto canonical=[](std::string p) {
#ifdef ARM9
        if(p.rfind("sd:",0)==0) p.erase(0,3);
        for(auto& c:p) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
#endif
        return p;
    };
    const auto key=canonical(path);
    auto it=std::find_if(saveFiles_.begin(),saveFiles_.end(),[&](const auto& x){return canonical(x.path)==key;});
    const auto stablePath=it==saveFiles_.end()?path:it->path;
    saves::SaveSummary updated;
    long size=0,stamp=0;
    if(io::fileStamp(stablePath,size,stamp)&&saves::probeSave(stablePath,updated)) {
        updated.fileSize=size;updated.modified=stamp;
        if(it!=saveFiles_.end()) *it=std::move(updated);
        else saveFiles_.push_back(std::move(updated));
    } else if(it!=saveFiles_.end()) saveFiles_.erase(it);
    std::sort(saveFiles_.begin(),saveFiles_.end(),[](const auto& x,const auto& y){return x.path<y.path;});
    browserCursor_=clampInt(browserCursor_,0,std::max(0,static_cast<int>(saveFiles_.size())-1));
    syncTransportCatalog();
}
void App::startTransportScan(bool source) {
    if(scanned_) return;
    transportScanSource_=source;
    // A single cooperative discovery starts on Home: menu entry MUST NOT
    // throw away the work already completed in idle startup frames.
    if(transportScan_) return;
    transportScan_.reset(new saves::IncrementalSaveScan(config_.scanRoot,3,5,{}));
    startupCatalogScan_=screen_==Screen::Home;
}
void App::tickTransportScan() {
    if(!transportScan_ || !transportScan_->step()) return;
    saveFiles_=transportScan_->take();transportScan_.reset();
    scanned_=true;syncTransportCatalog();
    preloadIndex_=0;
    if(screen_==Screen::Home) {
        // Startup preload does not navigate on behalf of the player.
        redraw_=true;
        return;
    }
    if(screen_==Screen::Browser) {browserCursor_=browserScroll_=0;return;}
    if(screen_==Screen::Transporter) showTransportSaveList(transportScanSource_);
}
void App::clearStartupImages() {
    preloadedImages_.clear();
    preloadedBytes_=0;
}
// Keep only compact, immutable snapshots for initial menu browsing.
// Large Gen IV/V data and all journal/backup files are excluded. Saves are
// verified again from disk before ANY transaction; this is never a write cache.
void App::tickStartupPreload() {
    if(!scanned_) return;
    // First finish the bounded save cache. Only after that prewarm actual art
    // on separate frames to keep the Home animation responsive.
    if(preloadIndex_>=saveFiles_.size()) {
        if(preloadArtIndex_<preloadArtQueue_.size()) {
            const auto& art=preloadArtQueue_[preloadArtIndex_++];
            sprites_.get(art.kind.c_str(),art.species);
            redraw_=true;
        }
        return;
    }
    constexpr size_t kMaxCacheBytes=384u*1024u;
    constexpr size_t kMaxCacheFiles=8;
    const auto& entry=saveFiles_[preloadIndex_++];
    const unsigned generation=saves::gameGeneration(entry.game);
    if(generation<1 || generation>3 ||
       preloadedImages_.size()>=kMaxCacheFiles ||
       entry.fileSize<=0 || static_cast<size_t>(entry.fileSize)>0x20100u ||
       static_cast<size_t>(entry.fileSize)>kMaxCacheBytes-preloadedBytes_) {
        redraw_=true;return;
    }
    long size=0,modified=0;
    if(!io::fileStamp(entry.path,size,modified) ||
       size!=entry.fileSize || modified!=entry.modified) {
        redraw_=true;return;
    }
    std::vector<uint8_t> image;
    if(io::readFile(entry.path,image) && image.size()==static_cast<size_t>(size)) {
        preloadedBytes_+=image.size();
        preloadedImages_.push_back({entry.path,size,modified,std::move(image)});
        // One image per idle frame; the total decoded sprite budget remains
        // enforced by SpriteStore (128 KiB). Prepare a few high-value covers.
        if(preloadArtQueue_.size()<8)
            preloadArtQueue_.push_back({"front",saves::coverSpecies(entry.version)});
        if(generation<=2 && preloadArtQueue_.size()<8) {
            auto source=saves::loadSaveFile(preloadedImages_.back().bytes);
            if(source) {
                const auto detected=visual::resolve(entry.path,entry.game,source->language()==1);
                const bool certain=detected.choices.size()==1 ||
                                   detected.evidence==visual::Evidence::Filename;
                if(certain) {
                    for(int b=0;b<source->boxCount();++b) {
                        bool found=false;
                        for(int slot=0;slot<source->boxSlotCount();++slot) {
                            std::array<uint8_t,pokemon::entity::kMaxPartySize> raw{};
                            if(source->readBoxSlot(b,slot,raw.data())!=saves::SlotState::Valid) continue;
                            const uint8_t fmt=source->format();
                            raw[fmt==1?pokemon::pk1::kLanguageOffset:pokemon::pk2::kLanguageOffset]=source->language();
                            const auto mon=pokemon::entity::decode(fmt,raw.data(),source->storedSize());
                            const char* kind=visual::spriteKind({detected.suggested,source->language()==1},mon.shiny);
                            if(kind && mon.species) preloadArtQueue_.push_back({kind,mon.species});
                            found=true;break;
                        }
                        if(found) break;
                    }
                }
            }
        }
    }
    redraw_=true;
}
bool App::readStartupImage(const std::string& path,std::vector<uint8_t>& image) {
    std::string normalized=path;
#ifdef ARM9
    if(normalized.rfind("sd:",0)==0) normalized.erase(0,3);
#endif
    for(const auto& cached:preloadedImages_) {
        if(cached.path!=normalized) continue;
        long size=0,modified=0;
        if(io::fileStamp(path,size,modified) &&
           size==cached.fileSize && modified==cached.modified) {
            image=cached.bytes;
            return true;
        }
        break; // Changed on SD: let ordinary read and validation handle it.
    }
    return false;
}
void App::cancelTransportBusy() {
    if(transportBatch_) {
        transportBatch_.reset();
        const auto focus=transportDest_;
        showTransportSaveList(false,"Review cancelled - no saves changed",focus);
    } else if(transportScan_) {
        transportScan_.reset();
        if(screen_==Screen::Browser) screen_=Screen::Home;
        else showTransportSaveList(transportScanSource_,"Scan cancelled - Y to retry");
    } else {
        const auto focus=transportLoadPath_;
        showTransportSaveList(transportLoadSource_,"Reading cancelled",focus);
    }
}
void App::openTransportBankSource() {
    if(bankDirty_||bankHeld_>=0||!bankWithdrawn_.empty()) {transportMessage_="Finish Bank edits first";return;}
    auto path=bankPath();
#ifdef ARM9
    if(!path.empty()&&path[0]=='/') path="sd:"+path;
#endif
    std::vector<uint8_t> bytes;
    bank::Bank source;
    if(!readImage(path,bytes)||!pokemon::pccs_bridge::loadTransferBank(bytes,source)) {
        transportMessage_="Create or save Bank in editor first";return;
    }
    std::vector<std::string> rows;
    std::vector<std::pair<int,int>> positions;
    std::vector<pokemon::Pokemon> mons;
    for(int b=0;b<bank::Bank::kBoxCount;++b) for(int s=0;s<bank::Bank::kSlotsPerBox;++s) {
        auto format=source.format(b,s);
        if(format!=1&&format!=2) continue;
        auto mon=pokemon::entity::decode(format,source.entity(b,s),pokemon::entity::storedSize(format));
        rows.push_back("[ ] "+slotLabel(b,s)+" "+mon.nickname+" Lv"+std::to_string(mon.level));
        positions.emplace_back(b,s);mons.push_back(std::move(mon));
    }
    if(rows.empty()) {transportMessage_="No Gen I/II Pokemon in Bank";return;}
    transportSource_=path;transportSourceBytes_=std::move(bytes);transportBankSource_=true;
    transportSourceVisual_={visual::Edition::Bank,false};
    transportSave_.reset();transportSelected_.clear();
    transportRows_=std::move(rows);transportPositions_=std::move(positions);transportMons_=std::move(mons);
    transportLanguage_=0;transportCursor_=transportScroll_=0;
    transportPhase_=TransportPhase::SourceMon;transportMessage_.clear();
}
void App::openTransportBank() {
    if(transportBankSource_) {transportMessage_="Choose a GBA save for Bank withdrawal";return;}
    // Existing Bank only: do not create or migrate it implicitly in a transfer.
    if(bankDirty_||bankHeld_>=0||!bankWithdrawn_.empty()) {transportMessage_="Finish Bank edits first";return;}
    auto path=bankPath();
#ifdef ARM9
    if(!path.empty()&&path[0]=='/') path="sd:"+path;
#endif
    std::vector<uint8_t> bytes;
    bank::Bank target;
    if(!readImage(path,bytes)||!pokemon::pccs_bridge::loadTransferBank(bytes,target)) {
        transportMessage_="Create or save Bank in editor first";return;
    }
    std::vector<std::pair<int,int>> positions;
    for(int b=0;b<bank::Bank::kBoxCount;++b) for(int s=0;s<bank::Bank::kSlotsPerBox;++s)
        if(!target.format(b,s)) positions.emplace_back(b,s);
    if(positions.size()<transportSelected_.size()) {transportMessage_="Not enough free Bank slots";return;}
    transportDest_=path;transportDestBytes_=std::move(bytes);transportBankDest_=true;
    transportDestVisual_={visual::Edition::Bank,false};
    transportPositions_=std::move(positions);transportRows_.clear();
    for(auto pos:transportPositions_) transportRows_.push_back(slotLabel(pos.first,pos.second)+" Empty");
    transportRows_.resize(transportRows_.size()-transportSelected_.size()+1);
    transportBoxSlots_=bank::Bank::kSlotsPerBox;
    transportBoxSpecies_.assign(bank::Bank::kBoxCount*transportBoxSlots_,0);
    for(int b=0;b<bank::Bank::kBoxCount;++b) for(int s=0;s<transportBoxSlots_;++s)
        if(auto raw=target.entity(b,s))
            transportBoxSpecies_[b*transportBoxSlots_+s]=pokemon::entity::decode(target.format(b,s),raw,bank::Bank::kEntitySize).species;
    transportCursor_=transportScroll_=0;transportPhase_=TransportPhase::DestinationSlot;transportMessage_.clear();
}
void App::openTransferHistory() {
    const auto j=activeTransferPath();
    transportHistoryPaths_.clear();
    for(const auto& p:io::listFiles(j.substr(0,j.find_last_of('/')),""))
        if(io::fileName(p).rfind("active.vpt.archive-",0)==0) transportHistoryPaths_.push_back(p);
    transportHistoryIndex_=0;transportResultPage_=0;transportPhase_=TransportPhase::History;
    loadTransferHistory();
}
void App::loadTransferHistory() {
    transportHistory_={};transportResultPage_=0;
    if(!transportHistoryPaths_.empty())
        pokemon::pccs_bridge::readTransferHistory(transportHistoryPaths_[transportHistoryIndex_],transportHistory_);
}
void App::transporterSelectSave(bool source) {
    if(transportCursor_<0 || transportCursor_>=static_cast<int>(transportSaves_.size())) return;
    if(!source&&!destinationReady(transportSaves_[transportCursor_],transportSelected_.size())) {
        transportMessage_="Destination unavailable - choose another or restart PKTool";return;
    }
    auto path=transportSaves_[transportCursor_].path;
#ifdef ARM9
    if(!path.empty() && path[0]=='/') path="sd:"+path;
#endif
    std::vector<uint8_t> bytes;
    auto invalidateDestination=[&]() {
        if(source) return;
        auto& current=transportSaves_[transportCursor_];
        current.boxIntegrityOk=false;
        for(auto& save:transportCatalog_) if(save.path==current.path) save.boxIntegrityOk=false;
        transportRows_[transportCursor_]=destinationRow(current,transportSelected_.size());
    };
    if(!readStartupImage(path,bytes) && !readImage(path,bytes)) {
        invalidateDestination();transportMessage_="Cannot read save - choose another";return;
    }
    auto loaded=saves::loadSaveFile(bytes);
    if(!loaded || !saves::validateSave(*loaded) || !pokemon::pccs_bridge::supportedLanguage(loaded->language()) ||
       (source?(loaded->format()!=1 && loaded->format()!=2):loaded->format()!=3)) {
        invalidateDestination();
        transportMessage_="Invalid or unsupported save - choose another";
        return;
    }
    if(source) {
        transportSource_=path; transportSourceBytes_=std::move(bytes);
        transportGeneration_=loaded->format();
        transportLanguage_=loaded->language()==1?1:languageHint(path);
        // Non-Japanese Gen I/II layout does not itself prove a country.
        // Non-English labels are hints from a clearly tagged filename only.
    } else { transportDest_=path; transportDestBytes_=std::move(bytes); }
    transportSave_=std::move(loaded);
    transportLoadPath_=path;
    prepareTransportEdition(source);
}
void App::prepareTransportEdition(bool source) {
    const auto family=transportSave_->game();
    const bool jp=transportSave_->language()==1;
    auto& selected=source?transportSourceVisual_:transportDestVisual_;
    const auto cached=std::find_if(transportVisualChoices_.begin(),transportVisualChoices_.end(),[&](const auto& item){
        return item.path==transportLoadPath_&&item.family==family&&item.visual.japanese==jp;
    });
    if(cached!=transportVisualChoices_.end()&&visual::contains(visual::options(family,jp),cached->visual.edition)) {
        selected=cached->visual;beginTransportBoxes(source);return;
    }
    transportEditionResolution_=visual::resolve(transportLoadPath_,family,jp);
    if(transportEditionResolution_.choices.size()==1) {
        selected={transportEditionResolution_.choices.front(),jp};beginTransportBoxes(source);return;
    }
    transportEditionSource_=source;transportPhase_=TransportPhase::Edition;
    transportRows_.clear();transportPositions_.clear();transportCursor_=transportScroll_=0;
    for(const auto e:transportEditionResolution_.choices) {
        if(e==transportEditionResolution_.suggested) transportCursor_=static_cast<int>(transportRows_.size());
        transportRows_.push_back(visual::label({e,jp}));
    }
    transportMessage_=visual::evidenceName(transportEditionResolution_.evidence);
}
void App::confirmTransportEdition() {
    if(transportCursor_<0||transportCursor_>=static_cast<int>(transportEditionResolution_.choices.size())) return;
    const auto choice=visual::VisualEdition{transportEditionResolution_.choices[transportCursor_],transportSave_->language()==1};
    const auto family=transportSave_->game();
    transportVisualChoices_.erase(std::remove_if(transportVisualChoices_.begin(),transportVisualChoices_.end(),
        [&](const auto& item){return item.path==transportLoadPath_;}),transportVisualChoices_.end());
    transportVisualChoices_.push_back({transportLoadPath_,family,choice});
    (transportEditionSource_?transportSourceVisual_:transportDestVisual_)=choice;
    beginTransportBoxes(transportEditionSource_);
}
void App::beginTransportBoxes(bool source) {
    transportRows_.clear();transportPositions_.clear();
    transportMons_.clear();transportBoxSpecies_.clear();
    transportBoxSlots_=transportSave_->boxSlotCount();
    if(!source) transportBoxSpecies_.resize(transportSave_->boxCount()*transportBoxSlots_);
    transportLoadSource_=source;transportLoadBox_=0;
    transportCursor_=transportScroll_=0;
}
void App::tickTransportBoxes() {
    const bool source=transportLoadSource_;
    const int b=transportLoadBox_;
    if(!transportSave_||b<0) {transportLoadBox_=-1;return;}
    {
        for(int s=0;s<transportSave_->boxSlotCount();++s) {
            std::array<uint8_t,pokemon::entity::kMaxPartySize> raw{};
            auto state=transportSave_->readBoxSlot(b,s,raw.data());
            if(source && state==saves::SlotState::Valid) {
                raw[transportGeneration_==1?pokemon::pk1::kLanguageOffset:pokemon::pk2::kLanguageOffset]=transportLanguage_;
                auto p=pokemon::entity::decode(transportSave_->format(),raw.data(),transportSave_->storedSize());
                transportMons_.push_back(p);
                transportRows_.push_back("[ ] "+slotLabel(b,s)+" "+p.nickname+" Lv"+std::to_string(p.level));
                transportPositions_.emplace_back(b,s);
            } else if(!source && state==saves::SlotState::Empty) {
                transportRows_.push_back(slotLabel(b,s)+" Empty");
                transportPositions_.emplace_back(b,s);
            } else if(!source) {
                transportBoxSpecies_[b*transportBoxSlots_+s]=state==saves::SlotState::Valid?
                    pokemon::pk3::toPokemon(raw.data(),80).species:0xffff;
            }
        }
    }
    ++transportLoadBox_;
    if(transportLoadBox_<transportSave_->boxCount()) return;
    transportLoadBox_=-1;
    if(!source) {
        // Refresh the capacity from the fully validated save, not just the
        // earlier partial probe. Keep the cached status accurate on return.
        for(auto* list:{&transportCatalog_,&transportSaves_}) for(auto& save:*list)
            if(save.path==transportLoadPath_||("sd:"+save.path)==transportLoadPath_) {
                save.freeBoxSlots=static_cast<int>(transportPositions_.size());
                save.boxIntegrityOk=true;save.language=transportSave_->language();
            }
    }
    if(transportRows_.empty()||(!source&&transportPositions_.size()<transportSelected_.size())) {
        const auto focus=transportLoadPath_;
        showTransportSaveList(source,source?"No boxed Pokemon - choose another save":"Not enough free slots - choose another save",focus);
        return;
    }
    // Only starting positions with enough following empty slots can be
    // selected. Keep the full positions vector for exact batch mapping.
    if(!source) transportRows_.resize(transportPositions_.size()-transportSelected_.size()+1);
    transportPhase_=source?TransportPhase::SourceMon:TransportPhase::DestinationSlot;
    transportCursor_=transportScroll_=0;
    transportMessage_.clear();
}
void App::refreshTransportNames() {
    if(!transportSave_) return;
    for(size_t i=0;i<transportPositions_.size();++i) {
        auto pos=transportPositions_[i];
        std::array<uint8_t,pokemon::entity::kMaxPartySize> raw{};
        if(transportSave_->readBoxSlot(pos.first,pos.second,raw.data())!=saves::SlotState::Valid) continue;
        raw[transportGeneration_==1?pokemon::pk1::kLanguageOffset:pokemon::pk2::kLanguageOffset]=transportLanguage_;
        auto p=pokemon::entity::decode(transportGeneration_,raw.data(),transportSave_->storedSize());
        transportMons_[i]=p;
        const bool marked=std::find(transportSelected_.begin(),transportSelected_.end(),pos)!=transportSelected_.end();
        transportRows_[i]=std::string(marked?"[X] ":"[ ] ")+slotLabel(pos.first,pos.second)+" "+p.nickname+" Lv"+std::to_string(p.level);
    }
}
void App::transporterPreview() {
    namespace br=pokemon::pccs_bridge;
    if(transportPhase_!=TransportPhase::Review) {
        if(transportCursor_<0 || transportSelected_.empty() ||
           static_cast<size_t>(transportCursor_)+transportSelected_.size()>transportPositions_.size()) {
            transportMessage_="Not enough empty slots from here";return;
        }
        transportReviewPairs_.clear();transportRepairIndices_.clear();
        for(size_t i=0;i<transportSelected_.size();++i) {
            auto f=transportSelected_[i],t=transportPositions_[transportCursor_+i];
            transportReviewPairs_.push_back({{f.first,f.second},{t.first,t.second}});
        }
    }
    const auto& pairs=transportReviewPairs_;
    transportConfirm_=false;
    transportSave_.reset();sprites_.clear();
    clearStartupImages(); // release preload RAM before retaining both 128KB snapshots
    transportNotes_.clear();transportAccepted_=false;
    transportEntries_.clear();transportReview_.reset();
    transportBatch_=std::make_unique<br::ReviewedBatch>(transportSource_,transportDest_,
        transportSourceBytes_,transportDestBytes_,pairs,transportLanguage_,transportRepairIndices_);
}
void App::tickTransportPreview() {
    if(!transportBatch_->step()) return;
    transportReview_=transportBatch_->take(transportMessage_,transportNotes_,&transportEntries_);
    transportBatch_.reset();
    const auto& pairs=transportReviewPairs_;
    transportAccepted_=bool(transportReview_);
    transportResultMons_.clear();transportResultPage_=0;
    if(transportReview_) {
        transportMessage_="Preview passed: "+std::to_string(pairs.size())+" Pokemon";
        const auto& batch=transportReview_->convertedBatch();
        std::vector<std::string> summary;
        for(size_t i=0;i<batch.size();++i) {
            auto p=pokemon::pk3::toPokemon(batch[i].data(),batch[i].size());
            transportResultMons_.push_back(p);
            summary.push_back(p.nickname+" Lv"+std::to_string(p.level)+" -> "+slotLabel(pairs[i].to.box,pairs[i].to.slot));
            summary.push_back("OT: "+p.trainer.name+" TID: "+std::to_string(p.trainer.tid));
            summary.push_back("PID: "+std::to_string(p.pid)+" SID: "+std::to_string(p.trainer.sid));
        }
        transportNotes_.insert(transportNotes_.begin(),summary.begin(),summary.end());
    }
    if(transportAccepted_) {
        std::vector<uint8_t>().swap(transportSourceBytes_);
        std::vector<uint8_t>().swap(transportDestBytes_);
    }
    if(!transportAccepted_) {
        // Keep complete diagnostic sentences as selectable scrolling rows.
        transportNotes_.insert(transportNotes_.begin(),transportMessage_);
    }
    transportRows_.clear();
    for(const auto& e:transportEntries_)
        transportRows_.push_back(e.mon.nickname);
    if(transportRows_.empty()) transportRows_=transportNotes_;
    transportPhase_=TransportPhase::Review;
    transportCursor_=transportScroll_=0;
    transportReviewDetail_=0;
}
void App::updateTransporter(const Input& in) {
    if(transportPhase_==TransportPhase::RepairConfirm) {
        const auto index=transportRepairFocus_;
        if(index>=transportEntries_.size()||!transportEntries_[index].repairAvailable ||
           transportEntries_[index].ready) {
            transportPhase_=TransportPhase::Review;return;
        }
        const auto& changes=transportEntries_[index].repairChanges;
        if(in.repeated&kKeyDown)
            transportRepairScroll_=std::min(transportRepairScroll_+1,std::max(0,static_cast<int>(changes.size())-3));
        if(in.repeated&kKeyUp)
            transportRepairScroll_=std::max(0,transportRepairScroll_-1);
        if(in.pressed&kKeyB) {
            transportPhase_=TransportPhase::Review;transportMessage_="Repair cancelled - no changes";
        } else if(in.pressed&kKeyA) {
            if(std::find(transportRepairIndices_.begin(),transportRepairIndices_.end(),index)==transportRepairIndices_.end())
                transportRepairIndices_.push_back(index);
            transportPhase_=TransportPhase::Review;
            transportCursor_=static_cast<int>(index);
            // The exact original paths and bytes are rechecked by ReviewedBatch
            // on the next frame. A repair never bypasses the journal.
            pending_=Pending::TransferPreview;
        }
        return;
    }
    if(transportPhase_==TransportPhase::Edition) {
        if(in.pressed&kKeyB) {
            const auto path=transportLoadPath_;
            showTransportSaveList(transportEditionSource_,"Choose another save",path);return;
        }
        const int count=static_cast<int>(transportRows_.size());
        if(in.repeated&kKeyUp) transportCursor_=wrap(transportCursor_-1,count);
        if(in.repeated&kKeyDown) transportCursor_=wrap(transportCursor_+1,count);
        if(in.pressed&kKeyA) confirmTransportEdition();
        return;
    }
    if(transportPhase_==TransportPhase::History) {
        if(in.pressed&kKeyB) {showTransportSaveList(true);return;}
        if(!transportHistoryPaths_.empty()&&(in.pressed&(kKeyLeft|kKeyRight))) {
            transportHistoryIndex_=wrap(transportHistoryIndex_+((in.pressed&kKeyRight)?1:-1),static_cast<int>(transportHistoryPaths_.size()));
            loadTransferHistory();
        }
        const int pages=std::max(1,static_cast<int>((transportHistory_.pokemon.size()+9)/10));
        if(in.pressed&kKeyL) transportResultPage_=wrap(transportResultPage_-1,pages);
        if(in.pressed&kKeyR) transportResultPage_=wrap(transportResultPage_+1,pages);
        return;
    }
    if(transportPhase_==TransportPhase::Review && !transportAccepted_ &&
       (in.pressed&kKeyY) && transportCursor_>=0 &&
       transportCursor_<static_cast<int>(transportEntries_.size())) {
        const auto& selected=transportEntries_[transportCursor_];
        if(!selected.ready && selected.repairAvailable) {
            transportRepairFocus_=static_cast<size_t>(transportCursor_);
            transportRepairScroll_=0;
            transportPhase_=TransportPhase::RepairConfirm;
        } else transportMessage_="No verified repair for this entry";
        return;
    }
    if(transportPhase_==TransportPhase::Review &&
       (in.pressed&(kKeyLeft|kKeyRight)) && transportCursor_>=0 &&
       transportCursor_<static_cast<int>(transportEntries_.size())) {
        const auto& entry=transportEntries_[transportCursor_];
        const int count=static_cast<int>(entry.notes.size());
        if(count>0)
            transportReviewDetail_=wrap(transportReviewDetail_+
                                       ((in.pressed&kKeyRight)?1:-1),count);
        return;
    }
    if(transportPhase_==TransportPhase::Source&&scanned_&&(in.pressed&kKeyY)) {openTransferHistory();return;}
    if(transportPhase_==TransportPhase::Source&&scanned_&&(in.pressed&kKeySelect)) {openTransportBankSource();return;}
    if(transportPhase_==TransportPhase::Destination&&(in.pressed&kKeySelect)) {openTransportBank();return;}
    if(transportPhase_==TransportPhase::SourceMon&&!transportPositions_.empty()&&(in.pressed&kKeyY)) {
        const int box=transportPositions_[transportCursor_].first;
        // Whole box selection replaces the current selection explicitly.
        transportSelected_.clear();
        for(size_t i=0;i<transportPositions_.size();++i) {
            const bool here=transportPositions_[i].first==box;
            transportRows_[i][1]=here?'X':' ';
            if(here) transportSelected_.push_back(transportPositions_[i]);
        }
        transportMessage_="Box selected: "+std::to_string(transportSelected_.size());return;
    }
    if((in.pressed&kKeyY)&&(transportPhase_==TransportPhase::Source||transportPhase_==TransportPhase::Destination)) {
        // Retry an incomplete first scan only. A completed session catalog
        // is never rescanned; restart the homebrew to discover external files.
        if(!scanned_) startTransportScan(transportPhase_==TransportPhase::Source);
        return;
    }
    if(in.pressed&kKeyB) {
        if(transportPhase_==TransportPhase::Review&&!transportAccepted_) {
            const auto focus=transportSource_;
            showTransportSaveList(true,"Choose another source save",focus);return;
        }
        if(transportPhase_==TransportPhase::DestinationSlot) {
            const auto focus=transportDest_;
            showTransportSaveList(false,"Choose destination save",focus);return;
        }
        if(transportPhase_==TransportPhase::SourceMon||transportPhase_==TransportPhase::Destination) {
            const auto focus=transportSource_;
            showTransportSaveList(true,"Choose source save",focus);return;
        }
        transportConfirm_=false;
        transportSave_.reset();transportReview_.reset();
        std::vector<uint8_t>().swap(transportSourceBytes_);
        std::vector<uint8_t>().swap(transportDestBytes_);
        transportRows_.clear();transportPositions_.clear();transportSaves_.clear();transportNotes_.clear();
        screen_=Screen::Home;
        return;
    }
#ifdef VPT_TRANSPORTER_WRITE
    if(transportPhase_==TransportPhase::Review && transportReview_ && transportAccepted_ &&
       (in.pressed&kKeyStart)) {
        if(transportConfirm_) {
            transportAnimating_=true;transportAnimationStart_=frame_;redraw_=true;
        }
        else {transportConfirm_=true;transportMessage_="START again to MOVE Pokemon";}
        return;
    }
#endif
    if(transportPhase_==TransportPhase::Review && !transportAccepted_ &&
       !transportSourceBytes_.empty() && transportEntries_.size()==transportReviewPairs_.size() &&
       (in.pressed&kKeyX)) {
        std::vector<pokemon::pccs_bridge::TransferPair> ready;
        for(size_t i=0;i<transportEntries_.size();++i)
            if(transportEntries_[i].ready) ready.push_back(transportReviewPairs_[i]);
        if(!ready.empty()&&ready.size()<transportReviewPairs_.size()) {
            // Explicitly re-review the subset, retaining original source/destination slots.
            transportReviewPairs_=std::move(ready);
            transportRepairIndices_.clear(); // old indices are now invalid
            pending_=Pending::TransferPreview;
        }
        return;
    }
    if(transportPhase_==TransportPhase::SourceMon && !transportBankSource_ && transportLanguage_!=1 &&
       (in.pressed&(kKeyLeft|kKeyRight))) {
        const uint8_t langs[]={2,3,5,4,7};
        int at=0;while(at<4&&langs[at]!=transportLanguage_) ++at;
        transportLanguage_=langs[(at+((in.pressed&kKeyRight)?1:4))%5];
        refreshTransportNames();
        return;
    }
    const int count=static_cast<int>(transportRows_.size());
    if(transportPhase_==TransportPhase::DestinationSlot && count>0) {
        // Move in the 6-column destination box, never to an occupied slot or
        // to a batch start that would exceed the verified available capacity.
        const auto here=transportPositions_[transportCursor_];
        auto nearestInBox=[&](int box,int slot,int exclude)->int {
            int best=-1,dist=1000000;
            for(int i=0;i<count;++i) {
                const auto candidate=transportPositions_[i];
                if(candidate.first!=box||i==exclude) continue;
                const int d=candidate.second>slot?candidate.second-slot:slot-candidate.second;
                if(d<dist) {dist=d;best=i;}
            }
            return best;
        };
        if(in.repeated&(kKeyL|kKeyR)) {
            const int direction=(in.repeated&kKeyR)?1:-1;
            const int limit=transportBankDest_?bank::Bank::kBoxCount:transportSave_->boxCount();
            for(int box=here.first+direction;box>=0&&box<limit;box+=direction) {
                const int candidate=nearestInBox(box,here.second,-1);
                if(candidate>=0) {transportCursor_=candidate;break;}
            }
        } else if(in.repeated&(kKeyLeft|kKeyRight)) {
            const int dir=(in.repeated&kKeyRight)?1:-1;
            for(int i=transportCursor_+dir;i>=0&&i<count;i+=dir) {
                if(transportPositions_[i].first!=here.first) break;
                if((dir>0 && transportPositions_[i].second>here.second) ||
                   (dir<0 && transportPositions_[i].second<here.second)) {
                    transportCursor_=i;break;
                }
            }
        } else if(in.repeated&(kKeyUp|kKeyDown)) {
            const int direction=(in.repeated&kKeyDown)?1:-1;
            const int target=here.second+direction*6;
            if(target>=0 && target<transportBoxSlots_) {
                // Prefer the matching column, then the nearest available
                // selectable start in the target row.
                int best=-1, distance=1000000;
                const int row=target/6;
                for(int i=0;i<count;++i) {
                    const auto candidate=transportPositions_[i];
                    if(candidate.first!=here.first||candidate.second/6!=row) continue;
                    const int d=candidate.second>target?candidate.second-target:target-candidate.second;
                    if(d<distance){distance=d;best=i;}
                }
                if(best>=0) transportCursor_=best;
            }
        }
    } else {
        if(in.repeated&kKeyUp) transportCursor_=wrap(transportCursor_-1,count);
        if(in.repeated&kKeyDown) transportCursor_=wrap(transportCursor_+1,count);
        if(transportPhase_==TransportPhase::SourceMon && count>0) {
            // L/R jumps between real origin boxes rather than skipping an
            // arbitrary 7 list entries; it does not change marked selections.
            const int direction=(in.repeated&kKeyR)?1:(in.repeated&kKeyL)?-1:0;
            if(direction) {
                const int box=transportPositions_[transportCursor_].first;
                for(int i=transportCursor_+direction;i>=0&&i<count;i+=direction) {
                    if((direction>0&&transportPositions_[i].first>box)||
                       (direction<0&&transportPositions_[i].first<box)) {
                        transportCursor_=i;break;
                    }
                }
            }
        } else {
            if(in.repeated&kKeyL) transportCursor_=clampInt(transportCursor_-7,0,std::max(0,count-1));
            if(in.repeated&kKeyR) transportCursor_=clampInt(transportCursor_+7,0,std::max(0,count-1));
        }
    }
    bool activate=in.pressed&kKeyA;
    int zone=in.touched?touchedZone(in):-1;
    if(zone>=kZoneItem && zone<kZoneItem+count) {
        activate=transportCursor_==zone-kZoneItem;
        transportCursor_=zone-kZoneItem;
    }
    if(transportPhase_==TransportPhase::SourceMon && (in.pressed&kKeyX) && count>0) {
        auto p=transportPositions_[transportCursor_];
        auto found=std::find(transportSelected_.begin(),transportSelected_.end(),p);
        if(found!=transportSelected_.end()) {
            transportSelected_.erase(found);transportRows_[transportCursor_][1]=' ';
        } else if(transportSelected_.size()<pokemon::pccs_bridge::kMaxBatch) {
            transportSelected_.push_back(p);transportRows_[transportCursor_][1]='X';
        }
        transportMessage_=std::to_string(transportSelected_.size())+" selected (max 30)";
        return;
    }
    if(!activate || count==0) return;
    switch(transportPhase_) {
        case TransportPhase::Edition:break;
        case TransportPhase::RepairConfirm:break; // handled before generic navigation
        case TransportPhase::History:break;
        case TransportPhase::Source: pending_=Pending::TransferLoadSource;break;
        case TransportPhase::SourceMon: {
            if(transportSelected_.empty()) transportSelected_.push_back(transportPositions_[transportCursor_]);
            transportMessage_=std::to_string(transportSelected_.size())+" Pokemon selected";
            showTransportSaveList(false,std::to_string(transportSelected_.size())+" Pokemon selected");
            break;
        }
        case TransportPhase::Destination:
            if(destinationReady(transportSaves_[transportCursor_],transportSelected_.size()))
                pending_=Pending::TransferLoadDestination;
            else transportMessage_="Destination unavailable - choose another or restart PKTool";
            break;
        case TransportPhase::DestinationSlot: pending_=Pending::TransferPreview;break;
        case TransportPhase::Review:
            if(transportReview_ && transportAccepted_) {
                if(transportReview_->originalsMatch()) {
                    transportMessage_="Exact review confirmed (read-only)";
                } else {
                    transportMessage_="Save changed; review invalidated";
                    transportAccepted_=false;transportReview_.reset();
                }
            } else if(!transportSourceBytes_.empty()&&!transportDestBytes_.empty()) {
                pending_=Pending::TransferPreview;
            }
            break; // No journal persistence in the preview build.
    }
}
void App::drawTransportSprite(Canvas& c,const pokemon::Pokemon& mon,int x,int y,int w,int h,uint8_t gen,bool enlarge,visual::VisualEdition edition) {
    // Resolve the artwork from the selected save edition, not the PK3 origin.
    // Old generation packs remain a fallback for missing/partial local art.
    const char* legacy=gen==1?"transport-gb":gen==2?(mon.shiny?"transport-gbc-shiny":"transport-gbc"):(mon.shiny?"transport-gba-shiny":"transport-gba");
    const char* exact=visual::spriteKind(edition,mon.shiny);
    auto im=sprites_.get(exact?exact:legacy,mon.species);
    if(!im.pixels&&exact) im=sprites_.get(legacy,mon.species);
    if(!im.pixels) im=sprites_.get(mon.shiny?"front-shiny":"front",mon.species);
    if(!im.pixels) {transferBall(c,x+w/2,y+h/2,9);return;}
    const int scale=std::max(1,std::max((im.width+w-1)/w,(im.height+h-1)/h));
    const int iw=(im.width+scale-1)/scale,ih=(im.height+scale-1)/scale;
    const int zoom=enlarge?std::max(1,std::min(2,std::min(w/iw,h/ih))):1;
    for(int j=0;j<ih;++j) for(int i=0;i<iw;++i) {
        auto px=im.pixels[(j*scale)*im.width+i*scale];
        if(!(px&0x8000)) continue;
        if(gen==1) {
            const int l=((px&31)*3+((px>>5)&31)*6+((px>>10)&31))/10;
            px=l<8?rgb(30,54,35):l<16?rgb(75,105,55):l<24?rgb(131,157,82):rgb(199,220,145);
        }
        const int dx=x+(w-iw*zoom)/2+i*zoom,dy=y+(h-ih*zoom)/2+j*zoom;
        if(zoom==1) c.pixel(dx,dy,px);
        else c.fillRect(dx,dy,zoom,zoom,px);
    }
}
void App::drawTransportAnimation(Canvas& top,Canvas& bottom) {
    const uint32_t duration=transportAnimationDuration();
    const uint32_t elapsed=std::min(duration-1,frame_-transportAnimationStart_);
    const size_t count=transportResultMons_.size();
    const bool finale=elapsed>=count*kLinkFramesPerPokemon;
    const size_t idx=count?std::min(count-1,static_cast<size_t>(elapsed/kLinkFramesPerPokemon)):0;
    const uint8_t generation=idx<transportEntries_.size()?transportEntries_[idx].mon.format:transportGeneration_;
    const uint32_t phase=finale?239:elapsed%kLinkFramesPerPokemon;
    const auto step=visual::linkStage(phase);
    const bool departure=step==visual::LinkStage::Departure;
    const bool outbound=step==visual::LinkStage::Outbound;
    const bool change=step==visual::LinkStage::Transform;
    const bool inbound=step==visual::LinkStage::Inbound;
    const bool arrival=step==visual::LinkStage::Arrival;
    const bool after=visual::destinationArt(phase);
    const char* stage=finale?"LINK SEQUENCE COMPLETE":departure?"READY TO DEPART":
                      outbound?"LEAVING GAME BOY":change?"TRANSFORMING SPRITE":
                      inbound?"ENTERING GBA":"ARRIVING ON GBA";
    const Color gb=generation==1?rgb(181,202,133):rgb(220,235,208);
    const Color gba=rgb(205,229,210),ink=rgb(30,54,35);
    top.clear(kBackground);bottom.clear(kBackground);
    top.text(kFontLarge,0,8,256,Align::Center,stage,kText);
    // VPT032: the actual source sprite travels to the centre transformer,
    // changes art during one flash and continues as the destination sprite.
    const auto srcModel=visual::console(transportSourceVisual_.edition);
    const auto dstModel=visual::console(transportDestVisual_.edition);
    const auto src=drawLinkConsole(top,srcModel,true);
    const auto dst=drawLinkConsole(top,dstModel,false);
    top.fillRect(92,83,72,5,kPanelDark);
    top.hline(92,85,72,kMuted);
    top.fillRect(90,79,5,13,kMuted);
    top.fillRect(161,79,5,13,kMuted);
    // The centre of the cable is a transformer, not the destination screen.
    top.roundRect(114,68,28,35,kBorder,kPanelDark,2);
    top.fillRect(119,73,18,24,rgb(77,99,128));
    top.hline(120,83,16,kAccent);
    top.hline(120,89,16,kAccent);
    if(count) {
        const auto& mon=transportResultMons_[idx];
        const auto& original=idx<transportEntries_.size()?transportEntries_[idx].mon:mon;
        if(departure) {
            drawTransportSprite(top,original,src.x,src.y,src.w,src.h,generation,false,transportSourceVisual_);
            top.brackets(src.x-2,src.y-2,src.w+4,src.h+4,kYellow,6);
        } else if(arrival) {
            drawTransportSprite(top,mon,dst.x,dst.y,dst.w,dst.h,3,false,transportDestVisual_);
            top.brackets(dst.x-2,dst.y-2,dst.w+4,dst.h+4,kGreen,6);
        } else if(outbound||inbound) {
            const int cx=visual::cableX(phase);
            top.hline(92,85,cx-91,kAccent);
            drawTransportSprite(top,outbound?original:mon,cx-11,73,22,24,outbound?generation:3,false,
                                outbound?transportSourceVisual_:transportDestVisual_);
        } else if(change) {
            const int t=static_cast<int>(phase-120);
            drawTransportSprite(top,after?mon:original,117,72,22,25,after?3:generation,false,
                                after?transportDestVisual_:transportSourceVisual_);
            const int radius=4+std::max(0,15-std::abs(t-15))/2;
            top.circle(128,85,radius,kYellow);
            top.blendRect(114,68,28,35,rgb(255,255,255),std::max(0,24-std::abs(t-15)*2));
        }
        top.text(kFontSmall,2,155,96,Align::Center,visual::consoleName(srcModel),kMuted);
        top.text(kFontSmall,159,155,96,Align::Center,visual::consoleName(dstModel),kMuted);
        top.text(kFontSmall,2,166,96,Align::Center,visual::label(transportSourceVisual_),kText);
        top.text(kFontSmall,159,166,96,Align::Center,visual::label(transportDestVisual_),kText);
        top.text(kFontSmall,0,180,256,Align::Center,finale?"Ready to write both saves":std::to_string(idx+1)+" / "+std::to_string(count)+" Pokemon",kAccent);

        // Lower screen focuses on one Pokemon at a time, with a larger
        // nearest-neighbour sprite and a deliberate reveal of the GBA art.
        bottom.roundRect(8,5,240,122,kBorder,after?gba:gb,2);
        if(outbound||inbound) {
            const int cx=visual::previewX(phase);
            bottom.hline(20,63,216,ink);
            bottom.roundRect(113,43,30,40,kBorder,kPanelDark,2);
            bottom.fillRect(119,49,18,28,rgb(77,99,128));
            drawTransportSprite(bottom,after?mon:original,cx-34,20,68,86,after?3:generation,true,
                                after?transportDestVisual_:transportSourceVisual_);
        } else if(change) {
            const int t=static_cast<int>(phase-120);
            drawTransportSprite(bottom,after?mon:original,54,10,148,112,after?3:generation,true,
                                after?transportDestVisual_:transportSourceVisual_);
            // Exactly one flash; the revealed sprite is already the GBA art.
            bottom.blendRect(10,7,236,118,rgb(255,255,255),std::max(0,26-std::abs(t-15)*2));
        } else {
            drawTransportSprite(bottom,after?mon:original,54,10,148,112,after?3:generation,true,
                                after?transportDestVisual_:transportSourceVisual_);
        }
        bottom.text(kFontLarge,8,130,240,Align::Center,finale?"All Pokemon prepared":after?mon.nickname:original.nickname,kText);
        const std::string detail=finale?"Saving begins after the animation":departure?"Original game sprite":
                                 outbound?"Sprite crosses first cable half":change?"Transformation flash":
                                 inbound?"GBA sprite crosses second half":"GBA sprite arrived";
        bottom.text(kFontSmall,8,146,240,Align::Center,detail,kMuted);
    }
    bottom.rect(13,161,230,8,kBorder);
    bottom.fillRect(15,163,static_cast<int>(226*elapsed/(duration-1)),4,kAccent);
    bottom.text(kFontSmall,0,178,256,Align::Center,"A / START: Skip animation",kMuted);
}

void App::drawTransporter(Canvas& top,Canvas& bottom) {
    if(transportAnimating_) {drawTransportAnimation(top,bottom);return;}
    if(transportPhase_==TransportPhase::RepairConfirm &&
       transportRepairFocus_<transportEntries_.size()) {
        const auto& candidate=transportEntries_[transportRepairFocus_];
        const auto& changes=candidate.repairChanges;
        drawChrome(top,"Repair for Transfer","PREVIEW",kYellow);
        top.roundRect(5,36,246,135,kBorder,kPanel);
        drawTransportSprite(top,candidate.mon,10,45,74,78,candidate.mon.format,false,transportSourceVisual_);
        marqueeActive_|=drawMarquee(top,93,48,150,Align::Left,candidate.mon.nickname,kText,frame_-marqueeStart_);
        marqueeActive_|=drawMarquee(top,93,66,150,Align::Left,candidate.error,kRed,frame_-marqueeStart_);
        top.text(kFontSmall,93,82,150,Align::Left,"Candidate verified",kGreen);
        top.text(kFontSmall,93,100,150,Align::Left,"PCCS LEGAL checked",kAccent);
        top.hline(12,128,232,kBorder);
        top.text(kFontSmall,11,135,234,Align::Left,"No repair written to source save.",kText);
        top.text(kFontSmall,11,150,234,Align::Left,"Transfer still removes the original.",kMuted);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        bottom.fillRect(0,0,256,22,kTitleBar);
        bottom.text(kFontSmall,7,4,244,Align::Left,
                    "PROPOSED CHANGES  "+std::to_string(changes.size()),kAccent);
        for(int row=0;row<3&&transportRepairScroll_+row<static_cast<int>(changes.size());++row) {
            const int index=transportRepairScroll_+row,y=27+40*row;
            drawUnovaTile(bottom,5,y,246,37,row==0);
            marqueeActive_|=drawMarquee(bottom,12,y+7,228,Align::Left,
                        std::to_string(index+1)+". "+changes[index],row==0?kText:kMuted,frame_-marqueeStart_);
        }
        bottom.text(kFontSmall,6,151,244,Align::Center,
                    "Not an official Nintendo transfer",kYellow);
        drawHints(bottom,{{"A","Apply"},{"B","Cancel"},{"+","Scroll"}},{kKeyA,kKeyB,0});
        return;
    }
    if(transportPhase_==TransportPhase::Edition) {
        drawChrome(top,"Confirm game edition","VISUAL ONLY",kAccent);
        top.roundRect(5,35,246,137,kBorder,kPanel);
        marqueeActive_|=drawMarquee(top,8,43,240,Align::Center,io::fileName(transportLoadPath_),kText,frame_-marqueeStart_);
        top.text(kFontSmall,8,63,240,Align::Center,transportMessage_,kYellow);
        top.text(kFontSmall,8,82,240,Align::Center,"Save family cannot identify edition",kMuted);
        if(!transportRows_.empty()) {
            const auto e=transportEditionResolution_.choices[transportCursor_];
            top.text(kFontLarge,8,106,240,Align::Center,visual::label({e,transportSave_->language()==1}),kText);
            top.text(kFontSmall,8,132,240,Align::Center,visual::consoleName(visual::console(e)),kAccent);
        }
        top.text(kFontSmall,8,151,240,Align::Center,"Choice remembered until restart",kMuted);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        for(int row=0;row<5 && row<static_cast<int>(transportRows_.size());++row) {
            const int y=8+row*29;
            drawUnovaTile(bottom,5,y,246,27,row==transportCursor_);
            if(row==transportCursor_)
                marqueeActive_|=drawMarquee(bottom,13,y+5,230,Align::Left,transportRows_[row],kText,frame_-marqueeStart_);
            else bottom.text(kFontSmall,13,y+5,230,Align::Left,transportRows_[row],kMuted);
            zones_.push_back({5,y,246,27,kZoneItem+row});
        }
        drawHints(bottom,{{"A","Confirm"},{"+","Move"},{"B","Back"}},{kKeyA,0,kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::History) {
        drawChrome(top,"Transfer history","READ ONLY",kAccent);
        top.text(kFontLarge,0,39,256,Align::Center,transportHistoryPaths_.empty()?"No archived transfers":std::to_string(transportHistoryIndex_+1)+" / "+std::to_string(transportHistoryPaths_.size()),kText);
        marqueeActive_|=drawMarquee(top,8,69,240,Align::Left,"From: "+io::fileName(transportHistory_.source),kText,frame_-marqueeStart_);
        marqueeActive_|=drawMarquee(top,8,87,240,Align::Left,"To: "+io::fileName(transportHistory_.destination),kText,frame_-marqueeStart_);
        top.text(kFontSmall,0,113,256,Align::Center,std::to_string(transportHistory_.pokemon.size())+" Pokemon",kMuted);
        top.text(kFontSmall,0,141,256,Align::Center,"LEFT / RIGHT: transfer",kMuted);
        bottom.clear(kBackground);
        const size_t start=static_cast<size_t>(transportResultPage_)*10;
        for(size_t i=start;i<std::min(start+10,transportHistory_.pokemon.size());++i) {
            const int x=5+static_cast<int>((i-start)%5)*50,y=16+static_cast<int>((i-start)/5)*57;
            bottom.roundRect(x,y,46,54,kBorder,kPanel);
            drawTransportSprite(bottom,transportHistory_.pokemon[i],x+2,y+1,42,38,3);
            bottom.text(kFontSmall,x,y+40,46,Align::Center,transportHistory_.pokemon[i].nickname,kText);
        }
        bottom.text(kFontSmall,0,142,256,Align::Center,"L/R: page | archived snapshots",kMuted);
        drawHints(bottom,{{"B","Back"}},{kKeyB});return;
    }
    if(transportBatch_) {
        const auto done=transportBatch_->completed(),total=transportBatch_->total();
        drawChrome(top,"Checking transfer","READ ONLY",kAccent);
        top.roundRect(5,35,246,137,kBorder,kPanel);
        top.text(kFontLarge,8,48,240,Align::Center,done==total?"Verifying saves":"Checking Pokemon",kText);
        top.text(kFontLarge,8,75,240,Align::Center,std::to_string(done)+" / "+std::to_string(total),kAccent);
        top.roundRect(19,104,218,13,kBorder,kPanelDark);
        top.fillRect(22,107,static_cast<int>(212*done/std::max<size_t>(1,total)),7,kGreen);
        if(done<total) {
            const auto pos=transportReviewPairs_[done].from;
            top.text(kFontSmall,8,135,240,Align::Center,"Next: "+slotLabel(pos.box,pos.slot),kText);
        }
        bottom.clear(kBackground);
        const auto& entries=transportBatch_->entries();
        const size_t start=std::min(done,total?total-1:0)/10*10;
        for(size_t i=start;i<std::min(start+10,total);++i) {
            const int x=5+static_cast<int>((i-start)%5)*50,y=25+static_cast<int>((i-start)/5)*48;
            const Color color=i<entries.size()?(entries[i].ready?kGreen:kRed):i==done?kYellow:kBorder;
            bottom.roundRect(x,y,46,43,color,kPanel,2);
            if(i<entries.size()&&entries[i].mon.species) drawIcon(bottom,x+4,y+1,38,28,entries[i].mon,false);
            bottom.text(kFontSmall,x,y+29,46,Align::Center,i<entries.size()?(entries[i].ready?"OK":"BLOCK"):std::to_string(i+1),color);
        }
        bottom.text(kFontSmall,0,141,256,Align::Center,"No save writes during review",kMuted);
        drawHints(bottom,{{"B","Cancel"}},{kKeyB});
        return;
    }
    if(transporterBusy()) {
        drawChrome(top,"Poke Transporter","READING",kAccent);
        top.roundRect(5,36,246,135,kBorder,kPanel);
        const bool scan=bool(transportScan_);
        top.text(kFontLarge,8,48,240,Align::Center,scan?"Finding saves":"Reading boxes",kText);
        const auto status=scan?std::to_string(transportScan_->examined())+" checked / "+std::to_string(transportScan_->found())+" found":
            "Box "+std::to_string(transportLoadBox_+1)+" / "+std::to_string(transportSave_->boxCount());
        top.text(kFontSmall,8,74,240,Align::Center,status,kText);
        if(!scan) marqueeActive_|=drawMarquee(top,8,94,240,Align::Center,io::fileName(transportLoadPath_),kMuted,frame_-marqueeStart_);
        top.roundRect(19,121,218,13,kBorder,kPanelDark);
        if(scan) top.fillRect(22+static_cast<int>((frame_*3)%183),124,30,7,kAccent);
        else top.fillRect(22,124,212*transportLoadBox_/std::max(1,transportSave_->boxCount()),7,kAccent);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        bottom.fillRect(0,0,256,24,kTitleBar);
        bottom.fillRect(0,0,16,23,kCursor);
        bottom.text(kFontLarge,25,5,227,Align::Left,"POKEDEX / LOADING",kText);
        bottom.roundRect(12,40,232,99,kBorder,kPanel);
        bottom.text(kFontLarge,20,59,216,Align::Center,
                    scan?"INDEXING SAVES":"PREPARING POKEMON BOXES",kAccent);
        bottom.text(kFontSmall,20,92,216,Align::Center,
                    "Please keep the SD card inserted",kText);
        bottom.text(kFontSmall,20,116,216,Align::Center,
                    "Read only - no save changes",kMuted);
        drawHints(bottom,{{"B","Cancel"}},{kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::Source) {
        drawChrome(top,"GB/GBC / SOURCE",std::to_string(transportSaves_.size())+" SAVES",kAccent);
        top.roundRect(5,36,246,136,kBorder,kPanel);
        top.text(kFontLarge,9,45,238,Align::Center,"Select original Game Boy save",kText);
        top.hline(11,68,234,kBorder);
        top.text(kFontSmall,11,79,234,Align::Left,"Red / Green / Blue / Yellow",kAccent);
        top.text(kFontSmall,11,97,234,Align::Left,"Gold / Silver / Crystal",kAccent);
        top.text(kFontSmall,11,122,234,Align::Left,
                 "Game edition sprites are selected later",kMuted);
        marqueeActive_|=drawMarquee(top,11,147,234,Align::Left,transportMessage_,kYellow,frame_-marqueeStart_);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+5) transportScroll_=transportCursor_-4;
        for(int row=0;row<5&&transportScroll_+row<static_cast<int>(transportSaves_.size());++row) {
            const int index=transportScroll_+row,y=5+33*row;
            drawUnovaTile(bottom,4,y,248,31,index==transportCursor_);
            const auto title=io::fileName(transportSaves_[index].path);
            if(index==transportCursor_)
                marqueeActive_|=drawMarquee(bottom,12,y+2,232,Align::Left,title,kText,frame_-marqueeStart_);
            else bottom.text(kFontSmall,12,y+2,232,Align::Left,title,kText);
            bottom.text(kFontSmall,12,y+17,232,Align::Left,
                        transportSaves_[index].trainer,kMuted);
            zones_.push_back({4,y,248,31,kZoneItem+index});
        }
        if(transportSaves_.empty())
            bottom.text(kFontSmall,5,75,246,Align::Center,"No Gen I / II saves found",kYellow);
        drawHints(bottom,{{"A","Select"},{"SELECT","Bank"},{"Y","History"},{"B","Back"}},
                  {kKeyA,kKeySelect,kKeyY,kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::SourceMon&&!transportMons_.empty()) {
        const auto& p=transportMons_[transportCursor_];
        const auto position=transportPositions_[transportCursor_];
        const uint8_t gen=transportBankSource_?p.format:transportGeneration_;
        drawChrome(top,transportBankSource_?"BANK / SOURCE":visual::label(transportSourceVisual_)+" / PC",
                   std::to_string(transportSelected_.size())+"/30",kAccent);
        top.roundRect(5,36,246,136,kBorder,kPanel);
        top.roundRect(11,43,93,90,kBorder,kPanelDark);
        // Source sprites remain 100% edition-accurate; only their frame changes.
        drawTransportSprite(top,p,14,46,87,83,gen,false,transportSourceVisual_);
        marqueeActive_|=drawMarquee(top,113,46,130,Align::Left,p.nickname,kText,frame_-marqueeStart_);
        top.text(kFontSmall,113,65,130,Align::Left,"Lv "+std::to_string(p.level),kAccent);
        top.text(kFontSmall,113,82,130,Align::Left,slotLabel(position.first,position.second),kMuted);
        if(p.shiny) top.text(kFontSmall,113,101,130,Align::Left,"SHINY",kYellow);
        top.hline(11,137,234,kBorder);
        const auto language=sourceLanguageLabel(transportBankSource_?p.language:transportLanguage_,
                                                transportBankSource_?"":transportSource_);
        marqueeActive_|=drawMarquee(top,11,141,234,Align::Left,"Source language: "+language,kText,frame_-marqueeStart_);
        top.text(kFontSmall,11,155,234,Align::Left,
                 transportBankSource_?"Stored per Pokemon":transportLanguage_==1?
                     "Japanese save layout":"Left/Right: adjust if misidentified",kMuted);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        bottom.fillRect(0,0,256,21,kTitleBar);
        bottom.hline(0,20,256,kBorder);
        bottom.text(kFontSmall,8,4,239,Align::Left,
                    "BOX "+twoDigits(position.first+1)+"   |   "+std::to_string(transportSelected_.size())+" / 30 SELECTED",kAccent);
        constexpr int visible=6, rowHeight=25;
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+visible) transportScroll_=transportCursor_-visible+1;
        for(int i=0;i<visible && transportScroll_+i<static_cast<int>(transportRows_.size());++i) {
            const int index=transportScroll_+i,y=23+rowHeight*i;
            const bool active=index==transportCursor_;
            const auto& pos=transportPositions_[index];
            const bool marked=std::find(transportSelected_.begin(),transportSelected_.end(),pos)!=transportSelected_.end();
            drawUnovaTile(bottom,5,y,246,rowHeight-2,active);
            bottom.roundRect(11,y+5,12,12,marked?kGreen:kDim,kPanelDark);
            if(marked) bottom.text(kFontSmall,11,y+4,12,Align::Center,"X",kGreen);
            const auto& mon=transportMons_[index];
            pokemon::Pokemon icon;icon.species=mon.species;
            // Icons may be 32x32; clip them to the compact 22x20 list slot
            // so they never paint over the mark box or adjacent rows.
            bottom.setClip(26,y+1,22,20);
            drawIcon(bottom,26,y+1,22,20,icon,false);
            bottom.resetClip();
            const std::string label=slotLabel(pos.first,pos.second)+" "+mon.nickname+" Lv"+std::to_string(mon.level);
            const Color ink=active?kText:marked?kGreen:kMuted;
            if(active) marqueeActive_|=drawMarquee(bottom,51,y+4,194,Align::Left,label,ink,frame_-marqueeStart_);
            else bottom.text(kFontSmall,51,y+4,194,Align::Left,label,ink);
            zones_.push_back({5,y,246,rowHeight-2,kZoneItem+index});
        }
        drawHints(bottom,{{"X","Mark"},{"Y","Box"},{"A","Next"},{"B","Back"}},{kKeyX,kKeyY,kKeyA,kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::DestinationSlot&&!transportPositions_.empty()) {
        const auto position=transportPositions_[transportCursor_];
        const int box=position.first;
        const int count=static_cast<int>(transportRows_.size());
        const int boxCount=transportBankDest_?bank::Bank::kBoxCount:transportSave_->boxCount();
        drawChrome(top,transportBankDest_?"BANK / DESTINATION":"GBA / DESTINATION","SELECT SLOT",kAccent);
        top.roundRect(5,36,246,136,kBorder,kPanel);
        top.text(kFontLarge,10,44,236,Align::Left,
                 std::string(transportBankDest_?"BANK":"PC")+" BOX "+twoDigits(box+1)+"/"+twoDigits(boxCount),kText);
        top.hline(11,66,233,kBorder);
        top.text(kFontSmall,10,73,236,Align::Left,visual::label(transportDestVisual_),kAccent);
        top.text(kFontSmall,10,90,236,Align::Left,"Start slot: "+slotLabel(box,position.second),kText);
        top.text(kFontSmall,10,105,236,Align::Left,"Batch: "+std::to_string(transportSelected_.size())+" Pokemon",kText);
        top.text(kFontSmall,10,120,236,Align::Left,
                 std::to_string(transportPositions_.size())+" free slots in destination",kMuted);
        top.text(kFontSmall,10,140,236,Align::Left,"Cyan = available   Red = cursor",kMuted);
        marqueeActive_|=drawMarquee(top,10,155,236,Align::Left,transportMessage_,kYellow,frame_-marqueeStart_);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        for(int i=0;i<transportBoxSlots_;++i) {
            const int x=8+(i%6)*40,y=9+(i/6)*32;
            const auto sp=transportBoxSpecies_[box*transportBoxSlots_+i];
            const auto it=std::find(transportPositions_.begin(),transportPositions_.begin()+count,
                                    std::make_pair(box,i));
            const bool fits=it!=transportPositions_.begin()+count;
            const bool selected=i==position.second;
            bottom.roundRect(x,y,38,30,selected?kCursor:fits?kAccent:kDim,
                             selected?kSelectedRow:kPanelDark,selected?2:1);
            if(sp && sp!=0xffff) {
                pokemon::Pokemon mon;mon.species=sp;
                drawIcon(bottom,x+1,y+1,36,27,mon,false);
            } else if(sp==0xffff) {
                bottom.text(kFontSmall,x,y+6,38,Align::Center,"!",kRed);
            } else {
                bottom.fillCircle(x+19,y+14,3,fits?kGreen:kDim);
            }
            if(fits) zones_.push_back({x,y,38,30,kZoneItem+static_cast<int>(it-transportPositions_.begin())});
        }
        drawHints(bottom,{{"A","Place"},{"L/R","Box"},{"B","Back"}},{kKeyA,0,kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::Review&&!transportEntries_.empty()) {
        const auto& e=transportEntries_[transportCursor_];
        const int ready=static_cast<int>(std::count_if(transportEntries_.begin(),transportEntries_.end(),[](const auto& r){return r.ready;}));
        const int blocked=static_cast<int>(transportEntries_.size())-ready;
        drawChrome(top,"Transfer review",transportAccepted_?"READY":"CHECK",transportAccepted_?kGreen:kYellow);
        top.roundRect(5,35,246,137,kBorder,kPanel);
        drawTransportSprite(top,e.mon,10,46,76,77,e.mon.format,false,transportSourceVisual_);
        marqueeActive_ |= drawMarquee(top,91,45,152,Align::Left,e.mon.nickname,kText,frame_-marqueeStart_);
        top.text(kFontSmall,91,65,152,Align::Left,e.ready?(e.returnedItem?"ITEM TO BAG":"READY"):"BLOCKED",e.ready?(e.returnedItem?kYellow:kGreen):kRed);
        const std::string primaryError=(!e.ready&&!e.notes.empty() &&
                                         e.notes.front().rfind("ERROR: ",0)==0)
                                        ?e.notes.front().substr(7):e.error;
        marqueeActive_ |= drawMarquee(top,91,84,152,Align::Left,
                 e.ready?(e.returnedItem?e.itemName+" x1":"Can transfer"):primaryError,
                 e.ready?kText:kRed,frame_-marqueeStart_);
        top.text(kFontSmall,9,109,238,Align::Center,visual::label(transportSourceVisual_)+" -> "+visual::label(transportDestVisual_),kAccent);
        if(!e.notes.empty()) {
            const size_t idx=static_cast<size_t>(wrap(transportReviewDetail_,
                                              static_cast<int>(e.notes.size())));
            const auto& finding=e.notes[idx];
            const Color c=finding.rfind("ERROR:",0)==0?kRed:
                          finding.rfind("WARN:",0)==0?kYellow:kMuted;
            marqueeActive_ |= drawMarquee(top,9,126,238,Align::Left,
                  std::to_string(idx+1)+"/"+std::to_string(e.notes.size())+
                  " "+finding,c,frame_-marqueeStart_);
        }
        marqueeActive_ |= drawMarquee(top,9,147,238,Align::Center,transportMessage_,transportAccepted_?kGreen:kYellow,frame_-marqueeStart_);
        bottom.clear(kBackground);
        bottom.text(kFontSmall,4,3,248,Align::Center,std::to_string(ready)+" ready / "+std::to_string(blocked)+" blocked",kText);
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+5) transportScroll_=transportCursor_-4;
        for(int row=0;row<5&&transportScroll_+row<static_cast<int>(transportEntries_.size());++row) {
            const int index=transportScroll_+row,y=20+30*row;
            const auto& r=transportEntries_[index];
            const Color color=r.ready?(r.returnedItem?kYellow:kGreen):kRed;
            drawUnovaTile(bottom, 4, y, 248, 28, index==transportCursor_);
            if(r.mon.species) drawIcon(bottom,7,y,35,28,r.mon,false);
            bottom.text(kFontSmall,45,y+3,135,Align::Left,r.mon.nickname,kText);
            bottom.text(kFontSmall,180,y+3,66,Align::Right,r.ready?(r.returnedItem?"BAG":"OK"):"BLOCK",color);
            const auto detail=r.ready?(r.returnedItem?r.itemName+" -> Bag":"Ready to move"):r.error;
            if(index==transportCursor_) marqueeActive_ |= drawMarquee(bottom,45,y+15,199,Align::Left,detail,color,frame_-marqueeStart_);
            else bottom.text(kFontSmall,45,y+15,199,Align::Left,detail,color);
            zones_.push_back({4,y,248,28,kZoneItem+index});
        }
#ifdef VPT_TRANSPORTER_WRITE
        if(transportAccepted_) drawHints(bottom,{{"START","Move"},{"B","Cancel"}},{kKeyStart,kKeyB});
        else
#endif
        if(!e.ready && e.notes.size()>1)
            bottom.text(kFontSmall,10,170,236,Align::Left,"LEFT / RIGHT: diagnostics",kMuted);
        if(!e.ready&&e.repairAvailable && ready>0)
            drawHints(bottom,{{"Y","Repair"},{"X","Ready"},{"A","Retry"},{"B","Saves"}},
                      {kKeyY,kKeyX,kKeyA,kKeyB});
        else if(!e.ready&&e.repairAvailable)
            drawHints(bottom,{{"Y","Repair"},{"A","Retry"},{"B","Saves"}},{kKeyY,kKeyA,kKeyB});
        else if(ready>0&&blocked>0)
            drawHints(bottom,{{"X","Ready only"},{"A","Retry"},{"B","Saves"}},{kKeyX,kKeyA,kKeyB});
        else drawHints(bottom,{{"A","Retry"},{"B","Saves"}},{kKeyA,kKeyB});
        return;
    }
    drawChrome(top,"Poke Transporter", "PCCS LEGAL",kAccent);
    top.roundRect(5,kBodyTop+3,246,136,kBorder,kPanel);
    if(transportPhase_==TransportPhase::Destination&&!transportSaves_.empty()) {
        const auto needed=transportSelected_.size();
        const auto& s=transportSaves_[transportCursor_];
        const bool ready=destinationReady(s,needed);
        const auto status=destinationStatus(s,needed);
        const Color color=ready?kGreen:kRed;
        drawChrome(top,"Select GBA save",status,color);
        top.roundRect(5,35,246,137,kBorder,kPanel);
        pokemon::Pokemon cover;cover.species=saves::coverSpecies(s.version);
        drawTransportSprite(top,cover,8,44,76,76,3);
        marqueeActive_|=drawMarquee(top,87,45,155,Align::Left,io::fileName(s.path),kText,frame_-marqueeStart_);
        top.text(kFontSmall,87,65,155,Align::Left,s.trainer,kMuted);
        top.text(kFontLarge,87,85,155,Align::Left,std::to_string(std::max(0,s.freeBoxSlots))+" free",color);
        top.text(kFontSmall,87,106,155,Align::Left,std::to_string(needed)+" Pokemon selected",kText);
        const auto detail=ready?"Enough space in PC":status=="INVALID"?"Save data not valid":"Not enough empty slots";
        top.text(kFontSmall,8,131,240,Align::Center,detail,color);
        marqueeActive_|=drawMarquee(top,8,149,240,Align::Center,transportMessage_,kMuted,frame_-marqueeStart_);
        bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+5) transportScroll_=transportCursor_-4;
        for(int row=0;row<5&&transportScroll_+row<static_cast<int>(transportSaves_.size());++row) {
            const int index=transportScroll_+row,y=5+33*row;
            const auto& item=transportSaves_[index];
            const bool ok=destinationReady(item,needed);
            const Color c=ok?kGreen:kRed;
            drawUnovaTile(bottom, 4, y, 248, 31, index==transportCursor_);
            bottom.fillCircle(12,y+8,3,c);
            const auto filename=io::fileName(item.path);
            if(index==transportCursor_) marqueeActive_|=drawMarquee(bottom,21,y+2,224,Align::Left,filename,kText,frame_-marqueeStart_);
            else bottom.text(kFontSmall,21,y+2,224,Align::Left,filename,kText);
            bottom.text(kFontSmall,21,y+16,224,Align::Left,destinationStatus(item,needed)+" | "+std::to_string(std::max(0,item.freeBoxSlots))+" free",c);
            zones_.push_back({4,y,248,31,kZoneItem+index});
        }
        if(!scanned_) drawHints(bottom,{{"Y","Retry"},{"B","Back"}},{kKeyY,kKeyB});
        else if(transportBankSource_) drawHints(bottom,{{"A","Select"},{"B","Back"}},{kKeyA,kKeyB});
        else drawHints(bottom,{{"A","Select"},{"SELECT","Bank"},{"B","Back"}},{kKeyA,kKeySelect,kKeyB});
        return;
    }
    const char* phase="Select GB/GBC save";
    switch(transportPhase_) {
        case TransportPhase::Edition:break;
        case TransportPhase::History:break;
        case TransportPhase::Source:break;
        case TransportPhase::SourceMon:phase="Select boxed Pokemon";break;
        case TransportPhase::Destination:phase="Select GBA save";break;
        case TransportPhase::DestinationSlot:phase="Select first empty slot";break;
        case TransportPhase::Review:phase=transportAccepted_?"Preview accepted":"Preview refused";break;
        case TransportPhase::RepairConfirm:phase="Repair preview";break;
    }
    top.text(kFontLarge,9,45,238,Align::Center,phase,kText);
    top.text(kFontSmall,9,66,238,Align::Center,"Gen I/II -> Gen III | 6 languages",kMuted);
    marqueeActive_ |= drawMarquee(top,9,84,238,Align::Left,"From: "+transportSource_,kText,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,9,99,238,Align::Left,"To: "+transportDest_,kText,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,9,116,238,Align::Center,transportMessage_,
        transportPhase_==TransportPhase::Review?(transportAccepted_?kGreen:kRed):kMuted,frame_-marqueeStart_);
#ifdef VPT_TRANSPORTER_WRITE
    top.text(kFontSmall,9,136,238,Align::Center,"Experimental - disposable saves only",kYellow);
#else
    top.text(kFontSmall,9,136,238,Align::Center,"READ ONLY - no save changes",kYellow);
#endif
    top.text(kFontSmall,9,151,238,Align::Center,"Unofficial conversion",kMuted);
    bottom.grid(0,0,256,kHintTop,kBackground,kGridLine,12);
    if(transportRows_.empty()) bottom.text(kFontLarge,0,80,256,Align::Center,"No eligible entries",kMuted);
    else {
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+7) transportScroll_=transportCursor_-6;
        for(int row=0;row<7&&transportScroll_+row<static_cast<int>(transportRows_.size());++row) {
            const int index=transportScroll_+row,y=8+row*22;
            drawUnovaTile(bottom,5,y,246,21,index==transportCursor_);
            bottom.text(kFontSmall,11,y+3,234,Align::Left,transportRows_[index],
                        index==transportCursor_?kText:kMuted);
            zones_.push_back({5,y,246,21,kZoneItem+index});
        }
    }
    if(transportPhase_==TransportPhase::Review) {
#ifdef VPT_TRANSPORTER_WRITE
        if(transportAccepted_) drawHints(bottom,{{"A","Recheck"},{"START","Move"},{"B","Cancel"}},{kKeyA,kKeyStart,kKeyB});
        else
#endif
        drawHints(bottom,{{"A","Recheck"},{"B","Saves"}},{kKeyA,kKeyB});
    }
    else if(transportPhase_==TransportPhase::SourceMon)
        drawHints(bottom,{{"X","Mark"},{"Y","Box"},{"A","Next"},{"B","Back"}},{kKeyX,kKeyY,kKeyA,kKeyB});
    else if(transportPhase_==TransportPhase::Source||transportPhase_==TransportPhase::Destination)
        if(!scanned_) drawHints(bottom,{{"Y","Retry"},{"B","Back"}},{kKeyY,kKeyB});
        else if(transportPhase_==TransportPhase::Source) drawHints(bottom,{{"A","Save"},{"SELECT","Bank"},{"Y","History"},{"B","Back"}},{kKeyA,kKeySelect,kKeyY,kKeyB});
        else if(transportBankSource_) drawHints(bottom,{{"A","Select"},{"B","Back"}},{kKeyA,kKeyB});
        else drawHints(bottom,{{"A","Select"},{"SELECT","Bank"},{"B","Back"}},{kKeyA,kKeySelect,kKeyB});
    else drawHints(bottom,{{"A","Select"},{"+","Move"},{"B","Back"}},{kKeyA,0,kKeyB});
}
}
#endif
