# hypervisor/hvf — HVF 后端

macOS Hypervisor.framework 后端实现。

## 概述

HVF（Hypervisor.framework）是 Apple 的硬件虚拟化 API。它提供：

- 硬件辅助虚拟化的 VM 创建
- 支持 EL2 的 vCPU 执行
- 内存映射和 MMIO 退出处理

## 要求

- macOS 10.15+（Catalina 或更高版本）
- Apple Silicon（M1/M2/M3/M4）或带 VT-x 的 Intel
- Hypervisor.framework 权限（沙盒应用需要）

## API 用法

```objc
#import <Hypervisor/Hypervisor.h>

// 创建 VM
hv_vm_create(0);

// 映射客户内存
hv_vm_map(host_addr, guest_addr, size, HV_MEMORY_READ | HV_MEMORY_WRITE);

// 创建 vCPU
hv_vcpu_create(&vcpu, &vcpu_exit, NULL);

// 运行
while (1) {
    hv_vcpu_run(vcpu);
    hv_vcpu_get_exit_reason(vcpu, &exit_reason);
    // 处理退出...
}
```

## 文件

| 文件 | 描述 |
|------|------|
| `hvf.c` | HVF 后端实现（C 与 ObjC 互操作） |

## 限制

- 不支持嵌套虚拟化
- GICv3 虚拟化需要 EL2
- 某些 ARM 功能可能未完全虚拟化

## 参考

- Apple Hypervisor.framework 文档
- `Hypervisor/Hypervisor.h` 头文件
