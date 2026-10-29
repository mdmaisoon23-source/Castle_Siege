#include "Engine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"   // https://github.com/nothings/stb (public domain)

namespace cs {

#ifdef CS_USE_GL
std::unique_ptr<IRenderDevice> CreateRenderDeviceGL();
#define CS_CREATE_RENDER() CreateRenderDeviceGL()
#else
#define CS_CREATE_RENDER() CreateRenderDeviceD3D11()
#endif
#ifdef CS_NULL_AUDIO
std::unique_ptr<IAudio> CreateNullAudio();
#define CS_CREATE_AUDIO() CreateNullAudio()
#else
#define CS_CREATE_AUDIO() CreateFmodAudio()
#endif

static double NowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------ AssetManager
void AssetManager::Init(IRenderDevice* dev, JobSystem* jobs, std::string root) {
    dev_ = dev; jobs_ = jobs; root_ = std::move(root);
    const uint32_t px = 0xFFFFFFFFu;
    white_ = dev_->CreateTextureRGBA8(1, 1, &px);
}

TextureHandle AssetManager::LoadTexture(const std::string& rel) {
    auto it = cache_.find(rel);
    if (it != cache_.end()) return it->second;
    cache_[rel] = white_;  // placeholder until decode finishes
    std::string path = root_ + "/" + rel;
    jobs_->Submit([this, rel, path] {
        int w, h, n;
        unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!p) return;  // missing asset: keep placeholder
        Decoded d{rel, w, h, std::vector<uint8_t>(p, p + (size_t)w * h * 4)};
        stbi_image_free(p);
        std::lock_guard<std::mutex> l(m_);
        ready_.push_back(std::move(d));
    });
    return white_;
}

void AssetManager::PumpMainThread() {
    std::vector<Decoded> batch;
    { std::lock_guard<std::mutex> l(m_); batch.swap(ready_); }
    for (auto& d : batch) {
        cache_[d.key] = dev_->CreateTextureRGBA8(d.w, d.h, d.px.data());
        sizes_[d.key] = { (float)d.w, (float)d.h };
    }
    // NOTE: scenes should re-query LoadTexture(rel) each frame (cheap map hit)
    // or subscribe to a "texture ready" event; phase 2 adds handles with indirection.
}

// ------------------------------------------------------------ Application
bool Application::Init(void* wnd, uint32_t w, uint32_t h, const std::string& root) {
    pw_ = w; ph_ = h;
    jobs_.Start();

    render_ = CS_CREATE_RENDER();
    if (!render_ || !render_->Init(wnd, w, h)) return false;

    audio_ = CS_CREATE_AUDIO();
    if (!audio_->Init(root + "/audio")) std::fprintf(stderr, "Audio disabled\n");

    assets_.Init(render_.get(), &jobs_, root);

    svc_ = { render_.get(), audio_.get(), &assets_, &jobs_, {} };
    svc_.changeScene = [this](std::unique_ptr<IScene> next) { pending_ = std::move(next); };
    scene_ = CreateTitleScene();
    scene_->OnEnter(svc_);
    last_ = NowSec();
    return true;
}

void Application::OnResize(uint32_t w, uint32_t h) {
    if (!w || !h || !render_) return;
    pw_ = w; ph_ = h;
    render_->Resize(w, h);
}

void Application::OnMouse(float px, float py, bool down) {
    Vec2 c = render_->CanvasSize();
    // map window pixels -> letterboxed canvas coordinates
    float scale = std::min(pw_ / c.x, ph_ / c.y);
    float ox = (pw_ - c.x * scale) * 0.5f, oy = (ph_ - c.y * scale) * 0.5f;
    input_.mouseX = (px - ox) / scale;
    input_.mouseY = (py - oy) / scale;
    if (down && !input_.mouseDown) input_.mousePressed = true;
    if (!down && input_.mouseDown) input_.mouseReleased = true;
    input_.mouseDown = down;
}

void Application::Frame() {
    double now = NowSec();
    double dt = std::min(now - last_, 0.25);
    last_ = now;

    // Apply a requested scene change before doing anything else this frame.
    if (pending_) {
        if (scene_) scene_->OnExit();
        scene_ = std::move(pending_);
        input_.mousePressed = input_.mouseReleased = false;
        scene_->OnEnter(svc_);
    }

    assets_.PumpMainThread();
    audio_->Update();

    const double step = 1.0 / 60.0;
    acc_ += dt;
    while (acc_ >= step) {
        scene_->Update((float)step, input_);
        input_.mousePressed = input_.mouseReleased = false;
        acc_ -= step;
    }

    render_->BeginFrame({0.05f, 0.07f, 0.10f, 1});
    scene_->Render();
    render_->EndFrame();
}

void Application::Shutdown() {
    if (scene_) scene_->OnExit();
    scene_.reset();
    jobs_.Stop();
    if (audio_) audio_->Shutdown();
    if (render_) render_->Shutdown();
}

} // namespace cs
