// OpenGL 3.2 core backend (macOS-first; uses SDL2 for window/context). Same sprite pipeline as the D3D11 backend.
#define GL_SILENCE_DEPRECATION
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#error "On Linux/Windows add a GL loader (e.g. glad) and include it here."
#endif
#include <algorithm>
#include <cstdio>
#include <vector>
#include "Engine.h"

namespace cs {
namespace {

struct Vertex { float x, y, u, v, r, g, b, a; };

const char* kVS = R"(#version 150
in vec2 pos; in vec2 uv; in vec4 col;
uniform vec2 canvas;
out vec2 vUV; out vec4 vCol;
void main() {
    gl_Position = vec4(pos.x / canvas.x * 2.0 - 1.0, 1.0 - pos.y / canvas.y * 2.0, 0.0, 1.0);
    vUV = uv; vCol = col;
})";

const char* kFS = R"(#version 150
in vec2 vUV; in vec4 vCol;
uniform sampler2D tex;
out vec4 outColor;
void main() { outColor = texture(tex, vUV) * vCol; })";

GLuint Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::fprintf(stderr, "Shader error: %s\n", log); }
    return s;
}

class GLDevice final : public IRenderDevice {
public:
    bool Init(void* wnd, uint32_t w, uint32_t h) override {
        window_ = (SDL_Window*)wnd;
        bbW_ = (float)w; bbH_ = (float)h;

        GLuint vs = Compile(GL_VERTEX_SHADER, kVS), fs = Compile(GL_FRAGMENT_SHADER, kFS);
        prog_ = glCreateProgram();
        glAttachShader(prog_, vs); glAttachShader(prog_, fs);
        glBindAttribLocation(prog_, 0, "pos");
        glBindAttribLocation(prog_, 1, "uv");
        glBindAttribLocation(prog_, 2, "col");
        glLinkProgram(prog_);
        GLint ok = 0; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
        if (!ok) return false;
        locCanvas_ = glGetUniformLocation(prog_, "canvas");
        locTex_ = glGetUniformLocation(prog_, "tex");

        glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)8);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)16);

        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glEnable(GL_MULTISAMPLE);
        return true;
    }

    void Resize(uint32_t w, uint32_t h) override { bbW_ = (float)w; bbH_ = (float)h; }

    TextureHandle CreateTextureRGBA8(uint32_t w, uint32_t h, const void* px) override {
        GLuint t; glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        textures_.push_back(t);
        return (TextureHandle)textures_.size();
    }

    void BeginFrame(const Color& c) override {
        glViewport(0, 0, (GLsizei)bbW_, (GLsizei)bbH_);
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
        float scale = std::min(bbW_ / kCW, bbH_ / kCH);
        glViewport((GLint)((bbW_ - kCW * scale) * 0.5f), (GLint)((bbH_ - kCH * scale) * 0.5f),
                   (GLsizei)(kCW * scale), (GLsizei)(kCH * scale));
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
        if (verts_.size() > 6 * 4096) Flush();
    }

    void DrawQuad(TextureHandle tex, const Vec2 p[4], const Rect& uv, const Color& t) override {
        if (tex != currentTex_) { Flush(); currentTex_ = tex; }
        float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
        Vertex a{p[0].x,p[0].y,u0,v0,t.r,t.g,t.b,t.a}, b{p[1].x,p[1].y,u1,v0,t.r,t.g,t.b,t.a};
        Vertex c{p[2].x,p[2].y,u1,v1,t.r,t.g,t.b,t.a}, d{p[3].x,p[3].y,u0,v1,t.r,t.g,t.b,t.a};
        Vertex q[6] = { a, b, c, a, c, d };
        verts_.insert(verts_.end(), q, q + 6);
        if (verts_.size() > 6 * 4096) Flush();
    }

    void EndFrame() override { Flush(); SDL_GL_SwapWindow(window_); }
    Vec2 CanvasSize() const override { return { kCW, kCH }; }
    void Shutdown() override {
        for (GLuint t : textures_) glDeleteTextures(1, &t);
        textures_.clear();
    }

private:
    static constexpr float kCW = 1280.f, kCH = 720.f;

    void Flush() {
        if (verts_.empty()) return;
        glUseProgram(prog_);
        glUniform2f(locCanvas_, kCW, kCH);
        glUniform1i(locTex_, 0);
        glActiveTexture(GL_TEXTURE0);
        if (currentTex_ && currentTex_ <= textures_.size()) glBindTexture(GL_TEXTURE_2D, textures_[currentTex_ - 1]);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(Vertex), verts_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());
        verts_.clear();
    }

    SDL_Window* window_ = nullptr;
    float bbW_ = 1, bbH_ = 1;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
    GLint locCanvas_ = -1, locTex_ = -1;
    std::vector<GLuint> textures_;
    std::vector<Vertex> verts_;
    TextureHandle currentTex_ = 0;
};

} // namespace

std::unique_ptr<IRenderDevice> CreateRenderDeviceGL() { return std::make_unique<GLDevice>(); }

} // namespace cs
