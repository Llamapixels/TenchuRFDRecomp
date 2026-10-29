#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "ui.h"
#include "images.h"
#include "port/connect_file.h"
#include "graphics/texture_packs.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

// Game files: everything the game unpacked into extracted\ on its first start. Files
// inside an archive live in <name>.con.files beside it; the game rebuilds the archive
// from them when it starts, so editing a file here is enough. The page previews files,
// edits textures in the player's image editor (each save there is converted back),
// replaces, exports and reverts files, turns changes into a mod, packs an archive into
// a game-ready .con and turns someone's modded .con into a mod of just its changes.
namespace wb {
namespace {
namespace con = tenchu::con;
using Clock = std::chrono::steady_clock;

enum Kind { kFolder, kTexture, kImage, kModel, kMotion, kSound, kVideo, kText, kProgram, kArchive, kData, kKinds };
const char* kKindNames[] = {"Folder", "Texture", "Image", "Model", "Motion", "Sound", "Video", "Text", "Program", "Archive (packed)", "Data"};
const char* kKindIcons[] = {ICON_FOLDER, ICON_PICTURE, ICON_PICTURE, ICON_PACKAGE, ICON_PLAY, ICON_AUDIO, ICON_VIDEO, ICON_FILE, ICON_CODE, ICON_LIBRARY, ICON_FILE};
enum State : uint8_t { kOriginal, kChanged, kAdded };

struct Archive {
    fs::path con;     // relative
    fs::path folder;  // relative, <name>.con.files
    con::Archive a;
    std::unordered_map<std::string, uint32_t> by_key;
    fs::file_time_type marker;  // .extracted: files newer than it are edits
};
struct Node {
    std::string name;
    fs::path rel;
    int parent = -1;
    std::vector<int> children;
    Kind kind = kData;
    uint64_t size = 0;
    fs::file_time_type time;
    int archive = -1;  // the archive this file belongs to (or this folder unpacks)
    int entry = -1;    // its entry, -1 when the archive has none
    State state = kOriginal;
    int changed = 0;  // folders: changed files inside
};
struct Index {
    std::vector<Node> nodes;  // 0 is extracted\ itself
    std::vector<Archive> archives;
    fs::file_time_type complete;
};

std::shared_ptr<Index> g_index;  // published by the worker
std::mutex g_index_lock;
std::thread g_worker;
std::atomic<bool> g_indexing{false};
std::atomic<size_t> g_seen{0};

int g_selected = -1;
int g_reveal = -1;  // the tree opens down to this one
char g_search[128];
int g_kind_filter = 0;  // 0 all, else Kind
bool g_changed_only = false;
int g_zoom = 0;         // 0 fit, else 1, 2, 4, 8
int g_channels = 0;     // 0 colour and alpha, 1 colour, 2 alpha
std::string g_text_for;
std::string g_text;
char g_mod_name[96] = "My mod";
bool g_restore_after_mod = true;
Clock::time_point g_last_stat;

// textures being edited in another program: the PNG there, the file it goes back to
struct Edit {
    fs::path png, target;
    fs::file_time_type seen;
};
std::vector<Edit> g_edits;
Clock::time_point g_last_edit_check;

Kind KindOf(const fs::path& p) {
    const std::string e = Lower(U8(p.extension()));
    if (e == ".dds") return kTexture;
    if (e == ".png" || e == ".bmp" || e == ".jpg" || e == ".jpeg" || e == ".tga") return kImage;
    if (e == ".msh") return kModel;
    if (e == ".mlp" || e == ".mls" || e == ".mot") return kMotion;
    if (e == ".wma" || e == ".wav" || e == ".bgm" || e == ".xwb" || e == ".adx" || e == ".snd") return kSound;
    if (e == ".xmv" || e == ".wmv" || e == ".bik" || e == ".sfd") return kVideo;
    if (e == ".txt" || e == ".ini" || e == ".cfg" || e == ".csv" || e == ".xml" || e == ".lst" || e == ".toml") return kText;
    if (e == ".xbe") return kProgram;
    if (e == ".con") return kArchive;
    return kData;
}

fs::path Originals() { return Extracted() / ".originals"; }

// the state of one file against the disc's
State StateOf(const Index& ix, const Node& n) {
    std::error_code ec;
    if (n.archive >= 0 && n.kind != kFolder) {
        const Archive& a = ix.archives[n.archive];
        if (n.entry < 0) return kAdded;
        return n.size != a.a.entries[n.entry].size || n.time > a.marker ? kChanged : kOriginal;
    }
    return fs::exists(Originals() / n.rel, ec) || n.time > ix.complete ? kChanged : kOriginal;
}

void Build(fs::path root) {
    auto ix = std::make_shared<Index>();
    std::error_code ec;
    ix->complete = fs::last_write_time(root / ".complete", ec);
    if (ec) ix->complete = fs::file_time_type::max();
    ix->nodes.push_back({"extracted", {}, -1, {}, kFolder});
    // folders first, by name
    std::function<void(int, int)> walk = [&](int parent, int archive) {
        std::vector<fs::directory_entry> items;
        for (fs::directory_iterator it(root / ix->nodes[parent].rel, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = U8(it->path().filename());
            if (name.empty() || name[0] == '.' || name.size() > 8 && name.compare(name.size() - 8, 8, ".partial") == 0) continue;
            items.push_back(*it);
        }
        std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            std::error_code e;
            if (a.is_directory(e) != b.is_directory(e)) return a.is_directory(e);
            return Lower(U8(a.path().filename())) < Lower(U8(b.path().filename()));
        });
        for (const auto& item : items) {
            Node n;
            n.name = U8(item.path().filename());
            n.rel = ix->nodes[parent].rel / item.path().filename();
            n.parent = parent;
            n.archive = archive;
            const bool dir = item.is_directory(ec);
            n.kind = dir ? kFolder : KindOf(n.rel);
            if (!dir) {
                n.size = item.file_size(ec);
                n.time = item.last_write_time(ec);
            }
            // an archive's unpacked folder
            if (dir && archive < 0 && Lower(n.name).size() > 10 && Lower(n.name).compare(n.name.size() - 10, 10, ".con.files") == 0) {
                Archive a;
                a.folder = n.rel;
                a.con = n.rel.parent_path() / FromU8(n.name.substr(0, n.name.size() - 6));
                std::string error;
                if (con::Parse(con::FileSource(root / a.con), fs::file_size(root / a.con, ec), a.a, error)) {
                    for (uint32_t i = 0; i < a.a.entries.size(); i++) a.by_key[con::Key(a.a.entries[i].name)] = i;
                    a.marker = fs::last_write_time(root / a.folder / ".extracted", ec);
                    if (ec) a.marker = fs::file_time_type::max();
                    n.archive = int(ix->archives.size());
                    ix->archives.push_back(std::move(a));
                }
            }
            if (n.archive >= 0 && !dir) {
                const Archive& a = ix->archives[n.archive];
                auto rel = U8(n.rel.lexically_relative(a.folder));
                auto e = a.by_key.find(con::Key(rel));
                n.entry = e == a.by_key.end() ? -1 : int(e->second);
            }
            const int at = int(ix->nodes.size());
            ix->nodes.push_back(std::move(n));
            ix->nodes[parent].children.push_back(at);
            g_seen++;
            if (dir) walk(at, ix->nodes[at].archive);
        }
    };
    walk(0, -1);
    for (size_t i = ix->nodes.size(); i-- > 1;) {
        Node& n = ix->nodes[i];
        if (n.kind != kFolder) n.state = StateOf(*ix, n);
        if (n.state != kOriginal || n.changed) ix->nodes[n.parent].changed += n.kind == kFolder ? n.changed : 1;
    }
    std::lock_guard l(g_index_lock);
    g_index = ix;
    g_indexing = false;
}

void Rebuild() {
    if (g_indexing) return;
    if (g_worker.joinable()) g_worker.join();
    g_indexing = true;
    g_seen = 0;
    g_worker = std::thread(Build, Extracted());
}

std::shared_ptr<Index> Current() {
    std::lock_guard l(g_index_lock);
    return g_index;
}

fs::path Full(const Node& n) { return Extracted() / n.rel; }

// refreshes one file's size, time and state after the workbench or another program changed it
void Restat(Index& ix, int i) {
    Node& n = ix.nodes[i];
    std::error_code ec;
    const State before = n.state;
    n.size = fs::file_size(Full(n), ec);
    n.time = fs::last_write_time(Full(n), ec);
    n.state = StateOf(ix, n);
    if ((before == kOriginal) != (n.state == kOriginal))
        for (int p = n.parent; p >= 0; p = ix.nodes[p].parent) ix.nodes[p].changed += n.state == kOriginal ? -1 : 1;
}

// the disc's version of a file: from its archive, or the copy kept before the first change
bool Original(const Index& ix, const Node& n, std::vector<uint8_t>& out) {
    if (n.archive >= 0 && n.entry >= 0) {
        const Archive& a = ix.archives[n.archive];
        return con::ReadEntry(con::FileSource(Extracted() / a.con), a.a.entries[n.entry], out);
    }
    return ReadFile(Originals() / n.rel, out);
}

// keeps the disc's copy of a loose file before the workbench first changes it
void KeepOriginal(const Node& n) {
    if (n.archive >= 0) return;  // the archive has it
    std::error_code ec;
    const fs::path keep = Originals() / n.rel;
    if (fs::exists(keep, ec)) return;
    fs::create_directories(keep.parent_path(), ec);
    fs::copy_file(Full(n), keep, ec);
}

bool WriteGameFile(Index& ix, int i, const std::vector<uint8_t>& data) {
    KeepOriginal(ix.nodes[i]);
    if (!WriteFile(Full(ix.nodes[i]), data)) {
        Notify("Writing " + ix.nodes[i].name + " failed.", true);
        return false;
    }
    Restat(ix, i);
    return true;
}

bool Revert(Index& ix, int i) {
    Node& n = ix.nodes[i];
    std::error_code ec;
    if (n.state == kAdded) {  // not part of the game: to the recycle bin
        std::wstring from = Full(n).wstring() + L'\0';
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        if (SHFileOperationW(&op)) return false;
        n.state = kOriginal;
        n.size = 0;
        for (int p = n.parent; p >= 0; p = ix.nodes[p].parent) ix.nodes[p].changed--;
        return true;
    }
    std::vector<uint8_t> data;
    if (!Original(ix, n, data) || !WriteFile(Full(n), data)) return false;
    if (n.archive >= 0) fs::last_write_time(Full(n), ix.archives[n.archive].marker - std::chrono::seconds(1), ec);
    else {
        fs::last_write_time(Full(n), ix.complete - std::chrono::seconds(1), ec);
        fs::remove(Originals() / n.rel, ec);
    }
    Restat(ix, i);
    return true;
}

// a file the player picked, made into what node i should hold: images become the texture's format
bool Convert(const Index& ix, int i, const fs::path& file, bool ask, std::vector<uint8_t>& out) {
    const Node& n = ix.nodes[i];
    std::vector<uint8_t> bytes, current;
    if (!ReadFile(file, bytes)) {
        Tell("Cannot read " + U8(file.filename()) + ".");
        return false;
    }
    Dds od;
    if (n.kind != kTexture || !ReadFile(Full(n), current) || !ParseDds(current, od)) {
        out = std::move(bytes);
        return true;
    }
    Dds nd;
    if (ParseDds(bytes, nd)) {
        if ((nd.format != od.format || nd.w != od.w || nd.h != od.h) && ask &&
            !Ask(U8(file.filename()) + " is " + FormatName(nd.format) + " " + std::to_string(nd.w) + "x" + std::to_string(nd.h) + ", the game's is " +
                 FormatName(od.format) + " " + std::to_string(od.w) + "x" + std::to_string(od.h) + ". Use it anyway?"))
            return false;
        out = std::move(bytes);
        return true;
    }
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    if (!tenchu::packs::ReadImage(file, px, w, h)) {
        Tell(U8(file.filename()) + " is not an image RFD Switchblade can read.");
        return false;
    }
    if (!CanEncode(od.format)) {
        Tell(std::string(FormatName(od.format)) + " textures cannot be written; use a DDS file.");
        return false;
    }
    if ((w != od.w || h != od.h) && ask &&
        Ask(U8(file.filename()) + " is " + std::to_string(w) + "x" + std::to_string(h) + ", the game's texture is " + std::to_string(od.w) + "x" +
            std::to_string(od.h) + ".\n\nResize it to the game's size? Choose No to keep its size; bigger textures use more of the game's memory.")) {
        px = Resize(px.data(), w, h, od.w, od.h);
        w = od.w;
        h = od.h;
    }
    out = EncodeDds(current, od, px.data(), w, h);
    return !out.empty();
}

void Replace(Index& ix, int i, const fs::path& file, bool ask) {
    std::vector<uint8_t> data;
    if (Convert(ix, i, file, ask, data) && WriteGameFile(ix, i, data)) Notify("Replaced " + ix.nodes[i].name + " with " + U8(file.filename()) + ".");
}

// ----- editing textures elsewhere: export a PNG, convert each save back -----

fs::path EditsDir() { return GameDir() / "workbench edits"; }

void StartEdit(Index& ix, int i) {
    const Node& n = ix.nodes[i];
    std::vector<uint8_t> file, px;
    Dds dds;
    if (!ReadFile(Full(n), file) || !ParseDds(file, dds) || !DecodeDds(file, dds, px)) {
        Tell("This texture cannot be read.");
        return;
    }
    fs::path png = EditsDir() / n.rel;
    png.replace_extension(L".png");
    std::error_code ec;
    fs::create_directories(png.parent_path(), ec);
    if (!WritePng(png, px.data(), dds.w, dds.h)) {
        Tell("Writing " + U8(png) + " failed.");
        return;
    }
    g_edits.erase(std::remove_if(g_edits.begin(), g_edits.end(), [&](const Edit& e) { return e.png == png; }), g_edits.end());
    g_edits.push_back({png, Full(n), fs::last_write_time(png, ec)});
    OpenWith(png, false);
    Notify("Opened " + n.name + " as a PNG. Each time you save it there, the game's texture is updated.");
}

void CheckEdits() {
    if (g_edits.empty() || Clock::now() - g_last_edit_check < std::chrono::milliseconds(600)) return;
    g_last_edit_check = Clock::now();
    auto ix = Current();
    if (!ix) return;
    for (Edit& e : g_edits) {
        std::error_code ec;
        const auto t = fs::last_write_time(e.png, ec);
        if (ec || t == e.seen) continue;
        // wait until the editor has finished writing
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        if (!tenchu::packs::ReadImage(e.png, px, w, h)) continue;
        e.seen = t;
        for (size_t i = 0; i < ix->nodes.size(); i++) {
            if (Full(ix->nodes[i]) != e.target) continue;
            std::vector<uint8_t> current;
            Dds od;
            if (!ReadFile(e.target, current) || !ParseDds(current, od)) break;
            auto out = EncodeDds(current, od, px.data(), w, h);
            if (!out.empty() && WriteGameFile(*ix, int(i), out))
                Notify("Applied your edit to " + ix->nodes[i].name + (w != od.w || h != od.h ? " at its new size " + std::to_string(w) + "x" + std::to_string(h) + "." : "."));
            break;
        }
    }
}

// ----- mods and archives -----

void MakeMod(Index& ix, const std::string& name, bool restore) {
    const fs::path mod = GameDir() / "mods" / FromU8(name);
    size_t written = 0, restored = 0;
    for (size_t i = 1; i < ix.nodes.size(); i++) {
        const Node& n = ix.nodes[i];
        if (n.kind == kFolder || n.state != kChanged || n.kind == kArchive) continue;
        std::vector<uint8_t> data;
        if (!ReadFile(Full(n), data) || !WriteFile(mod / n.rel, data)) {
            Tell("Copying " + U8(n.rel) + " into the mod failed.");
            return;
        }
        written++;
    }
    if (!written) {
        Notify("There are no changed files to put in a mod.", true);
        return;
    }
    WriteModToml(mod, name, "Made with RFD Switchblade.");
    if (restore)
        for (size_t i = 1; i < ix.nodes.size(); i++)
            if (ix.nodes[i].kind != kFolder && ix.nodes[i].state == kChanged && ix.nodes[i].kind != kArchive && Revert(ix, int(i))) restored++;
    Notify("Made mods\\" + name + " from " + std::to_string(written) + " changed files" + (restore ? ", originals restored" : "") +
           ". Turn it on in F6 > Mods.");
}

void Repack(Index& ix, int archive) {
    const Archive& a = ix.archives[archive];
    const fs::path out = SaveFile(L"Game archive (*.con)\0*.con\0", L"con", GameDir() / a.con.filename());
    if (out.empty()) return;
    std::error_code ec;
    if (fs::equivalent(out, Extracted() / a.con, ec)) {
        Tell("Choose another place: the game keeps the original archive there and rebuilds it from the folder by itself.");
        return;
    }
    std::map<std::string, std::vector<uint8_t>> replacements;
    for (const Node& n : ix.nodes)
        if (n.archive == archive && n.kind != kFolder && n.entry >= 0 && n.state == kChanged) {
            auto& data = replacements[con::Key(a.a.entries[n.entry].name)];
            ReadFile(Full(n), data);
        }
    std::string error;
    if (!con::Write(con::FileSource(Extracted() / a.con), a.a, replacements, out, error)) Tell("Packing failed: " + error);
    else Notify("Packed " + U8(out.filename()) + " with " + std::to_string(replacements.size()) + " changed files: ready for the game or an Xbox.");
}

// someone's whole modded .con: only the files that differ from the game's go into a mod
void ImportModdedArchive(Index& ix, const fs::path& file, const std::string& name) {
    con::Archive modded;
    std::string error;
    std::error_code ec;
    auto source = con::FileSource(file);
    if (!con::Parse(source, fs::file_size(file, ec), modded, error)) {
        Tell(U8(file.filename()) + " is not a game archive (" + error + ").");
        return;
    }
    // the game archive with the most entries in common
    int best = -1;
    size_t best_shared = 0;
    for (size_t i = 0; i < ix.archives.size(); i++) {
        size_t shared = 0;
        for (const auto& e : modded.entries) shared += ix.archives[i].by_key.count(con::Key(e.name));
        if (shared > best_shared || (shared == best_shared && best >= 0 && _wcsicmp(ix.archives[i].con.filename().c_str(), file.filename().c_str()) == 0)) {
            best = int(i);
            best_shared = shared;
        }
    }
    if (best < 0 || !best_shared) {
        Tell(U8(file.filename()) + " shares no files with the game's archives.");
        return;
    }
    const Archive& a = ix.archives[best];
    const fs::path mod = GameDir() / "mods" / FromU8(name);
    size_t changed = 0, extra = 0;
    std::vector<uint8_t> mine, theirs;
    for (const auto& e : modded.entries) {
        auto it = a.by_key.find(con::Key(e.name));
        if (it == a.by_key.end()) {
            extra++;
            continue;
        }
        if (!con::ReadEntry(source, e, theirs) || !con::ReadEntry(con::FileSource(Extracted() / a.con), a.a.entries[it->second], mine)) continue;
        if (theirs == mine) continue;
        if (!WriteFile(mod / a.folder / con::SafePath(e.name), theirs)) {
            Tell("Writing into the mod failed.");
            return;
        }
        changed++;
    }
    if (!changed) {
        Notify(U8(file.filename()) + " has no changes from the game's " + U8(a.con.filename()) + ".", true);
        return;
    }
    WriteModToml(mod, name, "Converted from a modded " + U8(a.con.filename()) + " by RFD Switchblade.");
    Notify("Made mods\\" + name + ": " + std::to_string(changed) + " changed files of " + U8(a.con) +
           (extra ? " (" + std::to_string(extra) + " files the game does not have were left out)" : "") + ".");
}

// ----- drawing -----

bool Visible(const Index& ix, int i, const std::string& search) {
    const Node& n = ix.nodes[i];
    if (n.kind == kFolder) return false;
    if (g_changed_only && n.state == kOriginal) return false;
    if (g_kind_filter && int(n.kind) != g_kind_filter && !(g_kind_filter == kImage && n.kind == kTexture)) return false;
    return search.empty() || Lower(U8(n.rel)).find(search) != std::string::npos;
}

void Badge(const char* text, ImVec4 colour) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

const ImVec4 kChangedColour(.95f, .80f, .40f, 1), kAddedColour(.55f, .80f, .95f, 1);

void Label(const Index& ix, int i, bool full_path) {
    const Node& n = ix.nodes[i];
    const std::string text = std::string(kKindIcons[n.kind]) + "  " + (full_path ? U8(n.rel) : n.name);
    if (n.state == kChanged) ImGui::TextColored(kChangedColour, "%s", text.c_str());
    else if (n.state == kAdded) ImGui::TextColored(kAddedColour, "%s", text.c_str());
    else ImGui::TextUnformatted(text.c_str());
}

void Tree(Index& ix, int i) {
    for (int c : ix.nodes[i].children) {
        Node& n = ix.nodes[c];
        ImGui::PushID(c);
        if (n.kind == kFolder) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (g_selected == c) flags |= ImGuiTreeNodeFlags_Selected;
            for (int p = g_reveal >= 0 ? ix.nodes[g_reveal].parent : -1; p > 0; p = ix.nodes[p].parent)
                if (p == c) ImGui::SetNextItemOpen(true);
            const bool archive_folder = n.archive >= 0 && ix.nodes[n.parent].archive != n.archive;
            const std::string label = std::string(archive_folder ? ICON_LIBRARY : ICON_FOLDER) + "  " + n.name + (n.changed ? "  (" + std::to_string(n.changed) + " changed)" : "");
            ImGui::PushStyleColor(ImGuiCol_Text, n.changed ? kChangedColour : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) g_selected = c;
            if (open) {
                Tree(ix, c);
                ImGui::TreePop();
            }
        } else {
            const bool selected = g_selected == c;
            if (ImGui::Selectable("##f", selected, ImGuiSelectableFlags_AllowOverlap)) g_selected = c;
            if (g_reveal == c) {
                ImGui::SetScrollHereY(.3f);
                g_reveal = -1;
            }
            ImGui::SameLine(0, 0);
            Label(ix, c, false);
        }
        ImGui::PopID();
    }
}

void Preview(Index& ix, const Node& n, ImVec2 room) {
    const float em = ImGui::GetFontSize();
    std::error_code ec;
    if (n.kind == kTexture || n.kind == kImage) {
        const fs::path path = Full(n);
        const int channels = g_channels;
        const bool dds = n.kind == kTexture;
        auto load = [path, channels, dds](Bitmap& b) {
            if (dds) {
                std::vector<uint8_t> file;
                Dds d;
                if (!ReadFile(path, file) || !ParseDds(file, d) || !DecodeDds(file, d, b.px)) return false;
                b.w = d.w;
                b.h = d.h;
            } else if (!tenchu::packs::ReadImage(path, b.px, b.w, b.h)) {
                return false;
            }
            for (size_t i = 0; i < b.px.size(); i += 4) {
                if (channels == 1) b.px[i + 3] = 255;
                if (channels == 2) b.px[i] = b.px[i + 1] = b.px[i + 2] = b.px[i + 3], b.px[i + 3] = 255;
            }
            return true;
        };
        const uint32_t side = g_zoom ? 8192 : 2048;
        auto pic = GetPicture(PictureKey("file|" + U8(path) + "|" + std::to_string(n.time.time_since_epoch().count()) + "|" + std::to_string(channels), side), side, load);
        if (!g_zoom || !pic || !pic->id) {
            PictureBox(pic, room);
            return;
        }
        ImGui::BeginChild("zoomed", room, 0, ImGuiWindowFlags_HorizontalScrollbar);
        ImageFit(*pic, ImVec2(float(pic->w * g_zoom), float(pic->h * g_zoom)));
        ImGui::EndChild();
        return;
    }
    if (n.kind == kText) {
        if (g_text_for != U8(n.rel) + std::to_string(n.time.time_since_epoch().count())) {
            std::vector<uint8_t> data;
            ReadFile(Full(n), data);
            data.resize(std::min<size_t>(data.size(), 256 * 1024));
            g_text.assign(data.begin(), data.end());
            g_text_for = U8(n.rel) + std::to_string(n.time.time_since_epoch().count());
        }
        ImGui::InputTextMultiline("##text", g_text.data(), g_text.size() + 1, room, ImGuiInputTextFlags_ReadOnly);
        return;
    }
    // anything else: the first bytes
    if (g_text_for != U8(n.rel) + std::to_string(n.time.time_since_epoch().count())) {
        std::vector<uint8_t> data;
        ReadFile(Full(n), data);
        g_text.clear();
        char line[128];
        for (size_t at = 0; at < std::min<size_t>(data.size(), 4096); at += 16) {
            int len = std::snprintf(line, sizeof(line), "%06zx  ", at);
            for (size_t k = 0; k < 16; k++) len += at + k < data.size() ? std::snprintf(line + len, sizeof(line) - len, "%02x ", data[at + k]) : std::snprintf(line + len, sizeof(line) - len, "   ");
            len += std::snprintf(line + len, sizeof(line) - len, " ");
            for (size_t k = 0; k < 16 && at + k < data.size(); k++) line[len++] = data[at + k] >= 32 && data[at + k] < 127 ? char(data[at + k]) : '.';
            line[len] = 0;
            g_text += line;
            g_text += '\n';
        }
        g_text_for = U8(n.rel) + std::to_string(n.time.time_since_epoch().count());
    }
    Hint("No preview for this kind of file. Open it with a program that knows it, or replace it with an edited copy. The first bytes:");
    ImGui::InputTextMultiline("##hex", g_text.data(), g_text.size() + 1, ImVec2(room.x, room.y - em * 2), ImGuiInputTextFlags_ReadOnly);
}

void Details(Index& ix) {
    const float em = ImGui::GetFontSize();
    if (g_selected <= 0 || g_selected >= int(ix.nodes.size())) {
        ImGui::Dummy(ImVec2(0, em * 2));
        Heading("Pick a file");
        Hint("Every file of the disc is here, and every archive is unpacked into its .con.files folder. Change any file and the game "
             "uses it from its next start; changed files are yellow. Turn your changes into a mod to share them.");
        return;
    }
    Node& n = ix.nodes[g_selected];
    // re-read the file now and then: another program may be editing it
    if (n.kind != kFolder && Clock::now() - g_last_stat > std::chrono::seconds(1)) {
        g_last_stat = Clock::now();
        Restat(ix, g_selected);
    }
    ImGui::PushFont(HeadingFont());
    ImGui::Text("%s  %s", kKindIcons[n.kind], n.name.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", U8(n.rel.parent_path()).c_str());
    const bool archive_folder = n.kind == kFolder && n.archive >= 0 && (n.parent < 0 || ix.nodes[n.parent].archive != n.archive);
    if (n.kind == kFolder) {
        ImGui::Text("%zu items", n.children.size());
        if (n.changed) Badge(("  " + std::to_string(n.changed) + " changed").c_str(), kChangedColour);
        if (archive_folder) {
            Hint("The files of an archive. The game packs them into the archive when it starts, so changing a file here is enough.");
            if (IconButton(ICON_PACKAGE, "Pack into a .con...", "A game-ready archive with your changes, for sharing or a real Xbox.")) Repack(ix, n.archive);
            ImGui::SameLine();
        }
        if (IconButton(ICON_FOLDER, "Show in Explorer")) ShowInExplorer(Full(n));
        return;
    }
    ImGui::TextDisabled("%s  ·  %s", n.kind == kArchive ? "Archive (packed)" : kKindNames[n.kind], SizeText(n.size).c_str());
    if (n.state == kChanged) Badge("  ·  changed", kChangedColour);
    if (n.state == kAdded) Badge("  ·  new: not in the game's archive, so the game ignores it", kAddedColour);
    if (n.kind == kTexture) {
        std::vector<uint8_t> file;
        Dds dds;
        if (ReadFile(Full(n), file) && ParseDds(file, dds)) {
            ImGui::SameLine();
            const uint64_t key = GameKey(file, dds);
            ImGui::TextDisabled("  ·  %s %u x %u", FormatName(dds.format), dds.w, dds.h);
            if (key) {
                ImGui::SameLine();
                ImGui::TextDisabled("  ·  key %016llx", static_cast<unsigned long long>(key));
            }
        }
    }

    // actions
    if (n.kind == kArchive) {
        Hint("The game's original archive, copied from the disc. Change files in its .con.files folder instead; the game builds the archive from them.");
    } else {
        if (n.kind == kTexture) {
            if (IconButton(ICON_EDIT, "Edit", "Opens it as a PNG in your image editor; each time you save there, the texture is updated.")) StartEdit(ix, g_selected);
        } else if (IconButton(ICON_EDIT, "Open", "Opens it in the program Windows uses for it.")) {
            KeepOriginal(n);
            OpenWith(Full(n), false);
        }
        ImGui::SameLine();
        if (IconButton(ICON_OPEN_WITH, "Open with...")) {
            KeepOriginal(n);
            OpenWith(Full(n), true);
        }
        ImGui::SameLine();
        if (IconButton(ICON_IMPORT, "Replace...", "Or drop a file on the window.")) {
            fs::path file = OpenFile(n.kind == kTexture ? L"Textures (*.png;*.dds;*.jpg;*.bmp;*.tga)\0*.png;*.dds;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0" : L"All files\0*.*\0");
            if (!file.empty()) Replace(ix, g_selected, file, true);
        }
        ImGui::SameLine();
        if (IconButton(ICON_EXPORT, "Export...")) {
            fs::path out = SaveFile(L"All files\0*.*\0", nullptr, n.rel.filename());
            std::vector<uint8_t> data;
            if (!out.empty()) Notify(ReadFile(Full(n), data) && WriteFile(out, data) ? "Exported " + U8(out) + "." : "Exporting failed.");
        }
        if (n.kind == kTexture) {
            ImGui::SameLine();
            if (IconButton(ICON_EXPORT, "PNG...")) {
                fs::path name = n.rel.filename();
                fs::path out = SaveFile(L"PNG image (*.png)\0*.png\0", L"png", name.replace_extension(L".png"));
                std::vector<uint8_t> file, px;
                Dds d;
                if (!out.empty() && ReadFile(Full(n), file) && ParseDds(file, d) && DecodeDds(file, d, px))
                    Notify(WritePng(out, px.data(), d.w, d.h) ? "Exported " + U8(out) + "." : "Exporting failed.");
            }
        }
        ImGui::SameLine();
        const bool can_revert = n.state == kAdded || (n.state == kChanged && (n.entry >= 0 || std::filesystem::exists(Originals() / n.rel)));
        ImGui::BeginDisabled(!can_revert);
        if (IconButton(ICON_UNDO, n.state == kAdded ? "Remove" : "Revert", n.state == kAdded ? "Moves it to the recycle bin." : "Puts the disc's version back.")) {
            if (Ask(n.state == kAdded ? "Move " + n.name + " to the recycle bin?" : "Put the disc's version of " + n.name + " back? Your changes to it are lost.")) {
                if (Revert(ix, g_selected)) Notify("Reverted " + n.name + ".");
                else Notify("Reverting " + n.name + " failed.", true);
            }
        }
        ImGui::EndDisabled();
        if (n.state == kChanged && !can_revert && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Changed outside RFD Switchblade; delete the extracted folder to unpack the disc again.");
    }
    ImGui::SameLine();
    if (IconButton(ICON_FOLDER, "Show")) ShowInExplorer(Full(n));
    if (!Dropped().empty() && n.kind != kArchive && Ask("Replace " + n.name + " with " + U8(Dropped().front().filename()) + "?"))
        Replace(ix, g_selected, Dropped().front(), true);

    // view options for pictures
    if (n.kind == kTexture || n.kind == kImage) {
        ImGui::Spacing();
        const int zooms[] = {0, 1, 2, 4, 8};
        const char* names[] = {"Fit", "100%", "200%", "400%", "800%"};
        int zoom = int(std::find(std::begin(zooms), std::end(zooms), g_zoom) - std::begin(zooms));
        if (Segmented("zoom", &zoom, names, 5)) g_zoom = zooms[zoom];
        ImGui::SameLine(0, em * 2);
        const char* channel_names[] = {"Colour + alpha", "Colour", "Alpha"};
        Segmented("channels", &g_channels, channel_names, 3);
    }
    ImGui::Spacing();
    Preview(ix, n, ImGui::GetContentRegionAvail());
}
}  // namespace

bool FilesBusy(Activity& a) {
    if (!g_indexing) return false;
    a.what = "Reading the game files (" + std::to_string(g_seen.load()) + ")";
    return true;
}

void FilesPage() {
    const float em = ImGui::GetFontSize();
    static fs::path built_for;
    if (built_for != GameDir()) {
        built_for = GameDir();
        g_selected = -1;
        Rebuild();
    }
    if (!g_indexing && g_worker.joinable()) g_worker.join();
    CheckEdits();
    auto ix = Current();
    if (ix && Automate().select >= 0 && g_selected < 0)
        for (size_t i = 0, n = 0; i < ix->nodes.size(); i++)
            if (ix->nodes[i].kind == kTexture && int(n++) == Automate().select) {
                g_selected = g_reveal = int(i);
                break;
            }

    Heading("Game files");
    ImGui::SameLine(0, em * 2);
    size_t changed = 0;
    if (ix) changed = ix->nodes[0].changed;
    ImGui::AlignTextToFramePadding();
    if (ix) ImGui::TextDisabled("%zu files  ·  %zu changed", ix->nodes.size() - 1, changed);
    ImGui::SameLine(0, em * 2);
    if (IconButton(ICON_REFRESH, "Refresh")) Rebuild();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ix || !changed);
    if (IconButton(ICON_PACKAGE, "Make mod from changes...", "Copies every changed file into a mod folder, to share or to switch on and off in F6 > Mods.")) ImGui::OpenPopup("Make mod");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton(ICON_IMPORT, "Import a modded .con...", "Someone shared a whole archive? This keeps only what they changed, as a mod that combines with others.")) {
        fs::path file = OpenFile(L"Game archive (*.con)\0*.con\0");
        if (!file.empty() && ix) {
            const std::string name = U8(file.stem()) + " (imported)";
            ImportModdedArchive(*ix, file, name);
        }
    }
    ImGui::SameLine();
    if (IconButton(ICON_FOLDER_OPEN, "Unpack a .con...", "Any archive, into a folder of your choice.")) {
        fs::path file = OpenFile(L"Game archive (*.con)\0*.con\0");
        con::Archive a;
        std::string error;
        std::error_code ec;
        if (!file.empty()) {
            auto source = con::FileSource(file);
            if (!con::Parse(source, fs::file_size(file, ec), a, error)) Tell(U8(file.filename()) + " is not a game archive (" + error + ").");
            else if (fs::path dir = PickFolder(L"Unpack into"); !dir.empty()) {
                size_t n = 0;
                std::vector<uint8_t> data;
                for (const auto& e : a.entries)
                    if (con::ReadEntry(source, e, data) && WriteFile(dir / con::SafePath(e.name), data)) n++;
                Notify("Unpacked " + std::to_string(n) + " files into " + U8(dir) + ".");
            }
        }
    }
    ImGui::Separator();

    if (!ix) {
        ImGui::Dummy(ImVec2(0, em * 2));
        Hint("Reading the game files...");
        return;
    }
    // dropped .con files become mods; other drops replace the selected file (in Details)
    for (const fs::path& d : Dropped())
        if (_wcsicmp(d.extension().c_str(), L".con") == 0 && Ask("Make a mod of the changes in " + U8(d.filename()) + "?")) ImportModdedArchive(*ix, d, U8(d.stem()) + " (imported)");

    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("tree", ImVec2(std::max(em * 20, ImGui::GetContentRegionAvail().x * .3f), height), ImGuiChildFlags_Border);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", ICON_SEARCH "  Find a file", g_search, sizeof(g_search));
    const char* kinds = "All kinds\0Textures and images\0Models\0Motion\0Sound\0Video\0Text\0";
    const int kind_values[] = {0, kImage, kModel, kMotion, kSound, kVideo, kText};
    int kind_pick = int(std::find(std::begin(kind_values), std::end(kind_values), g_kind_filter) - std::begin(kind_values));
    ImGui::SetNextItemWidth(em * 10);
    if (ImGui::Combo("##kind", &kind_pick, kinds)) g_kind_filter = kind_values[kind_pick];
    ImGui::SameLine();
    ImGui::Checkbox("Changed only", &g_changed_only);
    ImGui::Separator();
    ImGui::BeginChild("items");
    const std::string search = Lower(g_search);
    if (search.empty() && !g_kind_filter && !g_changed_only) {
        Tree(*ix, 0);
    } else {
        std::vector<int> list;
        for (size_t i = 1; i < ix->nodes.size(); i++)
            if (Visible(*ix, int(i), search)) list.push_back(int(i));
        if (list.empty()) Hint(g_changed_only ? "No changed files." : "Nothing matches.");
        ImGuiListClipper clipper;
        clipper.Begin(int(list.size()));
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
                ImGui::PushID(list[r]);
                if (ImGui::Selectable("##f", g_selected == list[r], ImGuiSelectableFlags_AllowOverlap)) g_selected = g_reveal = list[r];
                ImGui::SameLine(0, 0);
                Label(*ix, list[r], true);
                ImGui::PopID();
            }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    Details(*ix);
    ImGui::EndChild();

    if (ImGui::BeginPopupModal("Make mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Copies the %zu changed files into a folder in mods.", changed);
        ImGui::SetNextItemWidth(em * 20);
        ImGui::InputText("Mod name", g_mod_name, sizeof(g_mod_name));
        ImGui::Checkbox("Then put the originals back in extracted", &g_restore_after_mod);
        Hint("With this on, the mod carries your changes and can be switched off in F6 > Mods.");
        const bool ok = ValidModName(g_mod_name);
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Make mod", ImVec2(em * 7, 0))) {
            MakeMod(*ix, g_mod_name, g_restore_after_mod);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace wb
