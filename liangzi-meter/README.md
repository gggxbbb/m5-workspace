# liangzi-meter

M5StickS3 桌面摆件（固件 `v1.4.0` Obsidian HUD）：显示**北京时间**、**DeepSeek 峰谷状态**（高峰期「梁文峰」/ 非高峰期「梁文谷」）、**24 小时排班时间轴**、**赠金/充值资金拆分**与**40 次消耗趋势曲线**。配置（WiFi / NTP / API Key / 峰谷时段覆盖 / 提示音 / 屏幕方向）由 PyQt6 上位机经 USB 串口下发。

## 目录结构

```
liangzi-meter/
├── host/                  # PyQt6 上位机（uv 管理）
│   └── src/liangzi_meter/
├── lib/
│   └── LiangziUi/         # 解耦的纯 C++ 领域模型 + 4-bpp BFF 字库 + M5GFX 三页 HUD 引擎
├── sticks3/               # StickS3 固件（Core 0 异步 HTTPS + Core 1 显示/按键/IMU/RWR 音频）
├── tools/
│   ├── gen_fonts.py       # lv_font_conv BFF 字库生成 + M5GFX cmap 校验 + UI 汉字覆盖自检
│   ├── native-test/       # MSVC 纯 C++ 领域模型单测（DeepSeek 解析 / 2026-2027 节假日 / RWR）
│   └── ui-sim/            # MSVC + 真实本地 M5GFX 0.2.26 多方向模拟器与截图导出
├── docs/
│   ├── adr/               # 设计决策记录（ADR-0001 ~ ADR-0006）
│   └── ui-redesign.md     # Obsidian HUD 三页 UI 重构设计与验证记录
└── CONTEXT.md             # 领域术语表
```

## 快速开始

### 1. 上位机（配置工具）

```bash
cd host
uv sync            # 创建 venv 并安装依赖（PyQt6 + pyserial）
uv run liangzi-meter
# 或：uv run python -m liangzi_meter
```

操作：选 COM 口 → 连接 → 填 WiFi/NTP/API Key（可勾选覆盖官方峰谷时段、开启 RWR 提示音、配置默认方向或重力旋屏）→ 下发配置。
上位机本地保存的配置（`host/config.json`，含 API Key，已 gitignore）会在下次连接时自动下发。

### 2. 固件编译与烧录

```bash
# 从工作区根目录编译（StickS3 默认分区 8MB，需携带 --libraries）
arduino-cli compile --libraries ./liangzi-meter/lib --libraries ./lib --fqbn m5stack:esp32:m5stack_sticks3 ./liangzi-meter/sticks3

# 烧录（设备需进入下载模式：按住侧面复位键约 2 秒至绿色 LED 闪烁）
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 -p COMx ./liangzi-meter/sticks3
```

### 3. 三页界面与按键交互（同时适配竖屏 `135×240` 与横屏 `240×135`）

- **P0 主仪表**：北京时间、峰谷状态卡（高峰「梁文峰」琥珀金 / 低谷「梁文谷」翡翠绿，见 ADR-0003；含倒计时与内嵌 24h 时间轴；横屏下依据 ADR-0005 自动置于 USB 端侧）与可用余额卡（含最近变化 delta / `旧` 标记、低于阈值红框、赠金/充值双色条）。出厂未配置时显示 USB 配网引导卡。
- **P1 资金构成与消耗趋势**：拆解 `granted_balance`（优先扣费赠金）与 `topped_up_balance`（充值余额）、币种与可用态 + 最近 40 次余额采样消耗折线图。
- **P2 今日排班与系统诊断**：放大 24h 峰谷排班刻度（含周末/法定节假日低谷提示）与 WiFi（含 RSSI）、IP、NTP、RWR 监控状态、HTTP 状态码、屏幕方向及固件版本。
- **KEY1（A 键）短按**：不清空当前有效余额，触发顶栏青色扫光脉冲并由 Core 0 异步立即刷新（平时 5 分钟周期，开启提示音后的高峰期自动加速为 15 秒）；失败保留旧值并标记 `旧`。
- **KEY2（B 键）短按**：在 P0 / P1 / P2 三页间无回弹平滑横滑切换。
- **KEY2（B 键）长按（700 ms）**：弹出圆环蓄力指示条并顺时针切换方向（`0° → 90° → 180° → 270°`）并写入 NVS；若已开启 `auto_rotate` 重力旋屏，则保留自动方向并弹出「自动旋屏已开」提示。

### 4. 本地测试与 PC 预览工具

```bash
# 1. 重新生成 6 套 4-bpp BFF 字体并核验 M5GFX cmap 与 UI 汉字 100% 覆盖
python liangzi-meter/tools/gen_fonts.py

# 2. 编译并运行纯 C++ 领域单测
cmd /c liangzi-meter\tools\native-test\build.cmd
liangzi-meter\.build\native\native-test.exe

# 3. 编译并运行真实 M5GFX PC 模拟器（--dump-dir 可批量导出 12 帧竖/横屏 2x BMP）
cmd /c liangzi-meter\tools\ui-sim\build.cmd
liangzi-meter\.build\native\ui-sim.exe --dump-dir liangzi-meter\.build\ui-preview
```

## 串口协议

JSON Lines @ 115200，见 [docs/adr/0002-jsonl-serial-protocol.md](docs/adr/0002-jsonl-serial-protocol.md)。`state` 在完全保留已有字段的同时，已同步余额时会额外附带向后兼容的可选字段 `granted_balance`、`topped_up_balance`、`currency`、`balance_available`。

```json
{"type":"ping"}
{"type":"hello","fw":"1.4.0","model":"StickS3","configured":true}
{"type":"config","wifi":{"ssid":"...","password":"..."},"ntp":"ntp.aliyun.com","api_key":"sk-...","balance_warn":10.0,"peak_ranges":[{"start":"09:00","end":"12:00"},{"start":"14:00","end":"18:00"}]}
{"type":"ack","ok":true,"msg":"config applied"}
{"type":"get_state"}
{"type":"state","time":"2026-08-18 09:19:16","phase":"peak","weekend":false,"holiday":false,"balance":110.0,"balance_valid":true,"granted_balance":10.0,"topped_up_balance":100.0,"currency":"CNY","balance_available":true,"wifi":true,"ssid":"...","battery":87}
```

## 峰谷时段（官方规则与节假日）

按 [DeepSeek 官方定价页](https://api-docs.deepseek.com/zh-cn/quick_start/pricing/)（已于 **2026-11-21** 再次核实最新页面原文）：北京时间周一至周五（**不含中国法定节假日**）的 **09:00–12:00**、**14:00–18:00** 为高峰，其余时段为空闲（空闲时段价格为高峰时段的一半）。周末和法定节假日全天低谷，倒计时会自动跳过这些日期。上位机可覆盖每日高峰时刻范围，但周末/节假日日期规则不被覆盖。

固件（`LiangziUi/src/LiangziModel.h`）当前内置国务院办公厅公布的 **2026 年**（国办发明电〔2025〕7号）与 **2027 年**（国办发明电〔2026〕10号，2026-11-05 发布）完整放假日期；进入 2028 年前需依据当年通知更新 `isChinaPublicHoliday()`。

## 已知限制

- 设备无独立 RTC，断电重启后依赖 NTP 重新同步（开机自动同步，已同步后每 6 小时重同步）。
- API Key 以明文存储于设备 NVS 与 `host/config.json`（本地 USB 场景，风险可接受，见 ADR-0001）。
- TLS 根证书（DigiCert Global Root G2）内嵌固件，证书轮换需重刷。
