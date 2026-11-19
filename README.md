# SayCheese

A production-grade, ultra-fast, zero-dependency Windows 10/11 screenshot capture and annotation desktop application written in modern **C++20** (~1 MB standalone portable `.exe`), engineered specifically to eliminate OS/DWM freezes, input queue lockups, idle GPU footprint, and resource leaks.

## Why SayCheese?

- **Zero Input Lockups:** Never installs `WH_KEYBOARD_LL` or `WH_MOUSE_LL` hooks. Uses OS-managed `RegisterHotKey` (`PrintScreen`, with automatic `Ctrl+Shift+S` fallback + System Tray balloon if Windows 11 Snipping Tool owns `PrintScreen`).
- **Non-Blocking Multi-Monitor Capture:** Uses per-adapter DXGI Desktop Duplication (`IDXGIOutput5::DuplicateOutput1` / `IDXGIOutput1::DuplicateOutput`) with strict `<= 100 ms` frame timeouts, bounded retries, HDR (`R16G16B16A16_FLOAT` scRGB) and 10-bit (`R10G10B10A2_UNORM`) surface conversion, monitor rotation support, and automatic `BitBlt` fallback for RDP/basic adapters.
- **Zero Idle GPU Footprint:** Fully ephemeral per-capture GPU/overlay lifecycle. While sitting in the System Tray, SayCheese holds **0 MB VRAM** and **zero live D3D11/DXGI/Direct2D objects**.
- **Per-Monitor DPI v2 Accuracy:** Embeds a `PerMonitorV2` manifest, locks the capture canvas 1:1 to physical pixels across mixed-DPI multi-monitor layouts (including negative virtual coordinates), and scales floating Direct2D toolbars dynamically per monitor via `GetDpiForMonitor`.
- **WYSIWYG Export Guarantee:** Live preview and offscreen clipboard/file export share the exact same Direct2D 1.1 `SceneRenderer` (`ID2D1DeviceContext` -> offscreen `ID2D1Bitmap1` -> `IWICBitmap`), ensuring pixel-identical output for Pen, Highlighter opacity, DirectWrite Text, Mosaic Pixelate, and `CLSID_D2D1GaussianBlur`.
- **Zero External Dependencies:** Pure Win32 + DXGI + Direct2D 1.1 + DirectWrite + WIC, statically linked (`/MT`) with no VC++ Redistributable required.

---

## Architecture Overview

```text
System Tray (Idle: 0 GPU/COM objects)
   │  PrintScreen / Ctrl+Shift+S / Tray Menu
   ▼
ScreenCapture (capture/)
   ├─ Per-adapter DXGI Desktop Duplication (<=100ms timeout, HDR/10-bit, rotation)
   └─ Automatic per-output or global BitBlt fallback
   │  Releases all duplication interfaces immediately
   ▼
Ephemeral OverlayWindow (ui/ + render/ + tools/)
   ├─ D2DContext (D3D11 + Flip-Model SwapChain + ID2D1DeviceContext @ 96 DPI)
   ├─ SceneRenderer (shared WYSIWYG renderer: desktop + all 7 tools + Mosaic/GaussianBlur)
   ├─ UIRenderer (darkened backdrop, 8 handles, W×H badge, monitor-clamped toolbars)
   └─ ToolController + CommandStack (append-only Undo/Redo)
   │
   ├── Ctrl+C ──► RenderCropToWicBitmap ─► CF_DIBV5 + CF_DIB Clipboard ─► Instant Teardown
   ├── Ctrl+S ──► RenderCropToWicBitmap ─► WIC PNG / JPEG Save Dialog  ─► Instant Teardown
   └── ESC / Right-Click Outside ───────────────────────────────────────► Instant Teardown
```

---

## Annotation Tools & Keyboard Shortcuts

| Shortcut / Action | Behavior |
|---|---|
| `PrintScreen` *(or `Ctrl+Shift+S`)* | Trigger instant multi-monitor capture |
| Drag on overlay | Create selection region |
| Double-click monitor *(Select mode)* | Select the monitor under the cursor |
| `Ctrl+A` | Select the entire virtual desktop |
| Drag handles / selection body | Resize (8 handles) or move selection (hold `Space` to move while a tool is active) |
| Hold `Shift` *(Line / Arrow)* | Snap angle to 45° increments |
| Hold `Shift` *(Rectangle)* | Constrain to a square |
| Mouse Wheel | Adjust active stroke width, text font size, mosaic block size, or blur radius live |
| `Enter` *(Text tool)* | Insert newline in inline DirectWrite text box |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo annotation |
| `Ctrl+C` | Copy cropped WYSIWYG image to clipboard (`CF_DIBV5` + `CF_DIB`) and close |
| `Ctrl+S` | Save cropped WYSIWYG image as PNG (default) or JPEG via WIC |
| `ESC` | Commit active inline text box, or immediately cancel & release all resources |
| Right-click outside selection | Immediately cancel & release all resources |

---

## Building from Source

### Prerequisites
- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 or 2026 with **Desktop development with C++** (MSVC + Windows 10/11 SDK)
- CMake 3.20+

### Configure, Build & Test
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The standalone portable executable is produced at `build/Release/SayCheese.exe` (statically linked with `/MT`).

---

## Automated Leak & Stress Verification

SayCheese ships with a built-in 22-cycle stress verifier (2 warm-up cycles + 20 measured capture → overlay create → 7-tool render → offscreen WYSIWYG export → cancel/destroy cycles):

```powershell
.\build\Release\SayCheese.exe --self-test-leaks
```

It verifies between Cycle 2 (baseline) and Cycle 22 (final):
- `GR_GDIOBJECTS` delta `== 0`
- `GR_USEROBJECTS` delta `== 0`
- `GetProcessHandleCount` delta `== 0`
- `PrivateUsage` delta `< 2 MiB`
- In Debug builds: `IDXGIDebug1::ReportLiveObjects` confirms **0 live application COM/DXGI objects** and `_CrtSetDbgFlag` verifies **0 CRT heap leaks**.

---

## License

Released under the [MIT License](LICENSE).
