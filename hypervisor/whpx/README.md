# hypervisor/whpx — Windows Hypervisor Platform Backend (placeholder)

**Status**: Not yet implemented.

This directory will contain the WHPX (Windows Hypervisor Platform) backend
for SiliconV on Windows x64 hosts.

## Scope

- Uses `whpx.h` / `WinHvPlatform.h` API
- Creates and manages a Windows Hypervisor VM partition
- Provides vCPU execution via `WHvRunVpLoop()`
- Supports GICv3 and ARM64 virtualization (nested or emulated)

## Requirements

- Windows 10 20H1+ or Windows 11
- x64 processor with SLAT (Second Level Address Translation)
- Windows Hyper-V Platform feature enabled

## Implementation Plan

| Step | Description |
|------|-------------|
| 1 | VM partition creation and configuration |
| 2 | Memory management (Guest RAM mapping) |
| 3 | vCPU creation and register state initialization |
| 4 | MMIO exit handling and device dispatch |
| 5 | GICv3 virtualization |
| 6 | Integration with Qt6 GUI frontend |

## References

- [Windows Hypervisor Platform API](https://learn.microsoft.com/en-us/virtualization/api/)
- [WHvCreatePartition](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvcreatepartition)
