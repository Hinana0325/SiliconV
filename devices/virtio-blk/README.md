# devices/virtio-blk — Virtio Block Device

Virtio block device — provides the guest with a virtual disk (root filesystem).

## Overview

The guest sees a standard virtio-blk device as its root filesystem. The host backs it with a raw disk image file. All block I/O goes through virtqueues.

## Virtqueue Layout

| Queue | Description |
|-------|-------------|
| 0 | Request queue (read/write/flush) |

## Request Protocol

```
Guest → Device:
  struct virtio_blk_req {
      uint32_t type;    // VIRTIO_BLK_T_IN (read) / _OUT (write) / _FLUSH
      uint32_t reserved;
      uint64_t sector;  // LBA sector number
      // data payload
      uint8_t status;   // result (written back by device)
  };
```

## Supported Features

| Feature | Bit | Description |
|---------|-----|-------------|
| `BLK_F_SIZE_MAX` | 1 | Max segment size |
| `BLK_F_SEG_MAX` | 2 | Max segments |
| `BLK_F_GEOMETRY` | 4 | Disk geometry (cylinders/heads/sectors) |
| `BLK_F_RO` | 5 | Read-only disk |
| `BLK_F_BLK_SIZE` | 6 | Logical block size |
| `BLK_F_FLUSH` | 9 | Cache flush command |
| `BLK_F_TOPOLOGY` | 10 | Physical topology info |

## Files

| File | Description |
|------|-------------|
| `virtio_blk.c` / `virtio_blk.h` | Block device implementation |

## Usage

```bash
# Create a disk image
dd if=/dev/zero of=rootfs.img bs=1M count=4096

# Format it
mkfs.ext4 rootfs.img

# Run with the disk
./siliconv -k Image -r rootfs.img
```

## Reference

- Virtio 1.2 Specification, Section 5.2 (Block Device)
