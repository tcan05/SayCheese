# SayCheese Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build **SayCheese**, a zero-dependency C++20 Windows 10/11 screenshot capture and annotation tray application (~1 MB portable x64 `.exe`, static `/MT`) with anti-freeze DXGI capture, Direct2D 1.1 WYSIWYG rendering/export, automated leak stress verification, and GitHub Actions release automation.

**Architecture:** Approach A (Fully Ephemeral Per-Capture Pipeline). While idle in the System Tray, only a hidden Win32 window, tray icon, registered hotkey, and per-session mutex remain alive (0 MB GPU VRAM, zero COM/DirectX objects). Each capture spins up scoped per-adapter DXGI Desktop Duplication (bounded `<= 100 ms` timeout, `BitBlt` fallback), releases duplication before opening a scoped topmost overlay with Direct2D 1.1 (`ID2D1DeviceContext` locked to 96 DPI with per-monitor chrome scaling) and a single `SceneRenderer` shared by live preview and offscreen `ID2D1Bitmap1 -> IWICBitmap` export, then tears down all resources immediately on `ESC` or completion.

**Tech Stack:** C++20, MSVC (`/W4 /permissive-`, `/MT` Release / `/MTd` Debug), CMake 3.20+, pure Win32 API, D3D11, DXGI (`IDXGIOutputDuplication`), Direct2D 1.1 (`ID2D1DeviceContext`, `CLSID_D2D1GaussianBlur`), DirectWrite, WIC, `Microsoft::WRL::ComPtr`, CTest.

**Spec:** `docs/superpowers/specs/2026-11-19-saycheese-design.md`

---

## Global Constraints

- **Language & Warnings:** Modern C++20, MSVC `/W4 /permissive-`, `UNICODE`/`_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`. Zero raw `new`/`delete` anywhere in product or test code.
- **RAII Everywhere:** `Microsoft::WRL::ComPtr<T>` for all COM/DXGI/D3D11/D2D1/DirectWrite/WIC interfaces; custom RAII wrappers for all Win32/GDI/USER/Global handles.
- **Zero External Dependencies:** Link strictly to standard Windows SDK libraries (`d3d11`, `dxgi`, `d2d1`, `dwrite`, `windowscodecs`, `dwmapi`, `shell32`, `comdlg32`, `user32`, `gdi32`, `ole32`, `dxguid`, `shcore`, `psapi`).
- **Static CRT:** `CMAKE_MSVC_RUNTIME_LIBRARY` set to `"MultiThreaded$<$<CONFIG:Debug>:Debug>"` so Release builds require no VC++ Redistributable.
- **Anti-Freeze:** Never install `WH_KEYBOARD_LL` or `WH_MOUSE_LL`. `RegisterHotKey(VK_SNAPSHOT)` with fallback `Ctrl+Shift+S`. Bounded `<= 100 ms` DXGI `AcquireNextFrame` (max 5 attempts) with automatic `BitBlt` fallback. Instant `ESC` teardown.
- **No Stubs:** Zero `TODO` placeholders in DXGI capture, Blur/Pixelate censor, DirectWrite text, WYSIWYG export, or leak verification.
- **Build Commands (Local VS 2026 & CI MSVC):**
  - Local configure: `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64`
  - Build: `cmake --build build --config Debug` / `cmake --build build --config Release`
  - Test: `ctest --test-dir build -C Debug --output-on-failure` / `ctest --test-dir build -C Release --output-on-failure`

## Review Focus

1. **Negative & mixed-DPI multi-monitor coordinates** (e.g., secondary monitor at `(-1920, -200)` at 150% DPI beside `(0, 0)` at 100% DPI): canvas coordinate conversion, selection clamping, and toolbar docking must clamp inside the active monitor's canvas bounds without wrapping to `(0, 0)` or clipping off-screen. *(Pinned in Task 2.)*
2. **Degenerate / reversed drags and full-monitor selections** (bottom-right -> top-left drag, 0x0 click-release, selection filling 100% of a monitor): rectangles must normalize cleanly, zero-area drags must not push commands or allocate 0x0 targets, and full-monitor toolbars must tuck inside monitor edges. *(Pinned in Tasks 2 & 3.)*
3. **HDR scRGB FP16 / 10-bit surfaces & rotated portrait monitors** (`R16G16B16A16_FLOAT` with NaN/negative/HDR > 1.0 values, `R10G10B10A2_UNORM`, `DXGI_MODE_ROTATION_ROTATE90/180/270`): converters must clamp cleanly to BGRA8 with alpha `0xFF` and rotate into desktop coordinates without out-of-bounds writes. *(Pinned in Task 4.)*
4. **Inline DirectWrite Text edge cases** (backspace on empty string, delete at end, Enter newline, committing empty text, `ESC` while editing vs `ESC` when idle): cursor index must never underflow, empty text must never push an undo command, and `ESC` commits active text first while closing overlay when idle. *(Pinned in Task 3.)*
5. **Odd-width crop export (`137x91`, `1x1`) & opaque alpha:** `CF_DIBV5` + `CF_DIB` bottom-up row packing and WIC PNG/JPEG export must preserve exact dimensions, 4-byte stride alignment, and `0xFF` alpha without corrupting rows. *(Pinned in Task 6.)*

---

## File Responsibility Map

| Module | Files | Responsibility |
|---|---|---|
| Build & Resources | `CMakeLists.txt`, `.gitignore`, `resources/app.manifest`, `resources/resource.h`, `resources/app.rc`, `resources/saycheese.ico` | CMake targets (`saycheese_core`, `SayCheese`, tests), PerMonitorV2 manifest, version info, multi-size icon |
| Platform | `src/platform/{win32_raii.hpp, single_instance.hpp, dpi_monitor.hpp/.cpp, com_debug.hpp/.cpp}` | Win32 RAII guards, mutex, monitor/DPI enumeration, process resource snapshots & Debug DXGI live-object report |
| Pure Tools & Layout | `src/tools/{geometry.hpp/.cpp, annotation.hpp, command_stack.hpp/.cpp, tool_controller.hpp/.cpp}`, `src/ui/toolbar_layout.hpp/.cpp` | Selection math, Bezier smoothing, annotations, Undo/Redo stack, tool & text state machine, toolbar docking/hit-testing |
| Capture | `src/capture/{captured_frame.hpp, pixel_convert.hpp/.cpp, dxgi_capture.hpp/.cpp, gdi_capture.hpp/.cpp, screen_capture.hpp/.cpp}` | Pixel format/rotation converters, per-adapter DXGI Duplication, BitBlt fallback, virtual desktop compositing |
| Render | `src/render/{d2d_context.hpp/.cpp, scene_renderer.hpp/.cpp, ui_renderer.hpp/.cpp}` | Scoped D3D11/D2D1/SwapChain/DWrite/WIC context, shared WYSIWYG scene renderer, Direct2D overlay chrome |
| Export | `src/export/{wic_exporter.hpp/.cpp, clipboard_export.hpp/.cpp}` | Offscreen `ID2D1Bitmap1 -> IWICBitmap` render, PNG/JPEG encoding, native save dialog, `CF_DIBV5`/`CF_DIB` clipboard |
| App & Overlay UI | `src/ui/overlay_window.hpp/.cpp`, `src/app/{hotkey_manager.hpp/.cpp, tray_icon.hpp/.cpp, app_controller.hpp/.cpp, leak_self_test.hpp/.cpp}`, `src/main.cpp` | Topmost overlay HWND, hotkey registration/fallback, tray icon/menu, app coordinator, leak stress runner, `wWinMain` |
| Tests & CI | `tests/{test_runner.hpp, test_platform.cpp, test_geometry.cpp, test_command_stack.cpp, test_tool_controller.cpp, test_capture_conversion.cpp, test_export.cpp, test_leak_stress.cpp}`, `.github/workflows/release.yml`, `README.md`, `LICENSE` | Unit tests, WYSIWYG/export integration tests, 2+20 cycle leak verifier, GitHub Actions CI/Release, docs |

---

### Task 1: Repository Init, CMake Build System, Resources & Platform RAII Layer

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`
- Create: `resources/app.manifest`, `resources/resource.h`, `resources/app.rc`, `resources/saycheese.ico`
- Create: `src/platform/win32_raii.hpp`, `src/platform/single_instance.hpp`, `src/platform/dpi_monitor.hpp`, `src/platform/dpi_monitor.cpp`, `src/platform/com_debug.hpp`, `src/platform/com_debug.cpp`
- Test: `tests/test_runner.hpp`, `tests/test_platform.cpp`

**Interfaces:**
- Produces (`namespace saycheese::platform`):
  - `UniqueHandle`, `UniqueHwnd`, `UniqueHdc`, `UniqueWindowDc`, `UniqueHbitmap`, `UniqueHicon`, `UniqueHmenu`, `UniqueGlobalMem`, `SelectObjectGuard`, `ComInitScope` (all move-only, `noexcept` destructors).
  - `struct MonitorInfo { RECT virtualRect; RECT canvasRect; UINT dpiX; UINT dpiY; bool isPrimary; float Scale() const; };`
  - `struct VirtualDesktopMetrics { int originX; int originY; int width; int height; std::vector<MonitorInfo> monitors; };`
  - `VirtualDesktopMetrics QueryVirtualDesktopMetrics();`
  - `POINT VirtualToCanvas(POINT pt, int originX, int originY);`
  - `const MonitorInfo* FindMonitorContainingCanvasPoint(const std::vector<MonitorInfo>& monitors, float x, float y);`
  - `class SingleInstanceGuard { public: explicit SingleInstanceGuard(const wchar_t* mutexName); bool IsPrimaryInstance() const noexcept; };`
  - `struct ProcessResourceSnapshot { DWORD gdiObjects; DWORD userObjects; DWORD handleCount; SIZE_T privateUsageBytes; };`
  - `ProcessResourceSnapshot CaptureProcessResourceSnapshot();`
  - `struct DxgiLiveObjectReport { bool available; size_t liveApplicationObjects; std::string summary; };`
  - `DxgiLiveObjectReport ReportDebugDxgiLiveObjects();`

- [ ] **Step 1: Initialize git repo, `.gitignore`, resources (`app.manifest`, `resource.h`, `app.rc`, multi-resolution `saycheese.ico`), and commit the approved spec & plan**
  - Configure local git identity if missing (`SayCheese Dev` / `dev@saycheese.local`).
  - `app.manifest`: declare `<dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>`, `<dpiAware>true/pm</dpiAware>`, and Common Controls v6 dependency.
  - Generate crisp vector-derived `resources/saycheese.ico` containing PNG-encoded RGBA frames at `16, 24, 32, 48, 64, 256` px (clean viewfinder/shutter aperture design) via a one-off script in scratch space (not committed), then verify the `.ico` header/frames exist.
  - `app.rc`: embed `IDI_APP_ICON` (`101`), `RT_MANIFEST` (`1`) pointing to `app.manifest`, and `VS_VERSION_INFO` (`1.0.0.0`, `"SayCheese"`).

- [ ] **Step 2: Create `tests/test_runner.hpp` and failing `tests/test_platform.cpp` + initial `CMakeLists.txt`**
  - `tests/test_runner.hpp`: minimal zero-dependency macro suite (`SC_TEST`, `SC_EXPECT_TRUE`, `SC_EXPECT_EQ`, `SC_EXPECT_NEAR`) returning `0` on pass, `1` on failure.
  - `tests/test_platform.cpp` assertions:
    - `VirtualToCanvas({-1920, -200}, -1920, -200)` equals `{0, 0}` and `VirtualToCanvas({0, 0}, -1920, -200)` equals `{1920, 200}`.
    - `FindMonitorContainingCanvasPoint` returns monitor 0 (150% DPI, `Scale() == 1.5f`) for `(100, 100)` and monitor 1 (100% DPI, `Scale() == 1.0f`) for `(2000, 300)`, and falls back to primary/nearest when outside.
    - `SingleInstanceGuard` on a unique test mutex name: first guard reports `IsPrimaryInstance() == true`, concurrent second guard reports `false`, and after both release a fresh guard reports `true`.
    - `UniqueHandle` move semantics leave moved-from null and close kernel handle cleanly (`CaptureProcessResourceSnapshot()` handle delta `== 0` over 50 create/destroy iterations).
  - `CMakeLists.txt`: CMake 3.20+, C++20, `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"`, `/W4 /permissive-`, `enable_testing()`, `saycheese_core` static lib, `test_platform` executable + `add_test`.

- [ ] **Step 3: Run CMake configure/build to verify `test_platform` fails before platform implementation**
  - Expected: Compile error because `src/platform/*.hpp` / `.cpp` do not exist yet.

- [ ] **Step 4: Implement `src/platform/{win32_raii.hpp, single_instance.hpp, dpi_monitor.hpp/.cpp, com_debug.hpp/.cpp}`**
  - `dpi_monitor.cpp`: query `GetSystemMetrics(SM_XVIRTUALSCREEN..SM_CYVIRTUALSCREEN)`, enumerate via `EnumDisplayMonitors`, fetch per-monitor DPI via `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` (fallback 96).
  - `com_debug.cpp`: `GetGuiResources(GR_GDIOBJECTS / GR_USEROBJECTS)`, `GetProcessHandleCount`, `GetProcessMemoryInfo` (`PROCESS_MEMORY_COUNTERS_EX`); in `_DEBUG`, dynamically load `dxgidebug.dll` (`DXGIGetDebugInterface1`), query `IDXGIInfoQueue` message count before/after `IDXGIDebug1::ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)` to count live application objects accurately.

- [ ] **Step 5: Build & run `test_platform` in Debug and Release**
  - Run: `cmake --build build --config Debug --target test_platform && ctest --test-dir build -C Debug -R test_platform --output-on-failure`
  - Expected: `100% tests passed`, 0 warnings.

- [ ] **Step 6: Commit Task 1**
  - `git add` Task 1 files + docs, commit `"feat: add CMake build, Win32 resources, and platform RAII layer"`.

---

### Task 2: Geometry, Selection Box & Monitor-Clamped Toolbar Layout

**Files:**
- Create: `src/tools/geometry.hpp`, `src/tools/geometry.cpp`
- Create: `src/ui/toolbar_layout.hpp`, `src/ui/toolbar_layout.cpp`
- Modify: `CMakeLists.txt` (add sources to `saycheese_core`, add `test_geometry`)
- Test: `tests/test_geometry.cpp`

**Interfaces:**
- Consumes (`Task 1`): `saycheese::platform::MonitorInfo`, `FindMonitorContainingCanvasPoint`.
- Produces (`namespace saycheese::tools`):
  - `struct PointF { float x = 0.0f; float y = 0.0f; };`
  - `struct RectF { float left = 0.0f; float top = 0.0f; float right = 0.0f; float bottom = 0.0f; float Width() const; float Height() const; bool IsEmpty() const; bool Contains(PointF p) const; RectF Normalized() const; };`
  - `enum class ResizeHandle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, MoveBody };`
  - `struct SelectionBox { RectF rect; bool active = false; };`
  - `RectF ClampRectToBounds(RectF r, RectF bounds);`
  - `ResizeHandle HitTestSelection(const SelectionBox& sel, PointF pt, float handleRadiusPx);`
  - `RectF ApplySelectionDrag(RectF startRect, ResizeHandle handle, PointF delta, RectF canvasBounds, float minSize = 4.0f);`
  - `PointF SnapLine45Deg(PointF origin, PointF target);`
  - `RectF ConstrainSquare(PointF origin, PointF target);`
  - `struct ArrowheadTriangle { PointF tip; PointF left; PointF right; PointF shaftEnd; };`
  - `ArrowheadTriangle ComputeArrowhead(PointF start, PointF end, float strokeWidth);`
  - `struct QuadSegment { PointF control; PointF end; };`
  - `std::vector<QuadSegment> SmoothPolylineQuadratic(const std::vector<PointF>& points);`
- Produces (`namespace saycheese::ui`):
  - `enum class ToolbarButtonId { None, ToolPen, ToolLine, ToolArrow, ToolRect, ToolHighlighter, ToolText, ToolCensor, Undo, Redo, ColorPicker, StrokeWidth, Copy, Save, Cancel, ColorSwatch0..ColorSwatch5, ColorCustom, CensorMosaic, CensorBlur, PresetThin, PresetMedium, PresetThick };`
  - `struct ToolbarButton { ToolbarButtonId id; tools::RectF rect; };`
  - `struct ToolbarLayoutState { bool colorPopupOpen = false; bool strokePopupOpen = false; bool censorActive = false; };`
  - `struct ToolbarLayoutResult { float uiScale; tools::RectF toolBarRect; tools::RectF actionBarRect; tools::RectF badgeRect; tools::RectF colorPopupRect; tools::RectF strokePopupRect; tools::RectF censorPillRect; std::vector<ToolbarButton> buttons; };`
  - `ToolbarLayoutResult ComputeToolbarLayout(tools::RectF selection, const std::vector<platform::MonitorInfo>& monitors, tools::RectF canvasBounds, const ToolbarLayoutState& state);`
  - `ToolbarButtonId HitTestToolbar(const ToolbarLayoutResult& layout, tools::PointF pt);`

- [ ] **Step 1: Write failing `tests/test_geometry.cpp`**
  - Cover all geometry & Review Focus cases:
    - Reversed drag `RectF{300, 200, 100, 50}.Normalized()` == `{100, 50, 300, 200}`; empty/zero rect `IsEmpty() == true`.
    - `HitTestSelection` hits all 8 resize handles + `MoveBody` + `None` outside.
    - `ApplySelectionDrag` for `MoveBody` clamps strictly inside canvas bounds without shrinking width/height; edge/corner resize enforces `minSize` and canvas bounds.
    - `SnapLine45Deg({0, 0}, {100, 20})` snaps to `y == 0`; `{100, 90}` snaps to `45°` (`x == y`); `{-5, -100}` snaps to `x == 0`.
    - `ConstrainSquare({100, 100}, {60, 180})` produces square `80x80` anchored at `(100, 100)` extending left/down.
    - `ComputeArrowhead` scales arrowhead length (`>= max(12.0f, strokeWidth * 4.0f)`) and places `shaftEnd` cleanly behind `tip` along the shaft vector.
    - `SmoothPolylineQuadratic` produces smooth midpoint control points for 4+ input points and handles 1–2 point edge cases without crashing.
    - **Review Focus #1 & #2 (Negative multi-monitor + mixed DPI + full-monitor selection):** Layout on a 2-monitor canvas (left monitor `canvasRect [0..1920, 0..1080]` @ 150% DPI representing virtual `[-1920..0]`, right monitor `[1920..3840, 0..1080]` @ 100% DPI):
      - Selection near right edge of monitor 0 flips vertical tool bar left of selection and keeps `toolBarRect`, `actionBarRect`, `badgeRect`, `colorPopupRect`, and `censorPillRect` completely inside monitor 0's `canvasRect` with `uiScale == 1.5f`.
      - Selection covering 100% of monitor 1 (`[1920..3840, 0..1080]`) tucks both bars and badge strictly inside `[1920..3840, 0..1080]` with `uiScale == 1.0f` and zero overlap off-screen.
      - `HitTestToolbar` hits each button center and returns `None` outside.

- [ ] **Step 2: Run build to verify `test_geometry` fails before implementation**

- [ ] **Step 3: Implement `src/tools/geometry.hpp/.cpp` and `src/ui/toolbar_layout.hpp/.cpp`**
  - Docking priority: Vertical Tool Bar right of selection -> flip left -> tuck inside right; Horizontal Action Bar below bottom-right -> flip above top-right -> tuck inside bottom-right; Dimension badge above top-left -> tuck inside top-left; popups clamped strictly inside the active monitor's `canvasRect`.

- [ ] **Step 4: Build & run `test_geometry`**
  - Run: `cmake --build build --config Debug --target test_geometry && ctest --test-dir build -C Debug -R test_geometry --output-on-failure`
  - Expected: PASS, 0 warnings.

- [ ] **Step 5: Commit Task 2**
  - Commit `"feat: implement selection geometry, curve smoothing, and monitor-clamped toolbar layout"`.

---

### Task 3: Annotation Data Model, Command Pattern Undo/Redo & Tool State Machine

**Files:**
- Create: `src/tools/annotation.hpp`
- Create: `src/tools/command_stack.hpp`, `src/tools/command_stack.cpp`
- Create: `src/tools/tool_controller.hpp`, `src/tools/tool_controller.cpp`
- Modify: `CMakeLists.txt` (add sources + `test_command_stack` + `test_tool_controller`)
- Test: `tests/test_command_stack.cpp`, `tests/test_tool_controller.cpp`

**Interfaces:**
- Consumes (`Task 2`): `PointF`, `RectF`, `SnapLine45Deg`, `ConstrainSquare`.
- Produces (`namespace saycheese::tools`):
  - `enum class ToolType { Select, Pen, Line, Arrow, Rectangle, Highlighter, Text, Censor };`
  - `enum class CensorMode { Mosaic, GaussianBlur };`
  - `struct ColorRGBA { uint8_t r, g, b, a; };`
  - `struct Annotation { uint64_t id; ToolType type; ColorRGBA color; float strokeWidth; std::vector<PointF> points; PointF start; PointF end; RectF rect; std::wstring text; float fontSize; CensorMode censorMode; float censorIntensity; };`
  - `class ICommand { public: virtual ~ICommand() = default; virtual void Execute() = 0; virtual void Undo() = 0; };`
  - `class CommandStack { public: void PushAndExecute(std::unique_ptr<ICommand> cmd); bool Undo(); bool Redo(); bool CanUndo() const noexcept; bool CanRedo() const noexcept; void Clear() noexcept; size_t UndoCount() const noexcept; size_t RedoCount() const noexcept; };`
  - `struct TextEditorState { bool active = false; PointF anchor; std::wstring text; size_t caretIndex = 0; ColorRGBA color; float fontSize = 28.0f; };`
  - `enum class EscapeAction { CommittedText, CloseOverlay };`
  - `class ToolController` managing:
    - `const std::vector<Annotation>& Annotations() const;`
    - `const Annotation* ActivePreview() const;`
    - `const TextEditorState& TextEditor() const;`
    - `ToolType ActiveTool() const; void SetActiveTool(ToolType t);` (auto-commits active non-empty text before switching)
    - `CensorMode ActiveCensorMode() const; void SetCensorMode(CensorMode m);`
    - `ColorRGBA ActiveColor() const; void SetActiveColor(ColorRGBA c);` (updates active text editor live)
    - `float ActiveSize() const; void AdjustSizeByWheel(int wheelSteps); void ApplySizePreset(int presetIndex);` (presetIndex `0=Thin, 1=Medium, 2=Thick`: Stroke `2/5/10` clamped `1..24`, Text `18/28/42` clamped `14..72`, Mosaic `8/14/22` clamped `6..32`, Blur `6/12/18` clamped `4..24`)
    - `void OnPointerDown(PointF pt, bool shiftHeld); void OnPointerMove(PointF pt, bool shiftHeld); void OnPointerUp(PointF pt, bool shiftHeld);`
    - `void OnTextChar(wchar_t ch); void OnTextBackspace(); void OnTextDelete(); void OnTextNewline(); void OnTextCaretLeft(); void OnTextCaretRight(); void CommitActiveText();`
    - `EscapeAction HandleEscape();`
    - `bool Undo(); bool Redo(); bool CanUndo() const; bool CanRedo() const;`

- [ ] **Step 1: Write failing `tests/test_command_stack.cpp` and `tests/test_tool_controller.cpp`**
  - `test_command_stack.cpp`: push 3 commands -> undo 2 -> verify state -> redo 1 -> push new command -> verify redo branch cleared (`CanRedo() == false`) -> undo on empty returns `false` -> `Clear()` resets counts to 0.
  - `test_tool_controller.cpp`:
    - Pen & Highlighter drag appends annotation with unique ID; Highlighter uses `alpha == 89` (`~0.35f`) and wide width (`max(12.0f, strokeWidth * 4.0f)`).
    - Line & Arrow with `shiftHeld == true` snaps to 45°; Rectangle with `shiftHeld == true` produces square.
    - **Review Focus #2 (Zero-area guard):** Click-release without dragging (`start == end`) for Line, Arrow, Rectangle, Censor pushes 0 commands.
    - **Review Focus #4 (Inline Text edge cases):** Click opens text editor; backspace on empty & delete at end do not crash or underflow; typing `"Hi"`, caret left, typing `'!'` produces `"H!i"`; `Enter` inserts `'\n'`; changing color updates `TextEditor().color` live; `HandleEscape()` while editing non-empty text commits 1 `Text` annotation and returns `EscapeAction::CommittedText`; next `HandleEscape()` returns `EscapeAction::CloseOverlay`; committing empty/whitespace-only text pushes 0 commands.
    - Censor toggle switches between `Mosaic` and `GaussianBlur`; wheel scrolling clamps cleanly per tool category; presets set exact spec values (`2/5/10`, `18/28/42`, `8/14/22`, `6/12/18`).
    - Undo/Redo removes and restores committed annotations cleanly.

- [ ] **Step 2: Run build to verify tests fail before implementation**

- [ ] **Step 3: Implement `annotation.hpp`, `command_stack.hpp/.cpp`, `tool_controller.hpp/.cpp`**
  - `AddAnnotationCommand` appends/pops from `std::vector<Annotation>` owned by `ToolController`.

- [ ] **Step 4: Build & run `test_command_stack` and `test_tool_controller`**
  - Run: `cmake --build build --config Debug --target test_command_stack test_tool_controller && ctest --test-dir build -C Debug -R "test_command_stack|test_tool_controller" --output-on-failure`
  - Expected: PASS, 0 warnings.

- [ ] **Step 5: Commit Task 3**
  - Commit `"feat: implement annotation model, command stack undo/redo, and tool controller"`.

---

### Task 4: Non-Blocking Multi-Monitor Capture Pipeline (DXGI + HDR/Rotation + `BitBlt` Fallback)

**Files:**
- Create: `src/capture/captured_frame.hpp`
- Create: `src/capture/pixel_convert.hpp`, `src/capture/pixel_convert.cpp`
- Create: `src/capture/gdi_capture.hpp`, `src/capture/gdi_capture.cpp`
- Create: `src/capture/dxgi_capture.hpp`, `src/capture/dxgi_capture.cpp`
- Create: `src/capture/screen_capture.hpp`, `src/capture/screen_capture.cpp`
- Modify: `CMakeLists.txt` (add capture files + `test_capture_conversion`)
- Test: `tests/test_capture_conversion.cpp`

**Interfaces:**
- Consumes (`Task 1`): `win32_raii.hpp`, `VirtualDesktopMetrics`, `QueryVirtualDesktopMetrics`.
- Produces (`namespace saycheese::capture`):
  - `enum class CaptureBackend { None, DxgiDesktopDuplication, DxgiPartialWithGdiFallback, GdiBitBltFallback, Synthetic };`
  - `struct CapturedFrame { int originX = 0; int originY = 0; int width = 0; int height = 0; int stride = 0; std::vector<uint8_t> bgraPixels; std::vector<platform::MonitorInfo> monitors; CaptureBackend backend = CaptureBackend::None; bool IsValid() const noexcept; };`
  - `enum class SurfaceFormat { BGRA8, RGBA8, R10G10B10A2, RGBA16F };`
  - `enum class SurfaceRotation { Identity, Rotate90, Rotate180, Rotate270 };`
  - `void BlitConvertedSurfaceToFrame(const uint8_t* srcData, int srcWidth, int srcHeight, int srcRowPitch, SurfaceFormat format, SurfaceRotation rotation, float sdrWhiteMultiplier, int dstOffsetX, int dstOffsetY, CapturedFrame& dstFrame);`
  - `float HalfToFloat(uint16_t h) noexcept;`
  - `CapturedFrame CreateSyntheticTestFrame(int width, int height);`
  - `bool CaptureRegionBitBlt(int virtualX, int virtualY, int width, int height, int dstOffsetX, int dstOffsetY, CapturedFrame& dstFrame);`
  - `CapturedFrame CaptureVirtualDesktop();`

- [ ] **Step 1: Write failing `tests/test_capture_conversion.cpp`**
  - Cover all surface formats & Review Focus #3:
    - `BGRA8` copies channels intact and forces alpha to `0xFF` even if source alpha is `0x00`.
    - `RGBA8` swaps R and B channels and forces alpha `0xFF`.
    - `R10G10B10A2` converts `1023` -> `255`, `512` -> `128`, `0` -> `0`, alpha `0xFF`.
    - `RGBA16F` (HDR scRGB): `0.0` -> `0`, `1.0` -> `255` (at `sdrWhiteMultiplier == 1.0f`), negative/NaN/+Inf values clamp safely to `[0, 255]` with `0xFF` alpha, and highlights `> 1.0` tone-map smoothly without wraparound.
    - `Rotate90`, `Rotate180`, `Rotate270` map corner marker pixels from rotated staging surface into the exact expected desktop coordinates, clipping cleanly if offset exceeds bounds (no out-of-bounds write).
    - Negative multi-monitor compositing writes monitor `(-1920, 0)` at `dstOffsetX = 0` and `(0, 0)` at `dstOffsetX = 1920`.

- [ ] **Step 2: Run build to verify `test_capture_conversion` fails before implementation**

- [ ] **Step 3: Implement `captured_frame.hpp`, `pixel_convert.hpp/.cpp`, `gdi_capture.hpp/.cpp`, `dxgi_capture.hpp/.cpp`, `screen_capture.hpp/.cpp`**
  - `dxgi_capture.cpp`:
    - Enumerate hardware `IDXGIAdapter1`s (`DXGI_ADAPTER_FLAG_SOFTWARE` skipped).
    - Create scoped per-adapter `ID3D11Device` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, plus `D3D11_CREATE_DEVICE_DEBUG` in `_DEBUG`).
    - For each attached `IDXGIOutput`, prefer `IDXGIOutput5::DuplicateOutput1` (formats `[B8G8R8A8_UNORM, R8G8B8A8_UNORM, R10G10B10A2_UNORM, R16G16B16A16_FLOAT]`), fallback `IDXGIOutput1::DuplicateOutput`.
    - Bounded loop: max 5 attempts, `AcquireNextFrame(100, ...)`, RAII `ReleaseFrameGuard`, stage via `D3D11_USAGE_STAGING` + `Map`, query per-monitor SDR white level via `DisplayConfigGetDeviceInfo` when format is `R16G16B16A16_FLOAT`, call `BlitConvertedSurfaceToFrame`, immediately break on `DXGI_ERROR_ACCESS_LOST`.
    - Flush and release all DXGI/D3D11 COM objects before returning which monitors succeeded.
  - `gdi_capture.cpp`: RAII `GetDC(nullptr)`, `CreateCompatibleDC`, top-down 32-bit `CreateDIBSection`, `BitBlt(..., SRCCOPY | CAPTUREBLT)`, force alpha `0xFF`.
  - `screen_capture.cpp`: run DXGI first; invoke `CaptureRegionBitBlt` for any uncovered monitor (or full virtual screen if DXGI captured 0 monitors).

- [ ] **Step 4: Build & run `test_capture_conversion`**
  - Run: `cmake --build build --config Debug --target test_capture_conversion && ctest --test-dir build -C Debug -R test_capture_conversion --output-on-failure`
  - Expected: PASS, 0 warnings.

- [ ] **Step 5: Commit Task 4**
  - Commit `"feat: implement non-blocking DXGI capture, HDR/rotation conversion, and BitBlt fallback"`.

---

### Task 5: Direct2D 1.1 Context, Shared WYSIWYG Scene Renderer & Overlay Chrome Renderer

**Files:**
- Create: `src/render/d2d_context.hpp`, `src/render/d2d_context.cpp`
- Create: `src/render/scene_renderer.hpp`, `src/render/scene_renderer.cpp`
- Create: `src/render/ui_renderer.hpp`, `src/render/ui_renderer.cpp`
- Modify: `CMakeLists.txt` (add render sources to `saycheese_core`)

**Interfaces:**
- Consumes (`Tasks 1-4`): `CapturedFrame`, `Annotation`, `SelectionBox`, `TextEditorState`, `ToolbarLayoutResult`, `ComputeArrowhead`, `SmoothPolylineQuadratic`.
- Produces (`namespace saycheese::render`):
  - `class D2DContext`:
    - `bool InitializeForWindow(HWND hwnd, uint32_t width, uint32_t height, bool forceWarp = false);`
    - `bool InitializeHeadless(bool forceWarp = true);` (for offscreen WARP tests without HWND)
    - `void Reset() noexcept;` (flushes immediate context `ClearState(); Flush();` and releases every `ComPtr` in reverse order)
    - `bool UploadDesktopBitmap(const capture::CapturedFrame& frame);`
    - `ID2D1DeviceContext* DeviceContext() const noexcept;`
    - `ID2D1Factory1* D2DFactory() const noexcept;`
    - `IDWriteFactory* DWriteFactory() const noexcept;`
    - `IWICImagingFactory* WicFactory() const noexcept;`
    - `ID2D1Bitmap1* DesktopBitmap() const noexcept;`
    - `ID2D1Bitmap1* SwapChainTargetBitmap() const noexcept;`
    - `HRESULT Present();`
    - `static bool IsDeviceLostError(HRESULT hr) noexcept;`
  - `class SceneRenderer`:
    - `void ClearCache() noexcept;`
    - `void RenderScene(ID2D1DeviceContext* dc, ID2D1Factory1* d2dFactory, IDWriteFactory* dwriteFactory, ID2D1Bitmap* desktopBitmap, const capture::CapturedFrame& sourceFrame, tools::RectF clipRect, const std::vector<tools::Annotation>& annotations, const tools::Annotation* activePreview);`
  - `struct UIRenderState { tools::SelectionBox selection; ui::ToolbarLayoutResult layout; tools::ToolType activeTool; tools::CensorMode censorMode; tools::ColorRGBA activeColor; float activeSize; bool canUndo; bool canRedo; ui::ToolbarButtonId hoveredButton; tools::TextEditorState textEditor; bool showCaret; bool showSizeHud; };`
  - `class UIRenderer`:
    - `void RenderOverlayChrome(ID2D1DeviceContext* dc, IDWriteFactory* dwriteFactory, float canvasWidth, float canvasHeight, const UIRenderState& state);`

- [ ] **Step 1: Implement `src/render/d2d_context.hpp/.cpp`**
  - Hardware `D3D11CreateDevice` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, plus `D3D11_CREATE_DEVICE_DEBUG` in `_DEBUG`) with automatic `D3D_DRIVER_TYPE_WARP` fallback.
  - Create `ID2D1Factory1`, `ID2D1Device`, `ID2D1DeviceContext` (locked `SetDpi(96.0f, 96.0f)`), `IDWriteFactory`, `IWICImagingFactory`.
  - When `hwnd != nullptr`: create `IDXGISwapChain1` (`CreateSwapChainForHwnd`, `DXGI_FORMAT_B8G8R8A8_UNORM`, `BufferCount = 2`, `DXGI_SWAP_EFFECT_FLIP_DISCARD`) and target `ID2D1Bitmap1` from backbuffer; call `IDXGIFactory::MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)`.
  - `Reset()` clears D2D target, clears & flushes D3D11 context, and explicitly resets every `ComPtr`.

- [ ] **Step 2: Implement `src/render/scene_renderer.hpp/.cpp`**
  - Shared WYSIWYG rendering of desktop bitmap + all 7 tools:
    - **Pen:** quadratic Bezier `ID2D1PathGeometry`, round cap/join.
    - **Highlighter:** single `ID2D1PathGeometry` stroked once with `alpha = 0.35f`, `D2D1_CAP_STYLE_FLAT`, `D2D1_LINE_JOIN_ROUND` (zero self-overlap seams).
    - **Line:** round cap straight segment.
    - **Arrow:** shaft to `shaftEnd` + filled `ID2D1PathGeometry` triangle from `ComputeArrowhead`.
    - **Rectangle:** crisp miter outline.
    - **Text:** DirectWrite `IDWriteTextLayout` (`Segoe UI`, SemiBold, word wrapping disabled so explicit `\n` lines match inline preview identically).
    - **Censor — GaussianBlur:** `CLSID_D2D1Crop` (padded `ceil(3 * sigma)`, clamped to desktop bounds) -> `CLSID_D2D1Border` (`D2D1_BORDER_EDGE_MODE_CLAMP`) -> `CLSID_D2D1GaussianBlur` (`STANDARD_DEVIATION = sigma`, `BORDER_MODE = HARD`), drawn inside `PushAxisAlignedClip(censorRect)`.
    - **Censor — Mosaic:** exact box-averaged downscaled RGBA pixels computed from `sourceFrame` into a cached tiny `ID2D1Bitmap` (keyed by annotation ID + rect + block size, cleared on `ClearCache()`), drawn over `censorRect` with `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR`.

- [ ] **Step 3: Implement `src/render/ui_renderer.hpp/.cpp`**
  - Darkened 4-rect backdrop outside `selection` (`rgba(0, 0, 0, 0.45)`), crisp 1px selection outline + 8 DPI-scaled square handles, live `W × H px` dark rounded badge.
  - Floating glass dark Tool Bar, Action Bar, Color Palette popup (6 swatches + custom rainbow/plus icon), `Mosaic | Blur` pill, Thin/Medium/Thick stroke preset popup, live wheel size indicator pill, crisp vector Direct2D icons for every button, and active inline text dashed border + blinking caret.

- [ ] **Step 4: Build `saycheese_core` in Debug & Release**
  - Run: `cmake --build build --config Debug --target saycheese_core && cmake --build build --config Release --target saycheese_core`
  - Expected: Clean compile, 0 warnings at `/W4 /permissive-`.

- [ ] **Step 5: Commit Task 5**
  - Commit `"feat: implement Direct2D 1.1 context, shared WYSIWYG scene renderer, and UI chrome renderer"`.

---

### Task 6: Offscreen WYSIWYG WIC File Exporter & Clipboard DIB Exporter

**Files:**
- Create: `src/export/wic_exporter.hpp`, `src/export/wic_exporter.cpp`
- Create: `src/export/clipboard_export.hpp`, `src/export/clipboard_export.cpp`
- Modify: `CMakeLists.txt` (add export sources + `test_export`)
- Test: `tests/test_export.cpp`

**Interfaces:**
- Consumes (`Tasks 3-5`): `D2DContext`, `SceneRenderer`, `CapturedFrame`, `Annotation`, `RectF`.
- Produces (`namespace saycheese::export_img`):
  - `struct ExportedImage { uint32_t width = 0; uint32_t height = 0; uint32_t stride = 0; std::vector<uint8_t> bgraPixels; Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap; bool IsValid() const noexcept; };`
  - `enum class ImageFileFormat { Png, Jpeg };`
  - `ExportedImage RenderCropToWicBitmap(render::D2DContext& ctx, render::SceneRenderer& renderer, const capture::CapturedFrame& sourceFrame, tools::RectF cropRect, const std::vector<tools::Annotation>& annotations);`
  - `bool SaveWicBitmapToFile(IWICImagingFactory* wicFactory, IWICBitmap* bitmap, const std::wstring& filePath, ImageFileFormat format);`
  - `bool PromptAndSaveImage(HWND ownerHwnd, IWICImagingFactory* wicFactory, IWICBitmap* bitmap, std::wstring& inOutLastDirectory, std::wstring* outSavedPath = nullptr);`
  - `std::wstring BuildDefaultFilename();` (`SayCheese_YYYYMMDD_HHMMSS.png`)
  - `std::vector<uint8_t> BuildDibV5Payload(uint32_t width, uint32_t height, uint32_t srcStride, const uint8_t* bgraPixels);`
  - `std::vector<uint8_t> BuildDibPayload(uint32_t width, uint32_t height, uint32_t srcStride, const uint8_t* bgraPixels);`
  - `bool CopyImageToClipboard(HWND ownerHwnd, const ExportedImage& image);`

- [ ] **Step 1: Write failing `tests/test_export.cpp`**
  - **Review Focus #5 (Odd-width DIB packing & opaque alpha):**
    - `BuildDibV5Payload` and `BuildDibPayload` on `137x91` and `1x1` inputs: verify header sizes (`sizeof(BITMAPV5HEADER)` / `sizeof(BITMAPINFOHEADER)`), exact byte count (`header + height * width * 4`), bottom-up row flip (source row `0` lands at destination row `height - 1`), `bV5RedMask == 0x00FF0000`, `bV5CSType == LCS_sRGB`, and every alpha byte == `0xFF`.
  - **Headless WARP WYSIWYG integration test:**
    - Initialize `D2DContext::InitializeHeadless(true)`, upload a `320x240` synthetic desktop frame, create annotations covering Pen, Line, Arrow, Rectangle, Highlighter, DirectWrite Text, Mosaic Censor, and GaussianBlur Censor inside a `137x91` crop.
    - Call `RenderCropToWicBitmap(...)`: assert `width == 137`, `height == 91`, `wicBitmap != nullptr`, opaque alpha everywhere, and rendered pixels differ measurably from unannotated crop in the annotated regions.
    - Encode to temporary PNG and JPEG files via `SaveWicBitmapToFile`, verify PNG signature `\x89PNG` and JPEG signature `\xFF\xD8\xFF`, delete temp files cleanly.
    - Reset `D2DContext` and assert `ReportDebugDxgiLiveObjects().liveApplicationObjects == 0` in Debug builds!

- [ ] **Step 2: Run build to verify `test_export` fails before implementation**

- [ ] **Step 3: Implement `wic_exporter.hpp/.cpp` and `clipboard_export.hpp/.cpp`**
  - `RenderCropToWicBitmap`: create offscreen `ID2D1Bitmap1` (`D2D1_BITMAP_OPTIONS_TARGET`), save/restore `dc->GetTarget()`, translate by `(-cropLeft, -cropTop)`, call `SceneRenderer::RenderScene`, copy to `D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW` staging bitmap, `Map(D2D1_MAP_OPTIONS_READ)`, copy BGRA rows forcing alpha `0xFF`, wrap in `IWICImagingFactory::CreateBitmapFromMemory`.
  - `PromptAndSaveImage`: `GetSaveFileNameW` with PNG default (`*.png`) and JPEG (`*.jpg;*.jpeg`), `OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR`, infer format from filter index / extension, update `inOutLastDirectory`.
  - `CopyImageToClipboard`: `OpenClipboard` -> `EmptyClipboard` -> `SetClipboardData(CF_DIBV5, ...)` + `SetClipboardData(CF_DIB, ...)` -> `CloseClipboard` (RAII clipboard guard).

- [ ] **Step 4: Build & run `test_export` in Debug & Release**
  - Run: `cmake --build build --config Debug --target test_export && ctest --test-dir build -C Debug -R test_export --output-on-failure`
  - Expected: PASS, 0 warnings, 0 live DXGI objects in Debug.

- [ ] **Step 5: Commit Task 6**
  - Commit `"feat: implement offscreen WYSIWYG WIC exporter and CF_DIBV5/CF_DIB clipboard writer"`.

---

### Task 7: Ephemeral Overlay Window, Hotkey Manager, System Tray App & `wWinMain`

**Files:**
- Create: `src/ui/overlay_window.hpp`, `src/ui/overlay_window.cpp`
- Create: `src/app/hotkey_manager.hpp`, `src/app/hotkey_manager.cpp`
- Create: `src/app/tray_icon.hpp`, `src/app/tray_icon.cpp`
- Create: `src/app/app_controller.hpp`, `src/app/app_controller.cpp`
- Create: `src/main.cpp`
- Modify: `CMakeLists.txt` (add `overlay_window` and `app/*` sources to `saycheese_core`; add `SayCheese` `WIN32` executable target containing `src/main.cpp` + `resources/app.rc` linking `saycheese_core`)

**Interfaces:**
- Consumes (`Tasks 1-6`): `ScreenCapture`, `D2DContext`, `SceneRenderer`, `UIRenderer`, `ToolController`, `ToolbarLayout`, `WICExporter`, `ClipboardExport`, `SingleInstanceGuard`, `win32_raii.hpp`.
- Produces (`namespace saycheese::ui` / `namespace saycheese::app`):
  - `enum class OverlayExitReason { Cancelled, CopiedToClipboard, SavedToFile, RenderError, ProgrammaticClose };`
  - `struct OverlayCallbacks { std::function<void(const std::wstring& title, const std::wstring& msg)> notifyUser; std::wstring* lastSaveDir = nullptr; };`
  - `class OverlayWindow`:
    - `bool CreateAndShow(HINSTANCE hInstance, capture::CapturedFrame frame, OverlayCallbacks callbacks, bool headlessTestMode = false);`
    - `void RenderFrame();`
    - `void DestroyAndRelease() noexcept;` (idempotent fail-safe teardown)
    - `bool IsOpen() const noexcept;`
    - `OverlayExitReason ExitReason() const noexcept;`
    - `void ApplySyntheticTestAnnotations();` + `bool RunOffscreenExportCheck();` (for Task 8's leak self-test without polluting production UI)
  - `class HotkeyManager`: `bool RegisterPrimaryOrFallback(HWND hwnd); bool RetryPrimary(HWND hwnd); void Unregister(HWND hwnd) noexcept; bool IsUsingFallback() const noexcept; bool IsRegistered() const noexcept; std::wstring DescribeStatus() const;`
  - `class TrayIcon`: `bool Create(HWND hwnd, HICON icon, UINT callbackMsg); void ShowBalloon(const std::wstring& title, const std::wstring& message, DWORD infoFlags = NIIF_INFO); void Destroy() noexcept;`
  - `class AppController`: `int Run(HINSTANCE hInstance);`

- [ ] **Step 1: Implement `src/ui/overlay_window.hpp/.cpp`**
  - Register window class once (`SayCheeseOverlayWndClass`), create `WS_POPUP` with `WS_EX_TOPMOST | WS_EX_TOOLWINDOW` at `(frame.originX, frame.originY, frame.width, frame.height)` (in `headlessTestMode`, omit `WS_VISIBLE`/`WS_EX_TOPMOST` so stress tests never hijack focus or flash the user's screen while exercising the exact real Win32 HWND + D3D11/D2D1 SwapChain lifecycle!).
  - Route all pointer, wheel, keyboard (`Ctrl+Z/Y/C/S/A`, `ESC`, `WM_CHAR`, `WM_KEYDOWN` for text caret/backspace/delete/enter), double-click monitor selection, right-click-outside cancel, `WM_SETCURSOR`, and caret blink timer (`530 ms`, killed on destroy!).
  - Safely drop `HWND_TOPMOST` before `ChooseColorW` or `PromptAndSaveImage`, restoring `HWND_TOPMOST` if staying open.
  - Handle device-lost (`D2DContext::IsDeviceLostError`) with single clean rebuild from CPU `CapturedFrame`; on second failure, call `DestroyAndRelease()`.

- [ ] **Step 2: Implement `hotkey_manager.hpp/.cpp`, `tray_icon.hpp/.cpp`, `app_controller.hpp/.cpp`, and `main.cpp`**
  - `HotkeyManager`: tries `RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_SNAPSHOT)`; on failure tries `MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S'`.
  - `TrayIcon`: `Shell_NotifyIconW` (`NIF_MESSAGE | NIF_ICON | NIF_TIP`), balloon via `NIF_INFO`, context menu with *Capture Now*, *Hotkey Status: ...*, *Exit* (calls `SetForegroundWindow` before `TrackPopupMenu` + `PostMessage(WM_NULL)` per MSDN).
  - `AppController`: owns hidden message window (`HWND_MESSAGE`? Note: `RegisterHotKey` + `Shell_NotifyIconW` work best on a hidden top-level `WS_OVERLAPPED` window so it also receives `TaskbarCreated` message after Explorer restarts! Handle `RegisterWindowMessageW(L"TaskbarCreated")` to automatically restore tray icon!).
  - `main.cpp`: enable `_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)` in `_DEBUG`, initialize `ComInitScope(COINIT_APARTMENTTHREADED)`, parse `--self-test-leaks` (dispatches to stubbed/wired runner in Task 8, or directly once Task 8 lands — let's create `leak_self_test` interface stub? NO stubs! Wait: put `leak_self_test` right in Task 8 OR link it cleanly so Task 7 compiles with zero stubs! Better: parse `--self-test-leaks` in Task 8 when adding `leak_self_test.hpp/.cpp`!).

- [ ] **Step 3: Build `SayCheese.exe` in both Debug and Release and verify binary properties**
  - Run: `cmake --build build --config Debug --target SayCheese && cmake --build build --config Release --target SayCheese`
  - Verify Release `.exe` size (target ~1 MB) and run `dumpbin /dependents` (via `vcvars64.bat`) to confirm zero `VCRUNTIME*.dll` / `MSVCP*.dll` / third-party DLL dependencies!
  - Expected: Clean compile with 0 warnings, static CRT verified.

- [ ] **Step 4: Commit Task 7**
  - Commit `"feat: implement ephemeral overlay window, hotkey manager, system tray app, and entry point"`.

---

### Task 8: Automated 2+20 Cycle Leak Stress Suite, GitHub Actions Release Pipeline & Documentation

**Files:**
- Create: `src/app/leak_self_test.hpp`, `src/app/leak_self_test.cpp`
- Modify: `src/main.cpp` (wire `--self-test-leaks`: preserve redirected stdout pipes if `GetStdHandle(STD_OUTPUT_HANDLE)` is valid, else `AttachConsole(ATTACH_PARENT_PROCESS)` + `freopen_s` so stdout prints cleanly both interactively and when captured)
- Create: `tests/test_leak_stress.cpp`
- Modify: `CMakeLists.txt` (add `leak_self_test.cpp` to `saycheese_core` + add `test_leak_stress` console executable + `add_test`)
- Create: `.github/workflows/release.yml`, `README.md`, `LICENSE`

**Interfaces:**
- Consumes (`Tasks 1, 4, 5, 6, 7`): `CaptureVirtualDesktop`, `CreateSyntheticTestFrame`, `OverlayWindow`, `CaptureProcessResourceSnapshot`, `ReportDebugDxgiLiveObjects`.
- Produces (`namespace saycheese::app`):
  - `struct LeakTestResult { bool passed; bool dxgiSubtestSkipped; capture::CaptureBackend backendUsed; platform::ProcessResourceSnapshot cycle2Baseline; platform::ProcessResourceSnapshot cycle22Final; platform::DxgiLiveObjectReport dxgiReport; std::string reportText; };`
  - `LeakTestResult RunLeakSelfTest(HINSTANCE hInstance, int warmupCycles = 2, int measuredCycles = 20);`

- [ ] **Step 1: Implement `src/app/leak_self_test.hpp/.cpp`, wire `--self-test-leaks` in `src/main.cpp`, and add `tests/test_leak_stress.cpp`**
  - Loop `cycle = 1 .. (warmupCycles + measuredCycles)`:
    1. Call `capture::CaptureVirtualDesktop()`. If invalid (headless CI with no interactive desktop/DXGI output), record `dxgiSubtestSkipped = true` and use `capture::CreateSyntheticTestFrame(1280, 720)`.
    2. Create `OverlayWindow` in `headlessTestMode = true`, apply representative annotations covering all 7 tools (Pen, Line, Arrow, Rectangle, Highlighter, Text, Mosaic Censor, GaussianBlur Censor), render 2 frames, run `RunOffscreenExportCheck()`, then call `DestroyAndRelease()`.
    3. Pump pending Win32 destroy messages (`PeekMessageW` drain loop) so OS destroys `HWND` synchronously before measuring.
    4. Snapshot at `cycle == warmupCycles` (baseline) and `cycle == warmupCycles + measuredCycles` (final).
  - Assert:
    - `final.gdiObjects - baseline.gdiObjects == 0`
    - `final.userObjects - baseline.userObjects == 0`
    - `final.handleCount - baseline.handleCount == 0`
    - `final.privateUsageBytes <= baseline.privateUsageBytes + (2ULL * 1024 * 1024)` (delta `< 2 MiB`)
    - In `_DEBUG`: `dxgiReport.liveApplicationObjects == 0` (when `dxgidebug.dll` is present).
  - Print crisp structured summary table of Cycle 2, Cycle 22, and Deltas.

- [ ] **Step 2: Create `.github/workflows/release.yml`, `README.md`, and `LICENSE` (MIT)**
  - `.github/workflows/release.yml`: runs on `windows-latest` on `push`/`pull_request` to `main` and tag pushes `v*`; configures CMake x64, builds `Release`, runs `ctest -C Release --output-on-failure`, runs `SayCheese.exe --self-test-leaks`, and on `v*` tags generates `SHA256SUMS.txt` (`Get-FileHash -Algorithm SHA256`) and creates a GitHub Release via `softprops/action-gh-release@v2` attaching `SayCheese.exe` and `SHA256SUMS.txt`.
  - `README.md`: project overview, anti-freeze architecture overview, feature list, build instructions (CMake + MSVC), keyboard shortcuts table, `--self-test-leaks` verification instructions, license.
  - `LICENSE`: standard MIT License.

- [ ] **Step 3: Full Verification (Debug + Release Builds, Full CTest Suite & Local `--self-test-leaks`)**
  - Run:
    - `cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
    - `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
    - `.\build\Release\SayCheese.exe --self-test-leaks`
    - `.\build\Debug\SayCheese.exe --self-test-leaks`
  - Record exact measured Cycle 2 vs Cycle 22 GDI, USER, Handle, PrivateUsage, and Debug DXGI live-object counts.

- [ ] **Step 4: Commit Task 8**
  - Commit `"feat: add 20-cycle leak stress suite, GitHub Actions release pipeline, and docs"`.

---

## Plan Self-Review Notes
- **Spec Coverage:** Every section of `docs/superpowers/specs/2026-11-19-saycheese-design.md` (anti-freeze hotkeys, DXGI + HDR + rotation + BitBlt fallback, PerMonitorV2 + per-monitor DPI chrome, all 7 tools, append-only Undo/Redo, color/stroke controls, shared WYSIWYG export, `CF_DIBV5`/`CF_DIB` + PNG/JPEG, 2+20 leak stress test with Debug DXGI/CRT checks and headless CI fallback, GitHub Actions CI/Release, README/MIT) is explicitly owned by Tasks 1–8.
- **Type Consistency:** Verified `PointF`, `RectF`, `MonitorInfo`, `CapturedFrame`, `Annotation`, `ToolbarLayoutResult`, `D2DContext`, `SceneRenderer`, `ExportedImage`, and `ProcessResourceSnapshot` names and namespaces across all task Interfaces blocks.
- **Review Focus:** All 5 uncovered failure modes have explicit assertions pinned to Tasks 2, 3, 4, and 6.
