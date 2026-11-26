// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/legality/legality.h"
#include "core/pokemon/pk2.h"
#include "core/data/gen12.h"
#include "core/text/gen12_text.h"
#include <array>
#include <string>
#include <vector>

namespace pktool::pokemon::pccs_bridge {
// English Gen II glyph bytes E1 (<PK>) and E2 (<MN>) are displayed
// as braces by the PKTool GB decoder. The pinned PCCS charset intentionally
// transcribes them to P/M. Derive that exact expected spelling from raw bytes;
// never normalize arbitrary strings or ignore suffixes/other changes.
// The original entity remains unmodified and retained in the save journal.
inline bool pccsNameGlyphEquivalent(const uint8_t* raw, size_t maxChars,
                                    const std::string& original, const std::string& target, uint8_t generation = 2) {
    if (!raw || maxChars > pk2::kNicknameChars) return false;
    std::array<uint8_t, pk2::kStringBytes> mapped{};
    bool changed = false;
    size_t n = 0;
    for (; n < pk2::kStringBytes; ++n) {
        const uint8_t b = raw[n];
        if (b == 0x50) { mapped[n] = b; break; }
        if (n >= maxChars || b == 0x5D || data::charGb(generation,2,b) == 0xffff) return false;
        mapped[n] = b == 0xE1 ? 0x8F : b == 0xE2 ? 0x8C : b;
        changed |= mapped[n] != b;
    }
    if (!changed || n == pk2::kStringBytes) return false;
    return text::decodeGb(generation,raw,pk2::kStringBytes,2) == original &&
           text::decodeGb(generation,mapped.data(),pk2::kStringBytes,2) == target;
}
// Keep every mismatch visible and fail closed, except exact documented PCCS
// transcriptions from the original English raw name bytes.
inline bool checkProtectedIdentity(const Pokemon& a, const Pokemon& b, bool tradeOt,
                                   std::vector<std::string>& notes,
                                   const uint8_t* rawOt = nullptr,
                                   const uint8_t* rawNickname = nullptr) {
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
        } else if (a.language == 2 && b.language == 2 &&
                   pccsNameGlyphEquivalent(rawOt,pk2::kOtNameChars,a.trainer.name,b.trainer.name,a.format == 1 ? 1 : 2)) {
            notes.push_back("OT: PCCS PK/MN glyphs -> P/M: [" + a.trainer.name + "] -> [" + b.trainer.name + "]");
        } else {
            notes.push_back("OT: [" + a.trainer.name + "] -> [" + b.trainer.name + "]");
            ok = false;
        }
    }
    compare("OT gender", unsigned(a.trainer.gender), unsigned(b.trainer.gender));
    compare("Language", a.language, b.language);
    if (a.nickname != b.nickname) {
        if (a.language == 2 && b.language == 2 &&
            pccsNameGlyphEquivalent(rawNickname,pk2::kNicknameChars,a.nickname,b.nickname,a.format == 1 ? 1 : 2)) {
            notes.push_back("Nickname: PCCS PK/MN glyphs -> P/M: [" + a.nickname + "] -> [" + b.nickname + "]");
        } else {
            notes.push_back("Nickname: [" + a.nickname + "] -> [" + b.nickname + "]");
            ok = false;
        }
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
