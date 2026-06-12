# frontend/cocoa — macOS Native App

Native macOS application for SiliconV using Cocoa (Objective-C) with AppKit.

## Features

- Three-column layout: Device Tree sidebar | VM Display | Console
- SF Symbols toolbar: Start/Stop/Pause, Kernel/Rootfs pickers, Settings
- Branded VM display with pulse animation and CPU core indicators
- Tabbed serial console viewer (Kernel/Android/Logcat)
- Metal/CAMetalLayer placeholder for future virtio-gpu rendering
- Drag-and-drop kernel/rootfs loading
- Dark mode support (native)
- Keyboard input forwarding to guest UART
- Thread-safe VM lifecycle via dispatch_queue
- Localized (English, 简体中文)

## Files

| File | Description |
|------|-------------|
| `main.m` | Application entry point |
| `AppDelegate.h/m` | Thin coordinator (28 lines) |
| `MainWindowController.h/m` | Window management, toolbar, layout, device sidebar |
| `VMViewController.h/m` | VM display view + keyboard capture |
| `ConsoleViewController.h/m` | Tabbed serial console with ANSI rendering |
| `VMEngine.h/m` | Thread-safe wrapper around machine.c API |
| `SiliconV-Info.plist` | App metadata and configuration |
| `SiliconV.icns` | Application icon |
| `en.lproj/Localizable.strings` | English localization (85+ strings) |
| `zh-Hans.lproj/Localizable.strings` | Simplified Chinese localization |

## Building

```bash
# Requires macOS 11+ with Xcode command line tools
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiliconV
open build/SiliconV.app
```

## Architecture (MVC + Coordinator)

```
AppDelegate (28 lines)
    └── MainWindowController (840 lines)
         ├── VMViewController  — VM display + keyboard
         ├── ConsoleViewController — Kernel/Android/Logcat tabs
         └── VMEngine          — dispatch_queue VM execution
              └── sv_machine_t (C core)
```

## Requirements

- macOS 11.0+ (Big Sur or later)
- Apple Silicon recommended (for HVF backend)
- x86_64 Macs run in demo mode (no hypervisor backend available)
