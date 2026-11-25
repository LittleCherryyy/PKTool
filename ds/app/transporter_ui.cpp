// SPDX-License-Identifier: GPL-3.0-or-later
// Optional PCCS preview and separately enabled experimental write flow.
#ifdef VPT_TRANSPORTER_PREVIEW
#include "ds/app/app.h"
#include "ds/app/ui_common.h"
#include "core/io/file_io.h"
#include "core/saves/save_commit.h"
#include "core/pokemon/pk3.h"
#include "experiments/pccs/transfer_journal.h"
#include <array>
#include <algorithm>

namespace pktool::ui {
using namespace detail;
namespace {
constexpr size_t kMaxSave = 160 * 1024;
bool readImage(const std::string& path,std::vector<uint8_t>& bytes) {
    auto size=io::fileSize(path);
    return size>0 && static_cast<size_t>(size)<=kMaxSave &&
           io::readFile(path,bytes) && bytes.size()==static_cast<size_t>(size);
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
    drawChrome(top,"Poke Transporter","RECOVERY",kYellow);
    top.roundRect(5,kBodyTop+3,246,136,kBorder,kPanel);
    top.text(kFontLarge,8,45,240,Align::Center,"Transfer locked",kYellow);
    marqueeActive_ |= drawMarquee(top,8,68,240,Align::Center,transferStartup_.message,kText,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,8,87,240,Align::Left,"From: "+transferStartup_.source,kMuted,frame_-marqueeStart_);
    marqueeActive_ |= drawMarquee(top,8,101,240,Align::Left,"To: "+transferStartup_.destination,kMuted,frame_-marqueeStart_);
    top.text(kFontSmall,8,124,240,Align::Center,"Do not open either save in a game.",kYellow);
    top.text(kFontSmall,8,140,240,Align::Center,"Keep journal and sidecar files.",kMuted);
    bottom.clear(kBackground);
    bottom.text(kFontLarge,0,44,256,Align::Center,"Editor blocked",kText);
#ifdef VPT_TRANSPORTER_WRITE
    using pokemon::pccs_bridge::StartupState;
    bottom.text(kFontSmall,0,70,256,Align::Center,
        transferStartup_.state==StartupState::Pending?"X: Resume recorded transfer":
        transferStartup_.state==StartupState::Completed?"X: Archive completed transfer":"Conflicting files: no writes",kYellow);
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
    scanned_=false;saveFiles_.clear();
    if(refreshTransferGate()) showMessage("Transporter",r.message);
    else transferStartup_.message=r.message;
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
    sprites_.clear();
    transportSave_.reset();
    transportSourceBytes_.clear();transportDestBytes_.clear();transportReview_.reset();
    transportSource_.clear();transportDest_.clear();
    transportRows_.clear();transportNotes_.clear();transportPositions_.clear();
    transportMessage_.clear();transportAccepted_=false;transportConfirm_=false;
    transportSaves_=saves::findPokemonSaves(config_.scanRoot,3);
    transportSaves_.erase(std::remove_if(transportSaves_.begin(),transportSaves_.end(),
        [](const saves::SaveSummary& s){return s.game!=saves::Game::Crystal;}),
        transportSaves_.end());
    for(const auto& s:transportSaves_) transportRows_.push_back(io::fileName(s.path)+" | "+s.trainer);
    transportPhase_=TransportPhase::Source;
    transportCursor_=transportScroll_=0;
    screen_=Screen::Transporter;
}
void App::transporterSelectSave(bool source) {
    if(transportCursor_<0 || transportCursor_>=static_cast<int>(transportSaves_.size())) return;
    auto path=transportSaves_[transportCursor_].path;
#ifdef ARM9
    if(!path.empty() && path[0]=='/') path="sd:"+path;
#endif
    std::vector<uint8_t> bytes;
    if(!readImage(path,bytes)) { showMessage("Transporter","Cannot read save.");return; }
    auto loaded=saves::loadSaveFile(bytes);
    if(!loaded || !saves::validateSave(*loaded) || loaded->language()!=2 ||
       loaded->game()!=(source?saves::Game::Crystal:saves::Game::RubySapphire)) {
        showMessage("Transporter","Unsupported or invalid save.\nEnglish Crystal / Ruby-Sapphire only.");
        return;
    }
    if(source) {
        transportSource_=path; transportSourceBytes_=std::move(bytes);
    } else { transportDest_=path; transportDestBytes_=std::move(bytes); }
    transportSave_=std::move(loaded);
    transportRows_.clear();transportPositions_.clear();
    for(int b=0;b<transportSave_->boxCount();++b) {
        for(int s=0;s<transportSave_->boxSlotCount();++s) {
            std::array<uint8_t,pokemon::entity::kMaxPartySize> raw{};
            auto state=transportSave_->readBoxSlot(b,s,raw.data());
            if(source && state==saves::SlotState::Valid) {
                auto p=pokemon::entity::decode(2,raw.data(),transportSave_->storedSize());
                transportRows_.push_back(slotLabel(b,s)+" "+p.nickname+" Lv"+std::to_string(p.level));
                transportPositions_.emplace_back(b,s);
            } else if(!source && state==saves::SlotState::Empty) {
                transportRows_.push_back(slotLabel(b,s)+" Empty");
                transportPositions_.emplace_back(b,s);
            }
        }
    }
    transportPhase_=source?TransportPhase::SourceMon:TransportPhase::DestinationSlot;
    transportCursor_=transportScroll_=0;
}
void App::transporterPreview() {
    auto to=transportPositions_[transportCursor_];
    transportSave_.reset();sprites_.clear();
    transportNotes_.clear();transportAccepted_=false;
    transportReview_=pokemon::pccs_bridge::ReviewedTransfer::create(
        transportSource_,transportDest_,transportSourceBytes_,transportDestBytes_,
        {transportFromBox_,transportFromSlot_},{to.first,to.second},
        transportMessage_,transportNotes_);
    transportAccepted_=bool(transportReview_);
    if(transportReview_) {
        transportMessage_="Conversion preview passed";
        const auto& raw=transportReview_->converted();
        auto p=pokemon::pk3::toPokemon(raw.data(),raw.size());
        transportNotes_.insert(transportNotes_.begin(),p.nickname+" Lv"+std::to_string(p.level)+
            " -> "+slotLabel(to.first,to.second));
        transportNotes_.insert(transportNotes_.begin(),"OT: "+p.trainer.name+"  TID: "+std::to_string(p.trainer.tid));
        transportNotes_.insert(transportNotes_.begin(),"PID: "+std::to_string(p.pid)+"  SID: "+std::to_string(p.trainer.sid));
    }
    std::vector<uint8_t>().swap(transportSourceBytes_);
    std::vector<uint8_t>().swap(transportDestBytes_);
    if(!transportAccepted_) {
        // Keep complete diagnostic sentences as selectable scrolling rows.
        transportNotes_.insert(transportNotes_.begin(),transportMessage_);
    }
    transportRows_=transportNotes_;
    transportPhase_=TransportPhase::Review;
    transportCursor_=transportScroll_=0;
}
void App::updateTransporter(const Input& in) {
    if(in.pressed&kKeyB) {
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
        if(transportConfirm_) commitTransporter();
        else {transportConfirm_=true;transportMessage_="START again to MOVE Pokemon";}
        return;
    }
#endif
    int count=static_cast<int>(transportRows_.size());
    if(in.repeated&kKeyUp) transportCursor_=wrap(transportCursor_-1,count);
    if(in.repeated&kKeyDown) transportCursor_=wrap(transportCursor_+1,count);
    if(in.repeated&kKeyL) transportCursor_=clampInt(transportCursor_-7,0,std::max(0,count-1));
    if(in.repeated&kKeyR) transportCursor_=clampInt(transportCursor_+7,0,std::max(0,count-1));
    bool activate=in.pressed&kKeyA;
    int zone=in.touched?touchedZone(in):-1;
    if(zone>=kZoneItem && zone<kZoneItem+count) {
        activate=transportCursor_==zone-kZoneItem;
        transportCursor_=zone-kZoneItem;
    }
    if(!activate || count==0) return;
    switch(transportPhase_) {
        case TransportPhase::Source: transporterSelectSave(true);break;
        case TransportPhase::SourceMon: {
            auto p=transportPositions_[transportCursor_];
            transportFromBox_=p.first;transportFromSlot_=p.second;
            transportSave_.reset();
            transportSaves_=saves::findPokemonSaves(config_.scanRoot,3);
            transportSaves_.erase(std::remove_if(transportSaves_.begin(),transportSaves_.end(),
                [](const saves::SaveSummary& s){return s.game!=saves::Game::RubySapphire;}),
                transportSaves_.end());
            transportRows_.clear();
            for(const auto& s:transportSaves_) transportRows_.push_back(io::fileName(s.path)+" | "+s.trainer);
            transportPhase_=TransportPhase::Destination;
            transportCursor_=transportScroll_=0;
            break;
        }
        case TransportPhase::Destination: transporterSelectSave(false);break;
        case TransportPhase::DestinationSlot: transporterPreview();break;
        case TransportPhase::Review:
            if(transportReview_ && transportAccepted_) {
                if(transportReview_->originalsMatch()) {
                    transportMessage_="Exact review confirmed (read-only)";
                } else {
                    transportMessage_="Save changed; review invalidated";
                    transportAccepted_=false;transportReview_.reset();
                }
            }
            break; // No journal persistence in the preview build.
    }
}
void App::drawTransporter(Canvas& top,Canvas& bottom) {
    drawChrome(top,"Poke Transporter", "PCCS LEGAL",kAccent);
    top.roundRect(5,kBodyTop+3,246,136,kBorder,kPanel);
    const char* phase="Select Crystal save";
    switch(transportPhase_) {
        case TransportPhase::Source:break;
        case TransportPhase::SourceMon:phase="Select boxed Pokemon";break;
        case TransportPhase::Destination:phase="Select Ruby/Sapphire save";break;
        case TransportPhase::DestinationSlot:phase="Select empty destination slot";break;
        case TransportPhase::Review:phase=transportAccepted_?"Preview accepted":"Preview refused";break;
    }
    top.text(kFontLarge,9,45,238,Align::Center,phase,kText);
    top.text(kFontSmall,9,66,238,Align::Center,"Gen II -> Gen III | English only",kMuted);
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
    else drawList(bottom,transportRows_,transportCursor_,transportScroll_,5,8,246,22,7);
    if(transportPhase_==TransportPhase::Review) {
#ifdef VPT_TRANSPORTER_WRITE
        if(transportAccepted_) drawHints(bottom,{{"A","Recheck"},{"START","Move"},{"B","Cancel"}},{kKeyA,kKeyStart,kKeyB});
        else
#endif
        drawHints(bottom,{{"A","Recheck"},{"B","Home"}},{kKeyA,kKeyB});
    }
    else drawHints(bottom,{{"A","Select"},{"+","Move"},{"B","Cancel"}},{kKeyA,0,kKeyB});
}
}
#endif
