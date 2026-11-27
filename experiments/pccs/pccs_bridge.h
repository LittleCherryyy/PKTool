// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace pktool::pokemon::pccs_bridge {
struct Preview {
    bool ok = false;
    // An offered repair is optional; no adjusted PK3 is returned as accepted
    // until the caller explicitly re-runs preview with allowDestinationRepair.
    bool repairAvailable = false;
    bool repaired = false;
    std::vector<std::string> repairChanges;
    std::array<uint8_t, 80> data{}; // PKTool canonical decrypted PK3.
    std::string error;
    std::vector<std::string> notes;
};
// Caller must validate the actual Crystal/Ruby source and destination saves.
// Read-only: no bank or save mutations.
Preview previewGbToGba(const uint8_t* entity, std::size_t size, uint8_t generation,
                        bool allowDestinationRepair=false);
Preview previewCrystalToRuby(const uint8_t* entity, std::size_t size);
}
