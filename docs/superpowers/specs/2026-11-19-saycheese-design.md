# SayCheese — Architectural Design Specification

**Date:** 2026-11-19  
**Target Platform:** Windows 10 / 11 (x64)  
**Language & Toolchain:** Modern C++20, MSVC (`/W4 /permissive-`, static CRT `/MT`), CMake 3.20+, Zero Third-Party Dependencies  
**License:** MIT (Public GitHub Release)

---

## 1. Intent & Why

**SayCheese** is a production-grade, zero-dependency Windows screenshot capture and annotation desktop utility modeled after Lightshot's fast workflow, engineered specifically to eliminate OS/DWM freezes, input queue lockups, idle GPU bloat, and resource leaks.

Existing screenshot utilities commonly install low-level keyboard hooks (`WH_KEYBOARD_LL`) that stall Windows input processing, keep persistent capture sessions alive across display/UAC transitions, or pull in heavy frameworks. SayCheese targets a ~1 MB standalone portable x64 executable that runs silently in the System Tray with zero idle GPU footprint, captures multi-monitor mixed-DPI desktops reliably without ever hanging, provides crisp Direct2D annotation tools with pixel-identical WYSIWYG export, and verifies zero resource leaks automatically.

---

## 2. Goals, Non-Goals, and Anti-Goals

### Goals
- **Zero-dependency native build:** Pure Win32 API + DXGI Desktop Duplication + Direct2D 1.1 (`ID2D1DeviceContext`) + DirectWrite + WIC. Link only against standard Windows SDK libraries. Static CRT (`/MT` in Release) so no VC++ Redistributable is required.
- **Anti-freeze reliability:**
  - Use `RegisterHotKey` (`VK_SNAPSHOT` with graceful fallback to `Ctrl+Shift+S` + tray balloon notification) — never `WH_KEYBOARD_LL`.
  - Non-blocking DXGI capture with bounded timeout (`<= 100 ms` per frame acquire, bounded retries), HDR/10-bit format conversion, monitor rotation support, and automatic `BitBlt` fallback.
  - Per-Monitor DPI Awareness v2 (`PerMonitorV2`) with pixel-accurate multi-monitor capture (including negative virtual screen coordinates) and per-monitor scaled UI chrome.
  - Instant fail-safe teardown on `ESC` (or right-click outside selection), releasing all GPU/window/staging resources immediately.
- **Complete v1.0 Lightshot-style feature set:**
  - Single-instance System Tray background app (*Capture Now*, *Hotkey Status*, *Exit*).
  - Fullscreen frozen overlay with darkened backdrop (`rgba(0, 0, 0, 0.45)`), drag-to-select, move, 8 resize handles, and live `W × H px` badge.
  - Floating Direct2D toolbars clamped inside monitor bounds with 7 annotation tools: **Pen**, **Line** (`Shift` 45° snap), **Arrow**, **Rectangle** (`Shift` square), **Highlighter**, **Inline DirectWrite Text**, and **Censor (Mosaic Pixelate / Gaussian Blur)**.
  - Append-only annotation workflow with Command Pattern **Undo (`Ctrl+Z`) / Redo (`Ctrl+Y`)**, quick color palette + `ChooseColorW`, live stroke/text/censor sizing via mouse wheel + Thin/Medium/Thick presets, **Copy (`Ctrl+C`)**, **Save (`Ctrl+S`)**, and **Cancel (`ESC`)**.
  - **WYSIWYG export guarantee:** On-screen preview and offscreen export share the exact same Direct2D 1.1 rendering path.
- **Automated leak & stress verification:** 20-cycle programmatic capture → overlay → render → teardown stress test (after 2 warm-up cycles) asserting zero GDI/USER/Handle leaks, `< 2 MB` PrivateUsage delta, zero Debug DXGI/CRT leaks, and headless CI compatibility.
- **GitHub-ready release pipeline:** Clean modular repo structure, unit tests via CTest, GitHub Actions CI/Release workflow (`windows-latest`) attaching standalone `.exe` and `SHA256SUMS.txt` on `v*` tags, `README.md`, and MIT `LICENSE`.

### Non-Goals (v1.0)
- Post-commit annotation selection, dragging, resizing, or re-editing (explicitly append-only + Undo/Redo, decided by user).
- Cloud uploading, URL shortening, OCR, video/GIF recording, scrolling capture, or auto-updating.
- Persistent on-disk settings/registry configuration *(builder/architect proposal: keep v1.0 zero-footprint; remember last save directory in-memory for the current process session)*.
- Windows 7/8, x86, or ARM64 targets.

### Anti-Goals (Failure Conditions)
- Installing any `WH_KEYBOARD_LL` or `WH_MOUSE_LL` hook.
- Blocking indefinitely in `AcquireNextFrame` or leaving a topmost overlay stuck on screen after an error.
- Holding `IDXGIOutputDuplication` or D3D11/DXGI GPU contexts alive while idle in the tray.
- Divergent rendering logic between on-screen preview and exported clipboard/file output.
- Any GDI/USER/HANDLE/COM/CRT leak, raw `new`/`delete`, or stubbed/TODO pipelines.

---

## 3. Architectural Approaches Considered

### Approach A: Fully Ephemeral Per-Capture Pipeline *(Selected)*
- **Idle in tray:** Holds only a hidden Win32 message window, tray icon, registered hotkey, and per-session mutex. **0 MB GPU VRAM, zero D3D11/DXGI/Direct2D/DirectWrite/WIC objects.**
- **Capture phase:** Spins up scoped per-adapter D3D11 devices + `IDXGIOutputDuplication`, acquires and composites all monitors into a virtual-desktop BGRA8 CPU buffer (with HDR/rotation handling and `BitBlt` fallback), and releases **all** duplication interfaces before creating the overlay.
- **Overlay & Export phase:** Creates a scoped D3D11/Direct2D 1.1 device (hardware with automatic WARP fallback for headless CI), a flip-model swap chain (`CreateSwapChainForHwnd`), and a shared `SceneRenderer` driving both live preview and offscreen `ID2D1Bitmap1` → `IWICBitmap` export.
- **Teardown:** `ESC`, export completion, or unrecoverable error flushes the D3D11 immediate context and destroys every COM/GDI/HWND resource immediately via RAII.
- **Why selected:** Strongest anti-freeze and zero-idle-footprint guarantee (won't pin hybrid laptop dGPUs or hold stale contexts across sleep, docking, RDP, or driver resets) and ensures every capture cycle independently hits zero live DXGI objects. The ~40–80 ms cold-start cost is imperceptible on capture trigger.

### Approach B: Hybrid (Persistent GPU Context + Ephemeral Capture/Overlay) *(Not Selected)*
- Keeps `ID3D11Device`, `ID2D1Factory1`/`ID2D1Device`, `IDWriteFactory`, and `IWICImagingFactory` alive across the app lifetime while recreating duplication, swap chain, and overlay per session.
- **Trade-offs:** Saves ~20–40 ms trigger latency, but holds permanent GPU memory/driver threads while idle and increases device-lost/adapter-change complexity.

### Approach C: Persistent Background Duplication & Hidden Overlay *(Rejected)*
- Keeps `IDXGIOutputDuplication` and a hidden topmost overlay permanently alive.
- **Why rejected:** Directly conflicts with the anti-freeze goal due to continuous `DXGI_ERROR_ACCESS_LOST` churn on UAC/lock/fullscreen transitions, continuous GPU consumption, and topmost DWM focus hazards.

---

## 4. Constraints & Toolchain

- **Standard:** C++20, MSVC warnings at `/W4 /permissive-`, no raw `new`/`delete` anywhere in product or test code.
- **COM & Win32 Lifetime:** `Microsoft::WRL::ComPtr<T>` for all DirectX/DXGI/Direct2D/DirectWrite/WIC interfaces; custom lightweight RAII wrappers (`UniqueHandle`, `UniqueHwnd`, `UniqueHdc`, `UniqueHbitmap`, `UniqueHicon`, `UniqueHmenu`, `UniqueGlobalMem`, `ComInitScope`) for all Win32 handles.
- **Windows SDK Libraries Only:** `d3d11.lib`, `dxgi.lib`, `d2d1.lib`, `dwrite.lib`, `windowscodecs.lib`, `dwmapi.lib`, `shell32.lib`, `comdlg32.lib`, `user32.lib`, `gdi32.lib`, plus standard Windows SDK system libs required for COM/GUID/DPI/process metrics (`ole32.lib`, `dxguid.lib`, `shcore.lib`, `psapi.lib`). *(Zero external packages or third-party headers.)*
- **CRT Linking:** `CMAKE_MSVC_RUNTIME_LIBRARY` set to `"MultiThreaded$<$<CONFIG:Debug>:Debug>"` (`/MT` for Release, `/MTd` for Debug so `_CrtSetDbgFlag` heap leak diagnostics function cleanly).
- **Build Compatibility:** Standard CMake 3.20+ configuration working seamlessly with both Ninja / VS 2026 locally and MSVC generators on GitHub Actions `windows-latest`.
- **Resources:** `resources/app.manifest` (`PerMonitorV2` + Common Controls v6) and `resources/app.rc` (`VERSIONINFO` + crisp multi-resolution vector-derived `resources/saycheese.ico` with 16/24/32/48/64/256 px frames).

---

## 5. Module Architecture & Directory Layout

```text
SayCheese/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .github/workflows/release.yml
├── resources/
│   ├── app.manifest
│   ├── app.rc
│   ├── resource.h
│   └── saycheese.ico
├── src/
│   ├── main.cpp                      # wWinMain entry, CLI flag parsing (--self-test-leaks), COM/CRT init
│   ├── platform/
│   │   ├── win32_raii.hpp            # RAII guards for Win32 handles, GDI objects, GlobalLock, COM scope
│   │   ├── dpi_monitor.hpp/.cpp      # Virtual desktop rect, monitor enumeration, per-monitor DPI queries
│   │   ├── com_debug.hpp/.cpp        # Process GDI/USER/Handle/PrivateUsage snapshots + Debug DXGI live-object reporting
│   │   └── single_instance.hpp       # Named Local\SayCheese_SingleInstance_Mutex RAII guard
│   ├── app/
│   │   ├── app_controller.hpp/.cpp   # Hidden tray HWND message loop, hotkey & tray coordination, session lifecycle
│   │   ├── hotkey_manager.hpp/.cpp   # RegisterHotKey(VK_SNAPSHOT) + Ctrl+Shift+S fallback + retry logic
│   │   ├── tray_icon.hpp/.cpp        # Shell_NotifyIconW, balloon notifications, context menu
│   │   └── leak_self_test.hpp/.cpp   # Programmatic 2+20 cycle leak stress runner invoked by --self-test-leaks and CTest
│   ├── capture/
│   │   ├── captured_frame.hpp        # VirtualScreen bounds, monitor metadata, BGRA8 pixel buffer, CaptureBackend enum
│   │   ├── screen_capture.hpp/.cpp   # Top-level capture coordinator (DXGI -> per-output/global BitBlt fallback)
│   │   ├── dxgi_capture.hpp/.cpp     # Per-adapter D3D11 + IDXGIOutputDuplication, <=100ms timeout, HDR/10-bit & rotation conversion
│   │   └── gdi_capture.hpp/.cpp      # BitBlt SRCCOPY|CAPTUREBLT fallback with RAII DIBSection
│   ├── tools/
│   │   ├── geometry.hpp/.cpp         # Pure testable math: RectF/PointF, SelectionBox (drag/move/8 handles/clamp), 45° snap, square lock, arrowhead polygon, quadratic Bezier path smoothing
│   │   ├── annotation.hpp            # Data types for all 7 annotation tools + StrokeStyle + CensorMode
│   │   ├── command_stack.hpp/.cpp    # Command Pattern Undo/Redo stack
│   │   └── tool_controller.hpp/.cpp  # Pure testable state machine: active tool, pointer events, Shift modifiers, inline TextEditorState, wheel sizing, palette/preset state
│   ├── render/
│   │   ├── d2d_context.hpp/.cpp      # Scoped D3D11Device (HW -> WARP fallback), DXGI swap chain, ID2D1Device/DeviceContext, IDWriteFactory, IWICImagingFactory
│   │   ├── scene_renderer.hpp/.cpp   # Shared WYSIWYG renderer (desktop bitmap + all 7 annotations, GaussianBlur effect, Mosaic blocks)
│   │   └── ui_renderer.hpp/.cpp      # Direct2D overlay chrome (dark backdrop, handles, W×H badge, toolbars, color popup, censor pill, size HUD)
│   ├── ui/
│   │   ├── toolbar_layout.hpp/.cpp   # Pure testable toolbar geometry, smart monitor-clamped docking, button hit-testing
│   │   └── overlay_window.hpp/.cpp   # Ephemeral topmost Win32 overlay HWND, message/input dispatch, ChooseColorW / GetSaveFileNameW modal handling, fail-safe teardown
│   └── export/
│       ├── wic_exporter.hpp/.cpp     # Offscreen ID2D1Bitmap1 -> IWICBitmap render via SceneRenderer, WIC PNG/JPEG encoding, GetSaveFileNameW
│       └── clipboard_export.hpp/.cpp # BGRA pixel buffer -> CF_DIBV5 + CF_DIB clipboard writer
└── tests/
    ├── test_runner.hpp               # Minimal zero-dependency assertion & suite runner
    ├── test_command_stack.cpp        # Undo/Redo stack invariants & edge cases
    ├── test_geometry.cpp             # Selection math, negative coords, 45°/square snap, toolbar monitor clamping, DPI scaling
    ├── test_tool_controller.cpp      # Tool state machine, smoothing, inline text edit/commit/cancel, censor toggle, wheel bounds
    └── test_leak_stress.cpp          # CTest wrapper running the 2+20 cycle leak stress verifier
```

Core pure-logic modules (`tools/`, `ui/toolbar_layout`, `platform/dpi_monitor`) are compiled into a static library target (`saycheese_core`) linked by both `SayCheese.exe` and the unit test binaries so unit tests run directly against production code without requiring a GUI window.

---

## 6. Detailed Subsystem Specifications

### 6.1 Coordinate System & Mixed-DPI Mapping
- **Virtual Desktop Space (Physical Pixels):** Obtained via `GetSystemMetrics(SM_XVIRTUALSCREEN / SM_YVIRTUALSCREEN / SM_CXVIRTUALSCREEN / SM_CYVIRTUALSCREEN)`. Origin `(virtualX, virtualY)` may be negative when a secondary monitor sits left of or above the primary monitor.
- **Overlay Canvas Space (Local Physical Pixels):**
  - Overlay `HWND` is positioned at `(virtualX, virtualY)` with size `(virtualW, virtualH)`.
  - Client-area pointer coordinates `(x, y)` map directly to `[0, virtualW) × [0, virtualH)` (`canvasPt = virtualPt - virtualOrigin`).
  - `ID2D1DeviceContext::SetDpi(96.0f, 96.0f)` locks **1 D2D DIP == 1 physical pixel** so captured desktop pixels render 1:1 with zero resampling blur across mixed-DPI monitors.
- **Per-Monitor UI Chrome Scaling:**
  - `EnumDisplayMonitors` collects each monitor's `RECT` (shifted into canvas space) and effective DPI via `GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)` (default 96 DPI).
  - Toolbars, resize handles, dimension badges, and popups query `uiScale = monitorDpi / 96.0f` for the monitor containing the anchor point, ensuring crisp physical sizing on both 100% and 150%/200% displays.

### 6.2 Background Tray App & Hotkey Management
- **Single-Instance Guard:** `CreateMutexW(nullptr, FALSE, L"Local\\SayCheese_SingleInstance_Mutex")`. If `GetLastError() == ERROR_ALREADY_EXISTS`, exit cleanly with code `0` without spawning a second tray icon. *(Uses `Local\` per-session namespace so multi-user/RDP sessions work independently.)*
- **Tray Icon (`Shell_NotifyIconW`):** Embedded `IDI_APP_ICON`, tooltip `"SayCheese"`, custom callback message `WM_APP_TRAYICON`. Right-click context menu:
  1. **Capture Now** — triggers capture immediately.
  2. **Hotkey Status: `<PrintScreen | Ctrl+Shift+S (Retry PrintScreen)>`** — clicking displays a balloon with current status and, if currently on fallback, re-attempts registering `VK_SNAPSHOT` *(our call)*.
  3. Separator
  4. **Exit** — tears down any active session, removes tray icon, unregisters hotkey, exits cleanly.
  - Left double-click on tray icon also triggers **Capture Now** *(our call)*.
- **Hotkey Registration:**
  1. Attempt `RegisterHotKey(hwnd, HOTKEY_ID, MOD_NOREPEAT, VK_SNAPSHOT)`.
  2. If it fails (e.g., Windows 11 Snipping Tool owns `PrintScreen`), register fallback `RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S')` and show a tray balloon notification (`NIIF_INFO`) informing the user that PrintScreen is in use and `Ctrl+Shift+S` is active.
  3. If both fail, display a warning balloon and allow manual capture via *Capture Now* in the tray menu.
- **Re-entrancy Guard:** Pressing the hotkey while an overlay session is already active is ignored (does not stack overlays).

### 6.3 Non-Blocking DXGI Capture & `BitBlt` Fallback
1. Query virtual desktop bounds and monitor metadata (`MonitorInfo` list). Allocate `CapturedFrame` (`virtualW × virtualH × 4` bytes, BGRA8, alpha `0xFF`).
2. **DXGI Desktop Duplication Path:**
   - Create `IDXGIFactory1`. Enumerate hardware adapters (`EnumAdapters1`, skipping `DXGI_ADAPTER_FLAG_SOFTWARE`).
   - For each adapter that has ≥1 output with `AttachedToDesktop == TRUE`, create a scoped `ID3D11Device` on **that specific adapter** (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, plus `D3D11_CREATE_DEVICE_DEBUG` in Debug builds).
   - For each attached output:
     - Prefer `IDXGIOutput5::DuplicateOutput1` with supported formats `[B8G8R8A8_UNORM, R8G8B8A8_UNORM, R10G10B10A2_UNORM, R16G16B16A16_FLOAT]`, falling back to `IDXGIOutput1::DuplicateOutput`.
     - **Bounded acquisition:** At most **5 attempts**, each calling `AcquireNextFrame(100, &frameInfo, &resource)` (strict 100 ms timeout).
       - Wrap every successful `AcquireNextFrame` in an RAII `ReleaseFrameGuard`.
       - On `S_OK`: copy the `ID3D11Texture2D` to a `D3D11_USAGE_STAGING` (`CPU_ACCESS_READ`) texture. If `LastPresentTime.QuadPart != 0 || AccumulatedFrames > 0`, capture for this output is complete; if `LastPresentTime == 0` on attempt 0, allow one short retry (`50 ms`) and keep the valid staged frame if the retry times out on a static desktop.
       - On `DXGI_ERROR_WAIT_TIMEOUT`: retry up to the bounded attempt limit, then mark this output for `BitBlt` fallback.
       - On `DXGI_ERROR_ACCESS_LOST` or any failure `HRESULT`: abort retries immediately and mark this output for `BitBlt` fallback.
     - **Format Conversion & Rotation Normalization:**
       - `B8G8R8A8_UNORM` / `_SRGB`: direct copy with alpha forced to `0xFF`.
       - `R8G8B8A8_UNORM` / `_SRGB`: R/B channel swap, alpha `0xFF`.
       - `R10G10B10A2_UNORM`: 10-bit to 8-bit linear quantization `(v * 255 + 511) / 1023`, alpha `0xFF`.
       - `R16G16B16A16_FLOAT` (Windows HDR scRGB): IEEE-754 FP16 decode → scale using per-display SDR white level (`DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`, fallback `1.0`) → Reinhard/clamp highlight rolloff → sRGB transfer curve → BGRA8 (`0xFF`).
       - Respect `DXGI_OUTPUT_DESC::Rotation` (`ROTATE90`, `ROTATE180`, `ROTATE270`) when writing into the virtual desktop buffer at `DesktopCoordinates - virtualOrigin`.
   - Destroy all `IDXGIOutputDuplication`, staging textures, and capture `ID3D11Device` instances immediately upon leaving the capture scope.
3. **Automatic `BitBlt` Fallback:**
   - Runs automatically for any monitor not captured by DXGI (or for the entire virtual desktop if DXGI failed completely, e.g., RDP or basic display adapter).
   - Uses `GetDC(nullptr)`, `CreateCompatibleDC`, top-down 32-bit `CreateDIBSection`, and `BitBlt(..., SRCCOPY | CAPTUREBLT)` strictly wrapped in RAII handles, forcing alpha bytes to `0xFF`.

### 6.4 Ephemeral Overlay, Interaction & Fail-Safe Teardown
- **Window creation:** Borderless `WS_POPUP` with `WS_EX_TOPMOST | WS_EX_TOOLWINDOW`, spanning `(virtualX, virtualY, virtualW, virtualH)`.
- **Selection Lifecycle:**
  1. Initial state (`Selecting` / idle): entire screen dimmed (`rgba(0,0,0,0.45)`), crosshair cursor.
     - Dragging defines the selection rectangle.
     - Double-clicking selects the monitor under the cursor; `Ctrl+A` selects the entire virtual screen *(our call)*.
  2. Active Selection state:
     - Outside selection remains dimmed; inside shows crisp original capture + annotations.
     - **8 resize handles** (corners + edge midpoints) resize with clamping to virtual screen bounds; dragging inside selection moves it when no drawing tool is active (or while holding `Space` / dragging border).
     - Live dimension badge (`W × H px`) docked above top-left of selection (flipped inside if near monitor top).
- **Drawing Tools & Controls:**
  - **Pen:** Freehand points smoothed with quadratic midpoint Bezier curves (`ID2D1PathGeometry`), round cap/join.
  - **Line:** Start → end; holding `Shift` snaps angle to nearest `45°`.
  - **Arrow:** Shaft (with `Shift` 45° snap) stopped cleanly at arrowhead base + filled scaled arrowhead polygon.
  - **Rectangle:** Crisp outline; holding `Shift` constrains to a square.
  - **Highlighter:** Wide semi-transparent freehand stroke (`alpha = 0.35f`, flat cap, round join, width = `max(12, strokeWidth * 4)`) rendered as a single `ID2D1PathGeometry` so overlapping bends have uniform opacity.
  - **Text:** Click inside selection to create an inline editable DirectWrite text box (`Segoe UI`, live typing via `WM_CHAR`, `Backspace`/`Delete`/Left/Right arrows, `Enter` newline, color picker updates active box live, mouse wheel & presets adjust font size `14..72 px` with presets `18 / 28 / 42 px`). Clicking outside commits non-empty text into the command stack; empty text discards cleanly.
  - **Censor (Blur / Pixelate):** Drag a rectangle to censor sensitive content. Selecting Censor shows a docked `Mosaic | Blur` toggle pill; mouse wheel adjusts block size (`6..32 px`, presets `8 / 14 / 22 px`) or Gaussian blur sigma (`4..24 px`, presets `6 / 12 / 18 px`).
  - **Undo / Redo:** `Ctrl+Z` / `Ctrl+Y` (and toolbar buttons) operate on `CommandStack` (append-only commands; new annotation clears redo stack).
  - **Color Picker:** Quick palette (Red `#FF3B30`, Yellow `#FFCC00`, Green `#34C759`, Blue `#007AFF`, White `#FFFFFF`, Black `#1C1C1E`) + custom button opening native `ChooseColorW`.
  - **Stroke Width:** Mouse wheel adjusts active tool size live with an on-canvas HUD pill; preset buttons offer Thin (`2 px`), Medium (`5 px`), Thick (`10 px`).
- **Modal Dialog Safety:** Before calling `ChooseColorW` or `GetSaveFileNameW`, temporarily remove `HWND_TOPMOST` (passing overlay `HWND` as owner) and restore `HWND_TOPMOST` if returning to the overlay, ensuring system dialogs are never hidden behind the overlay.
- **Fail-Safe Exits:**
  - `ESC`: If an inline text box is currently open, commits non-empty text and closes text edit focus; pressing `ESC` again (or any time no text box is active, or on any error) **immediately** destroys the overlay and releases all GPU/staging/window resources *(our call — matches both "Escape/click-outside to commit" for Text and guaranteed instant fail-safe exit)*.
  - Right-click outside the active selection (or right-click when no selection exists) immediately cancels and destroys the overlay.
  - **Device-Lost Recovery:** If `EndDraw()` or `Present()` returns `D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, or `DXGI_ERROR_DEVICE_RESET`, attempt a single clean `D2DContext` recreation from the still-valid CPU `CapturedFrame`; if recreation fails, immediately trigger fail-safe teardown so the overlay can never freeze.

### 6.5 Shared WYSIWYG Direct2D 1.1 Rendering & Export Pipeline
- **Single Source of Truth (`SceneRenderer`):**
  - Renders `sourceDesktopBitmap` + all committed `Annotation` items (plus optional active drag preview) onto any `ID2D1DeviceContext*`, clipped to `selectionRect`.
  - **Gaussian Blur Censor:** Direct2D effect chain on `sourceDesktopBitmap`: `CLSID_D2D1Crop` (padded by `ceil(3 * sigma)`, clamped to desktop bounds) → `CLSID_D2D1Border` (`D2D1_BORDER_EDGE_MODE_CLAMP`) → `CLSID_D2D1GaussianBlur` (`STANDARD_DEVIATION = sigma`, `BORDER_MODE = HARD`), drawn inside `PushAxisAlignedClip(censorRect)`.
  - **Mosaic Pixelate Censor:** Computes exact box-averaged block colors from the CPU `CapturedFrame` for `censorRect` into a tiny `ceil(w/block) × ceil(h/block)` `ID2D1Bitmap` (cached by annotation ID inside `SceneRenderer`'s per-session resource cache) and draws it over `censorRect` with `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR`. Identical on hardware and WARP devices, zero edge bleed, and automatically freed on session teardown.
- **Offscreen Export (Pixel-Identical WYSIWYG):**
  1. Create an offscreen `ID2D1Bitmap1` (`D2D1_BITMAP_OPTIONS_TARGET`, `B8G8R8A8_UNORM`, `PREMULTIPLIED`, 96 DPI) sized to the exact integer pixel dimensions of `selectionRect` on the **same `ID2D1DeviceContext`**.
  2. Set target to offscreen bitmap, apply `Matrix3x2F::Translation(-selLeft, -selTop)`, call `SceneRenderer::RenderScene(...)` (without UI chrome or text edit caret), then restore swap chain target.
  3. Copy to a `D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW` staging bitmap, `Map(READ)`, copy rows with opaque alpha (`0xFF`) into an `IWICBitmap` (via `IWICImagingFactory::CreateBitmapFromMemory`).
  4. **Copy (`Ctrl+C`):** Writes both `CF_DIBV5` (32-bit `BITMAPV5HEADER` `BI_BITFIELDS`, bottom-up, `LCS_sRGB`, alpha `0xFF`) and `CF_DIB` (32-bit `BITMAPINFOHEADER` `BI_RGB`, bottom-up) in a single `OpenClipboard`/`EmptyClipboard` block for universal paste compatibility across Discord, WhatsApp, Chrome/Edge, Word, and Paint. Closes overlay immediately and shows a brief tray balloon confirmation.
  5. **Save (`Ctrl+S`):** Opens `GetSaveFileNameW` defaulting to `SayCheese_YYYYMMDD_HHMMSS.png` (filters: PNG `*.png` default, JPEG `*.jpg;*.jpeg`; remembers last directory in-memory). Encodes via WIC (`GUID_ContainerFormatPng` or `GUID_ContainerFormatJpeg` quality `0.92`). Closes overlay immediately on successful save (keeps overlay open if user cancels the save dialog).

### 6.6 Automated Leak & Stress Verification
- Exposed both via `SayCheese.exe --self-test-leaks` and as a CTest target (`test_leak_stress`).
- Runs **2 warm-up cycles + 20 measured cycles** (22 total). Each cycle executes:
  1. `ScreenCapture::CaptureVirtualDesktop()` (DXGI with `BitBlt` fallback; if running headless without an interactive desktop/DXGI output, logs `DXGI_CAPTURE: SKIPPED (headless)` and falls back to a synthetic 1280×720 BGRA frame so overlay/D2D/export leak assertions still run).
  2. Ephemeral `OverlayWindow` + `D2DContext` creation (hardware with WARP fallback if no GPU adapter is available in CI).
  3. Populates representative annotations covering all 7 tools (including both Mosaic and GaussianBlur censors and DirectWrite text), renders live frames, and executes an offscreen WYSIWYG export render pass (without touching the system clipboard).
  4. Programmatically cancels and destroys the overlay + GPU context (same path as `ESC`), flushing the D3D11 immediate context and resetting all `ComPtr`s and Win32 handles.
- **Assertions (Cycle 2 baseline vs Cycle 22 final):**
  1. `GR_GDIOBJECTS` delta `== 0`.
  2. `GR_USEROBJECTS` delta `== 0`.
  3. `GetProcessHandleCount` delta `== 0`.
  4. `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` delta `< 2 MiB` (`2,097,152` bytes).
  5. In Debug builds (`_DEBUG`): invokes `IDXGIDebug1::ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)` (loaded dynamically from `dxgidebug.dll`) and verifies zero live application COM/DXGI objects via `IDXGIInfoQueue` (when available), plus `_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)` for zero CRT heap leaks.
- Emits a clear tabular summary of Cycle 2, Cycle 22, and Delta metrics to stdout.

### 6.7 GitHub Actions Release Pipeline
- `.github/workflows/release.yml` on `windows-latest`:
  - Triggers on pushes/PRs to `main` and on tag pushes matching `v*`.
  - Configures and builds `Release` (x64, `/MT`) with CMake + MSVC, and also builds/runs tests via `ctest --output-on-failure`.
  - On `v*` tags: computes `SHA256SUMS.txt` for `SayCheese.exe` and publishes a GitHub Release attaching `SayCheese.exe` and `SHA256SUMS.txt`.

---

## 7. Decisions Made vs. Left to the Builder

### Decisions Locked In
- Append-only annotations + Command Pattern Undo/Redo (user decision).
- Approach A: Fully ephemeral per-capture GPU/overlay pipeline (user decision).
- Proposed UX calls included above (open for user override before approval):
  - Branding `SayCheese.exe`, default save name `SayCheese_YYYYMMDD_HHMMSS.png`, in-memory last-save-directory tracking (no registry/disk config in v1.0).
  - Double-click selects active monitor; `Ctrl+A` selects full virtual screen; Copy/Save requires an active selection.
  - Contextual `Mosaic | Blur` pill appears next to toolbar when Censor tool is active.
  - Mouse wheel & presets adjust active tool width, Text font size, or Censor block/blur intensity.
  - `ESC` commits an active inline Text box first; a second `ESC` (or `ESC` in any non-text/error state) immediately tears down the overlay.
  - Tray *Hotkey Status* menu item displays the active shortcut and retries `PrintScreen` registration when clicked in fallback mode; double-clicking the tray icon triggers *Capture Now*.

### Left to the Builder
- Visual styling details (toolbar glass dark palette, corner radii, subtle shadows, crisp vector icon paths for the 10–13 toolbar buttons, hover/active highlight colors).
- Exact internal helper breakdown inside each `.cpp` translation unit and exact vector artwork geometry for `saycheese.ico`.
- Specific unit test fixtures and coverage edge cases beyond the required test suites.
