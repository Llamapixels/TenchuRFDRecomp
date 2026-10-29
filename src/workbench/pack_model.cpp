#include "pack_model.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace wb::pm {
namespace {
std::string U8(const fs::path& p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
float Look(const packs::Score& s) { return std::clamp(s.ncc - 1.5f * s.color, 0.f, 1.f); }
// how strongly to offer an image: its look (detail less colour difference: by eye this picks
// right where the game's verdict alone does not), less for an image another texture is sure of
// and for another shape, which the game stretches to fit
float Offer(const Candidate& c) { return Look(c.s) - (c.taken ? .15f : 0) - (c.other_shape ? .10f : 0); }
bool Better(const Candidate& a, const Candidate& b) {
    const bool sa = a.v == packs::Verdict::Accept, sb = b.v == packs::Verdict::Accept;
    if (sa != sb) return sa;
    const float oa = Offer(a), ob = Offer(b);
    return oa != ob ? oa > ob : a.s.ncc > b.s.ncc;
}
}  // namespace

std::string GroupOf(const std::string& file) {
    const size_t slash = file.find_last_of("\\/");
    std::string name = file.substr(slash == std::string::npos ? 0 : slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    const size_t dash = name.find('-');
    if (dash == 16 && std::count(name.begin(), name.end(), '-') == 2) return name.substr(0, dash);
    return name;
}

void Prepare(Model& m) {
    m.by_file.clear();
    m.groups.clear();
    for (size_t i = 0; i < m.index.size(); i++) {
        m.by_file[m.index[i].file] = int(i);
        m.groups[GroupOf(m.index[i].file)].push_back(int(i));
    }
}

std::vector<Candidate> Match(const Tex& t, const Model& m) {
    std::vector<Candidate> scored(m.index.size());
    std::vector<int> order;
    if (t.d.flat) return {};
    const float aspect = std::log(float(t.w) / float(t.h));
    for (size_t k = 0; k < m.index.size(); k++) {
        const packs::PackImage& img = m.index[k];
        if (!img.ok || img.d.flat) continue;
        Candidate c{int(k), packs::Compare(t.d, img.d)};
        c.other_shape = std::fabs(std::log(float(img.d.w) / float(img.d.h)) - aspect) > kAspect;
        if (c.other_shape && c.s.ncc < kOtherShape) continue;
        c.v = packs::Judge(c.s);
        if (c.other_shape && c.v == packs::Verdict::Accept) c.v = packs::Verdict::Review;  // the game only takes same-shaped ones
        if (!c.other_shape && c.v == packs::Verdict::None && c.s.ncc >= kRecolour) {
            c.v = packs::Verdict::Review;
            c.recolour = true;
        }
        scored[k] = c;
        order.push_back(int(k));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return Better(scored[a], scored[b]); });
    std::vector<Candidate> out;
    std::unordered_set<std::string> seen;
    for (int k : order) {
        if (out.size() >= kKeep) break;
        const std::string group = GroupOf(m.index[k].file);
        if (!seen.insert(group).second) continue;
        // the palette that fits best
        Candidate best = scored[k];
        if (auto g = m.groups.find(group); g != m.groups.end())
            for (int other : g->second)
                if (scored[other].image >= 0 && Better(scored[other], best)) best = scored[other];
        out.push_back(best);
    }
    return out;
}

int AutoImage(const Tex& t) { return !t.cands.empty() && t.cands[0].v == packs::Verdict::Accept ? t.cands[0].image : -1; }
int Chosen(const Tex& t) {
    if (t.choice == Choice::Pick) return t.pick;
    if (t.choice == Choice::Skip) return -1;
    return AutoImage(t);
}
Status StatusOf(const Tex& t) {
    if (t.choice == Choice::Pick) return kChosen;
    if (t.choice == Choice::Skip) return kSkipped;
    if (t.cands.empty()) return kNoMatch;
    if (t.cands[0].v == packs::Verdict::Accept) return kAuto;
    for (const Candidate& c : t.cands)
        if (c.v == packs::Verdict::Review) return kCheck;
    return kNoMatch;
}

int Suggestion(const Tex& t) { return t.cands.empty() ? -1 : 0; }  // candidates are in the order to offer

float Confidence(const Tex& t) {
    const int s = Suggestion(t);
    if (s < 0) return -2;
    const Candidate& c = t.cands[s];
    return Offer(c) + (t.mutual ? .05f : 0) - (c.other_shape ? .10f : 0);  // other shapes after the rest
}

void Rank(Model& m) {
    std::unordered_set<int> taken;
    for (const Tex& t : m.tex)
        if (const int a = AutoImage(t); a >= 0) taken.insert(a);
    for (Tex& t : m.tex) {
        const int own = AutoImage(t);
        for (Candidate& c : t.cands) c.taken = c.image != own && taken.count(c.image);
        if (own < 0) std::stable_sort(t.cands.begin(), t.cands.end(), Better);
    }
    // which unsure texture each suggested image likes best
    std::unordered_map<int, std::pair<float, size_t>> best;
    for (size_t i = 0; i < m.tex.size(); i++) {
        Tex& t = m.tex[i];
        t.mutual = false;
        if (AutoImage(t) >= 0) continue;
        const int s = Suggestion(t);
        if (s < 0) continue;
        auto& b = best[t.cands[s].image];
        if (Look(t.cands[s].s) > b.first || b.second == 0) b = {Look(t.cands[s].s), i + 1};
    }
    for (auto& [image, b] : best) m.tex[b.second - 1].mutual = true;
}

float Similarity(const packs::Score& s) { return Look(s); }

std::vector<Ranked> AllImages(const Tex& t, const Model& m) {
    std::vector<Ranked> out;
    const float aspect = t.h ? std::log(float(t.w) / float(t.h)) : 0;
    for (size_t k = 0; k < m.index.size(); k++) {
        const packs::PackImage& img = m.index[k];
        if (!img.ok) continue;
        out.push_back({int(k), packs::Compare(t.d, img.d), std::fabs(std::log(float(img.d.w) / float(img.d.h)) - aspect) > kAspect});
    }
    std::sort(out.begin(), out.end(), [](const Ranked& a, const Ranked& b) {
        const float x = Similarity(a.s), y = Similarity(b.s);
        return x != y ? x > y : a.s.ncc > b.s.ncc;
    });
    return out;
}

void Apply(Model& m, const packs::PackMatches& matches) {
    for (Tex& t : m.tex) {
        if (!t.key) continue;
        if (matches.skip.count(t.key)) {
            t.choice = Choice::Skip;
            continue;
        }
        auto use = matches.use.find(t.key);
        if (use == matches.use.end()) continue;
        auto f = m.by_file.find(use->second);
        if (f == m.by_file.end()) continue;  // the image left the pack
        if (f->second == AutoImage(t)) t.choice = Choice::Auto;
        else {
            t.choice = Choice::Pick;
            t.pick = f->second;
        }
    }
}

packs::PackMatches Collect(const Model& m) {
    packs::PackMatches out;
    for (const Tex& t : m.tex) {
        if (!t.key) continue;
        if (t.choice == Choice::Skip) out.skip.insert(t.key);
        else if (const int image = Chosen(t); image >= 0) out.use[t.key] = m.index[image].file;
    }
    return out;
}

int AddImage(Model& m, const fs::path& file) {
    std::error_code ec;
    packs::PackImage img;
    img.file = U8(file.lexically_relative(m.dir));
    if (img.file.empty() || img.file.rfind("..", 0) == 0) return -1;
    img.size = fs::file_size(file, ec);
    img.time = fs::last_write_time(file, ec).time_since_epoch().count();
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    img.ok = packs::ReadImage(file, px, w, h);
    if (!img.ok) return -1;
    img.d = packs::Describe(px.data(), w, h);
    if (auto f = m.by_file.find(img.file); f != m.by_file.end()) {  // replaced: same place, so choices keep pointing at it
        m.index[f->second] = std::move(img);
        return f->second;
    }
    m.index.push_back(std::move(img));
    Prepare(m);
    return int(m.index.size()) - 1;
}

}  // namespace wb::pm
