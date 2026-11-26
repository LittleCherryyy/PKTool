// VPT031 sprite pack file-handle regression (host, no copyrighted sprite assets).
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include "core/io/file_io.h"
#include "ds/gfx/sprites.h"
#include "tests/test.h"

using namespace pktool;

namespace {

std::vector<uint8_t> singleSpritePack(uint16_t color) {
    // PKSP with one indexed sprite (species 1); PKS1 1x1 at offset 20.
    std::vector<uint8_t> bytes = {'P','K','S','P',1,0,0,0};
    for (uint32_t offset : {20u,20u,30u}) {
        for (int i=0;i<4;++i) bytes.push_back(static_cast<uint8_t>(offset>>(i*8)));
    }
    const uint8_t body[] = {'P','K','S','1',1,0,1,0,
                            static_cast<uint8_t>(color),static_cast<uint8_t>(color>>8)};
    bytes.insert(bytes.end(),body,body+sizeof body);
    return bytes;
}

// Synthetic 96x96 historical sprites: 18 KiB each, deliberately chosen so
// seven sprites fit in 128 KiB but the eighth must evict an older one.
std::vector<uint8_t> manySpritePack(int count) {
    constexpr int side=96;
    constexpr int blobSize=8 + side*side*2;
    const uint32_t header=8+4*static_cast<uint32_t>(count+2);
    std::vector<uint8_t> bytes={'P','K','S','P',
        static_cast<uint8_t>(count),static_cast<uint8_t>(count>>8),0,0};
    uint32_t offset=header;
    for(int i=0;i<count+2;++i) {
        for(int j=0;j<4;++j) bytes.push_back(static_cast<uint8_t>(offset>>(j*8)));
        if(i>=1&&i<=count) offset+=blobSize;
    }
    for(int species=1;species<=count;++species) {
        bytes.insert(bytes.end(),{'P','K','S','1',side,0,side,0});
        const uint16_t color=static_cast<uint16_t>(0x8000+species);
        for(int i=0;i<side*side;++i) {
            bytes.push_back(static_cast<uint8_t>(color));
            bytes.push_back(static_cast<uint8_t>(color>>8));
        }
    }
    return bytes;
}

}  // namespace

TEST(transport_sprite_packs_reopen_after_handle_eviction) {
    char root[]="/tmp/pktool-art-XXXXXX";
    CHECK(mkdtemp(root)!=nullptr);
    const std::string base=std::string(root)+"/nitro";
    const std::string extra=std::string(root)+"/sd";
    CHECK(io::makeDirectories(base));
    CHECK(io::makeDirectories(extra));
    CHECK(io::safeWriteFile(base+"/icons.pak",singleSpritePack(0x8001))==io::WriteResult::Ok);
    // Base Nitro directory has icons only. Exact edition art lives in SD.
    for (int i=0;i<10;++i)
        CHECK(io::safeWriteFile(extra+"/transport-test-"+std::to_string(i)+".pak",
                                singleSpritePack(static_cast<uint16_t>(0x8001+i)))==io::WriteResult::Ok);
    {
        gfx::SpriteStore store({base,extra});
        CHECK(store.available());
        for (int i=0;i<10;++i) {
            CHECK(store.openPackCount()<=4);
            const std::string kind="transport-test-"+std::to_string(i);
            const auto image=store.get(kind.c_str(),1);
            CHECK(image.pixels!=nullptr);
            CHECK(image.width==1&&image.height==1);
            CHECK(image.pixels[0]==0x8001+i);
            CHECK(store.openPackCount()<=4);
        }
        CHECK(store.openPackCount()==4);
        store.clear();
        CHECK(store.openPackCount()==0);
        CHECK(store.cachedPixelBytes()==0);  // Force re-reads of the early packs after LRU eviction.
        for (int i=9;i>=0;--i) {
            const std::string kind="transport-test-"+std::to_string(i);
            const auto image=store.get(kind.c_str(),1);
            CHECK(image.pixels!=nullptr);
            CHECK(image.pixels[0]==0x8001+i);
        }
        const auto missing=store.get("transport-not-installed",1);
        CHECK(missing.pixels==nullptr);
        const auto icons=store.get("icons",1);
        CHECK(icons.pixels!=nullptr&&icons.pixels[0]==0x8001);
        CHECK(store.openPackCount()<=4);
    }
    std::filesystem::remove_all(root);
}

TEST(transport_sprite_cache_respects_strict_128k_budget) {
    char root[]="/tmp/pktool-membudget-XXXXXX";
    CHECK(mkdtemp(root)!=nullptr);
    const std::string directory=root;
    CHECK(io::safeWriteFile(directory+"/icons.pak",singleSpritePack(0x8001))==io::WriteResult::Ok);
    CHECK(io::safeWriteFile(directory+"/transport-stress.pak",manySpritePack(12))==io::WriteResult::Ok);
    {
        gfx::SpriteStore store({directory});
        for(uint16_t species=1;species<=12;++species) {
            auto image=store.get("transport-stress",species);
            CHECK(image.width==96&&image.height==96&&image.pixels!=nullptr);
            CHECK(image.pixels[0]==static_cast<uint16_t>(0x8000+species));
            CHECK(store.cachedPixelBytes()<=128u*1024u);
            CHECK(store.openPackCount()<=4);
        }
        CHECK(store.cachedPixelBytes()<=128u*1024u);
        store.clear();
        CHECK(store.cachedPixelBytes()==0);
        CHECK(store.openPackCount()==0);
        // Clear must not lose access to the pack or break its lookup table.
        auto image=store.get("transport-stress",1);
        CHECK(image.pixels!=nullptr&&image.pixels[0]==0x8001);
    }
    std::filesystem::remove_all(root);
}
