# runtime — 运行时配置

VM 运行时配置、配置文件和会话管理。

## 模块

| 模块 | 描述 | 状态 |
|------|------|------|
| [config/](config/) | 全局配置（默认 VM 设置） | 🔲 |
| [launcher/](launcher/) | VM 启动器逻辑（预检、设备设置） | 🔲 |
| [profiles/](profiles/) | VM 配置文件（手机、平板、车载等） | 🔲 |
| [session/](session/) | 会话状态管理（保存/恢复） | 🔲 |

## VM 配置文件

配置文件定义不同设备类型的硬件配置：

```json
{
  "name": "phone",
  "cpus": 4,
  "memory": "4G",
  "display": { "width": 1080, "height": 2340, "dpi": 440 },
  "storage": "64G",
  "gpu": true,
  "net": true,
  "sensors": ["accelerometer", "gyroscope", "gps"]
}
```

## 会话管理

未来支持：
- **保存/恢复** — 将 VM 状态快照到磁盘
- **快速启动** — 从快照恢复而非冷启动
- **多实例** — 同时运行多个 VM
