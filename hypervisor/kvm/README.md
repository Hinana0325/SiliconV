# hypervisor/kvm — KVM Backend

Linux KVM (Kernel-based Virtual Machine) backend implementation.

## Overview

KVM is the primary hypervisor backend for SiliconV on Linux. It uses `/dev/kvm` to:

- Create VMs and vCPUs
- Map guest memory
- Run guest code in hardware-assisted virtualization
- Handle VM exits (MMIO, hypercalls, etc.)

## Requirements

- Linux kernel with KVM support (`CONFIG_KVM=y`)
- ARM64 host with hardware virtualization (EL2)
- `/dev/kvm` accessible (typically needs `kvm` group or root)

## VM Exit Handling

```
vcpu_run() returns
    │
    ├── KVM_EXIT_MMIO → dispatch to device handlers
    ├── KVM_EXIT_HVC  → PSCI handler
    ├── KVM_EXIT_IRQ  → GICv3 injection
    ├── KVM_EXIT_IO   → not used (ARM64 uses MMIO)
    └── KVM_EXIT_FAIL → error
```

## Files

| File | Description |
|------|-------------|
| `kvm.c` | KVM backend implementation |

## Checking KVM Availability

```bash
# Check if KVM is available
ls -la /dev/kvm

# Check if your CPU supports virtualization
grep -E 'vmx|svm' /proc/cpuinfo  # x86
grep 'VHE\|NVHE' /proc/cpuinfo   # ARM64
```

## Reference

- KVM API documentation: `Documentation/virt/kvm/api.rst`
- Linux kernel: `arch/arm64/kvm/`
