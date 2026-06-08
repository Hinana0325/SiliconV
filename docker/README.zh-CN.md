# docker — Docker 配置

用于可复现构建和 CI 的 Docker 配置。

## 文件

| 文件 | 描述 |
|------|------|
| `Dockerfile.kernel` | 编译 Android 内核的构建环境 |

## Dockerfile.kernel

提供可复现的环境来构建包含所有必需工具链的 Android Common Kernel。

```bash
# 构建 Docker 镜像
docker build -f docker/Dockerfile.kernel -t siliconv-kernel-builder .

# 在 Docker 中构建内核
docker run --rm -v $(pwd):/workspace siliconv-kernel-builder \
    ./scripts/build_kernel.sh android14-6.6
```

## 为什么用 Docker？

- **可复现** — 所有地方使用相同的工具链（CI、开发机）
- **不污染宿主** — 交叉编译器和构建依赖留在容器中
- **CI 友好** — GitHub Actions 可以使用相同的镜像
