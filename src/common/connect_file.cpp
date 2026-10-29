#include "port/connect_file.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>

namespace tenchu::con {
namespace fs = std::filesystem;

namespace {
constexpr char kMagic[] = "ConnectFile Version ";
constexpr uint32_t kSector = 2048, kNameBytes = 44, kMaxEntries = 100000;
uint32_t Round(uint64_t n) { return uint32_t((n + kSector - 1) / kSector * kSector); }
uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
void Put(std::vector<uint8_t>& b, uint32_t v) { uint8_t t[4]; std::memcpy(t, &v, 4); b.insert(b.end(), t, t + 4); }
}  // namespace

Source FileSource(const fs::path& path) {
    auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
    return [file](uint64_t offset, void* out, uint32_t len) {
        if (!*file) file->clear();
        file->seekg(std::streamoff(offset));
        file->read(static_cast<char*>(out), len);
        return bool(*file) && file->gcount() == std::streamsize(len);
    };
}

void Crypt(uint8_t* data, size_t size, uint32_t key) {
    uint8_t k[4];
    std::memcpy(k, &key, 4);
    for (size_t i = 0; i < size; i++) data[i] ^= k[i & 3];
}

bool Parse(const Source& source, uint64_t total, Archive& a, std::string& error) {
    uint8_t meta[8];
    if (!source(0, a.header, 24) || !source(24, meta, 8) || std::memcmp(a.header, kMagic, 20) || a.header[20] != '6') {
        error = "not a ConnectFile 6 archive";
        return false;
    }
    a.key = ~U32(a.header);
    Crypt(meta, 8, a.key);
    a.data_offset = U32(meta);
    const uint32_t count = U32(meta + 4);
    if (count > kMaxEntries || a.data_offset < 32 + 52ull * count || a.data_offset > total) {
        error = "invalid archive table";
        return false;
    }
    std::vector<uint8_t> table(size_t(count) * 52);
    if (count && !source(32, table.data(), uint32_t(table.size()))) {
        error = "truncated archive table";
        return false;
    }
    Crypt(table.data(), table.size(), a.key);
    a.entries.resize(count);
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; i++) {
        Entry& e = a.entries[i];
        e.size = U32(&table[4 * i]);
        e.offset = U32(&table[4 * (count + i)]);
        const char* name = reinterpret_cast<const char*>(&table[8 * count + kNameBytes * i]);
        e.name.assign(name, strnlen(name, kNameBytes));
        if (SafePath(e.name).empty() || !seen.insert(Key(e.name)).second) {
            error = "bad entry name " + e.name;
            return false;
        }
        if (e.offset < a.data_offset || uint64_t(e.offset) + e.size > total) {
            error = "entry outside the archive: " + e.name;
            return false;
        }
    }
    return true;
}

bool ReadEntry(const Source& source, const Entry& e, std::vector<uint8_t>& out) {
    out.resize(e.size);
    if (e.size && !source(e.offset, out.data(), e.size)) return false;
    Crypt(out.data(), out.size(), ~e.size);
    return true;
}

std::string Key(const std::string& name) {
    std::string k;
    for (char c : name) k += c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
    return k;
}

fs::path SafePath(const std::string& name) {
    if (name.empty() || name.find(':') != std::string::npos || name[0] == '\\' || name[0] == '/') return {};
    fs::path p;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = std::min(name.find_first_of("\\/", start), name.size());
        std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.find_first_of("<>\"|?*") != std::string::npos) return {};
        p /= fs::path(std::u8string(part.begin(), part.end()));
        start = end + 1;
    }
    return p;
}

bool Write(const Source& source, const Archive& a, const std::map<std::string, std::vector<uint8_t>>& replacements,
           const fs::path& out, std::string& error) {
    const uint32_t count = uint32_t(a.entries.size());
    std::vector<uint32_t> sizes(count), offsets(count);
    for (uint32_t i = 0; i < count; i++) {
        auto r = replacements.find(Key(a.entries[i].name));
        sizes[i] = r == replacements.end() ? a.entries[i].size : Round(r->second.size());
    }
    const uint32_t header_size = Round(32 + 52ull * count);
    uint64_t at = header_size;
    for (uint32_t i = 0; i < count; i++) {
        offsets[i] = uint32_t(at);
        at += sizes[i];
    }
    if (at > 0xFFFFFFFFull) {
        error = "archive would exceed 4 GiB";
        return false;
    }
    std::vector<uint8_t> table;
    Put(table, header_size);
    Put(table, count);
    for (uint32_t s : sizes) Put(table, s);
    for (uint32_t o : offsets) Put(table, o);
    for (const Entry& e : a.entries) {
        uint8_t name[kNameBytes]{};
        std::memcpy(name, e.name.data(), std::min<size_t>(e.name.size(), kNameBytes - 1));
        table.insert(table.end(), name, name + kNameBytes);
    }
    table.resize(header_size - 24, 0);
    Crypt(table.data(), table.size(), a.key);

    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    fs::path temp = out;
    temp += L".partial";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(a.header), 24);
        f.write(reinterpret_cast<const char*>(table.data()), std::streamsize(table.size()));
        std::vector<uint8_t> buffer;
        for (uint32_t i = 0; i < count && f; i++) {
            const Entry& e = a.entries[i];
            auto r = replacements.find(Key(e.name));
            if (r != replacements.end()) {
                buffer = r->second;
                buffer.resize(sizes[i], 0);
                Crypt(buffer.data(), buffer.size(), ~sizes[i]);
            } else {
                buffer.resize(e.size);
                if (e.size && !source(e.offset, buffer.data(), e.size)) {
                    error = "cannot read " + e.name;
                    f.close();
                    fs::remove(temp, ec);
                    return false;
                }
            }
            f.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(buffer.size()));
        }
        if (!f) {
            error = "cannot write " + temp.string();
            f.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, out, ec);
    if (ec) {
        fs::remove(out, ec);
        fs::rename(temp, out, ec);
    }
    if (ec) error = "cannot replace " + out.string();
    return !ec;
}

}  // namespace tenchu::con
