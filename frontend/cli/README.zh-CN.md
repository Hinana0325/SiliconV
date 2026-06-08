# frontend/cli — 命令行界面

SiliconV 的主要前端。简洁、可脚本化，适用于所有平台。

## 用法

```bash
./siliconv [选项]

选项：
  -k, --kernel <路径>     内核镜像路径（必需）
  -r, --rootfs <路径>     根文件系统镜像（必需）
  -c, --cpus <数量>       vCPU 数量（默认：4）
  -m, --memory <大小>     客户 RAM（默认：4G）
  -s, --serial <路径>     串口输出到文件（默认：stdout）
  -d, --disk <路径>       额外的 virtio-blk 磁盘
  -g, --gpu               启用 virtio-gpu
  -n, --net               启用 virtio-net
  -v, --verbose           详细日志
  -h, --help              显示帮助
```

## 示例

```bash
# 基本启动
./siliconv -k Image -r rootfs.img

# 自定义配置
./siliconv -k Image -r rootfs.img -c 8 -m 8G -g -n

# 串口输出到文件
./siliconv -k Image -r rootfs.img -s serial.log
```

## 文件

| 文件 | 描述 |
|------|------|
| `main.c` | CLI 入口点、参数解析、VM 启动 |

## 键盘快捷键（运行时）

| 按键 | 操作 |
|------|------|
| `Ctrl-A X` | 退出 |
| `Ctrl-A C` | 串口控制台监控 |
