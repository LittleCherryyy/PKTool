// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pccs_bridge.h"
#include "core/pokemon/pokemon.h"
#include "core/bank/bank.h"
#include <vector>
#include <memory>
namespace pktool::pokemon::pccs_bridge {
struct BoxSlot { int box; int slot; };
constexpr size_t kMaxBatch = 30;
// Only canonical current-size Bank files can participate in the same-size journal.
bool loadTransferBank(const std::vector<uint8_t>& bytes, pktool::bank::Bank& bank);
struct TransferPair { BoxSlot from; BoxSlot to; };
struct EntryReview {
    Pokemon mon;
    bool ready=false;
    // A repair is only offered after an independent preview of the exact
    // converted bytes succeeds. No source save is modified by the repair.
    bool repairAvailable=false, repaired=false;
    std::vector<std::string> repairChanges;
    uint16_t returnedItem=0;
    std::string itemName, error;
    std::vector<std::string> notes;
};
struct StagedTransfer {
    bool ok = false;
    std::string error;
    std::vector<std::string> notes;
    std::vector<EntryReview> entries;
    std::vector<uint8_t> sourceAfter;
    std::vector<uint8_t> destinationAfter;
    std::array<uint8_t,80> converted{};
    std::vector<std::array<uint8_t,80>> convertedBatch;
};
// Cooperative staging: immutable input images must outlive the session.
class BatchStage {
public:
    BatchStage(const std::vector<uint8_t>& source,const std::vector<uint8_t>& destination,
               const std::vector<TransferPair>& pairs,uint8_t sourceLanguage=0,
               const std::vector<size_t>& repairIndices={});
    ~BatchStage();
    bool step();
    size_t completed() const;
    size_t total() const;
    const std::vector<EntryReview>& entries() const;
    StagedTransfer take();
private:
    struct State;
    std::unique_ptr<State> state_;
};
// No disk IO. The caller must confirm actual English game editions from
// ROM metadata or explicit user selection; Ruby/Sapphire share save format.
StagedTransfer stageGbBatch(const std::vector<uint8_t>& source,
                            const std::vector<uint8_t>& destination,
                            const std::vector<TransferPair>& pairs, uint8_t sourceLanguage=0,
                            const std::vector<size_t>& repairIndices={});
StagedTransfer stageCrystalToRuby(const std::vector<uint8_t>& source,
                                  const std::vector<uint8_t>& destination,
                                  BoxSlot from, BoxSlot to);
}
