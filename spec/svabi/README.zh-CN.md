# spec/svabi — SiliconV ABI

SiliconV 应用二进制接口（SVABI）——hypervisor 与客户之间的契约。

## 文档

| 文档 | 描述 |
|------|------|
| [svabi-v0.md](svabi-v0.md) | ABI v0 规范（已冻结） |

## SVABI 定义的内容

- **寄存器约定** — vCPU 寄存器使用
- **调用约定** — HVC/SMC 调用接口
- **内存布局** — 客户物理地址映射
- **异常处理** — 客户如何处理陷阱
- **PSCI 接口** — CPU 生命周期协议

## 稳定性

SVABI v0 已**冻结**。变更需要新版本，且必须尽可能保持向后兼容。
