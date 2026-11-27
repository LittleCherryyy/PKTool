// SPDX-License-Identifier: GPL-3.0-or-later
// ARM9 link-anchor used by the private Transporter candidate. Never touch FAT.
// An interactive probe can be enabled explicitly only in a diagnostic build.
#include <nds.h>
#include <cstdio>
#include <array>
#include "experiments/pccs/pccs_bridge.h"
#include "experiments/pccs/transfer_journal.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk2.h"
#include "core/legality/legality.h"

void vptArm9Probe() {
    namespace pk = pktool::pokemon;
    // Keep journal symbols linked without executing any storage operation.
    // Volatile function pointers ensure that linker GC cannot discard them.
    auto volatile prepare = &pk::pccs_bridge::prepareJournal;
    auto volatile resume = &pk::pccs_bridge::resumeJournal;
    // Previously the interactive diagnostic called this preview. Without
    // that console screen the linker needs an explicit, non-executed anchor
    // to keep the exact PCCS entry point and its GB conversion dependency.
    auto volatile preview = &pk::pccs_bridge::previewCrystalToRuby;
    (void)prepare;
    (void)resume;
    (void)preview;
    // The hardware build always calls this function to keep the PCCS/Journaling
    // link anchors. Production/private candidates must never initialize the
    // console overlay or pause for a test-screen confirmation.
#if defined(VPT_BOOT_DIAGNOSTIC)
    consoleDemoInit();
    std::printf("VPT006 ARM9 read-only probe\n");
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
#endif  // VPT_BOOT_DIAGNOSTIC
}
