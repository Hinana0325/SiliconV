# spec/svabi — SiliconV ABI

The SiliconV Application Binary Interface (SVABI) — the contract between hypervisor and guest.

## Documents

| Document | Description |
|----------|-------------|
| [svabi-v0.md](svabi-v0.md) | ABI v0 specification (frozen) |

## What SVABI Defines

- **Register conventions** — vCPU register usage
- **Calling conventions** — HVC/SMC call interface
- **Memory layout** — guest physical address map
- **Exception handling** — how the guest handles traps
- **PSCI interface** — CPU lifecycle protocol

## Stability

SVABI v0 is **frozen**. Changes require a new version and must be backward-compatible where possible.
