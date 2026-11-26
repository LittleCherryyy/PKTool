// SPDX-License-Identifier: GPL-3.0-or-later
#include "pccs_preflight.h"
#include <array>
#include <cassert>
#include <cstdio>
using pktool::pokemon::pccs_bridge::Preflight;
using pktool::pokemon::pccs_bridge::preflight;
int main() {
    std::array<uint8_t,72> fixture{};
    fixture[0]=155; fixture[31]=10; fixture[70]=2;
    auto test=[&](std::size_t index,uint8_t value,Preflight expected){
        auto copy=fixture; copy[index]=value;
        assert(preflight(copy.data(),copy.size())==expected);
    };
    assert(preflight(fixture.data(),fixture.size())==Preflight::Ready);
    assert(preflight(nullptr,72)==Preflight::InvalidSize);
    assert(preflight(fixture.data(),71)==Preflight::InvalidSize);
    for(uint8_t lang:{1,2,3,4,5,7}) test(70,lang,Preflight::Ready);
    test(70,8,Preflight::UnsupportedLanguage);
    test(70,6,Preflight::UnsupportedLanguage);
    test(70,0,Preflight::UnsupportedLanguage);
    test(71,1,Preflight::Egg);
    test(0,0,Preflight::InvalidSpecies);
    test(0,252,Preflight::InvalidSpecies);
    test(1,1,Preflight::HeldItem);
    test(0,151,Preflight::Mythical);
    test(0,251,Preflight::Mythical);
    test(31,0,Preflight::InvalidLevel);
    test(31,101,Preflight::InvalidLevel);
    std::puts("PASS: multilingual preflight assertions (synthetic entities; no conversion or saves tested)");
}
