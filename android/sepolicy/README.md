# android/sepolicy — SELinux Policy

SELinux security policy for running Android inside SiliconV.

## Overview

Android enforces mandatory access control via SELinux. SiliconV needs custom policy rules because:

- Virtual devices (virtio-*) don't match standard Android device labels
- The hypervisor interface needs special permissions
- Some physical HALs are replaced by shims

## Files

| File | Description |
|------|-------------|
| `siliconv.te` | SiliconV-specific SELinux type enforcement rules |

## Policy Principles

- **Minimal permissions** — grant only what's needed for virtual devices
- **AOSP compatible** — extend AOSP policy, don't replace it
- **Permissive during development** — switch to enforcing once stable

## Adding Policy Rules

```te
# Example: allow init to access virtio-blk device
type siliconv_block_device, dev_type;
allow init siliconv_block_device:blk_file { read write open };
```

## Reference

- AOSP SELinux documentation
- `system/sepolicy/` in AOSP source tree
