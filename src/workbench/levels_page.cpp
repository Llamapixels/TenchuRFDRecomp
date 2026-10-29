// The Levels page: the game's stages in 3D, and custom levels made for F6 > Levels in the game
// (runtime/port/custom_levels.h). Data in level_model.cpp, drawing in viewport.cpp.
#include "level_model.h"
#include "ui.h"
#include "viewport.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace wb {
namespace {

enum Mode { kView, kMake };
struct State {
    bool started = false;
    std::string error;
    std::vector<lv::StageInfo> stages;
    int mode = kView;
    int view_stage = 0;  // index into stages
    // the stage being shown, loaded on a worker
    std::mutex lock;
    std::shared_ptr<lv::StageData> stage_data;
    int loaded_stage = -1, loading_stage = -1;
    std::atomic<bool> loading{false};
    bool show_map = true, show_collision = false, show_start = true, show_enemies = true, show_lights = true, show_items = true;
    // textures by t.con entry, uploaded as they decode
    std::map<std::string, std::shared_ptr<Picture>> textures;
    size_t textures_ready = 0;
    double last_rebuild = 0;
    bool dirty_scene = true;
    // the level being made
    lv::Level level;
    bool has_level = false, unsaved = false;
    int selected_box = -1, selected_model = -1, selected_enemy = -1, selected_light = -1, selected_item = -1;
    uint64_t revision = 0;  // goes up with every change to the level
    struct Budget {         // what the pack takes, worked out once per revision (it cuts every triangle)
        uint64_t revision = ~0ull;
        bool fits = false;
        size_t mesh = 0, collision = 0;
        int blocks = 0;
        uint32_t objects = 0;
        std::string problem;
    } budget;
    std::vector<std::string> level_textures;  // for the level's stage
    std::string status;
    Viewport viewport;
    Camera camera;
};
State& S() { static State s; return s; }

const lv::StageInfo* StageByNumber(int stage) {
    for (auto& s : S().stages) if (s.stage == stage) return &s;
    return nullptr;
}
const lv::StageInfo* ShownStage() {
    State& s = S();
    if (s.mode == kMake && s.has_level) return StageByNumber(s.level.stage);
    return s.view_stage < int(s.stages.size()) ? &s.stages[s.view_stage] : nullptr;
}

void Load(const lv::StageInfo& stage) {
    State& s = S();
    if (s.loading || s.loaded_stage == stage.stage) return;
    s.loading = true;
    s.loading_stage = stage.stage;
    std::thread([stage] {
        auto data = std::make_shared<lv::StageData>();
        std::string error;
        lv::LoadStage(stage, *data, error);
        State& s = S();
        std::lock_guard l(s.lock);
        s.stage_data = data;
        s.loaded_stage = stage.stage;
        s.loading = false;
        s.dirty_scene = true;
        if (!error.empty()) s.status = error;
    }).detach();
}

std::shared_ptr<Picture> Texture(const std::string& entry) {
    if (entry.empty()) return nullptr;
    State& s = S();
    auto& slot = s.textures[entry];
    if (slot && slot->id) return slot;
    slot = GetPicture(PictureKey("level texture " + entry, 512), 512, [entry](Bitmap& b) {
        return lv::LoadTexture(entry, b.px, b.w, b.h);
    });
    return slot && slot->id ? slot : nullptr;
}

// texture entries in use and how many are ready; textures arriving rebuild the scene now and then
void CountTextures() {
    State& s = S();
    size_t ready = 0;
    for (auto& [entry, picture] : s.textures) if (picture && picture->id) ready++;
    if (ready != s.textures_ready && ImGui::GetTime() - s.last_rebuild > 0.4) {
        s.textures_ready = ready;
        s.dirty_scene = true;
    }
}

void Line(std::vector<DrawVertex>& v, lv::Vec3 a, lv::Vec3 b, uint32_t colour) {
    v.push_back({a.x, a.y, a.z, colour, 0, 0});
    v.push_back({b.x, b.y, b.z, colour, 0, 0});
}
void Marker(std::vector<DrawVertex>& v, const float spawn[4], uint32_t colour) {
    const lv::Vec3 at{spawn[0], -spawn[1], spawn[2]};
    Line(v, at, {at.x, at.y + 180, at.z}, colour);
    Line(v, {at.x - 60, at.y, at.z}, {at.x + 60, at.y, at.z}, colour);
    Line(v, {at.x, at.y, at.z - 60}, {at.x, at.y, at.z + 60}, colour);
    // the direction it faces
    Line(v, {at.x, at.y + 90, at.z}, {at.x + std::sin(spawn[3]) * 150, at.y + 90, at.z + std::cos(spawn[3]) * 150}, colour);
}

// an item: a small diamond resting at its place (game coordinates)
void ItemMark(std::vector<DrawVertex>& v, float x, float y, float z, uint32_t colour) {
    const lv::Vec3 at{x, -y + 40, z};
    const lv::Vec3 c[4] = {{at.x + 40, at.y, at.z}, {at.x, at.y, at.z + 40}, {at.x - 40, at.y, at.z}, {at.x, at.y, at.z - 40}};
    for (int i = 0; i < 4; i++) {
        Line(v, c[i], c[(i + 1) % 4], colour);
        Line(v, c[i], {at.x, at.y + 40, at.z}, colour);
        Line(v, c[i], {at.x, at.y - 40, at.z}, colour);
    }
}

// a light: a small star at its place (game coordinates)
void LightMark(std::vector<DrawVertex>& v, float x, float y, float z, uint32_t colour) {
    const lv::Vec3 at{x, -y, z};
    for (const lv::Vec3 d : {lv::Vec3{40, 0, 0}, lv::Vec3{0, 40, 0}, lv::Vec3{0, 0, 40}, lv::Vec3{28, 28, 0}, lv::Vec3{0, 28, 28}})
        Line(v, {at.x - d.x, at.y - d.y, at.z - d.z}, {at.x + d.x, at.y + d.y, at.z + d.z}, colour);
}

void Rebuild() {
    State& s = S();
    s.dirty_scene = false;
    s.last_rebuild = ImGui::GetTime();
    std::vector<DrawBatch> batches;
    std::shared_ptr<lv::StageData> data;
    {
        std::lock_guard l(s.lock);
        data = s.stage_data;
    }
    const lv::StageInfo* stage = ShownStage();
    const bool making = s.mode == kMake && s.has_level;
    if (data && stage && data == s.stage_data && s.loaded_stage == stage->stage && (s.show_map || !making)) {
        const bool dim = making;  // the stage's own map under a level is a reference, drawn darker
        std::map<std::string, DrawBatch> by_texture;
        for (const auto& m : data->meshes)
            for (const auto& g : m.groups) {
                DrawBatch& b = by_texture[g.texture_entry];
                for (uint32_t i : g.triangles) {
                    const lv::Vertex& v = m.vertices[i];
                    uint32_t c = v.colour;
                    if (dim) c = (c & 0xFF000000) | ((c >> 2) & 0x003F3F3F);
                    b.vertices.push_back({v.x, v.y, v.z, c, v.u, v.v});
                }
            }
        for (auto& [entry, b] : by_texture) {
            b.texture = Texture(entry);
            batches.push_back(std::move(b));
        }
    }
    if (data && s.show_collision && stage && s.loaded_stage == stage->stage) {
        DrawBatch lines;
        lines.lines = true;
        for (const auto& t : data->collision)
            for (int e = 0; e < 3; e++) Line(lines.vertices, t[e], t[(e + 1) % 3], 0xFFE0C040);
        batches.push_back(std::move(lines));
    }
    DrawBatch marks;
    marks.lines = true;
    if (making) {
        std::map<std::string, DrawBatch> by_texture;
        for (const lv::Triangle& t : lv::LevelTriangles(s.level)) {
            DrawBatch& b = by_texture[t.texture];
            const uint32_t colour = lv::TriangleColour(s.level, t);
            for (int i = 0; i < 3; i++) b.vertices.push_back({t.p[i].x, t.p[i].y, t.p[i].z, colour, t.uv[i][0], t.uv[i][1]});
        }
        for (auto& [entry, b] : by_texture) {
            b.texture = Texture(entry);
            if (!b.texture) for (auto& v : b.vertices) v.colour = 0xFF6A7A6A;
            batches.push_back(std::move(b));
        }
        lv::Vec3 lo{}, hi{};
        bool outline = false;
        if (s.selected_box >= 0 && s.selected_box < int(s.level.boxes.size())) {
            const lv::Box& b = s.level.boxes[s.selected_box];
            lo = {b.x - b.width / 2, -b.bottom, b.z - b.depth / 2};
            hi = {b.x + b.width / 2, -b.bottom + b.height, b.z + b.depth / 2};
            outline = true;
        } else if (s.selected_model >= 0 && s.selected_model < int(s.level.models.size())) {
            outline = lv::ModelBox(s.level, s.selected_model, lo, hi);
        }
        if (outline) {
            const float x0 = lo.x - 4, x1 = hi.x + 4, z0 = lo.z - 4, z1 = hi.z + 4, y0 = lo.y - 4, y1 = hi.y + 4;
            const lv::Vec3 c[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
            for (int i = 0; i < 4; i++) {
                Line(marks.vertices, c[i], c[(i + 1) % 4], 0xFF60D0FF);
                Line(marks.vertices, c[i + 4], c[(i + 1) % 4 + 4], 0xFF60D0FF);
                Line(marks.vertices, c[i], c[i + 4], 0xFF60D0FF);
            }
        }
        Marker(marks.vertices, s.level.spawn, 0xFFFF5040);
        if (s.show_enemies) {  // the level's own, or the mission's it keeps (dimmer)
            const lv::StageInfo* st = StageByNumber(s.level.stage);
            const std::vector<lv::Enemy>& list = s.level.own_enemies ? s.level.enemies : st ? lv::StageEnemies(*st).enemies : s.level.enemies;
            for (int i = 0; i < int(list.size()); i++) {
                const lv::Enemy& e = list[i];
                if (e.removed || e.y < -50000) continue;
                const float at[4] = {e.x, e.y, e.z, e.facing * 3.14159265f / 180};
                const bool chosen = s.level.own_enemies && i == s.selected_enemy;
                Marker(marks.vertices, at, chosen ? 0xFF60D0FF : s.level.own_enemies ? 0xFFFFB020 : 0xFFA07818);
            }
        }
        if (s.show_items) {  // the level's items, or the mission's (dimmer)
            const lv::StageInfo* st = StageByNumber(s.level.stage);
            const std::vector<lv::Item> list = s.level.own_items ? s.level.items : st ? lv::StageItems(*st).items : std::vector<lv::Item>{};
            for (int i = 0; i < int(list.size()); i++)
                ItemMark(marks.vertices, list[i].x, list[i].y, list[i].z,
                         s.level.own_items && i == s.selected_item ? 0xFF60D0FF : s.level.own_items ? 0xFF60FF90 : 0xFF409060);
        }
        if (s.show_lights) {  // the level's placed lights, or the stage's (dimmer)
            const lv::StageInfo* st = StageByNumber(s.level.stage);
            const std::vector<lv::Light> list = s.level.own_lights ? s.level.lights : st ? lv::StageLights(*st) : std::vector<lv::Light>{};
            for (int i = 0; i < int(list.size()); i++)
                if (!list[i].removed)
                    LightMark(marks.vertices, list[i].x, list[i].y, list[i].z,
                              s.level.own_lights && i == s.selected_light ? 0xFF60D0FF : s.level.own_lights ? 0xFFFFF060 : 0xFFA0A040);
        }
    } else if (stage) {
        if (s.show_start) Marker(marks.vertices, stage->start, 0xFFFF5040);
        if (s.show_enemies)
            for (const lv::Enemy& e : lv::StageEnemies(*stage).enemies) {
                if (e.y < -50000) continue;
                const float at[4] = {e.x, e.y, e.z, e.facing * 3.14159265f / 180};
                Marker(marks.vertices, at, 0xFFFFB020);
            }
        if (s.show_lights)
            for (const lv::Light& l : lv::StageLights(*stage)) LightMark(marks.vertices, l.x, l.y, l.z, 0xFFFFF060);
        if (s.show_items)
            for (const lv::Item& i : lv::StageItems(*stage).items) ItemMark(marks.vertices, i.x, i.y, i.z, 0xFF60FF90);
    }
    batches.push_back(std::move(marks));
    s.viewport.SetBatches(std::move(batches));
}

void FrameStage() {
    State& s = S();
    std::shared_ptr<lv::StageData> data;
    {
        std::lock_guard l(s.lock);
        data = s.stage_data;
    }
    if (s.mode == kMake && s.has_level) {
        const float half = s.level.ground_size / 2;
        const float lo[3] = {s.level.spawn[0] - half, -s.level.ground_y, s.level.spawn[2] - half};
        const float hi[3] = {s.level.spawn[0] + half, -s.level.ground_y + 500, s.level.spawn[2] + half};
        s.camera.Frame(lo, hi);
    } else if (data && !data->meshes.empty()) {
        const float lo[3] = {data->low.x, data->low.y, data->low.z}, hi[3] = {data->high.x, data->high.y, data->high.z};
        s.camera.Frame(lo, hi);
    }
}

void SetLevelStage(int stage) {
    State& s = S();
    s.level.stage = stage;
    if (const lv::StageInfo* info = StageByNumber(stage)) {
        s.level_textures = lv::StageTextures(*info);
        Load(*info);
    }
    s.dirty_scene = true;
}

void Changed() {
    State& s = S();
    s.revision++;
    s.unsaved = true;
    s.dirty_scene = true;
    SetUnsaved("Levels", true);
}

bool TextureCombo(const char* label, std::string& entry) {
    State& s = S();
    bool changed = false;
    if (ImGui::BeginCombo(label, entry.empty() ? "(none)" : lv::TextureName(entry).c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& t : s.level_textures) {
            ImGui::PushID(t.c_str());
            if (auto pic = Texture(t)) { ImGui::Image(pic->id, ImVec2(ImGui::GetFontSize() * 1.6f, ImGui::GetFontSize() * 1.6f)); ImGui::SameLine(); }
            if (ImGui::Selectable(lv::TextureName(t).c_str(), t == entry)) { entry = t; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool StageCombo(const char* label, int& stage_number) {
    State& s = S();
    const lv::StageInfo* current = StageByNumber(stage_number);
    bool changed = false;
    if (ImGui::BeginCombo(label, current ? lv::StageLabel(*current).c_str() : "(none)")) {
        for (const auto& st : s.stages)
            if (ImGui::Selectable(lv::StageLabel(st).c_str(), st.stage == stage_number)) { stage_number = st.stage; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// the camera target dropped onto the ground plane, game coordinates
void ViewCentre(float& x, float& z) {
    x = S().camera.target[0];
    z = S().camera.target[2];
}

void ViewPanel() {
    State& s = S();
    Hint("The game's stages as the game draws them. Right-drag to turn, middle-drag (or Shift+right) to move, wheel to zoom, F to frame.");
    int number = s.view_stage < int(s.stages.size()) ? s.stages[s.view_stage].stage : -1;
    ImGui::SetNextItemWidth(-1);
    if (StageCombo("##stage", number))
        for (size_t i = 0; i < s.stages.size(); i++)
            if (s.stages[i].stage == number) { s.view_stage = int(i); SetSetting("levels_stage", std::to_string(number)); s.dirty_scene = true; }
    if (const lv::StageInfo* st = ShownStage()) {
        Load(*st);
        if (s.loading) ImGui::TextDisabled("Loading %s...", st->folder.c_str());
        else if (s.stage_data && s.loaded_stage == st->stage) {
            size_t tris = 0;
            for (auto& m : s.stage_data->meshes) for (auto& g : m.groups) tris += g.triangles.size() / 3;
            ImGui::Text("%zu blocks, %zu triangles", s.stage_data->meshes.size(), tris);
            ImGui::Text("%zu collision triangles", s.stage_data->collision.size());
        }
    }
    if (ImGui::Checkbox("Collision", &s.show_collision)) s.dirty_scene = true;
    if (ImGui::Checkbox("Player start", &s.show_start)) s.dirty_scene = true;
    if (ImGui::Checkbox("Enemies (Rikimaru, first layout)", &s.show_enemies)) s.dirty_scene = true;
    if (ImGui::Checkbox("Lights (candles, lanterns, torches)", &s.show_lights)) s.dirty_scene = true;
    if (ImGui::Checkbox("Items lying in the mission", &s.show_items)) s.dirty_scene = true;
    if (ImGui::Button(ICON_VIEW "  Frame stage")) FrameStage();
    ImGui::Spacing();
    if (ImGui::Button(ICON_EDIT "  Make a level on this stage") && ShownStage()) {
        s.level = lv::NewLevel(*ShownStage());
        s.revision++;
        s.has_level = true;
        s.selected_box = -1;
        s.mode = kMake;
        SetLevelStage(s.level.stage);
        Changed();
        FrameStage();
    }
}

void MakePanel() {
    State& s = S();
    if (IconButton(ICON_FILE, "New level")) {
        if (!s.unsaved || Ask("Discard the changes to " + s.level.name + "?")) {
            const lv::StageInfo* st = ShownStage() ? ShownStage() : (s.stages.empty() ? nullptr : &s.stages[0]);
            if (st) {
                s.level = lv::NewLevel(*st);
                s.revision++;
                s.has_level = true;
                s.selected_box = -1;
                SetLevelStage(s.level.stage);
                Changed();
                FrameStage();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::BeginCombo("##open", "Open...", ImGuiComboFlags_NoArrowButton)) {
        for (const auto& dir : lv::SavedLevels())
            if (ImGui::Selectable(U8(dir.filename()).c_str())) {
                lv::Level level;
                std::string error;
                if (lv::LoadLevel(dir, level, error)) {
                    s.level = level;
                    s.revision++;
                    s.has_level = true;
                    s.selected_box = -1;
                    s.unsaved = false;
                    SetUnsaved("Levels", false);
                    SetLevelStage(level.stage);
                    FrameStage();
                } else {
                    Notify("Could not open " + U8(dir.filename()) + ": " + error, true);
                }
            }
        ImGui::EndCombo();
    }
    if (!s.has_level) {
        Hint("Start a new level: a flat ground around a mission's start, then add boxes for walls and platforms. Save writes it "
             "into the game's levels folder; in the game, F6 > Levels > Use, then start that mission.");
        return;
    }
    lv::Level& l = s.level;
    char text[256];
    auto edit = [&](const char* label, std::string& value, bool multiline = false) {
        std::snprintf(text, sizeof(text), "%s", value.c_str());
        ImGui::SetNextItemWidth(-1);
        bool changed = multiline ? ImGui::InputTextMultiline(label, text, sizeof(text), ImVec2(-1, ImGui::GetFontSize() * 3))
                                 : ImGui::InputTextWithHint(label, label + 2, text, sizeof(text));
        if (changed) { value = text; Changed(); }
    };
    Heading("Level");
    edit("##Name", l.name);
    edit("##Author", l.author);
    edit("##Description", l.description, true);
    ImGui::TextUnformatted("Loads on");
    ImGui::SetNextItemWidth(-1);
    if (StageCombo("##loads", l.stage)) {
        if (const lv::StageInfo* st = StageByNumber(l.stage)) {
            const lv::Level fresh = lv::NewLevel(*st);
            std::memcpy(l.spawn, fresh.spawn, sizeof(l.spawn));
            l.ground_y = fresh.ground_y;
            l.ground_texture = fresh.ground_texture;
            for (auto& b : l.boxes) b.texture.clear();
        }
        SetLevelStage(l.stage);
        Changed();
        FrameStage();
    }
    Hint("Its mission, enemies and items stay; the map is replaced. Textures come from that stage's first area.");

    Heading("Ground");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##size", &l.ground_size, 50, 0, 200000, l.ground_size > 0 ? "Size %.0f" : "No ground")) Changed();
    if (l.ground_size <= 0) Hint("No ground: the level is its boxes and models (give the player start something to stand on).");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##height", &l.ground_y, 5, -20000, 20000, "Height %.0f")) Changed();
    ImGui::SetNextItemWidth(-1);
    if (TextureCombo("##ground texture", l.ground_texture)) Changed();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##scale", &l.texture_scale, 5, 50, 4000, "Texture repeat %.0f")) Changed();

    Heading("Lighting");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorEdit3("##light", l.light)) Changed();
    if (ImGui::Button("Plain")) { l.light[0] = l.light[1] = l.light[2] = .5f; Changed(); }
    ImGui::SameLine();
    if (ImGui::Button("Night")) { l.light[0] = .14f; l.light[1] = .15f; l.light[2] = .17f; Changed(); }  // the stages' own colours
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##sun", &l.sun, 0, 1, "Sun shading %.2f")) Changed();
    ImGui::BeginDisabled(l.sun <= 0);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderAngle("##sun yaw", &l.sun_yaw, -180, 180, "Sun from %.0f deg")) Changed();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderAngle("##sun height", &l.sun_height, 0, 90, "Sun height %.0f deg")) Changed();
    ImGui::EndDisabled();
    Hint("The light colours every surface: 128 is the plain brightness, higher is brighter (up to twice). "
         "Sun shading darkens the sides turned away from the sun.");

    Heading("Player start");
    // the mission's own start: a start elsewhere can land where the mission does something at the
    // start (stage 3's river kept the screen white), so only the facing is the level's
    lv::LockStart(l, StageByNumber(l.stage));
    ImGui::Text("%.0f, %.0f, %.0f", l.spawn[0], l.spawn[1], l.spawn[2]);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderAngle("##facing", &l.spawn[3], -180, 180, "Facing %.0f deg")) Changed();
    Hint("Fixed at the mission's own start, on the ground: build the level around it. "
         "Heights are the game's: smaller is higher (y points down).");

    Heading("Boxes");
    if (IconButton(ICON_IMPORT, "Add box")) {
        lv::Box b;
        ViewCentre(b.x, b.z);
        b.bottom = l.ground_y;
        b.texture = l.ground_texture;
        l.boxes.push_back(b);
        s.selected_box = int(l.boxes.size()) - 1;
        s.selected_model = -1;
        Changed();
    }
    for (int i = 0; i < int(l.boxes.size()); i++) {
        ImGui::PushID(i);
        char name[48];
        std::snprintf(name, sizeof(name), "Box %d  (%.0f x %.0f x %.0f)", i + 1, l.boxes[i].width, l.boxes[i].depth, l.boxes[i].height);
        if (ImGui::Selectable(name, s.selected_box == i)) { s.selected_box = i; s.selected_model = -1; s.dirty_scene = true; }
        ImGui::PopID();
    }
    if (s.selected_box >= 0 && s.selected_box < int(l.boxes.size())) {
        lv::Box& b = l.boxes[s.selected_box];
        float pos[3] = {b.x, b.bottom, b.z}, size[3] = {b.width, b.height, b.depth};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##pos", pos, 5, -100000, 100000, "%.0f")) { b.x = pos[0]; b.bottom = pos[1]; b.z = pos[2]; Changed(); }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##size", size, 5, 20, 20000, "%.0f")) { b.width = size[0]; b.height = size[1]; b.depth = size[2]; Changed(); }
        Hint("x, base height, z; then width, height, depth.");
        ImGui::SetNextItemWidth(-1);
        if (TextureCombo("##box texture", b.texture)) Changed();
        if (IconButton(ICON_CANCEL, "Remove box")) { l.boxes.erase(l.boxes.begin() + s.selected_box); s.selected_box = -1; Changed(); }
    }

    Heading("Models");
    if (IconButton(ICON_IMPORT, "Import OBJ")) {
        const fs::path file = OpenFile(L"Wavefront OBJ (*.obj)\0*.obj\0", FromU8(Setting("levels_models")));
        if (!file.empty()) {
            SetSetting("levels_models", U8(file.parent_path()));
            float x, z;
            ViewCentre(x, z);
            std::string error;
            if (lv::AddModel(l, file, x, z, error)) {
                s.selected_model = int(l.models.size()) - 1;
                s.selected_box = -1;
                Changed();
            } else {
                Notify("Could not import " + U8(file.filename()) + ": " + error, true);
            }
        }
    }
    Hint("Export from Blender as OBJ (Y up). Scale 100 turns metres into game units; a person is about 180 tall.");
    for (int i = 0; i < int(l.models.size()); i++) {
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(l.models[i].name.c_str(), s.selected_model == i)) { s.selected_model = i; s.selected_box = -1; s.dirty_scene = true; }
        ImGui::PopID();
    }
    if (s.selected_model >= 0 && s.selected_model < int(l.models.size())) {
        lv::Model& m = l.models[s.selected_model];
        float pos[3] = {m.x, m.y, m.z};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##model pos", pos, 5, -100000, 100000, "%.0f")) { m.x = pos[0]; m.y = pos[1]; m.z = pos[2]; Changed(); }
        Hint("x, height of its origin, z.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderAngle("##model yaw", &m.yaw, -180, 180, "Facing %.0f deg")) Changed();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##model scale", &m.scale, 0.5f, 0.01f, 100000, "Scale %.2f")) Changed();
        if (ImGui::Checkbox("Solid (collision)", &m.solid)) Changed();
        ImGui::SameLine();
        if (ImGui::Button("Sit on ground")) {
            lv::Vec3 lo, hi;
            if (lv::ModelBox(l, s.selected_model, lo, hi)) { m.y += -l.ground_y - lo.y; Changed(); }
        }
        ImGui::BeginDisabled(!m.solid);
        if (ImGui::Checkbox("Ledges", &m.ledges)) Changed();
        ImGui::EndDisabled();
        Hint("Ledges: you can hang from its edges, but the game then treats its whole footprint as solid up to its "
             "top. Untick for buildings to go into: only the mesh collides.");
        auto mesh = lv::ReadModel(lv::ModelPath(l, m));
        ImGui::Text("%zu triangles", mesh->faces.size());
        for (size_t k = 0; k < mesh->materials.size(); k++) {
            if (m.textures.size() < mesh->materials.size()) m.textures.resize(mesh->materials.size(), l.ground_texture);
            if (m.images.size() < mesh->materials.size()) m.images.resize(mesh->materials.size());
            ImGui::PushID(int(k));
            ImGui::TextDisabled("%s", mesh->materials[k].c_str());
            if (!m.images[k].empty()) {  // its own picture, packed into the level when saved
                ImGui::Text("Picture %s", U8(FromU8(m.images[k]).filename()).c_str());
                if (ImGui::Button("Use a stage texture")) { m.images[k].clear(); Changed(); }
            } else {
                ImGui::SetNextItemWidth(-1);
                if (TextureCombo("##material", m.textures[k])) Changed();
                if (ImGui::Button("Picture...")) {
                    const fs::path file = OpenFile(L"Pictures (*.png, *.jpg, *.bmp, *.tif, *.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0",
                                                   FromU8(Setting("levels_pictures")));
                    if (!file.empty()) {
                        SetSetting("levels_pictures", U8(file.parent_path()));
                        m.images[k] = U8(file);
                        Changed();
                    }
                }
            }
            ImGui::PopID();
        }
        Hint("Pictures (from the .mtl or chosen here) are packed into the level when it is saved: each "
             "replaces one of the stage's textures the level does not use, at that texture's size.");
        if (IconButton(ICON_CANCEL, "Remove model")) { l.models.erase(l.models.begin() + s.selected_model); s.selected_model = -1; Changed(); }
    }

    Heading("Enemies");
    static const lv::MissionEnemies kNone;
    const lv::StageInfo* on = StageByNumber(l.stage);
    const lv::MissionEnemies& mission = on ? lv::StageEnemies(*on) : kNone;
    if (mission.enemies.empty()) {
        Hint("This stage has no story enemies.");
    } else {
        if (ImGui::Checkbox("Own enemies", &l.own_enemies)) {
            if (l.own_enemies && l.enemies.empty()) l.enemies = mission.enemies;  // start from the mission's
            s.selected_enemy = -1;
            Changed();
        }
        if (ImGui::Checkbox("Show enemies", &s.show_enemies)) s.dirty_scene = true;
        if (!l.own_enemies) {
            Hint("The mission's enemies (dim orange) come as they are. Tick Own enemies to move, add or remove them.");
        } else {
            Hint("These replace the mission's for every character and layout. An enemy left where it was keeps its "
                 "patrol; moved or turned it stands guard. Removed ones are kept out of sight, as the mission may count them. "
                 "Names are model folders in m.con: EM enemies, BS bosses and story characters, NPC people.");
            if (IconButton(ICON_IMPORT, "Add enemy")) {
                lv::Enemy e = s.selected_enemy >= 0 && s.selected_enemy < int(l.enemies.size()) ? l.enemies[s.selected_enemy] : mission.enemies.back();
                ViewCentre(e.x, e.z);
                e.y = l.ground_y;
                e.source = -1;
                e.removed = false;
                l.enemies.push_back(e);
                s.selected_enemy = int(l.enemies.size()) - 1;
                Changed();
            }
            ImGui::SameLine();
            if (ImGui::Button("Mission's enemies")) { l.enemies = mission.enemies; s.selected_enemy = -1; Changed(); }
            for (int i = 0; i < int(l.enemies.size()); i++) {
                const lv::Enemy& e = l.enemies[i];
                char label[96];
                std::snprintf(label, sizeof(label), "%d  %s%s%s", i + 1, lv::EnemyKindName(e.kind).c_str(), e.source < 0 ? "  (new)" : "", e.removed ? "  (removed)" : "");
                ImGui::PushID(2000 + i);
                if (ImGui::Selectable(label, s.selected_enemy == i)) { s.selected_enemy = i; s.dirty_scene = true; }
                ImGui::PopID();
            }
            if (s.selected_enemy >= 0 && s.selected_enemy < int(l.enemies.size())) {
                lv::Enemy& e = l.enemies[s.selected_enemy];
                float pos[3] = {e.x, e.y, e.z};
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##enemy pos", pos, 5, -100000, 100000, "%.0f")) { e.x = pos[0]; e.y = pos[1]; e.z = pos[2]; Changed(); }
                Hint("x, height, z (the game's: smaller is higher).");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##enemy facing", &e.facing, -180, 360, "Facing %.0f deg")) Changed();
                if (ImGui::BeginCombo("##enemy kind", lv::EnemyKindName(e.kind).c_str())) {
                    std::map<int, int> kinds;
                    for (const auto& m : mission.enemies) kinds[m.kind]++;
                    for (auto& [kind, count] : kinds) {
                        const std::string name = lv::EnemyKindName(kind) + "  (" + std::to_string(count) + " in the mission)";
                        if (ImGui::Selectable(name.c_str(), kind == e.kind)) { e.kind = kind; if (e.source >= 0 && mission.enemies[e.source].kind != kind) e.source = -1; Changed(); }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Move here")) { ViewCentre(e.x, e.z); e.y = l.ground_y; Changed(); }
                ImGui::SameLine();
                if (ImGui::Button("Sit on ground")) { e.y = l.ground_y; Changed(); }
                if (e.source >= 0) {
                    ImGui::SameLine();
                    if (ImGui::Button("Mission's place")) {
                        const lv::Enemy& was = mission.enemies[e.source];
                        e.x = was.x; e.y = was.y; e.z = was.z; e.facing = was.facing; e.kind = was.kind;
                        Changed();
                    }
                }
                if (e.source >= 0) {  // the mission's: hidden, not deleted
                    if (IconButton(ICON_CANCEL, e.removed ? "Bring back" : "Remove enemy")) { e.removed = !e.removed; Changed(); }
                } else if (IconButton(ICON_CANCEL, "Remove enemy")) {
                    l.enemies.erase(l.enemies.begin() + s.selected_enemy);
                    s.selected_enemy = -1;
                    Changed();
                }
            }
        }
    }

    Heading("Items");
    static const lv::MissionItems kNoItems;
    const lv::MissionItems& mission_items = on ? lv::StageItems(*on) : kNoItems;
    if (mission_items.room == 0) {
        Hint("This stage has no items lying in its mission.");
    } else {
        if (ImGui::Checkbox("Own items", &l.own_items)) {
            if (l.own_items && l.items.empty()) l.items = mission_items.items;  // start from the mission's
            s.selected_item = -1;
            Changed();
        }
        if (ImGui::Checkbox("Show items", &s.show_items)) s.dirty_scene = true;
        if (!l.own_items) {
            Hint("The mission's items (dim green) lie where it puts them. Tick Own items to move, add or remove them.");
        } else {
            char room[160];
            std::snprintf(room, sizeof(room), "%d of %d: these replace the mission's for every character and layout. "
                          "Items 10049-10052 are the missions' own (stage 3's gate key is 10049).", int(l.items.size()), mission_items.room);
            Hint(room);
            ImGui::BeginDisabled(int(l.items.size()) >= mission_items.room);
            if (IconButton(ICON_IMPORT, "Add item")) {
                lv::Item item = s.selected_item >= 0 && s.selected_item < int(l.items.size()) ? l.items[s.selected_item]
                                : !mission_items.items.empty() ? mission_items.items.front() : lv::Item{};
                ViewCentre(item.x, item.z);
                item.y = l.ground_y;
                l.items.push_back(item);
                s.selected_item = int(l.items.size()) - 1;
                Changed();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Mission's items")) { l.items = mission_items.items; s.selected_item = -1; Changed(); }
            ImGui::SameLine();
            if (ImGui::Button("None")) { l.items.clear(); s.selected_item = -1; Changed(); }
            for (int i = 0; i < int(l.items.size()); i++) {
                char label[64];
                std::snprintf(label, sizeof(label), "%d  item %d%s", i + 1, l.items[i].id, l.items[i].id >= 10049 && l.items[i].id <= 10052 ? "  (mission's)" : "");
                ImGui::PushID(4000 + i);
                if (ImGui::Selectable(label, s.selected_item == i)) { s.selected_item = i; s.dirty_scene = true; }
                ImGui::PopID();
            }
            if (s.selected_item >= 0 && s.selected_item < int(l.items.size())) {
                lv::Item& item = l.items[s.selected_item];
                float pos[3] = {item.x, item.y, item.z};
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##item pos", pos, 5, -100000, 100000, "%.0f")) { item.x = pos[0]; item.y = pos[1]; item.z = pos[2]; Changed(); }
                Hint("x, height, z (the game's: smaller is higher).");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputInt("##item id", &item.id)) { item.id = std::clamp(item.id, 10000, 10053); Changed(); }
                Hint("The item's number (10000-10053); the mission's own are in the list under Mission's items.");
                if (ImGui::Button("Move here")) { ViewCentre(item.x, item.z); item.y = l.ground_y; Changed(); }
                ImGui::SameLine();
                if (ImGui::Button("Sit on ground")) { item.y = l.ground_y; Changed(); }
                if (IconButton(ICON_CANCEL, "Remove item")) { l.items.erase(l.items.begin() + s.selected_item); s.selected_item = -1; Changed(); }
            }
        }
    }

    Heading("Lights");
    static const std::vector<lv::Light> kNoLights;
    const std::vector<lv::Light>& stage_lights = on ? lv::StageLights(*on) : kNoLights;
    if (stage_lights.empty()) {
        Hint("This stage places no candles, lanterns or torches of its own.");
    } else {
        if (ImGui::Checkbox("Own lights", &l.own_lights)) {
            if (l.own_lights && l.lights.size() != stage_lights.size()) l.lights = stage_lights;  // start from the stage's
            s.selected_light = -1;
            Changed();
        }
        if (ImGui::Checkbox("Show lights", &s.show_lights)) s.dirty_scene = true;
        std::map<std::string, int> counts;
        for (const auto& light : stage_lights) counts[light.kind]++;
        std::string summary = lv::StageLabel(*on) + " has";
        for (auto& [kind, count] : counts) summary += " " + std::to_string(count) + " " + kind + (count > 1 ? "s," : ",");
        summary.back() = '.';
        summary += l.own_lights ? " Its code sets how many of each, so move them where you want them and remove the rest."
                                : " They stay where the stage had them (dim yellow). Tick Own lights to move or remove them.";
        Hint(summary.c_str());
        if (l.own_lights) {
            if (ImGui::Button("Remove all")) { for (auto& light : l.lights) light.removed = true; Changed(); }
            ImGui::SameLine();
            if (ImGui::Button("Stage's lights")) { l.lights = stage_lights; s.selected_light = -1; Changed(); }
            for (int i = 0; i < int(l.lights.size()); i++) {
                const lv::Light& light = l.lights[i];
                char label[96];
                std::snprintf(label, sizeof(label), "%s %d.%d%s", light.kind.c_str(), light.group + 1, light.index + 1, light.removed ? "  (removed)" : "");
                ImGui::PushID(3000 + i);
                if (ImGui::Selectable(label, s.selected_light == i)) { s.selected_light = i; s.dirty_scene = true; }
                ImGui::PopID();
            }
            if (s.selected_light >= 0 && s.selected_light < int(l.lights.size())) {
                lv::Light& light = l.lights[s.selected_light];
                float pos[3] = {light.x, light.y, light.z};
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##light pos", pos, 5, -100000, 100000, "%.0f")) { light.x = pos[0]; light.y = pos[1]; light.z = pos[2]; Changed(); }
                Hint("x, height, z (the game's: smaller is higher).");
                if (ImGui::Button("Move here")) { ViewCentre(light.x, light.z); light.y = l.ground_y - 150; light.removed = false; Changed(); }
                ImGui::SameLine();
                if (ImGui::Button("Stage's place")) {
                    for (const auto& was : stage_lights)
                        if (was.va == light.va) { light.x = was.x; light.y = was.y; light.z = was.z; }
                    Changed();
                }
                if (IconButton(ICON_CANCEL, light.removed ? "Bring back" : "Remove light")) { light.removed = !light.removed; Changed(); }
            }
        }
    }

    Heading("Save");
    const lv::StageInfo* st = StageByNumber(l.stage);
    if (s.budget.revision != s.revision) {
        std::vector<std::vector<uint8_t>> meshes;
        std::vector<uint8_t> collision;
        s.budget = {};
        s.budget.revision = s.revision;
        s.budget.fits = st && lv::BuildPack(l, *st, meshes, collision, s.budget.problem);
        for (const auto& m : meshes) s.budget.mesh += m.size();
        s.budget.blocks = int(meshes.size());
        s.budget.collision = collision.size();
        if (collision.size() >= 4) std::memcpy(&s.budget.objects, collision.data(), 4);
    }
    const bool fits = s.budget.fits;
    if (st && fits) {
        ImGui::Text("Mesh %s in %d of %d blocks", SizeText(s.budget.mesh).c_str(), std::max(1, s.budget.blocks), st->blocks);
        if (s.budget.mesh > st->blocks_bytes)
            ImGui::TextColored(ImVec4(1, .8f, .35f, 1), "More than the stage's own map (%s): the game may run short of memory.",
                               SizeText(st->blocks_bytes).c_str());
        ImGui::Text("Collision %u objects, %s", s.budget.objects, SizeText(s.budget.collision).c_str());
        if (s.budget.objects > st->collision_slots || s.budget.collision > st->boh_size)
            ImGui::TextColored(ImVec4(1, .8f, .35f, 1), "More than the stage's own (%u objects, %s): it is loaded from past the end of BOH.BIN.",
                               st->collision_slots, SizeText(st->boh_size).c_str());
    } else if (!s.budget.problem.empty()) {
        ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "%s", s.budget.problem.c_str());
    }
    ImGui::BeginDisabled(!fits);
    if (IconButton(ICON_SAVE, "Save level") || (fits && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
        std::string error;
        if (lv::SaveLevel(l, *st, error)) {
            s.unsaved = false;
            SetUnsaved("Levels", false);
            Notify("Saved " + l.name + ". In the game: F6 > Levels > Use, then start " + lv::StageLabel(*st) + "'s mission.");
        } else {
            Notify("Could not save: " + error, true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton(ICON_FOLDER_OPEN, "Levels folder")) {
        std::error_code ec;
        fs::create_directories(lv::LevelsDir(), ec);
        ShowInExplorer(lv::LevelsDir());
    }
    if (ImGui::Checkbox("Show the stage's own map", &s.show_map)) s.dirty_scene = true;
    if (ImGui::Checkbox("Show its collision", &s.show_collision)) s.dirty_scene = true;
}

// a ray against the level's boxes and models: the nearest hit selected (boxes 0.., models 1000..), or -1
int Pick(const float o[3], const float d[3]) {
    const lv::Level& l = S().level;
    int best = -1;
    float best_t = 1e30f;
    for (int i = 0; i < int(l.boxes.size() + l.models.size()); i++) {
        float lo[3], hi[3];
        if (i < int(l.boxes.size())) {
            const lv::Box& b = l.boxes[i];
            const float a[3] = {b.x - b.width / 2, -b.bottom, b.z - b.depth / 2}, c[3] = {b.x + b.width / 2, -b.bottom + b.height, b.z + b.depth / 2};
            std::memcpy(lo, a, sizeof(lo)); std::memcpy(hi, c, sizeof(hi));
        } else {
            lv::Vec3 a, c;
            if (!lv::ModelBox(l, i - l.boxes.size(), a, c)) continue;
            lo[0] = a.x; lo[1] = a.y; lo[2] = a.z; hi[0] = c.x; hi[1] = c.y; hi[2] = c.z;
        }
        float t0 = 0, t1 = 1e30f;
        bool hit = true;
        for (int k = 0; k < 3 && hit; k++) {
            if (std::fabs(d[k]) < 1e-6f) { hit = o[k] >= lo[k] && o[k] <= hi[k]; continue; }
            float a = (lo[k] - o[k]) / d[k], c = (hi[k] - o[k]) / d[k];
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a);
            t1 = std::min(t1, c);
            hit = t0 <= t1;
        }
        if (hit && t0 < best_t) { best_t = t0; best = i < int(l.boxes.size()) ? i : 1000 + i - int(l.boxes.size()); }
    }
    return best;
}

void ViewArea() {
    State& s = S();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (s.dirty_scene && (!s.loading || s.mode == kMake)) Rebuild();
    ImTextureID image = s.viewport.Render(int(size.x), int(size.y), s.camera);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (image) ImGui::Image(image, size);
    else ImGui::Dummy(size);
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("view", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        const bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (ImGui::IsMouseDown(ImGuiMouseButton_Right) && io.KeyShift);
        if (pan) s.camera.Pan(io.MouseDelta.x, io.MouseDelta.y);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) s.camera.Orbit(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel) s.camera.Zoom(io.MouseWheel);
        if (ImGui::IsKeyPressed(ImGuiKey_F)) FrameStage();
        if (s.mode == kMake && s.has_level && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float o[3], d[3];
            s.viewport.Ray(io.MousePos.x - at.x, io.MousePos.y - at.y, o, d);
            const int hit = Pick(o, d);
            s.selected_box = hit >= 0 && hit < 1000 ? hit : -1;
            s.selected_model = hit >= 1000 ? hit - 1000 : -1;
            s.dirty_scene = true;
        }
    }
    if (s.loading) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 12, at.y + 10), IM_COL32(220, 220, 220, 255), "Loading the stage...");
    }
}

}  // namespace

bool LevelsBusy(Activity& a) {
    if (!S().loading) return false;
    a.what = "Loading a stage";
    return true;
}

void LevelsPage() {
    State& s = S();
    if (!s.started) {
        s.started = true;
        s.stages = lv::Stages(s.error);
        const int remembered = std::atoi(Setting("levels_stage", "3").c_str());
        for (size_t i = 0; i < s.stages.size(); i++) if (s.stages[i].stage == remembered) s.view_stage = int(i);
        if (const lv::StageInfo* st = ShownStage()) { Load(*st); FrameStage(); }
        if (Automate().packs_mode == 1 && ShownStage()) {  // --capture of a new level
            s.level = lv::NewLevel(*ShownStage());
            s.revision++;
            s.level.boxes.push_back({s.level.spawn[0] + 600, s.level.spawn[2] + 300, s.level.ground_y, 800, 400, 300, s.level.ground_texture});
            std::string error;
            if (const char* obj = std::getenv("WB_LEVEL_TEST_OBJ"))  // a model in the capture too
                lv::AddModel(s.level, FromU8(obj), s.level.spawn[0] - 900, s.level.spawn[2] + 600, error);
            if (std::getenv("WB_LEVEL_TEST_ENEMIES")) {  // the mission's enemies as the level's own
                s.level.own_enemies = true;
                s.level.enemies = lv::StageEnemies(*ShownStage()).enemies;
            }
            if (const char* folder = std::getenv("WB_LEVEL_TEST_SAVE")) {  // and saved as levels\<folder>
                s.level.name = s.level.folder = folder;
                if (!lv::SaveLevel(s.level, *ShownStage(), error)) Notify("Save: " + error, true);
            }
            s.has_level = true;
            s.mode = kMake;
            SetLevelStage(s.level.stage);
            FrameStage();
        }
    }
    if (!s.error.empty() && s.stages.empty()) {
        Heading("Levels");
        ImGui::TextWrapped("%s", s.error.c_str());
        GameFolderMissing();
        return;
    }
    CountTextures();
    const float panel = ImGui::GetFontSize() * 20;
    ImGui::BeginChild("levels panel", ImVec2(panel, 0), ImGuiChildFlags_None);
    const char* modes[] = {"View a stage", "Make a level"};
    if (Segmented("levels mode", &s.mode, modes, 2)) {
        s.dirty_scene = true;
        if (s.mode == kMake && s.has_level) SetLevelStage(s.level.stage);
        FrameStage();
    }
    ImGui::Spacing();
    if (s.mode == kView) ViewPanel();
    else MakePanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("levels view", ImVec2(0, 0), ImGuiChildFlags_Border);
    ViewArea();
    ImGui::EndChild();
    // a loaded stage frames itself once
    static int framed = -1;
    if (!s.loading && s.loaded_stage >= 0 && s.loaded_stage != framed) {
        framed = s.loaded_stage;
        if (s.mode == kView) FrameStage();
        s.dirty_scene = true;
    }
}

}  // namespace wb
