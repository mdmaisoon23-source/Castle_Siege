// Win32 entry point (desktop dev build). UWP/iOS/Android shells call the same cs::Application API.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "../engine/Engine.h"

static cs::Application g_app;
static bool g_ready = false;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:
        if (g_ready && w != SIZE_MINIMIZED) g_app.OnResize(LOWORD(l), HIWORD(l));
        return 0;
    case WM_MOUSEMOVE:
        if (g_ready) g_app.OnMouse((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), (w & MK_LBUTTON) != 0);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(h);
        if (g_ready) g_app.OnMouse((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), true);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_ready) g_app.OnMouse((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), false);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WNDCLASSEXA wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "CastleSiegeWnd";
    RegisterClassExA(&wc);

    RECT rc{ 0, 0, 1280, 720 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Castle Siege (dev)", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);

    RECT client; GetClientRect(hwnd, &client);
    if (!g_app.Init(hwnd, client.right, client.bottom, "assets")) {
        MessageBoxA(hwnd, "Engine init failed", "Castle Siege", MB_ICONERROR);
        return 1;
    }
    g_ready = true;

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            g_app.Frame();   // vsync (Present(1)) paces the loop
        }
    }
    g_ready = false;
    g_app.Shutdown();
    return 0;
}
