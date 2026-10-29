#pragma once
// The game's K2 "ConnectFile Version 6" archives (Media\*.con). Layout:
//   24-byte header ("ConnectFile Version 6..."); key = ~(first 4 bytes)
//   then, XORed with the key: data offset, entry count, sizes[count],
//   offsets[count], names[count] (44 bytes each, NUL padded, '\' separated)
//   each entry's bytes are XORed with ~(its stored size)
// Rebuilding an archive with no changes reproduces the original byte for byte.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tenchu::con {

struct Entry {
    std::string name;  // as stored, '\' separated
    uint32_t offset = 0, size = 0;
};

struct Archive {
    uint8_t header[24]{};
    uint32_t key = 0, data_offset = 0;
    std::vector<Entry> entries;
};

// reads len bytes at an absolute offset of the archive; false when short
using Source = std::function<bool(uint64_t offset, void* out, uint32_t len)>;
Source FileSource(const std::filesystem::path& path);

bool Parse(const Source& source, uint64_t total, Archive& archive, std::string& error);
void Crypt(uint8_t* data, size_t size, uint32_t key);
bool ReadEntry(const Source& source, const Entry& entry, std::vector<uint8_t>& out);
// upper case, '/' separated: how mods and loose folders name an entry
std::string Key(const std::string& name);
// a relative path for an entry name, empty when unsafe (absolute, drive, "..")
std::filesystem::path SafePath(const std::string& name);
// writes the archive with the replaced entries (by Key), others copied as they are;
// written beside out and moved into place once whole
bool Write(const Source& source, const Archive& archive, const std::map<std::string, std::vector<uint8_t>>& replacements,
           const std::filesystem::path& out, std::string& error);

}  // namespace tenchu::con
