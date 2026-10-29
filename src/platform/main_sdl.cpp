// SDL2 entry point (macOS / Linux dev build). Same cs::Application API as the Win32 entry.
#include <SDL.h>
#include <cstdio>
#include "../engine/Engine.h"

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // required on macOS
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Smooth (anti-aliased) edges: try 4x MSAA first, fall back to none if the GPU refuses.
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    for (int samples : { 4, 0 }) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
        win = SDL_CreateWindow("Castle Siege (dev)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!win) continue;
        ctx = SDL_GL_CreateContext(win);
        if (ctx) break;
        SDL_DestroyWindow(win);
        win = nullptr;
    }
    if (!win) { std::fprintf(stderr, "Window: %s\n", SDL_GetError()); return 1; }
    if (!ctx) { std::fprintf(stderr, "GL context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
    cs::Application app;
    if (!app.Init(win, dw, dh, "assets")) { std::fprintf(stderr, "Engine init failed\n"); return 1; }

    // SDL reports mouse in window points; the engine wants drawable pixels (differs on Retina).
    auto toPixels = [&](int x, int y, float& px, float& py) {
        int ww, wh, w2, h2; SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &w2, &h2);
        px = x * (float)w2 / ww; py = y * (float)h2 / wh;
    };

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            float px, py;
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                    app.OnResize(dw, dh);
                }
                break;
            case SDL_MOUSEMOTION:
                toPixels(e.motion.x, e.motion.y, px, py);
                app.OnMouse(px, py, (e.motion.state & SDL_BUTTON_LMASK) != 0);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) { toPixels(e.button.x, e.button.y, px, py); app.OnMouse(px, py, true); }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) { toPixels(e.button.x, e.button.y, px, py); app.OnMouse(px, py, false); }
                break;
            }
        }
        app.Frame();  // swaps buffers (vsync-paced)
    }

    app.Shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
