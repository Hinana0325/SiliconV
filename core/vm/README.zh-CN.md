# core/vm — VM 机器

主虚拟机管理器。创建 VM、加载内核、注册所有设备、运行 vCPU 事件循环。

## 文件

| 文件 | 描述 |
|------|------|
| `machine.c` / `machine.h` | VM 主生命周期 — 创建、配置、运行、销毁 |
| `bootimg.c` / `bootimg.h` | Android boot.img 解析器（v0–v4） |
| `boot_stub.S` | 汇编启动桩 — 初始客户入口点 |
| `boot_stub.ld` | 启动桩的链接脚本 |

## 启动流程

```
1. 解析 boot.img → 提取 kernel、ramdisk、cmdline、DTB
2. 创建 VM（hypervisor 后端）
3. 加载内核到入口地址
4. 根据 SiliconV 规范生成 DTB
5. 注册设备（GIC、UART、virtio-blk 等）
6. 加载 ramdisk 到客户内存
7. 设置初始 CPU 状态（PC → boot_stub）
8. 运行 vCPU
9. 处理 MMIO 退出 → 分发到设备
```

## Boot Image 支持

支持 Android boot image 版本 0 到 4：
- **v0/v1**：原始格式，包含 kernel + ramdisk
- **v2/v3**：新增 recovery DTB、启动签名
- **v4**：Vendor boot image 分区（GKI）

## API

```c
/* 解析 boot.img 文件 */
sv_bootimg_t *sv_bootimg_parse(const char *path);

/* 创建并运行 VM */
sv_vm_t *sv_vm_create(const sv_vm_config_t *config);
int sv_vm_run(sv_vm_t *vm);
void sv_vm_destroy(sv_vm_t *vm);
```
