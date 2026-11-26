// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/saves/game.h"
#include "core/io/file_io.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace pktool::transport_visual {
enum class Edition : uint8_t {
    Unknown, Red, Green, Blue, Yellow, Gold, Silver, Crystal, Ruby, Sapphire,
    FireRed, LeafGreen, Emerald, Bank
};
enum class Console : uint8_t { Unknown, DMG, GBCPurple, GBAIndigo, GBASP, Bank };
// Logical asset identities for VPT031. No pack is silently aliased here.
enum class SpriteSet : uint8_t {
    Unknown, RedGreenJP, BlueJP, RedBlueINT, Yellow, Gold, Silver, Crystal,
    RubySapphire, FireRedLeafGreen, Emerald, BankClassic, BankPK3
};
enum class Evidence : uint8_t { None, SaveFamily, RomHeader, Filename, Conflict, User };
struct VisualEdition {
    Edition edition=Edition::Unknown;
    bool japanese=false;
};
inline const char* name(Edition e) {
    switch(e) {
        case Edition::Red:return "Red"; case Edition::Green:return "Green";
        case Edition::Blue:return "Blue"; case Edition::Yellow:return "Yellow";
        case Edition::Gold:return "Gold"; case Edition::Silver:return "Silver";
        case Edition::Crystal:return "Crystal"; case Edition::Ruby:return "Ruby";
        case Edition::Sapphire:return "Sapphire"; case Edition::FireRed:return "FireRed";
        case Edition::LeafGreen:return "LeafGreen"; case Edition::Emerald:return "Emerald";
        case Edition::Bank:return "Bank"; default:return "Edition unknown";
    }
}
inline std::string label(VisualEdition v) {
    const std::string base=name(v.edition);
    return v.japanese&&v.edition!=Edition::Bank&&v.edition!=Edition::Unknown?base+" (JP)":base;
}
constexpr Console console(Edition e) {
    switch(e) {
        case Edition::Red:case Edition::Green:case Edition::Blue:case Edition::Yellow:return Console::DMG;
        case Edition::Gold:case Edition::Silver:case Edition::Crystal:return Console::GBCPurple;
        case Edition::Ruby:case Edition::Sapphire:case Edition::FireRed:case Edition::LeafGreen:return Console::GBAIndigo;
        case Edition::Emerald:return Console::GBASP;
        case Edition::Bank:return Console::Bank;
        default:return Console::Unknown;
    }
}
inline const char* consoleName(Console c) {
    switch(c) {
        case Console::DMG:return "GAME BOY";
        case Console::GBCPurple:return "GAME BOY COLOR";
        case Console::GBAIndigo:return "GAME BOY ADVANCE";
        case Console::GBASP:return "GBA SP";
        case Console::Bank:return "BANK";
        default:return "UNKNOWN";
    }
}
constexpr SpriteSet spriteSet(VisualEdition v,bool destination=false) {
    switch(v.edition) {
        case Edition::Red:return v.japanese?SpriteSet::RedGreenJP:SpriteSet::RedBlueINT;
        case Edition::Green:return v.japanese?SpriteSet::RedGreenJP:SpriteSet::Unknown;
        case Edition::Blue:return v.japanese?SpriteSet::BlueJP:SpriteSet::RedBlueINT;
        case Edition::Yellow:return SpriteSet::Yellow;
        case Edition::Gold:return SpriteSet::Gold;
        case Edition::Silver:return SpriteSet::Silver;
        case Edition::Crystal:return SpriteSet::Crystal;
        case Edition::Ruby:case Edition::Sapphire:return SpriteSet::RubySapphire;
        case Edition::FireRed:case Edition::LeafGreen:return SpriteSet::FireRedLeafGreen;
        case Edition::Emerald:return SpriteSet::Emerald;
        case Edition::Bank:return destination?SpriteSet::BankPK3:SpriteSet::BankClassic;
        default:return SpriteSet::Unknown;
    }
}
// NitroFS/SD art packs are keyed by game edition, never by the conversion's
// destination generation. Pair editions share one sprite sheet only when the
// pinned source actually provides the same art (R/B international, R/S, FR/LG).
// Japanese Blue is intentionally a distinct, optional user-supplied pack:
// the pinned PokeAPI snapshot has no Japanese Blue front sheet.
inline const char* spriteKind(SpriteSet set,bool shiny=false) {
    switch(set) {
        case SpriteSet::RedGreenJP:return "transport-redgreen-jp";
        case SpriteSet::BlueJP:return "transport-blue-jp";
        case SpriteSet::RedBlueINT:return "transport-redblue-int";
        case SpriteSet::Yellow:return "transport-yellow";
        case SpriteSet::Gold:return shiny?"transport-gold-shiny":"transport-gold";
        case SpriteSet::Silver:return shiny?"transport-silver-shiny":"transport-silver";
        case SpriteSet::Crystal:return shiny?"transport-crystal-shiny":"transport-crystal";
        case SpriteSet::RubySapphire:return shiny?"transport-rs-shiny":"transport-rs";
        case SpriteSet::FireRedLeafGreen:return shiny?"transport-frlg-shiny":"transport-frlg";
        case SpriteSet::Emerald:return shiny?"transport-emerald-shiny":"transport-emerald";
        // A Bank slot does not identify its historic game art edition.
        case SpriteSet::BankClassic:case SpriteSet::BankPK3:case SpriteSet::Unknown:return nullptr;
    }
    return nullptr;
}
inline const char* spriteKind(VisualEdition edition,bool shiny=false,bool destination=false) {
    return spriteKind(spriteSet(edition,destination),shiny);
}
inline std::vector<Edition> options(saves::Game g,bool japanese) {
    using G=saves::Game;using E=Edition;
    switch(g) {
        case G::RedBlue:return japanese?std::vector<E>{E::Red,E::Green,E::Blue}:std::vector<E>{E::Red,E::Blue};
        case G::Yellow:return {E::Yellow};
        case G::GoldSilver:return {E::Gold,E::Silver};
        case G::Crystal:return {E::Crystal};
        case G::RubySapphire:return {E::Ruby,E::Sapphire};
        case G::FireRedLeafGreen:return {E::FireRed,E::LeafGreen};
        case G::Emerald:return {E::Emerald};
        default:return {};
    }
}
inline bool contains(const std::vector<Edition>& choices,Edition e) {
    return std::find(choices.begin(),choices.end(),e)!=choices.end();
}
inline std::string lowerAscii(std::string s) {
    for(auto& c:s) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
    return s;
}
inline std::string stem(const std::string& p) {
    const auto dot=p.find_last_of('.'),slash=p.find_last_of('/');
    return dot!=std::string::npos&&(slash==std::string::npos||dot>slash)?p.substr(0,dot):p;
}
inline Edition filenameHint(const std::string& path,const std::vector<Edition>& choices) {
    auto file=lowerAscii(stem(io::fileName(path)));
    for(auto& c:file) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9'))) c=' ';
    file=" "+file+" ";
    auto word=[&](const std::string& s){return file.find(" "+s+" ")!=std::string::npos;};
    const bool fr=word("firered")||word("fire red"),lg=word("leafgreen")||word("leaf green");
    const std::array<std::pair<Edition,bool>,12> names={{
        {Edition::Red,word("red")&&!fr},{Edition::Green,word("green")&&!lg},
        {Edition::Blue,word("blue")},{Edition::Yellow,word("yellow")},
        {Edition::Gold,word("gold")},{Edition::Silver,word("silver")},
        {Edition::Crystal,word("crystal")},{Edition::Ruby,word("ruby")},
        {Edition::Sapphire,word("sapphire")},{Edition::FireRed,fr},
        {Edition::LeafGreen,lg},{Edition::Emerald,word("emerald")}
    }};
    Edition found=Edition::Unknown;
    for(const auto& n:names) if(n.second) {
        // Conflicting named editions, including another family, are not evidence.
        if(found!=Edition::Unknown||!contains(choices,n.first)) return Edition::Unknown;
        found=n.first;
    }
    return found;
}
// Read only local header bytes. Header checksum verifies the header, not ROM
// authenticity or a cryptographic association between a ROM and a save.
inline Edition gbHeader(const uint8_t* p,size_t n,bool japanese) {
    if(n<0x150||p[0x14a]!=(japanese?0:1)) return Edition::Unknown;
    uint8_t check=0;
    for(size_t i=0x134;i<=0x14c;++i) check=static_cast<uint8_t>(check-p[i]-1);
    if(check!=p[0x14d]) return Edition::Unknown;
    const std::string title(reinterpret_cast<const char*>(p+0x134),11);
    std::string full(reinterpret_cast<const char*>(p+0x134),15);
    auto zero=full.find('\0');if(zero!=std::string::npos) full.resize(zero);
    while(!full.empty()&&full.back()==' ') full.pop_back();
    // Japanese Red/Green/Blue titles are not assumed distinguishable. Confirm
    // the exact edition manually unless a filename supplies a usable hint.
    if(!japanese) {
        if(full=="POKEMON RED") return Edition::Red;
        if(full=="POKEMON BLUE") return Edition::Blue;
    }
    if(full=="POKEMON YELLOW") return Edition::Yellow;
    if(title=="POKEMON_GLD"||full=="POKEMON GOLD") return Edition::Gold;
    if(title=="POKEMON_SLV"||full=="POKEMON SILVER") return Edition::Silver;
    if(full.rfind("PM_CRYSTAL",0)==0) return Edition::Crystal;
    return Edition::Unknown;
}
inline Edition gbaHeader(const uint8_t* p,size_t n,bool japanese) {
    if(n<0xc0||p[0xb2]!=0x96) return Edition::Unknown;
    uint8_t sum=0;
    for(size_t i=0xa0;i<=0xbc;++i) sum=static_cast<uint8_t>(sum-p[i]);
    if(static_cast<uint8_t>(sum-0x19)!=p[0xbd]) return Edition::Unknown;
    const char region=static_cast<char>(p[0xaf]);
    if(japanese?region!='J':std::string("EPFIDS").find(region)==std::string::npos) return Edition::Unknown;
    const std::string code(reinterpret_cast<const char*>(p+0xac),3);
    if(code=="AXV") return Edition::Ruby;
    if(code=="AXP") return Edition::Sapphire;
    if(code=="BPR") return Edition::FireRed;
    if(code=="BPG") return Edition::LeafGreen;
    if(code=="BPE") return Edition::Emerald;
    return Edition::Unknown;
}
struct Resolution {
    std::vector<Edition> choices;
    Edition suggested=Edition::Unknown;
    Evidence evidence=Evidence::None;
};
inline Resolution resolve(const std::string& path,saves::Game family,bool japanese) {
    Resolution r;r.choices=options(family,japanese);
    if(r.choices.empty())return r;
    if(r.choices.size()==1) {r.suggested=r.choices.front();r.evidence=Evidence::SaveFamily;return r;}
    const bool gba=saves::gameGeneration(family)==3;
    std::vector<std::string> stems={stem(path)};
    const auto slash=path.find_last_of('/');
    if(slash!=std::string::npos) {
        const auto dir=path.substr(0,slash);
        if(lowerAscii(io::fileName(dir))=="saves") {
            const auto parent=dir.find_last_of('/');
            if(parent!=std::string::npos) stems.push_back(io::joinPath(dir.substr(0,parent),stem(io::fileName(path))));
        }
    }
    Edition rom=Edition::Unknown;bool conflict=false;
    for(const auto& base:stems) for(const char* ext:gba?std::vector<const char*>{".gba"}:std::vector<const char*>{".gb",".gbc"}) {
        const auto candidate=base+ext;
        if(!io::fileExists(candidate)) continue;
        io::FileReader file;std::array<uint8_t,0x150> header{};
        const size_t length=gba?0xc0:header.size();
        if(!file.open(candidate)||!file.read(0,length,header.data())) continue;
        const auto e=gba?gbaHeader(header.data(),length,japanese):gbHeader(header.data(),length,japanese);
        if(e==Edition::Unknown) continue;
        if(!contains(r.choices,e)||(rom!=Edition::Unknown&&rom!=e)) conflict=true;
        else rom=e;
    }
    if(conflict) {r.evidence=Evidence::Conflict;return r;}
    if(rom!=Edition::Unknown) {r.suggested=rom;r.evidence=Evidence::RomHeader;return r;}
    r.suggested=filenameHint(path,r.choices);
    if(r.suggested!=Edition::Unknown) r.evidence=Evidence::Filename;
    return r;
}
inline const char* evidenceName(Evidence e) {
    switch(e) {
        case Evidence::SaveFamily:return "Save format";
        case Evidence::RomHeader:return "Matching ROM header (verify)";
        case Evidence::Filename:return "Filename hint (verify)";
        case Evidence::Conflict:return "ROM conflict - choose manually";
        case Evidence::User:return "Confirmed this session";
        default:return "Edition unknown - choose manually";
    }
}
}
