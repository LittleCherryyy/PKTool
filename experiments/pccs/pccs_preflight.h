// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace pktool::pokemon::pccs_bridge {
// PKTool's pinned PK2 entity layout. No save checksum validation here.
enum class Preflight { Ready, InvalidSize, UnsupportedLanguage, Egg,
                       InvalidSpecies, HeldItem, Mythical, InvalidLevel };
inline bool supportedLanguage(uint8_t lang) {
    return lang>=1 && lang<=5 || lang==7;
}
inline Preflight preflight(const uint8_t* data, std::size_t size) {
    if (!data || size != 72) return Preflight::InvalidSize;
    if (!supportedLanguage(data[70])) return Preflight::UnsupportedLanguage;
    if (data[71] != 0) return Preflight::Egg;
    if (data[0] == 0 || data[0] > 251) return Preflight::InvalidSpecies;
    if (data[1] != 0) return Preflight::HeldItem;
    if (data[0] == 151 || data[0] == 251) return Preflight::Mythical;
    if (data[31] == 0 || data[31] > 100) return Preflight::InvalidLevel;
    return Preflight::Ready;
}
}
