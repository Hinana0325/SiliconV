# tests — 测试套件

SiliconV 组件的自动化测试。

## 测试类别

| 类别 | 描述 | 状态 |
|------|------|------|
| [unit/](unit/) | 单个组件的单元测试 | ✅ |
| [integration/](integration/) | 集成测试（完整 VM 启动） | 🔲 |
| [performance/](performance/) | 性能基准 | 🔲 |
| [android/](android/) | Android 专项测试（启动、图形、IPC） | 🔲 |

## 运行测试

```bash
# 显式启用测试
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSV_BUILD_TESTS=ON

# 构建并运行全部已注册测试
cmake --build build
ctest --test-dir build --output-on-failure

# 仅运行单元测试
ctest --test-dir build -L unit

# 集成测试（需要 ARM64 或 QEMU）
ctest --test-dir build -L integration
```

## 测试矩阵

| 测试 | 类型 | 描述 |
|------|------|------|
| `test_dtb` | 单元 | 校验生成的 FDT 头和 SiliconV 节点 |
| `test_uart` | 单元 | PL011 TX 回调和 RX FIFO 行为 |
| `test_virtio_mmio` | 单元 | Virtio-MMIO 身份寄存器、avail ring 布局和中断确认 |
| `test_full_boot` | 集成 | 内核启动到 shell |
| `test_android_init` | 集成 | Android init 完成 |
| `test_gpu_render` | 集成 | VirGL 渲染一帧 |
| `test_boot_time` | 性能 | 首帧耗时 |
| `test_block_io` | 性能 | 磁盘 I/O 吞吐 |

## 添加测试

1. 将测试文件放入对应类别目录
2. 在 `CMakeLists.txt` 中用 `add_test()` 注册
3. 添加标签以便筛选（`unit`、`integration`、`performance`）
