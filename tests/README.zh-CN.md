# tests — 测试套件

SiliconV 组件的自动化测试。

## 测试分类

| 分类 | 描述 | 状态 |
|------|------|------|
| [unit/](unit/) | 单个组件的单元测试 | 🔲 |
| [integration/](integration/) | 集成测试（完整 VM 启动） | 🔲 |
| [performance/](performance/) | 性能基准测试 | 🔲 |
| [android/](android/) | Android 特定测试（启动、图形、IPC） | 🔲 |

## 运行测试

```bash
# 所有测试
cmake --build build --target test

# 仅单元测试
ctest --test-dir build -L unit

# 集成测试（需要 ARM64 或 QEMU）
ctest --test-dir build -L integration
```

## 测试矩阵

| 测试 | 类型 | 描述 |
|------|------|------|
| `test_gic_basic` | 单元 | GICv3 寄存器读/写 |
| `test_uart_output` | 单元 | PL011 字符输出 |
| `test_virtio_negotiation` | 单元 | Virtio 功能协商 |
| `test_bootimg_parse` | 单元 | Boot image v0-v4 解析 |
| `test_full_boot` | 集成 | 内核启动到 shell |
| `test_android_init` | 集成 | Android init 完成 |
| `test_gpu_render` | 集成 | VirGL 渲染一帧 |
| `test_boot_time` | 性能 | 首帧时间 |
| `test_block_io` | 性能 | 磁盘 I/O 吞吐量 |

## 添加测试

1. 将测试文件放在相应的分类目录
2. 在 `CMakeLists.txt` 中用 `add_test()` 注册
3. 添加标签用于过滤（`unit`、`integration`、`performance`）
