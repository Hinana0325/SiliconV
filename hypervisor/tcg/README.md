# SiliconV TCG Backend (Binary Translation Layer)

## 概述

TCG Backend 是 SiliconV 的 **纯软件 ARM64→x86_64 转译层**，使 SiliconV 能在没有 ARM64 KVM/HVF 的 x86_64 宿主机上运行。

```
┌─────────────────────────────────────────────┐
│             SiliconV Machine                │
│  (device models, MMIO dispatch, DTB, etc.) │
├─────────────────────────────────────────────┤
│          sv_hv_ops_t interface              │
├──────────┬──────────┬──────────┬───────────┤
│   KVM    │   HVF    │   WHPX   │  **TCG**  │  ← 新增
│ (ARM64)  │ (ARM64)  │ (ARM64)  │ (x86_64)  │
└──────────┴──────────┴──────────┴───────────┘
```

## 设计目标

1. **透明替换**: 实现 `sv_hv_ops_t` 接口，对上层 machine.c 完全透明
2. **轻量优先**: 不引入 QEMU 等重型依赖，纯 C 实现
3. **渐进优化**: 先实现可用的解释器，再逐步加入 JIT 热点优化
4. **指令覆盖**: 覆盖 ARM64 内核所需的指令集（A64 + 系统寄存器）

## 架构

```
hypervisor/tcg/
├── tcg.h           # 公共接口 + ARM64 结构定义
├── tcg.c           # 后端入口 (sv_hv_ops_t 实现)
├── tcg_cpu.h       # vCPU 状态 (寄存器、PC、PSTATE)
├── tcg_cpu.c       # vCPU 生命周期
├── tcg_decode.h    # 指令解码器
├── tcg_decode.c    # ARM64 → 中间表示
├── tcg_translate.h # 转译器
├── tcg_translate.c # IR → x86_64 机器码
├── tcg_exec.h      # 执行循环
├── tcg_exec.c      # 主循环 + MMIO 陷出
├── tcg_mmu.h       # 访存模拟
├── tcg_mmu.c       # 地址翻译 + MMIO 检测
├── CMakeLists.txt
└── README.md
```

## 执行模型

### 三级执行策略

```
            ┌─────────────┐
            │ ARM64 指令流 │
            └──────┬──────┘
                   ▼
         ┌─────────────────┐
         │   解码 (Decode)  │
         │  ARM64 → IR      │
         └────────┬────────┘
                  ▼
         ┌─────────────────┐     cold path
         │  解释器执行       │──────────────
         │  (switch-dispatch)│              │
         └────────┬────────┘              ▼
                  │ hot            ┌──────────────┐
                  ▼                │  JIT 编译     │
         ┌─────────────────┐      │  IR → x86_64  │
         │  计数/回跳检测    │      └──────┬───────┘
         │  (hotness tracking)│            │
         └─────────────────┘              ▼
                                 ┌──────────────┐
                                 │ x86_64 原生执行│
                                 │ (翻译缓存)     │
                                 └──────────────┘
```

### vCPU 状态

```c
typedef struct {
    uint64_t x[31];       // 通用寄存器 x0-x30
    uint64_t pc;          // 程序计数器
    uint64_t sp_el0;      // EL0 栈指针
    uint64_t sp_el1;      // EL1 栈指针
    uint64_t elr_el1;     // 异常返回地址
    uint64_t spsr_el1;    // 保存的程序状态
    uint64_t ttbr0_el1;   // 页表基址 (EL1)
    uint64_t ttbr1_el1;   // 页表基址 (EL1 kernel)
    uint64_t tcr_el1;     // 翻译控制
    uint64_t mair_el1;    // 内存属性
    uint64_t sctlr_el1;   // 系统控制 (MMU, cache)
    uint64_t vbar_el1;    // 向量基址
    uint64_t far_el1;     // 故障地址
    uint64_t esr_el1;     // 异常综合寄存器
    uint32_t pstate;      // 处理器状态 (NZCV + DAIF + EL)
    
    // 虚拟设备
    uint64_t cntfrq_el0;  // 计数器频率
    uint64_t cntvct_el0;  // 虚拟计数器
    
    // 性能统计
    uint64_t insn_count;
    uint64_t exit_count;
} tcg_vcpu_t;
```

### MMIO 陷出

ARM64 内核通过 MMIO 访问外设。转译层检测 MMIO 地址并陷出到 machine.c：

```
Load/Store 指令
     │
     ▼
tcg_mmu_translate(addr, is_write)
     │
     ├── RAM 范围 → 直接访存
     │
     └── MMIO 范围 → 陷出
              │
              ▼
         sv_vcpu_exit_t {
             .type = SV_EXIT_MMIO_READ/WRITE
             .mmio_addr = addr
             .mmio_data = value
         }
              │
              ▼
         machine_mmio_read/write()
              │
              ▼
         返回结果，继续执行
```

## 指令覆盖范围

### Phase 1: 最小可用 (内核启动到 busybox)

| 类别 | 指令 | 说明 |
|------|------|------|
| 数据移动 | mov, movz, movk, adr, adrp | 立即数/地址加载 |
| 加载存储 | ldr, str, ldp, stp (x-reg) | 通用访存 |
| 算术 | add, sub, cmp, and, orr, eor | 基本运算 |
| 分支 | b, bl, br, blr, ret, cbz, cbnz | 控制流 |
| 移位 | lsl, lsr, asr | 位移 |
| 系统 | mrs, msr (关键系统寄存器) | 系统寄存器访问 |
| 异常 | svc, eret | 系统调用/异常返回 |

### Phase 2: 完整内核 (SMP + 中断)

- 原子操作: ldaxr, stlxr, dmb, dsb, isb
- 完整系统寄存器: TTBR, TCR, MAIR, SCTLR
- 页表遍历: MMU 模拟
- GIC 虚拟化: ICC 系统寄存器

### Phase 3: Android 用户态 (EL0 执行)

- NEON/SIMD 指令 (可先陷出再模拟)
- 浮点指令
- 完整 EL0/EL1 切换

## 与 KVM 后端的差异

| 特性 | KVM Backend | TCG Backend |
|------|------------|-------------|
| 宿主机架构 | ARM64 only | x86_64 / ARM64 (通用) |
| vCPU 执行 | CPU 硬件虚拟化 | 软件解释/JIT |
| 性能 | 接近原生 | 10-100x 慢 |
| MMIO 处理 | KVM_EXIT_MMIO | tcg_mmu 检测 |
| 中断注入 | KVM_IRQ_LINE | 软件模拟 GIC |
| 页表 | 硬件 S2 页表 | 软件 MMU 遍历 |
| 启动时间 | 即时 | 即时 |

## 构建集成

```cmake
# hypervisor/tcg/CMakeLists.txt
add_library(sv_tcg STATIC
    tcg.c tcg_cpu.c tcg_decode.c
    tcg_translate.c tcg_exec.c tcg_mmu.c
)
target_link_libraries(sv_tcg PRIVATE sv_hv)
```

上层 CMakeLists.txt 中条件编译：
```cmake
if(NOT KVM_FOUND AND NOT HVF_FOUND)
    message(STATUS "No hw virt — building TCG backend")
    add_subdirectory(hypervisor/tcg)
    target_link_libraries(sv-cli PRIVATE sv_tcg)
endif()
```

## 参考

- ARM Architecture Reference Manual (Armv8-A)
- QEMU TCG (target/arm/translate-a64.c)
- Unicorn Engine (cpu emulation library)
