# frontend/cocoa — macOS 原生应用

使用 Cocoa（Objective-C）的 SiliconV 原生 macOS 应用。

## 功能

- 带 virtio-gpu 显示输出的 VM 窗口
- 串口控制台查看器
- 菜单栏 VM 生命周期控制（启动、停止、暂停）
- 拖放加载 kernel/rootfs
- 本地化（English、简体中文）

## 文件

| 文件 | 描述 |
|------|------|
| `main.m` | 应用入口点 |
| `AppDelegate.h` / `AppDelegate.m` | 主应用代理 |
| `SiliconV-Info.plist` | 应用元数据和配置 |
| `SiliconV.icns` | 应用图标 |
| `en.lproj/Localizable.strings` | 英文本地化 |
| `zh-Hans.lproj/Localizable.strings` | 简体中文本地化 |
| `icon.svg` | 源图标（矢量） |

## 构建

```bash
# 需要 Xcode 命令行工具
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiliconV
```

## 架构

```
AppDelegate
    ├── VM 控制器（启动/停止/暂停）
    ├── Metal/OpenGL 视图（显示输出）
    ├── 串口控制台窗口
    └── 设置面板
```
