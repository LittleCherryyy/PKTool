// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/legality/legality.h"

namespace pktool::pokemon::pccs_bridge {
// Applies ONLY to fresh output of the pinned PCCS LEGAL conversion, not
// ordinary Pokemon or arbitrary PK3 imports. Do not modify the base checker.
// Keep the warning in the preview and provenance; never downgrade Invalid.
inline bool acceptsFreshLegalOutput(const Pokemon& p, const legality::Report& r) {
    if (r.overall() == legality::Severity::Invalid) return false;
    for (const auto& f : r.findings) {
        if (f.severity == legality::Severity::Valid) continue;
        if (f.severity != legality::Severity::Warning ||
            f.text != "SID is 0" || p.format != 3 ||
            p.originGame != legality::version::kFireRed ||
            p.trainer.tid == 0 || p.trainer.sid != 0 ||
            p.shiny || pokemon::isShiny(p.pid, p.trainer.tid, p.trainer.sid) ||
            p.isEgg || p.species == 151 || p.species == 251) return false;
    }
    return true;
}
}
