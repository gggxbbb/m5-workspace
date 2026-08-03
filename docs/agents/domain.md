# Domain Docs

工程技能在本仓库消费领域文档的规则。本仓库是多项目工作区：**每个项目可以有自己的领域文档，工作区根部一份兜底**。

## 读取顺序（开始探索前）

1. **项目级**：在 `<project>/` 下工作时，先读 `<project>/CONTEXT.md` 和 `<project>/docs/adr/`（存在的话）
2. **工作区级**：根 `CONTEXT.md` + 根 `docs/adr/`——跨项目约定与兜底
3. **硬件知识**：`kb/` 是**权威的硬件事实来源**（引脚、FQBN、库 API、官方 demo 模式，见根 `AGENTS.md` 的查证顺序）。`kb/` 与 CONTEXT.md 冲突时，硬件事实以 `kb/` 为准并在 CONTEXT.md 中修正

文件不存在时**静默跳过**——不提示缺失、不主动创建。`/domain-modeling`（经 `/grill-with-docs`、`/improve-codebase-architecture` 触达）在术语或决策真正落地时惰性创建它们。

## 文件结构

```
/
├── CONTEXT.md                 ← 工作区级（惰性创建）
├── docs/adr/                  ← 跨项目决策
├── kb/                        ← 硬件权威知识（已存在，全部官方来源）
└── <project>/
    ├── CONTEXT.md             ← 项目级术语表/上下文（惰性创建）
    ├── docs/adr/              ← 项目级决策
    └── .scratch/              ← 项目 issue tracker（见 issue-tracker.md）
```

## 使用术语表的词汇

输出中命名领域概念时（issue 标题、重构提案、假设、测试名），用 CONTEXT.md 里定义的术语；术语表明确避免的同义词不要漂移过去。本仓库术语可以中英混用，但同一概念只能有一个首选写法。

需要的概念不在术语表：要么你在发明项目不用的语言（重新考虑），要么是真缺口（记给 `/domain-modeling`）。

## 标记 ADR 冲突

输出与现有 ADR 矛盾时，显式声明而不是静默覆盖：

> _Contradicts ADR-0007 (…) — but worth reopening because…_
