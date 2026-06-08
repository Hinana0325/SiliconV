# docker — Docker Configurations

Docker configurations for reproducible builds and CI.

## Files

| File | Description |
|------|-------------|
| `Dockerfile.kernel` | Build environment for compiling the Android kernel |

## Dockerfile.kernel

Provides a reproducible environment for building the Android Common Kernel with all required toolchains.

```bash
# Build the Docker image
docker build -f docker/Dockerfile.kernel -t siliconv-kernel-builder .

# Build the kernel inside Docker
docker run --rm -v $(pwd):/workspace siliconv-kernel-builder \
    ./scripts/build_kernel.sh android14-6.6
```

## Why Docker?

- **Reproducible** — same toolchain everywhere (CI, dev machines)
- **No host pollution** — cross-compilers and build deps stay in the container
- **CI-friendly** — GitHub Actions can use the same image
