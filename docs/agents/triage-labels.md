# Triage Labels

技能体系使用五个标准 triage 角色。本仓库的 issue tracker 是逐项目本地 markdown（见 `issue-tracker.md`），**标签通过 issue 文件顶部的 `Status:` 行落地**，不是 GitHub label。

| Label in mattpocock/skills | 本仓库写法（`Status:` 行） | 含义 |
| --- | --- | --- |
| `needs-triage` | `Status: needs-triage` | 维护者需要评估 |
| `needs-info` | `Status: needs-info` | 等待报告者补充信息 |
| `ready-for-agent` | `ready-for-agent` | 已完全明确，可交给 AFK agent |
| `ready-for-human` | `Status: ready-for-human` | 需要人来实施 |
| `wontfix` | `Status: wontfix` | 不处理 |

技能提到某个角色时（如"apply the AFK-ready triage label"），在 issue 文件顶部写对应的 `Status:` 行。

另外两个过程状态沿用本地 tracker 约定：`claimed`（已认领，开工前写）、`resolved`（已解决，附 `## Answer`）。
