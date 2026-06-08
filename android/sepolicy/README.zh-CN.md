# android/sepolicy — SELinux 策略

在 SiliconV 中运行 Android 的 SELinux 安全策略。

## 概述

Android 通过 SELinux 强制执行强制访问控制。SiliconV 需要自定义策略规则，因为：

- 虚拟设备（virtio-*）不匹配标准 Android 设备标签
- hypervisor 接口需要特殊权限
- 一些物理 HAL 被垫片替代

## 文件

| 文件 | 描述 |
|------|------|
| `siliconv.te` | SiliconV 特定的 SELinux 类型强制规则 |

## 策略原则

- **最小权限** — 只授予虚拟设备所需的权限
- **AOSP 兼容** — 扩展 AOSP 策略，不替换
- **开发期间宽容** — 稳定后切换到强制模式

## 添加策略规则

```te
# 示例：允许 init 访问 virtio-blk 设备
type siliconv_block_device, dev_type;
allow init siliconv_block_device:blk_file { read write open };
```

## 参考

- AOSP SELinux 文档
- AOSP 源码树中的 `system/sepolicy/`
