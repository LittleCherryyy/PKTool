// SPDX-License-Identifier: GPL-3.0-or-later
#include "stage_transfer.h"
#include "pccs_policy.h"
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
using saves::Game;
using Blob = std::array<uint8_t,entity::kMaxPartySize>;
bool inBounds(const SaveFile& s, BoxSlot p) {
    return p.box >= 0 && p.box < s.boxCount() && p.slot >= 0 && p.slot < s.boxSlotCount();
}
bool sameTrainer(const SaveFile& a, const SaveFile& b) {
    return a.game() == b.game() && a.trainerName() == b.trainerName() &&
           a.trainerId() == b.trainerId() && a.secretId() == b.secretId() &&
           a.trainerGender() == b.trainerGender() && a.partyCount() == b.partyCount();
}
bool sameParty(const SaveFile& a, const SaveFile& b) {
    if (!sameTrainer(a,b)) return false;
    for (int i = 0; i < a.partyCount(); ++i) {
        Blob x{}, y{};
        if (a.readPartySlot(i,x.data()) != SlotState::Valid ||
            b.readPartySlot(i,y.data()) != SlotState::Valid ||
            !std::equal(x.begin(),x.begin()+a.partySize(),y.begin())) return false;
    }
    return true;
}
std::vector<Blob> boxContents(const SaveFile& s, int box, int skip = -1) {
    std::vector<Blob> contents;
    for (int i = 0; i < s.boxSlotCount(); ++i) {
        Blob b{};
        if (i != skip && s.readBoxSlot(box,i,b.data()) == SlotState::Valid)
            contents.push_back(b);
    }
    return contents;
}
bool sourceMatches(const SaveFile& before,const SaveFile& after,BoxSlot from) {
    if (!sameParty(before,after) || before.boxCount()!=after.boxCount() ||
        before.boxSlotCount()!=after.boxSlotCount()) return false;
    for (int b=0;b<before.boxCount();++b) {
        if (before.boxName(b)!=after.boxName(b) ||
            boxContents(before,b,b==from.box?from.slot:-1)!=boxContents(after,b))
            return false;
    }
    return true;
}
bool destinationMatches(const SaveFile& before,const SaveFile& after,BoxSlot to,
                        const std::array<uint8_t,80>& converted) {
    if (!sameParty(before,after) || before.boxCount()!=after.boxCount() ||
        before.boxSlotCount()!=after.boxSlotCount()) return false;
    for(int b=0;b<before.boxCount();++b) {
        if(before.boxName(b)!=after.boxName(b)) return false;
        for(int i=0;i<before.boxSlotCount();++i) {
            Blob x{},y{};
            auto xs=before.readBoxSlot(b,i,x.data());
            auto ys=after.readBoxSlot(b,i,y.data());
            if(b==to.box && i==to.slot) {
                if(xs!=SlotState::Empty || ys!=SlotState::Valid ||
                    !std::equal(converted.begin(),converted.end(),y.begin())) return false;
            } else if(xs!=ys || (xs==SlotState::Valid && x!=y)) return false;
        }
    }
    return true;
}
}
StagedTransfer stageCrystalToRuby(const std::vector<uint8_t>& source,
                                  const std::vector<uint8_t>& destination,
                                  BoxSlot from,BoxSlot to) {
    StagedTransfer result;
    auto fail=[&](const char* error) { result.error=error; return result; };
    auto s=saves::loadSaveFile(source);
    auto d=saves::loadSaveFile(destination);
    if (!s || !d || !saves::validateSave(*s) || !saves::validateSave(*d))
        return fail("Invalid save or checksum");
    if (s->game()!=Game::Crystal || d->game()!=Game::RubySapphire ||
        s->language()!=2 || d->language()!=2)
        return fail("Only English Crystal and Ruby/Sapphire save format supported");
    if (!inBounds(*s,from) || !inBounds(*d,to)) return fail("Invalid box slot");
    Blob selected{}, existing{};
    if (s->readBoxSlot(from.box,from.slot,selected.data())!=SlotState::Valid)
        return fail("Source slot is not valid");
    if (d->readBoxSlot(to.box,to.slot,existing.data())!=SlotState::Empty)
        return fail("Destination slot is not empty");
    auto preview=previewCrystalToRuby(selected.data(),pk2::kEntitySize);
    result.notes=preview.notes;
    if (!preview.ok) { result.error=preview.error; return result; }
    // Keep original images immutable and mutate independent staged copies.
    auto sd=saves::loadSaveFile(source);
    auto dd=saves::loadSaveFile(destination);
    if(!sd || !dd || !dd->writeBoxSlot(to.box,to.slot,preview.data.data()))
        return fail("Cannot stage destination");
    dd->refreshChecksums();
    auto dr=saves::loadSaveFile(dd->data());
    if(!dr || !saves::validateSave(*dr) || !destinationMatches(*d,*dr,to,preview.data))
        return fail("Destination round-trip verification failed");
    // Remove only after the staged destination has been fully reloaded.
    if(!sd->clearBoxSlot(from.box,from.slot)) return fail("Cannot stage source removal");
    sd->refreshChecksums();
    auto sr=saves::loadSaveFile(sd->data());
    if(!sr || !saves::validateSave(*sr) || !sourceMatches(*s,*sr,from))
        return fail("Source round-trip verification failed");
    result.sourceAfter=sr->data();
    result.destinationAfter=dr->data();
    result.converted=preview.data;
    result.ok=true;
    return result;
}
}
