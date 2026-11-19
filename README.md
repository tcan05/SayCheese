# SayCheese

Ultra-fast, zero-dependency Windows 10/11 screenshot capture and annotation desktop application.


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


## License

Released under the [MIT License](LICENSE).
