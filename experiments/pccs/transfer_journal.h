// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_transfer.h"
#include <string>
#include <memory>
namespace pktool::pokemon::pccs_bridge {
// POSIX and experimental internal-SD FAT backend. Single writer only.
// No other application may open/modify the selected saves during recovery.
enum class StopPoint { None, DestinationMoved, DestinationWritten, SourceMoved };
// Inspect a single fixed active journal before opening the normal editor.
// A partially created journal is blocked, not treated as an empty session.
enum class StartupState { Clear, Pending, Completed, Blocked };
struct StartupReview {
    StartupState state = StartupState::Blocked;
    std::string source, destination, message;
};
StartupReview inspectActiveJournal(const std::string& journal);
enum class JournalStatus { Complete, Interrupted, Conflict, IoError, InvalidJournal };
struct JournalResult {
    JournalStatus status;
    std::string message;
};
// Explicit recovery only. Read paths from the validated fixed journal.
JournalResult recoverActiveJournal(const std::string& journal);
// Archive exact journal before releasing original sidecars. Restartable.
// stopAfterFirstSidecar is a host fault-injection hook.
JournalResult archiveActiveJournal(const std::string& journal,bool stopAfterFirstSidecar=false);
// Opaque reviewed transfer: only the factory can construct a conversion.
class ReviewedBatch;
class ReviewedTransfer {
public:
    static std::unique_ptr<ReviewedTransfer> create(
        const std::string& source,const std::string& destination,
        const std::vector<uint8_t>& expectedSource,const std::vector<uint8_t>& expectedDestination,
        BoxSlot from,BoxSlot to,std::string& error,std::vector<std::string>& notes);
    static std::unique_ptr<ReviewedTransfer> createBatch(
        const std::string& source,const std::string& destination,
        const std::vector<uint8_t>& expectedSource,const std::vector<uint8_t>& expectedDestination,
        const std::vector<TransferPair>& pairs,std::string& error,std::vector<std::string>& notes,uint8_t sourceLanguage=0,
        std::vector<EntryReview>* entries=nullptr);
    const std::array<uint8_t,80>& converted() const { return staged_.converted; }
    const std::vector<std::array<uint8_t,80>>& convertedBatch() const { return staged_.convertedBatch; }
    bool originalsMatch() const;
    JournalResult persist(const std::string& journal) const;
    ReviewedTransfer(const ReviewedTransfer&)=delete;
    ReviewedTransfer& operator=(const ReviewedTransfer&)=delete;
private:
    friend class ReviewedBatch;
    ReviewedTransfer()=default;
    std::string source_,destination_;
    std::vector<uint8_t> sourceBefore_,destinationBefore_;
    StagedTransfer staged_;
};
// Cooperative reviewed conversion. Input snapshots must remain immutable
// and alive until the session is destroyed. No journal or save writes.
class ReviewedBatch {
public:
    ReviewedBatch(const std::string& source,const std::string& destination,
        const std::vector<uint8_t>& expectedSource,const std::vector<uint8_t>& expectedDestination,
        const std::vector<TransferPair>& pairs,uint8_t sourceLanguage);
    bool step();
    size_t completed() const;
    size_t total() const;
    const std::vector<EntryReview>& entries() const;
    std::unique_ptr<ReviewedTransfer> take(std::string& error,std::vector<std::string>& notes,
                                            std::vector<EntryReview>* entries=nullptr);
private:
    std::string source_,destination_,error_;
    const std::vector<uint8_t>& sb_;
    const std::vector<uint8_t>& db_;
    BatchStage batch_;
    StagedTransfer staged_;
    bool checked_=false,done_=false;
};
// Prepare immutable journal; does not alter either original save.
// Use a fresh journal path outside both saves. Keep it as a backup afterward.
JournalResult prepareJournal(const std::string& journal, const std::string& source,
                             const std::string& destination, BoxSlot from, BoxSlot to);
// Resume exact recorded transaction; never recompute a Pokemon or overwrite
// unrelated changes. The stop point simulates a process interruption in tests.
JournalResult resumeJournal(const std::string& journal, const std::string& source,
                            const std::string& destination, StopPoint stop = StopPoint::None);
}
