# Issue tracker: 逐项目本地 Markdown

本仓库是多项目工作区（`<project>/` = 一个项目）。Issue 和 spec 以 markdown 文件形式存放在**项目自己的** `.scratch/` 目录：`<project>/.scratch/`。工作区根目录**没有**统一 tracker。

## Conventions

- 一个 feature 一个目录：`<project>/.scratch/<feature-slug>/`
- spec 位于 `<project>/.scratch/<feature-slug>/spec.md`
- 实施 issue 一票一文件：`<project>/.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 编号——不要合并成单文件
- Triage 状态记录在文件顶部的 `Status:` 行（角色字符串见 `triage-labels.md`）
- 评论/对话历史追加在文件底部 `## Comments` 标题下

## 硬件项目附加约定

- 涉及具体板型时在 issue 里写明 **FQBN**（如 `m5stack:esp32:m5stack_sticks3`）
- 硬件事实引用 `kb/` 文件路径与章节（如 `kb/demos-sticks3.md §wakeup`），不要凭记忆写引脚/参数
- 验证手段：`arduino-cli compile --libraries ./<project>/lib --fqbn <FQBN> <project>/<branch>` 编译通过为底线；**需要真机验证的项在 issue 里标注 `[需真机]`**，附期望的串口日志/屏幕现象
- 串口日志、屏幕照片等现场信息直接贴进 `## Comments`

## When a skill says "publish to the issue tracker"

在对应项目下新建 `<project>/.scratch/<feature-slug>/`（目录不存在则创建）。跨项目的工作区级事项用根目录 `.scratch/`（同结构）。

## When a skill says "fetch the relevant ticket"

读取引用路径对应的文件。用户通常会直接给路径或 `<project>/NN` 编号。

## Wayfinding operations

供 `/wayfinder` 使用。**map** 是一个文件，每张票一个 **child** 文件：

- **Map**：`<project>/.scratch/<effort>/map.md`——Notes / Decisions-so-far / Fog 正文
- **Child ticket**：`<project>/.scratch/<effort>/issues/NN-<slug>.md`，从 `01` 编号，问题写在正文。`Type:` 行记录类型（`research`/`prototype`/`grilling`/`task`）；`Status:` 行记录 `claimed`/`resolved`
- **Blocking**：文件顶部 `Blocked by: NN, NN` 行。所列文件全部 `resolved` 后解除阻塞
- **Frontier**：扫描 `<project>/.scratch/<effort>/issues/`，取开放、未阻塞、未认领中编号最小者
- **Claim**：开工前先把 `Status:` 改为 `claimed` 并保存
- **Resolve**：在 `## Answer` 标题下追加答案，`Status:` 改为 `resolved`，再把上下文指针（gist + 链接）追加到 map.md 的 Decisions-so-far
