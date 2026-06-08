# hypervisor/hvf — HVF Backend

macOS Hypervisor.framework backend implementation.

## Overview

HVF (Hypervisor.framework) is Apple's hardware virtualization API. It provides:

- VM creation with hardware-assisted virtualization
- vCPU execution with EL2 support
- Memory mapping and MMIO exit handling

## Requirements

- macOS 10.15+ (Catalina or later)
- Apple Silicon (M1/M2/M3/M4) or Intel with VT-x
- Hypervisor.framework entitlement (for sandboxed apps)

## API Usage

```objc
#import <Hypervisor/Hypervisor.h>

// Create VM
hv_vm_create(0);

// Map guest memory
hv_vm_map(host_addr, guest_addr, size, HV_MEMORY_READ | HV_MEMORY_WRITE);

// Create vCPU
hv_vcpu_create(&vcpu, &vcpu_exit, NULL);

// Run
while (1) {
    hv_vcpu_run(vcpu);
    hv_vcpu_get_exit_reason(vcpu, &exit_reason);
    // handle exit...
}
```

## Files

| File | Description |
|------|-------------|
| `hvf.c` | HVF backend implementation (C with ObjC interop) |

## Limitations

- No nested virtualization
- EL2 required for GICv3 virtualization
- Some ARM features may not be fully virtualized

## Reference

- Apple Hypervisor.framework documentation
- `Hypervisor/Hypervisor.h` header
