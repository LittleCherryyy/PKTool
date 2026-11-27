// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_transfer.h"
#include "pccs_policy.h"
#include "pccs_preflight.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/pk3.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_file.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
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
// A proposed repair is an ephemeral conversion input, not a modification of
// the original Game Boy save. Only a legalizer-produced candidate which passes
// the FULL PCCS LEGAL conversion and unchanged-identity policy can be offered.
// In particular, no automatic changes to species, OT, TID, nickname,
// level, shiny state or gender can be silently accepted.
bool identityPreserved(const Pokemon& before,const Pokemon& after) {
    return before.format==after.format &&
           before.species==after.species && before.level==after.level &&
           before.nickname==after.nickname &&
           before.trainer.name==after.trainer.name &&
           before.trainer.tid==after.trainer.tid &&
           before.trainer.gender==after.trainer.gender &&
           before.shiny==after.shiny && before.gender==after.gender &&
           before.form==after.form && before.language==after.language &&
           before.originGame==after.originGame && before.isEgg==after.isEgg;
}
struct RepairAttempt {
    bool available=false;
    Blob bytes{};
    std::vector<std::string> changes;
};
RepairAttempt proposeRepair(const Blob& original,size_t stored,uint8_t format) {
    RepairAttempt r;
    if((format!=1 && format!=2)||stored!=entity::storedSize(format)) return r;
    const Pokemon before=entity::decode(format,original.data(),stored);
    // A repair must never evade PCCS's hard transfer gates for eggs, mythical
    // events, impossible species or invalid source levels.
    if(before.isEgg||before.species==151||before.species==251||
       before.species==0||before.level<1||before.level>100 ||
       !supportedLanguage(before.language)) return r;
    auto proposal=legality::legalizeGb(before,500);
    if(proposal.changes.empty()||!proposal.unresolved.empty() ||
       proposal.report.overall()!=legality::Severity::Valid ||
       !identityPreserved(before,proposal.pokemon)) return r;
    r.bytes=original;
    entity::apply(proposal.pokemon,r.bytes.data());
    if(std::equal(original.begin(),original.begin()+stored,r.bytes.begin())) return r;
    const Pokemon decoded=entity::decode(format,r.bytes.data(),stored);
    if(!identityPreserved(before,decoded) ||
       legality::check(decoded).overall()!=legality::Severity::Valid) return r;
    // Mandatory independent PCCS/Pk3 review, not only a Gen I/II legality
    // badge. If it refuses, neither UI nor commit can opt into this repair.
    if(!previewGbToGba(r.bytes.data(),stored,format).ok) return r;
    r.available=true;
    r.changes=std::move(proposal.changes);
    return r;
}
}
bool loadTransferBank(const std::vector<uint8_t>& bytes,bank::Bank& bank) {
    return bank.loadBytes(bytes)==bank::Bank::LoadResult::Ok && bank.serialize()==bytes;
}
struct BatchStage::State {
    const std::vector<uint8_t>& source;
    const std::vector<uint8_t>& destination;
    const std::vector<TransferPair> pairs;
    const std::vector<size_t> repairIndices;
    uint8_t sourceLanguage;
    std::unique_ptr<SaveFile> s,d;
    std::unique_ptr<bank::Bank> bankDest,bankSource;
    StagedTransfer result;
    std::vector<std::array<uint8_t,80>> converted;
    std::vector<Pocket> plannedBag;
    std::vector<bool> changedBag;
    size_t index=0;
    bool allReady=true,done=false,initialized=false;
    State(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const std::vector<TransferPair>& p,uint8_t lang,
          const std::vector<size_t>& repairs)
        :source(a),destination(b),pairs(p),repairIndices(repairs),sourceLanguage(lang){}
    bool fail(const std::string& error) {result.error=error;done=true;return true;}
    bool step() {
        if(done) return true;
        if(!initialized) {
    if(pairs.empty()||pairs.size()>kMaxBatch) return fail("Select 1-30 Pokemon");
    for(size_t i=0;i<repairIndices.size();++i) {
        if(repairIndices[i]>=pairs.size()) return fail("Invalid repair entry");
        for(size_t j=0;j<i;++j)
            if(repairIndices[j]==repairIndices[i]) return fail("Duplicate repair entry");
    }
    for(size_t i=0;i<pairs.size();++i) for(size_t j=0;j<i;++j)
        if(sameSlot(pairs[i].from,pairs[j].from)||sameSlot(pairs[i].to,pairs[j].to))
            return fail("Duplicate source or destination slot");
    if(source.size()>=4&&std::equal(source.begin(),source.begin()+4,"PKBK")) {
        bankSource=std::make_unique<bank::Bank>();
        if(!loadTransferBank(source,*bankSource)) return fail("Bank needs validation or upgrade in editor");
    } else s=saves::loadSaveFile(source);
    if(destination.size()>=4&&std::equal(destination.begin(),destination.begin()+4,"PKBK")) {
        bankDest=std::make_unique<bank::Bank>();
        if(!loadTransferBank(destination,*bankDest)) return fail("Bank needs validation or upgrade in editor");
    } else d=saves::loadSaveFile(destination);
    if(bankSource&&bankDest) return fail("Bank-to-Bank transfer is not supported");
    if((!bankSource&&(!s||!saves::validateSave(*s)))||(!bankDest&&(!d||!saves::validateSave(*d)))) return fail("Invalid save or checksum");
    if((!bankSource&&((s->format()!=1&&s->format()!=2)||!supportedLanguage(s->language())))||
       (!bankDest&&(d->format()!=3||!supportedLanguage(d->language()))))
        return fail("Unsupported save format or language");
    if(!bankSource) {
        if(!sourceLanguage) sourceLanguage=s->language();
        if(!supportedLanguage(sourceLanguage)||(sourceLanguage==1)!=(s->language()==1))
            return fail("Selected language does not match GB save layout");
    }

            converted.reserve(pairs.size());
            if(s&&s->format()==2) {
                for(int p=0;p<s->pocketCount();++p) plannedBag.push_back(s->readPocket(p));
                changedBag.resize(plannedBag.size(),false);
            }
            initialized=true;return false;
        }
        if(index<pairs.size()) {
            const auto i=index;
        const auto p=pairs[i];
        const auto prefix=pairs.size()==1?std::string():"Pokemon "+std::to_string(i+1)+": ";
        EntryReview entry;
        Blob selected{},existing{};
        int pocket=-1;Pocket updated;uint16_t heldItem=0;
        const uint8_t format=bankSource?bankSource->format(p.from.box,p.from.slot):s->format();
        const auto raw=bankSource?bankSource->entity(p.from.box,p.from.slot):nullptr;
        const bool classic=format==1||format==2;
        const size_t stored=classic?entity::storedSize(format):0;
        const bool valid=bankSource?(classic&&raw):
            (inBounds(*s,p.from)&&s->readBoxSlot(p.from.box,p.from.slot,selected.data())==SlotState::Valid);
        if(!valid) entry.error="Source slot is not a valid classic Pokemon";
        else {
            if(bankSource) std::copy_n(raw,stored,selected.data());
            else selected[format==1?pk1::kLanguageOffset:pk2::kLanguageOffset]=sourceLanguage;
            entry.mon=entity::decode(format,selected.data(),stored);
            const bool empty=bankDest?
                (p.to.box>=0&&p.to.box<bank::Bank::kBoxCount&&p.to.slot>=0&&p.to.slot<bank::Bank::kSlotsPerBox&&!bankDest->format(p.to.box,p.to.slot)):
                (inBounds(*d,p.to)&&d->readBoxSlot(p.to.box,p.to.slot,existing.data())==SlotState::Empty);
            if(!empty)
                entry.error="Destination slot is not empty";
            else if(bankSource&&format==2&&selected[pk2::offset::kHeldItem])
                entry.error="Withdraw to Gen II and remove item first";
            else if(format==2 && selected[pk2::offset::kHeldItem]) {
                const auto item=selected[pk2::offset::kHeldItem];heldItem=item;
                entry.itemName=s->itemName(item);
                if(planItemReturn(*s,item,plannedBag,pocket,updated,entry.error))
                    selected[pk2::offset::kHeldItem]=0;
            }
            if(entry.error.empty()) {
                const bool requested=std::find(repairIndices.begin(),repairIndices.end(),i)!=repairIndices.end();
                auto preview=previewGbToGba(selected.data(),stored,format);
                if(!preview.ok && preview.error=="Source legality cannot be confirmed") {
                    const auto repair=proposeRepair(selected,stored,format);
                    if(repair.available) {
                        entry.repairAvailable=true;
                        entry.repairChanges=repair.changes;
                        if(requested) {
                            auto corrected=previewGbToGba(repair.bytes.data(),stored,format);
                            if(corrected.ok) {
                                preview=std::move(corrected);
                                entry.repaired=true;
                            }
                        }
                    }
                }
                entry.notes=std::move(preview.notes);
                if(requested&&!entry.repaired) entry.error="Repair unavailable for this Pokemon";
                else if(!preview.ok) entry.error=preview.error;
                else {
                    entry.ready=true;
                    if(entry.repaired) {
                        entry.notes.insert(entry.notes.begin(),"Optional Repair for Transfer applied to conversion copy only");
                        for(const auto& change:entry.repairChanges)
                            entry.notes.push_back("Repair: "+change);
                    }
                    if(pocket>=0) {
                        plannedBag[pocket]=std::move(updated);changedBag[pocket]=true;
                        entry.returnedItem=heldItem;
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

            ++index;return false;
        }
        if(!allReady) {done=true;return true;}
    // Stage and reload each save only once. Release mutable destination before
    // source staging to reduce the physical DSi heap peak.
    std::vector<uint8_t> destinationAfter;
    if(bankDest) {
        for(size_t i=0;i<pairs.size();++i) {
            const auto to=pairs[i].to;
            if(!bankDest->set(to.box,to.slot,3,converted[i].data())) return fail("Cannot stage Bank");
        }
        destinationAfter=bankDest->serialize();
        bankDest.reset();
        bank::Bank check;
        if(destinationAfter.size()!=destination.size()||!loadTransferBank(destinationAfter,check))
            return fail("Bank round-trip failed");
        // Canonical byte image: only the selected slots, format header if
        // needed, and trailing CRC may change. Verify exact converted data.
        constexpr size_t base=8+bank::Bank::kBoxCount*bank::Bank::kBoxNameBytes;
        constexpr size_t stride=4+bank::Bank::kEntitySize;
        auto expected=destination;
        for(size_t i=0;i<pairs.size();++i) {
            auto to=pairs[i].to;
            size_t off=base+stride*(to.box*bank::Bank::kSlotsPerBox+to.slot);
            expected[off]=3;
            std::copy(converted[i].begin(),converted[i].end(),expected.begin()+off+4);
        }
        expected[expected.size()-2]=destinationAfter[destinationAfter.size()-2];
        expected.back()=destinationAfter.back();
        if(expected!=destinationAfter) return fail("Unrelated Bank bytes changed");
    } else {
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
    if(bankSource) {
        for(const auto& p:pairs)
            if(!bankSource->clear(p.from.box,p.from.slot)) return fail("Cannot stage Bank removal");
        auto after=bankSource->serialize();
        bankSource.reset();
        bank::Bank check;
        if(after.size()!=source.size()||!loadTransferBank(after,check)) return fail("Bank source round-trip failed");
        constexpr size_t base=8+bank::Bank::kBoxCount*bank::Bank::kBoxNameBytes;
        constexpr size_t stride=4+bank::Bank::kEntitySize;
        auto expected=source;
        for(const auto& p:pairs) {
            const size_t off=base+stride*(p.from.box*bank::Bank::kSlotsPerBox+p.from.slot);
            std::fill_n(expected.begin()+off,stride,0);
        }
        // Last classic entity removal may downgrade v3 to v2.
        expected[4]=after[4];expected[5]=after[5];
        expected[expected.size()-2]=after[after.size()-2];expected.back()=after.back();
        if(expected!=after) return fail("Unrelated Bank bytes changed");
        result.sourceAfter=std::move(after);
    } else {
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
    }
    result.destinationAfter=std::move(destinationAfter);
    result.converted=converted.front();result.convertedBatch=std::move(converted);
    result.ok=true;

        done=true;return true;
    }
};
BatchStage::BatchStage(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
                       const std::vector<TransferPair>& pairs,uint8_t sourceLanguage,
                       const std::vector<size_t>& repairIndices)
    :state_(new State(source,destination,pairs,sourceLanguage,repairIndices)) {}
BatchStage::~BatchStage()=default;
bool BatchStage::step(){return state_->step();}
size_t BatchStage::completed() const{return state_->index;}
size_t BatchStage::total() const{return state_->pairs.size();}
const std::vector<EntryReview>& BatchStage::entries() const{return state_->result.entries;}
StagedTransfer BatchStage::take(){
    if(!state_->done) {StagedTransfer r;r.error="Batch not complete";return r;}
    return std::move(state_->result);
}
StagedTransfer stageGbBatch(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
                           const std::vector<TransferPair>& pairs,uint8_t sourceLanguage,
                           const std::vector<size_t>& repairIndices) {
    BatchStage batch(source,destination,pairs,sourceLanguage,repairIndices);
    while(!batch.step()) {}
    return batch.take();
}
StagedTransfer stageCrystalToRuby(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
                                  BoxSlot from,BoxSlot to) {
    return stageGbBatch(source,destination,{{from,to}});
}
}
