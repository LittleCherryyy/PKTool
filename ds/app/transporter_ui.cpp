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
namespace {
constexpr size_t kMaxSave = 160 * 1024;
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
    if(path.find("(france)")!=std::string::npos || path.find("(fr)")!=std::string::npos) return 3;
    if(path.find("(italy)")!=std::string::npos || path.find("(it)")!=std::string::npos) return 4;
    if(path.find("(germany)")!=std::string::npos || path.find("(de)")!=std::string::npos) return 5;
    if(path.find("(spain)")!=std::string::npos || path.find("(es)")!=std::string::npos) return 7;
    return 2;
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
        bottom.text(kFontSmall,0,2,256,Align::Center,"GBA PC - Transfer Complete",kText);
        if(!transportResultMons_.empty()) {
            for(size_t i=0;i<transportResultMons_.size();++i) {
                const int x=5+static_cast<int>(i%5)*50,y=18+static_cast<int>(i/5)*42;
                bottom.roundRect(x,y,46,40,kBorder,kPanel);
                drawTransportSprite(bottom,transportResultMons_[i],x+2,y+1,42,27,3);
                bottom.text(kFontSmall,x,y+27,46,Align::Center,transportResultMons_[i].nickname,kText);
            }
        } else bottom.text(kFontSmall,0,53,256,Align::Center,"Original backups retained.",kText);
#ifdef VPT_TRANSPORTER_WRITE
        bottom.roundRect(18,109,220,44,kAccent,kPanel,2);
        bottom.text(kFontLarge,18,117,220,Align::Center,"Return to main menu",kText);
        bottom.text(kFontSmall,18,134,220,Align::Center,"A / B / X / START",kMuted);
        zones_.push_back({18,109,220,44,kKeyA});
        bottom.text(kFontSmall,0,162,256,Align::Center,"Finishes and archives the transfer.",kMuted);
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
    scanned_=false;saveFiles_.clear();transportCatalog_.clear();
    if(refreshTransferGate()) {
        if(r.status==br::JournalStatus::Complete) {
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
    transportSelected_.clear();transportMons_.clear();transportResultMons_.clear();
    transportBoxSpecies_.clear();transportAnimating_=false;
    transportMessage_.clear();transportAccepted_=false;transportConfirm_=false;
    transportCatalog_=saves::findPokemonSaves(config_.scanRoot,3,&transportCatalog_);
    transportSaves_=transportCatalog_;
    transportSaves_.erase(std::remove_if(transportSaves_.begin(),transportSaves_.end(),
        [](const saves::SaveSummary& s){return saves::gameGeneration(s.game)!=1 && saves::gameGeneration(s.game)!=2;}),
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
    if(!loaded || !saves::validateSave(*loaded) || !pokemon::pccs_bridge::supportedLanguage(loaded->language()) ||
       (source?(loaded->format()!=1 && loaded->format()!=2):loaded->format()!=3)) {
        showMessage("Transporter","Unsupported or invalid save.\nGen I/II to Gen III; Korean excluded.");
        return;
    }
    if(source) {
        transportSource_=path; transportSourceBytes_=std::move(bytes);
        transportGeneration_=loaded->format();
        transportLanguage_=loaded->language()==1?1:languageHint(path);
    } else { transportDest_=path; transportDestBytes_=std::move(bytes); }
    transportSave_=std::move(loaded);
    transportRows_.clear();transportPositions_.clear();
    transportMons_.clear();transportBoxSpecies_.clear();
    transportBoxSlots_=transportSave_->boxSlotCount();
    if(!source) transportBoxSpecies_.resize(transportSave_->boxCount()*transportBoxSlots_);
    for(int b=0;b<transportSave_->boxCount();++b) {
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
    transportPhase_=source?TransportPhase::SourceMon:TransportPhase::DestinationSlot;
    transportCursor_=transportScroll_=0;
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
    if(transportCursor_<0 || transportSelected_.empty() ||
       static_cast<size_t>(transportCursor_)+transportSelected_.size()>transportPositions_.size()) {
        transportMessage_="Not enough empty slots from here";return;
    }
    std::vector<br::TransferPair> pairs;
    for(size_t i=0;i<transportSelected_.size();++i) {
        auto f=transportSelected_[i], t=transportPositions_[transportCursor_+i];
        pairs.push_back({{f.first,f.second},{t.first,t.second}});
    }
    transportSave_.reset();sprites_.clear();
    transportNotes_.clear();transportAccepted_=false;
    transportReview_=br::ReviewedTransfer::createBatch(
        transportSource_,transportDest_,transportSourceBytes_,transportDestBytes_,
        pairs,transportMessage_,transportNotes_,transportLanguage_);
    transportAccepted_=bool(transportReview_);
    transportResultMons_.clear();
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
        if(transportConfirm_) {
            transportAnimating_=true;transportAnimationStart_=frame_;redraw_=true;
        }
        else {transportConfirm_=true;transportMessage_="START again to MOVE Pokemon";}
        return;
    }
#endif
    if(transportPhase_==TransportPhase::SourceMon && transportLanguage_!=1 &&
       (in.pressed&(kKeyLeft|kKeyRight))) {
        const uint8_t langs[]={2,3,5,4,7};
        int at=0;while(at<4&&langs[at]!=transportLanguage_) ++at;
        transportLanguage_=langs[(at+((in.pressed&kKeyRight)?1:4))%5];
        refreshTransportNames();
        return;
    }
    int count=static_cast<int>(transportRows_.size());
    if(in.repeated&kKeyUp) transportCursor_=wrap(transportCursor_-1,count);
    if(in.repeated&kKeyDown) transportCursor_=wrap(transportCursor_+1,count);
    if(transportPhase_==TransportPhase::DestinationSlot&&count>0) {
        const auto here=transportPositions_[transportCursor_];
        const int delta=(in.repeated&kKeyR)?1:(in.repeated&kKeyL)?-1:0;
        if(delta) {
            int chosen=transportCursor_;
            if(delta>0) {
                for(int i=transportCursor_+1;i<count;++i) if(transportPositions_[i].first>here.first) {chosen=i;break;}
            } else {
                for(int i=transportCursor_-1;i>=0;--i) if(transportPositions_[i].first<here.first) {chosen=i;break;}
            }
            transportCursor_=chosen;
        }
        if(in.repeated&kKeyLeft) transportCursor_=wrap(transportCursor_-1,count);
        if(in.repeated&kKeyRight) transportCursor_=wrap(transportCursor_+1,count);
    } else {
        if(in.repeated&kKeyL) transportCursor_=clampInt(transportCursor_-7,0,std::max(0,count-1));
        if(in.repeated&kKeyR) transportCursor_=clampInt(transportCursor_+7,0,std::max(0,count-1));
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
        transportMessage_=std::to_string(transportSelected_.size())+" selected (max 10)";
        return;
    }
    if(!activate || count==0) return;
    switch(transportPhase_) {
        case TransportPhase::Source: transporterSelectSave(true);break;
        case TransportPhase::SourceMon: {
            if(transportSelected_.empty()) transportSelected_.push_back(transportPositions_[transportCursor_]);
            transportMessage_=std::to_string(transportSelected_.size())+" Pokemon selected";
            transportSave_.reset();
            transportSaves_=transportCatalog_;
            transportSaves_.erase(std::remove_if(transportSaves_.begin(),transportSaves_.end(),
                [](const saves::SaveSummary& s){return saves::gameGeneration(s.game)!=3;}),
                transportSaves_.end());
            transportRows_.clear();
            for(const auto& s:transportSaves_) transportRows_.push_back(io::fileName(s.path)+" | "+s.trainer);
            transportPhase_=TransportPhase::Destination;
            transportCursor_=transportScroll_=0;
            break;
        }
        case TransportPhase::Destination: transporterSelectSave(false);break;
        case TransportPhase::DestinationSlot: pending_=Pending::TransferPreview;break;
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
void App::drawTransportSprite(Canvas& c,const pokemon::Pokemon& mon,int x,int y,int w,int h,uint8_t gen) {
    const char* kind=gen==1?"transport-gb":gen==2?(mon.shiny?"transport-gbc-shiny":"transport-gbc"):(mon.shiny?"transport-gba-shiny":"transport-gba");
    auto im=sprites_.get(kind,mon.species);
    if(!im.pixels) im=sprites_.get(mon.shiny?"front-shiny":"front",mon.species);
    if(!im.pixels) {transferBall(c,x+w/2,y+h/2,9);return;}
    const int scale=std::max(1,std::max((im.width+w-1)/w,(im.height+h-1)/h));
    const int iw=(im.width+scale-1)/scale,ih=(im.height+scale-1)/scale;
    for(int j=0;j<ih;++j) for(int i=0;i<iw;++i) {
        auto px=im.pixels[(j*scale)*im.width+i*scale];
        if(!(px&0x8000)) continue;
        if(gen==1) {
            const int l=((px&31)*3+((px>>5)&31)*6+((px>>10)&31))/10;
            px=l<8?rgb(30,54,35):l<16?rgb(75,105,55):l<24?rgb(131,157,82):rgb(199,220,145);
        }
        c.pixel(x+(w-iw)/2+i,y+(h-ih)/2+j,px);
    }
}
void App::drawTransportAnimation(Canvas& top,Canvas& bottom) {
    const auto elapsed=std::min<uint32_t>(89,frame_-transportAnimationStart_);
    top.clear(kBackground);bottom.clear(kBackground);
    top.text(kFontLarge,0,12,256,Align::Center,"LINK TRANSPORT",kText);
    top.roundRect(14,36,76,115,kBorder,rgb(139,149,142),2);
    top.roundRect(166,54,76,79,kBorder,rgb(83,92,165),2);
    top.fillRect(23,45,58,63,rgb(181,202,133));
    top.fillRect(175,63,58,49,rgb(205,229,210));
    top.hline(90,84,76,kAccent);
    top.vline(90,84,17,kAccent);top.vline(165,84,17,kAccent);
    top.fillRect(41,119,19,5,kPanelDark);top.fillRect(48,112,5,19,kPanelDark);
    top.fillCircle(72,125,5,kRed);
    const auto idx=transportResultMons_.empty()?0:std::min<size_t>(transportResultMons_.size()-1,elapsed*transportResultMons_.size()/90);
    if(!transportResultMons_.empty()) {
        const auto& mon=transportResultMons_[idx];
        drawTransportSprite(top,mon,23,45,58,63,transportGeneration_);
        drawTransportSprite(top,mon,175,63,58,49,3);
        const int x=90+static_cast<int>(elapsed)*75/89;
        transferBall(top,x,84,6);
        drawTransportSprite(bottom,mon,83,18,90,90,elapsed<45?transportGeneration_:3);
        bottom.text(kFontSmall,0,114,256,Align::Center,mon.nickname,kText);
    }
    top.text(kFontSmall,14,157,76,Align::Center,"GB / GBC",kMuted);
    top.text(kFontSmall,166,157,76,Align::Center,"GBA",kMuted);
    bottom.rect(23,134,210,9,kBorder);
    bottom.fillRect(25,136,static_cast<int>(elapsed)*206/89,5,kAccent);
    bottom.text(kFontSmall,0,151,256,Align::Center,"Preparing link - A / START skip",kMuted);
}
void App::drawTransporter(Canvas& top,Canvas& bottom) {
    if(transportAnimating_) {drawTransportAnimation(top,bottom);return;}
    if(transportPhase_==TransportPhase::SourceMon&&!transportMons_.empty()) {
        const Color bg=transportGeneration_==1?rgb(181,202,133):rgb(220,235,208);
        const Color ink=rgb(30,54,35);
        const auto& p=transportMons_[transportCursor_];
        top.clear(bg);bottom.clear(bg);
        top.rect(5,5,246,182,ink,2);
        top.text(kFontLarge,10,10,236,Align::Center,transportGeneration_==1?"GAME BOY - PC":"GAME BOY COLOR - PC",ink);
        drawTransportSprite(top,p,12,35,100,100,transportGeneration_);
        marqueeActive_ |= drawMarquee(top,115,39,130,Align::Left,p.nickname,ink,frame_-marqueeStart_);
        top.text(kFontSmall,115,60,"Lv "+std::to_string(p.level),ink);
        top.text(kFontSmall,115,80,slotLabel(transportPositions_[transportCursor_].first,transportPositions_[transportCursor_].second),ink);
        top.text(kFontSmall,115,100,130,Align::Left,p.shiny?"SHINY":"",ink);
        top.text(kFontSmall,10,143,236,Align::Center,"Source language: "+std::string(data::languageName(transportLanguage_)),ink);
        top.text(kFontSmall,10,158,236,Align::Center,transportLanguage_==1?"Japanese layout":"LEFT / RIGHT: confirm language",ink);
        top.text(kFontSmall,10,173,236,Align::Center,std::to_string(transportSelected_.size())+" / 10 selected",ink);
        if(transportCursor_<transportScroll_) transportScroll_=transportCursor_;
        if(transportCursor_>=transportScroll_+7) transportScroll_=transportCursor_-6;
        for(int i=0;i<7&&transportScroll_+i<static_cast<int>(transportRows_.size());++i) {
            const int index=transportScroll_+i,y=4+24*i;
            bottom.rect(4,y,248,23,ink,index==transportCursor_?2:1);
            if(index==transportCursor_) bottom.arrow(8,y+8,7,1,ink);
            marqueeActive_ |= index==transportCursor_?drawMarquee(bottom,21,y+4,225,Align::Left,transportRows_[index],ink,frame_-marqueeStart_):false;
            if(index!=transportCursor_) bottom.text(kFontSmall,21,y+4,225,Align::Left,transportRows_[index],ink);
            zones_.push_back({4,y,248,23,kZoneItem+index});
        }
        drawHints(bottom,{{"X","Mark"},{"A","Next"},{"B","Cancel"}},{kKeyX,kKeyA,kKeyB});
        return;
    }
    if(transportPhase_==TransportPhase::DestinationSlot&&!transportPositions_.empty()) {
        const auto pos=transportPositions_[transportCursor_];
        const int box=pos.first;
        top.clear(rgb(32,93,108));bottom.clear(rgb(66,129,103));
        top.text(kFontLarge,4,16,248,Align::Center,"GBA PC - BOX "+twoDigits(box+1),kText);
        top.text(kFontSmall,4,40,248,Align::Center,slotLabel(box,pos.second)+" | "+std::to_string(transportSelected_.size())+" Pokemon",kText);
        top.text(kFontSmall,4,72,248,Align::Center,"Choose the first free slot.",kText);
        top.text(kFontSmall,4,90,248,Align::Center,"Following empty slots are filled in order.",kText);
        marqueeActive_ |= drawMarquee(top,8,123,240,Align::Center,transportMessage_,kText,frame_-marqueeStart_);
        for(int i=0;i<transportBoxSlots_;++i) {
            const int x=8+(i%6)*40,y=12+(i/6)*31;
            const auto sp=transportBoxSpecies_[box*transportBoxSlots_+i];
            bottom.roundRect(x,y,38,29,i==pos.second?kYellow:kPanelDark,kPanel, i==pos.second?2:1);
            if(sp&&sp!=0xffff) {pokemon::Pokemon mon;mon.species=sp;drawIcon(bottom,x,y,38,29,mon,false);}
            else if(sp==0xffff) bottom.text(kFontSmall,x,y+6,38,Align::Center,"!",kRed);
            else bottom.pixel(x+19,y+14,kMuted);
            if(!sp) {
                const auto it=std::find(transportPositions_.begin(),transportPositions_.end(),std::make_pair(box,i));
                if(it!=transportPositions_.end()) zones_.push_back({x,y,38,29,kZoneItem+static_cast<int>(it-transportPositions_.begin())});
            }
        }
        drawHints(bottom,{{"A","Select"},{"L/R","Page"},{"B","Cancel"}},{kKeyA,0,kKeyB});
        return;
    }
    drawChrome(top,"Poke Transporter", "PCCS LEGAL",kAccent);
    top.roundRect(5,kBodyTop+3,246,136,kBorder,kPanel);
    const char* phase="Select GB/GBC save";
    switch(transportPhase_) {
        case TransportPhase::Source:break;
        case TransportPhase::SourceMon:phase="Select boxed Pokemon";break;
        case TransportPhase::Destination:phase="Select GBA save";break;
        case TransportPhase::DestinationSlot:phase="Select first empty slot";break;
        case TransportPhase::Review:phase=transportAccepted_?"Preview accepted":"Preview refused";break;
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
    else drawList(bottom,transportRows_,transportCursor_,transportScroll_,5,8,246,22,7);
    if(transportPhase_==TransportPhase::Review) {
#ifdef VPT_TRANSPORTER_WRITE
        if(transportAccepted_) drawHints(bottom,{{"A","Recheck"},{"START","Move"},{"B","Cancel"}},{kKeyA,kKeyStart,kKeyB});
        else
#endif
        drawHints(bottom,{{"A","Recheck"},{"B","Home"}},{kKeyA,kKeyB});
    }
    else if(transportPhase_==TransportPhase::SourceMon)
        drawHints(bottom,{{"X","Mark"},{"A","Next"},{"B","Cancel"}},{kKeyX,kKeyA,kKeyB});
    else drawHints(bottom,{{"A","Select"},{"+","Move"},{"B","Cancel"}},{kKeyA,0,kKeyB});
}
}
#endif
