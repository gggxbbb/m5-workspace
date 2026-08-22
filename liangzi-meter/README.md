# liangzi-meter

M5StickS3 桌面摆件：显示**北京时间**、**DeepSeek 峰谷状态**（高峰期「梁文峰」/ 非高峰期「梁文谷」）与**账户余额**。配置（WiFi / NTP / API Key / 峰谷时段覆盖）由 PyQt6 上位机经 USB 串口下发。

## 目录结构

```
liangzi-meter/
├── host/            # PyQt6 上位机（uv 管理）
│   └── src/liangzi_meter/
├── sticks3/         # StickS3 固件（.ino + config.h + certs.h）
├── docs/
│   └── adr/         # 设计决策记录
└── CONTEXT.md       # 领域术语表
```

## 快速开始

### 1. 上位机（配置工具）

```bash
cd host
uv sync            # 创建 venv 并安装依赖（PyQt6 + pyserial）
uv run liangzi-meter
# 或：uv run python -m liangzi_meter
```

操作：选 COM 口 → 连接 → 填 WiFi/NTP/API Key（可勾选覆盖官方峰谷时段）→ 下发配置。
上位机本地保存的配置（`host/config.json`，含 API Key，已 gitignore）会在下次连接时自动下发。

### 2. 固件

```bash
# 编译（StickS3 默认分区 8MB，原生 USB CDC 已启用）
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 ./sticks3

# 烧录（设备需进入下载模式：按住侧面复位键约 2 秒至绿色 LED 闪烁）
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 -p COMx ./sticks3
```

### 3. 使用

- 屏幕主页（核心区自上而下）：
  - 顶部小字：北京时间（含日期/星期）
  - 「当前时段剩余」+ 倒计时数字（两行）——距下一次峰谷切换
  - 「现在是」→ **超大字峰谷**：非高峰「梁文谷」**绿色** / 高峰「梁文峰」**黄色**，垂直居中（峰谷不使用红色；"峰/谷"对仗为刻意用字，见 ADR-0003）
  - 「余额」→ **金额大字**：余额低于告警阈值（上位机可配置，默认 10 元）时显示**红色**
- KEY1：立即刷新余额；KEY2：切换「余额详情」页。
- 余额每 5 分钟自动查询（`GET https://api.deepseek.com/user/balance`），失败保留旧值并标记「（旧）」。

## 串口协议

JSON Lines @ 115200，见 [docs/adr/0002-jsonl-serial-protocol.md](docs/adr/0002-jsonl-serial-protocol.md)。

```json
{"type":"ping"}
{"type":"hello","fw":"1.0.0","model":"StickS3","configured":true}
{"type":"config","wifi":{"ssid":"...","password":"..."},"ntp":"ntp.aliyun.com","api_key":"sk-...","balance_warn":10.0,"peak_ranges":[{"start":"09:00","end":"12:00"},{"start":"14:00","end":"18:00"}]}
{"type":"ack","ok":true,"msg":"config applied"}
{"type":"get_state"}
{"type":"state","time":"2026-08-18 09:19:16","phase":"peak","balance":110.0,"balance_valid":true,"wifi":true,"ssid":"...","battery":87}
```

## 峰谷时段（官方）

北京时间 **09:00–12:00**、**14:00–18:00** 为高峰（工作日），其余为空闲（价格为高峰一半）。**周末（周六、周日）全天不区分峰谷，统一按低谷价收取**（2026-08-23 起执行）；空闲/低谷时段倒计时指向下一个工作日（周一）的首个高峰边界。时段可在上位机勾选「覆盖官方峰谷时段」自定义，未勾选则使用上述默认。

## 已知限制

- 设备无独立 RTC，断电重启后依赖 NTP 重新同步（开机自动同步，已同步后每 6 小时重同步）。
- API Key 以明文存储于设备 NVS 与 `host/config.json`（本地 USB 场景，风险可接受，见 ADR-0001）。
- TLS 根证书（DigiCert Global Root G2）内嵌固件，证书轮换需重刷。
