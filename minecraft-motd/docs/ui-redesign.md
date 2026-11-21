# `minecraft-motd` StickS3 旗舰级 UI 重构方案

> **适用项目**：`minecraft-motd`（M5StickS3，`240×135` 双横屏，8 MB OPI PSRAM，250 mAh 电池）
> **状态**：已实现并刷写至 StickS3（`COM9`）；已完成固件编译、协议单测与真实 `M5GFX` 8 屏渲染核验，待手动复位退出下载模式后做实机视觉/串口终验
> **底层参考**：[ui-frameworks.md](../../kb/ui-frameworks.md)、[lib-m5gfx.md](../../kb/lib-m5gfx.md)、[m5stick-s3.md](../../kb/m5stick-s3.md)

---

## 1. 现状诊断与升级约束

对照当前实现（[sticks3.ino](file:///c:/Users/gameg/Desktop/M5Stack/minecraft-motd/sticks3/sticks3.ino) 与 [MinecraftMotd.cpp](file:///c:/Users/gameg/Desktop/M5Stack/minecraft-motd/lib/MinecraftMotd/src/MinecraftMotd.cpp)），当前固件虽然协议与稳定性扎实，但界面表现力远未发挥 StickS3 硬件潜力：

1. **视觉扁平且有锯齿**：通篇使用 `efontCN_10/12/14`（1-bit 点阵字）+ `Font0` + 单条 `drawFastHLine` 分割线，缺乏卡片层次、抗锯齿圆角与数字主导字重；当前已占用 `240×135` 离屏画布，却在 `loop()` 里以硬编码 `100 ms`（10 FPS）刷屏，动效生硬。
2. **协议返回的高价值信息被大量丢弃**：Minecraft Server List Ping 的 `rawJson` 已经返回了三类极佳的 UI 素材，但当前解析器与界面均未使用：
   - **`favicon`**（`"data:image/png;base64,..."` 64×64 PNG 图标）：当前完全忽略，而 `M5GFX` 原生支持 `drawPng()` 内存解码。
   - **`players.sample`**（在线玩家名数组）：当前只取了 `online` / `max`，未展示在线玩家列表与进服/退服人数变化趋势。
   - **MC 富文本格式控制符**：当前解析了颜色与 `§x` 24-bit RGB，但丢弃了 **`bold`（`§l`）、`italic`（`§o`）、`underlined`（`§n`）、`strikethrough`（`§m`）、`obfuscated`（`§k` 乱码跳动）**。
3. **缺少时序与实时生命感**：没有最近 N 次 Ping 延迟折线趋势、没有 MC 原版五格信号图标、没有长按切换旋转的视觉进度反馈、没有数据刷新倒计时与脉冲动效。
4. **调样式效率低**：所有坐标和配色写死在 `sticks3.ino` 里，与 Wi-Fi/Preferences/FreeRTOS 强耦合，改几个像素就得重新进下载模式烧录实机。

### 不可破坏的硬约束（与 [README.md](../README.md) 保持一致）

- **设备界面语言保持英文**（MOTD 原文支持任意简体中文/Unicode）。
- **USB JSONL 串口协议完全兼容**：`get_config`、`configure`、`refresh`、`get_state`（含 4096 字节上限、`password_set`、`result.raw_json`、2 秒周期轻量 `state` 推送）与 NVS `"minecraft-motd"` 存储格式保持不变，不破坏 `host/start.py` 上位机。
- **`MinecraftMotd` 核心库保持跨平台纯 C++**：不能依赖 `Arduino.h` / `M5GFX.h`，保证 Windows 原生探针 `tools/native-probe` 与 `tools/verify_host.py` 继续一键编译通过。

---

## 2. 技术选型决策：为什么是“M5GFX 高阶离屏合成 + `smooth_ui_toolkit`”而非 LVGL

结合 [ui-frameworks.md](../../kb/ui-frameworks.md) 的调研结论，本固件选定：

> **渲染底座**：`M5GFX 0.2.26` 抗锯齿矢量原语 + PSRAM 多图层 `M5Canvas` 合成 + **混合字体栈（4-bpp BFF 英文/数字字库 + `efontCN_14` 中文 MOTD 兜底）**
> **动效引擎**：`Forairaaaaa/smooth_ui_toolkit`（`AnimateValue` / `Spring` / `Easing`）
> **桌面预览**：独立 `MotdUi` 渲染库 + PlatformIO `native` SDL2 PC 模拟器

### 选型理由（对比 LVGL v9）

| 评估项 | 选定方案（M5GFX 进阶 + `smooth_ui_toolkit`） | LVGL v9 |
|---|---|---|
| **交互契合度** | StickS3 无触摸只有 A/B 双键，页面切换、物理弹簧指示器与自绘卡片极其贴合 | 大量控件样式为触摸点击优化，在 240×135 双键小屏上价值有限 |
| **Minecraft 特色还原** | 可原生实现 **MC 1px 文字阴影、64×64 PNG Favicon 主色提取、`§k` 逐帧乱码字符、两边渐隐的 Marquee 遮罩**、像素草方块兜底图标 | 需写大量自定义 `lv_draw` 钩子才能实现上述效果，反而比直接用 `M5Canvas` 更绕 |
| **StickS3 内存与固件开销** | PSRAM 占用约 **~120 KB**（占 8 MB 的 1.5%），Flash 增量约几十 KB（总量仍在 ~1.9 MB / 3.2 MB） | 额外引入几十万行 C 代码、`lv_conf.h` 维护与字库膨胀 |
| **PC 预览一致性** | `MotdUi` 只依赖 `LovyanGFX` 基类，与 `M5GFX` 的 `PlatformIO_SDL` 零摩擦直通 | 需额外维护两套 HAL 适配 |

---

## 3. 数据层升级（挖掘 `rawJson` 的全部价值）

### 3.1 `MinecraftMotd` 库扩展（保持纯 C++ 可移植性）

1. **`minecraft::Span` 增加样式位掩码**：
   ```cpp
   enum StyleFlag : uint8_t {
     kBold          = 1 << 0, // JSON "bold": true 或 §l
     kItalic        = 1 << 1, // JSON "italic": true 或 §o
     kUnderlined    = 1 << 2, // JSON "underlined": true 或 §n
     kStrikethrough = 1 << 3, // JSON "strikethrough": true 或 §m
     kObfuscated    = 1 << 4, // JSON "obfuscated": true 或 §k
   };
   ```
   - 在 `Motd::component()` 中继承并解析 `bold`/`italic`/`underlined`/`strikethrough`/`obfuscated`（以及 `reset`）。
   - 在 `Motd::text()` 中补全 `§k`/`§l`/`§m`/`§n`/`§o` 与 `§r`（重置颜色为 `0xFFFFFF` 并清空样式位）。
2. **`minecraft::Result` 新增两个低成本字段**：
   - `std::vector<uint8_t> faviconPng;`：解析 `json["favicon"]`，验证 `"data:image/png;base64,"` 前缀后原地 Base64 解码；设定 **32 KiB 解码上限** 并校验 8 字节 PNG 文件头（`\x89PNG\r\n\x1a\n`），非法或超限静默丢弃（不阻断状态页显示）。
   - `std::vector<std::string> playerSample;`：解析 `json["players"]["sample"]`，最多保留前 **16 条**（去除玩家名中可能携带的 `§` 代码并限制单条字节长度），为“玩家名片墙”页提供数据。

### 3.2 固件侧时序与图标预处理流水线

1. **40 条遥测环形缓冲区**（保存在 `sticks3` 主程序态）：
   - 记录每次查询的 `{ int32_t pongMs; int32_t online; bool complete; uint32_t atMs; }`。
   - 顺带记录上一次成功查询的 `prevOnline`，计算在线人数增量 `playerDelta`（如 `+2` / `-1` 绿色/红色短时脉冲徽章）。
2. **PSRAM Favicon 缓存与动态主题色提取**（**仅在收到新 `Result` 时执行一次**，不在渲染循环里每帧解码 PNG）：
   - 利用 StickS3 的 8 MB OPI PSRAM 分配 1 个固定 `44×44` 的 `M5Canvas iconCanvas`（16-bit 仅占 **3.87 KB**）。
   - 若 `faviconPng` 非空：用 `iconCanvas.drawPng(faviconPng.data(), faviconPng.size(), 0, 0, 44, 44, 0, 0, 44.0f/64.0f, 44.0f/64.0f)` 缩放到 44×44，再对其四角涂圆角遮罩并与卡片底色融合；同时遍历中心采样像素（避开黑/灰/纯白背景），计算平均饱和高亮色作为 **`accentColor`**（限制亮度与饱和度范围，防止刺眼或黑屏）。
   - 若服务器未提供 `favicon`：在 `iconCanvas` 用程序化绘制一个 **44×44 像素风 Minecraft 草方块（Grass Block）** 作为默认徽标，并使用翡翠绿 `#4ADE80` 作为 `accentColor`。

---

## 4. 视觉设计系统（Obsidian HUD × Minecraft）

告别单调纯色背景与 1px 硬分割线，采用 **「暗黑黑曜石面板 + 服务器图标动态流光 + 等宽 Tabular 数字 + 原版 MC 像素风语义符号」** 的组合：

### 4.1 调色板规范

| 角色 | 色值 | 用途 |
|---|---|---|
| `kBgBase` | `#090D12` | 全局黑曜石底色 |
| `kCardBg` | `#121A23` | 抬升圆角卡片底色（与底色形成明确层级） |
| `kWellBg` | `#0D141C` | MOTD 内嵌凹槽底色（还原 MC 服务器列表暗框感） |
| `kCardBorder` | `#1F2E3D` | 1px 柔和卡片描边 |
| `kTextPrimary` | `#EEF4F8` | 主标题与重点数值 |
| `kTextMuted` | `#7E93A6` | 辅助标签（`ms`、`ago`、版本次要信息） |
| `kEmerald` | `#4ADE80` | 在线状态、低延迟（<150 ms）、人数上升 |
| `kGold` | `#FBBF24` | 中等延迟（150–350 ms）、部分成功告警 |
| `kRedstone` | `#F87171` | 高延迟 / 超时 / 断线错误 |
| `kDiamond` | `#38BDF8` | 查询进行中扫光与刷新反馈 |
| **动态 `accentColor`** | 源自 Favicon 主色 | 与 `kCardBg` 按 **20% 混合**，作为主卡片顶部 2px 流光条与图标外框微妙辉光 |

### 4.2 混合字体栈（零锯齿 Chrome + 全中文 MOTD）

1. **Chrome 英文与数值字体（3 套 4-bpp 抗锯齿 BFF，通过 `M5GFX` 内置 `ft_lvgl` 加载，不引入 LVGL）**：
   - `ui_meta_10`（约 10–11 px 紧凑无衬线，仅 ASCII 0x20–0x7E）：用于顶栏状态、底栏键位提示、标签名。
   - `ui_title_14`（约 14 px 半粗体，仅 ASCII）：用于服务器地址、页面标题、版本标签。
   - `ui_num_18`（约 18 px **Tabular 等宽数字** `0-9 . / - + m s %`）：用于在线人数 `2 / 20`、延迟 `31 ms` 与阶段毫秒数——数字跳变时宽度不抖动。
   - 预估总占用：3 套精简字符集 4-bpp BFF 合计仅 **~25–35 KB Flash**。
2. **动态 MOTD 字体**：
   - 使用 `fonts::efontCN_14` 兜底渲染任意中文、日文与特殊符号，保证不乱码。
   - **原版 MC 文字阴影（Drop Shadow）**：每个 MOTD Span 先用 `color * 0.25`（暗化 75%）绘制在 `(x + 1, y + 1)`，再用原色绘制在 `(x, y)`，瞬间还原《我的世界》游戏内多人联机列表的立体质感。
   - **样式扩展绘制**：`Bold` 采用 `(x, y)` + `(x+1, y)` 伪粗体加阴影；`Underlined` / `Strikethrough` 在对应偏移画 1px 线段；`Obfuscated`（`§k`）每帧将 ASCII 可打印字符替换为同宽随机字符。

### 4.3 程序化矢量微图标（告别纯文字状态）

- **MC 原版 5 格信号条（12×10 px）**：
  - 5 根递增高度圆角柱：`pongMs < 150` 满 5 格翠绿、`< 300` 4 格、`< 600` 3 格金黄、`< 1000` 2 格橙、其余 1 格红；Pong 失败或断连显示红叉；正在 `busy` 查询时 5 格依次波浪扫亮。
- **状态胶囊（Status Pill）**：左侧带呼吸圆点，右侧紧凑胶囊文字（`ONLINE` / `PINGING` / `PONG ERR` / `OFFLINE` / `SETUP`）。
- **电池胶囊**：绘制电池外框 + 内部填充条 + 百分比，并在 `M5.Power.isCharging()` 为真时叠加闪电符号。

---

## 5. `240×135` 三页信息架构与像素级布局

保持全屏高度 `135 px` 的黄金切分：**Top Bar (`y: 0..17`) + 主画布舞台 (`y: 19..118`，共 100 px 高度) + Footer (`y: 120..134`)**。

### 5.1 全局 Chrome（三页共享）

- **Top Bar (`y: 0..17`)**：
  - 左侧：状态呼吸圆点 + 状态胶囊（`ONLINE` / `PINGING` / `SETUP` / `ERROR`）。
  - 顶边最上方 `y: 0..1`：当 `busy == true` 时，一条 **40 px 宽的 `kDiamond` 渐变扫光条** 左右匀速穿梭，给出极克制但高级的“后台请求中”反馈。
  - 右侧：Wi-Fi 信号弧图标 + 电池胶囊与电量（充电时变翠绿带闪电）。
- **Footer (`y: 121..134`)**：
  - 左侧：`[A] Ping`（紧凑键帽胶囊）。
  - 居中：**弹簧药丸分页指示器**（当前页高亮拉长成 12×4 圆角胶囊，其余页为 4×4 圆点，支持 3 页平滑弹性形变）。
  - 右侧：距上次刷新时间（如 `12s ago`）+ `[B]`。
  - 最底边 `y: 133..134`：**2px 自动刷新倒计时细条**（随 `(now - finishedAt) / settings.refreshMs` 平滑推进，让用户一眼感知下次自动刷新还有多久）。

### 5.2 三大页面设计（短按 B 键横向弹簧切换）

#### Page 0：Server Card（主名片页 — 默认展示）

```text
y=0..17   ● ONLINE                             WiFi  [■■■ 84%]
          ┌───────────────────────────────────────────────────┐ y=19
          │ ▓▓▓▓  play.hypixel-like.net          ▂▄▆█ 31 ms   │
          │ ▓▓▓▓  [ 14 / 200 ] ████░░░░          1.20.4       │
          └───────────────────────────────────────────────────┘ y=71
          ┌───────────────────────────────────────────────────┐ y=74
          │  Welcome to CSJ's Survival Server!  [渐变隐没]  · │
          │  Season 4 · 生存建筑与红石工艺                   · │
          └───────────────────────────────────────────────────┘ y=118
y=121..134  [A] Ping        ●───  ○  ○              12s ago [B]
          ───────────────────────────────────────────────────  (倒计时细条)
```

- **上半部 Hero Card (`x: 6..234, y: 19..71`，`fillSmoothRoundRect r=7`)**：
  - 卡片顶部内置 2px `accentColor` 高光边。
  - 左侧 `x: 12, y: 23`：**44×44 圆角 Favicon**（由 `iconCanvas` 推送，新图标到达时带轻微由 `0.85 → 1.0` 的弹簧缩放出场）。
  - 中上 `x: 62, y: 24`：服务器 `settings.host`（超宽自动平滑 Marquee，无生硬截断）。
  - 右上：MC **5 格信号图标** + `ui_num_18` 彩色延迟数字（如 `31` + 小号 `ms`）。
  - 中下 `x: 62, y: 46`：**在线人数胶囊**（`14 / 200` + `+2` 进服增量气泡 + 底部 2px 满员率迷你进度条），右侧对齐 `result->version`（超宽自动横向滚动或智能缩写）。
- **下半部 MOTD Obsidian Well (`x: 6..234, y: 74..118`，`fillSmoothRoundRect r=6`，`kWellBg` 凹槽)**：
  - 承载 **2 行富文本 MOTD**（带 MC 1px 阴影、`§l/§n/§m/§k` 特效）。
  - **丝滑滚动与边缘渐隐遮罩（告别硬切裁剪）**：在 MOTD 凹槽左右两端各叠加 **10 px 宽逐列 Alpha 混合的 `kWellBg` 渐隐遮罩条**，长行在其中滚动时如同从暗影中滑入/淡出，彻底消除目前 `setClipRect` 一刀切的粗糙感。
  - 多于 2 行时，采用 **垂直弹簧翻页**（两行整体向上滑出、下两行弹性滑入），右缘显示极细的页码点提示当前第几页 MOTD。

#### Page 1：Players & Latency Trend（玩家与延迟趋势页）

```text
y=19..67  ┌───────────────────────────────────────────────────┐
          │ LATENCY TREND (40)       min 24  avg 33  now 31ms │
          │   ╱╲    ╱──╲                                      │
          │ ──  ╲──╱    ╲───────● (翠绿渐变面积+抗锯齿宽线)   │
          └───────────────────────────────────────────────────┘
y=70..118 ┌───────────────────────────────────────────────────┐
          │ ONLINE SAMPLE (14)                           +2 ▲ │
          │ [Steve]  [Alex]  [CSJ_Builder]  [M5Player]  ...   │
          └───────────────────────────────────────────────────┘
```

- **上半区**：使用 `M5GFX` 的 `drawWideLine()`（线宽 1.5 px 抗锯齿）+ 半透明填充区域，绘制最近 40 次 Ping 历史折线；超时/失败采样点用 `kRedstone` 竖虚线标记，右上显示 `min / avg / now`。
- **下半区**：将 `result->playerSample` 渲染为自动换行的圆角玩家铭牌胶囊（超过 2 行时自动垂直循环滚动）；若服务器未返回 `sample`（如隐藏名单或离线），优雅降级显示 `Sample hidden by server` + 本轮可用率（`Uptime 97.5%`）与峰值人数。

#### Page 2：Network Waterfall（网络耗时瀑布诊断页）

```text
y=19..46  ┌───────────────────────────────────────────────────┐
          │ TARGET 198.51.100.42:25565            PROTO v47   │
          │ ████████████████████▓▓▓▓▓▓▓▓▓▒▒▒▒▒▒▒░░░░░░░░ 84ms │
          └───────────────────────────────────────────────────┘
y=49..118 ┌────────────────────────┐ ┌────────────────────────┐
          │ DNS               4 ms │ │ TCP              18 ms │
          ├────────────────────────┤ ├────────────────────────┤
          │ STATUS           31 ms │ │ PING/PONG        31 ms │
          └────────────────────────┘ └────────────────────────┘
```

- **堆叠耗时瀑布条**：四段颜色分别对应 `DNS`（紫）、`TCP`（蓝）、`Status`（金）、`Ping/Pong`（翠绿），各段宽度用 `AnimateValue` 弹簧展开，直观看出瓶颈卡在握手还是传输。
- **2×2 诊断卡片**：`ui_num_18` 等宽大数字展示四阶段精确耗时，失败阶段自动高亮红色描边与阶段标签。

### 5.3 异常与未配置状态专页

- **`Setup` 未配置页**：居中极简卡片提示 `Connect USB & run host tool` + 呼吸 USB 图标。
- **`Wi-Fi Connecting`**：平滑扩散的三重 Wi-Fi 雷达弧线动画 + 15 秒后转为 `kRedstone` 提示 `[A] Reconnect`。
- **Partial Success（`statusReceived && !complete`）**：保留 Page 0 的 Favicon 与 MOTD 展示，仅将右上延迟区替换为 `PONG ERR` 红色徽章，兼顾可读性与真实状态。
- **完全失败页**：保留结构化错误阶段标签（`DNS` / `TCP` / `STATUS`）与自动换行错误信息卡片，不破坏顶/底栏呼吸节奏。

### 5.4 按键交互与长按视觉闭环

保留并增强既有物理按键定义（不改变习惯）：

- **短按 A 键**：触发手动刷新 / Wi-Fi 重连，同时在顶栏触发一次 `kDiamond` 脉冲闪光，立刻给予按压确认感。
- **短按 B 键**：在 `Page 0 (Card) → Page 1 (Trend) → Page 2 (Waterfall)` 之间弹性横滑切换。
- **长按 B 键（700 ms）**：按住超过 150 ms 后在右下角/屏幕中央半透明浮层弹出 **抗锯齿圆环蓄力条（`fillArc`）+ `FLIP 180°`**，进度条填满 700 ms 瞬间完成 `90° ↔ 270°` 旋转并写入 NVS，松手未满则平滑回弹消失。

---

## 6. 动效规格、自适应帧率与资源预算

### 6.1 `smooth_ui_toolkit` 动效参数建议

| 动画对象 | 类型 | 推荐配置 | 效果 |
|---|---|---|---|
| 页面横向切换 `pageScrollX` | `Spring` | `visualDuration = 0.28f, bounce = 0.15f` | 干脆略带回弹的推拉切页 |
| 底部页码指示胶囊宽/位置 | `Spring` | `visualDuration = 0.24f, bounce = 0.25f` | 灵动的药丸形变 |
| MOTD 垂直翻页偏移 | `Spring` | `visualDuration = 0.35f, bounce = 0.10f` | 顺滑上推下入 |
| 瀑布条分段 / 人数进度条 | `Easing` | `ease_out_cubic`, `300 ms` | 新数据到达时线性填充展开 |
| Favicon 到场缩放 pop | `Spring` | `visualDuration = 0.30f, bounce = 0.30f` | 轻微 Q 弹点亮名片 |
| 长按 B 键蓄力环 | 实时进度 + `Easing` 回退 | `0 → 360°`（150..700 ms） | 明确告知“再按一会即翻转” |

### 6.2 自适应帧率策略（兼顾 40fps 丝滑感与 250 mAh 续航）

不能固定以 60fps 死循环重绘，建议在渲染引擎中实现 `needsActiveFrame()` 判定：

- **高刷态（30–45 FPS，间隔 ~22–33 ms）**：仅在以下任一条件满足时激活：
  - 任意 `AnimateValue` 尚未收敛到目标值；
  - 当前存在超宽 `host` / `version` / `MOTD` 横向 Marquee 滚动；
  - 当前页面正在显示含 `§k`（Obfuscated）片段的 MOTD；
  - `busy == true`（顶栏扫描光条）或正在按住 B 键蓄力或处于 Wi-Fi 连接雷达动画中。
- **静态省电态（2 FPS，间隔 500 ms）**：当动画全部落定、无横向滚动且无 `§k` 时，自动降频刷新（仅维持秒级 `ago` 时间和低频状态），显著降低 SPI 刷新频率与 StickS3 250 mAh 电池功耗。

### 6.3 内存与 Flash 预算

| 资源项 | 预算 | 占 StickS3 上限比例 |
|---|---|---|
| 主合成画布 `canvas`（240×135×2） | 64,800 B | 放 PSRAM（占 8 MB 的 0.8%） |
| Page 切换离屏辅助层（如需横滑双画布） | 64,800 B | 放 PSRAM（可按需创建或只重绘可见区两页偏移，总 PSRAM < 150 KB） |
| `iconCanvas`（44×44×2） | 3,872 B | 放 PSRAM |
| `faviconPng` 原始缓存（上限 32 KB） | ≤ 32 KB | 动态随新结果覆盖 |
| 40 条遥测环形缓冲 | < 1 KB | 内部 SRAM |
| 3 套精简 4-bpp BFF 字体 | ~30 KB | Flash |
| `smooth_ui_toolkit` + 新 UI 代码增量 | ~40–60 KB | Flash（总固件预计 ~1.88 MB / 3.2 MB，约 58%） |

---

## 7. 模块解耦与 PC SDL2 预览开发流

为了解决“调样式每次都要进下载模式烧真机”的痛点，重构为严格三层解耦：

```text
minecraft-motd/
├── lib/
│   ├── MinecraftMotd/       ← 纯 C++ 协议解析（+ Span.style / faviconPng / playerSample）
│   └── MotdUi/              ← 新增：只依赖 LovyanGFX 基类与 smooth_ui_toolkit 的纯 UI 库
│       ├── include/MotdUi.h ← 输入只读 UiModel，输出到传入的 LovyanGFX* 画布
│       └── src/
│           ├── fonts/       ← 3 套 4-bpp BFF 字体 C 数组
│           ├── MotdUi.cpp   ← 合成管线、动态帧率判定、渐变遮罩、草方块图标
│           └── Pages.cpp    ← 3 个页面与状态页绘制
├── sticks3/                 ← 极薄硬件胶水层：Wi-Fi/FreeRTOS/NVS/串口协议/M5PM1 -> 填充 UiModel
└── tools/
    └── ui-sim/              ← 新增：PlatformIO native SDL2 PC 模拟器（-DM5GFX_BOARD=board_M5StickS3 -DM5GFX_SCALE=2）
```

**PC 模拟器价值**：在 `tools/ui-sim` 内置 4 组典型测试固件快照（Hypixel 风格复杂彩色+`§k`+Favicon、超长中文多行 MOTD、高延迟抖动曲线、Pong 失败半残缺状态），在 Windows 执行 `pio run -e native -t exec` 即可用键盘 `A`/`B` 键 2 倍放大窗口秒级调优像素、动效曲线与抗锯齿效果。

> **二期可选扩展（本期规划外，不纳入首期必须项）**：
> - 参考同工作区 `liangzi-meter` [ADR-0005](../../liangzi-meter/docs/adr/0005-gravity-auto-rotate.md) 的真机标定轴系，为桌面摆放场景增加可选的 BMI270 重力自动横屏（`rotation 1 ↔ 3`）；
> - 基于 StickS3 内置 ES8311 扬声器，提供一个**默认静音、上位机可开启**的轻柔进服提示音（严格控制音量与电源状态）。

---

## 8. 分阶段实施路线图与验收命令

| 阶段 | 核心交付 | 验收方式 |
|---|---|---|
| **M1：协议解析增强** | 扩展 `MinecraftMotd` 的 `Span.style`、`faviconPng` Base64 解码与 PNG 校验、`playerSample` 提取；保持接口向后兼容 | `cmd /c minecraft-motd\tools\native-probe\build.cmd`<br>`minecraft-motd/.build/native/native-probe.exe <HOST> <PORT>`<br>`python minecraft-motd/tools/verify_host.py <HOST> <PORT>` |
| **M2：解耦 `MotdUi` 与 SDL2 PC 模拟器** | 引入 `smooth_ui_toolkit`，生成 3 套 4-bpp BFF 字体，完成 `MotdUi` 3 页 + 动效 + 边缘渐隐 + 草方块兜底；跑通 Windows SDL2 2× 预览并打磨视觉 | `pio run -d minecraft-motd/tools/ui-sim -e native -t exec` |
| **M3：StickS3 固件接入与自适应帧率** | 瘦身 `sticks3.ino` 为硬件数据源，接入遥测环形缓冲、充电态图标、长按 B 圆环蓄力与动态降频策略 | `arduino-cli compile --libraries ./minecraft-motd/lib --libraries ./lib --fqbn m5stack:esp32:m5stack_sticks3 --build-path ./minecraft-motd/.build/firmware minecraft-motd/sticks3` |
| **M4：实机烧录与回归测试** | 刷写 StickS3 实机验证 90°/270°、USB JSONL 协议、真实服务器 Favicon/中文 MOTD/玩家名单与长按翻转 | `arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 --input-dir ./minecraft-motd/.build/firmware -p COMx minecraft-motd/sticks3`<br>`python minecraft-motd/tools/verify_usb.py COMx --wait-query` |
