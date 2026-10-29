# Castle Siege (native) - Title Screen Architecture

## 1. Reference analysis (title screen)
Layer stack, back to front (virtual canvas 1280x720, anchored, letterbox-safe):

| Z | Layer | Notes |
|---|-------|-------|
| 0 | Sky / battlefield backdrop | Pre-rendered, slow horizontal parallax (0.02) |
| 1 | Distant castle wall + banners | Parallax 0.05, banner cloth sway shader |
| 2 | Smoke columns (burning projectiles) | GPU/CPU particle emitters, additive+alpha blend |
| 3 | Left siege engine (catapult) | Sprite, parallax 0.10, idle sway |
| 4 | Crowd of troops silhouette | Bottom-left, parallax 0.12 |
| 5 | Right foreground: armored king | Sprite, parallax 0.20, breathing idle |
| 6 | Crest logo + ribbon banner | Center-top, anchor TopCenter, 1-time intro scale-in |
| 7 | UI: PLAY / Settings / Version | Anchor BottomCenter, safe-area aware |
| 8 | Fade overlay | Scene transitions |

NOTE: all art here is placeholder. Ship your own licensed assets; do not ship the original game's logo or art.

## 2. Layering
```
Platform (Win32/UWP, iOS, Android)  -> window, input, lifecycle, safe area
Engine Core                         -> Application, JobSystem, SceneManager, AssetManager, Input
RHI                                 -> IRenderDevice: D3D11/12 | OpenGL ES 3 | Metal
Renderer                            -> SpriteBatch, ParticleSystem, Lighting (2D normal-mapped)
Audio                               -> IAudio -> FMOD Studio (banks: Master, Strings, Title, UI)
Game                                -> TitleScene, (later) VillageScene, BattleScene
Net (next turn)                     -> TCP client (custom framing), Azure-hosted C++/Node services
```

## 3. Threads
- Main: OS messages, input, game update, submit render commands
- Render: (phase 2) consumes command buffers; phase 1 renders on main for simplicity behind the same API
- Job workers (N-1 cores): asset decode, pathfinding, audio bank load polling
- Audio: owned by FMOD

## 4. Core classes
- `Application` - owns subsystems, fixed-step loop (60 Hz update, uncapped render with interpolation)
- `IRenderDevice` - Init, Resize, CreateTexture, BeginFrame, DrawSprite, EndFrame
- `AssetManager` - async texture loads on JobSystem, placeholder fallback
- `IAudio` / `FmodAudio` - non-blocking bank loading, event instances, per-frame update
- `IScene` - OnEnter, Update(dt), Render, OnInput, OnExit
- `SceneManager` - stack + transitions
- `TitleScene` - parallax layers, smoke emitters, Play button

## 5. Boot sequence
1. Platform creates window (HWND / CoreWindow / CAMetalLayer / ANativeWindow)
2. `Application::Init`: JobSystem -> RenderDevice -> Audio -> AssetManager -> SceneManager
3. Audio: create FMOD Studio system, load Master + Strings banks (blocking, tiny), Title bank NONBLOCKING (streams .fsb)
4. TitleScene::OnEnter: queue async texture loads; show placeholders until ready
5. When Title bank reports LOADED, start `event:/Music/Title`
6. Loop: pump messages -> fixed updates -> render -> audio update
7. Shutdown in reverse order

## 6. Backend hooks (later)
Title scene's PLAY triggers `NetClient::Connect` (TCP, length-prefixed binary frames, TLS), auth, then matchmaking/alliance services. Combat results are server-authoritative; the client only predicts.
