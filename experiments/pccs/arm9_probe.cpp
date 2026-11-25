// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only ARM9 entry point. Never write a save or initialize FAT.
#include <nds.h>
#include <cstdio>
#include <array>
#include "experiments/pccs/pccs_bridge.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk2.h"
#include "core/legality/legality.h"

void vptArm9Probe() {
    namespace pk = pktool::pokemon;
    consoleDemoInit();
    std::printf("VPT003 ARM9 read-only probe\n");
    pk::create4::Trainer trainer;
    trainer.name = "ASH";
    trainer.tid = 12345;
    trainer.version = pktool::legality::version::kCrystal;
    trainer.language = 2;
    int passed = 0;
    for (uint16_t species : {155, 158, 152}) {
        auto model = pk::create4::newPokemon(trainer, species, 5, 100 + species);
        model.heldItem = 0;
        std::array<uint8_t, pk::entity::kMaxPartySize> entity{};
        pk::create4::encode(model, entity.data());
        std::printf("Testing species %u...\n", unsigned(species));
        auto result = pk::pccs_bridge::previewCrystalToRuby(entity.data(), pk::pk2::kEntitySize);
        std::printf("%s\n", result.ok ? "PASS" : result.error.c_str());
        passed += result.ok;
    }
    std::printf("%d/3 accepted. A: continue\n", passed);
    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_A) break;
    }
}
