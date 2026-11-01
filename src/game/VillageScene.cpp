#include <algorithm>
#include <cmath>
#include <cstring>
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
using gfx::Hash01;

constexpr float kCanvasW = 1280.f, kCanvasH = 720.f;
constexpr float kPiHalf = 1.5707963f;

// Dense walled castle + full attack interface (deployable troop / hero bar, live battle, loot, stars, result).
// All art is original and procedural. Replace with sprites later; the game logic stays the same.
class VillageScene final : public IScene {
public:
    void OnEnter(Services& s) override { s_ = &s; InitBattle(); }

    void Update(float dt, const InputState& in) override {
        mx_ = in.mouseX; my_ = in.mouseY;
        UpdateUi(in);
        float sdt = dt * (fast_ ? 2.f : 1.f);
        t_ += sdt;
        if (over_) endT_ += dt; else UpdateBattle(sdt, in);
        UpdateParticles(sdt);
        pctDisp_ += (Pct() - pctDisp_) * std::min(1.f, dt * 6.f);
    }

    void Render() override {
        gfx::Painter p = gfx::MakePainter(*s_);
        p.ox = kOX; p.oy = kOY; p.tw = kTW; p.th = kTH;

        DrawGround(p);

        items_.clear();
        for (int i = 0; i < (int)blds_.size(); ++i) {
            float d = blds_[i].gx + blds_[i].gy + (blds_[i].dead ? 0.3f : 1.f);
            items_.push_back({ d, [=, this, &p] { DrawBuilding(p, i); } });
        }
        for (auto& pr : props_) { auto q = pr; items_.push_back({ q.gx + q.gy + 1.f, [=, this, &p] { Prop(p, q.gx, q.gy, q.h); } }); }
        for (auto& tr : trees_) { auto q = tr; items_.push_back({ q.gx + q.gy, [=, this, &p] { Vec2 sp = p.S(q.gx, q.gy); Tree(p, sp.x, sp.y, q.seed); } }); }
        for (int i = 0; i < (int)defs_.size(); ++i) {
            const Def& d = defs_[i];
            if (!d.alive && d.deadT > 1.2f) continue;
            items_.push_back({ d.x + d.y + 0.4f, [=, this, &p] { DrawDefender(p, i); } });
        }
        for (int i = 0; i < (int)units_.size(); ++i) {
            const Unit& u = units_[i];
            if (!u.alive && u.deadT > 1.2f) continue;
            items_.push_back({ u.x + u.y + 0.45f, [=, this, &p] { DrawUnit(p, i); } });
        }
        std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
        for (auto& it : items_) it.fn();

        DrawBars(p);
        DrawProjectiles(p);
        DrawParticles(p);
        DrawCursor(p);
        DrawHud(p);
        DrawTroopBar(p);
        if (over_) DrawResult(p);
    }

private:
    // ============================================================ data
    struct Item { float depth; std::function<void()> fn; };
    struct Prp { float gx, gy, h; };
    struct Tre { float gx, gy, seed; };

    enum UT { kSword, kArcher, kKnight, kGiant, kCata, kKing, kMage, kUT };
    struct UStat { const char* name; float hp, dmg, range, speed, cd; int count; bool hero, prefDef; };
    static const UStat& St(int t) {
        static const UStat s[kUT] = {
            { "Swordsman", 70,  11, 0.9f, 1.7f, 0.80f, 24, false, false },
            { "Archer",    40,   9, 3.4f, 1.6f, 0.90f, 16, false, false },
            { "Knight",   160,  20, 0.9f, 2.8f, 0.90f,  8, false, false },
            { "Giant",    700,  34, 0.9f, 1.0f, 1.20f,  4, false, true  },
            { "Catapult", 160,  70, 6.0f, 0.8f, 2.60f,  3, false, false },
            { "King",    1100,  46, 1.0f, 1.8f, 0.70f,  1, true,  false },
            { "Mage",     650,  60, 4.2f, 1.6f, 1.10f,  1, true,  false },
        };
        return s[t];
    }
    struct Unit { int type = 0; float x = 0, y = 0, hp = 1, maxHp = 1, cd = 0, phase = 0, rage = 0, atk = 0, retarget = 0, deadT = 0, aimX = 1;
                  int target = -1; bool alive = true, flip = false; };
    struct Def  { float x = 0, y = 0, hp = 80, maxHp = 80, cd = 0, phase = 0, wx = 0, wy = 0, deadT = 0; bool alive = true, flip = false; };
    struct Bld  { char c = '.'; int gx = 0, gy = 0; float hp = 1, maxHp = 1, top = 30, cd = 0, aim = 0, flash = 0, deadT = 0; int def = 0; bool dead = false; };
    struct Proj { float x0, y0, z0, x1, y1, t = 0, dur = 1, delay = 0, arc = 0, dmg = 0, aoe = 0; int kind = 0, tb = -1, tu = -1; bool fromDef = false; };
    struct Part { float x, y, z, vx, vy, vz, life, maxLife, size; Color col; int kind; };

    static bool Hit(const Rect& r, const InputState& in) {
        return in.mouseX >= r.x && in.mouseX <= r.x + r.w && in.mouseY >= r.y && in.mouseY <= r.y + r.h;
    }
    static float Dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

    // ============================================================ layout
    static constexpr int   N = 14;
    static constexpr float kTW = 80.f, kTH = 42.f, kOX = 640.f, kOY = 75.f;
    static constexpr float kS = kTW / 92.f;                 // art scale (art was authored for 92px tiles)
    static float Z(float h) { return h * kS; }

    // W wall  T tower  G gate  K keep  C cathedral  R barracks  E stables  H house  S smithy  M windmill
    // F farm  Q quarry/storage  A ballista  X market  L lumber  D vault  k cannon  B watch tower  + road
    static const char* const* Layout() {
        static const char* rows[N] = {
            "..............",
            ".....TGWT.....",
            ".....W+HW.....",
            ".....W+ATWGWT.",
            ".TWGWT++HHCEW.",
            ".W++HHR+HHLXW.",
            ".WH++++K++++W.",
            ".W++++++++++G.",
            ".TWWT+HH+FFQW.",
            "....W+HH+MFFW.",
            "....WHHE+AHQW.",
            "....WkHH+HDkW.",
            "....TWWWGWWWT.",
            "..............",
        };
        return rows;
    }
    static char At(int x, int y) { return (x < 0 || y < 0 || x >= N || y >= N) ? '.' : Layout()[y][x]; }
    static bool WallLike(char c) { return c == 'W' || c == 'T' || c == 't' || c == 'G'; }

    static bool BInfo(char c, float& hp, float& top, int& def) {
        def = 0;
        switch (c) {
        case 'W': hp = 180; top = 26; return true;
        case 'T': hp = 650; top = 100; def = 1; return true;
        case 't': hp = 480; top = 70; def = 1; return true;
        case 'G': hp = 520; top = 48; return true;
        case 'K': hp = 3200; top = 130; return true;
        case 'C': hp = 700; top = 105; return true;
        case 'R': hp = 650; top = 60; return true;
        case 'E': hp = 450; top = 46; return true;
        case 'H': hp = 260; top = 52; return true;
        case 'S': hp = 420; top = 56; return true;
        case 'M': hp = 380; top = 78; return true;
        case 'F': hp = 220; top = 22; return true;
        case 'Q': hp = 300; top = 32; return true;
        case 'A': hp = 460; top = 70; def = 3; return true;
        case 'X': hp = 340; top = 40; return true;
        case 'L': hp = 320; top = 38; return true;
        case 'D': hp = 420; top = 42; return true;
        case 'k': hp = 500; top = 34; def = 2; return true;
        case 'B': hp = 480; top = 84; def = 1; return true;
        default: return false;
        }
    }

    Color stoneTop() const { return RGB(0.95f, 0.95f, 0.98f); }
    Color stoneL()   const { return RGB(0.86f, 0.80f, 0.68f); }
    Color stoneR()   const { return RGB(0.63f, 0.57f, 0.47f); }
    Color mortar()   const { return RGB(0.34f, 0.30f, 0.24f, 0.55f); }

    // ============================================================ setup / reset
    void InitBattle() {
        blds_.clear(); units_.clear(); defs_.clear(); projs_.clear(); parts_.clear();
        trees_.clear(); props_.clear(); roads_.clear();
        rng_ = 987654u;
        const char* const* L = Layout();

        // flood fill from the border: whatever it cannot reach is "inside the walls"
        bool out[N][N] = {};
        std::vector<std::pair<int, int>> st;
        for (int i = 0; i < N; ++i) { st.push_back({ i, 0 }); st.push_back({ i, N - 1 }); st.push_back({ 0, i }); st.push_back({ N - 1, i }); }
        while (!st.empty()) {
            auto [x, y] = st.back(); st.pop_back();
            if (x < 0 || y < 0 || x >= N || y >= N || out[y][x] || WallLike(L[y][x])) continue;
            out[y][x] = true;
            st.push_back({ x + 1, y }); st.push_back({ x - 1, y }); st.push_back({ x, y + 1 }); st.push_back({ x, y - 1 });
        }
        totalHp_ = 0; lostHp_ = 0;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                char c = L[y][x];
                inside_[y][x] = !out[y][x] && !WallLike(c);
                Bld b; b.c = c; b.gx = x; b.gy = y;
                if (BInfo(c, b.hp, b.top, b.def)) {
                    b.hp *= 1.35f; b.maxHp = b.hp; b.cd = Hash01(x * 9 + y) * 1.f; totalHp_ += b.hp;
                    blds_.push_back(b);
                } else if (inside_[y][x]) {
                    if (c == '+') roads_.push_back({ x + 0.5f, y + 0.5f });
                    else if (Hash01(x * 31 + y * 17) > 0.2f) props_.push_back({ (float)x, (float)y, Hash01(x * 13 + y * 5) });
                }
            }
        // trees on the open land around the castle
        for (int i = 0; i < 400 && (int)trees_.size() < 34; ++i) {
            float gx = -7.f + Hash01(i * 5 + 1) * 28.f, gy = -7.f + Hash01(i * 5 + 2) * 28.f;
            if (gx > -1.f && gx < N + 1.f && gy > -1.f && gy < N + 1.f) continue;
            Vec2 sp = { kOX + (gx - gy) * kTW / 2, kOY + (gx + gy) * kTH / 2 };
            if (sp.x < 30 || sp.x > kCanvasW - 30 || sp.y < 190 || sp.y > 560) continue;
            if ((sp.x < 290 && sp.y < 330) || (sp.x > 990 && sp.y < 330)) continue;
            trees_.push_back({ gx, gy, (float)i });
        }
        // defenders
        for (int i = 0; i < 10 && !roads_.empty(); ++i) {
            Def d; Vec2 r = roads_[(size_t)(Rnd() * roads_.size()) % roads_.size()];
            d.x = d.wx = r.x; d.y = d.wy = r.y; d.phase = Rnd() * 6; d.hp = d.maxHp = 80;
            defs_.push_back(d);
        }
        for (int i = 0; i < kUT; ++i) counts_[i] = St(i).count;
        heroId_[0] = heroId_[1] = -1; heroCd_[0] = heroCd_[1] = 0;
        sel_ = 0; started_ = false; over_ = false; endT_ = 0; idleT_ = 0; deployCd_ = 0; dragOK_ = false;
        timeLeft_ = 5 * 60 + 37; keepDead_ = false; pctDisp_ = 0; starT_[0] = starT_[1] = starT_[2] = -1.f;
        fast_ = false;
    }

    // ============================================================ UI input
    Rect SlotRect(int i) const { return { 260.f + i * 110.f, 616.f, 100.f, 98.f }; }

    bool OverUi(float x, float y) const {
        if (y >= 606.f) return true;
        if (x < 262 && y < 300) return true;
        if (x > 1018 && y < 300) return true;
        if (y < 150) return true;
        return false;
    }

    bool ValidDeploy(float sx, float sy) const {
        if (OverUi(sx, sy) || sx < 8 || sx > kCanvasW - 8) return false;
        Vec2 g = ToGrid(sx, sy);
        int cx = (int)std::floor(g.x), cy = (int)std::floor(g.y);
        if (cx >= 0 && cx < N && cy >= 0 && cy < N) {
            if (inside_[cy][cx]) return false;
            char c = At(cx, cy);
            if (c != '.' && c != '+') return false;
        }
        return true;
    }

    Vec2 ToGrid(float sx, float sy) const {
        float a = (sx - kOX) / (kTW / 2), b = (sy - kOY) / (kTH / 2);
        return { (a + b) * 0.5f, (b - a) * 0.5f };
    }

    void UpdateUi(const InputState& in) {
        closeRect_ = { 22, 622, 78, 78 };
        fastRect_  = { 1180, 622, 78, 78 };
        closeHot_ = Hit(closeRect_, in);
        fastHot_  = Hit(fastRect_, in);

        if (over_ && endT_ > 1.0f) {
            replayRect_ = { (kCanvasW - 520) / 2 + 40, (kCanvasH - 420) / 2 + 420 - 100, 200, 66 };
            homeRect_   = { (kCanvasW - 520) / 2 + 520 - 240, (kCanvasH - 420) / 2 + 420 - 100, 200, 66 };
            replayHot_ = Hit(replayRect_, in); homeHot_ = Hit(homeRect_, in);
            if (in.mouseReleased) {
                if (replayHot_) { InitBattle(); return; }
                if (homeHot_) { s_->changeScene(CreateTitleScene()); return; }
            }
            return;
        }
        if (over_) return;

        if (in.mousePressed) {
            dragOK_ = false;
            int hit = -1;
            for (int i = 0; i < kUT; ++i) if (Hit(SlotRect(i), in)) hit = i;
            if (hit >= 0) OnSlot(hit);
            else if (!OverUi(in.mouseX, in.mouseY)) dragOK_ = true;
        }
        if (!in.mouseDown) dragOK_ = false;
        if (in.mouseReleased) {
            if (closeHot_) {
                if (!started_) s_->changeScene(CreateTitleScene());
                else Finish();
            } else if (fastHot_) fast_ = !fast_;
        }
    }

    void OnSlot(int i) {
        if (St(i).hero && counts_[i] == 0) {                // hero already on the field -> ability
            int h = i - kKing;
            Unit* u = (heroId_[h] >= 0) ? &units_[heroId_[h]] : nullptr;
            if (u && u->alive && heroCd_[h] <= 0.f) { UseAbility(i, *u); heroCd_[h] = kAbilityCd; }
        } else if (counts_[i] > 0) sel_ = i;
    }

    static constexpr float kAbilityCd = 18.f;

    void UseAbility(int type, Unit& u) {
        if (type == kKing) {
            u.rage = 6.f; u.hp = std::min(u.maxHp, u.hp + u.maxHp * 0.25f);
            for (int i = 0; i < 18; ++i) {
                float a = i * 0.35f;
                parts_.push_back({ u.x, u.y, 4, std::cos(a) * 1.6f, std::sin(a) * 1.6f * 0.6f, 40, 0.8f, 0.8f, 6, RGB(1.f, 0.85f, 0.25f, 0.9f), 1 });
            }
            parts_.push_back({ u.x, u.y, 0, 0, 0, 0, 0.7f, 0.7f, 70, RGB(1.f, 0.8f, 0.2f), 2 });
        } else {
            float cx = u.x, cy = u.y;
            int ti = (u.target >= 0 && !blds_[u.target].dead) ? u.target : PickTarget(u);
            if (ti >= 0) { cx = blds_[ti].gx + 0.5f; cy = blds_[ti].gy + 0.5f; }
            for (int k = 0; k < 7; ++k) {
                float tx = cx + (Rnd() - 0.5f) * 3.6f, ty = cy + (Rnd() - 0.5f) * 3.6f;
                Fire(false, 3, tx, ty, 330, tx, ty, 150, 1.25f, -1, -1, 0.7f, 0, k * 0.16f);
            }
        }
    }

    // ============================================================ battle simulation
    float Pct() const { return totalHp_ > 0 ? std::min(1.f, lostHp_ / totalHp_) : 0.f; }
    int Stars() const { return (Pct() >= 0.30f ? 1 : 0) + (keepDead_ ? 1 : 0) + (Pct() >= 0.80f ? 1 : 0); }

    unsigned rng_ = 1;
    float Rnd() { rng_ = rng_ * 1664525u + 1013904223u; return ((rng_ >> 8) & 0xFFFFFF) / 16777216.f; }

    void Finish() { if (over_) return; over_ = true; endT_ = 0; }

    void Deploy(int type, float gx, float gy) {
        Unit u; u.type = type; u.x = gx + (Rnd() - 0.5f) * 0.3f; u.y = gy + (Rnd() - 0.5f) * 0.3f;
        u.hp = u.maxHp = St(type).hp; u.phase = Rnd() * 6.f;
        units_.push_back(u);
        counts_[type]--; started_ = true;
        deployCd_ = St(type).hero ? 0.5f : 0.09f;
        if (St(type).hero) heroId_[type - kKing] = (int)units_.size() - 1;
        for (int i = 0; i < 5; ++i)
            parts_.push_back({ u.x, u.y, 2, (Rnd() - .5f) * 1.2f, (Rnd() - .5f) * 0.8f, 10, 0.5f, 0.5f, 7, RGB(0.85f, 0.8f, 0.65f, 0.7f), 0 });
        if (counts_[type] == 0 && !St(type).hero) {
            for (int i = 0; i < kUT; ++i) if (counts_[i] > 0 && !St(i).hero) { sel_ = i; return; }
            for (int i = 0; i < kUT; ++i) if (counts_[i] > 0) { sel_ = i; return; }
        }
    }

    void UpdateBattle(float dt, const InputState& in) {
        if (started_) timeLeft_ = std::max(0.f, timeLeft_ - dt);
        for (int h = 0; h < 2; ++h) if (heroCd_[h] > 0) heroCd_[h] -= dt;
        deployCd_ -= dt;
        if (dragOK_ && in.mouseDown && deployCd_ <= 0 && counts_[sel_] > 0 && ValidDeploy(in.mouseX, in.mouseY)) {
            Vec2 g = ToGrid(in.mouseX, in.mouseY);
            Deploy(sel_, g.x, g.y);
        }
        UpdateDefenses(dt); UpdateDefenders(dt); UpdateUnits(dt); UpdateProj(dt);
        for (auto& b : blds_) { if (b.flash > 0) b.flash -= dt; if (b.dead) b.deadT += dt; }
        for (auto& d : defs_) if (!d.alive) d.deadT += dt;

        // star pop timers
        int st = Stars();
        for (int i = 0; i < 3; ++i) if (i < st && starT_[i] < 0) starT_[i] = t_;

        if (started_) {
            int alive = 0, left = 0;
            for (auto& u : units_) if (u.alive) ++alive;
            for (int i = 0; i < kUT; ++i) left += counts_[i];
            if (Pct() >= 0.995f || timeLeft_ <= 0.f) Finish();
            else if (alive == 0 && left == 0) { idleT_ += dt; if (idleT_ > 1.5f) Finish(); }
            else idleT_ = 0;
        }
    }

    int PickTarget(const Unit& u) const {
        int best = -1; float bd = 1e9f;
        for (int pass = 0; pass < 2 && best < 0; ++pass)
            for (int i = 0; i < (int)blds_.size(); ++i) {
                const Bld& b = blds_[i];
                if (b.dead) continue;
                if (pass == 0 && !(St(u.type).prefDef && b.def)) continue;
                float d = Dist(u.x, u.y, b.gx + 0.5f, b.gy + 0.5f);
                if (d < bd) { bd = d; best = i; }
            }
        return best;
    }

    void UpdateUnits(float dt) {
        for (auto& u : units_) {
            if (!u.alive) { u.deadT += dt; continue; }
            const UStat& st = St(u.type);
            if (u.cd > 0) u.cd -= dt;
            if (u.rage > 0) u.rage -= dt;
            if (u.atk > 0) u.atk -= dt * 2.5f;
            float spd = st.speed * (u.rage > 0 ? 1.35f : 1.f);
            float dmgMul = u.rage > 0 ? 2.f : 1.f;

            // defenders in reach are fought first
            int di = -1; float best = 1e9f;
            float reach = std::max(st.range, 0.9f) + 0.4f;
            for (int i = 0; i < (int)defs_.size(); ++i) {
                const Def& d = defs_[i]; if (!d.alive) continue;
                float dd = Dist(u.x, u.y, d.x, d.y);
                if (dd < reach && dd < best) { best = dd; di = i; }
            }
            if (di >= 0) {
                u.flip = ((defs_[di].x - defs_[di].y) - (u.x - u.y)) < 0;
                if (u.cd <= 0) {
                    u.cd = st.cd; u.atk = 1.f;
                    HurtD(di, st.dmg * dmgMul);
                }
                continue;
            }

            if (u.target < 0 || blds_[u.target].dead || (u.retarget -= dt) < 0) { u.target = PickTarget(u); u.retarget = 1.5f; }
            if (u.target < 0) continue;
            Bld& b = blds_[u.target];
            float tx = b.gx + 0.5f, ty = b.gy + 0.5f, dd = Dist(u.x, u.y, tx, ty);
            u.flip = ((tx - ty) - (u.x - u.y)) < 0;
            float stop = st.range + 0.45f;
            if (dd > stop) {
                u.x += (tx - u.x) / dd * spd * dt; u.y += (ty - u.y) / dd * spd * dt;
                u.phase += dt * 9.f * spd / 1.7f;
            } else if (u.cd <= 0) {
                u.cd = st.cd; u.atk = 1.f;
                if (st.range < 1.5f) {
                    HurtB(u.target, st.dmg * dmgMul);
                    parts_.push_back({ tx - 0.2f, ty + 0.2f, 12, (Rnd() - .5f) * 1.0f, 0.4f, 14, 0.35f, 0.35f, 5, RGB(0.9f, 0.85f, 0.7f, 0.7f), 0 });
                } else {
                    int kind = (u.type == kCata) ? 2 : (u.type == kMage ? 3 : 0);
                    float aoe = u.type == kCata ? 1.1f : (u.type == kMage ? 0.9f : 0.f);
                    Fire(false, kind, u.x, u.y, Z(24), tx, ty, st.dmg * dmgMul, aoe, u.target, -1, kind == 2 ? 4.5f : 8.f, kind == 2 ? 90.f : 22.f);
                }
            }
        }
        // soft separation so troops do not stack
        for (size_t i = 0; i < units_.size(); ++i) {
            if (!units_[i].alive) continue;
            for (size_t j = i + 1; j < units_.size(); ++j) {
                if (!units_[j].alive) continue;
                float dx = units_[j].x - units_[i].x, dy = units_[j].y - units_[i].y, d = std::sqrt(dx * dx + dy * dy);
                if (d < 0.32f && d > 0.0001f) {
                    float push = (0.32f - d) * 0.5f;
                    units_[i].x -= dx / d * push; units_[i].y -= dy / d * push;
                    units_[j].x += dx / d * push; units_[j].y += dy / d * push;
                }
            }
        }
    }

    void UpdateDefenders(float dt) {
        for (auto& d : defs_) {
            if (!d.alive) continue;
            if (d.cd > 0) d.cd -= dt;
            int ti = -1; float best = 3.2f;
            for (int i = 0; i < (int)units_.size(); ++i) {
                if (!units_[i].alive) continue;
                float dd = Dist(d.x, d.y, units_[i].x, units_[i].y);
                if (dd < best) { best = dd; ti = i; }
            }
            if (ti >= 0) {
                Unit& u = units_[ti];
                d.flip = ((u.x - u.y) - (d.x - d.y)) < 0;
                if (best > 0.7f) { d.x += (u.x - d.x) / best * 1.5f * dt; d.y += (u.y - d.y) / best * 1.5f * dt; d.phase += dt * 11.f; }
                else if (d.cd <= 0) { d.cd = 0.9f; HurtU(ti, 7.f); }
            } else if (!roads_.empty()) {
                float dd = Dist(d.x, d.y, d.wx, d.wy);
                if (dd < 0.15f) { Vec2 r = roads_[(size_t)(Rnd() * roads_.size()) % roads_.size()]; d.wx = r.x + (Rnd() - .5f) * .4f; d.wy = r.y + (Rnd() - .5f) * .4f; }
                else { d.x += (d.wx - d.x) / dd * 0.8f * dt; d.y += (d.wy - d.y) / dd * 0.8f * dt; d.phase += dt * 7.f; d.flip = ((d.wx - d.wy) - (d.x - d.y)) < 0; }
            }
        }
    }

    void UpdateDefenses(float dt) {
        for (auto& b : blds_) {
            if (b.dead || !b.def) continue;
            if (b.cd > 0) b.cd -= dt;
            float range = b.def == 1 ? 5.8f : (b.def == 2 ? 6.5f : 7.0f);
            float cx = b.gx + 0.5f, cy = b.gy + 0.5f;
            int ti = -1; float best = range;
            for (int i = 0; i < (int)units_.size(); ++i) {
                if (!units_[i].alive) continue;
                float d = Dist(cx, cy, units_[i].x, units_[i].y);
                if (d < best) { best = d; ti = i; }
            }
            if (ti < 0) continue;
            const Unit& u = units_[ti];
            b.aim = std::atan2((u.y - cy) - (u.x - cx) * 0.f, (u.x - cx) - (u.y - cy));
            if (b.cd > 0) continue;
            float z = Z(b.top) - 4.f;
            if (b.def == 2) { b.cd = 2.4f; Fire(true, 1, cx, cy, Z(20), u.x, u.y, 40, 0.9f, -1, -1, 6.f, 40.f); }
            else if (b.def == 3) { b.cd = 1.6f; Fire(true, 0, cx, cy, z, u.x, u.y, 18, 0, -1, ti, 9.f, 22.f); }
            else { b.cd = 1.1f; Fire(true, 0, cx, cy, z, u.x, u.y, 11, 0, -1, ti, 9.f, 18.f); }
        }
    }

    void Fire(bool fromDef, int kind, float x0, float y0, float z0, float x1, float y1, float dmg, float aoe, int tb, int tu,
              float speed, float arc, float delay = 0.f) {
        Proj q; q.fromDef = fromDef; q.kind = kind; q.x0 = x0; q.y0 = y0; q.z0 = z0; q.x1 = x1; q.y1 = y1;
        q.dmg = dmg; q.aoe = aoe; q.tb = tb; q.tu = tu; q.arc = arc; q.delay = delay;
        q.dur = std::max(0.25f, Dist(x0, y0, x1, y1) / speed);
        if (kind == 3) q.dur = 0.55f;
        projs_.push_back(q);
    }

    void UpdateProj(float dt) {
        for (auto& q : projs_) {
            if (q.delay > 0) { q.delay -= dt; continue; }
            q.t += dt;
            if (q.tu >= 0 && units_[q.tu].alive) { q.x1 = units_[q.tu].x; q.y1 = units_[q.tu].y; }
            if (q.t >= q.dur) { Impact(q); q.dur = -1.f; }
        }
        projs_.erase(std::remove_if(projs_.begin(), projs_.end(), [](const Proj& q) { return q.dur < 0; }), projs_.end());
    }

    void Impact(const Proj& q) {
        bool big = q.aoe > 0.f;
        if (q.fromDef) {
            if (q.aoe > 0) { for (int i = 0; i < (int)units_.size(); ++i) if (units_[i].alive && Dist(units_[i].x, units_[i].y, q.x1, q.y1) < q.aoe) HurtU(i, q.dmg); }
            else if (q.tu >= 0) HurtU(q.tu, q.dmg);
        } else {
            if (q.aoe > 0) {
                for (int i = 0; i < (int)blds_.size(); ++i)
                    if (!blds_[i].dead && Dist(blds_[i].gx + 0.5f, blds_[i].gy + 0.5f, q.x1, q.y1) < q.aoe + 0.35f) HurtB(i, q.dmg);
                for (int i = 0; i < (int)defs_.size(); ++i) if (defs_[i].alive && Dist(defs_[i].x, defs_[i].y, q.x1, q.y1) < q.aoe + 0.3f) HurtD(i, q.dmg);
            } else if (q.tb >= 0) HurtB(q.tb, q.dmg);
        }
        if (big) Boom(q.x1, q.y1, q.kind == 3 ? 1.4f : 1.0f, q.kind == 3);
        else parts_.push_back({ q.x1, q.y1, 6, 0, 0, 10, 0.3f, 0.3f, 5, RGB(0.9f, 0.85f, 0.7f, 0.6f), 0 });
    }

    void HurtB(int i, float dmg) {
        Bld& b = blds_[i]; if (b.dead) return;
        float real = std::min(dmg, b.hp);
        b.hp -= dmg; lostHp_ += real; b.flash = 0.18f;
        if (b.hp <= 0) {
            b.hp = 0; b.dead = true; b.deadT = 0;
            if (b.c == 'K') keepDead_ = true;
            Boom(b.gx + 0.5f, b.gy + 0.5f, b.c == 'K' ? 2.2f : 1.3f, false);
            for (int k = 0; k < 10; ++k)
                parts_.push_back({ b.gx + 0.5f, b.gy + 0.5f, 10, (Rnd() - .5f) * 3.f, (Rnd() - .5f) * 3.f, 70 + Rnd() * 90, 1.2f, 1.2f, 5,
                                   (k & 1) ? RGB(0.62f, 0.56f, 0.46f) : RGB(0.45f, 0.30f, 0.16f), 1 });
            for (int k = 0; k < 3; ++k)
                parts_.push_back({ b.gx + 0.5f, b.gy + 0.5f, 10, (Rnd() - .5f) * 0.6f, (Rnd() - .5f) * 0.6f, 30, 1.4f, 1.4f, 9, RGB(1.f, 0.85f, 0.2f), 3 });
        }
    }
    void HurtU(int i, float dmg) {
        Unit& u = units_[i]; if (!u.alive) return;
        u.hp -= dmg;
        if (u.hp <= 0) { u.alive = false; u.deadT = 0; parts_.push_back({ u.x, u.y, 6, 0, 0, 20, 0.5f, 0.5f, 8, RGB(0.9f, 0.9f, 0.9f, 0.7f), 0 }); }
    }
    void HurtD(int i, float dmg) {
        Def& d = defs_[i]; if (!d.alive) return;
        d.hp -= dmg;
        if (d.hp <= 0) { d.alive = false; d.deadT = 0; }
    }

    // ============================================================ particles
    void Boom(float gx, float gy, float s, bool fire) {
        if (parts_.size() > 700) return;
        parts_.push_back({ gx, gy, 6, 0, 0, 0, 0.35f, 0.35f, 26 * s, fire ? RGB(1.f, 0.6f, 0.15f) : RGB(1.f, 0.9f, 0.5f), 4 });
        parts_.push_back({ gx, gy, 0, 0, 0, 0, 0.5f, 0.5f, 28 * s, RGB(1.f, 0.9f, 0.7f), 2 });
        for (int i = 0; i < (int)(6 * s); ++i)
            parts_.push_back({ gx, gy, 8, (Rnd() - .5f) * 1.6f, (Rnd() - .5f) * 1.6f, 12 + Rnd() * 20, 0.9f, 0.9f, 10 * s,
                               fire ? RGB(0.3f, 0.2f, 0.2f, 0.6f) : RGB(0.55f, 0.47f, 0.34f, 0.6f), 0 });
    }

    void UpdateParticles(float dt) {
        for (auto& q : parts_) {
            q.life -= dt;
            q.x += q.vx * dt; q.y += q.vy * dt; q.z += q.vz * dt;
            if (q.kind == 1) { q.vz -= 300.f * dt; if (q.z < 0) { q.z = 0; q.vz *= -0.3f; q.vx *= 0.5f; q.vy *= 0.5f; } }
            else if (q.kind == 3) q.vz *= 0.97f;
        }
        parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Part& q) { return q.life <= 0; }), parts_.end());
    }

    // ============================================================ ground
    void DrawGround(gfx::Painter& p) {
        p.VGrad(0, 0, kCanvasW, kCanvasH, RGB(0.80f, 0.87f, 0.95f), RGB(0.91f, 0.95f, 0.99f));
        for (int i = 0; i < 30; ++i) {          // soft snow drifts
            float x = Hash01(i * 7 + 1) * kCanvasW, y = Hash01(i * 7 + 2) * kCanvasH;
            p.Ellipse(x, y, 40 + Hash01(i * 7 + 3) * 90, 10 + Hash01(i * 7 + 4) * 24, RGB(0.66f, 0.76f, 0.90f, 0.22f), 14);
            p.Ellipse(x - 6, y - 4, 30 + Hash01(i * 7 + 5) * 50, 6 + Hash01(i * 7 + 6) * 10, RGB(1, 1, 1, 0.35f), 12);
        }
        const char* const* L = Layout();
        const Color cob = RGB(0.62f, 0.63f, 0.69f);

        // grey cobbled roads leaving every gate
        for (int gy = 0; gy < N; ++gy)
            for (int gx = 0; gx < N; ++gx) {
                if (L[gy][gx] != 'G') continue;
                static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d) {
                    int nx = gx + dx[d], ny = gy + dy[d];
                    bool outside = nx < 0 || ny < 0 || nx >= N || ny >= N || (!inside_[ny][nx] && !WallLike(L[ny][nx]));
                    if (!outside) continue;
                    for (int k = 1; k <= 14; ++k) {
                        float cx = gx + 0.5f + dx[d] * (k - 0.5f), cy = gy + 0.5f + dy[d] * (k - 0.5f);
                        float px = std::abs((float)dy[d]) * 0.26f, py = std::abs((float)dx[d]) * 0.26f, ex = std::abs((float)dx[d]) * 0.5f, ey = std::abs((float)dy[d]) * 0.5f;
                        Vec2 a0 = p.S(cx - px - ex, cy - py - ey), a1 = p.S(cx + px - ex, cy + py - ey), a2 = p.S(cx + px + ex, cy + py + ey), a3 = p.S(cx - px + ex, cy - py + ey);
                        p.Quad(a0, a1, a2, a3, Mul(cob, 0.96f + 0.06f * ((k + gx) & 1)));
                        p.Line(a0, a3, 2.f, RGB(0.34f, 0.35f, 0.42f, 0.7f)); p.Line(a1, a2, 2.f, RGB(0.34f, 0.35f, 0.42f, 0.7f));
                        if (k % 4 == 0) { Vec2 m = p.S(cx + px * 1.7f, cy + py * 1.7f); p.Fill(m.x - 3, m.y - 11, 6, 11, RGB(0.55f, 0.50f, 0.46f)); p.Fill(m.x - 3, m.y - 14, 6, 4, RGB(0.78f, 0.28f, 0.20f)); }
                    }
                }
            }
        // castle floor: trampled snow, cobbled lanes, soil, rock
        for (int gy = 0; gy < N; ++gy)
            for (int gx = 0; gx < N; ++gx) {
                char ch = L[gy][gx];
                if (!(inside_[gy][gx] || WallLike(ch))) continue;
                float v = Hash01(gx * 7 + gy * 131);
                Color col = ((gx + gy) & 1) ? RGB(0.94f, 0.96f, 1.0f) : RGB(0.90f, 0.94f, 0.99f);
                if (ch == '+') col = Mul(RGB(0.68f, 0.69f, 0.75f), 0.94f + 0.08f * v);
                else if (ch == 'F') col = RGB(0.50f, 0.37f, 0.25f);
                else if (ch == 'Q') col = RGB(0.62f, 0.62f, 0.68f);
                else if (ch != '.') col = Mul(RGB(0.80f, 0.82f, 0.88f), 0.94f + 0.1f * v);
                p.Quad(p.S((float)gx, (float)gy), p.S(gx + 1.f, (float)gy), p.S(gx + 1.f, gy + 1.f), p.S((float)gx, gy + 1.f), col);
                if (ch == '+')
                    for (int k = 0; k < 3; ++k) {
                        Vec2 c = p.S(gx + 0.15f + 0.7f * Hash01(gx * 5 + gy * 3 + k), gy + 0.15f + 0.7f * Hash01(gx * 11 + gy * 7 + k));
                        p.Ellipse(c.x, c.y, 4, 2, RGB(0.45f, 0.46f, 0.54f, 0.7f), 6);
                    }
            }
    }

    // ============================================================ buildings
    void DrawBuilding(gfx::Painter& p, int idx) {
        const Bld& b = blds_[idx];
        if (b.dead) { Rubble(p, b); return; }
        if (b.c != 'W' && b.c != 'T' && b.c != 'G' && b.c != 'F') {
            Vec2 sc = p.S(b.gx + 0.5f, b.gy + 0.6f);
            p.Ellipse(sc.x + 8, sc.y + 6, kTW * 0.46f, kTH * 0.46f, RGB(0.30f, 0.38f, 0.58f, 0.24f), 12);
        }
        float save = p.ox;
        if (b.flash > 0) p.ox += std::sin(t_ * 70.f) * 1.6f;
        if (!TrySprite(p, b))
        switch (b.c) {
        case 'W': Wall(p, b.gx, b.gy); break;
        case 'T': RoundTower(p, b.gx, b.gy, 0.62f, Z(58), Z(40), RGB(0.80f, 0.28f, 0.14f), b); break;
        case 't': RoundTower(p, b.gx, b.gy, 0.42f, Z(40), Z(30), RGB(0.62f, 0.20f, 0.12f), b); break;
        case 'G': Gate(p, b.gx, b.gy); break;
        case 'K': Keep(p, b.gx, b.gy); break;
        case 'C': Cathedral(p, b.gx, b.gy); break;
        case 'R': Barracks(p, b.gx, b.gy); break;
        case 'E': Stables(p, b.gx, b.gy); break;
        case 'H': House(p, b.gx, b.gy); break;
        case 'S': Smithy(p, b.gx, b.gy); break;
        case 'M': Windmill(p, b.gx, b.gy); break;
        case 'F': Farm(p, b.gx, b.gy); break;
        case 'Q': Quarry(p, b.gx, b.gy); break;
        case 'A': Ballista(p, b); break;
        case 'X': Market(p, b.gx, b.gy); break;
        case 'L': Lumber(p, b.gx, b.gy); break;
        case 'D': Vault(p, b.gx, b.gy); break;
        case 'k': Cannon(p, b); break;
        case 'B': WatchTower(p, b.gx, b.gy); break;
        default: break;
        }
        p.ox = save;
        float f = b.hp / b.maxHp;
        if (f < 0.6f && b.c != 'F') {
            Vec2 c = p.S(b.gx + 0.5f, b.gy + 0.5f, Z(b.top) * 0.8f);
            Flame(p, c.x, c.y, f < 0.3f ? 1.1f : 0.8f);
            if (f < 0.3f) Flame(p, c.x + 9, c.y + 2, 0.7f);
            Smoke(p, c.x, c.y - 8, b.gx * 0.3f + b.gy * 0.17f, 1.0f);
        }
    }

    // Optional art: put PNGs in assets/buildings/ and they replace the drawn version automatically.
    // Names: keep cathedral barracks stables house0..house3 smithy windmill farm quarry ballista market lumber vault cannon watchtower tower
    static const char* SpriteName(char c) {
        switch (c) {
        case 'K': return "keep"; case 'C': return "cathedral"; case 'R': return "barracks"; case 'E': return "stables";
        case 'S': return "smithy"; case 'M': return "windmill"; case 'F': return "farm"; case 'Q': return "quarry";
        case 'A': return "ballista"; case 'X': return "market"; case 'L': return "lumber"; case 'D': return "vault";
        case 'k': return "cannon"; case 'B': return "watchtower"; case 'T': return "tower"; default: return nullptr;
        }
    }
    bool TrySprite(gfx::Painter& p, const Bld& b) {
        std::string rel;
        if (b.c == 'H') rel = "buildings/house" + std::to_string((b.gx * 7 + b.gy * 3) % 4) + ".png";
        else { const char* n = SpriteName(b.c); if (!n) return false; rel = std::string("buildings/") + n + ".png"; }
        Vec2 sz = s_->assets->TextureSize(rel);
        TextureHandle tex = s_->assets->LoadTexture(rel);
        if (sz.x <= 0) return false;
        float w = kTW * (b.c == 'K' ? 2.6f : (b.c == 'C' ? 1.5f : 1.15f)), h = w * sz.y / sz.x;
        Vec2 a = p.S(b.gx + 0.5f, b.gy + 0.92f);
        s_->render->DrawSprite(tex, { a.x - w / 2, a.y - h, w, h }, { 0, 0, 1, 1 }, RGB(1, 1, 1));
        return true;
    }

    void Rubble(gfx::Painter& p, const Bld& b) {
        Vec2 c = p.S(b.gx + 0.5f, b.gy + 0.5f);
        float s = b.c == 'K' ? 2.0f : 1.f;
        p.Ellipse(c.x, c.y + 2, 26 * s, 12 * s, RGB(0.18f, 0.15f, 0.12f, 0.75f), 12);
        for (int k = 0; k < 4; ++k) {
            float ax = (Hash01(b.gx * 7 + k) - 0.5f) * 28 * s, ay = (Hash01(b.gy * 5 + k * 3) - 0.5f) * 14 * s;
            p.RotRect(c.x + ax, c.y + ay - 2, 6 + Hash01(k + b.gx) * 6, 4 + Hash01(k * 2 + b.gy) * 3, Hash01(k) * 3.f,
                      (k & 1) ? RGB(0.62f, 0.57f, 0.48f) : RGB(0.42f, 0.28f, 0.15f));
        }
        if (b.deadT < 7.f) Smoke(p, c.x, c.y - 6, b.gx * 0.4f, 0.9f * (1.f - b.deadT / 8.f));
        if (b.deadT < 3.f) Flame(p, c.x - 4, c.y, 0.8f * (1.f - b.deadT / 3.f));
    }

    static Color RoofColor(float h) {
        static const Color c[6] = { RGB(0.14f, 0.52f, 0.54f), RGB(0.10f, 0.40f, 0.46f), RGB(0.78f, 0.20f, 0.20f),
                                    RGB(0.56f, 0.36f, 0.20f), RGB(0.90f, 0.52f, 0.16f), RGB(0.32f, 0.38f, 0.54f) };
        return c[(int)(h * 5.99f)];
    }
    static Color Plaster(float h) {
        static const Color c[4] = { RGB(0.90f, 0.80f, 0.62f), RGB(0.82f, 0.68f, 0.48f), RGB(0.94f, 0.89f, 0.77f), RGB(0.72f, 0.70f, 0.66f) };
        return c[(int)(h * 3.99f)];
    }

    void Gable(gfx::Painter& p, float x0, float y0, float x1, float y1, float z, float h, Color slope, Color dark, Color gableCol) {
        p.GableRoofX(x0, y0, x1, y1, z, h, slope, dark, gableCol);
        float mid = (y0 + y1) / 2;
        Vec2 a = p.S(x0, y1, z), b = p.S(x1, y1, z), c = p.S(x1, mid, z + h), d = p.S(x0, mid, z + h);
        p.Quad(LerpV(a, d, 0.5f), LerpV(b, c, 0.5f), c, d, RGB(0.96f, 0.98f, 1.0f));
        p.Line(LerpV(a, d, 0.5f), LerpV(b, c, 0.5f), 1.2f, RGB(0.72f, 0.80f, 0.92f));
    }

    void HipRoof(gfx::Painter& p, float x0, float y0, float x1, float y1, float z, float h, Color col, bool snow = true) {
        Vec2 apex = p.S((x0 + x1) / 2, (y0 + y1) / 2, z + h);
        Vec2 a = p.S(x0, y1, z), b = p.S(x1, y1, z), c = p.S(x1, y0, z);
        p.Tri(a, b, apex, col);
        p.Tri(b, c, apex, Mul(col, 0.66f));
        p.Line(a, b, 1.4f, Mul(col, 0.5f)); p.Line(b, c, 1.4f, Mul(col, 0.4f));
        for (int k = 1; k < 4; ++k) { float t = k / 4.f; p.Line(LerpV(a, apex, t), LerpV(b, apex, t), 1.f, Alpha(Mul(col, 0.55f), 0.5f)); }
        if (snow) { p.Tri(LerpV(a, apex, 0.55f), LerpV(b, apex, 0.55f), apex, RGB(0.96f, 0.98f, 1.0f)); p.Tri(LerpV(b, apex, 0.55f), LerpV(c, apex, 0.55f), apex, RGB(0.80f, 0.86f, 0.95f)); }
    }

    void Wall(gfx::Painter& p, int gx, int gy) {
        const float H = Z(27.f);
        bool nN = WallLike(At(gx, gy - 1)), nS = WallLike(At(gx, gy + 1)), nW = WallLike(At(gx - 1, gy)), nE = WallLike(At(gx + 1, gy));
        const float a = 0.25f, b = 0.75f;
        auto armX = [&](float x0, float x1) {                 // arm along gx
            p.Prism(x0, gy + a, x1, gy + b, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
            for (int k = 0; k < 2; ++k) { float u = x0 + 0.08f + k * 0.2f; p.Prism(u, gy + b - 0.08f, u + 0.09f, gy + b, H, H + Z(5), stoneTop(), stoneL(), stoneR()); }
        };
        auto armY = [&](float y0, float y1) {
            p.Prism(gx + a, y0, gx + b, y1, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
            for (int k = 0; k < 2; ++k) { float u = y0 + 0.08f + k * 0.2f; p.Prism(gx + b - 0.08f, u, gx + b, u + 0.09f, H, H + Z(5), stoneTop(), stoneL(), stoneR()); }
        };
        if (nN) armY((float)gy, gy + 0.5f);
        if (nW) armX((float)gx, gx + 0.5f);
        p.Prism(gx + a, gy + a, gx + b, gy + b, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
        if (nE) armX(gx + 0.5f, gx + 1.f);
        if (nS) armY(gy + 0.5f, gy + 1.f);
    }

    void RoundTower(gfx::Painter& p, int gx, int gy, float r, float h, float roofH, Color roof, const Bld& b) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        float R = r * kTW * 0.7071f;
        p.Cylinder(c.x, c.y, R, 0, h, stoneL(), stoneTop(), true, mortar());
        for (int k = -1; k <= 1; ++k) {
            float a = kPiHalf + k * 0.7f;
            float sx = c.x + R * std::cos(a), sy = c.y - h * 0.55f + R * 0.5f * std::sin(a);
            p.Fill(sx - 1.5f, sy - 5, 3, 9, RGB(0.10f, 0.08f, 0.07f));
        }
        p.Cone(c.x, c.y, R + 4, h, roofH, roof);
        Flag(p, c.x, c.y - h - roofH, 14, RGB(0.20f, 0.35f, 0.85f), 0.f);
        (void)b;
    }

    void Gate(gfx::Painter& p, int gx, int gy) {
        const float H = Z(36.f);
        float x0 = gx + 0.08f, x1 = gx + 0.92f, y0 = gy + 0.08f, y1 = gy + 0.92f;
        p.Prism(x0, y0, x1, y1, 0, H, stoneTop(), stoneL(), stoneR(), true, mortar());
        auto arch = [&](Vec2 A, Vec2 B) {
            auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - Z(z) }; };
            const Color dark = RGB(0.06f, 0.05f, 0.05f);
            p.Quad(at(0.25f, 0), at(0.75f, 0), at(0.75f, 15), at(0.25f, 15), dark);
            p.Quad(at(0.25f, 15), at(0.75f, 15), at(0.68f, 21), at(0.32f, 21), dark);
            p.Quad(at(0.32f, 21), at(0.68f, 21), at(0.55f, 24), at(0.45f, 24), dark);
            for (int k = 1; k < 6; ++k) p.Line(at(0.25f + 0.5f * k / 6, 0), at(0.25f + 0.5f * k / 6, 20), 1.2f, RGB(0.35f, 0.28f, 0.2f));
        };
        arch(p.S(x0, y1, 0), p.S(x1, y1, 0));
        arch(p.S(x1, y1, 0), p.S(x1, y0, 0));
        for (int k = 0; k < 4; ++k) { float u = 0.18f + 0.22f * k; p.Prism(gx + u, y1 - 0.1f, gx + u + 0.12f, y1, H, H + Z(6), stoneTop(), stoneL(), stoneR()); }
        Vec2 top = p.S(gx + 0.5f, gy + 0.5f, H + Z(6));
        Flag(p, top.x, top.y, 24, RGB(0.85f, 0.14f, 0.12f), (float)(gx + gy));
    }

    void Keep(gfx::Painter& p, int gx, int gy) {
        Vec2 c0 = p.S(gx + 0.5f, gy + 0.5f);
        p.Ellipse(c0.x, c0.y + 8, 100, 46, RGB(0.35f, 0.42f, 0.58f, 0.35f), 18);
        const Color gt = RGB(0.82f, 0.82f, 0.88f), gl = RGB(0.64f, 0.64f, 0.72f), gr = RGB(0.44f, 0.44f, 0.54f), mo = RGB(0.2f, 0.2f, 0.28f, 0.45f);
        const Color tur = RGB(0.82f, 0.24f, 0.32f);
        auto turret = [&](float u, float v, float R, float h) {
            Vec2 c = p.S(gx + u, gy + v);
            p.Cylinder(c.x, c.y, R, 0, h, gl, gt, true, mo, 12);
            p.Cone(c.x, c.y, R + 4, h, h * 0.62f, tur, 12);
            p.Tri({ c.x - R * 0.6f, c.y - h - h * 0.62f * 0.35f }, { c.x + R * 0.6f, c.y - h - h * 0.62f * 0.35f }, { c.x, c.y - h - h * 0.62f }, RGB(0.96f, 0.98f, 1.0f));
        };
        turret(-0.02f, -0.02f, 15, Z(84));
        turret(1.02f, -0.02f, 14, Z(70));
        turret(-0.02f, 1.02f, 14, Z(70));
        p.Prism(gx + 0.0f, gy + 0.0f, gx + 1.0f, gy + 1.0f, 0, Z(56), gt, gl, gr, true, mo);
        for (int k = 0; k < 4; ++k) {                                      // crenels
            float u = 0.08f + k * 0.26f;
            p.Prism(gx + u, gy + 0.9f, gx + u + 0.12f, gy + 1.0f, Z(56), Z(64), gt, gl, gr);
            p.Prism(gx + 0.9f, gy + u, gx + 1.0f, gy + u + 0.12f, Z(56), Z(64), gt, gl, gr);
        }
        p.Prism(gx + 0.28f, gy + 0.28f, gx + 0.74f, gy + 0.74f, Z(56), Z(94), gt, gl, gr, true, mo);
        HipRoof(p, gx + 0.22f, gy + 0.22f, gx + 0.80f, gy + 0.80f, Z(94), Z(40), tur);
        Vec2 d0 = p.S(gx + 0.5f, gy + 1.0f, 0);
        p.Quad({ d0.x - 11, d0.y }, { d0.x + 11, d0.y }, { d0.x + 11, d0.y - Z(24) }, { d0.x - 11, d0.y - Z(24) }, RGB(0.18f, 0.11f, 0.07f));
        p.Quad({ d0.x - 11, d0.y - Z(24) }, { d0.x + 11, d0.y - Z(24) }, { d0.x + 6, d0.y - Z(32) }, { d0.x - 6, d0.y - Z(32) }, RGB(0.18f, 0.11f, 0.07f));
        for (int k = -1; k <= 1; k += 2) {
            float bx = d0.x + k * 30.f, by = d0.y - Z(38), sway = std::sin(t_ * 2.5f + k) * 1.5f;
            p.Quad({ bx - 6, by - 14 }, { bx + 6, by - 14 }, { bx + 6 + sway, by + 12 }, { bx - 6 + sway, by + 12 }, RGB(0.18f, 0.32f, 0.78f));
            p.Star(bx + sway * 0.5f, by - 2, 4.5f, RGB(0.98f, 0.80f, 0.20f));
        }
        Vec2 top = p.S(gx + 0.5f, gy + 0.5f, Z(134));
        Flag(p, top.x, top.y, 24, RGB(0.85f, 0.14f, 0.12f), 1.3f);
    }

    void Cathedral(gfx::Painter& p, int gx, int gy) {
        Color wall = RGB(0.90f, 0.87f, 0.80f);
        p.Prism(gx + 0.10f, gy + 0.22f, gx + 0.90f, gy + 0.82f, 0, Z(26), wall, Mul(wall, 0.95f), Mul(wall, 0.7f), true, Alpha(mortar(), 0.35f));
        Gable(p, gx + 0.06f, gy + 0.16f, gx + 0.94f, gy + 0.88f, Z(26), Z(20), RGB(0.34f, 0.42f, 0.62f), RGB(0.18f, 0.22f, 0.36f), wall);
        // bell tower with golden spire
        p.Prism(gx + 0.12f, gy + 0.55f, gx + 0.42f, gy + 0.85f, 0, Z(56), wall, Mul(wall, 0.95f), Mul(wall, 0.68f), true, Alpha(mortar(), 0.4f));
        Vec2 A = p.S(gx + 0.12f, gy + 0.85f, 0), B = p.S(gx + 0.42f, gy + 0.85f, 0);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - Z(z) }; };
        p.Quad(at(0.3f, 36), at(0.7f, 36), at(0.7f, 50), at(0.3f, 50), RGB(0.14f, 0.12f, 0.16f));
        p.Disc(at(0.5f, 20).x, at(0.5f, 20).y, 4.5f, RGB(0.95f, 0.78f, 0.25f));          // rose window
        HipRoof(p, gx + 0.08f, gy + 0.51f, gx + 0.46f, gy + 0.89f, Z(56), Z(46), RGB(0.98f, 0.78f, 0.18f), false);
        Vec2 top = p.S(gx + 0.27f, gy + 0.70f, Z(56 + 46));
        p.Line(top, { top.x, top.y - 10 }, 2.f, RGB(1.f, 0.9f, 0.4f));
        p.Line({ top.x - 4, top.y - 7 }, { top.x + 4, top.y - 7 }, 2.f, RGB(1.f, 0.9f, 0.4f));
        Vec2 d = LerpV(p.S(gx + 0.5f, gy + 0.82f, 0), p.S(gx + 0.9f, gy + 0.82f, 0), 0.5f);
        p.Quad({ d.x - 4, d.y }, { d.x + 4, d.y }, { d.x + 4, d.y - 11 }, { d.x - 4, d.y - 11 }, RGB(0.30f, 0.18f, 0.09f));
    }

    void Barracks(gfx::Painter& p, int gx, int gy) {
        Color wl = RGB(0.78f, 0.68f, 0.56f);
        p.Prism(gx + 0.08f, gy + 0.2f, gx + 0.92f, gy + 0.82f, 0, Z(22), wl, wl, Mul(wl, 0.7f));
        Vec2 A = p.S(gx + 0.08f, gy + 0.82f, 0), B = p.S(gx + 0.92f, gy + 0.82f, 0);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - Z(z) }; };
        for (float t : { 0.f, 0.33f, 0.66f, 1.f }) p.Line(at(t, 0), at(t, 22), 2.f, RGB(0.36f, 0.22f, 0.10f));
        p.Quad(at(0.38f, 0), at(0.62f, 0), at(0.62f, 12), at(0.38f, 12), RGB(0.25f, 0.15f, 0.08f));
        Gable(p, gx + 0.04f, gy + 0.14f, gx + 0.96f, gy + 0.88f, Z(22), Z(18), RGB(0.82f, 0.22f, 0.18f), RGB(0.42f, 0.10f, 0.08f), wl);
        // weapon rack with swords
        Vec2 r = p.S(gx + 0.88f, gy + 0.86f);
        for (int k = 0; k < 3; ++k) p.Line({ r.x - 4 + k * 4, r.y }, { r.x - 5 + k * 4, r.y - 13 }, 1.5f, RGB(0.85f, 0.88f, 0.95f));
        Vec2 f = p.S(gx + 0.5f, gy + 0.5f, Z(40));
        Flag(p, f.x, f.y, 22, RGB(0.18f, 0.34f, 0.85f), gx * 0.5f);
    }

    void Stables(gfx::Painter& p, int gx, int gy) {
        Color wood = RGB(0.62f, 0.44f, 0.26f);
        p.Prism(gx + 0.1f, gy + 0.22f, gx + 0.9f, gy + 0.78f, 0, Z(14), wood, wood, Mul(wood, 0.7f));
        p.Quad(p.S(gx + 0.15f, gy + 0.78f, 0), p.S(gx + 0.55f, gy + 0.78f, 0), p.S(gx + 0.55f, gy + 0.78f, Z(11)), p.S(gx + 0.15f, gy + 0.78f, Z(11)), RGB(0.16f, 0.10f, 0.07f));
        Gable(p, gx + 0.05f, gy + 0.16f, gx + 0.95f, gy + 0.84f, Z(14), Z(14), RGB(0.55f, 0.36f, 0.20f), RGB(0.28f, 0.17f, 0.09f), wood);
        // horse in the paddock
        Vec2 h = p.S(gx + 0.78f, gy + 0.95f);
        p.Ellipse(h.x, h.y - 7, 8, 4.5f, RGB(0.55f, 0.32f, 0.16f), 10);
        p.Quad({ h.x + 5, h.y - 9 }, { h.x + 9, h.y - 16 }, { h.x + 12, h.y - 14 }, { h.x + 8, h.y - 8 }, RGB(0.55f, 0.32f, 0.16f));
        for (float lx : { -5.f, -2.f, 3.f, 6.f }) p.Line({ h.x + lx, h.y - 5 }, { h.x + lx, h.y }, 1.6f, RGB(0.3f, 0.18f, 0.1f));
        for (int k = 0; k < 4; ++k) { Vec2 f = p.S(gx + 0.55f + k * 0.14f, gy + 0.98f); p.Line(f, { f.x, f.y - 6 }, 1.5f, RGB(0.4f, 0.28f, 0.14f)); }
    }

    void House(gfx::Painter& p, int gx, int gy) {
        float h = Hash01(gx * 13 + gy * 7);
        float w = 0.70f + 0.2f * Hash01(gx * 5 + gy * 11), d = 0.64f + 0.2f * Hash01(gx * 3 + gy * 17);
        float x0 = gx + 0.5f - w / 2, x1 = gx + 0.5f + w / 2, y0 = gy + 0.5f - d / 2, y1 = gy + 0.5f + d / 2;
        float wall = Z(h > 0.55f ? 32.f : 22.f);
        Color pl = Plaster(Hash01(gx * 29 + gy * 3));
        Color roof = RoofColor(Hash01(gx * 7 + gy * 23));
        p.Prism(x0, y0, x1, y1, 0, wall, pl, pl, Mul(pl, 0.72f));
        Vec2 A = p.S(x0, y1, 0), B = p.S(x1, y1, 0);
        const Color wood = RGB(0.38f, 0.24f, 0.12f);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - z }; };
        for (float t : { 0.f, 0.5f, 1.f }) p.Line(at(t, 0), at(t, wall), 1.6f, wood);
        p.Line(at(0, wall * 0.5f), at(1, wall * 0.5f), 1.4f, wood);
        p.Quad(at(0.12f, 0), at(0.34f, 0), at(0.34f, Z(10)), at(0.12f, Z(10)), RGB(0.30f, 0.18f, 0.09f));
        float lit = 0.88f + 0.06f * std::sin(t_ * 3 + h * 9);
        p.Quad(at(0.6f, wall * 0.45f), at(0.85f, wall * 0.45f), at(0.85f, wall * 0.8f), at(0.6f, wall * 0.8f), RGB(1.f, lit, 0.5f));
        if (h < 0.5f) Gable(p, x0 - 0.05f, y0 - 0.04f, x1 + 0.05f, y1 + 0.04f, wall, Z(14), roof, Mul(roof, 0.55f), pl);
        else HipRoof(p, x0 - 0.05f, y0 - 0.05f, x1 + 0.05f, y1 + 0.05f, wall, Z(15), roof);
        if (h > 0.3f && h < 0.75f) {
            p.Prism(x0 + 0.1f, y0 + 0.1f, x0 + 0.2f, y0 + 0.2f, wall + Z(4), wall + Z(20), stoneTop(), stoneL(), stoneR());
            Vec2 ch = p.S(x0 + 0.15f, y0 + 0.15f, wall + Z(20));
            Smoke(p, ch.x, ch.y, h * 5, 0.6f);
        }
    }

    void Smithy(gfx::Painter& p, int gx, int gy) {
        p.Prism(gx + 0.14f, gy + 0.2f, gx + 0.86f, gy + 0.8f, 0, Z(16), stoneTop(), stoneL(), stoneR(), true, mortar());
        Vec2 A = p.S(gx + 0.14f, gy + 0.8f, 0), B = p.S(gx + 0.86f, gy + 0.8f, 0);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - Z(z) }; };
        float fl = 0.75f + 0.25f * std::sin(t_ * 11.f);
        p.Quad(at(0.14f, 0), at(0.50f, 0), at(0.50f, 10), at(0.14f, 10), RGB(0.12f, 0.06f, 0.04f));
        p.Quad(at(0.20f, 0), at(0.44f, 0), at(0.44f, 6), at(0.20f, 6), RGB(1.0f, 0.45f + 0.3f * fl, 0.08f));
        p.Fill(at(0.62f, 0).x, at(0.62f, 0).y - 4, 8, 4, RGB(0.25f, 0.25f, 0.28f));
        Gable(p, gx + 0.08f, gy + 0.14f, gx + 0.92f, gy + 0.86f, Z(16), Z(14), RGB(0.33f, 0.36f, 0.44f), RGB(0.15f, 0.16f, 0.2f), stoneL());
        p.Prism(gx + 0.64f, gy + 0.3f, gx + 0.76f, gy + 0.42f, Z(18), Z(38), stoneTop(), stoneL(), stoneR(), true, mortar());
        Vec2 ch = p.S(gx + 0.70f, gy + 0.36f, Z(38));
        Smoke(p, ch.x, ch.y, 2.2f, 0.9f);
        for (int k = 0; k < 3; ++k) {
            float a = std::fmod(t_ * 1.6f + k * 0.3f, 1.f);
            p.Fill(at(0.3f, 4).x + (k - 1.f) * 4 + a * 8, at(0.3f, 4).y - a * 14, 2, 2, RGB(1, 0.8f, 0.2f, 1 - a));
        }
    }

    void Windmill(gfx::Painter& p, int gx, int gy) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.55f);
        p.Cylinder(c.x, c.y, 18, 0, Z(44), RGB(0.92f, 0.86f, 0.72f), RGB(0.9f, 0.85f, 0.7f), false);
        p.Cone(c.x, c.y, 21, Z(44), Z(26), RGB(0.58f, 0.37f, 0.18f));
        Vec2 hub{ c.x + 3, c.y - Z(40) };
        p.Quad({ c.x - 4, c.y + 10 }, { c.x + 4, c.y + 10 }, { c.x + 4, c.y - 2 }, { c.x - 4, c.y - 2 }, RGB(0.30f, 0.18f, 0.09f));
        for (int k = 0; k < 4; ++k) {
            float ang = t_ * 1.1f + k * kPiHalf, dx = std::cos(ang), dy = std::sin(ang);
            p.RotRect(hub.x + dx * 16, hub.y + dy * 16, 32, 2.6f, ang, RGB(0.45f, 0.30f, 0.15f));
            p.RotRect(hub.x + dx * 20 - dy * 4, hub.y + dy * 20 + dx * 4, 17, 7, ang, RGB(0.96f, 0.93f, 0.84f));
        }
        p.Disc(hub.x, hub.y, 3.6f, RGB(0.28f, 0.18f, 0.1f));
    }

    void Farm(gfx::Painter& p, int gx, int gy) {
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 5; ++k) {
                Vec2 q = p.S(gx + 0.16f + r * 0.22f, gy + 0.12f + k * 0.18f);
                float sway = std::sin(t_ * 2.2f + r + k * 0.7f) * 1.4f;
                p.Tri({ q.x - 2.5f, q.y }, { q.x + 2.5f, q.y }, { q.x + sway, q.y - 8 }, RGB(0.25f, 0.60f, 0.15f));
                p.Tri({ q.x - 1, q.y }, { q.x + 4, q.y }, { q.x + 2.5f + sway, q.y - 6 }, RGB(0.55f, 0.78f, 0.22f));
            }
        Vec2 s = p.S(gx + 0.88f, gy + 0.15f);
        p.Line({ s.x, s.y }, { s.x, s.y - 18 }, 1.8f, RGB(0.4f, 0.26f, 0.12f));
        p.Line({ s.x - 7, s.y - 13 }, { s.x + 7, s.y - 13 }, 1.8f, RGB(0.4f, 0.26f, 0.12f));
        p.Disc(s.x, s.y - 20, 3.2f, RGB(0.9f, 0.75f, 0.4f));
    }

    void Quarry(gfx::Painter& p, int gx, int gy) {
        const Color rockTop = RGB(0.80f, 0.82f, 0.86f), rockL = RGB(0.62f, 0.64f, 0.70f), rockR = RGB(0.46f, 0.48f, 0.54f);
        p.Prism(gx + 0.55f, gy + 0.10f, gx + 0.92f, gy + 0.45f, 0, Z(16), rockTop, rockL, rockR);
        p.Prism(gx + 0.12f, gy + 0.15f, gx + 0.42f, gy + 0.42f, 0, Z(10), rockTop, rockL, rockR);
        p.Prism(gx + 0.15f, gy + 0.55f, gx + 0.45f, gy + 0.85f, 0, Z(8), RGB(0.92f, 0.88f, 0.78f), RGB(0.80f, 0.75f, 0.64f), RGB(0.6f, 0.55f, 0.46f), true, mortar());
        p.Prism(gx + 0.60f, gy + 0.62f, gx + 0.90f, gy + 0.9f, 0, Z(8), RGB(0.92f, 0.88f, 0.78f), RGB(0.80f, 0.75f, 0.64f), RGB(0.6f, 0.55f, 0.46f), true, mortar());
        Vec2 w = p.S(gx + 0.5f, gy + 0.55f);
        float swing = std::sin(t_ * 5.f);
        p.Figure(w.x, w.y, 1.0f, RGB(0.55f, 0.35f, 0.18f), RGB(0.95f, 0.8f, 0.65f), 0.f);
        p.Line({ w.x + 3, w.y - 11 }, { w.x + 10, w.y - 15 + swing * 7 }, 1.8f, RGB(0.4f, 0.28f, 0.15f));
    }

    void Ballista(gfx::Painter& p, const Bld& b) {
        int gx = b.gx, gy = b.gy;
        p.Prism(gx + 0.2f, gy + 0.2f, gx + 0.8f, gy + 0.8f, 0, Z(36), RGB(0.70f, 0.52f, 0.30f), RGB(0.64f, 0.46f, 0.26f), RGB(0.44f, 0.30f, 0.16f));
        Vec2 A = p.S(gx + 0.2f, gy + 0.8f, 0), B = p.S(gx + 0.8f, gy + 0.8f, 0);
        for (int k = 1; k < 4; ++k) p.Line(LerpV(A, B, k / 4.f), { LerpV(A, B, k / 4.f).x, LerpV(A, B, k / 4.f).y - Z(36) }, 1.4f, RGB(0.33f, 0.2f, 0.1f));
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f, Z(40));
        p.Ellipse(c.x, c.y, 17, 8, RGB(0.58f, 0.42f, 0.24f), 10);
        float a = b.aim;
        p.Line({ c.x - std::cos(a) * 9, c.y - 5 - std::sin(a) * 4 }, { c.x + std::cos(a) * 14, c.y - 5 + std::sin(a) * 6 }, 2.6f, RGB(0.25f, 0.18f, 0.10f));
        p.Line({ c.x - 8, c.y - 12 }, { c.x + 8, c.y + 0 }, 2.f, RGB(0.7f, 0.7f, 0.74f));
        Flag(p, c.x, c.y - 6, 20, RGB(0.95f, 0.82f, 0.22f), gx * 0.7f);
    }

    void Market(gfx::Painter& p, int gx, int gy) {
        const Color aw[3][2] = { { RGB(0.85f, 0.2f, 0.2f), RGB(0.98f, 0.95f, 0.9f) }, { RGB(0.2f, 0.4f, 0.85f), RGB(0.98f, 0.95f, 0.9f) }, { RGB(0.95f, 0.7f, 0.15f), RGB(0.98f, 0.95f, 0.9f) } };
        for (int s = 0; s < 3; ++s) {
            float x0 = gx + 0.06f + s * 0.31f, x1 = x0 + 0.27f, y0 = gy + 0.3f + 0.1f * (s & 1), y1 = y0 + 0.4f;
            p.Prism(x0, y0, x1, y1, 0, Z(12), RGB(0.7f, 0.5f, 0.3f), RGB(0.62f, 0.44f, 0.26f), RGB(0.45f, 0.3f, 0.17f));
            Vec2 a = p.S(x0 - 0.03f, y1 + 0.06f, Z(26)), b = p.S(x1 + 0.03f, y1 + 0.06f, Z(26)), c = p.S(x1 + 0.03f, y0, Z(34)), d = p.S(x0 - 0.03f, y0, Z(34));
            for (int k = 0; k < 4; ++k) {
                float t0 = k / 4.f, t1 = (k + 1) / 4.f;
                p.Quad(LerpV(a, b, t0), LerpV(a, b, t1), LerpV(d, c, t1), LerpV(d, c, t0), aw[s][k & 1]);
            }
            Vec2 g = p.S((x0 + x1) / 2, y1, Z(12));
            p.Disc(g.x - 3, g.y - 2, 2.6f, RGB(0.9f, 0.2f, 0.15f)); p.Disc(g.x + 3, g.y - 2, 2.6f, RGB(0.95f, 0.75f, 0.2f));
        }
    }

    void Lumber(gfx::Painter& p, int gx, int gy) {
        p.Prism(gx + 0.1f, gy + 0.12f, gx + 0.6f, gy + 0.55f, 0, Z(14), RGB(0.64f, 0.46f, 0.26f), RGB(0.6f, 0.42f, 0.24f), RGB(0.42f, 0.28f, 0.15f));
        Gable(p, gx + 0.06f, gy + 0.08f, gx + 0.64f, gy + 0.6f, Z(14), Z(12), RGB(0.45f, 0.3f, 0.17f), RGB(0.25f, 0.15f, 0.08f), RGB(0.6f, 0.42f, 0.24f));
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3 - r; ++k) {
                Vec2 q = p.S(gx + 0.5f + k * 0.14f + r * 0.07f, gy + 0.85f);
                p.Disc(q.x, q.y - 3 - r * 5, 4.2f, RGB(0.55f, 0.36f, 0.17f));
                p.Disc(q.x, q.y - 3 - r * 5, 2.4f, RGB(0.85f, 0.68f, 0.42f));
            }
    }

    void Vault(gfx::Painter& p, int gx, int gy) {
        p.Prism(gx + 0.15f, gy + 0.2f, gx + 0.85f, gy + 0.8f, 0, Z(24), stoneTop(), stoneL(), stoneR(), true, mortar());
        HipRoof(p, gx + 0.1f, gy + 0.15f, gx + 0.9f, gy + 0.85f, Z(24), Z(14), RGB(0.28f, 0.34f, 0.5f));
        Vec2 A = p.S(gx + 0.15f, gy + 0.8f, 0), B = p.S(gx + 0.85f, gy + 0.8f, 0);
        auto at = [&](float t, float z) { Vec2 q = LerpV(A, B, t); return Vec2{ q.x, q.y - Z(z) }; };
        p.Quad(at(0.3f, 0), at(0.7f, 0), at(0.7f, 14), at(0.3f, 14), RGB(0.9f, 0.7f, 0.15f));
        p.Quad(at(0.3f, 0), at(0.5f, 0), at(0.5f, 14), at(0.3f, 14), RGB(0.7f, 0.5f, 0.08f));
        Vec2 g = p.S(gx + 0.5f, gy + 0.95f);
        for (int k = 0; k < 5; ++k) p.Disc(g.x - 8 + k * 4, g.y - 2 - (k % 2) * 3, 3.2f, RGB(1.f, 0.82f, 0.2f));
        float tw = std::fmod(t_ * 1.3f, 1.f);
        p.Star(g.x - 4, g.y - 8, 4 * (1 - tw), RGB(1, 1, 0.8f, 1 - tw));
    }

    void Cannon(gfx::Painter& p, const Bld& b) {
        Vec2 c = p.S(b.gx + 0.5f, b.gy + 0.5f);
        p.Cylinder(c.x, c.y, 18, 0, Z(18), stoneL(), stoneTop(), true, mortar(), 12);
        float a = b.aim;
        Vec2 base{ c.x, c.y - Z(22) };
        p.Line(base, { base.x + std::cos(a) * 16, base.y + std::sin(a) * 8 - 3 }, 7.f, RGB(0.14f, 0.14f, 0.17f));
        p.Disc(base.x, base.y, 7.f, RGB(0.2f, 0.2f, 0.24f));
        if (b.cd > 2.1f) p.Star(base.x + std::cos(a) * 20, base.y + std::sin(a) * 10 - 3, 8, RGB(1.f, 0.8f, 0.3f));
    }

    void WatchTower(gfx::Painter& p, int gx, int gy) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        float R = 0.30f * kTW * 0.7071f;
        p.Cylinder(c.x, c.y, R, 0, Z(46), stoneL(), stoneTop(), true, mortar());
        p.Cone(c.x, c.y, R + 4, Z(46), Z(28), RGB(0.30f, 0.45f, 0.82f));
        Flag(p, c.x, c.y - Z(74), 14, RGB(0.95f, 0.82f, 0.22f), gx * 0.7f);
    }

    void Prop(gfx::Painter& p, float gx, float gy, float h) {
        Vec2 c = p.S(gx + 0.5f, gy + 0.5f);
        if (h < 0.3f) {
            p.Cylinder(c.x, c.y, 6, 0, 9, RGB(0.62f, 0.40f, 0.18f), RGB(0.75f, 0.52f, 0.28f), false, Color{}, 10);
            p.Line({ c.x - 6, c.y - 3 }, { c.x + 6, c.y - 3 }, 1.2f, RGB(0.25f, 0.2f, 0.18f));
        } else if (h < 0.5f) {
            p.Prism(gx + 0.3f, gy + 0.3f, gx + 0.62f, gy + 0.62f, 0, Z(10), RGB(0.80f, 0.62f, 0.35f), RGB(0.68f, 0.50f, 0.26f), RGB(0.50f, 0.36f, 0.18f));
        } else if (h < 0.75f) {
            p.Ellipse(c.x, c.y + 3, 12, 5, RGB(0.1f, 0.25f, 0.05f, 0.3f), 10);
            p.Disc(c.x - 4, c.y - 4, 8, RGB(0.18f, 0.48f, 0.15f)); p.Disc(c.x + 4, c.y - 3, 7, RGB(0.24f, 0.58f, 0.19f));
        } else {   // little tree / well
            p.Ellipse(c.x + 3, c.y + 2, 12, 4, RGB(0.05f, 0.15f, 0.03f, 0.3f), 8);
            p.Fill(c.x - 2, c.y - 12, 4, 13, RGB(0.40f, 0.26f, 0.12f));
            p.Disc(c.x, c.y - 17, 10, RGB(0.17f, 0.50f, 0.16f)); p.Disc(c.x - 3, c.y - 21, 6, RGB(0.30f, 0.66f, 0.22f));
        }
    }

    void Tree(gfx::Painter& p, float x, float y, float seed) {
        float s = 0.9f + 0.4f * Hash01((int)seed * 3);
        p.Ellipse(x + 5, y + 3, 22 * s, 8 * s, RGB(0.45f, 0.55f, 0.70f, 0.35f), 12);
        p.Fill(x - 2.5f * s, y - 8 * s, 5 * s, 10 * s, RGB(0.34f, 0.22f, 0.12f));
        for (int k = 0; k < 4; ++k) {
            float w = (24 - k * 5) * s, yb = y - (6 + k * 14) * s, h = 22 * s, sw = std::sin(t_ * 1.2f + seed + k) * 1.2f;
            p.Tri({ x - w, yb }, { x + w, yb }, { x + sw, yb - h }, RGB(0.10f + 0.03f * k, 0.34f + 0.04f * k, 0.24f));
            p.Tri({ x - w * 0.72f, yb - h * 0.28f }, { x + w * 0.72f, yb - h * 0.28f }, { x + sw, yb - h }, RGB(0.96f, 0.98f, 1.0f));
        }
    }

    // ============================================================ effects
    void Flame(gfx::Painter& p, float x, float y, float s) {
        for (int k = 0; k < 3; ++k) {
            float h = (16 + 9 * std::sin(t_ * 14.f + k * 2.1f)) * s, w = (7 - k * 1.8f) * s, off = (k - 1) * 3.f * s;
            Color c = k == 0 ? RGB(1.0f, 0.32f, 0.05f) : (k == 1 ? RGB(1.0f, 0.62f, 0.10f) : RGB(1.0f, 0.95f, 0.45f));
            p.Tri({ x - w + off, y }, { x + w + off, y }, { x + std::sin(t_ * 9.f + k) * 2.f * s + off, y - h }, c);
        }
    }
    void Smoke(gfx::Painter& p, float x, float y, float seed, float size) {
        if (size <= 0.02f) return;
        for (int i = 0; i < 4; ++i) {
            float a = std::fmod(t_ * 0.45f + seed + i / 4.f, 1.f);
            float xx = x + std::sin(a * 6.f + seed * 9.f) * 6.f * size + a * 14.f * size;
            float yy = y - a * 50.f * size;
            p.Ellipse(xx, yy, (4.f + a * 11.f) * size, (4.f + a * 11.f) * size, RGB(0.38f, 0.38f, 0.42f, (1.f - a) * 0.5f), 9);
        }
    }
    void Flag(gfx::Painter& p, float x, float y, float h, Color col, float seed) {
        p.Line({ x, y }, { x, y - h }, 2.f, RGB(0.32f, 0.22f, 0.12f));
        float prevTop = y - h, prevBot = y - h + 9;
        for (int i = 1; i <= 4; ++i) {
            float wave = std::sin(t_ * 6.f + i * 0.9f + seed) * 2.f * i / 4.f;
            float nt = y - h + wave + i * 0.7f, nb = y - h + 9 + wave + i * 0.7f;
            p.Quad({ x + (i - 1) * 5.f, prevTop }, { x + i * 5.f, nt }, { x + i * 5.f, nb }, { x + (i - 1) * 5.f, prevBot }, Mul(col, 0.85f + 0.15f * (i & 1)));
            prevTop = nt; prevBot = nb;
        }
    }

    void DrawParticles(gfx::Painter& p) {
        for (auto& q : parts_) {
            float u = 1.f - q.life / q.maxLife;
            Vec2 s = p.S(q.x, q.y, q.z);
            switch (q.kind) {
            case 0: p.Ellipse(s.x, s.y, q.size * (0.5f + u), q.size * (0.5f + u), Alpha(q.col, q.col.a * (1 - u)), 9); break;
            case 1: p.Fill(s.x - 1.8f, s.y - 1.8f, 3.6f, 3.6f, Alpha(q.col, 1.f)); break;
            case 2: p.Ellipse(s.x, s.y, q.size * (0.3f + u * 1.4f), q.size * (0.3f + u * 1.4f) * 0.5f, Alpha(q.col, (1 - u) * 0.45f), 16); break;
            case 3: p.Star(s.x, s.y, q.size * (0.7f + 0.3f * std::sin(u * 20)), Alpha(q.col, 1 - u * u)); break;
            case 4: p.Star(s.x, s.y, q.size * (1 - u * 0.5f), Alpha(q.col, 1 - u)); break;
            }
        }
    }

    void DrawProjectiles(gfx::Painter& p) {
        for (auto& q : projs_) {
            if (q.delay > 0) { if (q.kind == 3) { Vec2 g = p.S(q.x1, q.y1); p.Ellipse(g.x, g.y, 10, 5, RGB(1.f, 0.4f, 0.1f, 0.25f), 10); } continue; }
            float u = std::min(1.f, q.t / q.dur);
            auto pos = [&](float uu) {
                float x = q.x0 + (q.x1 - q.x0) * uu, y = q.y0 + (q.y1 - q.y0) * uu;
                float z = q.z0 * (1 - uu) + std::sin(uu * gfx::kPi) * q.arc;
                return p.S(x, y, z);
            };
            Vec2 a = pos(u), b = pos(std::min(1.f, u + 0.05f));
            switch (q.kind) {
            case 0: p.Line(a, b, 2.2f, RGB(0.3f, 0.2f, 0.1f)); p.Disc(b.x, b.y, 1.8f, RGB(0.85f, 0.85f, 0.9f)); break;
            case 1: p.Disc(a.x, a.y, 4.f, RGB(0.12f, 0.12f, 0.14f)); break;
            case 2: p.Disc(a.x, a.y, 5.f, RGB(0.5f, 0.45f, 0.4f)); break;
            case 3: { Vec2 g = p.S(q.x1, q.y1); p.Ellipse(g.x, g.y, 10 + 8 * u, (10 + 8 * u) * 0.5f, RGB(1.f, 0.4f, 0.1f, 0.3f), 10);
                      p.Disc(a.x, a.y, 8.f, RGB(1.f, 0.5f, 0.1f)); p.Disc(a.x, a.y, 4.5f, RGB(1.f, 0.9f, 0.5f));
                      Vec2 tail = pos(std::max(0.f, u - 0.08f)); p.Line(tail, a, 6.f, RGB(1.f, 0.4f, 0.1f, 0.5f)); break; }
            }
        }
    }

    // ============================================================ units
    static float BaseScale(int t) { static const float v[kUT] = { 1.25f, 1.25f, 1.1f, 2.1f, 1.2f, 1.7f, 1.5f }; return v[t]; }
    static float IconMul(int t)   { static const float v[kUT] = { 2.0f, 2.0f, 1.45f, 1.2f, 1.6f, 1.45f, 1.35f }; return v[t]; }

    // x,y = ground position on screen. k = extra scale (icons use >1).
    void DrawUnitSprite(gfx::Painter& p, float x, float y, int type, float k, float phase, bool flip, float rage, float atk, float alpha = 1.f) {
        float s = BaseScale(type) * k, dir = flip ? -1.f : 1.f;
        const Color skin = RGB(0.95f, 0.78f, 0.62f);
        y -= std::sin(std::min(1.f, std::max(0.f, atk)) * gfx::kPi) * 2.f * s;
        auto A = [&](Color c) { c.a *= alpha; return c; };
        switch (type) {
        case kSword: {
            p.Figure(x, y, s, A(RGB(0.18f, 0.38f, 0.92f)), A(skin), phase, flip);
            float sx = x - 6.f * s * dir, sy = y - 9.f * s;
            p.Quad({ sx - 3 * s, sy - 4 * s }, { sx + 3 * s, sy - 4 * s }, { sx + 2 * s, sy + 4 * s }, { sx - 2 * s, sy + 4 * s }, A(RGB(0.93f, 0.93f, 0.97f)));
            p.Fill(sx - 0.8f * s, sy - 4 * s, 1.6f * s, 8 * s, A(RGB(0.15f, 0.3f, 0.8f)));
            if (atk > 0) p.Line({ x + 4 * s * dir, y - 12 * s }, { x + (5 + 8 * atk) * s * dir, y - (6 + 8 * atk) * s }, 1.6f * s, A(RGB(0.95f, 0.97f, 1.f)));
            break; }
        case kArcher: {
            p.Figure(x, y, s, A(RGB(0.16f, 0.58f, 0.26f)), A(skin), phase, flip);
            p.Tri({ x - 3.6f * s, y - 17 * s }, { x + 3.6f * s, y - 17 * s }, { x, y - 24 * s }, A(RGB(0.1f, 0.42f, 0.18f)));
            Vec2 t0{ x + 7 * s * dir, y - 18 * s }, t1{ x + 10 * s * dir, y - 11 * s }, t2{ x + 7 * s * dir, y - 4 * s };
            p.Line(t0, t1, 1.5f * s, A(RGB(0.5f, 0.32f, 0.14f))); p.Line(t1, t2, 1.5f * s, A(RGB(0.5f, 0.32f, 0.14f)));
            p.Line(t0, t2, 0.8f, A(RGB(0.9f, 0.9f, 0.9f)));
            break; }
        case kKnight: {
            float bob = std::sin(phase * 1.2f) * 1.0f * s, sw = std::sin(phase) * 3.f * s;
            const Color horse = A(RGB(0.52f, 0.33f, 0.18f));
            for (float lx : { -7.f, -4.f, 4.f, 7.f }) p.Line({ x + lx * s, y - 7 * s }, { x + lx * s + sw * (lx > 0 ? 1 : -1), y }, 2.f * s, A(RGB(0.28f, 0.17f, 0.1f)));
            p.Ellipse(x, y - 9 * s + bob, 11 * s, 5.5f * s, horse, 12);
            p.Quad({ x + 7 * s * dir, y - 11 * s }, { x + 11 * s * dir, y - 19 * s }, { x + 14 * s * dir, y - 17 * s }, { x + 10 * s * dir, y - 9 * s }, horse);
            p.Ellipse(x + 14 * s * dir, y - 18 * s, 3.5f * s, 2.5f * s, horse, 8);
            p.Quad({ x - 5 * s, y - 13 * s }, { x + 5 * s, y - 13 * s }, { x + 5 * s, y - 11 * s }, { x - 5 * s, y - 11 * s }, A(RGB(0.75f, 0.12f, 0.12f)));
            p.Figure(x - 1 * s * dir, y - 9 * s + bob, s * 0.85f, A(RGB(0.75f, 0.78f, 0.86f)), A(skin), 0, flip);
            p.Line({ x + 3 * s * dir, y - 16 * s }, { x + 17 * s * dir, y - 25 * s }, 1.3f * s, A(RGB(0.85f, 0.85f, 0.9f)));
            break; }
        case kGiant: {
            p.Ellipse(x, y - 1, 9 * s, 3 * s, A(RGB(0, 0, 0, 0.0f)), 4);
            p.Figure(x, y, s, A(RGB(0.82f, 0.42f, 0.14f)), A(RGB(0.9f, 0.68f, 0.5f)), phase, flip);
            p.Fill(x - 3.3f * s, y - 18.5f * s, 6.6f * s, 3.5f * s, A(RGB(0.35f, 0.2f, 0.1f)));    // hair
            p.Quad({ x - 2 * s * dir, y - 12 * s }, { x + 10 * s * dir, y - 20 * s }, { x + 12 * s * dir, y - 17 * s }, { x, y - 9 * s }, A(RGB(0.45f, 0.3f, 0.15f)));
            p.Disc(x + 11 * s * dir, y - 19 * s, 3.4f * s, A(RGB(0.4f, 0.27f, 0.13f)));
            break; }
        case kCata: {
            float swing = std::max(0.f, atk);
            for (float wx : { -7.f, 7.f }) { p.Disc(x + wx * s, y - 3 * s, 4 * s, A(RGB(0.25f, 0.16f, 0.09f))); p.Disc(x + wx * s, y - 3 * s, 1.6f * s, A(RGB(0.6f, 0.45f, 0.25f))); }
            p.RotRect(x, y - 7 * s, 17 * s, 4 * s, 0, A(RGB(0.55f, 0.37f, 0.18f)));
            float ang = -1.9f + swing * 1.4f;
            Vec2 tip{ x + std::cos(ang) * 14 * s * dir, y - 8 * s + std::sin(ang) * 14 * s };
            p.Line({ x, y - 8 * s }, tip, 2.2f * s, A(RGB(0.45f, 0.30f, 0.15f)));
            if (swing < 0.5f) p.Disc(tip.x, tip.y, 2.6f * s, A(RGB(0.5f, 0.47f, 0.42f)));
            p.Figure(x - 9 * s * dir, y, s * 0.55f, A(RGB(0.18f, 0.38f, 0.92f)), A(skin), phase, flip);
            break; }
        case kKing: {
            if (rage > 0) p.Ellipse(x, y - 1, 13 * s, 5 * s, RGB(1.f, 0.8f, 0.2f, 0.45f + 0.2f * std::sin(phase * 3)), 14);
            p.Quad({ x - 4 * s, y - 12 * s }, { x + 4 * s, y - 12 * s }, { x + 6 * s - 3 * s * dir, y - 2 * s }, { x - 6 * s - 3 * s * dir, y - 2 * s }, A(RGB(0.75f, 0.1f, 0.12f)));
            p.Figure(x, y, s, A(RGB(0.95f, 0.75f, 0.2f)), A(skin), phase, flip);
            for (int c = -1; c <= 1; ++c) p.Tri({ x + c * 2.2f * s - 1.2f * s, y - 18.5f * s }, { x + c * 2.2f * s + 1.2f * s, y - 18.5f * s }, { x + c * 2.2f * s, y - 23 * s }, A(RGB(1.f, 0.9f, 0.3f)));
            p.Line({ x + 3.5f * s * dir, y - 10 * s }, { x + 10 * s * dir, y - 21 * s }, 1.8f * s, A(RGB(1.f, 0.95f, 0.7f)));
            break; }
        case kMage: {
            float pulse = 0.7f + 0.3f * std::sin(phase * 2.f);
            p.Figure(x, y, s, A(RGB(0.5f, 0.2f, 0.82f)), A(skin), phase, flip);
            p.Tri({ x - 4.8f * s, y - 18.5f * s }, { x + 4.8f * s, y - 18.5f * s }, { x + 1.5f * s * dir, y - 29 * s }, A(RGB(0.4f, 0.15f, 0.7f)));
            p.Star(x, y - 22 * s, 1.8f * s, A(RGB(1.f, 0.9f, 0.3f)));
            p.Line({ x - 5 * s * dir, y - 3 * s }, { x - 6 * s * dir, y - 22 * s }, 1.4f * s, A(RGB(0.45f, 0.3f, 0.15f)));
            p.Disc(x - 6 * s * dir, y - 23 * s, 3.2f * s * pulse, A(RGB(0.4f, 0.8f, 1.f)));
            break; }
        }
    }

    void DrawUnit(gfx::Painter& p, int i) {
        const Unit& u = units_[i];
        Vec2 sp = p.S(u.x, u.y);
        if (!u.alive) {
            float a = std::max(0.f, 1.f - u.deadT / 1.2f);
            p.RotRect(sp.x, sp.y - 3, 16, 7, 0.1f, RGB(0.18f, 0.36f, 0.85f, a));
            p.Disc(sp.x + 9, sp.y - 3, 3.f, RGB(0.9f, 0.72f, 0.6f, a));
            return;
        }
        float w = St(u.type).hero ? 15.f : (u.type == kGiant ? 14.f : 9.f);
        p.Ellipse(sp.x, sp.y, w, w * 0.45f, RGB(0.15f, 0.45f, 1.f, 0.40f), 12);
        p.Ellipse(sp.x, sp.y, w * 0.6f, w * 0.27f, RGB(0, 0, 0, 0.28f), 10);
        DrawUnitSprite(p, sp.x, sp.y, u.type, 1.f, u.phase, u.flip, u.rage, u.atk);
    }

    void DrawDefender(gfx::Painter& p, int i) {
        const Def& d = defs_[i];
        Vec2 sp = p.S(d.x, d.y);
        if (!d.alive) {
            float a = std::max(0.f, 1.f - d.deadT / 1.2f);
            p.RotRect(sp.x, sp.y - 3, 14, 6, -0.1f, RGB(0.9f, 0.7f, 0.2f, a));
            return;
        }
        p.Ellipse(sp.x, sp.y, 8, 3.6f, RGB(0, 0, 0, 0.28f), 10);
        p.Figure(sp.x, sp.y, 1.15f, RGB(0.95f, 0.75f, 0.20f), RGB(0.95f, 0.8f, 0.65f), d.phase, d.flip);
    }

    void HpBar(gfx::Painter& p, float x, float y, float w, float f) {
        f = std::clamp(f, 0.f, 1.f);
        p.Fill(x - w / 2 - 1, y - 1, w + 2, 5, RGB(0.05f, 0.05f, 0.07, 0.85f));
        Color c = f > 0.5f ? RGB(0.35f, 0.85f, 0.25f) : (f > 0.25f ? RGB(0.95f, 0.75f, 0.15f) : RGB(0.9f, 0.2f, 0.15f));
        p.Fill(x - w / 2, y, w * f, 3, c);
    }

    void DrawBars(gfx::Painter& p) {
        for (auto& b : blds_) {
            if (b.dead || b.hp >= b.maxHp) continue;
            Vec2 c = p.S(b.gx + 0.5f, b.gy + 0.5f, Z(b.top) + 10);
            HpBar(p, c.x, c.y, b.c == 'K' ? 50.f : 30.f, b.hp / b.maxHp);
        }
        for (auto& u : units_) {
            if (!u.alive || (u.hp >= u.maxHp && !St(u.type).hero)) continue;
            Vec2 sp = p.S(u.x, u.y);
            float h = BaseScale(u.type) * (u.type == kKnight ? 26.f : 25.f);
            HpBar(p, sp.x, sp.y - h, St(u.type).hero ? 30.f : 18.f, u.hp / u.maxHp);
        }
    }

    void DrawCursor(gfx::Painter& p) {
        if (over_ || counts_[sel_] <= 0 || !ValidDeploy(mx_, my_)) return;
        float pul = 1.f + 0.12f * std::sin(t_ * 8.f);
        const int n = 20;
        for (int i = 0; i < n; ++i) {
            float a0 = 2 * gfx::kPi * i / n, a1 = 2 * gfx::kPi * (i + 1) / n;
            p.Line({ mx_ + std::cos(a0) * 20 * pul, my_ + std::sin(a0) * 10 * pul }, { mx_ + std::cos(a1) * 20 * pul, my_ + std::sin(a1) * 10 * pul }, 2.2f, RGB(1, 1, 1, 0.85f));
        }
        DrawUnitSprite(p, mx_, my_ + 3, sel_, 1.f, 0, false, 0, 0, 0.5f);
    }

    // ============================================================ HUD
    enum Icon { kApple, kWood, kStone };

    void DrawIcon(gfx::Painter& p, float x, float y, Icon ic) {
        p.Ellipse(x + 2, y + 18, 17, 5, RGB(0, 0, 0, 0.3f), 12);
        if (ic == kApple) {
            p.Disc(x - 5, y + 2, 14, RGB(0.70f, 0.06f, 0.06f)); p.Disc(x + 5, y + 2, 14, RGB(0.70f, 0.06f, 0.06f));
            p.Disc(x - 4, y + 1, 12, RGB(0.90f, 0.14f, 0.12f)); p.Disc(x + 5, y + 1, 12, RGB(0.90f, 0.14f, 0.12f));
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
        std::string mt = "Max: " + maxText;
        float mw = p.TextWidth(mt, 15);
        gfx::DrawTextOutlined(p, right ? bx + 6 : bx + w - mw - 4, y - 13, 15, mt, RGB(1, 1, 1));
        p.Fill(bx - 3, y - 3, w + 6, h + 6, RGB(0.08f, 0.08f, 0.10f));
        p.Fill(bx - 1.5f, y - 1.5f, w + 3, h + 3, RGB(0.70f, 0.72f, 0.78f));
        p.Fill(bx, y, w, h, back);
        float fw = w * std::clamp(frac, 0.f, 1.f);
        p.VGrad(bx, y, fw, h, top, bot);
        p.Fill(bx, y, fw, h * 0.38f, Alpha(RGB(1, 1, 1), 0.25f));
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
            for (int k = -1; k <= 1; ++k) p.Disc(ix + k * 10, y - 16, 3.4f, RGB(1.0f, 0.95f, 0.6f));
            p.Disc(ix, y + 5, 3.2f, RGB(0.85f, 0.15f, 0.2f));
        }
        float tw = p.TextWidth(text, 26);
        gfx::DrawTextOutlined(p, right ? ix - 26 - tw : ix + 26, y - 9, 26, text, RGB(1, 1, 1));
    }

    void DrawHud(gfx::Painter& p) {
        // base numbers (ours start part-full and fill with loot; the enemy's storage drains)
        static const float ours0[3] = { 12000, 8000, 3200 }, oursMax[3] = { 20998, 13342, 6445 };
        static const float opp[3] = { 46705, 121789, 56535 }, oppMax[3] = { 267500, 300000, 170000 };
        float loot[3], mine[3], theirs[3];
        for (int i = 0; i < 3; ++i) {
            loot[i] = opp[i] * 0.45f * pctDisp_;
            mine[i] = std::min(oursMax[i], ours0[i] + loot[i]);
            theirs[i] = opp[i] - loot[i];
        }
        lootShown_[0] = (int)loot[0]; lootShown_[1] = (int)loot[1]; lootShown_[2] = (int)loot[2];

        const Color rt = RGB(0.95f, 0.30f, 0.26f), rb = RGB(0.70f, 0.10f, 0.10f), rk = RGB(0.30f, 0.05f, 0.05f);
        const Color wt = RGB(1.00f, 0.72f, 0.22f), wb = RGB(0.85f, 0.45f, 0.06f), wk = RGB(0.35f, 0.18f, 0.03f);
        const Color st = RGB(0.32f, 0.62f, 1.00f), sb = RGB(0.12f, 0.36f, 0.82f), sk = RGB(0.05f, 0.14f, 0.35f);
        ResourceBar(p, 14, 48, 236, mine[0] / oursMax[0], rt, rb, rk, gfx::FmtInt((int)mine[0]), gfx::FmtInt((int)oursMax[0]), false, kApple);
        ResourceBar(p, 14, 104, 236, mine[1] / oursMax[1], wt, wb, wk, gfx::FmtInt((int)mine[1]), gfx::FmtInt((int)oursMax[1]), false, kWood);
        ResourceBar(p, 14, 160, 236, mine[2] / oursMax[2], st, sb, sk, gfx::FmtInt((int)mine[2]), gfx::FmtInt((int)oursMax[2]), false, kStone);
        Badge(p, 18, 218, "504", false, false);
        Badge(p, 18, 262, "19", true, false);

        float rx = kCanvasW - 14 - 236 - 30;
        const Color gt = RGB(0.45f, 0.80f, 0.32f), gb = RGB(0.20f, 0.52f, 0.14f), gk = RGB(0.07f, 0.20f, 0.06f);
        ResourceBar(p, rx, 48, 236, theirs[0] / oppMax[0], gt, gb, gk, gfx::FmtInt((int)theirs[0]), gfx::FmtInt((int)oppMax[0]), true, kApple);
        ResourceBar(p, rx, 104, 236, theirs[1] / oppMax[1], gt, gb, gk, gfx::FmtInt((int)theirs[1]), gfx::FmtInt((int)oppMax[1]), true, kWood);
        ResourceBar(p, rx, 160, 236, theirs[2] / oppMax[2], gt, gb, gk, gfx::FmtInt((int)theirs[2]), gfx::FmtInt((int)oppMax[2]), true, kStone);
        Badge(p, kCanvasW - 18, 218, "4,597", false, true);
        Badge(p, kCanvasW - 18, 262, "2,408", true, true);

        // stars light up as the attack succeeds
        int stars = Stars();
        for (int i = 0; i < 3; ++i) {
            float sx = 590 + i * 50.f, sy = 32;
            bool on = i < stars;
            float pop = 1.f;
            if (on && starT_[i] >= 0) { float a = t_ - starT_[i]; if (a < 0.5f) pop = 1.f + 0.6f * std::sin(a / 0.5f * gfx::kPi); }
            p.Star(sx, sy + 1, 22 * pop, RGB(0.10f, 0.10f, 0.12f));
            p.Star(sx, sy, 19 * pop, on ? RGB(1.0f, 0.78f, 0.12f) : RGB(0.70f, 0.72f, 0.76f));
            p.Star(sx - 1, sy - 2, 11 * pop, on ? RGB(1.0f, 0.95f, 0.6f) : RGB(0.88f, 0.90f, 0.94f));
        }
        gfx::DrawTextCentered(p, 640, 66, 18, std::to_string((int)std::round(pctDisp_ * 100.f)) + "%", RGB(1, 1, 1));
        int secs = (int)timeLeft_;
        gfx::DrawTextCentered(p, 640, 94, 20, "Battle ends in:", RGB(1.0f, 0.95f, 0.75f));
        gfx::DrawTextCentered(p, 640, 124, 30, std::to_string(secs / 60) + "m " + std::to_string(secs % 60) + "s", RGB(1, 1, 1));

        if (!started_ && !over_) {
            float a = 0.6f + 0.4f * std::sin(t_ * 4.f);
            gfx::DrawTextCentered(p, 640, 584, 22, "Tap outside the walls to deploy your troops", RGB(1.f, 1.f, 0.8f, a));
        }

        // bottom corner buttons
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

    // ---- bottom deploy bar: 5 troop slots + 2 hero slots, counts update live
    void DrawTroopBar(gfx::Painter& p) {
        p.Fill(240, 606, 800, 114, RGB(0.04f, 0.04f, 0.07f, 0.55f));
        p.Fill(240, 606, 800, 2, RGB(1, 1, 1, 0.25f));
        for (int i = 0; i < kUT; ++i) {
            const UStat& st = St(i);
            Rect r = SlotRect(i);
            bool sel = (i == sel_) && counts_[i] > 0;
            bool hero = st.hero;
            bool deployed = hero && counts_[i] == 0;
            Unit* hu = (hero && heroId_[i - kKing] >= 0) ? &units_[heroId_[i - kKing]] : nullptr;
            bool heroAlive = hu && hu->alive;
            bool avail = counts_[i] > 0;
            bool abilityReady = deployed && heroAlive && heroCd_[i - kKing] <= 0.f;
            float y = r.y - (sel ? 9.f : 0.f);

            p.Ellipse(r.x + r.w / 2, r.y + r.h + 2, r.w * 0.5f, 6, RGB(0, 0, 0, 0.35f), 12);
            if (sel) p.Fill(r.x - 4, y - 4, r.w + 8, r.h + 8, RGB(1.0f, 0.86f, 0.25f));
            else if (abilityReady) p.Fill(r.x - 4, y - 4, r.w + 8, r.h + 8, RGB(1.0f, 0.9f, 0.4f, 0.55f + 0.45f * std::sin(t_ * 8.f)));
            Color base = hero ? RGB(0.68f, 0.50f, 0.16f) : RGB(0.34f, 0.40f, 0.60f);
            if (!avail && !deployed) base = RGB(0.30f, 0.30f, 0.34f);
            p.Bevel(r.x, y, r.w, r.h, base);
            p.VGrad(r.x + 6, y + 6, r.w - 12, 56, hero ? RGB(0.45f, 0.30f, 0.62f) : RGB(0.42f, 0.62f, 0.86f),
                    hero ? RGB(0.22f, 0.14f, 0.36f) : RGB(0.22f, 0.36f, 0.58f));
            DrawUnitSprite(p, r.x + r.w / 2, y + 60, i, IconMul(i), t_ * 5.f + i, false, (heroAlive && hu->rage > 0) ? 1.f : 0.f, 0, (avail || (deployed && heroAlive)) ? 1.f : 0.35f);

            // count / status pill
            p.Fill(r.x + 8, y + r.h - 29, r.w - 16, 22, RGB(0.04f, 0.04f, 0.08f, 0.8f));
            std::string label;
            if (!hero) label = "x" + std::to_string(counts_[i]);
            else if (!deployed) label = "HERO";
            else if (!heroAlive) label = "FALLEN";
            else label = abilityReady ? "SKILL!" : std::to_string((int)std::ceil(heroCd_[i - kKing])) + "s";
            Color lc = (!avail && !deployed) ? RGB(0.7f, 0.7f, 0.7f) : (abilityReady ? RGB(1.f, 0.9f, 0.3f) : RGB(1, 1, 1));
            gfx::DrawTextCentered(p, r.x + r.w / 2, y + r.h - 28, 17, label, lc);

            // hero cooldown sweep
            if (deployed && heroAlive && heroCd_[i - kKing] > 0)
                p.Fill(r.x + 6, y + 6, r.w - 12, 56 * (heroCd_[i - kKing] / kAbilityCd), RGB(0, 0, 0, 0.45f));
            if (deployed && !heroAlive) p.Fill(r.x + 6, y + 6, r.w - 12, 56, RGB(0, 0, 0, 0.6f));
            // little name tag
            gfx::DrawTextOutlined(p, r.x + 8, y + 7, 12, st.name, RGB(1, 1, 1, 0.95f));
        }
    }

    // ---- result overlay
    void DrawResult(gfx::Painter& p) {
        float a = std::clamp((endT_ - 0.5f) / 0.5f, 0.f, 1.f);
        p.Fill(0, 0, kCanvasW, kCanvasH, RGB(0, 0, 0, 0.55f * a));
        if (a < 0.2f) return;
        float pw = 520, ph = 420, px = (kCanvasW - pw) / 2, py = (kCanvasH - ph) / 2 - 4 + (1 - a) * 30;
        p.Bevel(px, py, pw, ph, RGB(0.32f, 0.36f, 0.52f));
        int stars = Stars();
        bool win = stars > 0;
        gfx::DrawTextCentered(p, px + pw / 2, py + 22, 48, win ? "VICTORY!" : "DEFEAT", win ? RGB(1.f, 0.85f, 0.25f) : RGB(0.95f, 0.35f, 0.3f));
        for (int i = 0; i < 3; ++i) {
            float sx = px + pw / 2 + (i - 1) * 84, sy = py + 130;
            float grow = std::clamp((endT_ - 0.6f - i * 0.25f) / 0.3f, 0.f, 1.f);
            bool on = i < stars;
            p.Star(sx, sy + 2, 40, RGB(0.10f, 0.10f, 0.12f));
            p.Star(sx, sy, 36 * (on ? grow : 1.f), on ? RGB(1.0f, 0.78f, 0.12f) : RGB(0.55f, 0.57f, 0.62f));
            if (on) p.Star(sx - 2, sy - 4, 20 * grow, RGB(1.0f, 0.95f, 0.6f));
        }
        gfx::DrawTextCentered(p, px + pw / 2, py + 184, 28, "Destruction: " + std::to_string((int)std::round(Pct() * 100.f)) + "%", RGB(1, 1, 1));
        float ry = py + 245;
        const Icon ics[3] = { kApple, kWood, kStone };
        for (int i = 0; i < 3; ++i) {
            float cx = px + 90 + i * 170;
            DrawIcon(p, cx, ry + 8, ics[i]);
            gfx::DrawTextOutlined(p, cx + 30, ry - 4, 24, "+" + gfx::FmtInt(lootShown_[i]), RGB(1, 1, 1));
        }
        if (endT_ > 1.0f) {
            auto btn = [&](const Rect& r, bool hot, Color c, const char* txt) {
                float k = hot ? 1.06f : 1.f;
                Rect d{ r.x - r.w * (k - 1) / 2, r.y - r.h * (k - 1) / 2, r.w * k, r.h * k };
                p.Fill(d.x - 4, d.y - 4, d.w + 8, d.h + 8, RGB(0.08f, 0.10f, 0.08f));
                p.Fill(d.x, d.y, d.w, d.h, Mul(c, 0.8f));
                p.Fill(d.x, d.y, d.w, d.h * 0.5f, c);
                p.Fill(d.x, d.y + d.h - 5, d.w, 5, Mul(c, 0.6f));
                gfx::DrawTextCentered(p, d.x + d.w / 2, d.y + d.h / 2 - 14, 32, txt, RGB(1, 1, 1));
            };
            btn(replayRect_, replayHot_, RGB(0.26f, 0.68f, 0.26f), "Replay");
            btn(homeRect_, homeHot_, RGB(0.55f, 0.58f, 0.66f), "Home");
        }
    }

    // ============================================================ state
    Services* s_ = nullptr;
    float t_ = 0, mx_ = 0, my_ = 0;
    float timeLeft_ = 5 * 60 + 37;
    bool fast_ = false, closeHot_ = false, fastHot_ = false, replayHot_ = false, homeHot_ = false;
    Rect closeRect_{}, fastRect_{}, replayRect_{}, homeRect_{};
    std::vector<Item> items_;

    std::vector<Bld> blds_; std::vector<Unit> units_; std::vector<Def> defs_; std::vector<Proj> projs_; std::vector<Part> parts_;
    std::vector<Prp> props_; std::vector<Tre> trees_; std::vector<Vec2> roads_;
    bool inside_[N][N] = {};
    int counts_[kUT] = {};
    int sel_ = 0, heroId_[2] = { -1, -1 };
    float heroCd_[2] = { 0, 0 };
    bool started_ = false, over_ = false, dragOK_ = false, keepDead_ = false;
    float endT_ = 0, idleT_ = 0, deployCd_ = 0, totalHp_ = 1, lostHp_ = 0, pctDisp_ = 0;
    float starT_[3] = { -1, -1, -1 };
    int lootShown_[3] = { 0, 0, 0 };
};

} // namespace

std::unique_ptr<IScene> CreateVillageScene() { return std::make_unique<VillageScene>(); }

} // namespace cs
