# SiliconV Specification v0

> Phase 0: Specification Freeze

## Structure

```
spec/
├── svabi/
│   └── svabi-v0.md          # SiliconV ABI — hypervisor ↔ guest contract
├── memory/
│   └── mmio-map.md           # MMIO address layout
├── irq/
│   └── irq-map.md            # Interrupt number allocation
├── devices/
│   └── virtio-matrix.md      # Virtio device requirements
├── boot/
│   ├── boot-flow.md          # Boot sequence definition
│   └── dtb-schema.md         # Device tree schema
└── gpu/                      # (future: GPU spec)
```

## Status

| Document | Status | Last Updated |
|----------|--------|-------------|
| SVABI v0 | Draft | 2026-05-29 |
| MMIO Layout | Draft | 2026-05-29 |
| IRQ Layout | Draft | 2026-05-29 |
| Boot Flow | Draft | 2026-05-29 |
| DTB Schema | Draft | 2026-05-29 |
| Virtio Matrix | Draft | 2026-05-29 |

## Principles

1. **Spec before code** — every line of implementation traces back to a spec entry
2. **Fixed addresses** — no runtime MMIO discovery, everything deterministic
3. **MMIO transport** — Virtio over MMIO, not PCI (simpler, debuggable)
4. **ARM64 only** — no x86, no RISC-V (focus beats breadth)
5. **Linux-first** — the spec is designed around what Linux expects

## How to Use

1. **Implementing a device?** → Check `mmio-map.md` for address, `irq-map.md` for IRQ
2. **Adding a Virtio device?** → Add it to `virtio-matrix.md` first
3. **Changing boot flow?** → Update `boot-flow.md` and `dtb-schema.md` together
4. **Building a hypervisor backend?** → Conform to `svabi-v0.md` Level 0

## Contributing

Changes to the spec require:
1. Update the relevant spec document
2. Bump ABI minor version if backward-compatible
3. Bump ABI major version if breaking
4. Update all dependent documents
