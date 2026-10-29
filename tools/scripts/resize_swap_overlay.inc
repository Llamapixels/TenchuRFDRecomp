bool D3D12Backend::ResizeSwap() {
    RECT rc{};
    if (!GetClientRect(static_cast<HWND>(app::Window()), &rc)) return false;
    if (rc.right <= rc.left || rc.bottom <= rc.top) return false;
    const uint32_t w = uint32_t(rc.right - rc.left), h = uint32_t(rc.bottom - rc.top);
    bool complete = true;
    for (const auto& b : bb_) complete = complete && bool(b);
    if (w == bb_w_ && h == bb_h_ && complete) return true;
    Flush();
    for (auto& f : frames_) if (f.fence) WaitFence(f.fence);
    for (auto& b : bb_) b.Reset();
    const HRESULT hr = swap_->ResizeBuffers(kFrames, w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
        tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        LOGE("Swapchain resize %ux%u failed: %08X; deferring presentation", w, h, unsigned(hr));
        // On failure DXGI retains its old buffers. Reacquire them but do not
        // claim the new size succeeded, and never draw through null resources.
        CreateBackBufferViews();
        return false;
    }
    if (!CreateBackBufferViews()) return false;
    bb_w_ = w; bb_h_ = h;
    LOGR("Swapchain resized: %ux%u", w, h);
    return true;
}

