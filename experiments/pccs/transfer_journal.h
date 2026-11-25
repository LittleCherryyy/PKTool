// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_transfer.h"
#include <string>
namespace pktool::pokemon::pccs_bridge {
// POSIX and experimental internal-SD FAT backend. Single writer only.
// No other application may open/modify the selected saves during recovery.
enum class StopPoint { None, DestinationMoved, DestinationWritten, SourceMoved };
enum class JournalStatus { Complete, Interrupted, Conflict, IoError, InvalidJournal };
struct JournalResult {
    JournalStatus status;
    std::string message;
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
