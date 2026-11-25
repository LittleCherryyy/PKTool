// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pccs_bridge.h"
#include <vector>
namespace pktool::pokemon::pccs_bridge {
struct BoxSlot { int box; int slot; };
struct StagedTransfer {
    bool ok = false;
    std::string error;
    std::vector<std::string> notes;
    std::vector<uint8_t> sourceAfter;
    std::vector<uint8_t> destinationAfter;
    std::array<uint8_t,80> converted{};
};
// No disk IO. The caller must confirm actual English game editions from
// ROM metadata or explicit user selection; Ruby/Sapphire share save format.
StagedTransfer stageCrystalToRuby(const std::vector<uint8_t>& source,
                                  const std::vector<uint8_t>& destination,
                                  BoxSlot from, BoxSlot to);
}
