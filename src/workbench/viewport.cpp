#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "viewport.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace wb {
namespace {

constexpr float kFov = 1.0f, kNear = 20, kFar = 200000;

const char kShader[] = R"(
cbuffer View : register(b0) { row_major float4x4 view_projection; float4 flags; };
Texture2D picture : register(t0);
SamplerState wrap : register(s0);
struct VIn { float3 pos : POSITION; float4 colour : COLOR; float2 uv : TEXCOORD; };
struct VOut { float4 pos : SV_Position; float4 colour : COLOR; float2 uv : TEXCOORD; float depth : DEPTH; };
VOut vs(VIn i) {
    VOut o;
    o.pos = mul(float4(i.pos, 1), view_projection);
    o.colour = i.colour;
    o.uv = i.uv;
    o.depth = o.pos.w;
    return o;
}
float4 ps(VOut i) : SV_Target {
    float4 c = i.colour;
    if (flags.x > 0) c *= picture.Sample(wrap, i.uv) * 2;   // the game's vertex colours are half bright
    float fog = saturate(i.depth / 60000);                   // far geometry fades to the background
    return float4(lerp(c.rgb, float3(0.11, 0.12, 0.14), fog * 0.35), 1);
}
)";

struct Vec { float x, y, z; };
Vec Sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec CrossV(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float DotV(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Norm(Vec a) { float l = std::sqrt(DotV(a, a)); return l > 0 ? Vec{a.x / l, a.y / l, a.z / l} : a; }

// the camera's eye and axes: forward looks at the target, y up
void Axes(const Camera& c, Vec& eye, Vec& right, Vec& up, Vec& forward) {
    forward = {std::cos(c.pitch) * std::sin(c.yaw), -std::sin(c.pitch), std::cos(c.pitch) * std::cos(c.yaw)};
    eye = {c.target[0] - forward.x * c.distance, c.target[1] - forward.y * c.distance, c.target[2] - forward.z * c.distance};
    right = Norm(CrossV({0, 1, 0}, forward));
    up = CrossV(forward, right);
}

}  // namespace

void Camera::Orbit(float dx, float dy) {
    yaw += dx * 0.008f;
    pitch = std::clamp(pitch + dy * 0.008f, -1.5f, 1.5f);
}
void Camera::Pan(float dx, float dy) {
    Vec eye, right, up, forward;
    Axes(*this, eye, right, up, forward);
    const float scale = distance * 0.0015f;
    for (int i = 0; i < 3; i++) target[i] += (&right.x)[i] * -dx * scale + (&up.x)[i] * dy * scale;
}
void Camera::Zoom(float wheel) { distance = std::clamp(distance * std::pow(0.85f, wheel), 100.0f, 150000.0f); }
void Camera::Frame(const float low[3], const float high[3]) {
    for (int i = 0; i < 3; i++) target[i] = (low[i] + high[i]) / 2;
    const float size = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2], 400.0f});
    distance = std::clamp(size * 0.9f, 300.0f, 150000.0f);
}

struct Viewport::Impl {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth_state;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11Texture2D> colour, depth;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth_view;
    ComPtr<ID3D11ShaderResourceView> view;
    int width = 0, height = 0;
    bool ready = false;
    struct Uploaded {
        ComPtr<ID3D11Buffer> buffer;
        UINT count = 0;
        std::shared_ptr<Picture> texture;
        bool lines = false;
    };
    std::vector<Uploaded> batches;
    Camera last;

    bool Init() {
        ID3D11Device* dev = Device();
        if (!dev) return false;
        ComPtr<ID3DBlob> vsb, psb, err;
        if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "viewport", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &err)) ||
            FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "viewport", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &err)))
            return false;
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        dev->CreateInputLayout(elements, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout);
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 80;
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cb, nullptr, &constants);
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;  // both sides: the game's strips alternate their winding
        rd.DepthClipEnable = TRUE;
        dev->CreateRasterizerState(&rd, &raster);
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dev->CreateDepthStencilState(&dd, &depth_state);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &sampler);
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bd, &blend);
        return vs && ps && layout && constants;
    }

    bool Resize(int w, int h) {
        if (w == width && h == height && target) return true;
        ID3D11Device* dev = Device();
        colour.Reset(); depth.Reset(); target.Reset(); depth_view.Reset(); view.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &colour))) return false;
        dev->CreateRenderTargetView(colour.Get(), nullptr, &target);
        dev->CreateShaderResourceView(colour.Get(), nullptr, &view);
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &depth))) return false;
        dev->CreateDepthStencilView(depth.Get(), nullptr, &depth_view);
        width = w;
        height = h;
        return target && view && depth_view;
    }
};

Viewport::Viewport() : impl_(std::make_unique<Impl>()) {}
Viewport::~Viewport() = default;

void Viewport::SetBatches(std::vector<DrawBatch> batches) {
    impl_->batches.clear();
    ID3D11Device* dev = Device();
    if (!dev) return;
    for (auto& b : batches) {
        if (b.vertices.empty()) continue;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(b.vertices.size() * sizeof(DrawVertex));
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{b.vertices.data(), 0, 0};
        Impl::Uploaded u;
        if (FAILED(dev->CreateBuffer(&bd, &data, &u.buffer))) continue;
        u.count = UINT(b.vertices.size());
        u.texture = std::move(b.texture);
        u.lines = b.lines;
        impl_->batches.push_back(std::move(u));
    }
}

ImTextureID Viewport::Render(int width, int height, const Camera& camera) {
    Impl& m = *impl_;
    width = std::max(width, 16);
    height = std::max(height, 16);
    if (!m.ready && !(m.ready = m.Init())) return ImTextureID{};
    if (!m.Resize(width, height)) return ImTextureID{};
    m.last = camera;
    ID3D11DeviceContext* ctx = Context();

    // the window's targets and viewport, put back afterwards
    ComPtr<ID3D11RenderTargetView> old_target;
    ComPtr<ID3D11DepthStencilView> old_depth;
    ctx->OMGetRenderTargets(1, &old_target, &old_depth);
    UINT old_count = 1;
    D3D11_VIEWPORT old_viewport{};
    ctx->RSGetViewports(&old_count, &old_viewport);

    const float clear[4] = {0.11f, 0.12f, 0.14f, 1};
    ctx->ClearRenderTargetView(m.target.Get(), clear);
    ctx->ClearDepthStencilView(m.depth_view.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    ID3D11RenderTargetView* rtv = m.target.Get();
    ctx->OMSetRenderTargets(1, &rtv, m.depth_view.Get());
    D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(m.raster.Get());
    ctx->OMSetDepthStencilState(m.depth_state.Get(), 0);
    ctx->OMSetBlendState(m.blend.Get(), nullptr, 0xFFFFFFFF);
    ctx->IASetInputLayout(m.layout.Get());
    ctx->VSSetShader(m.vs.Get(), nullptr, 0);
    ctx->PSSetShader(m.ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m.constants.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, m.constants.GetAddressOf());
    ctx->PSSetSamplers(0, 1, m.sampler.GetAddressOf());

    // view and projection, left-handed, multiplied as row vectors
    Vec eye, right, up, forward;
    Axes(camera, eye, right, up, forward);
    const float v[16] = {right.x, up.x, forward.x, 0, right.y, up.y, forward.y, 0, right.z, up.z, forward.z, 0,
                         -DotV(right, eye), -DotV(up, eye), -DotV(forward, eye), 1};
    const float ys = 1 / std::tan(kFov / 2), xs = ys * height / width, q = kFar / (kFar - kNear);
    const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, q, 1, 0, 0, -kNear * q, 0};
    float vpm[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            vpm[r * 4 + c] = 0;
            for (int k = 0; k < 4; k++) vpm[r * 4 + c] += v[r * 4 + k] * p[k * 4 + c];
        }
    for (const auto& b : m.batches) {
        D3D11_MAPPED_SUBRESOURCE map;
        if (FAILED(ctx->Map(m.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) continue;
        float flags[4] = {b.texture && b.texture->id ? 1.0f : 0.0f, 0, 0, 0};
        std::memcpy(map.pData, vpm, 64);
        std::memcpy(static_cast<uint8_t*>(map.pData) + 64, flags, 16);
        ctx->Unmap(m.constants.Get(), 0);
        ID3D11ShaderResourceView* srv = b.texture && b.texture->id ? reinterpret_cast<ID3D11ShaderResourceView*>(b.texture->id) : nullptr;
        ctx->PSSetShaderResources(0, 1, &srv);
        UINT stride = sizeof(DrawVertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, b.buffer.GetAddressOf(), &stride, &offset);
        ctx->IASetPrimitiveTopology(b.lines ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(b.count, 0);
    }
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ID3D11RenderTargetView* back = old_target.Get();
    ctx->OMSetRenderTargets(1, &back, old_depth.Get());
    ctx->RSSetViewports(1, &old_viewport);
    return reinterpret_cast<ImTextureID>(m.view.Get());
}

void Viewport::Ray(float px, float py, float origin[3], float direction[3]) const {
    const Impl& m = *impl_;
    Vec eye, right, up, forward;
    Axes(m.last, eye, right, up, forward);
    const float t = std::tan(kFov / 2), aspect = float(m.width) / std::max(1, m.height);
    const float nx = (px / std::max(1, m.width)) * 2 - 1, ny = 1 - (py / std::max(1, m.height)) * 2;
    Vec d = Norm({forward.x + right.x * nx * t * aspect + up.x * ny * t, forward.y + right.y * nx * t * aspect + up.y * ny * t,
                  forward.z + right.z * nx * t * aspect + up.z * ny * t});
    origin[0] = eye.x; origin[1] = eye.y; origin[2] = eye.z;
    direction[0] = d.x; direction[1] = d.y; direction[2] = d.z;
}

}  // namespace wb
