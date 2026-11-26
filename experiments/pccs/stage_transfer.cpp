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
    // Convert immutable original slots first. No staged save allocations during PCCS.
    for(size_t i=0;i<pairs.size();++i) {
        const auto p=pairs[i];
        const auto prefix=pairs.size()==1?std::string():"Pokemon "+std::to_string(i+1)+": ";
        if(!inBounds(*s,p.from)||!inBounds(*d,p.to)) return fail(prefix+"Invalid box slot");
        Blob selected{},existing{};
        if(s->readBoxSlot(p.from.box,p.from.slot,selected.data())!=SlotState::Valid) return fail(prefix+"Source slot is not valid");
        if(d->readBoxSlot(p.to.box,p.to.slot,existing.data())!=SlotState::Empty) return fail(prefix+"Destination slot is not empty");
        // International GB saves contain no language byte per Pokemon.
        // The explicit source language applies only to this temporary entity.
        selected[s->format()==1?pk1::kLanguageOffset:pk2::kLanguageOffset]=sourceLanguage;
        auto preview=previewGbToGba(selected.data(),s->storedSize(),s->format());
        if(!preview.ok) {
            for(auto& note:preview.notes) result.notes.push_back(prefix+note);
            return fail(prefix+preview.error);
        }
        for(auto& note:preview.notes) result.notes.push_back(prefix+note);
        converted.push_back(preview.data);
    }
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
    sd->refreshChecksums();
    auto image=sd->data(); sd.reset();
    auto sr=saves::loadSaveFile(std::move(image));
    if(!sr||!saves::validateSave(*sr)||!sourceMatches(*s,*sr,pairs))
        return fail("Source round-trip verification failed");
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
