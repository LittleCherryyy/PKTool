// SPDX-License-Identifier: GPL-3.0-or-later
#include "experiments/pccs/transport_visual.h"
#include <cassert>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
namespace v=pktool::transport_visual;namespace io=pktool::io;
using E=v::Edition;using C=v::Console;using G=pktool::saves::Game;using S=v::SpriteSet;
int main() {
    for(auto e:{E::Red,E::Green,E::Blue,E::Yellow}) assert(v::console(e)==C::DMG);
    for(auto e:{E::Gold,E::Silver,E::Crystal}) assert(v::console(e)==C::GBCPurple);
    for(auto e:{E::Ruby,E::Sapphire,E::FireRed,E::LeafGreen}) assert(v::console(e)==C::GBAIndigo);
    assert(v::console(E::Emerald)==C::GBASP&&v::console(E::Bank)==C::Bank);
    assert(v::spriteSet({E::Red,true})==S::RedGreenJP&&v::spriteSet({E::Blue,true})==S::BlueJP);
    assert(v::spriteSet({E::Blue,false})==S::RedBlueINT&&v::spriteSet({E::Green,false})==S::Unknown);
    assert(v::spriteSet({E::Gold,false})!=v::spriteSet({E::Silver,false}));
    assert(v::spriteSet({E::Emerald,false})!=v::spriteSet({E::Ruby,false}));
    assert(v::spriteSet({E::Bank,false})==S::BankClassic&&v::spriteSet({E::Bank,false},true)==S::BankPK3);

    // VPT031: edition-specific sheets must not silently alias other editions.
    assert(std::string(v::spriteKind({E::Red,true}))=="transport-redgreen-jp");
    assert(std::string(v::spriteKind({E::Red,false}))=="transport-redblue-int");
    assert(std::string(v::spriteKind({E::Blue,true}))=="transport-blue-jp");
    assert(std::string(v::spriteKind({E::Yellow,false}))=="transport-yellow");
    assert(std::string(v::spriteKind({E::Gold,false},true))=="transport-gold-shiny");
    assert(std::string(v::spriteKind({E::Silver,false},true))=="transport-silver-shiny");
    assert(std::string(v::spriteKind({E::Crystal,false},false))=="transport-crystal");
    assert(std::string(v::spriteKind({E::Ruby,false},false))=="transport-rs");
    assert(std::string(v::spriteKind({E::Sapphire,false},true))=="transport-rs-shiny");
    assert(std::string(v::spriteKind({E::FireRed,false},true))=="transport-frlg-shiny");
    assert(std::string(v::spriteKind({E::LeafGreen,false}))=="transport-frlg");
    assert(std::string(v::spriteKind({E::Emerald,false}))=="transport-emerald");
    assert(v::spriteKind({E::Unknown,false})==nullptr);
    assert(v::spriteKind({E::Bank,false})==nullptr);
    assert(v::spriteKind({E::Bank,false},false,true)==nullptr);
    assert(std::string(v::spriteKind({E::Blue,false}))==std::string(v::spriteKind({E::Red,false})));
    assert(std::string(v::spriteKind({E::Ruby,false}))!=std::string(v::spriteKind({E::Emerald,false})));
    assert(v::options(G::RedBlue,true).size()==3&&v::options(G::RedBlue,false).size()==2);
    assert(v::filenameHint("/Gold/Pokemon - Silver Version (USA).sav",v::options(G::GoldSilver,false))==E::Silver);
    assert(v::filenameHint("Pokémon - Fire Red (USA).sav",v::options(G::FireRedLeafGreen,false))==E::FireRed);
    assert(v::filenameHint("Pokemon - LeafGreen.sav",v::options(G::FireRedLeafGreen,false))==E::LeafGreen);
    assert(v::filenameHint("silver-gold.sav",v::options(G::GoldSilver,false))==E::Unknown);
    assert(v::filenameHint("redacted.sav",v::options(G::RedBlue,false))==E::Unknown);
    assert(v::filenameHint("Pokemon Green.sav",v::options(G::RedBlue,false))==E::Unknown);
    char tmp[]="/tmp/vpt030-XXXXXX";assert(mkdtemp(tmp));
    struct Cleanup{const char* p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{tmp};
    const std::string root=tmp;
    auto makeGba=[](const std::string& code){
        std::vector<uint8_t> h(0xc0);
        std::copy(code.begin(),code.end(),h.begin()+0xac);h[0xb2]=0x96;
        uint8_t s=0;for(int i=0xa0;i<=0xbc;++i)s=static_cast<uint8_t>(s-h[i]);
        h[0xbd]=static_cast<uint8_t>(s-0x19);return h;
    };
    auto makeGb=[](const std::string& title,bool jp){
        std::vector<uint8_t> h(0x150);
        std::copy(title.begin(),title.end(),h.begin()+0x134);h[0x14a]=jp?0:1;
        uint8_t s=0;for(int i=0x134;i<=0x14c;++i)s=static_cast<uint8_t>(s-h[i]-1);
        h[0x14d]=s;return h;
    };
    auto put=[&](const std::string& path,const std::vector<uint8_t>& data){assert(io::safeWriteFile(path,data)==io::WriteResult::Ok);};
    put(root+"/ruby.gba",makeGba("AXPE"));
    io::resetReadStats();
    auto r=v::resolve(root+"/ruby.sav",G::RubySapphire,false);
    assert(r.suggested==E::Sapphire&&r.evidence==v::Evidence::RomHeader);
    assert(io::readStats().bytes==0xc0);
    r=v::resolve(root+"/ruby.sav",G::FireRedLeafGreen,false);
    assert(r.suggested==E::Unknown&&r.evidence==v::Evidence::Conflict);
    auto bad=makeGba("AXPE");bad[0xbd]^=1;put(root+"/ruby.gba",bad);
    r=v::resolve(root+"/ruby.sav",G::RubySapphire,false);
    assert(r.suggested==E::Ruby&&r.evidence==v::Evidence::Filename);
    assert(io::makeDirectories(root+"/saves"));
    put(root+"/gold.gbc",makeGb("POKEMON_SLV",false));
    r=v::resolve(root+"/saves/gold.sav",G::GoldSilver,false);
    assert(r.suggested==E::Silver&&r.evidence==v::Evidence::RomHeader);
    put(root+"/gold.gb",makeGb("POKEMON_GLD",false));
    r=v::resolve(root+"/saves/gold.sav",G::GoldSilver,false);
    assert(r.suggested==E::Unknown&&r.evidence==v::Evidence::Conflict);
    r=v::resolve(root+"/unlabeled.sav",G::GoldSilver,false);
    assert(r.suggested==E::Unknown&&r.choices.size()==2);
    r=v::resolve(root+"/Pokemon Green.sav",G::RedBlue,true);
    assert(r.suggested==E::Green&&r.evidence==v::Evidence::Filename);
    r=v::resolve(root+"/unlabeled.sav",G::Emerald,false);
    assert(r.suggested==E::Emerald&&r.evidence==v::Evidence::SaveFamily);
    std::puts("PASS: all edition/model mappings, JP choices, filename boundaries, bounded ROM header reads, conflicts, sibling saves and unknown editions");
}
