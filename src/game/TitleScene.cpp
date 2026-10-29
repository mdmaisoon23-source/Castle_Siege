#include <algorithm>
#include <cmath>
#include "../engine/Engine.h"
#include "../engine/Gfx.h"

namespace cs {
namespace {

constexpr float kCanvasW = 1280.f, kCanvasH = 720.f;

class TitleScene final : public IScene {
public:
    void OnEnter(Services& s) override {
        s_ = &s;
        s.audio->LoadBankAsync("Title.bank");
    }

    void Update(float dt, const InputState& in) override {
        t_ += dt;
        mx_ = in.mouseX / kCanvasW - 0.5f;
        my_ = in.mouseY / kCanvasH - 0.5f;

        if (!musicStarted_ && s_->audio->IsBankLoaded("Title.bank")) {
            s_->audio->PlayEvent("event:/Music/Title");
            musicStarted_ = true;
        }

        playRect_ = { 540, 610, 200, 70 };
        playHot_ = in.mouseX >= playRect_.x && in.mouseX <= playRect_.x + playRect_.w &&
                   in.mouseY >= playRect_.y && in.mouseY <= playRect_.y + playRect_.h;
        if (playHot_ && in.mouseReleased) OnPlayPressed();
    }

    void Render() override {
        auto& r = *s_->render;
        const Rect full{0, 0, 1, 1};
        gfx::Painter p = gfx::MakePainter(*s_);

        // ---- Background: one full-scene image scaled to COVER the window, tiny parallax.
        {
            TextureHandle tex = s_->assets->LoadTexture(kBg);
            bool placeholder = tex == s_->assets->White();
            Vec2 sz = s_->assets->TextureSize(kBg);
            float iw = sz.x > 0 ? sz.x : kCanvasW;
            float ih = sz.y > 0 ? sz.y : kCanvasH;
            float scale = std::max(kCanvasW / iw, kCanvasH / ih) * 1.04f;
            float w = iw * scale, h = ih * scale;
            float x = (kCanvasW - w) * 0.5f - mx_ * (w - kCanvasW) * 0.8f;
            float y = (kCanvasH - h) * 0.5f - my_ * (h - kCanvasH) * 0.8f;
            r.DrawSprite(tex, { x, y, w, h }, full,
                         placeholder ? Color{0.35f, 0.45f, 0.55f, 1} : Color{1, 1, 1, 1});
        }

        // ---- PLAY button: dark border, green body with a lighter top half, white "PLAY" text.
        {
            float k = playHot_ ? 1.06f : 1.0f;
            Rect d{ playRect_.x - playRect_.w * (k - 1) / 2, playRect_.y - playRect_.h * (k - 1) / 2,
                    playRect_.w * k, playRect_.h * k };
            Color base = playHot_ ? gfx::RGB(0.30f, 0.72f, 0.28f) : gfx::RGB(0.22f, 0.60f, 0.22f);
            p.Fill(d.x - 4, d.y - 4, d.w + 8, d.h + 8, gfx::RGB(0.08f, 0.10f, 0.08f));      // border
            p.Fill(d.x, d.y, d.w, d.h, gfx::Mul(base, 0.8f));                                // body
            p.Fill(d.x, d.y, d.w, d.h * 0.5f, base);                                          // highlight
            p.Fill(d.x, d.y + d.h - 5, d.w, 5, gfx::Mul(base, 0.6f));                         // bottom edge
            gfx::DrawTextCentered(p, d.x + d.w / 2, d.y + d.h / 2 - 15.f, 42, "PLAY", gfx::RGB(1, 1, 1));
        }
    }

private:
    void OnPlayPressed() {
        s_->changeScene(CreateLoadingScene());
    }

    static constexpr const char* kBg = "title/bg_battlefield.png";

    Services* s_ = nullptr;
    Rect playRect_{};
    bool playHot_ = false, musicStarted_ = false;
    float t_ = 0, mx_ = 0, my_ = 0;
};

} // namespace

std::unique_ptr<IScene> CreateTitleScene() { return std::make_unique<TitleScene>(); }

} // namespace cs
