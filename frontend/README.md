# frontend — User Interfaces

Frontend applications for controlling SiliconV. Multiple UI options for different platforms.

## Frontends

| Frontend | Platform | Status | Description |
|----------|----------|--------|-------------|
| [cli/](cli/) | All | ✅ | Command-line interface (primary) |
| [cocoa/](cocoa/) | macOS | 🔄 | Native macOS app (Cocoa/ObjC) |
| [qt/](qt/) | All | 🔲 | Cross-platform Qt GUI |
| [web/](web/) | All | 🔲 | Web-based control panel |

## Architecture

```
┌──────────────────────────────────┐
│         Frontend Layer            │
│  ┌──────┬───────┬─────┬──────┐  │
│  │ CLI  │ Cocoa │ Qt  │ Web  │  │
│  └──┬───┴───┬───┴──┬──┴──┬───┘  │
├─────┴───────┴──────┴─────┴──────┤
│         SiliconV Core            │
│  (VM manager, devices, GPU)      │
└──────────────────────────────────┘
```

## CLI Frontend

The primary interface. Launch SiliconV from the command line:

```bash
./build/sv-cli -k Image -r rootfs.img --cpus 4 --memory 4096
```

## Cocoa Frontend (macOS)

Native macOS application with:
- VM window with display output
- Menu bar controls
- Serial console viewer
- VM lifecycle management

## Adding a New Frontend

1. Create a directory under `frontend/`
2. Link against `libsiliconv` (the core library)
3. Implement VM lifecycle callbacks (start, stop, pause)
4. Route display output from virtio-gpu to your UI
5. Add build target in `CMakeLists.txt`
