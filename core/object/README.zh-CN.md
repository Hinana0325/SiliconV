# core/object — PSCI

PSCI（电源状态协调接口）实现，用于 CPU 生命周期管理。

## 概述

PSCI 是标准的 ARM 接口，供客户内核控制 CPU 电源状态。客户内核通过 **HVC**（Hypervisor Call）调用 PSCI 函数来：

- 启动辅助 CPU（多核）
- 挂起/恢复 CPU
- 关闭或重启系统

## 支持的函数

| 函数 | ID | 描述 |
|------|----|----|
| `PSCI_VERSION` | `0x84000000` | 返回 PSCI 版本（1.1） |
| `CPU_ON_64` | `0xC4000003` | 唤醒辅助 CPU |
| `CPU_OFF` | `0x84000002` | 关闭调用 CPU |
| `CPU_SUSPEND_64` | `0xC4000001` | 挂起调用 CPU |
| `AFFINITY_INFO_64` | `0xC4000004` | 查询 CPU 电源状态 |
| `SYSTEM_OFF` | `0x84000008` | 关闭 VM |
| `SYSTEM_RESET` | `0x84000009` | 重启 VM |
| `FEATURES` | `0x8400000A` | 查询支持的函数 |

## 文件

| 文件 | 描述 |
|------|------|
| `psci.c` / `psci.h` | PSCI 处理器实现 |

## 参考

- ARM PSCI 规范 v1.1
- Linux 内核：`drivers/firmware/psci.c`
