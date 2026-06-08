# runtime — Runtime Configuration

VM runtime configuration, profiles, and session management.

## Modules

| Module | Description | Status |
|--------|-------------|--------|
| [config/](config/) | Global configuration (default VM settings) | 🔲 |
| [launcher/](launcher/) | VM launcher logic (pre-flight checks, device setup) | 🔲 |
| [profiles/](profiles/) | VM profiles (phone, tablet, automotive, etc.) | 🔲 |
| [session/](session/) | Session state management (save/restore) | 🔲 |

## VM Profiles

Profiles define hardware configurations for different device types:

```json
{
  "name": "phone",
  "cpus": 4,
  "memory": "4G",
  "display": { "width": 1080, "height": 2340, "dpi": 440 },
  "storage": "64G",
  "gpu": true,
  "net": true,
  "sensors": ["accelerometer", "gyroscope", "gps"]
}
```

## Session Management

Future support for:
- **Save/Restore** — snapshot VM state to disk
- **Fast boot** — resume from snapshot instead of cold boot
- **Multi-instance** — run multiple VMs simultaneously
