#pragma once
// Immediate-mode drawing helpers on top of IRenderDevice:
//  - solid shapes, gradients, bevels, stars
//  - isometric prisms (brick/plaster), shaded cylinders and cone roofs
//  - smooth anti-aliased text from assets/ui/font.png (falls back to a built-in pixel font if missing)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "Engine.h"
#include "FontData.h"

namespace cs::gfx {

constexpr float kPi = 3.14159265f;

inline Color RGB(float r, float g, float b, float a = 1.f) { return Color{ r, g, b, a }; }
inline Color Mul(Color c, float k) { return Color{ std::min(c.r * k, 1.f), std::min(c.g * k, 1.f), std::min(c.b * k, 1.f), c.a }; }
inline Color Lerp(Color a, Color b, float t) {
    return Color{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}
inline Color Alpha(Color c, float a) { return Color{ c.r, c.g, c.b, a }; }

inline Vec2 LerpV(Vec2 a, Vec2 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
inline float Hash01(int n) {   // deterministic pseudo-random 0..1
    unsigned x = (unsigned)n * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28) + 4)) ^ x) * 277803737u;
    x = (x >> 22) ^ x;
    return (x & 0xFFFFFF) / 16777215.f;
}

struct Painter {
    IRenderDevice* r;
    TextureHandle white;
    TextureHandle font = 0;
    bool fontOk = false;
    // isometric projection: grid (gx, gy, z) -> screen
    float ox = 640, oy = 150, tw = 92, th = 46;
    const Rect full{ 0, 0, 1, 1 };

    Vec2 S(float gx, float gy, float z = 0) const {
        return { ox + (gx - gy) * tw / 2, oy + (gx + gy) * th / 2 - z };
    }

    // ------------------------------------------------ basic shapes
    void Fill(float x, float y, float w, float h, Color c) { r->DrawSprite(white, { x, y, w, h }, full, c); }
    void Fill(const Rect& d, Color c) { r->DrawSprite(white, d, full, c); }
    void Quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col) { Vec2 p[4] = { a, b, c, d }; r->DrawQuad(white, p, full, col); }
    void Tri(Vec2 a, Vec2 b, Vec2 c, Color col) { Quad(a, b, c, c, col); }

    void Line(Vec2 a, Vec2 b, float thick, Color col) {
        float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) return;
        float nx = -dy / len * thick / 2, ny = dx / len * thick / 2;
        Quad({ a.x + nx, a.y + ny }, { b.x + nx, b.y + ny }, { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny }, col);
    }

    void RotRect(float cx, float cy, float w, float h, float angle, Color col) {
        float ca = std::cos(angle), sa = std::sin(angle);
        auto P = [&](float lx, float ly) { return Vec2{ cx + lx * ca - ly * sa, cy + lx * sa + ly * ca }; };
        Quad(P(-w / 2, -h / 2), P(w / 2, -h / 2), P(w / 2, h / 2), P(-w / 2, h / 2), col);
    }

    // Filled ellipse (fan of triangles).
    void Ellipse(float cx, float cy, float rx, float ry, Color col, int n = 20) {
        for (int i = 0; i < n; ++i) {
            float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            Tri({ cx, cy }, { cx + rx * std::cos(a0), cy + ry * std::sin(a0) }, { cx + rx * std::cos(a1), cy + ry * std::sin(a1) }, col);
        }
    }
    void Disc(float cx, float cy, float radius, Color col) { Ellipse(cx, cy, radius, radius, col, 18); }

    void Diamond(float cx, float cy, float w, float h, Color col) {
        Quad({ cx, cy - h / 2 }, { cx + w / 2, cy }, { cx, cy + h / 2 }, { cx - w / 2, cy }, col);
    }

    // Five-point star.
    void Star(float cx, float cy, float R, Color col, float rot = -kPi / 2) {
        Vec2 pts[10];
        for (int i = 0; i < 10; ++i) {
            float rr = (i & 1) ? R * 0.45f : R, a = rot + i * kPi / 5;
            pts[i] = { cx + rr * std::cos(a), cy + rr * std::sin(a) };
        }
        for (int i = 0; i < 10; ++i) Tri({ cx, cy }, pts[i], pts[(i + 1) % 10], col);
    }

    // Vertical gradient built from thin strips.
    void VGrad(float x, float y, float w, float h, Color top, Color bot) {
        int n = std::max(1, std::min(24, (int)(h / 2.f)));
        for (int i = 0; i < n; ++i)
            Fill(x, y + h * i / n, w, h / n + 0.6f, Lerp(top, bot, (i + 0.5f) / n));
    }

    // Raised bevelled panel (stone-button / bar frame look).
    void Bevel(float x, float y, float w, float h, Color base) {
        Fill(x - 3, y - 3, w + 6, h + 6, RGB(0.10f, 0.10f, 0.12f));
        VGrad(x, y, w, h, Mul(base, 1.18f), Mul(base, 0.72f));
        Fill(x, y, w, 2, Alpha(RGB(1, 1, 1), 0.55f));
        Fill(x, y, 2, h, Alpha(RGB(1, 1, 1), 0.30f));
        Fill(x, y + h - 2, w, 2, Alpha(RGB(0, 0, 0), 0.35f));
        Fill(x + w - 2, y, 2, h, Alpha(RGB(0, 0, 0), 0.25f));
    }

    // ------------------------------------------------ isometric solids
    // Brick / block courses on a vertical face. A,B = bottom edge (screen), h = face height in pixels.
    void FaceBricks(Vec2 A, Vec2 B, float h, Color mortar, float courseH = 5.f, float jointFrac = 0.22f) {
        int n = (int)(h / courseH);
        for (int k = 1; k < n; ++k)
            Line({ A.x, A.y - k * courseH }, { B.x, B.y - k * courseH }, 1.f, mortar);
        for (int j = 0; j < n; ++j) {
            float off = (j & 1) ? jointFrac / 2 : 0.f;
            for (float t = off + jointFrac; t < 0.98f; t += jointFrac) {
                Vec2 P = LerpV(A, B, t);
                Line({ P.x, P.y - j * courseH }, { P.x, P.y - (j + 1) * courseH }, 1.f, mortar);
            }
        }
    }

    // Box with footprint [gx0,gx1]x[gy0,gy1] (grid units) from height z0 to z1.
    // Visible faces: lower-left (gy = gy1) and lower-right (gx = gx1).
    void Prism(float gx0, float gy0, float gx1, float gy1, float z0, float z1,
               Color top, Color left, Color right, bool bricks = false, Color mortar = Color{}) {
        Vec2 lb0 = S(gx0, gy1, z0), lb1 = S(gx1, gy1, z0), lt0 = S(gx0, gy1, z1), lt1 = S(gx1, gy1, z1);
        Vec2 rb0 = S(gx1, gy1, z0), rb1 = S(gx1, gy0, z0), rt0 = S(gx1, gy1, z1), rt1 = S(gx1, gy0, z1);
        Quad(lb0, lb1, lt1, lt0, left);
        Quad(rb0, rb1, rt1, rt0, right);
        if (bricks) {
            FaceBricks(lb0, lb1, z1 - z0, mortar);
            FaceBricks(rb0, rb1, z1 - z0, mortar);
        }
        Quad(S(gx0, gy0, z1), S(gx1, gy0, z1), S(gx1, gy1, z1), S(gx0, gy1, z1), top);
        // soft edge highlight on the top rim
        Line(lt0, lt1, 1.2f, Alpha(RGB(1, 1, 1), 0.35f));
    }

    // Gable roof over footprint, ridge running along gx. Front slope faces +gy.
    void GableRoofX(float gx0, float gy0, float gx1, float gy1, float z, float height,
                    Color slope, Color slopeDark, Color gable, bool courses = true) {
        float mid = (gy0 + gy1) / 2;
        Vec2 a = S(gx0, gy1, z), b = S(gx1, gy1, z), c = S(gx1, mid, z + height), d = S(gx0, mid, z + height);
        // gable end (triangle at gx1)
        Tri(S(gx1, gy1, z), S(gx1, gy0, z), S(gx1, mid, z + height), gable);
        // back slope sliver then front slope
        Quad(a, b, c, d, slope);
        if (courses) {
            for (int k = 1; k < 5; ++k) {
                float t = k / 5.f;
                Line(LerpV(a, d, t), LerpV(b, c, t), 1.1f, Alpha(slopeDark, 0.55f));
            }
        }
        Line(d, c, 2.f, Mul(slope, 1.25f));
        Line(a, b, 1.5f, slopeDark);
    }

    // Vertical cylinder on the ground at screen (cx, cy) with screen-radius R (ellipse ratio 1:2).
    void Cylinder(float cx, float cy, float R, float z0, float z1, Color base, Color topCol,
                  bool bricks = true, Color mortar = Color{}, int seg = 16) {
        const float ry = R * 0.5f;
        auto P = [&](float th_, float z) { return Vec2{ cx + R * std::cos(th_), cy - z + ry * std::sin(th_) }; };
        for (int i = 0; i < seg; ++i) {
            float a0 = kPi * i / seg, a1 = kPi * (i + 1) / seg, am = (a0 + a1) / 2;
            float shade = std::min(1.f, 0.80f - 0.36f * std::cos(am));
            Quad(P(a0, z0), P(a1, z0), P(a1, z1), P(a0, z1), Mul(base, shade));
        }
        if (bricks) {
            float courseH = 6.f;
            int n = (int)((z1 - z0) / courseH);
            for (int k = 1; k < n; ++k)
                for (int i = 0; i < seg; ++i)
                    Line(P(kPi * i / seg, z0 + k * courseH), P(kPi * (i + 1) / seg, z0 + k * courseH), 1.f, mortar);
            for (int j = 0; j < n; ++j)
                for (int i = ((j & 1) ? 1 : 0); i <= seg; i += 2)
                    Line(P(kPi * i / seg, z0 + j * courseH), P(kPi * i / seg, z0 + (j + 1) * courseH), 1.f, mortar);
        }
        // top ellipse
        Ellipse(cx, cy - z1, R, ry, topCol, 24);
    }

    // Battlements around a cylinder rim.
    void Merlons(float cx, float cy, float R, float z, int count, Color col, float h = 8.f, float w = 0.0f) {
        const float ry = R * 0.5f;
        if (w <= 0) w = R * 0.28f;
        for (int pass = 0; pass < 2; ++pass)           // 0 = back half, 1 = front half
            for (int i = 0; i < count; ++i) {
                float a = 2 * kPi * i / count;
                bool front = std::sin(a) >= 0;
                if (front != (pass == 1)) continue;
                float x = cx + R * std::cos(a), y = cy - z + ry * std::sin(a);
                float shade = std::min(1.f, 0.80f - 0.36f * std::cos(a));
                Fill(x - w / 2, y - h, w, h, Mul(col, shade));
                Fill(x - w / 2, y - h, w, 1.5f, Alpha(RGB(1, 1, 1), 0.4f));
            }
    }

    // Cone roof on a cylinder.
    void Cone(float cx, float cy, float R, float z, float height, Color col, int seg = 16) {
        const float ry = R * 0.5f;
        Vec2 apex{ cx, cy - z - height };
        auto P = [&](float th_) { return Vec2{ cx + R * std::cos(th_), cy - z + ry * std::sin(th_) }; };
        for (int i = 0; i < seg; ++i) {
            float a0 = kPi * i / seg, a1 = kPi * (i + 1) / seg, am = (a0 + a1) / 2;
            float shade = std::min(1.f, 0.78f - 0.40f * std::cos(am));
            Tri(P(a0), P(a1), apex, Mul(col, shade));
            if ((i & 1) == 0) Line(P(am), apex, 1.f, Alpha(RGB(0, 0, 0), 0.18f));   // tile seams
        }
    }

    // Small standing figure (soldier / villager). `phase` drives the walk cycle.
    void Figure(float x, float y, float s, Color body, Color head, float phase, bool flip = false) {
        float sw = std::sin(phase) * 3.f * s;
        Fill(x - 3 * s + sw, y - 5 * s, 2.5f * s, 5 * s, RGB(0.2f, 0.15f, 0.1f));    // legs
        Fill(x + 0.5f * s - sw, y - 5 * s, 2.5f * s, 5 * s, RGB(0.2f, 0.15f, 0.1f));
        Fill(x - 3.5f * s, y - 12 * s, 7 * s, 8 * s, body);                            // torso
        Fill(x - 3.5f * s, y - 12 * s, 7 * s, 2 * s, Mul(body, 1.3f));
        Disc(x, y - 15 * s, 3.3f * s, head);                                           // head
        Fill(x - 3.3f * s, y - 18.5f * s, 6.6f * s, 2.5f * s, Mul(body, 0.7f));        // helmet
        float dir = flip ? -1.f : 1.f;
        Line({ x + 3.5f * s * dir, y - 10 * s }, { x + 7 * s * dir, y - 15 * s + sw * 0.3f }, 1.2f * s, RGB(0.8f, 0.8f, 0.85f)); // spear
    }

    // ------------------------------------------------ text
    float TextWidth(const std::string& s, float size) const {
        if (!fontOk) return s.empty() ? 0.f : (float)s.size() * 6.f * size * 0.104f - size * 0.104f;
        float scale = size / kFontPx, w = 0;
        for (char c : s) { int i = c - 32; w += (i >= 0 && i < 95) ? kFontAdv[i] * scale : 0.f; }
        return w;
    }
};

// ---------------------------------------------------------------- 5x7 fallback pixel font
struct Glyph { char c; unsigned char rows[7]; };
inline const Glyph* FindGlyph(char c) {
    static const Glyph kGlyphs[] = {
        {'A',{0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}}, {'B',{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}},
        {'C',{0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110}}, {'D',{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}},
        {'E',{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}}, {'F',{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}},
        {'G',{0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111}}, {'H',{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}},
        {'I',{0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}}, {'J',{0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100}},
        {'K',{0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}}, {'L',{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}},
        {'M',{0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}}, {'N',{0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}},
        {'O',{0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}}, {'P',{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}},
        {'Q',{0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}}, {'R',{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}},
        {'S',{0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}}, {'T',{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}},
        {'U',{0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}}, {'V',{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}},
        {'W',{0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001}}, {'X',{0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}},
        {'Y',{0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}}, {'Z',{0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}},
        {'0',{0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}}, {'1',{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}},
        {'2',{0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}}, {'3',{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110}},
        {'4',{0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}}, {'5',{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}},
        {'6',{0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}}, {'7',{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}},
        {'8',{0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}}, {'9',{0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}},
        {':',{0b00000,0b00100,0b00000,0b00000,0b00000,0b00100,0b00000}}, {'.',{0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100}},
        {',',{0b00000,0b00000,0b00000,0b00000,0b00110,0b00100,0b01000}}, {'/',{0b00001,0b00001,0b00010,0b00100,0b01000,0b10000,0b10000}},
        {'%',{0b11001,0b11010,0b00010,0b00100,0b01000,0b01011,0b10011}}, {'-',{0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000}},
        {'!',{0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100}},
    };
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (const Glyph& g : kGlyphs) if (g.c == c) return &g;
    return nullptr;
}

// Create a painter and bind the smooth font if assets/ui/font.png has been loaded.
inline Painter MakePainter(Services& s) {
    Painter p{ s.render, s.assets->White() };
    TextureHandle f = s.assets->LoadTexture("ui/font.png");
    p.fontOk = s.assets->TextureSize("ui/font.png").x > 0;
    p.font = f;
    return p;
}

// y = top of capital letters, x = left edge. size = font size in pixels.
inline void DrawText(Painter& p, float x, float y, float size, const std::string& s, Color col) {
    if (p.fontOk) {
        const float scale = size / kFontPx;
        const float atlasW = (float)(kFontCellW * kFontCols), atlasH = (float)(kFontCellH * 6);
        float cx = x;
        for (char c : s) {
            int i = c - 32;
            if (i < 0 || i >= 95) continue;
            Rect uv{ (i % kFontCols) * kFontCellW / atlasW, (i / kFontCols) * kFontCellH / atlasH,
                     kFontCellW / atlasW, kFontCellH / atlasH };
            p.r->DrawSprite(p.font, { cx - kFontPadX * scale, y - kFontCapTop * scale, kFontCellW * scale, kFontCellH * scale }, uv, col);
            cx += kFontAdv[i] * scale;
        }
    } else {
        const float sc = size * 0.104f;
        float cx = x;
        for (char ch : s) {
            if (const Glyph* g = FindGlyph(ch))
                for (int row = 0; row < 7; ++row)
                    for (int bit = 0; bit < 5; ++bit)
                        if (g->rows[row] & (1 << (4 - bit))) p.Fill(cx + bit * sc, y + row * sc, sc, sc, col);
            cx += 6.f * sc;
        }
    }
}

// Text with a dark outline + soft shadow (readable on any background).
inline void DrawTextOutlined(Painter& p, float x, float y, float size, const std::string& s, Color col,
                             Color outline = Color{ 0.05f, 0.04f, 0.04f, 1.f }) {
    float t = std::max(1.2f, size * 0.07f);
    static const float dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{.7f,.7f},{-.7f,.7f},{.7f,-.7f},{-.7f,-.7f} };
    DrawText(p, x + t * 0.6f, y + t * 1.4f, size, s, Color{ 0, 0, 0, 0.35f * col.a });
    for (auto& d : dirs) DrawText(p, x + d[0] * t, y + d[1] * t, size, s, outline);
    DrawText(p, x, y, size, s, col);
}

inline void DrawTextCentered(Painter& p, float cx, float y, float size, const std::string& s, Color col) {
    DrawTextOutlined(p, cx - p.TextWidth(s, size) / 2, y, size, s, col);
}

inline std::string FmtInt(int v) {
    std::string s = std::to_string(v), out;
    int n = (int)s.size();
    for (int i = 0; i < n; ++i) {
        out += s[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) out += ',';
    }
    return out;
}

} // namespace cs::gfx
