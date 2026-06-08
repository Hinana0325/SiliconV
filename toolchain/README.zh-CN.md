# toolchain — 构建工具

用于构建、修补和打包 SiliconV 镜像的工具。

## 工具列表

| 工具 | 描述 | 状态 |
|------|------|------|
| [dtb/](dtb/) | 设备树 blob 操作工具 | 🔲 |
| [kernel_replace/](kernel_replace/) | 替换 boot.img 中的内核镜像 | 🔲 |
| [repack/](repack/) | 重新打包包含修改组件的 boot.img | 🔲 |
| [unpack/](unpack/) | 将 boot.img 解包为 kernel、ramdisk、DTB | 🔲 |
| [shim_generator/](shim_generator/) | 从头文件自动生成 HAL 垫片代码 | 🔲 |
| [vendor_patch/](vendor_patch/) | 将厂商特定补丁应用到 AOSP | 🔲 |

## 工作流

```
原始 boot.img
    ↓ unpack/
┌───────────────┐
│ kernel (Image) │
│ ramdisk.img    │
│ dtb（可选）    │
│ cmdline        │
└───────┬───────┘
    ↓ 修改
    ↓ repack/
修改后的 boot.img
```

## 用法

```bash
# 解包 boot.img
./toolchain/unpack/unpack boot.img -o extracted/

# 使用新内核重新打包
./toolchain/repack/repack extracted/ -k new_Image -o new_boot.img

# 替换现有 boot.img 中的内核
./toolchain/kernel_replace/replace boot.img new_Image
```
