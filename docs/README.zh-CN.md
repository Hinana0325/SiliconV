# docs — 文档

按主题组织的项目文档。

## 目录

| 目录 | 描述 |
|------|------|
| [android/](android/) | Android 集成指南和说明 |
| [architecture/](architecture/) | 系统架构深入分析 |
| [gpu/](gpu/) | GPU 虚拟化文档 |
| [svabi/](svabi/) | SiliconV ABI 规范详情 |
| [virtualization/](virtualization/) | 虚拟化概念和参考 |

## 文档标准

- 所有文档使用 Markdown（`.md`）
- 使用 ASCII 艺术或 Mermaid 语法包含图表
- 保持代码示例与当前 API 同步
- 对权威定义交叉引用规范（`spec/`）

## 构建文档（未来）

```bash
# 生成 HTML 文档
pip install mkdocs
mkdocs serve
```
