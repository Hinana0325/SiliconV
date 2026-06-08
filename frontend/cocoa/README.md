# frontend/cocoa — macOS Native App

Native macOS application for SiliconV using Cocoa (Objective-C).

## Features

- VM window with display output from virtio-gpu
- Serial console viewer
- Menu bar for VM lifecycle (Start, Stop, Pause)
- Drag-and-drop kernel/rootfs loading
- Localized (English, 简体中文)

## Files

| File | Description |
|------|-------------|
| `main.m` | Application entry point |
| `AppDelegate.h` / `AppDelegate.m` | Main application delegate |
| `SiliconV-Info.plist` | App metadata and configuration |
| `SiliconV.icns` | Application icon |
| `en.lproj/Localizable.strings` | English localization |
| `zh-Hans.lproj/Localizable.strings` | Simplified Chinese localization |
| `icon.svg` | Source icon (vector) |

## Building

```bash
# Requires Xcode command line tools
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target siliconv-cocoa
```

## Architecture

```
AppDelegate
    ├── VM Controller (start/stop/pause)
    ├── Metal/OpenGL View (display output)
    ├── Serial Console Window
    └── Settings Panel
```
