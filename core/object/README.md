# core/object — PSCI

PSCI (Power State Coordination Interface) implementation for CPU lifecycle management.

## Overview

PSCI is the standard ARM interface for the guest kernel to control CPU power states. The guest kernel calls PSCI functions via **HVC** (Hypervisor Call) to:

- Boot secondary CPUs (multi-core)
- Suspend/resume CPUs
- Shut down or reset the system

## Supported Functions

| Function | ID | Description |
|----------|----|-------------|
| `PSCI_VERSION` | `0x84000000` | Return PSCI version (1.1) |
| `CPU_ON_64` | `0xC4000003` | Wake up a secondary CPU |
| `CPU_OFF` | `0x84000002` | Power off calling CPU |
| `CPU_SUSPEND_64` | `0xC4000001` | Suspend calling CPU |
| `AFFINITY_INFO_64` | `0xC4000004` | Query CPU power state |
| `SYSTEM_OFF` | `0x84000008` | Shut down the VM |
| `SYSTEM_RESET` | `0x84000009` | Reboot the VM |
| `FEATURES` | `0x8400000A` | Query supported functions |

## Files

| File | Description |
|------|-------------|
| `psci.c` / `psci.h` | PSCI handler implementation |

## Reference

- ARM PSCI Specification v1.1
- Linux kernel: `drivers/firmware/psci.c`
