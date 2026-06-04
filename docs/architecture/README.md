# Finepaper Architecture Documentation

本文档目录按当前版本重写，用于说明 Finepaper / Ipcraft 的架构现状、公开契约、内部设计和迁移边界。

## 阅读顺序

| 文档 | 作用 |
|------|------|
| `current-architecture-design-report.zh.md` | 汇报型总览：当前架构定位、分层、核心模型和主要流程。 |
| `internal-architecture-report.zh.md` | 实现型说明：代码目录、模块职责、内部依赖方向和开发判断准则。 |
| `v1-core-architecture.md` | 公开架构契约：schema、CLI、diagnostics、security、migration 和 audit contract。 |
| `v1-refactor-overview.zh.md` | 重构总览：为什么从 Graph-centric 架构切换到 project/package contract 架构。 |
| `ipcraft-architecture-deletion-map.md` | 删除与适配边界：哪些旧概念要 delete、replace 或 adapter only。 |

## 当前架构口径

Finepaper 当前不是单纯的 NoC 画布工具，而是 package contract 驱动的 IP 制作与组合平台：

- 项目事实源：`ipcraft.project.v1` / `ProjectDocument`
- Package 能力契约：`ipcraft.package.v1` / `PackageSpec`
- 实例配置：`ConfigSchema` / `ConfigBundle`
- 项目级连接：`CompositionModel`
- 实例内部结构：`GraphConfig`
- 外部执行：`PackageInputBuilder` / `FlowRunner` / `ArtifactCollector`
- 结构化诊断：`DiagnosticStore`
- Qt 画布：当前仍是 `Graph` projection，不是长期项目根模型

## 文档维护规则

1. 公共 schema、CLI 命令、诊断 rule id 的变更必须同步 `v1-core-architecture.md`。
2. 代码模块职责或依赖方向改变时，同步 `internal-architecture-report.zh.md`。
3. 面向汇报的表达或架构概览改变时，同步 `current-architecture-design-report.zh.md`。
4. 旧 Graph-centric 概念的删除、替换或保留为 adapter 时，同步 `ipcraft-architecture-deletion-map.md`。
5. 不要让 `Graph`、NoC package id 或 generator 兼容格式重新变成 core contract。
