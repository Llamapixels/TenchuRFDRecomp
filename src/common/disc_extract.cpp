#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "port/connect_file.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// First start: the disc image the player picked is unpacked into extracted\ beside
// the game, like the Tenchu Z port, and every Media\*.con archive is unpacked next
// to itself into <name>.con.files\ so modders can open, edit and replace single
// files. Later starts run from extracted\ (game_files overlay) once .complete is
// there. Files already present are never overwritten, so a re-run keeps edits.
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kChunk = 4ull << 20;
constexpr uint64_t kSpaceNeeded = 5ull << 30;  // disc files plus unpacked archives, with room to spare

struct Progress {
    std::atomic<uint64_t> done{0}, total{1};
    std::atomic<bool> finished{false}, cancel{false}, ok{false};
    std::mutex lock;
    std::wstring current, error;
};
Progress g;

fs::path Root() { return app::ExeDir() / "extracted"; }

void Collect(const kernel::Disc::Entry& dir, const fs::path& prefix, std::vector<std::pair<const kernel::Disc::Entry*, fs::path>>& out) {
    for (const auto& e : dir.children) {
        fs::path p = prefix / fs::path(std::u8string(e.name.begin(), e.name.end()));
        if (e.dir) Collect(e, p, out);
        else out.emplace_back(&e, p);
    }
}

void SetCurrent(const fs::path& p) {
    std::lock_guard l(g.lock);
    g.current = p.wstring();
}

bool Fail(const std::wstring& what) {
    std::lock_guard l(g.lock);
    g.error = what;
    return false;
}

bool CopyFromDisc(const kernel::Disc& disc, const kernel::Disc::Entry& e, const fs::path& out) {
    std::error_code ec;
    if (fs::exists(out, ec) && fs::file_size(out, ec) == e.size) {
        g.done += e.size;
        return true;
    }
    fs::create_directories(out.parent_path(), ec);
    fs::path temp = out;
    temp += L".partial";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        std::vector<char> buffer(size_t(std::min<uint64_t>(kChunk, std::max<uint32_t>(e.size, 1))));
        for (uint64_t pos = 0; pos < e.size && f; pos += buffer.size()) {
            if (g.cancel) return false;
            uint32_t n = uint32_t(std::min<uint64_t>(buffer.size(), e.size - pos));
            if (disc.Read(e, pos, buffer.data(), n) != n) return Fail(L"Reading the disc image failed at " + out.filename().wstring());
            f.write(buffer.data(), n);
            g.done += n;
        }
        if (!f) return Fail(L"Writing " + out.wstring() + L" failed.");
    }
    fs::rename(temp, out, ec);
    return ec ? Fail(L"Saving " + out.wstring() + L" failed.") : true;
}

bool UnpackArchive(const fs::path& archive) {
    tenchu::con::Archive a;
    std::string error;
    auto source = tenchu::con::FileSource(archive);
    std::error_code ec;
    if (!tenchu::con::Parse(source, fs::file_size(archive, ec), a, error)) {
        LOGR("Extract: %s is not an archive this port reads (%s); left packed", archive.string().c_str(), error.c_str());
        return true;
    }
    fs::path folder = archive;
    folder += L".files";
    std::vector<uint8_t> data;
    for (const auto& e : a.entries) {
        if (g.cancel) return false;
        fs::path out = folder / tenchu::con::SafePath(e.name);
        if (fs::exists(out, ec)) {  // an existing file may be the player's edit
            g.done += e.size;
            continue;
        }
        if (!tenchu::con::ReadEntry(source, e, data)) return Fail(L"Reading " + archive.filename().wstring() + L" failed.");
        fs::create_directories(out.parent_path(), ec);
        fs::path temp = out;
        temp += L".partial";
        {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            if (!f) return Fail(L"Writing " + out.wstring() + L" failed.");
        }
        fs::rename(temp, out, ec);
        g.done += e.size;
    }
    // edits are files newer than this marker, or of another size than the archive's entry
    std::ofstream(folder / ".extracted") << archive.filename().string() << " " << a.entries.size() << " files\n";
    return true;
}

void Work(const kernel::Disc* disc) {
    std::vector<std::pair<const kernel::Disc::Entry*, fs::path>> files;
    Collect(*disc->Find(std::vector<std::string>{}), {}, files);
    uint64_t total = 0;
    for (auto& [e, p] : files) {
        total += e->size;
        if (_wcsicmp(p.extension().c_str(), L".con") == 0) total += e->size;  // unpacked once more
    }
    g.total = std::max<uint64_t>(total, 1);
    bool ok = true;
    for (auto& [e, p] : files) {
        if (!ok || g.cancel) break;
        SetCurrent(p);
        ok = CopyFromDisc(*disc, *e, Root() / p);
    }
    for (auto& [e, p] : files) {
        if (!ok || g.cancel) break;
        if (_wcsicmp(p.extension().c_str(), L".con")) continue;
        SetCurrent(p);
        ok = UnpackArchive(Root() / p);
    }
    if (ok && !g.cancel) {
        std::ofstream(Root() / ".complete") << "Unpacked from the game disc. Delete this folder to unpack it again.\n";
        g.ok = true;
    }
    g.finished = true;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (g.finished) DestroyWindow(hwnd);
            else InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_CLOSE:
            if (MessageBoxW(hwnd, L"Stop unpacking? The game will run from the disc image this time and carry on unpacking next time.",
                            app::kGame.short_title, MB_YESNO | MB_ICONQUESTION) == IDYES)
                g.cancel = true;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT r;
            GetClientRect(hwnd, &r);
            FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(220, 228, 210));
            HFONT font = CreateFontW(-MulDiv(11, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, font);
            const double share = double(g.done) / double(g.total);
            std::wstring current;
            {
                std::lock_guard l(g.lock);
                current = g.current;
            }
            std::wstring text = L"Unpacking the game files for modding (first start only)\n\n" +
                                std::to_wstring(int(share * 100)) + L"%   " + std::to_wstring(g.done / (1 << 20)) + L" of " +
                                std::to_wstring(g.total / (1 << 20)) + L" MB\n" + current;
            RECT t = r;
            InflateRect(&t, -24, -20);
            DrawTextW(dc, text.c_str(), -1, &t, DT_LEFT | DT_WORDBREAK);
            RECT bar{t.left, r.bottom - 44, t.right, r.bottom - 28};
            HBRUSH empty = CreateSolidBrush(RGB(30, 36, 28)), full = CreateSolidBrush(RGB(120, 160, 90));
            FillRect(dc, &bar, empty);
            bar.right = bar.left + LONG((bar.right - bar.left) * share);
            FillRect(dc, &bar, full);
            DeleteObject(empty);
            DeleteObject(full);
            SelectObject(dc, old);
            DeleteObject(font);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

// True when extracted\ is complete. Uses the disc image the SDK just mounted.
bool TenchuExtractDisc() {
    std::error_code ec;
    if (fs::exists(Root() / ".complete", ec)) return true;
    if (GetEnvironmentVariableW(L"TENCHU_GPU_SELFTEST", nullptr, 0) || GetEnvironmentVariableW(L"TENCHU_NO_EXTRACT", nullptr, 0)) return false;
    const kernel::Disc* disc = kernel::MountedDisc();
    if (!disc) return false;
    ULARGE_INTEGER free{};
    fs::create_directories(Root(), ec);
    if (GetDiskFreeSpaceExW(Root().c_str(), &free, nullptr, nullptr) && free.QuadPart < kSpaceNeeded) {
        MessageBoxW(nullptr, L"Unpacking the game files for modding needs about 5 GB of free space on this drive. "
                             L"The game will run from the disc image until there is room.",
                    app::kGame.short_title, MB_ICONWARNING);
        return false;
    }
    LOGR("Extract: unpacking the disc into %s", Root().string().c_str());
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TenchuRFDExtract";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    const int w = 620, h = 200;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, app::kGame.title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                                nullptr, nullptr, wc.hInstance, nullptr);
    std::thread worker(Work, disc);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetTimer(hwnd, 1, 100, nullptr);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    worker.join();
    if (!g.ok) {
        std::wstring error;
        {
            std::lock_guard l(g.lock);
            error = g.error;
        }
        LOGR("Extract: %s", g.cancel ? "stopped by the player" : "failed");
        if (!error.empty())
            MessageBoxW(nullptr, (error + L"\n\nThe game will run from the disc image and try again next time.").c_str(),
                        app::kGame.short_title, MB_ICONWARNING);
        return false;
    }
    LOGR("Extract: done, %llu MB", static_cast<unsigned long long>(g.done / (1 << 20)));
    return true;
}
