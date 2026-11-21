# StickS3 Minecraft MOTD 观测器

## 使用设备

- M5StickS3，`m5stack:esp32:m5stack_sticks3`。
- 240×135 横屏，支持 90°、270° 两个方向。
- Java 版服务器通过 TCP Server List Ping 查询。

## 上位机配置

Windows 使用带有 tkinter 的 Python 3.11 或更新版本，并安装 pyserial：

```powershell
python -m pip install "pyserial>=3.5,<4"
python minecraft-motd/host/start.py
```

1. 通过 USB 连接 StickS3，选择串口，点击“Connect”。
2. 填写 Wi-Fi 名称、密码、服务器地址和端口，选择屏幕方向。
3. 点击“Save to device”。设备保存设置并连接 Wi-Fi，随后开始查询。
4. 在“Server response”查看耗时和服务器原始 JSON；点击“Query now”刷新结果。

设备将设置保存在 NVS 中，重新上电时加载设置。上位机通过 USB 串口配置设备，Wi-Fi 与服务器字段由用户填写。密码输入框留空表示保留设备已存密码，选择“Open network”表示使用空密码。

| 设置 | 范围或用途 |
|---|---|
| Wi-Fi 名称 | 1～32 个 UTF-8 字节，使用 2.4 GHz 网络 |
| Wi-Fi 密码 | 8～63 个 UTF-8 字节，开放网络使用空密码 |
| 服务器地址 | IPv4 地址或域名 |
| 服务器端口 | 1～65535，默认 25565 |
| 屏幕方向 | 90°、270° |
| 连接目标 | 可选，指定实际 TCP 连接域名，握手保留服务器地址 |
| 握手协议编号 | 0～2147483647，默认 47 |
| 刷新间隔 | 10～3600 秒，默认 30 秒 |
| 阶段超时 | 1～30 秒，默认 5 秒，分别约束 TCP、状态交换和 Ping/Pong |
| 屏幕亮度 | 20～255，默认 150 |

DNS 解析使用 ESP32 网络库的解析时限。域名使用 SRV 记录时，在“Connect host (optional)”填写 SRV 的目标域名，在“Server port”填写 SRV 的端口。

## 设备界面

设备和上位机界面使用英文。采用基于 `M5GFX` 抗锯齿原语、PSRAM 多图层离屏合成与弹簧物理动效（`MotdUi`）的三页 Obsidian HUD 设计：

| 页面 | 内容 |
|---|---|
| Page 0（Server Card） | 呼吸状态胶囊、Wi-Fi 与充电状态指示、`44×44` 圆角服务器 Favicon（自动提取主色作为卡片流光；无图标时显示像素风草方块）、主机名滚动、MC 原版 5 格信号条与等宽延迟、人数胶囊与进退服增量气泡、双行富文本 MOTD（带 1px 阴影、`bold`/`underlined`/`strikethrough`/`§k` 乱码跳动、左右 10px 渐变隐没遮罩与垂直弹簧翻页） |
| Page 1（Trend & Players） | 最近 40 次 Ping 历史抗锯齿折线图（`min`/`avg`/`now` 及失败采样标记）、服务器可用率与 `players.sample` 玩家铭牌墙（无名单时优雅降级提示） |
| Page 2（Network Waterfall） | 解析 IP、协议号、4 阶段耗时堆叠瀑布条（DNS / TCP / Status / Ping-Pong）与 `2×2` 诊断瓷砖 |

| 操作 | 行为 |
|---|---|
| 短按 A 键 | 刷新查询（顶栏触发蓝色扫描脉冲）；Wi-Fi 连接失败时重新连接 |
| 短按 B 键 | 在 3 个页面间弹簧横滑切换 |
| 长按 B 键 700 毫秒 | 弹出 `FLIP 180` 圆环蓄力条，满格后切换两个横屏方向并保存设置 |

刷新调度采用自适应帧率：动画、Marquee 滚动、`§k`、请求进行中或长按蓄力时以约 40 FPS（25 ms）渲染，静止后自动降频至 2 FPS（500 ms）省电。

查询由独立 FreeRTOS 任务执行。修改配置、断开 Wi-Fi 后，设备丢弃旧查询结果。状态响应有效且 Pong 失败时，界面保留已收到的 MOTD 与图标，并标记 `PONG ERR`。每次失败均显示当前阶段和错误信息。

MOTD 支持字符串、JSON `text`、`extra`、数组、标准颜色、十六进制颜色、`§` 颜色与格式代码（`§k`/`§l`/`§m`/`§n`/`§o`/`§r`），以及 `favicon`（≤32 KiB PNG Base64 解码）与 `players.sample`（≤16 条）。`translate` 显示服务器返回的翻译键。状态包限制为 128 KiB，MOTD 最多 64 行、512 个文字片段；超出限制时报告错误。

## 编译与上传

依赖 M5Unified 0.2.19、M5GFX 0.2.26、ArduinoJson 7.4.3，板卡包使用 `m5stack:esp32` 3.3.8。

在工作区根目录执行：

```powershell
arduino-cli compile --libraries ./minecraft-motd/lib --libraries ./lib --fqbn m5stack:esp32:m5stack_sticks3 --build-path ./minecraft-motd/.build/firmware minecraft-motd/sticks3
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 --input-dir ./minecraft-motd/.build/firmware -p COMx minecraft-motd/sticks3
```

将 `COMx` 替换为设备串口。下载模式为按住侧面复位键约两秒，等待绿色 LED 闪烁后松开。刷写完成后，需要用户手动退出 download 模式，再打开上位机完成配置。

## 串口接口

USB CDC 使用 115200 波特率，每行传输一个 JSON 对象。命令包含 `id` 和 `cmd`，设备通过 `type: "reply"`、相同 `id` 和 `ok` 返回结果。命令上限为 4096 字节，接收时限为三秒。

| 命令 | 输入与结果 |
|---|---|
| `get_config` | 返回 `config`，包含 `password_set` |
| `configure` | 接收 `settings` 对象，验证并保存成功后返回 `config` |
| `refresh` | 提交一次查询；配置、连接或查询状态不满足条件时返回错误 |
| `get_state` | 返回 `state` 事件，包含当前查询状态及原始响应 |

`settings` 字段为 `ssid`、`host`、`connect_host`、`port`、`protocol`、`refresh_seconds`、`timeout_seconds`、`brightness`、`rotation`，以及可选的 `password`。省略 `password` 时使用相同 Wi-Fi 名称对应的已存密码；更换 Wi-Fi 名称时需要显式提供密码。`rotation` 使用 90 或 270。

设备每两秒发送一次轻量 `state` 事件。查询完成事件和 `get_state` 结果包含 `result.raw_json`。配置响应通过 `password_set` 表示密码是否已保存。

## 验证工具

Windows 原生查询工具与固件共用 `MinecraftMotd` 查询实现，通过 Winsock 连接真实服务器。本机构建脚本使用 Visual Studio 2022 Community 和已安装的 ArduinoJson。

```powershell
cmd /c minecraft-motd\tools\native-probe\build.cmd
minecraft-motd\.build\native\native-probe.exe --self-test
minecraft-motd\.build\native\native-probe.exe SERVER_HOST SERVER_PORT
python minecraft-motd/tools/verify_host.py SERVER_HOST SERVER_PORT
```

Windows 原生 UI 桌面模拟器与固件共用 `MotdUi` 与真实 `M5GFX` 渲染后端（2× 窗口，支持 `1-4` 预设、`A` 刷新、`B` 切页/长按翻转、`C` 切换充电状态），亦支持无头导出截图验证：

```powershell
cmd /c minecraft-motd\tools\ui-sim\build.cmd
minecraft-motd\.build\native\ui-sim.exe --dump-dir minecraft-motd\.build\ui-preview
```

如需重新生成 `MotdUi` 内置的 4-bpp BFF 英文字体数组（依赖 Node.js `npx lv_font_conv`）：

```powershell
python minecraft-motd/tools/gen_fonts.py
```

将 `SERVER_HOST` 和 `SERVER_PORT` 替换为待查询地址。`verify_host.py` 创建真实 Tk 窗口并隐藏窗口，验证两个方向、上位机字段、固件共用的配置校验代码和密码响应行为。

通过 `python minecraft-motd/tools/verify_usb.py COMx` 验证真实设备的配置读取与状态响应。添加 `--wait-query` 验证设备连接服务器并完成查询。

## 验证结果

2026-11-21（UI 重构版）完成以下验证：

- StickS3 新版 `MotdUi` 固件编译通过，程序占用 2,185,919 字节（约占应用分区的 65%）；全局静态内存占用 51,128 字节（15%，剩余 276,552 字节局部变量空间）。
- 新版固件已通过 `COM9` 刷写至 StickS3（ESP32-S3-PICO-1，写入 2,186,064 字节，`Hash of data verified` 通过）；因该机无法自动退出下载模式，刷写后需手动按侧边复位键退出并完成联机验收。
- `native-probe.exe --self-test` 通过：验证 JSON 组件样式继承、`§` 颜色/格式重置、`favicon` Base64 PNG 解码与越界安全丢弃、`players.sample` 解析与上限截断。
- `verify_host.py` 英文界面与 8 项边界校验通过。
- `ui-sim.exe --dump-dir` 真实 `M5GFX` 渲染导出 8 帧关键状态截图（含 3 页切换、真实 PNG Favicon 提取主色、无图标草方块兜底、中文阴影与边缘渐隐、`FLIP 180` 圆环蓄力与错误态）并通过人工视觉逐帧核验。

2026-11-04 初版实机历史验收：

- 英文固件实机 `get_config`、`get_state`、`Done` 状态查询（`CSJ's Server` / `1.20.1` / `2/20` / `31 ms`）及 USB 上位机读写均已实机通过。

硬件接口以本工作区 `kb/` 为依据，查询顺序参考 [PrismarineJS 的 Ping 实现](https://github.com/PrismarineJS/node-minecraft-protocol/blob/master/src/ping.js)。
