// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_transfer.h"
#include "pccs_policy.h"
#include "pccs_preflight.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_file.h"
#include <array>
#include <algorithm>
namespace pktool::pokemon::pccs_bridge {
namespace {
using saves::SaveFile;
using saves::SlotState;
using Blob = std::array<uint8_t,entity::kMaxPartySize>;
bool sameSlot(BoxSlot a,BoxSlot b) {return a.box==b.box&&a.slot==b.slot;}
bool inBounds(const SaveFile& s, BoxSlot p) {
    return p.box>=0&&p.box<s.boxCount()&&p.slot>=0&&p.slot<s.boxSlotCount();
}
bool sameParty(const SaveFile& a,const SaveFile& b) {
    if(a.game()!=b.game()||a.trainerName()!=b.trainerName()||a.trainerId()!=b.trainerId()||
       a.secretId()!=b.secretId()||a.trainerGender()!=b.trainerGender()||a.partyCount()!=b.partyCount()||
       a.boxCount()!=b.boxCount()||a.boxSlotCount()!=b.boxSlotCount()) return false;
    for(int i=0;i<a.partyCount();++i) {
        Blob x{},y{};
        if(a.readPartySlot(i,x.data())!=SlotState::Valid||b.readPartySlot(i,y.data())!=SlotState::Valid||
           !std::equal(x.begin(),x.begin()+a.partySize(),y.begin())) return false;
    }
    return true;
}
using Pocket=std::vector<saves::BagSlot>;
bool samePocket(const Pocket& a,const Pocket& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) if(a[i].item!=b[i].item||a[i].count!=b[i].count) return false;
    return true;
}
bool planItemReturn(const SaveFile& save,uint16_t item,std::vector<Pocket>& pockets,
                    int& pocket,Pocket& updated,std::string& error) {
    std::string name=save.itemName(item);
    // Mail has separate payload semantics. Never silently discard its message.
    if(name.find("Mail")!=std::string::npos||name.find("mail")!=std::string::npos) {
        error="Remove Mail in the original game";return false;
    }
    // Gen II pocket 4 is PC storage, not the player's Bag.
    for(int i=0;i<std::min(4,save.pocketCount());++i) {
        if(!save.pocketAllows(i,item)) continue;
        if(save.pocketInfo(i).kind==data::PocketKind::KeyItems) {
            error="Unsupported held key item";return false;
        }
        if(!save.validGen2BagPocket(i)) {error="Invalid source bag pocket";return false;}
        pocket=i;updated=pockets[i];
        const auto max=save.itemMaxCount(i,item);
        auto it=std::find_if(updated.begin(),updated.end(),[&](const saves::BagSlot& a){return a.item==item;});
        if(it!=updated.end()) {
            // Respect the normal Gen II stack limit; never overwrite or discard.
            if(it->count>=max) {error="Bag item stack full";return false;}
            ++it->count;
        } else {
            if(updated.size()>=static_cast<size_t>(save.pocketInfo(i).capacity)) {error="Source bag pocket full";return false;}
            updated.push_back({item,1});
            // TM/HM pocket is serialized in item-table order, not insertion order.
            if(save.pocketInfo(i).kind==data::PocketKind::TMsHMs) {
                const auto order=save.pocketItems(i);
                std::sort(updated.begin(),updated.end(),[&](const saves::BagSlot& a,const saves::BagSlot& b){
                    return std::find(order.begin(),order.end(),a.item)<std::find(order.begin(),order.end(),b.item);
                });
            }
        }
        return true;
    }
    error="Held item cannot be returned to Bag";return false;
}
bool sourceMatches(const SaveFile& before,const SaveFile& after,const std::vector<TransferPair>& pairs) {
    if(!sameParty(before,after)) return false;
    for(int b=0;b<before.boxCount();++b) {
        if(before.boxName(b)!=after.boxName(b)) return false;
        int next=0;
        for(int i=0;i<before.boxSlotCount();++i) {
            Blob x{},y{};
            if(before.readBoxSlot(b,i,x.data())!=SlotState::Valid) continue;
            if(std::any_of(pairs.begin(),pairs.end(),[&](const TransferPair& p){return sameSlot(p.from,{b,i});})) continue;
            // GB boxes are compacted, but compare every surviving entity exactly.
            if(next>=after.boxSlotCount()||after.readBoxSlot(b,next++,y.data())!=SlotState::Valid||
                !std::equal(x.begin(),x.begin()+before.storedSize(),y.begin())) return false;
        }
        for(;next<after.boxSlotCount();++next) {
            Blob x{};
            if(after.readBoxSlot(b,next,x.data())!=SlotState::Empty) return false;
        }
    }
    return true;
}
bool destinationMatches(const SaveFile& before,const SaveFile& after,const std::vector<TransferPair>& pairs,
                        const std::vector<std::array<uint8_t,80>>& converted) {
    if(!sameParty(before,after)) return false;
    for(int b=0;b<before.boxCount();++b) {
        if(before.boxName(b)!=after.boxName(b)) return false;
        for(int i=0;i<before.boxSlotCount();++i) {
            Blob x{},y{};
            auto xs=before.readBoxSlot(b,i,x.data()),ys=after.readBoxSlot(b,i,y.data());
            auto p=std::find_if(pairs.begin(),pairs.end(),[&](const TransferPair& p){return sameSlot(p.to,{b,i});});
            if(p!=pairs.end()) {
                const auto& expected=converted[static_cast<size_t>(p-pairs.begin())];
                if(xs!=SlotState::Empty||ys!=SlotState::Valid||!std::equal(expected.begin(),expected.end(),y.begin())) return false;
            } else if(xs!=ys||(xs==SlotState::Valid&&x!=y)) return false;
        }
    }
    return true;
}
}
StagedTransfer stageGbBatch(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
                           const std::vector<TransferPair>& pairs,uint8_t sourceLanguage) {
    StagedTransfer result;
    auto fail=[&](const std::string& error) {
        result.error=error;
        return std::move(result);
    };
    if(pairs.empty()||pairs.size()>kMaxBatch) return fail("Select 1-10 Pokemon");
    for(size_t i=0;i<pairs.size();++i) for(size_t j=0;j<i;++j)
        if(sameSlot(pairs[i].from,pairs[j].from)||sameSlot(pairs[i].to,pairs[j].to))
            return fail("Duplicate source or destination slot");
    auto s=saves::loadSaveFile(source),d=saves::loadSaveFile(destination);
    if(!s||!d||!saves::validateSave(*s)||!saves::validateSave(*d)) return fail("Invalid save or checksum");
    if((s->format()!=1&&s->format()!=2)||d->format()!=3||
       !supportedLanguage(s->language())||!supportedLanguage(d->language()))
        return fail("Unsupported save format or language");
    if(!sourceLanguage) sourceLanguage=s->language();
    if(!supportedLanguage(sourceLanguage)||(sourceLanguage==1)!=(s->language()==1))
        return fail("Selected language does not match GB save layout");
    std::vector<std::array<uint8_t,80>> converted;
    converted.reserve(pairs.size());
    std::vector<Pocket> plannedBag;
    std::vector<bool> changedBag;
    if(s->format()==2) {
        for(int p=0;p<s->pocketCount();++p) plannedBag.push_back(s->readPocket(p));
        changedBag.resize(plannedBag.size(),false);
    }
    bool allReady=true;
    // Review every selected entity even if another is blocked. Bag capacity is
    // reserved only for an entity whose conversion succeeded.
    for(size_t i=0;i<pairs.size();++i) {
        const auto p=pairs[i];
        const auto prefix=pairs.size()==1?std::string():"Pokemon "+std::to_string(i+1)+": ";
        EntryReview entry;
        Blob selected{},existing{};
        int pocket=-1;Pocket updated;
        if(!inBounds(*s,p.from)||s->readBoxSlot(p.from.box,p.from.slot,selected.data())!=SlotState::Valid)
            entry.error="Source slot is not valid";
        else {
            selected[s->format()==1?pk1::kLanguageOffset:pk2::kLanguageOffset]=sourceLanguage;
            entry.mon=entity::decode(s->format(),selected.data(),s->storedSize());
            if(!inBounds(*d,p.to)||d->readBoxSlot(p.to.box,p.to.slot,existing.data())!=SlotState::Empty)
                entry.error="Destination slot is not empty";
            else if(s->format()==2 && selected[pk2::offset::kHeldItem]) {
                const auto item=selected[pk2::offset::kHeldItem];
                entry.itemName=s->itemName(item);
                if(planItemReturn(*s,item,plannedBag,pocket,updated,entry.error))
                    selected[pk2::offset::kHeldItem]=0;
            }
            if(entry.error.empty()) {
                auto preview=previewGbToGba(selected.data(),s->storedSize(),s->format());
                entry.notes=std::move(preview.notes);
                if(!preview.ok) entry.error=preview.error;
                else {
                    entry.ready=true;
                    if(pocket>=0) {
                        plannedBag[pocket]=std::move(updated);changedBag[pocket]=true;
                        entry.returnedItem=entry.mon.heldItem;
                        entry.notes.insert(entry.notes.begin(),"Return to source Bag: "+entry.itemName+" x1");
                    }
                    converted.push_back(preview.data);
                }
            }
        }
        if(!entry.ready) {
            allReady=false;
            if(result.error.empty()) result.error=prefix+entry.error;
        }
        for(const auto& note:entry.notes) result.notes.push_back(prefix+note);
        result.entries.push_back(std::move(entry));
    }
    if(!allReady) return result;
    // Stage and reload each save only once. Release mutable destination before
    // source staging to reduce the physical DSi heap peak.
    std::vector<uint8_t> destinationAfter;
    {
        auto dd=saves::loadSaveFile(destination);
        if(!dd) return fail("Cannot stage destination");
        for(size_t i=0;i<pairs.size();++i) {
            auto to=pairs[i].to;
            if(!dd->writeBoxSlot(to.box,to.slot,converted[i].data())) return fail("Cannot stage destination");
        }
        dd->refreshChecksums();
        auto image=dd->data(); dd.reset();
        auto dr=saves::loadSaveFile(std::move(image));
        if(!dr||!saves::validateSave(*dr)||!destinationMatches(*d,*dr,pairs,converted))
            return fail("Destination round-trip verification failed");
        for(int p=0;p<d->pocketCount();++p)
            if(!samePocket(d->readPocket(p),dr->readPocket(p))) return fail("Destination Bag changed");
        destinationAfter=dr->data();
    }
    d.reset();
    auto sd=saves::loadSaveFile(source);
    if(!sd) return fail("Cannot stage source");
    std::vector<BoxSlot> order;
    for(auto p:pairs) order.push_back(p.from);
    std::sort(order.begin(),order.end(),[](BoxSlot a,BoxSlot b){
        return a.box!=b.box?a.box>b.box:a.slot>b.slot;
    });
    for(auto from:order) if(!sd->clearBoxSlot(from.box,from.slot)) return fail("Cannot stage source removal");
    for(size_t p=0;p<changedBag.size();++p)
        if(changedBag[p]&&!sd->setPocket(static_cast<int>(p),plannedBag[p]))
            return fail("Cannot stage source Bag");
    sd->refreshChecksums();
    auto image=sd->data(); sd.reset();
    auto sr=saves::loadSaveFile(std::move(image));
    if(!sr||!saves::validateSave(*sr)||!sourceMatches(*s,*sr,pairs))
        return fail("Source round-trip verification failed");
    for(size_t p=0;p<plannedBag.size();++p)
        if(!samePocket(plannedBag[p],sr->readPocket(static_cast<int>(p))) ||
           (changedBag[p]&&!sr->validGen2BagPocket(static_cast<int>(p))))
            return fail("Source Bag round-trip verification failed");
    result.sourceAfter=sr->data();
    result.destinationAfter=std::move(destinationAfter);
    result.converted=converted.front();result.convertedBatch=std::move(converted);
    result.ok=true;
    return result;
}
StagedTransfer stageCrystalToRuby(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
                                  BoxSlot from,BoxSlot to) {
    return stageGbBatch(source,destination,{{from,to}});
}
}
