# scripts — 构建与测试脚本

用于构建、测试和调试 SiliconV 的工具脚本。

## 脚本列表

| 脚本 | 描述 |
|------|------|
| `build_kernel.sh` | 为 SiliconV 构建 Android Common Kernel（Linux） |
| `build_kernel_macos.sh` | 在 macOS 上构建 Android Common Kernel（交叉编译） |
| `test_qemu.sh` | 使用 QEMU 测试 SiliconV 内核启动（验证） |

## build_kernel.sh

使用 SiliconV 配置构建 Android Common Kernel：

```bash
./scripts/build_kernel.sh android14-6.6

# 选项：
#   <分支>     要构建的内核分支（默认：android14-6.6）
#   --clean    清理构建（删除之前的构建）
#   --menuconfig 构建前打开内核 menuconfig
```

## build_kernel_macos.sh

在 macOS 上交叉编译 ARM64 内核：

```bash
./scripts/build_kernel_macos.sh android14-6.6

# 需要：
#   brew install aarch64-elf-gcc aarch64-elf-binutils
```

## test_qemu.sh

使用 QEMU 测试启动内核，无需运行完整的 SiliconV hypervisor 即可快速验证：

```bash
./scripts/test_qemu.sh

# 适用于：
#   - 验证内核是否正确启动
#   - 测试 DTB 兼容性
#   - 调试早期启动问题
```

## 添加脚本

将新脚本放在此目录并设置可执行权限：

```bash
chmod +x scripts/my_script.sh
```

在此 README 中记录脚本的用途和用法。
