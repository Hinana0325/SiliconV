# hypervisor/abstraction — Hypervisor 抽象层

定义所有 hypervisor 后端必须实现的通用接口。

## 目的

SiliconV 支持多个 hypervisor 后端（KVM、HVF、WHPX）。抽象层确保 VM 管理器（`core/vm/`）不依赖任何特定后端。

## 文件

| 文件 | 描述 |
|------|------|
| `hv.c` / `hv.h` | 后端接口定义、工厂函数、通用工具 |

## 接口

```c
/* 选择并初始化后端 */
sv_hv_type_t sv_hv_detect(void);           // 自动检测最佳后端
sv_hv_ops_t *sv_hv_get_ops(sv_hv_type_t);  // 获取后端操作

/* VM 生命周期 */
int  sv_hv_vm_create(sv_vm_t *vm);
void sv_hv_vm_destroy(sv_vm_t *vm);

/* vCPU 生命周期 */
int sv_hv_vcpu_create(sv_vcpu_t *vcpu);
int sv_hv_vcpu_run(sv_vcpu_t *vcpu);
int sv_hv_vcpu_get_reg(sv_vcpu_t *vcpu, int reg, uint64_t *val);
int sv_hv_vcpu_set_reg(sv_vcpu_t *vcpu, int reg, uint64_t val);

/* 内存映射 */
int sv_hv_mem_map(sv_vm_t *vm, uint64_t guest_addr, void *host_addr,
                  size_t size, int prot);
```

## 后端选择

启动时，SiliconV 自动检测可用后端：

1. 尝试 KVM（`/dev/kvm`）— Linux
2. 尝试 HVF — macOS
3. 尝试 WHPX — Windows
4. 如果都不可用，报错退出
