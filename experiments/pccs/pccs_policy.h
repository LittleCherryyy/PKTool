// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/legality/legality.h"
#include <string>
#include <vector>

namespace pktool::pokemon::pccs_bridge {
// Keep every mismatch visible and fail closed. Only the exact English
// Gen II in-game trade token may expand into its rendered Gen III name.
inline bool checkProtectedIdentity(const Pokemon& a, const Pokemon& b, bool tradeOt,
                                   std::vector<std::string>& notes) {
    bool ok = true;
    const auto compare = [&](const char* label, unsigned from, unsigned to) {
        if (from != to) {
            notes.push_back(std::string(label) + ": " + std::to_string(from) + " -> " + std::to_string(to));
            ok = false;
        }
    };
    compare("Species", a.species, b.species);
    compare("Egg", 0, b.isEgg);
    compare("Shiny", a.shiny, b.shiny);
    compare("Gender (0 M,1 F,2 none)", unsigned(a.gender), unsigned(b.gender));
    compare("TID", a.trainer.tid, b.trainer.tid);
    if (a.trainer.name != b.trainer.name) {
        if (tradeOt && a.language == 2 && b.language == 2 &&
            a.trainer.name == "*" && b.trainer.name == "TRAINER") {
            notes.push_back("OT: GB trade token -> TRAINER");
        } else {
            notes.push_back("OT: [" + a.trainer.name + "] -> [" + b.trainer.name + "]");
            ok = false;
        }
    }
    compare("OT gender", unsigned(a.trainer.gender), unsigned(b.trainer.gender));
    compare("Language", a.language, b.language);
    if (a.nickname != b.nickname) {
        notes.push_back("Nickname: [" + a.nickname + "] -> [" + b.nickname + "]");
        ok = false;
    }
    compare("Form", a.form, b.form);
    return ok;
}
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
