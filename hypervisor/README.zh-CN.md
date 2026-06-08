# hypervisor — 硬件虚拟化

Hypervisor 抽象层和后端实现。SiliconV 在此与宿主的硬件虚拟化扩展交互。

## 架构

```
┌──────────────────────────────────┐
│     SiliconV VM 管理器            │
│         (core/vm/)               │
├──────────────────────────────────┤
│     Hypervisor 抽象层             │  ← abstraction/
│     (sv_hv 接口)                  │
├──────┬──────┬──────┬────────────┤
│ KVM  │ HVF  │ WHPX │  （未来）   │  ← 后端
│Linux │macOS │ Win  │           │
└──────┴──────┴──────┴───────────┘
```

## 后端

| 后端 | 平台 | 状态 | 描述 |
|------|------|------|------|
| [kvm/](kvm/) | Linux | ✅ | Linux KVM，通过 `/dev/kvm` |
| [hvf/](hvf/) | macOS | 🔄 | Apple Hypervisor.framework |
| [whpx/](whpx/) | Windows | 🔲 | Windows Hypervisor Platform |

## 抽象接口

所有后端实现 `abstraction/hv.h` 中定义的相同接口：

```c
typedef struct sv_hv_ops {
    int  (*vm_create)(sv_vm_t *vm);
    int  (*vcpu_create)(sv_vcpu_t *vcpu);
    int  (*vcpu_run)(sv_vcpu_t *vcpu);
    int  (*vcpu_get_reg)(sv_vcpu_t *vcpu, int reg, uint64_t *val);
    int  (*vcpu_set_reg)(sv_vcpu_t *vcpu, int reg, uint64_t val);
    int  (*mmio_read)(sv_vcpu_t *vcpu, uint64_t addr, uint64_t *val);
    int  (`mmio_write)(sv_vcpu_t *vcpu, uint64_t addr, uint64_t val);
    void (*vm_destroy)(sv_vm_t *vm);
} sv_hv_ops_t;
```

## 添加新后端

1. 在 `hypervisor/` 下创建目录（如 `xen/`）
2. 实现 `sv_hv_ops_t` 中的所有函数
3. 在 `abstraction/hv.c` 中注册后端
4. 在 `CMakeLists.txt` 中添加构建配置
