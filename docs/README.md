# docs — Documentation

Project documentation organized by topic.

## Directories

| Directory | Description |
|-----------|-------------|
| [android/](android/) | Android integration guides and notes |
| [architecture/](architecture/) | System architecture deep-dives |
| [gpu/](gpu/) | GPU virtualization documentation |
| [svabi/](svabi/) | SiliconV ABI specification details |
| [virtualization/](virtualization/) | Virtualization concepts and references |

## Documentation Standards

- Use Markdown (`.md`) for all documentation
- Include diagrams using ASCII art or Mermaid syntax
- Keep code examples up to date with the current API
- Cross-reference the spec (`spec/`) for authoritative definitions

## Building Docs (future)

```bash
# Generate HTML docs
pip install mkdocs
mkdocs serve
```
