#pragma once
// Memory patches shared as text files, the way players post Cheat Engine patches:
//
//   [Always gets grandmaster rank]
//   Author=me
//   Address,1008E40A0,value,00000063
//
// Each .txt in memory_patches\ next to the game holds one or more patches; lines it does not know are ignored,
// so a whole forum post can be pasted in. F6 > Patches lists them by name and author and turns each on or off.
// While on, a patch's values are written again many times a second (Cheat Engine's freeze); the bytes it found
// are put back when it is turned off. Addresses are the game's own (00000000-7FFFFFFF) or Cheat Engine's host
// addresses (game memory sits at 0x100000000, else 0x200000000 or 0x300000000).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tenchu::patches {

enum class Operation { Value, Mask, Add, Copy, Xor };

struct Write {
    uint32_t va = 0;       // game address
    uint32_t value = 0;    // little endian, as Cheat Engine shows it
    uint8_t size = 4;      // bytes: 1, 2 or 4, from the value's hex digits; floats are 4
    Operation operation = Operation::Value;
    uint32_t argument = 0; // owned bits for Mask, saturation limit for Add
    bool absolute = false; // GlobalAddress/GlobalCheck use a fixed global in a pointer patch
};

struct Patch {
    std::string name, author, notes;
    std::string file;      // UTF-8 file name in the patches folder
    std::vector<Write> writes;
    std::vector<std::string> problems;  // lines that could not be read
    // Optional file/patch metadata: resolve addresses inside this original
    // object range through a fixed game pointer, on every live update.
    uint32_t pointer = 0, original_base = 0, object_size = 0;
    struct Check { Write read; std::string compare; std::vector<uint32_t> values; };
    std::vector<Check> checks;
    bool once = false, restore = true, strict = false;
    bool noclip = false; // Controller=NoClip: frame-driven player flight, no periodic data writes
    std::string Id() const { return file + "|" + name; }
};

// every patch in one file's text; file is only recorded
std::vector<Patch> Parse(const std::string& text, const std::string& file);

// a game memory range holding code: writes there change the Xbox code bytes, which the recompiled game never runs
struct Range {
    uint32_t begin = 0, end = 0;
    std::string name;
};
// the XBE's sections that hold code, read through map from the image header at 0x10000; empty while unreadable
std::vector<Range> CodeRanges(const std::function<const uint8_t*(uint32_t va, uint32_t size)>& map);
const Range* InCode(const Patch& p, const std::vector<Range>& code);

// what a patch is doing, for F6 > Patches
enum class State { Off, Active, Waiting, Outside, Applied, Queued, Skipped };

// the patches folder, its patches and their switches; Apply writes enabled patches through map (game memory,
// null where it is not readable and writable) and restores the bytes of patches turned off
class Set {
public:
    using Map = std::function<uint8_t*(uint32_t va, uint32_t size)>;
    void Load(std::vector<Patch> patches, const Map& map);  // keeps switches by Id, restores patches that went away
    void SetEnabled(const std::string& id, bool on);
    bool Enabled(const std::string& id) const;
    void Apply(const Map& map, bool menu_open = false);
    State StateOf(const std::string& id) const;
    const std::vector<Patch>& Patches() const { return patches_; }
    std::vector<std::string> EnabledIds() const { return enabled_; }
    void SetEnabledIds(std::vector<std::string> ids) { enabled_ = std::move(ids); }

private:
    struct Saved {
        std::string id;
        std::vector<std::vector<uint8_t>> original;  // per write, empty until it was first written
        State state = State::Off;
        uint32_t object = 0; // allocation whose original bytes were captured
    };
    Saved& Slot(const std::string& id);
    void Restore(Saved& s, const Patch& p, const Map& map);
    std::vector<Patch> patches_;
    std::vector<Saved> saved_;
    std::vector<std::string> enabled_;
};

// The game's: the folder, scanning, the writer thread, settings
std::filesystem::path Folder();
void Rescan();
void Start();  // once the game is loaded: scans and starts writing
void SetMenuOpen(bool open); // defer one-shot actions until the F6 menu closes
bool MenuOpen();
struct Status {
    std::string id, name, author, notes, file;
    std::vector<std::string> problems;
    std::string code;  // the code section a write lands in, empty when none
    bool enabled = false;
    State state = State::Off;
    bool once = false;
};
std::vector<Status> List();
void SetEnabled(const std::string& id, bool on);
void Load(const void* toml_table);  // [memory_patches] in the port settings
void Save(void* toml_table);

}  // namespace tenchu::patches
