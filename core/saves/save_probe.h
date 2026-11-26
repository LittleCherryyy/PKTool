#pragma once

#include <cstdint>
#include <memory>
#include <cstddef>
#include <string>
#include <vector>

#include "core/saves/game.h"

// Finding Pokémon saves on the SD card and summarising them for the browser.
namespace pktool::saves {

struct SaveSummary {
    std::string path;
    Game game = Game::Unknown;
    uint8_t version = 0;       // game version ID (Diamond, Pearl, ..., Black 2)
    std::string trainer;
    uint16_t trainerId = 0;
    std::vector<uint16_t> party;  // species
    // Gen III active slot is already loaded during the probe. Capacity is
    // informational; revalidate the complete file before any transfer.
    int freeBoxSlots = -1;
    bool boxIntegrityOk = false;
    uint8_t language = 0;
    long fileSize = 0;            // file size and modification time when it was read:
    long modified = 0;            // findPokemonSaves() reuses the summary while both match
};

// Loads `path` if it is a supported Pokémon save (other files, including
// other games' saves, are rejected quickly by size or format).
bool probeSave(const std::string& path, SaveSummary& out);

// Same result as probeSave() but reading the whole file (the reference the partial
// reads of probeSave() must agree with; probeSave() falls back to it when in doubt).
bool probeSaveFull(const std::string& path, SaveSummary& out);

// Every supported Pokémon save up to `maxDepth` folders below `root`. Files whose size and
// modification time match an entry of `previous` (an earlier scan) are not read again.
std::vector<SaveSummary> findPokemonSaves(const std::string& root, int maxDepth,
                                          const std::vector<SaveSummary>* previous = nullptr);

// Cooperative scan: bounded directory work and at most one candidate probe
// per step. Summaries are hints, never authorization to edit a save.
class IncrementalSaveScan {
public:
    IncrementalSaveScan(const std::string& root,int maxDepth,uint8_t maxGeneration,
                        const std::vector<SaveSummary>& previous);
    ~IncrementalSaveScan();
    bool step();
    size_t examined() const;
    size_t found() const;
    std::vector<SaveSummary> take();
private:
    struct State;
    std::unique_ptr<State> state_;
};

// "Diamond", "Black 2", ... ("Pokemon" if unknown).
const char* versionName(uint8_t version);

// Species on the box art of a game version (Dialga for Diamond, ...).
uint16_t coverSpecies(uint8_t version);

}  // namespace pktool::saves
