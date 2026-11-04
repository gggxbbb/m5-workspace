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

1. 通过 USB 连接 StickS3，选择串口，点击“连接设备”。
2. 填写 Wi-Fi 名称、密码、服务器地址和端口，选择屏幕方向。
3. 点击“保存到设备”。设备保存设置并连接 Wi-Fi，随后开始查询。
4. 在“响应信息”查看耗时和服务器原始 JSON；点击“立即查询”刷新结果。

设备将设置保存在 NVS 中，重新上电时加载设置。上位机通过 USB 串口配置设备，Wi-Fi 与服务器字段由用户填写。密码输入框留空表示保留设备已存密码，选择“开放网络”表示使用空密码。

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

DNS 解析使用 ESP32 网络库的解析时限。域名使用 SRV 记录时，在“连接目标”填写 SRV 的目标域名，在“服务器端口”填写 SRV 的端口。

## 设备界面

主页面显示查询状态、电量、服务器地址、Ping/Pong 延迟、彩色 MOTD、在线人数、服务器版本和结果时间。中文使用 M5GFX 内置字库。过长的 MOTD 行水平滚动，多行内容按两行分页；翻页时长随行宽增加。

| 操作 | 行为 |
|---|---|
| 短按 A 键 | 刷新查询；Wi-Fi 连接失败时重新连接 |
| 短按 B 键 | 切换 MOTD 页面与耗时详情页面 |
| 长按 B 键 700 毫秒 | 切换两个横屏方向并保存设置 |

查询由独立 FreeRTOS 任务执行。修改配置、断开 Wi-Fi 后，设备丢弃旧查询结果。状态响应有效且 Pong 失败时，界面保留已收到的 MOTD，并标记 Pong 失败。每次失败均显示当前阶段和错误信息。

MOTD 支持字符串、JSON `text`、`extra`、数组、标准颜色、十六进制颜色和 `§` 颜色代码。`translate` 显示服务器返回的翻译键。字体样式使用统一字形。状态包限制为 128 KiB，MOTD 最多 64 行、512 个文字片段；超出限制时报告错误。

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
minecraft-motd/.build/native/native-probe.exe SERVER_HOST SERVER_PORT
python minecraft-motd/tools/verify_host.py SERVER_HOST SERVER_PORT
```

将 `SERVER_HOST` 和 `SERVER_PORT` 替换为待查询地址。`verify_host.py` 创建真实 Tk 窗口并隐藏窗口，验证两个方向、上位机字段、固件共用的配置校验代码和密码响应行为。

通过 `python minecraft-motd/tools/verify_usb.py COMx` 验证真实设备的配置读取与状态响应。

## 验证结果

2026-11-04 完成以下验证：

- StickS3 固件编译通过，程序占用 1,806,811 字节，约为应用分区的 54%；静态数据占用 49,616 字节。
- 固件通过 COM9 刷写到 ESP32-S3-PICO-1，写入数据校验通过。
- 实机 `get_config`、`get_state` 返回成功，配置与运行状态读取通过。
- 共用查询代码通过真实服务器查询，收到 `CSJ's Server`、版本 `1.20.1`、人数 `2/20`，测得 Ping/Pong 为 31 毫秒。
- 上位机窗口初始化、两个方向、中文及空格名称、配置保存格式、密码保留和八项边界校验通过。

设备已通过上位机完成网络与服务器配置，用户确认实机服务器查询正常。

硬件接口以本工作区 `kb/` 为依据，查询顺序参考 [PrismarineJS 的 Ping 实现](https://github.com/PrismarineJS/node-minecraft-protocol/blob/master/src/ping.js)。
