#include "ds/gfx/sprites.h"

#include <algorithm>
#include <utility>

#include "core/io/file_io.h"
#include "core/util/bytes.h"

namespace pktool::gfx {
namespace {

// Bounds memory use (the DS heap is ~2.5 MB and saving needs room for a
// 512 KB save): box icons are ~2.4 KB, front sprites ~12.8 KB.
constexpr size_t kMaxCachedSprites = 96;
constexpr size_t kMaxCachedBytes = 128 * 1024;
constexpr int kMaxDimension = 128;

}  // namespace

bool SpriteStore::parse(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint16_t>& pixels) {
    if (bytes.size() < 8 || bytes[0] != 'P' || bytes[1] != 'K' || bytes[2] != 'S' ||
        (bytes[3] != '1' && bytes[3] != '2')) return false;
    width=util::readU16(&bytes[4]);
    height=util::readU16(&bytes[6]);
    if(width<=0||height<=0||width>kMaxDimension||height>kMaxDimension) return false;
    const size_t count=static_cast<size_t>(width)*height;
    if(bytes[3]=='1') {
        if(bytes.size()<8+count*2) return false;
        pixels.resize(count);
        for(size_t i=0;i<count;++i) pixels[i]=util::readU16(&bytes[8+i*2]);
        return true;
    }
    // PKS2: compact lossless 16-bit RLE. Each control byte packs 1..128
    // pixels: bit7=repeat a 16-bit colour, otherwise raw literal colours.
    // Unlike PNG/zlib this has no extra library, heap or CPU setup on ARM9.
    // Decode into a local vector so malformed packs never poison the cache.
    std::vector<uint16_t> decoded(count);
    size_t offset=8,at=0;
    while(at<count) {
        if(offset>=bytes.size()) return false;
        const uint8_t token=bytes[offset++];
        const size_t n=static_cast<size_t>(token&0x7F)+1;
        if(n>count-at) return false;
        if(token&0x80) {
            if(offset+2>bytes.size()) return false;
            const uint16_t colour=util::readU16(&bytes[offset]);
            offset+=2;
            std::fill(decoded.begin()+at,decoded.begin()+at+n,colour);
        } else {
            if(offset+2*n>bytes.size()) return false;
            for(size_t i=0;i<n;++i)
                decoded[at+i]=util::readU16(&bytes[offset+2*i]);
            offset+=2*n;
        }
        at+=n;
    }
    if(offset!=bytes.size()) return false;
    pixels=std::move(decoded);
    return true;
}

int SpriteStore::kindIndex(const char* kind) {
    for (size_t i = 0; i < kinds_.size(); ++i) {
        if (kinds_[i] == kind) return static_cast<int>(i);
    }
    kinds_.push_back(kind);
    return static_cast<int>(kinds_.size() - 1);
}

Image SpriteStore::get(const char* kind, uint16_t species) {
    // Called for every icon of every frame: the lookup must not build strings or allocate.
    const int kindId = kindIndex(kind);
    const uint32_t id = (static_cast<uint32_t>(kindId) << 16) | species;
    auto it = cache_.find(id);
    if (it != cache_.end()) {
        recent_.splice(recent_.begin(), recent_, it->second.recent);
    } else {
        Entry entry;
        std::vector<uint8_t> bytes;
        const std::string* dir = directory();
        // A packed miss may be supplied as a loose override on SD.
        const std::string key = std::string(kind) + "/" + std::to_string(species);
        bool loaded = false;
        if (dir != nullptr) {
            loaded = readPacked(kindId, species, bytes) && !bytes.empty();
            if (!loaded) {
                for (const auto& candidate : directories_) {
                    if (io::readFile(io::joinPath(candidate, key + ".bin"), bytes)) {
                        loaded = true;
                        break;
                    }
                }
            }
        }
        if (loaded && !parse(bytes, entry.width, entry.height, entry.pixels))
            entry.pixels.clear();

        // The exact budget applies to the *new* decoded image as well. The
        // old pre-insertion check could overshoot 128 KiB on every load.
        const size_t incoming = entry.pixels.size() * sizeof(uint16_t);
        while (!recent_.empty() &&
               (cache_.size() >= kMaxCachedSprites ||
                incoming > kMaxCachedBytes - cachedBytes_)) {
            const uint32_t oldestId = recent_.back();
            const auto oldest = cache_.find(oldestId);
            cachedBytes_ -= oldest->second.pixels.size() * sizeof(uint16_t);
            cache_.erase(oldest);
            recent_.pop_back();
        }
        cachedBytes_ += incoming;
        recent_.push_front(id);
        entry.recent = recent_.begin();
        it = cache_.emplace(id, std::move(entry)).first;
    }
    Image image;
    if (!it->second.pixels.empty()) {
        image.width = it->second.width;
        image.height = it->second.height;
        image.pixels = it->second.pixels.data();
    }
    return image;
}

const std::string* SpriteStore::directory() {
    if (chosen_ == -2) {
        chosen_ = -1;
        for (size_t i = 0; i < directories_.size(); ++i) {
            if (io::fileExists(io::joinPath(directories_[i], "icons.pak")) ||
                io::fileExists(io::joinPath(directories_[i], "icons/25.bin"))) {
                chosen_ = static_cast<int>(i);
                break;
            }
        }
    }
    return chosen_ >= 0 ? &directories_[chosen_] : nullptr;
}

void SpriteStore::clear() {
    cache_.clear();
    recent_.clear();
    cachedBytes_ = 0;
    for (Pack& pack : packs_) {
        if (pack.file != nullptr) std::fclose(pack.file);
        pack.file = nullptr;
    }
    packs_.clear();  // release decoded indexes too, not only FILE* objects
    packClock_ = 0;
    chosen_ = -2;
}

SpriteStore::~SpriteStore() {
    clear();
}

bool SpriteStore::readPacked(int kind, uint16_t species, std::vector<uint8_t>& bytes) {
    if (static_cast<size_t>(kind) >= packs_.size()) packs_.resize(static_cast<size_t>(kind) + 1);
    Pack& pack = packs_[static_cast<size_t>(kind)];
    if (!pack.tried) {
        pack.tried = true;
        if (directory() == nullptr) return false;
        // An older NitroFS image may have basic icons but not the optional
        // edition art. Search each directory for this specific pack, in order.
        for (const auto& dir : directories_) {
            const auto path = io::joinPath(dir, kinds_[static_cast<size_t>(kind)] + ".pak");
            if (io::fileExists(path)) { pack.path = path; break; }
        }
    }
    if (pack.path.empty()) return false;
    if (pack.file == nullptr) {
        // Switching among editions must not leave one open FILE* per kind.
        // Keep four (source, destination, shiny and one fallback) and reuse
        // the parsed index when an older pack is reopened.
        constexpr size_t kMaxOpenPacks = 4;
        size_t open = 0;
        size_t oldest = packs_.size();
        for (size_t i = 0; i < packs_.size(); ++i) {
            if (!packs_[i].file) continue;
            ++open;
            if (i != static_cast<size_t>(kind) &&
                (oldest == packs_.size() || packs_[i].lastUsed < packs_[oldest].lastUsed))
                oldest = i;
        }
        if (open >= kMaxOpenPacks && oldest != packs_.size()) {
            std::fclose(packs_[oldest].file);
            packs_[oldest].file = nullptr;
        }
        std::FILE* file = std::fopen(pack.path.c_str(), "rb");
        if (file == nullptr) return false;
        if (pack.offsets.empty()) {
            uint8_t header[8];
            if (std::fread(header, 1, sizeof header, file) != sizeof header ||
                header[0] != 'P' || header[1] != 'K' ||
                header[2] != 'S' || header[3] != 'P') {
                std::fclose(file); return false;
            }
            const size_t count = util::readU16(header + 4);
            if (count > 2048) { std::fclose(file); return false; }
            std::vector<uint8_t> table((count + 2) * 4);
            if (std::fread(table.data(), 1, table.size(), file) != table.size()) {
                std::fclose(file); return false;
            }
            pack.offsets.resize(count + 2);
            for (size_t i = 0; i < pack.offsets.size(); ++i)
                pack.offsets[i] = util::readU32(&table[i * 4]);
        }
        pack.file = file;
    }
    pack.lastUsed = ++packClock_;
    bytes.clear();
    if (species + 1u >= pack.offsets.size()) return true;
    const uint32_t begin = pack.offsets[species];
    const uint32_t end = pack.offsets[species + 1u];
    if (end <= begin || end - begin > 8 + 2u * kMaxDimension * kMaxDimension) return true;
    bytes.resize(end - begin);
    if (std::fseek(pack.file, static_cast<long>(begin), SEEK_SET) != 0 ||
        std::fread(bytes.data(), 1, bytes.size(), pack.file) != bytes.size()) {
        bytes.clear();
    }
    return true;
}

bool SpriteStore::available() {
    return directory() != nullptr;
}

}  // namespace pktool::gfx
