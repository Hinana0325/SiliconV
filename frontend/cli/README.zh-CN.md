# frontend/cli — 命令行界面

SiliconV 的主要前端。简洁、可脚本化，适用于所有平台。

## 用法

```bash
./build/sv-cli [选项]

选项：
  -k, --kernel 路径     内核镜像或 Android boot.img（必需）
  -d, --dtb 路径        设备树 blob（可选；省略时自动生成）
  -r, --rootfs 路径     virtio-blk 根文件系统镜像
  -c, --cmdline 字符串  内核命令行
  -m, --memory 大小     客户 RAM，单位 MB（默认：4096，最小：64）
  -n, --cpus 数量       vCPU 数量（默认：4，最大：8）
      --dry-run         只加载并校验配置，不启动 vCPU
  -h, --help            显示帮助
```

## 示例

```bash
# 不启动 vCPU，只校验内核/rootfs 配置
./build/sv-cli --dry-run -k Image -r rootfs.img -m 1024 -n 2

# ARM64 宿主上的基本启动
./build/sv-cli -k Image -r rootfs.img

# 自定义内核命令行
./build/sv-cli -k Image -r rootfs.img \
  -c "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw"
```

在 x86_64 或其他没有 SiliconV Hypervisor 后端的宿主上，推荐用 `--dry-run` 做冒烟测试；它仍会验证参数解析、镜像加载、DTB 生成和设备装配。

## 文件

| 文件 | 描述 |
|------|------|
| `main.c` | CLI 入口点、参数解析、VM 启动 |
