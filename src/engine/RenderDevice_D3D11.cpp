// D3D11 backend with a batched sprite pipeline (letterboxed 1280x720 canvas)
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include "Engine.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace cs {
namespace {

const char* kHLSL = R"(
cbuffer CB : register(b0) { float4 viewport; }  // xy = canvas size, zw = unused
struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VSOut VS(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x / viewport.x * 2 - 1, 1 - i.pos.y / viewport.y * 2, 0, 1);
    o.uv = i.uv; o.col = i.col; return o;
}
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 PS(VSOut i) : SV_TARGET { return tex.Sample(smp, i.uv) * i.col; }
)";

struct Vertex { float x, y, u, v; float r, g, b, a; };

class D3D11Device final : public IRenderDevice {
public:
    bool Init(void* wnd, uint32_t w, uint32_t h) override {
        hwnd_ = (HWND)wnd;
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0 };
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                fl, 1, D3D11_SDK_VERSION, &sd, &swap_, &dev_, nullptr, &ctx_)))
            return false;
        if (!CreatePipeline()) return false;
        CreateBackbuffer(w, h);
        return true;
    }

    void Resize(uint32_t w, uint32_t h) override {
        rtv_.Reset();
        swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackbuffer(w, h);
    }

    TextureHandle CreateTextureRGBA8(uint32_t w, uint32_t h, const void* px) override {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{ px, w * 4, 0 };
        ComPtr<ID3D11Texture2D> t;
        if (FAILED(dev_->CreateTexture2D(&d, &sr, &t))) return 0;
        ComPtr<ID3D11ShaderResourceView> srv;
        dev_->CreateShaderResourceView(t.Get(), nullptr, &srv);
        textures_.push_back(srv);
        return (TextureHandle)textures_.size();  // 1-based
    }

    void BeginFrame(const Color& c) override {
        float clr[4] = { c.r, c.g, c.b, c.a };
        ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
        ctx_->ClearRenderTargetView(rtv_.Get(), clr);
        // letterbox viewport preserving 16:9
        float scale = std::min(bbW_ / kCW, bbH_ / kCH);
        D3D11_VIEWPORT vp{ (bbW_ - kCW * scale) * 0.5f, (bbH_ - kCH * scale) * 0.5f,
                           kCW * scale, kCH * scale, 0, 1 };
        ctx_->RSSetViewports(1, &vp);
        verts_.clear(); currentTex_ = 0;
    }

    void DrawSprite(TextureHandle tex, const Rect& d, const Rect& uv, const Color& t) override {
        if (tex != currentTex_) { Flush(); currentTex_ = tex; }
        float x0 = d.x, y0 = d.y, x1 = d.x + d.w, y1 = d.y + d.h;
        float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
        Vertex q[6] = {
            {x0,y0,u0,v0,t.r,t.g,t.b,t.a}, {x1,y0,u1,v0,t.r,t.g,t.b,t.a}, {x1,y1,u1,v1,t.r,t.g,t.b,t.a},
            {x0,y0,u0,v0,t.r,t.g,t.b,t.a}, {x1,y1,u1,v1,t.r,t.g,t.b,t.a}, {x0,y1,u0,v1,t.r,t.g,t.b,t.a},
        };
        verts_.insert(verts_.end(), q, q + 6);
        if (verts_.size() + 6 > kMaxVerts) Flush();
    }

    void DrawQuad(TextureHandle tex, const Vec2 p[4], const Rect& uv, const Color& t) override {
        if (tex != currentTex_) { Flush(); currentTex_ = tex; }
        float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
        Vertex a{p[0].x,p[0].y,u0,v0,t.r,t.g,t.b,t.a}, b{p[1].x,p[1].y,u1,v0,t.r,t.g,t.b,t.a};
        Vertex c{p[2].x,p[2].y,u1,v1,t.r,t.g,t.b,t.a}, d{p[3].x,p[3].y,u0,v1,t.r,t.g,t.b,t.a};
        Vertex q[6] = { a, b, c, a, c, d };
        verts_.insert(verts_.end(), q, q + 6);
        if (verts_.size() + 6 > kMaxVerts) Flush();
    }

    void EndFrame() override { Flush(); swap_->Present(1, 0); }
    Vec2 CanvasSize() const override { return { kCW, kCH }; }
    void Shutdown() override { textures_.clear(); rtv_.Reset(); ctx_.Reset(); swap_.Reset(); dev_.Reset(); }

private:
    static constexpr float kCW = 1280.f, kCH = 720.f;
    static constexpr size_t kMaxVerts = 6 * 4096;

    bool CreatePipeline() {
        ComPtr<ID3DBlob> vs, ps, err;
        if (FAILED(D3DCompile(kHLSL, strlen(kHLSL), 0, 0, 0, "VS", "vs_5_0", 0, 0, &vs, &err))) return false;
        if (FAILED(D3DCompile(kHLSL, strlen(kHLSL), 0, 0, 0, "PS", "ps_5_0", 0, 0, &ps, &err))) return false;
        dev_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_);
        dev_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_);
        D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        dev_->CreateInputLayout(il, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_);

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (UINT)(kMaxVerts * sizeof(Vertex)); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev_->CreateBuffer(&bd, nullptr, &vb_);

        float cb[4] = { kCW, kCH, 0, 0 };
        D3D11_BUFFER_DESC cbd{ 16, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA cbs{ cb, 0, 0 };
        dev_->CreateBuffer(&cbd, &cbs, &cbuf_);

        D3D11_BLEND_DESC blend{};
        auto& rt = blend.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev_->CreateBlendState(&blend, &blend_);

        D3D11_SAMPLER_DESC sm{};
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        dev_->CreateSamplerState(&sm, &samp_);

        D3D11_RASTERIZER_DESC rs{}; rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE;
        dev_->CreateRasterizerState(&rs, &rast_);
        return true;
    }

    void CreateBackbuffer(uint32_t w, uint32_t h) {
        bbW_ = (float)w; bbH_ = (float)h;
        ComPtr<ID3D11Texture2D> bb;
        swap_->GetBuffer(0, IID_PPV_ARGS(&bb));
        dev_->CreateRenderTargetView(bb.Get(), nullptr, &rtv_);
    }

    void Flush() {
        if (verts_.empty()) return;
        D3D11_MAPPED_SUBRESOURCE m;
        ctx_->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, verts_.data(), verts_.size() * sizeof(Vertex));
        ctx_->Unmap(vb_.Get(), 0);
        UINT stride = sizeof(Vertex), off = 0;
        ctx_->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &stride, &off);
        ctx_->IASetInputLayout(layout_.Get());
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_.Get(), nullptr, 0);
        ctx_->VSSetConstantBuffers(0, 1, cbuf_.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, samp_.GetAddressOf());
        ctx_->RSSetState(rast_.Get());
        float bf[4] = {};
        ctx_->OMSetBlendState(blend_.Get(), bf, 0xFFFFFFFF);
        if (currentTex_ && currentTex_ <= textures_.size())
            ctx_->PSSetShaderResources(0, 1, textures_[currentTex_ - 1].GetAddressOf());
        ctx_->Draw((UINT)verts_.size(), 0);
        verts_.clear();
    }

    HWND hwnd_ = nullptr;
    float bbW_ = 1, bbH_ = 1;
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vb_, cbuf_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11SamplerState> samp_;
    ComPtr<ID3D11RasterizerState> rast_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;
    std::vector<Vertex> verts_;
    TextureHandle currentTex_ = 0;
};

} // namespace

std::unique_ptr<IRenderDevice> CreateRenderDeviceD3D11() { return std::make_unique<D3D11Device>(); }

} // namespace cs
