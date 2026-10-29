"""Checked build overlays for SDK runtime sources: rendering, texture packs and mods."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
sdk=root/'tools/XboxXBERecomp/runtime'
out=Path(sys.argv[1])
def write(name, edits, prefix='', suffix=''):
    text=(sdk/name).read_text()
    for old,new in edits:
        assert text.count(old)==1, f'Upstream graphics source changed: {name}: {old[:70]}'
        text=text.replace(old,new)
    text=prefix+text+suffix
    p=out/name
    p.parent.mkdir(parents=True,exist_ok=True)
    if not p.exists() or p.read_text()!=text: p.write_text(text)
write('core/diag.h', [('return v ? std::filesystem::path(v) : std::filesystem::path();', 'return v && *v ? std::filesystem::path(v) : std::filesystem::path("render_captures");')])
write('gpu/state.h', [('struct Surface {','struct Surface {\n    float tenchu_scale_x = 1, tenchu_scale_y = 1;\n    bool tenchu_backbuffer = false;\n    uint32_t tenchu_samples_x = 1, tenchu_samples_y = 1;')])
write('gpu/shaders.h', [('    float zparams[4];','    float tenchu_raster[4]; // XYRHW backbuffer scale, independently of target allocation\n    float zparams[4];')])
write('gpu/shaders.cpp', [
    # Depth precision: the title's projection gives z = w - near, so z and w are
    # both about the distance. Subtracting them after the transform lost most of
    # the reversed depth and coplanar far surfaces z-fought (black wall patches).
    # w - z is taken in one dot product of the column difference instead.
    ('        s += "    float4 cp = mul(ip, wvp);\\n";',
     '        s += "    precise float4 cp = mul(ip, wvp);\\n";\n'
     '        s += "    precise float tenchu_wz = dot(ip, wvp._m03_m13_m23_m33 - wvp._m02_m12_m22_m32);\\n";'),
    ('    return w - z - zparams.z * w;\n}\n',
     '    return w - z - zparams.z * w;\n}\n'
     'float TenchuReversedClip(float wz, float w) {\n'
     '    if (zparams.x != 0) return zparams.x * (zparams.y - (w + zparams.z));\n'
     '    return wz - zparams.z * w;\n}\n'),
    ('InsertAfter(s, "    o.pos = cp;\\n", "    o.pos.z = ReversedClip(cp.z, cp.w);\\n");',
     'InsertAfter(s, "    o.pos = cp;\\n", "    o.pos.z = TenchuReversedClip(tenchu_wz, cp.w);\\n");'),
    ('    float4 zparams;', '    float4 tenchu_raster;\n    float4 zparams;'),
    ('float4(rhw.xy, rhw.z / surf.z, w)', 'float4(rhw.xy * tenchu_raster.xy, rhw.z / surf.z, w)'),
    ('    q.x = p.x * 2 / surf.x - 1;', '''    // Center screen overlays inside the active raster area, not the unused
    // remainder of a half/three-quarter-sized movie backbuffer.
    float tenchu_center_x = surf.x * tenchu_raster.x * 0.5;
    // Solid black screen masks (cutscene bars/fades) cover the complete raster.
    // They are not proportion-preserved HUD sprites. Textured black pixels
    // must retain the normal HUD/movie correction.
    float tenchu_hud_x = tenchu_raster.w != 0 && all(oD0.rgb == 0) ? 1 : tenchu_raster.z;
    q.x = ((p.x - tenchu_center_x) * tenchu_hud_x + tenchu_center_x) * 2 / surf.x - 1;'''),
])
write('xdk/d3d8.cpp', [
    ('std::array<uint32_t, 3> g_buffers{};', 'float tenchu_backbuffer_x = 1, tenchu_backbuffer_y = 1;\nuint32_t tenchu_samples_x = 1, tenchu_samples_y = 1;\nstd::array<uint32_t, 3> g_buffers{};'),
    ('    s.header = header;', '''    s.header = header;
    if (header && std::find(g_buffers.begin(), g_buffers.end(), header) != g_buffers.end()) {
        s.tenchu_backbuffer = true;
        s.tenchu_scale_x = tenchu_backbuffer_x;
        s.tenchu_scale_y = tenchu_backbuffer_y;
        s.tenchu_samples_x = tenchu_samples_x;
        s.tenchu_samples_y = tenchu_samples_y;
    }'''),
    ('    g_state.zs = SurfaceOf(zs);', '''    g_state.zs = SurfaceOf(zs);
    const float tenchu_sx = g_state.rt.tenchu_scale_x, tenchu_sy = g_state.rt.tenchu_scale_y;
    std::memcpy(HostPtr(kDevice + kDevSuperX), &tenchu_sx, 4);
    std::memcpy(HostPtr(kDevice + kDevSuperY), &tenchu_sy, 4);
    UpdateViewportConstants();'''),
    ('    float hw = v.w * 0.5f, hh = v.h * 0.5f;', '    const float bx = g_state.rt.tenchu_scale_x, by = g_state.rt.tenchu_scale_y;\n    float hw = v.w * 0.5f * bx, hh = v.h * 0.5f * by;'),
    ('{v.x + g_state.ss_offset_x + hw, v.y + g_state.ss_offset_y + hh, v.min_z * g_state.zscale, 0}', '{(v.x + g_state.ss_offset_x) * bx + hw, (v.y + g_state.ss_offset_y) * by + hh, v.min_z * g_state.zscale, 0}'),
    ('void SetFrameBuffers(const char* caller, uint32_t pp) {', 'void SetFrameBuffers(const char* caller, uint32_t pp) {\n    tenchu_backbuffer_x = tenchu_backbuffer_y = 1;'),
    ('    LOGI("d3d: %s %ux%u format', '    LOGR("d3d: %s %ux%u format'),
    ('    uint32_t color_fmt = gpu::LinearColorFormat(fmt);', '''    const uint32_t tenchu_mode=R32(pp+16);
    tenchu_samples_x=std::clamp((tenchu_mode>>4)&15u,1u,4u);
    tenchu_samples_y=std::clamp(tenchu_mode&15u,1u,4u);
    LOGR("Render sampling: presentation multisample mode %08X, grid %ux%u",tenchu_mode,tenchu_samples_x,tenchu_samples_y);
    uint32_t color_fmt = gpu::LinearColorFormat(fmt);'''),
], suffix='''
// Narrow bridge from this title's SetBackBufferScale hook into the HLE device.
uint32_t TenchuSetBackbufferScale(float x, float y) {
    using namespace d3d;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (R32(kDevicePtr) != kDevice) return 0x80004005u;
    tenchu_backbuffer_x = x;
    tenchu_backbuffer_y = y;
    std::memcpy(HostPtr(kDevice + XDK_CDevice_BackBufferScale), &x, 4);
    std::memcpy(HostPtr(kDevice + XDK_CDevice_BackBufferScale + 4), &y, 4);
    g_state.rt = SurfaceOf(Dev(kDevRenderTarget));
    const float sx = g_state.rt.tenchu_scale_x, sy = g_state.rt.tenchu_scale_y;
    std::memcpy(HostPtr(kDevice + kDevSuperX), &sx, 4);
    std::memcpy(HostPtr(kDevice + kDevSuperY), &sy, 4);
    UpdateViewportConstants();
    return 0;
}
''')
# Limit edits to the named functions rather than similar copy/clear code.
text=(sdk/'gpu/d3d12.cpp').read_text()
def edit(old,new):
    global text
    assert text.count(old)==1,f'Upstream D3D12 source changed: {old[:80]}'
    text=text.replace(old,new)
edit('void D3D12Backend::UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages,', r'''// A scrolling screen-space backdrop spans at least a viewport horizontally,
// at zero depth with reciprocal W=1. It must fill the raster, not follow HUD
// proportions. Use geometry, not texture addresses or draw order.
bool TenchuScreenBackdrop(const Assembled& a, const State& s) {
    if (!a.pretransformed || a.vertex_count < 4 || a.vertex_count > 6 || !(a.regs & 1)) return false;
    const auto& e=a.elements[0];
    if(e.format != DXGI_FORMAT_R32G32B32A32_FLOAT || e.buffer >= a.buffer_count) return false;
    const auto& b=a.buffers[e.buffer];
    if(!b.src || b.stride < e.offset+16 || uint64_t(a.vertex_count-1)*b.stride+e.offset+16>b.bytes) return false;
    float loX=1e30f,hiX=-1e30f,loY=1e30f,hiY=-1e30f;
    for(uint32_t i=0;i<a.vertex_count;++i){
        float v[4];std::memcpy(v,b.src+i*b.stride+e.offset,16);
        if(!std::isfinite(v[0])||!std::isfinite(v[1])||v[2]!=0.f||v[3]!=1.f)return false;
        loX=std::min(loX,v[0]);hiX=std::max(hiX,v[0]);
        loY=std::min(loY,v[1]);hiY=std::max(hiY,v[1]);
    }
    return hiX-loX >= float(s.viewport.w)-.5f && hiY-loY >= float(s.viewport.h)*.5f;
}

void D3D12Backend::UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages,''')
edit('    FillTextureConstants(s, sh, stages, c);', '''    FillTextureConstants(s, sh, stages, c);
    c.tenchu_raster[2] = tenchu::wide::HudScale(s.rt.tenchu_backbuffer, !sh.prog && !sh.fixed_transform,
        stages.rt[0] || stages.rt[1] || stages.rt[2] || stages.rt[3]);
    if (!k.zen && TenchuScreenBackdrop(asm_,s)) c.tenchu_raster[2]=1.f;
    c.tenchu_raster[3] = s.rt.tenchu_backbuffer && !sh.prog && !sh.fixed_transform &&
        !s.textures[0] && !s.textures[1] && !s.textures[2] && !s.textures[3] ? 1.f : 0.f;''')
edit('    c.surf[2] = 1.0f / s.zscale;', '    c.surf[2] = 1.0f / s.zscale;\n    c.tenchu_raster[0] = s.rt.tenchu_scale_x;\n    c.tenchu_raster[1] = s.rt.tenchu_scale_y;')
edit('    t.h = std::max(1u, uint32_t(std::lround(t.gh * sc)));','    t.h = std::max(1u, uint32_t(std::lround(t.gh * sc)));\n    if (tenchu::GraphicsTestEnabled()) LOGR("GPU target allocation: %ux%u -> %ux%u",t.gw,t.gh,t.w,t.h);')
edit('    void CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state, Target& dst_t,\n                    const CopyList& l);','    void CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state, Target& dst_t,\n                    const CopyList& l, float src_x = 1, float src_y = 1, float dst_x = 1, float dst_y = 1);')
edit('void D3D12Backend::CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state,\n                              Target& dst_t, const CopyList& l) {','void D3D12Backend::CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state,\n                              Target& dst_t, const CopyList& l, float src_x, float src_y, float dst_x, float dst_y) {')
start=text.index('void D3D12Backend::CopyByDraw(')
end=text.index('// one CopyTextureRegion per rect',start)
part=text[start:end]
part=part.replace('r.dx * dst_scale, r.dy * dst_scale, (r.x1 - r.x0) * dst_scale, (r.y1 - r.y0) * dst_scale',
                  'r.dx * dst_scale * dst_x, r.dy * dst_scale * dst_y, (r.x1 - r.x0) * dst_scale * dst_x, (r.y1 - r.y0) * dst_scale * dst_y')
part=part.replace('r.x0 / src_t.gw, r.y0 / src_t.gh, (r.x1 - r.x0) / src_t.gw, (r.y1 - r.y0) / src_t.gh',
                  'r.x0 * src_x / src_t.gw, r.y0 * src_y / src_t.gh, (r.x1 - r.x0) * src_x / src_t.gw, (r.y1 - r.y0) * src_y / src_t.gh')
part=part.replace('        uint32_t taps =', '''        if (tenchu::GraphicsTestEnabled()) LOGR("Copy geometry: src %ux%u dst %ux%u scale %.2f %.2f -> %.2f %.2f viewport %.1f %.1f uv %.2f %.2f",src_t.gw,src_t.gh,dst_t.gw,dst_t.gh,double(src_x),double(src_y),double(dst_x),double(dst_y),double(vp.Width),double(vp.Height),double(uv[2]),double(uv[3]));
        uint32_t taps =''')
text=text[:start]+part+text[end:]
edit('    if (!src_t || !src_t->drawn) {', '''    const bool tenchu_scaled_copy = src.tenchu_scale_x != 1 || src.tenchu_scale_y != 1 || dst.tenchu_scale_x != 1 || dst.tenchu_scale_y != 1 || src.tenchu_samples_x != 1 || src.tenchu_samples_y != 1;
    if (!src_t || (!src_t->drawn && !tenchu_scaled_copy)) {''')
edit('    Resolve(*src_t);\n    Target* dst_t = EnsureTarget(dst, false);','    if (!src_t->drawn && LatchCpuWrite(*src_t)) UploadTarget(*src_t, src);\n    Resolve(*src_t);\n    Target* dst_t = EnsureTarget(dst, false);')
edit('    if (src_t->fmt != dst_t->fmt) {\n        // formats differ: drawn across\n        CopyByDraw(*src_t, src_t->res.Get(), src_t->state, *dst_t, l);',
'''    const bool tenchu_raw_sample_layout = !count && !src_rects && dst.tenchu_samples_x == 1 && dst.tenchu_samples_y == 1 &&
        (src.tenchu_samples_x != 1 || src.tenchu_samples_y != 1) &&
        dst_t->gw == src_t->gw * src.tenchu_samples_x && dst_t->gh == src_t->gh * src.tenchu_samples_y;
    const float tenchu_dx = tenchu_raw_sample_layout ? src.tenchu_scale_x * src.tenchu_samples_x : dst.tenchu_scale_x;
    const float tenchu_dy = tenchu_raw_sample_layout ? src.tenchu_scale_y * src.tenchu_samples_y : dst.tenchu_scale_y;
    if (src_t->fmt != dst_t->fmt || tenchu_scaled_copy) {
        // Copy logical pixels, resampling scaled active regions rather than
        // copying the unused physical backbuffer edges into screen effects.
        CopyByDraw(*src_t, src_t->res.Get(), src_t->state, *dst_t, l,
                   src.tenchu_scale_x, src.tenchu_scale_y, tenchu_dx, tenchu_dy);''')
edit('                   src.tenchu_scale_x, src.tenchu_scale_y, tenchu_dx, tenchu_dy);\n        return;', '''                   src.tenchu_scale_x, src.tenchu_scale_y, tenchu_dx, tenchu_dy);
        if (cap_) {
            char name[48];
            snprintf(name,sizeof(name),"%04u_scaled_copy_%08X",cap_i_,dst_t->data);
            CapSave(*dst_t,name);
        }
        return;''')
edit('    if (count <= kQuadVertices) CaptureInputs(s, a, c);', '    CaptureInputs(s, a, c);')
edit('    fprintf(cap_, "\\n     verts");', '''    fprintf(cap_, "\\n     viewport %u %u %u %u raster %.4f %.4f",s.viewport.x,s.viewport.y,s.viewport.w,s.viewport.h,double(s.rt.tenchu_scale_x),double(s.rt.tenchu_scale_y));
    fprintf(cap_,"\\n     depth_constants %.9g %.9g %.9g %.9g zscale %.9g",double(c.zparams[0]),double(c.zparams[1]),double(c.zparams[2]),double(c.zparams[3]),double(s.zscale));
    for(unsigned row=0;row<4;++row)
        fprintf(cap_,"\\n     wvp%u %.9g %.9g %.9g %.9g",row,double(c.wvp[row*4]),double(c.wvp[row*4+1]),double(c.wvp[row*4+2]),double(c.wvp[row*4+3]));
    for (unsigned stage=0;stage<4;++stage)
        fprintf(cap_,"\\n     texsize%u %.4f %.4f %.4f %.4f",stage,double(c.texsize[stage][0]),double(c.texsize[stage][1]),double(c.texsize[stage][2]),double(c.texsize[stage][3]));
    for (unsigned stage=0;stage<4;++stage) {
        const uint32_t ttf=s.tss[stage][kTssTextureTransformFlags],tci=s.tss[stage][kTssTexCoordIndex];
        if(!ttf&&!(tci>>16))continue;
        const float* m=s.transforms[kTsTexture0+stage].data();
        fprintf(cap_,"\\n     texxform%u flags %X index %X",stage,ttf,tci);
        for(unsigned row=0;row<4;++row)fprintf(cap_," | %.6g %.6g %.6g %.6g",double(m[row*4]),double(m[row*4+1]),double(m[row*4+2]),double(m[row*4+3]));
    }
    {
        for(unsigned b=0;b<a.buffer_count;++b) {
            const auto& vb=a.buffers[b];
            if(!vb.src||!vb.stride)continue;
            const size_t bytes=std::min<size_t>(vb.bytes,16u*1024u*1024u);
            char name[64];snprintf(name,sizeof(name),"draw_%04u_vb%u.bin",cap_i_?cap_i_-1:0,b);
            std::ofstream raw(cap_dir_/name,std::ios::binary);
            raw.write(reinterpret_cast<const char*>(vb.src),bytes);
            fprintf(cap_,"\\n     raw %s stride %u bytes %zu",name,vb.stride,bytes);
        }
        for(unsigned r=0;r<16;++r)if(a.regs&(1u<<r)) {
            const auto& element=a.elements[r];
            fprintf(cap_,"\\n     input v%u buffer %u offset %u format %u",r,unsigned(element.buffer),unsigned(element.offset),unsigned(element.format));
        }
    }
    if(a.index_src && a.index_count){
        char name[64];snprintf(name,sizeof(name),"draw_%04u_indices.bin",cap_i_?cap_i_-1:0);
        const size_t bytes=std::min<size_t>(size_t(a.index_count)*(a.index16?2:4),16u*1024u*1024u);
        std::ofstream indices(cap_dir_/name,std::ios::binary);
        indices.write(reinterpret_cast<const char*>(a.index_src),bytes);
        fprintf(cap_,"\\n     indices %s count %u bits %u base %d",name,a.index_count,a.index16?16u:32u,a.base_vertex);
    }
    fprintf(cap_, "\\n     verts");''')
edit('    float sx = float(v.w) / gw;\n    float sy = float(v.h) / gh;\n    float ox = (2.0f * v.x + v.w) / gw - 1;\n    float oy = 1 - (2.0f * v.y + v.h) / gh;', '''    float sx = float(v.w) * s.rt.tenchu_scale_x / gw;
    float sy = float(v.h) * s.rt.tenchu_scale_y / gh;
    float ox = (2.0f * v.x + v.w) * s.rt.tenchu_scale_x / gw - 1;
    float oy = 1 - (2.0f * v.y + v.h) * s.rt.tenchu_scale_y / gh;''')
start=text.index('void D3D12Backend::SetViewportScissor(')
end=text.index('\nvoid D3D12Backend::SetBlendFactorStencilRef',start)
part=text[start:end].replace('    float sc = W / gw;', '    float scx = W / gw * s.rt.tenchu_scale_x;\n    float scy = W / gw * s.rt.tenchu_scale_y;')
for expr in ['s.viewport.x','(s.viewport.x + s.viewport.w)','s.scissors[0]','s.scissors[2]']:
    part=part.replace(expr+' * sc',expr+' * scx')
for expr in ['s.viewport.y','(s.viewport.y + s.viewport.h)','s.scissors[1]','s.scissors[3]']:
    part=part.replace(expr+' * sc',expr+' * scy')
assert ' * sc)' not in part
text=text[:start]+part+text[end:]
edit('D3D12_RECT ScaledRect(const Target& t, int x0, int y0, int x1, int y1) {\n    float sc = float(t.w) / t.gw;\n    return {std::clamp<LONG>(LONG(x0 * sc), 0, t.w), std::clamp<LONG>(LONG(y0 * sc), 0, t.h),\n            std::clamp<LONG>(LONG(x1 * sc), 0, t.w), std::clamp<LONG>(LONG(y1 * sc), 0, t.h)};\n}', '''D3D12_RECT ScaledRect(const Target& t, int x0, int y0, int x1, int y1, float bx = 1, float by = 1) {
    float scx = float(t.w) / t.gw * bx;
    float scy = float(t.h) / t.gh * by;
    return {std::clamp<LONG>(LONG(x0 * scx), 0, t.w), std::clamp<LONG>(LONG(y0 * scy), 0, t.h),
            std::clamp<LONG>(LONG(x1 * scx), 0, t.w), std::clamp<LONG>(LONG(y1 * scy), 0, t.h)};
}''')
edit('rs.push_back(ScaledRect(t, r[0], r[1], r[2], r[3]));','rs.push_back(ScaledRect(t, r[0], r[1], r[2], r[3], s.rt.tenchu_scale_x, s.rt.tenchu_scale_y));')
edit('rs.push_back(ScaledRect(t, int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h)));','rs.push_back(ScaledRect(t, int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h), s.rt.tenchu_scale_x, s.rt.tenchu_scale_y));')
edit('    void BlitFrame(Target& t);','    void BlitFrame(Target& t, const Surface& front);')
edit('    void DrawFrame(Target* t, uint32_t bi);','    void DrawFrame(Target* t, uint32_t bi, const Surface& front);')
edit('void D3D12Backend::BlitFrame(Target& t) {','void D3D12Backend::BlitFrame(Target& t, const Surface& front) {')
edit('    float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;', '    float aspect = tenchu::wide::enabled.load() ? 16.0f / 9.0f : 4.0f / 3.0f;')
edit('    float uv[4] = {0, 0, 1, 1};\n    D3D12_VIEWPORT vp{(w - vw)', '    float uv[4] = {0, 0, front.tenchu_scale_x, front.tenchu_scale_y};\n    D3D12_VIEWPORT vp{(w - vw)')
edit('void D3D12Backend::DrawFrame(Target* t, uint32_t bi) {','void D3D12Backend::DrawFrame(Target* t, uint32_t bi, const Surface& front) {')
edit('    if (t) BlitFrame(*t);','    if (t) BlitFrame(*t, front);')
edit('    DrawFrame(t, bi);','    DrawFrame(t, bi, front);')
edit('    DrawFrame(t, bi, front);\n    Submit(false);', '''    DrawFrame(t, bi, front);
    tenchu::FrameReadback tenchu_readback;
    if (tenchu::GraphicsTestEnabled() || tenchu::CaptureTitleFrame(presents_)) tenchu_readback.Record(dev_.Get(), cl_.Get(), bb_[bi].Get());
    Submit(false);
    if (tenchu_readback.buffer) {
        WaitFence(fence_value_);
        tenchu_readback.SaveAndCheck(app::ExeDir() / "test_artifacts", tenchu::GraphicsTestEnabled(), presents_);
    }''')
edit('    float2 px = fwidth(i.uv);','''    float2 px = fwidth(i.uv);
    uint iw, ih;
    img.GetDimensions(iw, ih);
    float2 halfpx = 0.5 / float2(iw, ih);
    float2 lo = min(uvrect.xy, uvrect.xy + uvrect.zw) + halfpx;
    float2 hi = max(lo, max(uvrect.xy, uvrect.xy + uvrect.zw) - halfpx);''')
edit('img.SampleLevel(ls, i.uv + px * ((float2(x, y) + 0.5) / n - 0.5), 0)', 'img.SampleLevel(ls, clamp(i.uv + px * ((float2(x, y) + 0.5) / n - 0.5), lo, hi), 0)')
edit('    swap_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);', '''    HRESULT tenchu_present = swap_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (FAILED(tenchu_present)) LOGE("D3D12 present failed: %08X", unsigned(tenchu_present));
    else if (presents_ == 0) LOGR("Milestone: first D3D12 present succeeded");''')
assert text.count('app::RenderScale(guest_h_)')==2
text=text.replace('app::RenderScale(guest_h_)','TenchuInternalScale(guest_h_)')
# Disable only the title's observed double-width cutscene history accumulation.
edit('    BuildPipelineKey(s, a, sh.vh, sh.ph, k);', '''    BuildPipelineKey(s, a, sh.vh, sh.ph, k);
    TextureDesc tenchu_history_source;
    if(!sh.prog && !s.ps && s.textures[0] && Describe(s.textures[0],tenchu_history_source) &&
       tenchu::motion::Bypass(TenchuScreenBackdrop(a,s),s.rt.tenchu_backbuffer,stages.rt[0],k.zen,k.blend,
           k.src,k.dst,tenchu_history_source.format,tenchu_history_source.width,tenchu_history_source.height,
           s.viewport.w,s.viewport.h)){
        k.blend=0;
        if(cap_)fprintf(cap_,"cutscene history: previous-frame blend bypassed\\n");
    }''')
# Optional final-image processing; intermediate copies and game textures bypass it.
edit('    bool over = false;        // premultiplied over the target, which keeps its alpha', '    bool post = false;\n    bool over = false;        // premultiplied over the target, which keeps its alpha')
edit('cbuffer B : register(b1) { float4 uvrect; float4 opts; };', 'cbuffer B : register(b1) { float4 uvrect; float4 opts; float4 post0; float4 post1; float4 post2; float4 post3; float4 post4; };')
edit('    return float4(c.rgb, opts.y > 0 ? c.a : 1);', (root/'runtime/graphics/post_processing.hlsl').read_text()+'    return float4(c.rgb, opts.y > 0 ? c.a : 1);')
edit('    float c[8] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3],', '    float c[28] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3],')
edit('    std::memcpy(cb.cpu, c, sizeof(c));', '    c[8]=o.post && tenchu::post::enabled.load() ? 1.f : 0.f;\n    for(unsigned i=0;i<tenchu::post::count;++i)c[9+i]=tenchu::post::values[i].load();\n    c[25]=tenchu::post::compare.load()?1.f:0.f; c[26]=tenchu::post::divider.load();\n    std::memcpy(cb.cpu, c, sizeof(c));')
edit('    Blit(t.res.Get(), t.fmt, uv, {.gamma = gamma_});', '    Blit(t.res.Get(), t.fmt, uv, {.gamma = gamma_, .post = true});')
# F3 must work from the user shortcut and retain neighboring frames for trails.
edit('    void StartCapture();', '    void StartCapture();\n    unsigned tenchu_capture_remaining_ = 0;')
edit('''    if (cap_) {
        FinishCapture(front);
    } else if (g_capture.exchange(false) && CaptureOn()) {
        StartCapture();
    }''', '''    if (cap_) {
        FinishCapture(front);
        if(tenchu_capture_remaining_)--tenchu_capture_remaining_;
    }
    if(g_capture.exchange(false))tenchu_capture_remaining_=3;
    if(!cap_ && tenchu_capture_remaining_ && CaptureOn())StartCapture();''')
edit('    char stamp[32];\n    snprintf(stamp, sizeof(stamp), "%02u%02u%02u", tm.wHour, tm.wMinute, tm.wSecond);',
     '    char stamp[80];\n    snprintf(stamp,sizeof(stamp),"%04u%02u%02u_%02u%02u%02u_%03u_%llu",tm.wYear,tm.wMonth,tm.wDay,tm.wHour,tm.wMinute,tm.wSecond,tm.wMilliseconds,(unsigned long long)presents_);')
edit('    cap_ = _wfopen((cap_dir_ / "frame.txt").c_str(), L"w");',
     '    cap_ = ec ? nullptr : _wfopen((cap_dir_ / "frame.txt").c_str(), L"w");\n    if(!cap_){tenchu_capture_remaining_=0;LOGE("F3 capture folder could not be written");return;}\n    tenchu::capture_next_presentation=true;')
# Window resizing is a transaction: pause while dragging, drain, resize once,
# validate every backbuffer before rendering, and continue guest fence progress.
edit('    void ResizeSwap();', '    bool ResizeSwap();')
edit('    void CreateBackBufferViews();', '    bool CreateBackBufferViews();')
edit('    CreateBackBufferViews();\n    if (!CreateRootSignature())', '    if (!CreateBackBufferViews()) return false;\n    if (!CreateRootSignature())')
start=text.index('void D3D12Backend::CreateBackBufferViews() {')
end=text.index('void D3D12Backend::CreateHeaps()',start)
text=text[:start]+"""bool D3D12Backend::CreateBackBufferViews() {
    std::array<ComPtr<ID3D12Resource>, kFrames> buffers;
    for (int i = 0; i < kFrames; ++i) {
        const HRESULT hr = swap_->GetBuffer(i, IID_PPV_ARGS(&buffers[i]));
        if (FAILED(hr) || !buffers[i]) {
            LOGE("Swapchain backbuffer %d unavailable: %08X", i, unsigned(hr));
            return false;
        }
    }
    for (int i = 0; i < kFrames; ++i) {
        bb_[i] = std::move(buffers[i]);
        dev_->CreateRenderTargetView(bb_[i].Get(), nullptr, Cpu(rtv_heap_.Get(), kRtvCount + i, rtv_inc_));
    }
    return true;
}

"""+text[end:]
start=text.index('void D3D12Backend::ResizeSwap() {')
end=text.index('void D3D12Backend::FinishCapture(',start)
text=text[:start]+(root/'tools/scripts/resize_swap_overlay.inc').read_text()+text[end:]
edit('    ResizeSwap();\n    Target* t = EnsureTarget(front, false);', """    if (TenchuPresentationPaused() || !ResizeSwap()) {
        // Submit offscreen work and complete the guest frame even when the
        // native window cannot present. Never stall the title on a missing swap.
        Submit(false);
        present_fence_[presents_ % (kFrames + 1)].store(fence_value_);
        DeliverReports();
        ++presents_;
        presents_seen_.store(presents_);
        ++frame_;
        ReleaseOldTextures();
        ResetFrame();
        CountFps();
        ApplySettings();
        Sleep(16);
        return;
    }
    Target* t = EnsureTarget(front, false);""")
edit('    if (FAILED(tenchu_present)) LOGE', '    if (SUCCEEDED(tenchu_present)) TenchuPresentationStarted();\n    if (FAILED(tenchu_present)) LOGE')
# UnitTexcoords reads the vertices' own coordinates; a fixed-function texture
# transform (the scrolling sky and water) or generated coordinates move them past
# 0..1 afterwards, so those stages must keep wrapping.
edit('''// UnitTexcoords only changes stages that wrap
bool AnyStageWraps(const State& s) {''', '''// a wrapping stage whose coordinates the fixed-function pipeline transforms or generates
bool TenchuMovedTexcoords(const State& s, const Assembled& a) {
    if (a.pretransformed || ((s.vs_handle & 1) && s.vs && !s.vs->program.empty())) return false;
    for (const auto& t : s.tss) {
        if ((t[kTssAddressU] == kTaddressWrap || t[kTssAddressV] == kTaddressWrap) &&
            ((t[kTssTextureTransformFlags] & 7) || (t[kTssTexCoordIndex] >> 16))) return true;
    }
    return false;
}

// UnitTexcoords only changes stages that wrap
bool AnyStageWraps(const State& s) {''')
edit('AnyStageWraps(s) && UnitTexcoords(a)', 'AnyStageWraps(s) && !TenchuMovedTexcoords(s, a) && UnitTexcoords(a)')
for api in ('CreateDXGIFactory2','D3D12CreateDevice','D3D12GetDebugInterface','D3D12SerializeRootSignature'):
    assert api+'(' in text
    text=text.replace(api+'(', 'Tenchu'+api+'(')
text='#include "graphics/cutscene_motion_blur.h"\n#include "graphics/post_processing.h"\n#include "graphics/frame_readback.h"\n#include "graphics/widescreen.h"\n#include "graphics/renderer_selection.h"\nfloat TenchuInternalScale(uint32_t);\n'+text
text=text.replace('LOGI("d3d12: frame captured to %s", cap_dir_.string().c_str());','LOGR("d3d12: frame captured to %s", cap_dir_.string().c_str());')
p=out/'gpu/d3d12.cpp'
p.parent.mkdir(parents=True,exist_ok=True)
if not p.exists() or p.read_text()!=text: p.write_text(text)

# Texture packs for other versions of the game (PS2 Wrath of Heaven): matched by
# appearance in src/common/texture_packs.cpp. The loader submits each texture's
# level 0 once and asks the matcher when its own key has no pack file.
write('gpu/texturedumper.cpp', [
    ('bool Replacing() { return g.set.replacing && !g.files.empty(); }',
     'bool Replacing() { return g.set.replacing && (!g.files.empty() || tenchu::packs::Active()); }'),
    ('''    bool reloaded = g.set.replacing && UpdatePackFiles();
''', '''    bool reloaded = g.set.replacing && UpdatePackFiles();
    // F6 > Textures changed packs or choices: every texture looks its replacement up again
    static uint32_t tenchu_revision = 0;
    if (uint32_t r = tenchu::packs::Revision(); r != tenchu_revision) {
        tenchu_revision = r;
        std::lock_guard l(g.lock);
        g.revision++;
        g.asked.clear();
        g.loaded.clear();
        reloaded = true;
    }
'''),
    ('''void Dump(uint64_t key, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette) {
''', '''void Dump(uint64_t key, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette) {
    if (key && g.set.replacing && !g.files.count(key) && tenchu::packs::Wants(key)) {
        Post([key, img = Decode(d, guest, palette)] {
            std::vector<uint8_t> px;
            if (TenchuBgra(img, px)) tenchu::packs::Submit(key, img.width, img.height, std::move(px));
            else tenchu::packs::Submit(key, 0, 0, {});
        });
    }
'''),
    ('''    auto file = g.files.find(key);
    if (!g.set.replacing || file == g.files.end()) return Lookup::None;
''', '''    if (!g.set.replacing) return Lookup::None;
    fs::path path;
    bool ps2_alpha = false;
    if (auto file = g.files.find(key); file != g.files.end()) {
        path = file->second;
        tenchu::packs::NoteExact(key, path);  // counted and listed as replaced in F6 > Textures
    } else {
        tenchu::packs::Result found = tenchu::packs::Find(key, path, ps2_alpha);
        if (found == tenchu::packs::Result::Pending) return Lookup::Loading;
        if (found == tenchu::packs::Result::None) return Lookup::None;
    }
'''),
    ('Post([key, path = file->second, revision] { Load(key, path, revision); });',
     'Post([key, path, ps2_alpha, revision] { Load(key, path, revision, ps2_alpha); });'),
    ('void Load(uint64_t key, const fs::path& path, uint32_t revision) {', r'''// Mipmaps stored in a replacement are kept only while they still show its top level: an editor that saves the
// edited top level over a dump's old chain would bring the original texture back wherever the texture is drawn
// smaller. Uncompressed images always get a chain made from their top level.
bool TenchuTopToBgra(Image& img) {
    IWICImagingFactory* wic = Wic();
    std::vector<uint8_t> dds;
    ComPtr<IWICBitmapSource> bgra;
    if (!wic || img.levels.empty() || FAILED(LevelZeroBitmap(wic, img, dds, bgra))) return false;
    Level top{std::vector<uint8_t>(size_t(img.width) * img.height * 4), img.width * 4};
    if (FAILED(bgra->CopyPixels(nullptr, top.pitch, UINT(top.px.size()), top.px.data()))) return false;
    img.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    img.levels.clear();
    img.levels.push_back(std::move(top));
    return true;
}

void TenchuFreshMipmaps(Image& img, const fs::path& path) {
    if (img.levels.size() <= 1) return;
    if (Uncompressed(img.format)) {
        img.levels.resize(1);  // AddMipmaps makes the chain again
        return;
    }
    // compressed: level 1 against level 0 halved, mean difference per channel in 0..255
    constexpr double kStale = 10;
    Image top{img.format, img.width, img.height, {img.levels[0]}};
    Image second{img.format, std::max(img.width / 2, 1u), std::max(img.height / 2, 1u), {img.levels[1]}};
    if (!TenchuTopToBgra(top) || !TenchuTopToBgra(second)) return;
    double diff = 0;
    uint8_t box[4];
    for (uint32_t y = 0; y < second.height; y++)
        for (uint32_t x = 0; x < second.width; x++) {
            AverageBox(top.levels[0], top.width, top.height, x, y, box);
            const uint8_t* s = &second.levels[0].px[size_t(y) * second.levels[0].pitch + x * 4];
            for (int c = 0; c < 4; c++) diff += std::abs(int(box[c]) - int(s[c]));
        }
    diff /= double(second.width) * second.height * 4;
    if (diff <= kStale) return;
    LOGR("texture pack: %s: its mipmaps do not match its top level (difference %.1f), made again from it",
         Utf8(path).c_str(), diff);
    img = std::move(top);
}

void Load(uint64_t key, const fs::path& path, uint32_t revision, bool ps2_alpha = false) {'''),
    ('''    if (ok) {
        AddMipmaps(img);''', '''    if (ok) TenchuFreshMipmaps(img, path);
    if (ok && ps2_alpha && Uncompressed(img.format)) tenchu::packs::ScaleAlpha(img.levels[0].px.data(), size_t(img.width) * img.height);
    if (ok) {
        AddMipmaps(img);'''),
    # Dumps hold the top level only: editors show one image, and loading makes the chain from it.
    ('''    img.levels.resize(d.levels);
    for (uint32_t l = 0; l < d.levels; l++) {''',
     '''    img.levels.resize(1);
    for (uint32_t l = 0; l < 1; l++) {'''),
    ('    ListPack(g.pack_dir);', '''    ListPack(g.pack_dir);
    tenchu::packs::ClearExact();'''),
    ('''}  // namespace

bool Update() {''', '''// level 0 as tightly packed BGRA for the pack matcher
bool TenchuBgra(const Image& img, std::vector<uint8_t>& px) {
    if (img.levels.empty() || !img.width || !img.height) return false;
    IWICImagingFactory* wic = Wic();
    std::vector<uint8_t> dds;
    ComPtr<IWICBitmapSource> bgra;
    if (!wic || FAILED(LevelZeroBitmap(wic, img, dds, bgra))) return false;
    px.resize(size_t(img.width) * img.height * 4);
    return SUCCEEDED(bgra->CopyPixels(nullptr, img.width * 4, UINT(px.size()), px.data()));
}

}  // namespace

bool Update() {'''),
], prefix='#include "graphics/texture_packs.h"\n')

# F6 > Mods: a file in an enabled mod folder replaces the disc file at the same
# path. src/common/mods.cpp answers for every path below the disc root.
write('kernel/files.cpp', [
    ('''    if (best->first == kCdRom)
        if (const Disc* disc = MountedDisc()) return {{}, true, disc->Find(parts)};
''', '''    if (best->first == kCdRom) {
        if (std::string mod = TenchuModOverride(parts); !mod.empty()) return {mod};
        if (const Disc* disc = MountedDisc()) return {{}, true, disc->Find(parts)};
    }
'''),
    # F6 > Levels: the active custom level swaps the bytes of the stage files it replaces as they
    # are read (src/common/custom_levels.cpp)
    # past the file's end too: a level's blocks bigger than the stage's are served from there
    ('''        n += uint32_t(done);
        if (done < chunk) break;''', '''        if (!write && done) TenchuLevelPatchRead(f->xbox, pos + n, static_cast<uint8_t*>(HostPtr(at)), done);
        if (!write && done < chunk)
            done += TenchuLevelTailRead(f->xbox, pos + n + done, static_cast<uint8_t*>(HostPtr(at)) + done, chunk - done);
        n += uint32_t(done);
        if (done < chunk) break;'''),
    # and the file is as long as they need: the game's reader asks for the size and calls a read
    # past it a bad disc
    ('''        size = fs::file_size(f->host, ec);
    }
    std::memset(HostPtr(out), 0, len);''', '''        size = fs::file_size(f->host, ec);
    }
    if (!f->dir) size = TenchuLevelFileSize(f->xbox, size);
    std::memset(HostPtr(out), 0, len);'''),
    ('''            if (f->entry) FillNetworkInfo(out, *f->entry);
            else FillNetworkInfo(out, f->host);''', '''            if (f->entry) FillNetworkInfo(out, *f->entry);
            else FillNetworkInfo(out, f->host);
            if (!f->dir) FillNetworkInfo(out, R64(out), size, false);'''),
    # TENCHU_FILE_TRACE=1: every read with the game's path, offset and length (which archive
    # entries a stage loads; analysis/scripts map the offsets to entries)
    ('''    f->pos = pos + n;
    if (write && n < len)''', '''    f->pos = pos + n;
    static const bool trace = std::getenv("TENCHU_FILE_TRACE") != nullptr;
    if (trace && !write) LOGR("file read %s at %llX length %X", f->xbox.c_str(), (unsigned long long)pos, n);
    if (write && n < len)'''),
], prefix='#include <cstdint>\n#include <string>\n#include <vector>\nstd::string TenchuModOverride(const std::vector<std::string>& parts);\n'
          'void TenchuLevelPatchRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size);\n'
          'size_t TenchuLevelTailRead(const std::string& xbox_path, uint64_t offset, uint8_t* data, size_t size);\n'
          'uint64_t TenchuLevelFileSize(const std::string& xbox_path, uint64_t size);\n')


# Releases carry no game files: when the assets folder has no matching copy, use
# the disc image picked on an earlier start, else ask the player for one
# (src/common/game_location.cpp). Development builds still find assets first.
write('app/game_files.cpp', [
    ("""    if (other_name.empty()) {
        MessageBoxW(nullptr, kGame.missing, kGame.short_title, MB_ICONERROR);""",
     """    auto tenchu_try_iso = [&](const fs::path& iso) {
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        std::vector<uint8_t> image;
        if (!disc->Open(iso, error)) {
            LOGR("Game: %s: %s", iso.string().c_str(), error.c_str());
            return false;
        }
        if (!disc->Load(IMAGE_XBE, image)) {
            LOGR("Game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
            return false;
        }
        return consider(iso, std::move(image), std::move(disc));
    };
    if (fs::path iso = TenchuRememberedIso(); !iso.empty() && fs::exists(iso, ec) && tenchu_try_iso(iso)) {
        TenchuExtractDisc();
        return true;
    }
    for (bool retry = false;; retry = true) {
        fs::path iso = TenchuPickIso(retry);
        if (iso.empty()) break;
        if (tenchu_try_iso(iso)) {
            TenchuRememberIso(iso);
            TenchuExtractDisc();
            return true;
        }
    }
    if (other_name.empty()) {
        MessageBoxW(nullptr, kGame.missing, kGame.short_title, MB_ICONERROR);"""),
    # a disc image in assets is unpacked as well
    ("""        if (consider(iso, std::move(xbe), std::move(disc))) return true;""",
     """        if (consider(iso, std::move(xbe), std::move(disc))) {
            TenchuExtractDisc();
            return true;
        }"""),
    # once unpacked, the game runs from extracted\ beside it (src/common/disc_extract.cpp)
    ("""    auto dir = ExeDir();
    std::error_code ec;
""", """    auto dir = ExeDir();
    std::error_code ec;
    if (fs::exists(dir / "extracted" / ".complete", ec) && fs::exists(dir / "extracted" / IMAGE_XBE, ec)) return dir / "extracted";
"""),
], prefix='#include <filesystem>\nbool TenchuExtractDisc();\nstd::filesystem::path TenchuRememberedIso();\nvoid TenchuRememberIso(const std::filesystem::path&);\nstd::filesystem::path TenchuPickIso(bool retry);\n')
