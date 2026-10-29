#pragma once
// Castle Siege engine - core interfaces (phase 1)
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cs {

struct Vec2  { float x = 0, y = 0; };
struct Rect  { float x = 0, y = 0, w = 0, h = 0; };
struct Color { float r = 1, g = 1, b = 1, a = 1; };
using TextureHandle = uint32_t;

// ---------------------------------------------------------------- JobSystem
class JobSystem {
public:
    void Start(unsigned workers = 0) {
        if (!workers) workers = std::max(1u, std::thread::hardware_concurrency() - 1);
        running_ = true;
        for (unsigned i = 0; i < workers; ++i)
            threads_.emplace_back([this] { Worker(); });
    }
    void Stop() {
        { std::lock_guard<std::mutex> l(m_); running_ = false; }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
        threads_.clear();
    }
    void Submit(std::function<void()> job) {
        { std::lock_guard<std::mutex> l(m_); jobs_.push(std::move(job)); }
        cv_.notify_one();
    }
private:
    void Worker() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [this] { return !running_ || !jobs_.empty(); });
                if (!running_ && jobs_.empty()) return;
                job = std::move(jobs_.front()); jobs_.pop();
            }
            job();
        }
    }
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> jobs_;
    std::mutex m_;
    std::condition_variable cv_;
    bool running_ = false;
};

// ---------------------------------------------------------------- RHI
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual bool Init(void* nativeWindow, uint32_t w, uint32_t h) = 0;
    virtual void Resize(uint32_t w, uint32_t h) = 0;
    virtual TextureHandle CreateTextureRGBA8(uint32_t w, uint32_t h, const void* pixels) = 0;
    virtual void BeginFrame(const Color& clear) = 0;
    // dst in virtual canvas units (1280x720), uv in 0..1
    virtual void DrawSprite(TextureHandle tex, const Rect& dst, const Rect& uv, const Color& tint) = 0;
    // Arbitrary (rotated / skewed) quad. Corners in order: top-left, top-right, bottom-right, bottom-left.
    virtual void DrawQuad(TextureHandle tex, const Vec2 corners[4], const Rect& uv, const Color& tint) = 0;
    virtual void EndFrame() = 0;
    virtual Vec2 CanvasSize() const = 0;
    virtual void Shutdown() = 0;
};
std::unique_ptr<IRenderDevice> CreateRenderDeviceD3D11();
// Later: CreateRenderDeviceMetal(), CreateRenderDeviceGLES()

// ---------------------------------------------------------------- Audio
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual bool Init(const std::string& bankDir) = 0;
    virtual void LoadBankAsync(const std::string& file) = 0;
    virtual bool IsBankLoaded(const std::string& file) = 0;
    virtual void PlayEvent(const std::string& path) = 0;
    virtual void Update() = 0;
    virtual void Shutdown() = 0;
};
std::unique_ptr<IAudio> CreateFmodAudio();

// ---------------------------------------------------------------- Input
struct InputState {
    float mouseX = 0, mouseY = 0;  // canvas units
    bool  mouseDown = false, mousePressed = false, mouseReleased = false;
};

// ---------------------------------------------------------------- Assets
class AssetManager {
public:
    void Init(IRenderDevice* dev, JobSystem* jobs, std::string root);
    // Returns a placeholder immediately, swaps to real texture when decoded.
    TextureHandle LoadTexture(const std::string& relPath);
    void PumpMainThread();  // uploads decoded pixels to GPU on the render thread
    TextureHandle White() const { return white_; }
    // Pixel size of a loaded texture; (0,0) until it has finished loading.
    Vec2 TextureSize(const std::string& relPath) const {
        auto it = sizes_.find(relPath);
        return it == sizes_.end() ? Vec2{} : it->second;
    }
private:
    struct Decoded { std::string key; int w, h; std::vector<uint8_t> px; };
    std::unordered_map<std::string, Vec2> sizes_;
    IRenderDevice* dev_ = nullptr; JobSystem* jobs_ = nullptr; std::string root_;
    TextureHandle white_ = 0;
    std::unordered_map<std::string, TextureHandle> cache_;
    std::mutex m_; std::vector<Decoded> ready_;
};

// ---------------------------------------------------------------- Scenes
struct Services;

class IScene {
public:
    virtual ~IScene() = default;
    virtual void OnEnter(Services&) = 0;
    virtual void Update(float dt, const InputState&) = 0;
    virtual void Render() = 0;
    virtual void OnExit() {}
};

struct Services {
    IRenderDevice* render; IAudio* audio; AssetManager* assets; JobSystem* jobs;
    // Ask the application to switch scenes (applied safely at the start of the next frame).
    std::function<void(std::unique_ptr<IScene>)> changeScene;
};

std::unique_ptr<IScene> CreateTitleScene();
std::unique_ptr<IScene> CreateLoadingScene();
std::unique_ptr<IScene> CreateVillageScene();

// ---------------------------------------------------------------- Application
class Application {
public:
    bool Init(void* nativeWindow, uint32_t w, uint32_t h, const std::string& assetRoot);
    void Frame();                       // call once per OS loop iteration
    void OnResize(uint32_t w, uint32_t h);
    void OnMouse(float px, float py, bool down);   // pixel coords
    void Shutdown();
private:
    JobSystem jobs_;
    std::unique_ptr<IRenderDevice> render_;
    std::unique_ptr<IAudio> audio_;
    AssetManager assets_;
    std::unique_ptr<IScene> scene_;
    std::unique_ptr<IScene> pending_;
    Services svc_{};
    InputState input_{};
    uint32_t pw_ = 1, ph_ = 1;
    double last_ = 0, acc_ = 0;
};

} // namespace cs
