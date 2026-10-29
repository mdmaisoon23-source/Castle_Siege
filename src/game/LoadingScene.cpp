#include <algorithm>
#include <cmath>
#include "../engine/Engine.h"
#include "../engine/Gfx.h"

namespace cs {
namespace {

constexpr float kCanvasW = 1280.f, kCanvasH = 720.f;

// Black "Loading..." screen with a ring of spinning swords (original vector art drawn with quads).
class LoadingScene final : public IScene {
public:
    void OnEnter(Services& s) override { s_ = &s; }

    void Update(float dt, const InputState&) override {
        t_ += dt;
        // Phase 2: wait for real work here (network login, asset streaming). For now: minimum show time.
        if (!switched_ && t_ >= kMinSeconds) {
            switched_ = true;
            s_->changeScene(CreateVillageScene());
        }
    }

    void Render() override {
        gfx::Painter p = gfx::MakePainter(*s_);
        p.Fill(0, 0, kCanvasW, kCanvasH, gfx::RGB(0, 0, 0));

        const float cx = kCanvasW / 2, cy = 330.f;

        // Swords appear one by one (1..6) and the whole ring keeps turning, then it restarts.
        const int   kMax = 6;
        const float cycle = 2.4f;
        float phase = std::fmod(t_, cycle) / cycle;
        int   count = 1 + (int)(phase * kMax);
        if (count > kMax) count = kMax;
        float spin = t_ * 2.6f;

        for (int i = 0; i < count; ++i) {
            float ang = spin + i * (2.f * gfx::kPi / kMax);
            DrawSword(p, cx, cy, ang);
        }

        gfx::DrawTextCentered(p, kCanvasW / 2, 512, 40, "Loading...", gfx::RGB(1, 1, 1));
    }

private:
    // One sword, hilt near the ring centre, blade pointing outward at `angle` (0 = straight up).
    static void DrawSword(gfx::Painter& p, float cx, float cy, float angle) {
        float ca = std::cos(angle), sa = std::sin(angle);
        // local coords: x sideways, y negative = outward
        auto P = [&](float lx, float ly) { return Vec2{ cx + lx * ca - ly * sa, cy + lx * sa + ly * ca }; };

        const Color bladeL = gfx::RGB(0.86f, 0.89f, 0.94f);
        const Color bladeR = gfx::RGB(0.58f, 0.63f, 0.72f);
        const Color guard  = gfx::RGB(0.45f, 0.48f, 0.54f);
        const Color grip   = gfx::RGB(0.86f, 0.60f, 0.14f);
        const Color gripD  = gfx::RGB(0.55f, 0.36f, 0.08f);

        // blade (two shaded halves + pointed tip)
        p.Quad(P(-8, -34), P(0, -34), P(0, -88), P(-8, -76), bladeL);
        p.Quad(P(0, -34), P(8, -34), P(8, -76), P(0, -88), bladeR);
        // cross guard
        p.Quad(P(-15, -34), P(15, -34), P(15, -28), P(-15, -28), guard);
        // grip (striped)
        p.Quad(P(-3.5f, -28), P(3.5f, -28), P(3.5f, -12), P(-3.5f, -12), grip);
        p.Quad(P(-3.5f, -23), P(3.5f, -23), P(3.5f, -21), P(-3.5f, -21), gripD);
        p.Quad(P(-3.5f, -17), P(3.5f, -17), P(3.5f, -15), P(-3.5f, -15), gripD);
        // pommel
        p.Quad(P(-5, -12), P(5, -12), P(5, -7), P(-5, -7), guard);
    }

    static constexpr float kMinSeconds = 3.0f;

    Services* s_ = nullptr;
    float t_ = 0;
    bool switched_ = false;
};

} // namespace

std::unique_ptr<IScene> CreateLoadingScene() { return std::make_unique<LoadingScene>(); }

} // namespace cs
