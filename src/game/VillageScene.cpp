#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "../engine/Engine.h"
#include "../engine/Gfx.h"

namespace cs {
namespace {

using gfx::RGB;
using gfx::Mul;
using gfx::Alpha;
using gfx::LerpV;

constexpr float kCanvasW = 1280.f, kCanvasH = 720.f;

// Isometric walled castle under siege + in-game HUD.
// All art is original and drawn procedurally (brick walls, round towers, timber houses, windmill, forge,
// arrows, smoke, fire, marching soldiers). Swap in your own sprites later; the scene logic stays the same.
class VillageScene final : public IScene {
public:
    void OnEnter(Services& s) override { s_ = &s; }

    void Update(float dt, const InputState& in) override {
        t_ += dt;
        timeLeft_ = std::max(0.f, timeLeft_ - dt * (fast_ ? 3.f : 1.f));
        if (fast_) t_ += dt * 1.0f;   // world animates faster too

        closeRect_ = { 22, 622, 78, 78 };
        fastRect_  = { 1180, 622, 78, 78 };
        closeHot_ = Hit(closeRect_, in);
        fastHot_  = Hit(fastRect_, in);

        if (in.mouseReleased) {
            if (closeHot_) s_->changeScene(CreateTitleScene());
            else if (fastHot_) fast_ = !fast_;
        }
    }

    void Render() override {
        gfx::Painter p = gfx::MakePainter(*s_);
        p.ox = kOX; p.oy = kOY; p.tw = kTW; p.th = kTH;

        DrawGround(p);

        // ---- depth-sorted world objects (back to front)
        items_.clear();
        const char* const* L = Layout();
        for (int gy = 0; gy < N; ++gy)
            for (int gx = 0; gx < N; ++gx) {
                char c = L[gy][gx];
                if (c == '+' || c == '.') {
                    // sparse decoration on empty grass
                    float h = gfx::Hash01(gx * 31 + gy * 17);
                    if (c == '.' && h > 0.82f) items_.push_back({ gx + gy + 1.f, [=, this, &p] { Prop(p, gx, gy, h); } });
                    continue;
                }
                items_.push_back({ gx + gy + 1.f, [=, this, &p] { DrawTile(p, c, gx, gy); } });
            }
        AddTrees(p);
        AddUnits(p);
        std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
        for (auto& it : items_) it.fn();

        DrawBattleFx(p);
        DrawHud(p);
    }

private:
    struct Item { float depth; std::function<void()> fn; };

    static bool Hit(const Rect& r, const InputState& in) {
        return in.mouseX >= r.x && in.mouseX <= r.x + r.w && in.mouseY >= r.y && in.mouseY <= r.y + r.h;
    }

    // ------------------------------------------------------------ layout
    static constexpr int   N = 11;
    static constexpr float kTW = 92.f, kTH = 46.f, kOX = 640.f, kOY = 168.f;

    // '.' grass '+' road 'G' gate 'W' wall 'T' corner tower 't' small tower 'K' keep
    // 'H' house 'S' smithy 'M' windmill 'F' farm 'Q' quarry 'B' watch tower
    static const char* const* Layout() {
        static const char* rows[N] = {
            "TWtWWGWWtWT",
            "W....+....W",
            "tH.F.+..S.t",
            "W..F.+.B..W",
            "W....+....W",
            "G++++K++++G",
            "W....+....W",
            "W.B..+..F.W",
            "tM...+..H.t",
            "W..Q.+....W",
            "TWtWWGWWtWT",
        };
        return rows;
    }

    // palette
    Color stoneTop()  const { return RGB(0.88f, 0.84f, 0.74f); }
    Color stoneL()    const { return RGB(0.77f, 0.72f, 0.61f); }
    Color stoneR()    const { return RGB(0.57f, 0.52f, 0.44f); }
    Color mortar()    const { return RGB(0.34f, 0.30f, 0.24f, 0.55f); }

    // ------------------------------------------------------------ ground
    void DrawGround(gfx::Painter& p) {
        p.VGrad(0, 0, kCanvasW, kCanvasH, RGB(0.36f, 0.65f, 0.21f), RGB(0.47f, 0.76f, 0.27f));

        // grass tufts (deterministic)
        for (int i = 0; i < 320; ++i) {
            float x = gfx::Hash01(i * 3) * kCanvasW, y = gfx::Hash01(i * 3 + 1) * kCanvasH;
            float sway = std::sin(t_ * 1.6f + i) * 1.5f;
            Color c = (i & 1) ? RGB(0.30f, 0.56f, 0.16f, 0.65f) : RGB(0.58f, 0.82f, 0.36f, 0.6f);
            p.Tri({ x - 3, y }, { x + 3, y }, { x + sway, y - 7 }, c);
            p.Tri({ x + 2, y }, { x + 7, y }, { x + 5 + sway, y - 5 }, c);
        }

        // dirt road leading out of each gate
        const Color sand = RGB(0.82f, 0.73f, 0.52f);
        for (int k = 1; k <= 4; ++k) {
            float a = 5.f, b = 6.f;
            p.Quad(p.S(a, -k), p.S(b, -k), p.S(b, -k + 1), p.S(a, -k + 1), Mul(sand, 0.98f - 0.03f * k));
            p.Quad(p.S(a, N - 1 + k), p.S(b, N - 1 + k), p.S(b, N + k), p.S(a, N + k), Mul(sand, 0.98f - 0.03f * k));
            p.Quad(p.S(-k, a), p.S(-k + 1, a), p.S(-k + 1, b), p.S(-k, b), Mul(sand, 0.98f - 0.03f * k));
            p.Quad(p.S(N - 1 + k, a), p.S(N + k, a), p.S(N + k, b), p.S(N - 1 + k, b), Mul(sand, 0.98f - 0.03f * k));
        }

        // raised plateau under the castle
        p.Quad(p.S(-0.7f, -0.7f), p.S(N + 0.7f, -0.7f), p.S(N + 0.7f, N + 0.7f), p.S(-0.7f, N + 0.7f), RGB(0.30f, 0.52f, 0.17f));
        p.Quad(p.S(-0.4f, -0.4f), p.S(N + 0.4f, -0.4f), p.S(N + 0.4f, N + 0.4f), p.S(-0.4f, N + 0.4f), RGB(0.45f, 0.72f, 0.25f));

        const char* const* L = Layout();
        for (int gy = 0; gy < N; ++gy)
            for (int gx = 0; gx < N; ++gx) {
                char ch = L[gy][gx];
                float v = gfx::Hash01(gx * 7 + gy * 131);
                Color col = ((gx + gy) & 1) ? RGB(0.49f, 0.78f, 0.29f) : RGB(0.45f, 0.74f, 0.26f);
                if (ch == '+' || ch == 'G' || ch == 'K') col = Mul(RGB(0.82f, 0.73f, 0.52f), 0.92f + 0.08f * v);
                if (ch == 'F') col = RGB(0.58f, 0.40f, 0.21f);
                if (ch == 'Q') col = RGB(0.62f, 0.60f, 0.55f);
                p.Quad(p.S((float)gx, (float)gy), p.S(gx + 1.f, (float)gy), p.S(gx + 1.f, gy + 1.f), p.S((float)gx, gy + 1.f), col);
                if (ch == '+' || ch == 'G')   // pebbles
                    for (int k = 0; k < 3; ++k) {
                        Vec2 c = p.S(gx + 0.2f + 0.6f * gfx::Hash01(gx * 5 + gy * 3 + k), gy + 0.2f + 0.6f * gfx::Hash01(gx * 11 + gy * 7 + k));
                        p.Ellipse(c.x, c.y, 3, 1.5f, RGB(0.62f, 0.54f, 0.38f), 6);
                    }
            }

        // drifting cloud shadows
        for (int i = 0; i < 3; ++i) {
            float x = std::fmod(t_ * (14.f + i * 5.f) + i * 480.f, kCanvasW + 600.f) - 300.f;
            p.Ellipse(x, 170.f + i * 210.f, 220, 70, RGB(0.05f, 0.18f, 0.05f, 0.10f), 24);
        }
    }

    // ------------------------------------------------------------ tiles
    void DrawTile(gfx::Painter& p, char c, int gx, int gy) {
        switch (c) {
        case 'W': Wall(p, gx, gy); break;
        case 'T': RoundTower(p, gx, gy, 0.62f, 58.f, 40.f, RGB(0.80f, 0.28f, 0.14f)); break;
        case 't': RoundTower(p, gx, gy, 0.42f, 40.f, 30.f, RGB(0.62f, 0.20f, 0.12f)); break;
        case 'G': Gate(p, gx, gy); break;
        case 'K': Keep(p, gx, gy); break;
        case 'H': House(p, gx, gy); break;
        case 'S': Smithy(p, gx, gy); break;
        case 'M': Windmill(p, gx, gy); break;
        case 'F': Farm(p, gx, gy); break;
        case 'Q': Quarry(p, gx, gy); break;
        case 'B': WatchTower(p, gx, gy); break;
        default: break;
        }
    }

    void Wall(gfx::Painter& p, int gx, int gy) {
        const bool horiz = (gy == 0 || gy == N - 1);
        const float H = 20.f;
        float x0, x1, y0, y1;
        if (horiz) { x0 = (float)gx; x1 = gx + 1.f; y0 = gy + 0.30f; y1 = gy + 0.70f; }
        else       { x0 = gx + 0.30f; x1 = gx + 0.70f; y0 = (float)gy; y1 = gy + 1.f; }
        p.Prism(x0, y0, x1, y1, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
        // battlements on both edges
        for (int edge = 0; edge < 2; ++edge)
            for (int k = 0; k < 3; ++k) {
                float u = 0.17f + 0.33f * k;
                float mx0, mx1, my0, my1;
                if (horiz) { mx0 = gx + u - 0.07f; mx1 = gx + u + 0.07f; my0 = edge ? y1 - 0.09f : y0; my1 = edge ? y1 : y0 + 0.09f; }
                else       { mx0 = edge ? x1 - 0.09f : x0; mx1 = edge ? x1 : x0 + 0.09f; my0 = gy + u - 0.07f; my1 = gy + u + 0.07f; }
                p.Prism(mx0, my0, mx1, my1, H, H + 6.f, stoneTop(), stoneL(), stoneR());
            }
        // burning sections on the siege-facing walls
        if ((gx == 3 && gy == N - 1) || (gx == N - 1 && gy == 3)) {
            Vec2 c = p.S(gx + 0.5f, gy + 0.5f, H + 4);
            Flame(p, c.x - 6, c.y, 1.1f);
            Flame(p, c.x + 8, c.y + 2, 0.8f);
            Smoke(p, c.x, c.y - 10, gx * 0.31f + gy * 0.17f, 1.3f);
        }
    }

    void RoundTower(gfx::Painter& p, int gx, int gy, float r, float h, float roofH, Color roof) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        float R = r * kTW * 0.7071f;
        p.Cylinder(c.x, c.y, R, 0, h, stoneL(), stoneTop(), true, mortar());
        // arrow slits
        for (int k = -1; k <= 1; ++k) {
            float a = kPiHalf + k * 0.7f;
            float sx = c.x + R * std::cos(a), sy = c.y - h * 0.55f + R * 0.5f * std::sin(a);
            p.Fill(sx - 2, sy - 6, 4, 11, RGB(0.10f, 0.08f, 0.07f));
        }
        // stone ring where roof meets the wall + cone roof with overhang
        p.Cone(c.x, c.y, R + 5, h, roofH, roof);
        // tiny pennant
        Flag(p, c.x, c.y - h - roofH, 16, RGB(0.20f, 0.35f, 0.85f), 0.f);
    }

    void Gate(gfx::Painter& p, int gx, int gy) {
        const float H = 36.f;
        float x0 = gx + 0.08f, x1 = gx + 0.92f, y0 = gy + 0.08f, y1 = gy + 0.92f;
        p.Prism(x0, y0, x1, y1, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
        // arched openings on both visible faces
        auto arch = [&](Vec2 A, Vec2 B) {
            auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - z }; };
            const Color dark = RGB(0.06f, 0.05f, 0.05f);
            p.Quad(at(0.25f, 0), at(0.75f, 0), at(0.75f, 15), at(0.25f, 15), dark);
            p.Quad(at(0.25f, 15), at(0.75f, 15), at(0.68f, 21), at(0.32f, 21), dark);
            p.Quad(at(0.32f, 21), at(0.68f, 21), at(0.55f, 24), at(0.45f, 24), dark);
            for (int k = 1; k < 6; ++k)    // portcullis bars
                p.Line(at(0.25f + 0.5f * k / 6, 0), at(0.25f + 0.5f * k / 6, 20), 1.4f, RGB(0.35f, 0.28f, 0.2f));
        };
        arch(p.S(x0, y1, 0), p.S(x1, y1, 0));
        arch(p.S(x1, y1, 0), p.S(x1, y0, 0));
        for (int k = 0; k < 4; ++k) {        // merlons on top
            float u = 0.18f + 0.22f * k;
            p.Prism(gx + u, y1 - 0.1f, gx + u + 0.12f, y1, H, H + 6, stoneTop(), stoneL(), stoneR());
        }
        Vec2 top = p.S(gx + 0.5f, gy + 0.5f, H + 6);
        Flag(p, top.x, top.y, 30, RGB(0.85f, 0.14f, 0.12f), (float)(gx + gy));
    }

    void Keep(gfx::Painter& p, int gx, int gy) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        p.Ellipse(c.x, c.y + 4, 74, 38, RGB(0.20f, 0.30f, 0.10f, 0.35f), 24);    // ground shadow
        p.Cylinder(c.x, c.y, 62, 0, 40, stoneL(), stoneTop(), true, mortar(), 20);
        p.Cylinder(c.x, c.y - 40 + 0, 47, 0, 40, Mul(stoneL(), 1.06f), stoneTop(), true, mortar(), 20);
        float topZ = 80.f;
        p.Merlons(c.x, c.y, 47, topZ, 12, stoneTop(), 9.f);
        // keep door
        p.Quad({ c.x - 11, c.y + 30 }, { c.x + 11, c.y + 30 }, { c.x + 11, c.y + 8 }, { c.x - 11, c.y + 8 }, RGB(0.20f, 0.12f, 0.07f));
        p.Quad({ c.x - 11, c.y + 8 }, { c.x + 11, c.y + 8 }, { c.x + 6, c.y + 1 }, { c.x - 6, c.y + 1 }, RGB(0.20f, 0.12f, 0.07f));
        // hanging blue banners with gold emblem
        for (int k = -1; k <= 1; k += 2) {
            float bx = c.x + k * 30.f, by = c.y - 40 + 26 + 0.5f * 0;
            float sway = std::sin(t_ * 2.5f + k) * 1.5f;
            p.Quad({ bx - 9, by - 22 }, { bx + 9, by - 22 }, { bx + 9 + sway, by + 14 }, { bx - 9 + sway, by + 14 }, RGB(0.18f, 0.32f, 0.78f));
            p.Tri({ bx - 9 + sway, by + 14 }, { bx + 9 + sway, by + 14 }, { bx + sway, by + 22 }, RGB(0.18f, 0.32f, 0.78f));
            p.Star(bx + sway * 0.5f, by - 4, 6.5f, RGB(0.98f, 0.80f, 0.20f));
        }
        // roofed turret + big flag on the keep
        p.Cylinder(c.x - 14, c.y - topZ - 4 + 2, 17, 0, 20, Mul(stoneL(), 1.1f), stoneTop(), true, mortar(), 12);
        p.Cone(c.x - 14, c.y - topZ - 4 + 2, 21, 20, 28, RGB(0.80f, 0.28f, 0.14f), 12);
        Flag(p, c.x - 14, c.y - topZ - 4 - 46, 30, RGB(0.85f, 0.14f, 0.12f), 1.3f);
    }

    void House(gfx::Painter& p, int gx, int gy) {
        float h = gfx::Hash01(gx * 3 + gy * 9);
        Color plaster = RGB(0.95f, 0.89f, 0.75f);
        p.Prism(gx + 0.16f, gy + 0.2f, gx + 0.84f, gy + 0.8f, 0, 17, plaster, plaster, Mul(plaster, 0.72f));
        Vec2 A = p.S(gx + 0.16f, gy + 0.8f, 0), B = p.S(gx + 0.84f, gy + 0.8f, 0);
        const Color wood = RGB(0.38f, 0.24f, 0.12f);
        for (float t : { 0.f, 0.5f, 1.f }) { Vec2 q = LerpV(A, B, t); p.Line(q, { q.x, q.y - 17 }, 2.2f, wood); }
        p.Line({ A.x, A.y - 8 }, { B.x, B.y - 8 }, 2.f, wood);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - z }; };
        p.Quad(at(0.12f, 0), at(0.36f, 0), at(0.36f, 10), at(0.12f, 10), RGB(0.30f, 0.18f, 0.09f));            // door
        p.Quad(at(0.60f, 8), at(0.86f, 8), at(0.86f, 14), at(0.60f, 14), RGB(1.0f, 0.88f + 0.06f * std::sin(t_ * 3 + h * 9), 0.50f)); // lit window
        Color roof = (h > 0.5f) ? RGB(0.84f, 0.32f, 0.16f) : RGB(0.36f, 0.48f, 0.78f);
        p.GableRoofX(gx + 0.10f, gy + 0.14f, gx + 0.90f, gy + 0.86f, 17, 15, roof, Mul(roof, 0.55f), plaster);
        // chimney + smoke
        p.Prism(gx + 0.62f, gy + 0.36f, gx + 0.72f, gy + 0.46f, 20, 34, stoneTop(), stoneL(), stoneR());
        Vec2 ch = p.S(gx + 0.67f, gy + 0.41f, 34);
        Smoke(p, ch.x, ch.y, h * 5, 0.9f);
    }

    void Smithy(gfx::Painter& p, int gx, int gy) {
        p.Prism(gx + 0.14f, gy + 0.2f, gx + 0.86f, gy + 0.8f, 0, 16, stoneTop(), stoneL(), stoneR(), true, mortar());
        Vec2 A = p.S(gx + 0.14f, gy + 0.8f, 0), B = p.S(gx + 0.86f, gy + 0.8f, 0);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - z }; };
        float fl = 0.75f + 0.25f * std::sin(t_ * 11.f);
        p.Quad(at(0.14f, 0), at(0.50f, 0), at(0.50f, 10), at(0.14f, 10), RGB(0.12f, 0.06f, 0.04f));
        p.Quad(at(0.20f, 0), at(0.44f, 0), at(0.44f, 6), at(0.20f, 6), RGB(1.0f, 0.45f + 0.3f * fl, 0.08f));
        p.Fill(at(0.62f, 0).x, at(0.62f, 0).y - 5, 10, 5, RGB(0.25f, 0.25f, 0.28f));      // anvil
        p.GableRoofX(gx + 0.08f, gy + 0.14f, gx + 0.92f, gy + 0.86f, 16, 14, RGB(0.33f, 0.36f, 0.44f), RGB(0.15f, 0.16f, 0.2f), stoneL());
        p.Prism(gx + 0.64f, gy + 0.3f, gx + 0.76f, gy + 0.42f, 18, 38, stoneTop(), stoneL(), stoneR(), true, mortar());
        Vec2 ch = p.S(gx + 0.70f, gy + 0.36f, 38);
        Smoke(p, ch.x, ch.y, 2.2f, 1.1f);
        // sparks
        for (int k = 0; k < 4; ++k) {
            float a = std::fmod(t_ * 1.6f + k * 0.25f, 1.f);
            p.Fill(at(0.3f, 4).x + (k - 1.5f) * 4 + a * 8, at(0.3f, 4).y - a * 18, 2, 2, RGB(1, 0.8f, 0.2f, 1 - a));
        }
    }

    void Windmill(gfx::Painter& p, int gx, int gy) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.55f);
        p.Cylinder(c.x, c.y, 24, 0, 44, RGB(0.92f, 0.86f, 0.72f), RGB(0.9f, 0.85f, 0.7f), false);
        p.Cone(c.x, c.y, 28, 44, 26, RGB(0.58f, 0.37f, 0.18f));
        Vec2 hub{ c.x + 4, c.y - 40 };
        p.Quad({ c.x - 5, c.y + 13 }, { c.x + 5, c.y + 13 }, { c.x + 5, c.y - 3 }, { c.x - 5, c.y - 3 }, RGB(0.30f, 0.18f, 0.09f));   // door
        for (int k = 0; k < 4; ++k) {
            float ang = t_ * 1.1f + k * kPiHalf;
            float dx = std::cos(ang), dy = std::sin(ang);
            p.RotRect(hub.x + dx * 21, hub.y + dy * 21, 42, 3.2f, ang, RGB(0.45f, 0.30f, 0.15f));
            p.RotRect(hub.x + dx * 27 - dy * 5, hub.y + dy * 27 + dx * 5, 22, 9, ang, RGB(0.96f, 0.93f, 0.84f));
        }
        p.Disc(hub.x, hub.y, 4.5f, RGB(0.28f, 0.18f, 0.1f));
    }

    void Farm(gfx::Painter& p, int gx, int gy) {
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 6; ++k) {
                Vec2 q = p.S(gx + 0.16f + r * 0.22f, gy + 0.1f + k * 0.15f);
                float sway = std::sin(t_ * 2.2f + r + k * 0.7f) * 1.6f;
                p.Tri({ q.x - 3, q.y }, { q.x + 3, q.y }, { q.x + sway, q.y - 9 }, RGB(0.25f, 0.60f, 0.15f));
                p.Tri({ q.x - 1, q.y }, { q.x + 5, q.y }, { q.x + 3 + sway, q.y - 7 }, RGB(0.42f, 0.75f, 0.22f));
            }
        Vec2 s = p.S(gx + 0.88f, gy + 0.15f);   // scarecrow
        p.Line({ s.x, s.y }, { s.x, s.y - 22 }, 2.f, RGB(0.4f, 0.26f, 0.12f));
        p.Line({ s.x - 9, s.y - 16 }, { s.x + 9, s.y - 16 }, 2.f, RGB(0.4f, 0.26f, 0.12f));
        p.Disc(s.x, s.y - 24, 4, RGB(0.9f, 0.75f, 0.4f));
        p.Tri({ s.x - 6, s.y - 26 }, { s.x + 6, s.y - 26 }, { s.x, s.y - 34 }, RGB(0.55f, 0.35f, 0.15f));
    }

    void Quarry(gfx::Painter& p, int gx, int gy) {
        const Color rockTop = RGB(0.80f, 0.82f, 0.86f), rockL = RGB(0.62f, 0.64f, 0.70f), rockR = RGB(0.46f, 0.48f, 0.54f);
        p.Prism(gx + 0.55f, gy + 0.10f, gx + 0.92f, gy + 0.45f, 0, 16, rockTop, rockL, rockR);
        p.Prism(gx + 0.12f, gy + 0.15f, gx + 0.42f, gy + 0.42f, 0, 10, rockTop, rockL, rockR);
        p.Prism(gx + 0.15f, gy + 0.55f, gx + 0.45f, gy + 0.85f, 0, 7, RGB(0.92f, 0.88f, 0.78f), RGB(0.80f, 0.75f, 0.64f), RGB(0.6f, 0.55f, 0.46f), true, mortar());
        p.Prism(gx + 0.60f, gy + 0.62f, gx + 0.90f, gy + 0.9f, 0, 7, RGB(0.92f, 0.88f, 0.78f), RGB(0.80f, 0.75f, 0.64f), RGB(0.6f, 0.55f, 0.46f), true, mortar());
        Vec2 w = p.S(gx + 0.5f, gy + 0.55f);        // worker swinging a pick
        float swing = std::sin(t_ * 5.f);
        p.Figure(w.x, w.y, 1.2f, RGB(0.55f, 0.35f, 0.18f), RGB(0.95f, 0.8f, 0.65f), 0.f);
        p.Line({ w.x + 4, w.y - 14 }, { w.x + 12, w.y - 18 + swing * 8 }, 2.f, RGB(0.4f, 0.28f, 0.15f));
        p.Line({ w.x + 9, w.y - 21 + swing * 8 }, { w.x + 15, w.y - 15 + swing * 8 }, 2.f, RGB(0.7f, 0.7f, 0.75f));
    }

    void WatchTower(gfx::Painter& p, int gx, int gy) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        float R = 0.30f * kTW * 0.7071f;
        p.Cylinder(c.x, c.y, R, 0, 46, stoneL(), stoneTop(), true, mortar());
        p.Cone(c.x, c.y, R + 5, 46, 28, RGB(0.30f, 0.45f, 0.82f));
        p.Fill(c.x - 2, c.y + 2 - 28, 4, 9, RGB(0.10f, 0.08f, 0.07f));
        Flag(p, c.x, c.y - 74, 14, RGB(0.95f, 0.82f, 0.22f), gx * 0.7f);
    }

    void Prop(gfx::Painter& p, int gx, int gy, float h) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        if (h < 0.88f) {           // barrel
            p.Cylinder(c.x, c.y, 7, 0, 10, RGB(0.62f, 0.40f, 0.18f), RGB(0.75f, 0.52f, 0.28f), false);
            p.Line({ c.x - 7, c.y - 3 }, { c.x + 7, c.y - 3 }, 1.4f, RGB(0.25f, 0.2f, 0.18f));
        } else if (h < 0.94f) {    // crates
            p.Prism(gx + 0.3f, gy + 0.3f, gx + 0.6f, gy + 0.6f, 0, 9, RGB(0.80f, 0.62f, 0.35f), RGB(0.68f, 0.50f, 0.26f), RGB(0.50f, 0.36f, 0.18f));
        } else {                   // bush
            p.Ellipse(c.x, c.y + 3, 14, 6, RGB(0.1f, 0.25f, 0.05f, 0.3f), 12);
            p.Disc(c.x - 5, c.y - 4, 9, RGB(0.18f, 0.48f, 0.15f));
            p.Disc(c.x + 5, c.y - 3, 8, RGB(0.24f, 0.58f, 0.19f));
            p.Disc(c.x, c.y - 9, 8, RGB(0.30f, 0.66f, 0.22f));
        }
    }

    // ------------------------------------------------------------ trees, units
    void AddTrees(gfx::Painter& p) {
        for (int i = 0; i < 90 && treeCount_ < 34; ++i) {
            float gx = -5.f + gfx::Hash01(i * 5 + 1) * 21.f, gy = -4.f + gfx::Hash01(i * 5 + 2) * 20.f;
            bool inCastle = gx > -1.0f && gx < N + 1.0f && gy > -1.0f && gy < N + 1.0f;
            bool onRoad = (std::fabs(gx - 5.5f) < 1.4f && (gy < 0 || gy > N)) || (std::fabs(gy - 5.5f) < 1.4f && (gx < 0 || gx > N));
            if (inCastle || onRoad) continue;
            Vec2 sp = p.S(gx, gy);
            if (sp.x < 10 || sp.x > kCanvasW - 10 || sp.y < 150 || sp.y > kCanvasH - 10) continue;
            // keep trees out of the corner HUD areas
            if ((sp.x < 340 && sp.y < 320) || (sp.x > 940 && sp.y < 320)) continue;
            treeCount_++;
            float seed = (float)i;
            items_.push_back({ gx + gy, [=, this, &p] { Tree(p, sp.x, sp.y, seed); } });
        }
        treeCount_ = 0;
    }

    void Tree(gfx::Painter& p, float x, float y, float seed) {
        float sway = std::sin(t_ * 1.4f + seed) * 2.f;
        float s = 0.9f + 0.4f * gfx::Hash01((int)seed * 3);
        p.Ellipse(x + 6, y + 3, 26 * s, 9 * s, RGB(0.05f, 0.15f, 0.03f, 0.30f), 14);
        p.Fill(x - 4 * s, y - 20 * s, 8 * s, 22 * s, RGB(0.40f, 0.26f, 0.12f));
        p.Fill(x + 1 * s, y - 20 * s, 3 * s, 22 * s, RGB(0.30f, 0.19f, 0.09f));
        p.Disc(x + sway * 0.3f, y - 32 * s, 26 * s, RGB(0.13f, 0.40f, 0.12f));
        p.Disc(x - 8 * s + sway * 0.6f, y - 40 * s, 19 * s, RGB(0.19f, 0.50f, 0.16f));
        p.Disc(x + 8 * s + sway * 0.6f, y - 42 * s, 17 * s, RGB(0.23f, 0.56f, 0.19f));
        p.Disc(x - 3 * s + sway, y - 50 * s, 13 * s, RGB(0.32f, 0.68f, 0.24f));
    }

    void AddUnits(gfx::Painter& p) {
        // defenders marching along the two main roads
        for (int i = 0; i < 10; ++i) {
            float u = std::fmod(t_ * 0.07f + i * 0.1f, 1.f);
            float tri = u < 0.5f ? u * 2 : 2 - u * 2;                 // ping-pong 0..1
            float pos = 1.2f + tri * (N - 2.4f);
            bool vertical = (i & 1) == 0;
            float gx = vertical ? 5.5f + ((i % 4) < 2 ? -0.12f : 0.12f) : pos;
            float gy = vertical ? pos : 5.5f + ((i % 4) < 2 ? -0.12f : 0.12f);
            if (std::fabs((vertical ? gy : gx) - 5.5f) < 0.9f) continue;   // avoid walking through the keep
            bool back = u >= 0.5f;
            Vec2 sp = p.S(gx, gy);
            Color body = (i % 3 == 0) ? RGB(0.20f, 0.38f, 0.92f) : RGB(0.95f, 0.75f, 0.20f);
            float ph = t_ * 9.f + i;
            items_.push_back({ gx + gy, [=, this, &p] { p.Figure(sp.x, sp.y, 1.15f, body, RGB(0.95f, 0.8f, 0.65f), ph, back); } });
        }
        // attackers running at the three gates
        struct Lane { float x0, y0, x1, y1; };
        static const Lane lanes[6] = {
            { -7.f, 5.2f, -0.6f, 5.2f }, { -8.f, 6.0f, -0.6f, 6.0f }, { 4.8f, 16.f, 4.8f, 10.7f },
            { 6.2f, 17.f, 6.2f, 10.7f }, { 16.f, 4.8f, 10.7f, 4.8f }, { 17.f, 6.0f, 10.7f, 6.0f },
        };
        for (int k = 0; k < 6; ++k)
            for (int m = 0; m < 3; ++m) {
                float u = std::fmod(t_ * 0.075f + k * 0.13f + m * 0.31f, 1.f);
                float gx = lanes[k].x0 + (lanes[k].x1 - lanes[k].x0) * std::min(u / 0.9f, 1.f);
                float gy = lanes[k].y0 + (lanes[k].y1 - lanes[k].y0) * std::min(u / 0.9f, 1.f) + (m - 1) * 0.25f;
                Vec2 sp = p.S(gx, gy);
                if (sp.x < -20 || sp.x > kCanvasW + 20 || sp.y > kCanvasH + 20 || sp.y < 120) continue;
                bool dead = u > 0.9f;
                float ph = t_ * 11.f + k * 2 + m;
                items_.push_back({ gx + gy, [=, this, &p] {
                    if (!dead) p.Figure(sp.x, sp.y, 1.1f, RGB(0.82f, 0.14f, 0.14f), RGB(0.9f, 0.72f, 0.6f), ph, true);
                    else {
                        p.RotRect(sp.x, sp.y - 3, 16, 7, 0.1f, RGB(0.6f, 0.1f, 0.1f));
                        p.Disc(sp.x + 9, sp.y - 3, 3.2f, RGB(0.9f, 0.72f, 0.6f));
                    }
                } });
            }
    }

    // ------------------------------------------------------------ animated effects
    static constexpr float kPiHalf = 1.5707963f;

    void Flame(gfx::Painter& p, float x, float y, float s) {
        for (int k = 0; k < 3; ++k) {
            float h = (16 + 9 * std::sin(t_ * 14.f + k * 2.1f)) * s, w = (7 - k * 1.8f) * s, off = (k - 1) * 3.f * s;
            Color c = k == 0 ? RGB(1.0f, 0.32f, 0.05f) : (k == 1 ? RGB(1.0f, 0.62f, 0.10f) : RGB(1.0f, 0.95f, 0.45f));
            p.Tri({ x - w + off, y }, { x + w + off, y }, { x + std::sin(t_ * 9.f + k) * 2.f * s + off, y - h }, c);
        }
    }

    void Smoke(gfx::Painter& p, float x, float y, float seed, float size) {
        for (int i = 0; i < 6; ++i) {
            float a = std::fmod(t_ * 0.45f + seed + i / 6.f, 1.f);
            float xx = x + std::sin(a * 6.f + seed * 9.f) * 7.f * size + a * 16.f * size;
            float yy = y - a * 58.f * size;
            p.Disc(xx, yy, (4.f + a * 13.f) * size, RGB(0.38f, 0.38f, 0.42f, (1.f - a) * 0.5f));
        }
    }

    void Flag(gfx::Painter& p, float x, float y, float h, Color col, float seed) {
        p.Line({ x, y }, { x, y - h }, 2.2f, RGB(0.32f, 0.22f, 0.12f));
        float prevTop = y - h, prevBot = y - h + 11;
        for (int i = 1; i <= 4; ++i) {
            float wave = std::sin(t_ * 6.f + i * 0.9f + seed) * 2.5f * i / 4.f;
            float nt = y - h + wave + i * 0.8f, nb = y - h + 11 + wave + i * 0.8f;
            p.Quad({ x + (i - 1) * 6.f, prevTop }, { x + i * 6.f, nt }, { x + i * 6.f, nb }, { x + (i - 1) * 6.f, prevBot },
                   gfx::Mul(col, 0.85f + 0.15f * (i & 1)));
            prevTop = nt; prevBot = nb;
        }
    }

    // Arrows / cannonballs flying out of the corner towers and dust where they land.
    void DrawBattleFx(gfx::Painter& p) {
        static const Vec2 start[6] = { {0.5f, 0.5f}, {0.5f, 10.5f}, {0.5f, 10.5f}, {10.5f, 10.5f}, {10.5f, 0.5f}, {10.5f, 10.5f} };
        static const Vec2 target[6] = { {-2.2f, 4.6f}, {-2.8f, 6.4f}, {6.0f, 12.8f}, {4.7f, 13.4f}, {12.5f, 4.7f}, {13.2f, 6.5f} };
        for (int i = 0; i < 6; ++i) {
            float c = std::fmod(t_ * 0.9f + i * 0.53f, 3.2f);
            Vec2 a = p.S(start[i].x, start[i].y, 70.f), b = p.S(target[i].x, target[i].y, 0.f);
            if (c < 1.1f) {
                float u = c / 1.1f;
                Vec2 pos = LerpV(a, b, u); pos.y -= std::sin(u * gfx::kPi) * 95.f;
                float u2 = std::min(1.f, u + 0.03f);
                Vec2 nxt = LerpV(a, b, u2); nxt.y -= std::sin(u2 * gfx::kPi) * 95.f;
                p.Line({ pos.x, pos.y }, { nxt.x, nxt.y }, 2.4f, RGB(0.25f, 0.18f, 0.1f));
                p.Disc(nxt.x, nxt.y, 2.8f, RGB(0.15f, 0.15f, 0.17f));
                p.Line({ pos.x, pos.y }, { pos.x - (nxt.x - pos.x) * 3, pos.y - (nxt.y - pos.y) * 3 }, 1.2f, RGB(1, 1, 1, 0.35f));
            } else if (c < 1.9f) {
                float age = (c - 1.1f) / 0.8f;
                for (int k = 0; k < 5; ++k) {
                    float a2 = k * 1.256f + i;
                    p.Disc(b.x + std::cos(a2) * age * 16, b.y - age * 12 + std::sin(a2) * age * 6, 5 + age * 12, RGB(0.55f, 0.47f, 0.34f, (1 - age) * 0.55f));
                }
                if (age < 0.35f) p.Star(b.x, b.y - 4, 16 * (1 - age), RGB(1.0f, 0.85f, 0.3f, 1 - age * 2));
            }
        }
    }

    // ------------------------------------------------------------ HUD
    enum Icon { kApple, kWood, kStone };

    void DrawIcon(gfx::Painter& p, float x, float y, Icon ic) {
        p.Ellipse(x + 2, y + 18, 17, 5, RGB(0, 0, 0, 0.3f), 12);
        if (ic == kApple) {
            p.Disc(x - 5, y + 2, 14, RGB(0.70f, 0.06f, 0.06f));
            p.Disc(x + 5, y + 2, 14, RGB(0.70f, 0.06f, 0.06f));
            p.Disc(x - 4, y + 1, 12, RGB(0.90f, 0.14f, 0.12f));
            p.Disc(x + 5, y + 1, 12, RGB(0.90f, 0.14f, 0.12f));
            p.Ellipse(x - 8, y - 5, 4, 6, RGB(1, 1, 1, 0.5f), 8);
            p.Line({ x, y - 8 }, { x + 2, y - 19 }, 3.f, RGB(0.35f, 0.22f, 0.10f));
            p.Quad({ x + 3, y - 16 }, { x + 15, y - 22 }, { x + 13, y - 12 }, { x + 4, y - 11 }, RGB(0.28f, 0.68f, 0.22f));
        } else if (ic == kWood) {
            for (int k = 0; k < 2; ++k) {
                float yy = y + k * 9 - 5;
                p.RotRect(x, yy, 38, 13, -0.12f, RGB(0.78f, 0.46f, 0.12f));
                p.RotRect(x, yy - 3, 38, 4, -0.12f, RGB(0.96f, 0.68f, 0.24f));
                p.Ellipse(x + 18, yy - 2, 6, 7, RGB(0.95f, 0.80f, 0.50f), 10);
                p.Ellipse(x + 18, yy - 2, 3, 4, RGB(0.70f, 0.45f, 0.18f), 8);
            }
        } else {
            p.Quad({ x - 18, y + 8 }, { x - 14, y - 8 }, { x + 4, y - 14 }, { x + 18, y - 2 }, RGB(0.66f, 0.72f, 0.82f));
            p.Quad({ x - 18, y + 8 }, { x + 18, y - 2 }, { x + 15, y + 12 }, { x - 12, y + 14 }, RGB(0.42f, 0.48f, 0.60f));
            p.Quad({ x - 12, y - 4 }, { x - 4, y - 10 }, { x + 2, y - 6 }, { x - 6, y + 2 }, RGB(0.88f, 0.92f, 0.98f));
        }
    }

    void ResourceBar(gfx::Painter& p, float x, float y, float w, float frac, Color top, Color bot, Color back,
                     const std::string& value, const std::string& maxText, bool right, Icon icon) {
        const float h = 30.f;
        float bx = right ? x : x + 30;
        // "Max: n" label
        std::string mt = "Max: " + maxText;
        float mw = p.TextWidth(mt, 15);
        gfx::DrawTextOutlined(p, right ? bx + 6 : bx + w - mw - 4, y - 13, 15, mt, RGB(1, 1, 1));
        // frame, back, fill
        p.Fill(bx - 3, y - 3, w + 6, h + 6, RGB(0.08f, 0.08f, 0.10f));
        p.Fill(bx - 1.5f, y - 1.5f, w + 3, h + 3, RGB(0.70f, 0.72f, 0.78f));
        p.Fill(bx, y, w, h, back);
        float fw = w * std::clamp(frac, 0.f, 1.f);
        p.VGrad(bx, y, fw, h, top, bot);
        p.Fill(bx, y, fw, h * 0.38f, Alpha(RGB(1, 1, 1), 0.25f));
        // value
        float vw = p.TextWidth(value, 23);
        gfx::DrawTextOutlined(p, right ? bx + w - vw - 46 : bx + 46, y + 7.f, 23, value, RGB(1, 1, 1));
        DrawIcon(p, right ? bx + w + 6 : x + 16, y + h / 2, icon);
    }

    void Badge(gfx::Painter& p, float x, float y, const std::string& text, bool crown, bool right) {
        float ix = right ? x - 18 : x + 18;
        if (!crown) {
            p.Quad({ ix - 15, y - 14 }, { ix + 15, y - 14 }, { ix + 15, y + 14 }, { ix - 15, y + 14 }, RGB(0.10f, 0.12f, 0.30f));
            p.Tri({ ix - 15, y + 14 }, { ix + 15, y + 14 }, { ix, y + 26 }, RGB(0.10f, 0.12f, 0.30f));
            p.Quad({ ix - 12.5f, y - 11.5f }, { ix + 12.5f, y - 11.5f }, { ix + 12.5f, y + 13 }, { ix - 12.5f, y + 13 }, RGB(0.18f, 0.34f, 0.85f));
            p.Tri({ ix - 12.5f, y + 13 }, { ix + 12.5f, y + 13 }, { ix, y + 22 }, RGB(0.18f, 0.34f, 0.85f));
            p.Disc(ix, y + 1, 9, RGB(0.98f, 0.76f, 0.15f));
            p.RotRect(ix, y + 1, 3.2f, 20, 0.75f, RGB(0.5f, 0.32f, 0.05f));
            p.RotRect(ix, y + 1, 3.2f, 20, -0.75f, RGB(0.5f, 0.32f, 0.05f));
        } else {
            Color g = RGB(0.99f, 0.78f, 0.14f), gd = RGB(0.80f, 0.55f, 0.05f);
            p.Quad({ ix - 16, y + 12 }, { ix + 16, y + 12 }, { ix + 16, y + 2 }, { ix - 16, y + 2 }, gd);
            p.Quad({ ix - 16, y + 2 }, { ix + 16, y + 2 }, { ix + 19, y - 15 }, { ix - 19, y - 15 }, g);
            p.Tri({ ix - 19, y - 15 }, { ix - 8, y - 2 }, { ix - 3, y - 15 }, RGB(0, 0, 0, 0.0f));
            for (int k = -1; k <= 1; ++k) p.Disc(ix + k * 10, y - 16, 3.4f, RGB(1.0f, 0.95f, 0.6f));
            p.Disc(ix, y + 5, 3.2f, RGB(0.85f, 0.15f, 0.2f));
        }
        float tw = p.TextWidth(text, 26);
        gfx::DrawTextOutlined(p, right ? ix - 26 - tw : ix + 26, y - 9, 26, text, RGB(1, 1, 1));
    }

    void DrawHud(gfx::Painter& p) {
        // left: our resources (full bars)
        ResourceBar(p, 14, 48, 236, 1.f, RGB(0.95f, 0.30f, 0.26f), RGB(0.70f, 0.10f, 0.10f), RGB(0.30f, 0.05f, 0.05f), gfx::FmtInt(20998), gfx::FmtInt(20998), false, kApple);
        ResourceBar(p, 14, 104, 236, 1.f, RGB(1.00f, 0.72f, 0.22f), RGB(0.85f, 0.45f, 0.06f), RGB(0.35f, 0.18f, 0.03f), gfx::FmtInt(13342), gfx::FmtInt(13342), false, kWood);
        ResourceBar(p, 14, 160, 236, 1.f, RGB(0.32f, 0.62f, 1.00f), RGB(0.12f, 0.36f, 0.82f), RGB(0.05f, 0.14f, 0.35f), gfx::FmtInt(6445), gfx::FmtInt(6445), false, kStone);
        Badge(p, 18, 218, "504", false, false);
        Badge(p, 18, 262, "19", true, false);

        // right: opponent resources (partly filled)
        float rx = kCanvasW - 14 - 236 - 30;
        const Color gt = RGB(0.45f, 0.80f, 0.32f), gb = RGB(0.20f, 0.52f, 0.14f), gk = RGB(0.07f, 0.20f, 0.06f);
        ResourceBar(p, rx, 48, 236, 46705.f / 267500.f, gt, gb, gk, gfx::FmtInt(46705), gfx::FmtInt(267500), true, kApple);
        ResourceBar(p, rx, 104, 236, 121789.f / 300000.f, gt, gb, gk, gfx::FmtInt(121789), gfx::FmtInt(300000), true, kWood);
        ResourceBar(p, rx, 160, 236, 56535.f / 170000.f, gt, gb, gk, gfx::FmtInt(56535), gfx::FmtInt(170000), true, kStone);
        Badge(p, kCanvasW - 18, 218, "4,597", false, true);
        Badge(p, kCanvasW - 18, 262, "2,408", true, true);

        // centre: stars + percent + countdown
        for (int i = 0; i < 3; ++i) {
            float sx = 590 + i * 50.f, sy = 32;
            p.Star(sx, sy + 1, 22, RGB(0.10f, 0.10f, 0.12f));
            p.Star(sx, sy, 19, RGB(0.70f, 0.72f, 0.76f));
            p.Star(sx - 1, sy - 2, 11, RGB(0.88f, 0.90f, 0.94f));
        }
        gfx::DrawTextCentered(p, 640, 66, 18, "0%", RGB(1, 1, 1));
        int secs = (int)timeLeft_;
        gfx::DrawTextCentered(p, 640, 94, 20, "Battle ends in:", RGB(1.0f, 0.95f, 0.75f));
        gfx::DrawTextCentered(p, 640, 124, 30, std::to_string(secs / 60) + "m " + std::to_string(secs % 60) + "s", RGB(1, 1, 1));

        // bottom buttons
        DrawStoneButton(p, closeRect_, closeHot_);
        float cx = closeRect_.x + closeRect_.w / 2, cy = closeRect_.y + closeRect_.h / 2;
        for (int pass = 0; pass < 2; ++pass) {
            float grow = pass == 0 ? 7.f : 0.f;
            Color c = pass == 0 ? RGB(0.30f, 0.04f, 0.04f) : RGB(0.92f, 0.14f, 0.12f);
            p.RotRect(cx, cy, 20 + grow, 54 + grow, 0.785f, c);
            p.RotRect(cx, cy, 20 + grow, 54 + grow, -0.785f, c);
        }
        p.RotRect(cx - 3, cy - 3, 6, 30, 0.785f, RGB(1, 0.6f, 0.55f, 0.6f));

        DrawStoneButton(p, fastRect_, fastHot_);
        float fx = fastRect_.x + 20, fy = fastRect_.y + fastRect_.h / 2;
        Color gold = fast_ ? RGB(1.0f, 0.97f, 0.55f) : RGB(0.99f, 0.78f, 0.14f);
        for (int pass = 0; pass < 2; ++pass) {
            float g = pass == 0 ? 5.f : 0.f;
            Color c = pass == 0 ? RGB(0.28f, 0.16f, 0.02f) : gold;
            p.Tri({ fx - g, fy - 24 - g }, { fx - g, fy + 24 + g }, { fx + 22 + g, fy }, c);
            p.Tri({ fx + 20 - g, fy - 24 - g }, { fx + 20 - g, fy + 24 + g }, { fx + 42 + g, fy }, c);
        }
    }

    void DrawStoneButton(gfx::Painter& p, const Rect& r, bool hot) {
        float k = hot ? 1.1f : 1.0f;
        float w = r.w * k, h = r.h * k, x = r.x - (w - r.w) / 2, y = r.y - (h - r.h) / 2;
        p.Ellipse(x + w / 2 + 3, y + h + 1, w * 0.55f, 8, RGB(0, 0, 0, 0.3f), 14);
        p.Bevel(x, y, w, h, hot ? RGB(0.78f, 0.80f, 0.85f) : RGB(0.64f, 0.66f, 0.72f));
    }

    Services* s_ = nullptr;
    float t_ = 0;
    float timeLeft_ = 5 * 60 + 37;
    bool fast_ = false, closeHot_ = false, fastHot_ = false;
    Rect closeRect_{}, fastRect_{};
    std::vector<Item> items_;
    int treeCount_ = 0;
};

} // namespace

std::unique_ptr<IScene> CreateVillageScene() { return std::make_unique<VillageScene>(); }

} // namespace cs
