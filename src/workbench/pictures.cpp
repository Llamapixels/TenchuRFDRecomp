#include "ui.h"
#include "images.h"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

// Pictures load on worker threads, newest request first, so scrolling and moving through
// lists stays smooth; the UI thread uploads what finished and keeps the recently drawn.
namespace wb {
namespace {

struct Request {
    uint64_t key;
    uint32_t side;
    LoadPicture load;
};
struct Done {
    uint64_t key;
    uint32_t generation;
    Bitmap bitmap;
    bool ok;
};
struct Entry {
    std::shared_ptr<Picture> picture;
    uint64_t used = 0;
};

class Loader {
public:
    ~Loader() { Stop(); }
    std::shared_ptr<Picture> Get(uint64_t key, uint32_t side, const LoadPicture& load) {
        Pump();
        if (auto it = cache_.find(key); it != cache_.end()) {
            it->second.used = frame_;
            return it->second.picture;
        }
        Want(key, side, load);
        return nullptr;
    }
    void Want(uint64_t key, uint32_t side, const LoadPicture& load) {
        if (cache_.count(key)) return;
        std::lock_guard l(lock_);
        if (!queued_.insert(key).second) return;
        requests_.push_back({key, side, load});
        if (requests_.size() > 600) {  // old wishes nobody looks at any more
            queued_.erase(requests_.front().key);
            requests_.erase(requests_.begin());
        }
        if (threads_.empty())
            for (unsigned i = 0; i < std::clamp(std::thread::hardware_concurrency() / 2, 2u, 6u); i++) threads_.emplace_back([this] { Work(); });
        wake_.notify_one();
    }
    void Forget(uint64_t key) { cache_.erase(key); }
    bool Idle() {
        std::lock_guard l(lock_);
        return requests_.empty() && done_.empty() && busy_ == 0;
    }
    void Clear() {
        std::unique_lock l(lock_);
        generation_++;
        requests_.clear();
        queued_.clear();
        done_.clear();
        idle_.wait(l, [&] { return busy_ == 0; });
        cache_.clear();
    }
    void Stop() {
        {
            std::lock_guard l(lock_);
            stop_ = true;
            requests_.clear();
            wake_.notify_all();
        }
        for (auto& t : threads_) t.join();
        threads_.clear();
        cache_.clear();
    }

private:
    // once a frame: upload what finished (a few at a time) and drop what was not drawn lately
    void Pump() {
        const int f = ImGui::GetFrameCount();
        if (f == pumped_) return;
        pumped_ = f;
        frame_++;
        std::vector<Done> done;
        {
            std::lock_guard l(lock_);
            const size_t n = std::min<size_t>(done_.size(), 24);
            done.assign(std::make_move_iterator(done_.begin()), std::make_move_iterator(done_.begin() + n));
            done_.erase(done_.begin(), done_.begin() + n);
        }
        for (Done& d : done) {
            if (d.generation != generation_) continue;
            cache_[d.key] = {d.ok ? Upload(d.bitmap.px.data(), d.bitmap.w, d.bitmap.h) : std::make_shared<Picture>(), frame_};
        }
        if (cache_.size() > 700) {
            std::vector<std::pair<uint64_t, uint64_t>> age;
            for (auto& [k, e] : cache_) age.emplace_back(e.used, k);
            std::sort(age.begin(), age.end());
            for (size_t i = 0; i < cache_.size() - 500; i++) cache_.erase(age[i].second);
        }
    }
    void Work() {
        std::unique_lock l(lock_);
        for (;;) {
            wake_.wait(l, [&] { return stop_ || !requests_.empty(); });
            if (stop_) return;
            Request r = std::move(requests_.back());
            requests_.pop_back();
            const uint32_t generation = generation_;
            busy_++;
            l.unlock();
            Done d{r.key, generation, {}, false};
            d.ok = r.load && r.load(d.bitmap) && d.bitmap.w && d.bitmap.h && d.bitmap.px.size() >= size_t(d.bitmap.w) * d.bitmap.h * 4;
            if (d.ok && (d.bitmap.w > r.side || d.bitmap.h > r.side)) {
                const float scale = float(r.side) / float(std::max(d.bitmap.w, d.bitmap.h));
                const uint32_t w = std::max(1u, uint32_t(d.bitmap.w * scale)), h = std::max(1u, uint32_t(d.bitmap.h * scale));
                d.bitmap.px = Resize(d.bitmap.px.data(), d.bitmap.w, d.bitmap.h, w, h);
                d.bitmap.w = w;
                d.bitmap.h = h;
            }
            l.lock();
            busy_--;
            queued_.erase(r.key);
            if (generation == generation_) done_.push_back(std::move(d));
            idle_.notify_all();
        }
    }

    std::mutex lock_;
    std::condition_variable wake_, idle_;
    std::vector<Request> requests_;
    std::set<uint64_t> queued_;
    std::vector<Done> done_;
    std::vector<std::thread> threads_;
    uint32_t generation_ = 0, busy_ = 0;
    bool stop_ = false;
    std::unordered_map<uint64_t, Entry> cache_;  // UI thread only
    uint64_t frame_ = 0;
    int pumped_ = -1;
};

Loader& Shared() {
    static Loader loader;
    return loader;
}
}  // namespace

uint64_t PictureKey(const std::string& text, uint32_t side) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) h = (h ^ c) * 1099511628211ull;
    return (h ^ side) * 1099511628211ull;
}
std::shared_ptr<Picture> GetPicture(uint64_t key, uint32_t side, const LoadPicture& load) { return Shared().Get(key, side, load); }
void PrefetchPicture(uint64_t key, uint32_t side, const LoadPicture& load) { Shared().Want(key, side, load); }
void ForgetPicture(uint64_t key) { Shared().Forget(key); }
void ForgetPictures() { Shared().Clear(); }
bool PicturesIdle() { return Shared().Idle(); }

}  // namespace wb
