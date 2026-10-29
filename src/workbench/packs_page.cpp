#include "ui.h"
#include "images.h"
#include "pack_model.h"
#include "port/connect_file.h"
#include <imgui.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Texture packs: a PS2 Wrath of Heaven pack against every texture of the game. Sure
// matches are taken as the game takes them. Review shows the close calls a page at a
// time, most likely first: click the wrong ones, accept the page. Browse shows one
// texture beside every pack image, closest first, to pick or import anything. Save
// writes rfd_matches.toml into the pack folder, which the game reads.
//
// Measured on WoH Last Round: 1,260 of 3,725 game textures are sure; near misses are
// right about 70% of the time by eye (85-90% for mutual pairs), so they are reviewed,
// not taken; about 2,000 game textures have no counterpart in that pack.
namespace wb {
namespace {
namespace con = tenchu::con;
namespace packs = tenchu::packs;
using pm::Choice;

constexpr uint32_t kThumbSide = 320, kBigSide = 1024;
const char* kStatusNames[] = {"Matched", "Check", "Chosen", "Skipped", "No match"};
const ImVec4 kStatusColours[] = {{.55f, .85f, .40f, 1}, {.95f, .80f, .40f, 1}, {.55f, .80f, .95f, 1}, {.60f, .60f, .60f, 1}, {.45f, .50f, .45f, 1}};

enum Stage { kIdle, kReading, kIndexing, kMatching, kReady, kBuilding };
std::atomic<int> g_stage{kIdle};
std::atomic<size_t> g_done{0}, g_total{0}, g_errors{0};
std::atomic<bool> g_cancel{false};
std::thread g_worker;
std::string g_worker_result;  // written by the worker before it changes g_stage

pm::Model g_m;  // owned by the worker until g_stage is kReady
bool g_dirty = false;
int g_mode = 0;  // 0 review, 1 browse

struct Change {
    int tex;
    Choice choice;
    int pick;
};
std::vector<std::vector<Change>> g_undo;

// review
std::unordered_set<int> g_deferred, g_rejected;
std::unordered_map<int, int> g_alternative;  // texture -> candidate shown on its card
bool g_weak = false;
int g_page = 1;  // 12, 24 or 48 cards
constexpr int kPageSizes[] = {12, 24, 48};
float g_card = 0;

// browse
int g_selected = -1;
int g_preview = -2;  // pack image in the big view; -2 follows the chosen one
bool g_scroll_list = false;
char g_search[128], g_image_search[128];
bool g_show[pm::kStatuses] = {true, true, true, true, false};
int g_sort = 0;
int g_all_sel = -1;           // texture the ranked list below belongs to
std::vector<pm::Ranked> g_all;
std::string g_folder;         // image folder filter, "" all
bool g_hide_used = false;
float g_thumb = 0;
char g_mod_name[96] = "PS2 textures";

void Join() {
    if (g_worker.joinable()) g_worker.join();
}
void Dirty() {
    g_dirty = true;
    SetUnsaved("Texture packs", true);
}

// archives parsed once; reads open their own file, so any thread can read
std::mutex g_archives_lock;
std::map<fs::path, std::shared_ptr<const std::pair<con::Archive, std::unordered_map<std::string, uint32_t>>>> g_archives;
bool ReadTexture(const pm::Place& place, std::vector<uint8_t>& file, Dds& dds) {
    const fs::path path = Extracted() / place.archive;
    std::shared_ptr<const std::pair<con::Archive, std::unordered_map<std::string, uint32_t>>> opened;
    {
        std::lock_guard l(g_archives_lock);
        auto& slot = g_archives[path];
        if (!slot) {
            auto o = std::make_shared<std::pair<con::Archive, std::unordered_map<std::string, uint32_t>>>();
            std::error_code ec;
            std::string error;
            if (!con::Parse(con::FileSource(path), fs::file_size(path, ec), o->first, error)) return false;
            for (uint32_t i = 0; i < o->first.entries.size(); i++) o->second[o->first.entries[i].name] = i;
            slot = o;
        }
        opened = slot;
    }
    auto it = opened->second.find(place.entry);
    return it != opened->second.end() && con::ReadEntry(con::FileSource(path), opened->first.entries[it->second], file) && ParseDds(file, dds);
}

// pictures: loaders capture what they need by value, the model may change meanwhile
std::shared_ptr<Picture> GamePicture(int tex, uint32_t side, bool prefetch = false) {
    if (tex < 0 || tex >= int(g_m.tex.size())) return nullptr;
    const pm::Place place = g_m.tex[tex].places[0];
    const uint64_t key = PictureKey("game|" + U8(place.archive) + "|" + place.entry, side);
    auto load = [place](Bitmap& b) {
        std::vector<uint8_t> file;
        Dds dds;
        if (!ReadTexture(place, file, dds) || !DecodeDds(file, dds, b.px)) return false;
        b.w = dds.w;
        b.h = dds.h;
        return true;
    };
    if (prefetch) {
        PrefetchPicture(key, side, load);
        return nullptr;
    }
    return GetPicture(key, side, load);
}
std::shared_ptr<Picture> PackPicture(int image, uint32_t side, bool prefetch = false) {
    if (image < 0 || image >= int(g_m.index.size())) return nullptr;
    const packs::PackImage& img = g_m.index[image];
    const fs::path path = g_m.dir / FromU8(img.file);
    const bool ps2 = img.d.ps2_alpha;
    const uint64_t key = PictureKey("pack|" + U8(path) + "|" + std::to_string(img.time), side);
    auto load = [path, ps2](Bitmap& b) {
        if (!packs::ReadImage(path, b.px, b.w, b.h)) return false;
        if (ps2) packs::ScaleAlpha(b.px.data(), b.px.size() / 4);
        return true;
    };
    if (prefetch) {
        PrefetchPicture(key, side, load);
        return nullptr;
    }
    return GetPicture(key, side, load);
}

void Set(std::vector<Change>& undo, int tex, Choice choice, int pick = -1) {
    pm::Tex& t = g_m.tex[tex];
    undo.push_back({tex, t.choice, t.pick});
    if (choice == Choice::Pick && pick == pm::AutoImage(t)) choice = Choice::Auto;
    t.choice = choice;
    t.pick = choice == Choice::Pick ? pick : -1;
}
void Commit(std::vector<Change> undo) {
    if (undo.empty()) return;
    g_undo.push_back(std::move(undo));
    if (g_undo.size() > 200) g_undo.erase(g_undo.begin());
    Dirty();
}
void Undo() {
    if (g_undo.empty()) return;
    auto& last = g_undo.back();
    for (auto it = last.rbegin(); it != last.rend(); ++it) {
        g_m.tex[it->tex].choice = it->choice;
        g_m.tex[it->tex].pick = it->pick;
        g_deferred.erase(it->tex);
    }
    Notify("Undid " + std::to_string(last.size()) + (last.size() == 1 ? " choice." : " choices."));
    g_undo.pop_back();
    Dirty();
}

template <class F>
void Parallel(size_t count, F f) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 2u, 16u);
    for (unsigned t = 0; t < threads; t++)
        pool.emplace_back([&, t] {
            for (size_t i; !g_cancel && (i = next++) < count;) {
                f(i, t);
                g_done++;
            }
        });
    for (auto& t : pool) t.join();
}

// choices an earlier workbench saved to textures\pack_choices.toml, for a pack without its own file
void ApplyOldChoices(pm::Model& m, const std::string& name) {
    const fs::path file = GameDir() / "textures" / "pack_choices.toml";
    std::error_code ec;
    if (!fs::exists(file, ec)) return;
    packs::PackMatches old;
    try {
        auto t = toml::parse_file(U8(file));
        if (auto* a = t["approved"].as_table())
            for (auto&& [k, v] : *a)
                if (auto s = v.value<std::string>(); s && s->rfind(name + "|", 0) == 0)
                    old.use[std::strtoull(std::string(k.str()).c_str(), nullptr, 16)] = s->substr(name.size() + 1);
    } catch (const std::exception&) {
    }
    pm::Apply(m, old);
}

void Scan(fs::path dir, fs::path cache) {
    pm::Model m;
    m.dir = dir;
    // the game's textures, from the original archives
    g_stage = kReading;
    const std::vector<fs::path> archives = GameArchives();
    std::vector<con::Archive> parsed(archives.size());
    std::vector<std::pair<size_t, size_t>> items;
    for (size_t a = 0; a < archives.size(); a++) {
        const fs::path path = Extracted() / archives[a];
        std::error_code ec;
        std::string error;
        if (!con::Parse(con::FileSource(path), fs::file_size(path, ec), parsed[a], error)) continue;
        for (size_t e = 0; e < parsed[a].entries.size(); e++) {
            const std::string& n = parsed[a].entries[e].name;
            if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".dds") == 0) items.emplace_back(a, e);
        }
    }
    g_done = 0;
    g_errors = 0;
    g_total = items.size();
    std::vector<pm::Tex> found(items.size());
    std::vector<char> ok(items.size(), 0);
    std::vector<std::vector<con::Source>> sources(16, std::vector<con::Source>(archives.size()));
    Parallel(items.size(), [&](size_t i, unsigned thread) {
        auto [a, e] = items[i];
        con::Source& source = sources[thread][a];
        if (!source) source = con::FileSource(Extracted() / archives[a]);
        std::vector<uint8_t> file, px;
        Dds dds;
        if (!con::ReadEntry(source, parsed[a].entries[e], file) || !ParseDds(file, dds) || !DecodeDds(file, dds, px)) {
            g_errors++;
            return;
        }
        pm::Tex& t = found[i];
        t.places.push_back({archives[a], parsed[a].entries[e].name});
        t.w = dds.w;
        t.h = dds.h;
        t.format = dds.format;
        t.key = GameKey(file, dds);
        t.d = packs::Describe(px.data(), dds.w, dds.h);
        ok[i] = 1;
    });
    std::unordered_map<uint64_t, size_t> by_key;
    for (size_t i = 0; i < found.size(); i++) {
        if (!ok[i]) continue;
        if (found[i].key) {
            auto [it, fresh] = by_key.emplace(found[i].key, m.tex.size());
            if (!fresh) {
                m.tex[it->second].places.push_back(found[i].places[0]);
                continue;
            }
        }
        m.tex.push_back(std::move(found[i]));
    }
    found.clear();

    // the pack, sharing the game's descriptor cache
    g_stage = kIndexing;
    g_done = 0;
    std::atomic<size_t> total{0}, errors{0};
    m.index = packs::IndexFolder(dir, cache, total, g_done, errors);
    g_total = total.load();
    pm::Prepare(m);

    g_stage = kMatching;
    g_done = 0;
    g_total = m.tex.size();
    Parallel(m.tex.size(), [&](size_t i, unsigned) { m.tex[i].cands = pm::Match(m.tex[i], m); });
    if (g_cancel) {
        g_worker_result = "Matching stopped.";
        g_stage = kIdle;
        return;
    }
    pm::Rank(m);
    packs::PackMatches saved;
    std::string error;
    const bool own = packs::ReadMatches(dir, saved, &error);
    if (own) pm::Apply(m, saved);
    else ApplyOldChoices(m, U8(dir.filename()));
    size_t counts[pm::kStatuses] = {};
    for (const pm::Tex& t : m.tex) counts[pm::StatusOf(t)]++;
    g_worker_result = std::to_string(counts[pm::kAuto]) + " sure matches, " + std::to_string(counts[pm::kCheck]) + " to check" +
                      (own ? ", saved matches loaded" : "") + (errors ? " (" + std::to_string(errors.load()) + " pack images unreadable)" : "") +
                      (error.empty() ? "." : ". " + std::string(packs::kMatchesFile) + " not read: " + error);
    g_m = std::move(m);
    g_stage = kReady;
}

void StartScan() {
    Join();
    ForgetPictures();
    g_m = {};
    g_undo.clear();
    g_deferred.clear();
    g_rejected.clear();
    g_alternative.clear();
    g_selected = g_all_sel = -1;
    g_preview = -2;
    g_cancel = false;
    g_dirty = false;
    SetUnsaved("Texture packs", false);
    g_worker_result.clear();
    const fs::path dir = FromU8(Setting("pack"));
    g_worker = std::thread(Scan, dir, GameDir() / "textures" / "cache" / packs::CacheName(U8(dir.filename())));
}

void Save() {
    std::string error;
    if (!packs::WriteMatches(g_m.dir, pm::Collect(g_m), &error)) {
        Tell("Saving the matches failed: " + error);
        return;
    }
    g_dirty = false;
    SetUnsaved("Texture packs", false);
    std::error_code ec;
    const bool in_game = fs::equivalent(g_m.dir.parent_path(), GameDir() / "textures" / "packs", ec);
    Notify(std::string("Saved ") + packs::kMatchesFile + " in the pack folder." +
           (in_game ? " The game uses it from its next start." : " Put the pack in textures\\packs for the game to use it."));
}

void BuildMod(fs::path mod, std::string name) {
    std::vector<size_t> todo;
    for (size_t i = 0; i < g_m.tex.size(); i++)
        if (pm::Chosen(g_m.tex[i]) >= 0 && CanEncode(g_m.tex[i].format)) todo.push_back(i);
    g_done = 0;
    g_errors = 0;
    g_total = todo.size();
    Parallel(todo.size(), [&](size_t n, unsigned) {
        const pm::Tex& t = g_m.tex[todo[n]];
        const int image = pm::Chosen(t);
        std::vector<uint8_t> px, file;
        uint32_t w = 0, h = 0;
        Dds dds;
        if (!packs::ReadImage(g_m.dir / FromU8(g_m.index[image].file), px, w, h) || !ReadTexture(t.places[0], file, dds)) {
            g_errors++;
            return;
        }
        if (g_m.index[image].d.ps2_alpha) packs::ScaleAlpha(px.data(), px.size() / 4);
        px = Resize(px.data(), w, h, t.w, t.h);
        const std::vector<uint8_t> out = EncodeDds(file, dds, px.data(), t.w, t.h);
        for (const pm::Place& p : t.places) {
            fs::path folder = mod / p.archive;
            folder += L".files";
            if (out.empty() || !WriteFile(folder / con::SafePath(p.entry), out)) g_errors++;
        }
    });
    WriteModToml(mod, name, "PS2 texture pack images at the game's texture sizes, made with RFD Switchblade.");
    g_worker_result = std::string(g_cancel ? "Stopped after " : "Wrote ") + std::to_string(g_done.load()) + " textures to mods\\" + name +
                      (g_errors ? " (" + std::to_string(g_errors.load()) + " failed)" : "") + ". Turn it on in F6 > Mods.";
    g_stage = kReady;
}

// an image from anywhere becomes part of the pack (in _imported) and the texture's choice
void Import(int tex, const fs::path& file) {
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> bytes;
    Dds dds;
    if (ReadFile(file, bytes) && ParseDds(bytes, dds) && DecodeDds(bytes, dds, px)) {
        w = dds.w;
        h = dds.h;
    } else if (!packs::ReadImage(file, px, w, h)) {
        Tell(U8(file.filename()) + " is not an image RFD Switchblade can read.");
        return;
    }
    std::string stem = U8(fs::path(FromU8(g_m.tex[tex].places[0].entry)).stem());
    for (char& c : stem)
        if (std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
    const fs::path out = g_m.dir / "_imported" / FromU8(stem + "_" + std::to_string(g_m.tex[tex].key & 0xFFFFFF) + ".png");
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (!WritePng(out, px.data(), w, h)) {
        Tell("Writing " + U8(out) + " failed.");
        return;
    }
    const int image = pm::AddImage(g_m, out);
    if (image < 0) return;
    std::vector<Change> undo;
    Set(undo, tex, Choice::Pick, image);
    Commit(std::move(undo));
    g_preview = -2;
    g_all_sel = -1;
    Notify("Imported " + U8(file.filename()) + " into the pack's _imported folder for " + g_m.tex[tex].places[0].entry + ".");
}

// ---------- review ----------

std::vector<int> Queue() {
    std::vector<int> q, later;
    for (size_t i = 0; i < g_m.tex.size(); i++) {
        const pm::Tex& t = g_m.tex[i];
        if (t.choice != Choice::Auto) continue;
        const pm::Status s = pm::StatusOf(t);
        const bool weak = s == pm::kNoMatch && g_weak && pm::Suggestion(t) >= 0 && pm::Similarity(t.cands[pm::Suggestion(t)].s) >= .5f;
        if (s != pm::kCheck && !weak) continue;
        (g_deferred.count(int(i)) ? later : q).push_back(int(i));
    }
    auto order = [](int a, int b) { return pm::Confidence(g_m.tex[a]) > pm::Confidence(g_m.tex[b]); };
    std::stable_sort(q.begin(), q.end(), order);
    std::stable_sort(later.begin(), later.end(), order);
    q.insert(q.end(), later.begin(), later.end());
    return q;
}

int CardCandidate(int tex) {
    auto it = g_alternative.find(tex);
    return it != g_alternative.end() ? it->second : pm::Suggestion(g_m.tex[tex]);
}

std::string Tags(const pm::Candidate& c, bool mutual) {
    std::string s;
    auto add = [&](const char* t) { s += (s.empty() ? "" : " · ") + std::string(t); };
    if (c.v == packs::Verdict::Accept) add("sure");
    if (mutual) add("best pair");
    if (c.recolour) add("recoloured");
    if (c.other_shape) add("other shape: stretched");
    if (c.taken) add("used elsewhere");
    return s;
}

void OpenInBrowse(int tex) {
    g_mode = 1;
    SetSetting("packs.mode", "1");
    g_selected = tex;
    g_preview = -2;
    g_scroll_list = true;
    g_show[pm::StatusOf(g_m.tex[tex])] = true;
}

void Review() {
    const std::vector<int> queue = Queue();
    const float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    if (queue.empty()) {
        ImGui::Dummy(ImVec2(0, em * 2));
        Heading(ICON_CHECK "  Nothing left to review");
        Hint("Every close call has been decided. Save the matches, or use Browse to look at any texture.");
        ImGui::Checkbox("Also review weak guesses (50% alike and up)", &g_weak);
        return;
    }
    const int n = std::min<int>(kPageSizes[std::clamp(g_page, 0, 2)], int(queue.size()));
    ImGui::Text("%zu left to review", queue.size());
    ImGui::SameLine();
    ImGui::TextDisabled("  most likely first. Click a card to reject it; %s / %s on a card shows its other images.", ICON_LEFT, ICON_RIGHT);
    ImGui::SameLine(0, em * 2);
    ImGui::Checkbox("Weak guesses", &g_weak);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also offer textures without a close call whose best image is at least 50%% alike.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 7.5f);
    ImGui::Combo("##page", &g_page, "12 per page\0" "24 per page\0" "48 per page\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 8);
    ImGui::SliderFloat("##card", &g_card, em * 12, em * 30, "card size");

    // the cards
    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.6f;
    ImGui::BeginChild("cards", ImVec2(0, ImGui::GetContentRegionAvail().y - footer));
    const ImGuiStyle& st = ImGui::GetStyle();
    const float card_w = g_card, half = (card_w - st.ItemSpacing.x) / 2;
    const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / (card_w + st.ItemSpacing.x * 2)));
    for (int k = 0; k < n; k++) {
        const int tex = queue[k];
        pm::Tex& t = g_m.tex[tex];
        int ci = CardCandidate(tex);
        if (ci >= int(t.cands.size())) ci = int(t.cands.size()) - 1;
        if (k % cols) ImGui::SameLine(0, st.ItemSpacing.x * 2);
        ImGui::PushID(tex);
        ImGui::BeginGroup();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool rejected = g_rejected.count(tex) != 0;
        auto game = GamePicture(tex, kThumbSide);
        auto pack = ci >= 0 ? PackPicture(t.cands[ci].image, kThumbSide) : nullptr;
        PictureBox(game, ImVec2(half, half));
        ImGui::SameLine();
        PictureBox(pack, ImVec2(half, half), ci >= 0 ? "loading" : "no image");
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("toggle", ImVec2(card_w, half))) {
            if (rejected) g_rejected.erase(tex);
            else g_rejected.insert(tex);
        }
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (rejected) {
            draw->AddRectFilled(at, ImVec2(at.x + card_w, at.y + half), IM_COL32(120, 10, 10, 120), 4);
            ImGui::PushFont(HeadingFont());
            const ImVec2 size = ImGui::CalcTextSize(ICON_CANCEL);
            draw->AddText(ImVec2(at.x + (card_w - size.x) / 2, at.y + (half - size.y) / 2), IM_COL32(255, 120, 110, 255), ICON_CANCEL);
            ImGui::PopFont();
        }
        draw->AddRect(at, ImVec2(at.x + card_w, at.y + half), rejected ? IM_COL32(220, 70, 60, 255) : hovered ? IM_COL32(150, 220, 100, 200) : IM_COL32(40, 80, 25, 255), 4, 0, 2);
        if (hovered && ImGui::BeginTooltip()) {
            const float big = em * 20;
            PictureBox(GamePicture(tex, kBigSide), ImVec2(big, big));
            ImGui::SameLine();
            PictureBox(ci >= 0 ? PackPicture(t.cands[ci].image, kBigSide) : nullptr, ImVec2(big, big), ci >= 0 ? "loading" : "");
            ImGui::TextUnformatted(t.places[0].entry.c_str());
            if (ci >= 0) ImGui::TextDisabled("%s", g_m.index[t.cands[ci].image].file.c_str());
            ImGui::EndTooltip();
        }
        // caption: likeness, tags, other candidates
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(at.x + card_w - em * 6.9f, at.y + half + em * 4), true);
        if (ci >= 0) {
            const pm::Candidate& c = t.cands[ci];
            ImGui::TextColored(rejected ? ImVec4(.9f, .45f, .4f, 1) : ImVec4(.85f, .95f, .75f, 1), "%s %.0f%%", rejected ? "skip" : "use", pm::Similarity(c.s) * 100);
            ImGui::SameLine();
            ImGui::TextColored(c.other_shape ? ImVec4(1, .65f, .35f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                               Tags(c, t.mutual && ci == pm::Suggestion(t)).c_str());
        }
        ImGui::PopClipRect();
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(at.x + card_w, at.y + half + em * 4), true);
        ImGui::TextDisabled("%s", t.places[0].entry.c_str());
        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(at.x + card_w - em * 6.6f, at.y + half + st.ItemSpacing.y));
        ImGui::BeginDisabled(ci <= 0);
        if (ImGui::SmallButton(ICON_LEFT)) g_alternative[tex] = ci - 1;
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(ci + 1 >= int(t.cands.size()));
        if (ImGui::SmallButton(ICON_RIGHT)) g_alternative[tex] = ci + 1;
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton(ICON_GRID)) OpenInBrowse(tex);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("See every pack image for this texture");
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + half + em * 3.2f));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::EndGroup();
        ImGui::PopID();
    }
    // the next page's pictures, ahead of time
    for (int k = n; k < std::min<int>(n * 2, int(queue.size())); k++) {
        GamePicture(queue[k], kThumbSide, true);
        if (const int ci = pm::Suggestion(g_m.tex[queue[k]]); ci >= 0) PackPicture(g_m.tex[queue[k]].cands[ci].image, kThumbSide, true);
    }
    ImGui::EndChild();

    // accept or skip the page
    int rejected = 0;
    for (int k = 0; k < n; k++) rejected += g_rejected.count(queue[k]) != 0;
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .07f, 1));
    const std::string accept = std::string(ICON_CHECK "  Accept page: use ") + std::to_string(n - rejected) + ", skip " + std::to_string(rejected) + "   (Enter)";
    if (ImGui::Button(accept.c_str()) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        std::vector<Change> undo;
        for (int k = 0; k < n; k++) {
            const int tex = queue[k];
            const int ci = std::min(CardCandidate(tex), int(g_m.tex[tex].cands.size()) - 1);
            if (g_rejected.count(tex) || ci < 0) Set(undo, tex, Choice::Skip);
            else Set(undo, tex, Choice::Pick, g_m.tex[tex].cands[ci].image);
            g_rejected.erase(tex);
            g_alternative.erase(tex);
        }
        Notify("Page accepted: " + std::to_string(n - rejected) + " used, " + std::to_string(rejected) + " skipped. Ctrl+Z undoes it.");
        Commit(std::move(undo));
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (IconButton(ICON_RIGHT, "Skip page for now   (Space)") || (keys && ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
        for (int k = 0; k < n; k++) g_deferred.insert(queue[k]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reject all on page")) for (int k = 0; k < n; k++) g_rejected.insert(queue[k]);
    ImGui::SameLine();
    if (ImGui::Button("Clear rejections")) for (int k = 0; k < n; k++) g_rejected.erase(queue[k]);
}

// ---------- browse ----------

void SelectTex(int tex) {
    g_selected = tex;
    g_preview = -2;
    g_scroll_list = true;
}

void BrowseList(std::vector<int>& shown, int& row) {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", ICON_SEARCH "  Filter textures by name", g_search, sizeof(g_search));
    const std::string search = Lower(g_search);
    shown.clear();
    for (size_t i = 0; i < g_m.tex.size(); i++) {
        if (!g_show[pm::StatusOf(g_m.tex[i])]) continue;
        if (!search.empty() && Lower(g_m.tex[i].places[0].entry).find(search) == std::string::npos) continue;
        shown.push_back(int(i));
    }
    auto like = [](int i) { const auto& c = g_m.tex[i].cands; return c.empty() ? -1.f : pm::Similarity(c[0].s); };
    if (g_sort == 0) std::stable_sort(shown.begin(), shown.end(), [&](int a, int b) { return like(a) > like(b); });
    else std::stable_sort(shown.begin(), shown.end(), [](int a, int b) { return g_m.tex[a].places[0].entry < g_m.tex[b].places[0].entry; });
    if (Automate().select >= 0 && g_selected < 0 && Automate().select < int(shown.size())) SelectTex(shown[Automate().select]);
    row = -1;
    for (size_t r = 0; r < shown.size(); r++)
        if (shown[r] == g_selected) row = int(r);
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (keys && !shown.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) SelectTex(shown[row < 0 ? 0 : std::min(row + 1, int(shown.size()) - 1)]);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) SelectTex(shown[row <= 0 ? 0 : row - 1]);
        for (size_t r = 0; r < shown.size(); r++)
            if (shown[r] == g_selected) row = int(r);
    }
    if (ImGui::BeginTable("list", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Texture");
        ImGui::TableSetupColumn("Like", ImGuiTableColumnFlags_WidthFixed, em * 2.4f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, em * 5);
        ImGui::TableHeadersRow();
        if (g_scroll_list && row >= 0) {
            const float y = row * ImGui::GetTextLineHeightWithSpacing(), top = ImGui::GetScrollY(), h = ImGui::GetWindowHeight() - em * 3;
            if (y < top || y > top + h) ImGui::SetScrollY(std::max(0.f, y - h / 3));
        }
        g_scroll_list = false;
        ImGuiListClipper clipper;
        clipper.Begin(int(shown.size()));
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
                const int i = shown[r];
                const pm::Tex& t = g_m.tex[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable((t.places[0].entry + "##" + std::to_string(i)).c_str(), g_selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    g_selected = i;
                    g_preview = -2;
                }
                ImGui::TableNextColumn();
                if (like(i) >= 0) ImGui::TextDisabled("%.0f", like(i) * 100);
                ImGui::TableNextColumn();
                const pm::Status s = pm::StatusOf(t);
                ImGui::TextColored(kStatusColours[s], "%s", kStatusNames[s]);
            }
        ImGui::EndTable();
    }
    if (row >= 0 && row + 1 < int(shown.size())) GamePicture(shown[row + 1], kBigSide, true);
}

void Browse() {
    const float em = ImGui::GetFontSize();
    size_t counts[pm::kStatuses] = {};
    for (const pm::Tex& t : g_m.tex) counts[pm::StatusOf(t)]++;
    for (int s = 0; s < pm::kStatuses; s++) {
        if (s) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kStatusColours[s]);
        ImGui::Checkbox((std::string(kStatusNames[s]) + " " + std::to_string(counts[s])).c_str(), &g_show[s]);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 9);
    ImGui::Combo("##sort", &g_sort, "Most alike first\0By name\0");

    std::vector<int> shown;
    int row = -1;
    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("list", ImVec2(std::max(em * 16, ImGui::GetContentRegionAvail().x * .24f), height), ImGuiChildFlags_Border);
    BrowseList(shown, row);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("detail", ImVec2(0, height));
    if (g_selected < 0 || g_selected >= int(g_m.tex.size())) {
        ImGui::Dummy(ImVec2(0, em * 2));
        Hint("Pick a texture on the left. Below it you will see every image of the pack, most alike first, to choose from; "
             "or drop an image from anywhere on the window to use it for that texture.");
        ImGui::EndChild();
        return;
    }
    pm::Tex& t = g_m.tex[g_selected];
    const int chosen = pm::Chosen(t);
    const int guess = chosen < 0 && t.choice != Choice::Skip && pm::Suggestion(t) >= 0 ? t.cands[pm::Suggestion(t)].image : -1;
    const int shown_image = g_preview == -2 ? (chosen >= 0 ? chosen : guess) : g_preview;
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    // the comparison
    ImGui::PushFont(HeadingFont());
    ImGui::TextUnformatted(t.places[0].entry.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s  ·  %s %u x %u%s", U8(t.places[0].archive).c_str(), FormatName(t.format), t.w, t.h,
                        t.places.size() > 1 ? ("  ·  " + std::to_string(t.places.size()) + " copies").c_str() : "");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    const float side = std::clamp(ImGui::GetContentRegionAvail().y * .46f, em * 8, half);
    ImGui::BeginGroup();
    ImGui::TextDisabled("GAME");
    PictureBox(GamePicture(g_selected, kBigSide), ImVec2(half, side));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (shown_image < 0) ImGui::TextDisabled(t.choice == Choice::Skip ? "PACK  -  skipped, the game keeps its own" : "PACK  -  nothing chosen");
    else if (shown_image == chosen) ImGui::TextColored(kStatusColours[0], "PACK  -  in use  %s", ICON_CHECK);
    else if (shown_image == guess && g_preview == -2) ImGui::TextColored(kStatusColours[1], "PACK  -  best guess, Enter to use it");
    else ImGui::TextColored(kStatusColours[1], "PACK  -  previewing, Enter to use it");
    PictureBox(shown_image >= 0 ? PackPicture(shown_image, kBigSide) : nullptr, ImVec2(half, side), shown_image >= 0 ? "loading" : "",
               shown_image >= 0 && shown_image != chosen ? IM_COL32(240, 200, 90, 255) : 0);
    ImGui::EndGroup();

    // actions
    std::vector<Change> undo;
    ImGui::BeginDisabled(shown_image < 0 || shown_image == chosen);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .07f, 1));
    if (IconButton(ICON_CHECK, "Use this   (Enter)") || (keys && shown_image >= 0 && shown_image != chosen && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        Set(undo, g_selected, Choice::Pick, shown_image);
        g_preview = -2;
        if (row >= 0 && row + 1 < int(shown.size())) SelectTex(shown[row + 1]);
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton(ICON_CANCEL, "Skip   (S)", "This pack leaves the texture alone; the game keeps its own.") || (keys && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::GetIO().KeyCtrl)) {
        Set(undo, g_selected, Choice::Skip);
        g_preview = -2;
        if (row >= 0 && row + 1 < int(shown.size())) SelectTex(shown[row + 1]);
    }
    ImGui::SameLine();
    if (IconButton(ICON_REFRESH, "Automatic   (A)", "The best image only when it is a sure match, as the game does.") || (keys && ImGui::IsKeyPressed(ImGuiKey_A, false))) {
        Set(undo, g_selected, Choice::Auto);
        g_preview = -2;
    }
    ImGui::SameLine();
    if (IconButton(ICON_IMPORT, "Import image...", "Use an image from anywhere: it is copied into the pack's _imported folder. You can also drop it on the window.")) {
        fs::path file = OpenFile(L"Images (*.png;*.dds;*.jpg;*.bmp;*.tga)\0*.png;*.dds;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0");
        if (!file.empty()) Import(g_selected, file);
    }
    if (shown_image >= 0) {
        ImGui::SameLine();
        if (IconButton(ICON_FOLDER, "Show")) ShowInExplorer(g_m.dir / FromU8(g_m.index[shown_image].file));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g_m.index[shown_image].file.c_str());
    }
    Commit(std::move(undo));
    if (!Dropped().empty()) Import(g_selected, Dropped().front());

    // every image of the pack, most alike first
    if (g_all_sel != g_selected) {
        g_all = pm::AllImages(t, g_m);
        g_all_sel = g_selected;
    }
    ImGui::SeparatorText("All pack images");
    std::set<std::string> folders;
    for (const auto& img : g_m.index) {
        const size_t slash = img.file.find_last_of("\\/");
        folders.insert(slash == std::string::npos ? "" : img.file.substr(0, slash));
    }
    ImGui::SetNextItemWidth(em * 12);
    if (ImGui::BeginCombo("##folder", g_folder.empty() ? "All folders" : g_folder.c_str())) {
        if (ImGui::Selectable("All folders", g_folder.empty())) g_folder.clear();
        for (const std::string& f : folders)
            if (!f.empty() && ImGui::Selectable(f.c_str(), g_folder == f)) g_folder = f;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 12);
    ImGui::InputTextWithHint("##isearch", ICON_SEARCH "  File name", g_image_search, sizeof(g_image_search));
    ImGui::SameLine();
    ImGui::Checkbox("Hide images in use", &g_hide_used);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 8);
    ImGui::SliderFloat("##thumb", &g_thumb, em * 5, em * 14, "size");
    std::unordered_set<int> used;
    if (g_hide_used)
        for (const pm::Tex& o : g_m.tex)
            if (const int c = pm::Chosen(o); c >= 0 && &o != &t) used.insert(c);
    const std::string isearch = Lower(g_image_search);
    std::vector<const pm::Ranked*> list;
    for (const pm::Ranked& r : g_all) {
        const std::string& file = g_m.index[r.image].file;
        if (!g_folder.empty() && file.rfind(g_folder + "\\", 0) != 0 && file.rfind(g_folder + "/", 0) != 0) continue;
        if (!isearch.empty() && Lower(file).find(isearch) == std::string::npos) continue;
        if (used.count(r.image)) continue;
        list.push_back(&r);
    }
    // Left/Right step through this list
    if (keys && !list.empty()) {
        int at = -1;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i]->image == shown_image) at = int(i);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) g_preview = list[std::min(at + 1, int(list.size()) - 1)]->image;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) g_preview = list[std::max(at - 1, 0)]->image;
    }
    ImGui::BeginChild("grid");
    const ImGuiStyle& st = ImGui::GetStyle();
    const float cell = g_thumb, cell_h = cell + ImGui::GetTextLineHeightWithSpacing() * 2;
    const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / (cell + st.ItemSpacing.x)));
    ImGuiListClipper clipper;
    clipper.Begin(int((list.size() + cols - 1) / cols), cell_h + st.ItemSpacing.y);
    while (clipper.Step())
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++)
            for (int c = 0; c < cols; c++) {
                const size_t i = size_t(r) * cols + c;
                if (i >= list.size()) break;
                const pm::Ranked& item = *list[i];
                if (c) ImGui::SameLine();
                ImGui::PushID(item.image);
                ImGui::BeginGroup();
                const ImVec2 at = ImGui::GetCursorScreenPos();
                auto pic = PackPicture(item.image, kThumbSide);
                const ImU32 frame = item.image == chosen ? IM_COL32(150, 230, 100, 255) : item.image == shown_image ? IM_COL32(240, 200, 90, 255) : 0;
                PictureBox(pic, ImVec2(cell, cell), "loading", frame);
                ImGui::SetCursorScreenPos(at);
                if (ImGui::InvisibleButton("pick", ImVec2(cell, cell))) g_preview = item.image;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    std::vector<Change> u;
                    Set(u, g_selected, Choice::Pick, item.image);
                    Commit(std::move(u));
                    g_preview = -2;
                }
                if (ImGui::IsItemHovered() && pic && pic->id && ImGui::BeginTooltip()) {
                    PictureBox(PackPicture(item.image, kBigSide), ImVec2(em * 22, em * 22));
                    ImGui::TextUnformatted(g_m.index[item.image].file.c_str());
                    ImGui::TextDisabled("%u x %u  ·  detail %.0f%%, colour difference %.2f", g_m.index[item.image].d.w, g_m.index[item.image].d.h,
                                        std::max(0.f, item.s.ncc) * 100, item.s.color);
                    ImGui::EndTooltip();
                }
                ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(at.x + cell, at.y + cell_h), true);
                const packs::Verdict v = packs::Judge(item.s);
                ImGui::TextColored(v == packs::Verdict::Accept ? kStatusColours[0] : v == packs::Verdict::Review ? kStatusColours[1] : ImVec4(.6f, .65f, .6f, 1),
                                   "%.0f%%%s", pm::Similarity(item.s) * 100, item.image == chosen ? "  in use" : item.image == guess ? "  best guess" : "");
                const std::string& file = g_m.index[item.image].file;
                const size_t slash = file.find_last_of("\\/");
                ImGui::TextDisabled("%s%s", item.other_shape ? "other shape · " : "", slash == std::string::npos ? "" : file.substr(0, slash).c_str());
                ImGui::PopClipRect();
                ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + cell_h));
                ImGui::Dummy(ImVec2(cell, 0));
                ImGui::EndGroup();
                ImGui::PopID();
            }
    ImGui::EndChild();
    ImGui::EndChild();
}

void PackChooser() {
    const float em = ImGui::GetFontSize();
    const fs::path current = FromU8(Setting("pack"));
    std::vector<fs::path> folders;
    std::error_code ec;
    for (fs::directory_iterator it(GameDir() / "textures" / "packs", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) folders.push_back(it->path());
    if (current.empty() && folders.size() == 1) SetSetting("pack", U8(folders[0]));  // the only pack there is
    ImGui::SetNextItemWidth(em * 20);
    if (ImGui::BeginCombo("##pack", current.empty() ? "Choose a pack" : (ICON_LIBRARY "  " + U8(current.filename())).c_str())) {
        for (const fs::path& f : folders)
            if (ImGui::Selectable(U8(f.filename()).c_str(), f == current)) SetSetting("pack", U8(f));
        if (folders.empty()) ImGui::TextDisabled("No packs in textures\\packs.");
        ImGui::Separator();
        if (ImGui::Selectable(ICON_FOLDER "  Other folder...")) {
            fs::path dir = PickFolder(L"The pack's folder of PNG images (for PCSX2: the replacements folder)", current);
            if (!dir.empty()) SetSetting("pack", U8(dir));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const bool loaded = !g_m.tex.empty() && g_m.dir == current;
    ImGui::BeginDisabled(current.empty());
    if (IconButton(ICON_REFRESH, loaded ? "Match again" : "Match")) {
        if (!g_dirty || Ask("Your unsaved choices will be lost. Match again?")) StartScan();
    }
    ImGui::EndDisabled();
}
}  // namespace

bool PacksBusy(Activity& a) {
    const int stage = g_stage;
    if (stage != kReading && stage != kIndexing && stage != kMatching && stage != kBuilding) return false;
    a.what = stage == kReading ? "Reading the game's textures" : stage == kIndexing ? "Reading the pack (once; kept in textures\\cache)"
           : stage == kMatching ? "Comparing every texture with every pack image" : "Building the mod";
    a.progress = g_total ? float(g_done) / float(g_total) : -1;
    return true;
}

void PacksPage() {
    const float em = ImGui::GetFontSize();
    if (g_card == 0) {
        g_card = SettingFloat("packs.card", em * 17);
        g_thumb = SettingFloat("packs.thumb", em * 8);
        g_page = std::atoi(Setting("packs.page", "1").c_str());
        g_mode = std::atoi(Setting("packs.mode", "0").c_str());
        g_weak = Setting("packs.weak") == "1";
        if (Automate().packs_mode >= 0) g_mode = Automate().packs_mode;
    }
    const int stage = g_stage;
    const bool busy = stage == kReading || stage == kIndexing || stage == kMatching || stage == kBuilding;
    if (!busy) Join();
    if (stage == kReady && !g_worker_result.empty()) {
        Notify(g_worker_result);
        g_worker_result.clear();
    }

    // header
    Heading("Texture packs");
    ImGui::SameLine(0, em * 2);
    ImGui::BeginDisabled(busy);
    PackChooser();
    ImGui::EndDisabled();
    static bool auto_matched = false;
    if (Automate().match && !auto_matched && !busy && !Setting("pack").empty()) {
        auto_matched = true;
        StartScan();
    }
    const bool ready = !busy && !g_m.tex.empty();
    if (ready) {
        ImGui::SameLine(0, em * 2);
        const char* modes[] = {ICON_GRID "  Review", ICON_LIST "  Browse"};
        Segmented("mode", &g_mode, modes, 2);
        ImGui::SameLine(ImGui::GetWindowWidth() - em * 25);
        ImGui::BeginDisabled(g_undo.empty());
        if (IconButton(ICON_UNDO, "Undo", "Ctrl+Z")) Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (IconButton(ICON_SAVE, g_dirty ? "Save matches*" : "Save matches", "Writes rfd_matches.toml into the pack folder (Ctrl+S). The game reads it.")) Save();
        ImGui::SameLine();
        if (IconButton(ICON_PACKAGE, "Build mod...", "The chosen images resized to the game's sizes, as a mod to share without the pack.")) ImGui::OpenPopup("Build mod");
    }
    ImGui::Separator();

    if (busy) {
        Activity a;
        PacksBusy(a);
        ImGui::Dummy(ImVec2(0, em * 3));
        Heading(a.what.c_str());
        ImGui::ProgressBar(std::max(0.f, a.progress), ImVec2(std::min(ImGui::GetContentRegionAvail().x, em * 40), 0));
        ImGui::Text("%zu of %zu", g_done.load(), g_total.load());
        if (IconButton(ICON_CANCEL, "Stop")) g_cancel = true;
        return;
    }
    if (!ready) {
        ImGui::Dummy(ImVec2(0, em * 2));
        ImGui::PushTextWrapPos(em * 46);
        Heading("Match a PS2 texture pack to this game");
        Hint("Texture packs made for the PS2 Tenchu: Wrath of Heaven name their images by PS2 data, so they cannot be looked up "
             "directly. RFD Switchblade compares every texture of this game with every image of the pack, like the game does in "
             "F6 > Textures. Sure matches are taken; you review the close calls a page at a time, and can choose or import any "
             "image for any texture.");
        ImGui::Spacing();
        Hint("Choose a pack above (a folder in textures\\packs, or anywhere), then Match. The first match reads every image of the "
             "pack once; after that it takes seconds.");
        ImGui::PopTextWrapPos();
        for (const fs::path& d : Dropped()) {
            std::error_code ec;
            if (fs::is_directory(d, ec)) SetSetting("pack", U8(d));
        }
        return;
    }

    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (keys && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
    if (keys && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
    if (g_mode == 0) Review();
    else Browse();

    if (ImGui::BeginPopupModal("Build mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Every matched or chosen texture, resized to the game's size and converted\nto its format, as a mod in the mods folder.");
        ImGui::SetNextItemWidth(em * 20);
        ImGui::InputText("Mod name", g_mod_name, sizeof(g_mod_name));
        const bool ok = ValidModName(g_mod_name);
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Build", ImVec2(em * 6, 0))) {
            Join();
            g_cancel = false;
            g_stage = kBuilding;
            g_worker = std::thread(BuildMod, GameDir() / "mods" / FromU8(g_mod_name), std::string(g_mod_name));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(em * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // remember the layout
    if (Automate().page >= 0) return;
    SetSetting("packs.card", std::to_string(int(g_card)));
    SetSetting("packs.thumb", std::to_string(int(g_thumb)));
    SetSetting("packs.page", std::to_string(g_page));
    SetSetting("packs.mode", std::to_string(g_mode));
    SetSetting("packs.weak", g_weak ? "1" : "0");
}

}  // namespace wb
