# spec/boot — 启动规范

定义 SiliconV 的启动流程和设备树模式。

## 文档

| 文档 | 描述 |
|------|------|
| [boot-flow.md](boot-flow.md) | 从上电到内核入口的启动序列 |
| [dtb-schema.md](dtb-schema.md) | 设备树 blob 格式和必需节点 |

## 关键要点

- **直启模式** — 无需引导加载程序（U-Boot、GRUB）
- **兼容 Android boot.img** — 解析标准启动镜像格式
- **运行时生成 DTB** — 匹配 VM 配置
- **v0 已冻结** — 变更需要规范版本升级
