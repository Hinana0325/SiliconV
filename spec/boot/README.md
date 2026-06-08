# spec/boot — Boot Specification

Defines the SiliconV boot flow and device tree schema.

## Documents

| Document | Description |
|----------|-------------|
| [boot-flow.md](boot-flow.md) | Boot sequence from power-on to kernel entry |
| [dtb-schema.md](dtb-schema.md) | Device tree blob format and required nodes |

## Key Points

- **Direct boot** — no bootloader (U-Boot, GRUB) required
- **Android boot.img compatible** — parse standard boot image format
- **DTB generated at runtime** — matches VM configuration
- **Frozen as of v0** — changes require spec version bump
