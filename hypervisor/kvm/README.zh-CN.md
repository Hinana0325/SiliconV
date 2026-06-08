# hypervisor/kvm — KVM 后端

Linux KVM（内核虚拟机）后端实现。

## 概述

KVM 是 SiliconV 在 Linux 上的主要 hypervisor 后端。它使用 `/dev/kvm` 来：

- 创建 VM 和 vCPU
- 映射客户内存
- 在硬件辅助虚拟化中运行客户代码
- 处理 VM 退出（MMIO、超级调用等）

## 要求

- 带 KVM 支持的 Linux 内核（`CONFIG_KVM=y`）
- 带硬件虚拟化（EL2）的 ARM64 宿主
- `/dev/kvm` 可访问（通常需要 `kvm` 组或 root）

## VM 退出处理

```
vcpu_run() 返回
    │
    ├── KVM_EXIT_MMIO → 分发到设备处理程序
    ├── KVM_EXIT_HVC  → PSCI 处理器
    ├── KVM_EXIT_IRQ  → GICv3 注入
    ├── KVM_EXIT_IO   → 未使用（ARM64 使用 MMIO）
    └── KVM_EXIT_FAIL → 错误
```

## 文件

| 文件 | 描述 |
|------|------|
| `kvm.c` | KVM 后端实现 |

## 检查 KVM 可用性

```bash
# 检查 KVM 是否可用
ls -la /dev/kvm

# 检查 CPU 是否支持虚拟化
grep -E 'vmx|svm' /proc/cpuinfo  # x86
grep 'VHE\|NVHE' /proc/cpuinfo   # ARM64
```

## 参考

- KVM API 文档：`Documentation/virt/kvm/api.rst`
- Linux 内核：`arch/arm64/kvm/`
