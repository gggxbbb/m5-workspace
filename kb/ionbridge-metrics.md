# IonBridge 本地 Metrics

> 本文记录小电拼 IonBridge 固件在局域网提供的 Prometheus 与 JSON 指标接口。
> 文档基于 2026-09-20 对一台 **CP02s / Ultra** 的实机采样，并用官方开源仓库核对枚举和单位。

## 结论速览

- Prometheus 端点：`http://<设备 mDNS 主机名>.local/metrics`
- JSON 端点：`http://<设备 mDNS 主机名>.local/metrics.json`
- 实机固件：ESP32 应用 `2.1.22`，产品族 `CP02s`，型号 `ultra`
- `/metrics` 返回 Prometheus text exposition format 0.0.4，`Content-Type` 为 `text/plain; version=0.0.4; charset=utf-8`
- `/metrics.json` 返回 UTF-8 JSON，`Content-Type` 为 `application/json`
- 两个端点在实测设备上均为明文 HTTP，局域网内无需认证即可读取；不要直接暴露到公网
- 电流单位为 **mA**，电压单位为 **mV**，功率需由两者换算：`W = mA × mV / 1,000,000`
- 端口 `id` 从 `0` 开始；该 Ultra 实机返回 5 个端口，即 `0` 到 `4`

## 证据等级与适用范围

本文用三个等级区分结论来源：

| 标记 | 含义 |
|---|---|
| **[实机]** | 从 CP02s / Ultra、ESP32 应用 2.1.22 的实时 HTTP 响应确认 |
| **[源码]** | 从官方 [ifanrx/IonBridge](https://github.com/ifanrx/IonBridge) 仓库确认 |
| **[推断]** | 由名称、数值关系或 Prometheus 约定推断，尚未在当前公开实现中核实 |

截至采样日，官方仓库 `main` 的 HEAD 为 `2372580e728ab4309634594cc38bda38b8d2d362`，仓库默认版本仍写作 `0.9.11`。公开主分支当前的 Web Server 没有包含实机 2.1.22 所使用的 `/metrics` 与 `/metrics.json` 处理器。因此：

- 快充协议、端口状态、mA/mV、功率预算和会话电量单位可由公开源码交叉核实。
- 指标名称、标签、Prometheus 类型和 JSON 字段集合来自实机响应。
- 不能据此断言 CP02、PA768s 或其他固件版本拥有完全相同的指标集合。

### 产品兼容性

| 产品族 / 型号 | Metrics 状态 | 说明 |
|---|---|---|
| `CP02s` / `ultra` | **已确认** | ESP32 应用 2.1.22 实机验证 `/metrics` 与 `/metrics.json` |
| `CP02` | **[未确认]** | 官方源码识别该产品族，但未做端点实测 |
| `PA768s` | **[未确认]** | 官方源码识别该产品族，但未做端点实测 |
| 其他产品 / 固件 | **[未确认]** | 需逐台核对 HTTP 状态、字段和单位 |

## 访问与发现

设备首页会公布自身的 mDNS 主机名。通常可直接访问：

```text
http://<mdns-hostname>.local/metrics
http://<mdns-hostname>.local/metrics.json
```

Windows 浏览器和同一局域网内支持 mDNS 的客户端通常可以解析 `.local`。Prometheus 所在的 Linux 主机不一定配置了 mDNS/NSS；正式采集建议给设备做 DHCP 地址保留并使用固定 IP，或先确认 Prometheus 主机能够稳定解析该 `.local` 名称。

快速检查：

```bash
curl --fail --silent --show-error \
  http://<mdns-hostname>.local/metrics

curl --fail --silent --show-error \
  http://<mdns-hostname>.local/metrics.json | jq .
```

响应中可能包含 Wi-Fi SSID、BSSID、任务名等局域网和固件内部信息。保存样本、提交 issue 或共享截图前应先脱敏。

## Prometheus 指标目录

### 端口

| 指标 | 类型 | 标签 | 单位 / 取值 | 含义 |
|---|---|---|---|---|
| `ionbridge_port_state` | gauge | `id` | 状态枚举 | 当前端口状态；见“端口状态枚举” |
| `ionbridge_port_fc_protocol` | gauge | `id` | 协议枚举 | 当前快充协议；见“快充协议枚举” |
| `ionbridge_port_current` | gauge | `id` | mA | 端口输出电流 **[源码+实机]** |
| `ionbridge_port_voltage` | gauge | `id` | mV | 端口输出电压 **[源码+实机]** |

Prometheus 端点不直接给出功率，可以派生：

```promql
# 每个端口的实时功率，单位 W
ionbridge_port_voltage * ionbridge_port_current / 1e6

# 全部端口的实时总功率，单位 W
sum(ionbridge_port_voltage * ionbridge_port_current) / 1e6
```

#### 端口状态枚举

公开源码中的 `PortStateType` 从 `UNKNOWN = -1` 开始，后续值连续递增。实机中 `ionbridge_port_state 1` 与 `/metrics.json` 的 `state: "ACTIVE"` 一致。

| 值 | 名称 | 说明 |
|---:|---|---|
| -1 | `UNKNOWN` | 未知 |
| 0 | `BOOTING` | 启动中 |
| 1 | `ACTIVE` | 端口启用、当前未连接设备也可能处于此状态 |
| 2 | `INACTIVE` | 端口停用 |
| 3 | `ATTACHED` | 已连接负载 |
| 4 | `DETACHING` | 正在断开 |
| 5 | `OPENING` | 正在开启 |
| 6 | `CLOSING` | 正在关闭 |
| 7 | `OVER_TEMP_WARNING` | 过温警告 |
| 8 | `OVER_TEMP_ALERT` | 过温告警 |
| 9 | `COOLING` | 冷却中 |
| 10 | `CHECKING` | 检查中 |
| 11 | `RECOVERING` | 恢复中 |
| 12 | `DEAD` | 端口故障 / 不可用 |
| 13 | `POWER_LIMITING` | 功率限制；源码字符串为 `LIMITED_POWER` |

状态枚举来自官方源码 [`components/port/include/port_state.h`](https://github.com/ifanrx/IonBridge/blob/2372580e728ab4309634594cc38bda38b8d2d362/components/port/include/port_state.h)。未来固件可能扩展枚举；未知值应保留原始数值，不要强行映射。

#### 快充协议枚举

| 值 | 协议 | 值 | 协议 |
|---:|---|---:|---|
| 0 | `NONE` | 11 | `TFCP` |
| 1 | `QC2` | 12 | `UFCS` |
| 2 | `QC3` | 13 | `PE1` |
| 3 | `QC3P` | 14 | `PE2` |
| 4 | `SFCP` | 15 | `PD_FIX5V` |
| 5 | `AFC` | 16 | `PD_FIXHV` |
| 6 | `FCP` | 17 | `PD_SPR_AVS` |
| 7 | `SCP` | 18 | `PD_PPS` |
| 8 | `VOOC1P0` | 19 | `PD_EPR_HV` |
| 9 | `VOOC4P0` | 20 | `PD_AVS` |
| 10 | `SVOOC2P0` | 255 | `NOT_CHARGING` |

枚举来自官方源码 [`components/chip_data_types/include/data_types.h`](https://github.com/ifanrx/IonBridge/blob/2372580e728ab4309634594cc38bda38b8d2d362/components/chip_data_types/include/data_types.h) 和 protobuf 定义。`NONE = 0` 不等于“没有连接”：实机曾出现端口已连接且有 5V 输出、协议值仍为 0；判断连接状态应优先使用 JSON 的 `attached` 或端口状态。

### 系统与运行时间

| 指标 | 类型 | 标签 | 单位 / 取值 | 含义 |
|---|---|---|---|---|
| `ionbridge_uptime_seconds` | gauge | 无 | s | 设备运行时间 **[实机]** |
| `ionbridge_boot_time_seconds` | gauge | `reset_reason` | s；语义待确认 | 名称看似“启动时间”，但实测值与 `uptime_seconds` 相同，并随运行增长；不要当作 Unix 时间戳 |
| `ionbridge_free_heap` | gauge | 无 | bytes | 当前空闲堆内存；JSON 同值 **[实机]** |

`reset_reason` 是数字形式的复位原因。当前响应没有附带文字映射，文档暂不把它强行解释成具体 ESP-IDF 枚举。

### 电源管理与轻睡眠

| 指标 | 类型 | 标签 | 单位 / 取值 | 含义 |
|---|---|---|---|---|
| `ionbridge_pm_enabled` | gauge | 无 | 0 / 1 | 电源管理是否启用 **[推断]** |
| `ionbridge_tickless_enabled` | gauge | 无 | 0 / 1 | FreeRTOS tickless idle 是否启用 **[推断]** |
| `ionbridge_light_sleep_configured` | gauge | 无 | 0 / 1 | 轻睡眠是否已配置 **[推断]** |
| `ionbridge_light_sleep_callbacks_supported` | gauge | 无 | 0 / 1 | 当前环境是否支持轻睡眠回调 **[推断]** |
| `ionbridge_light_sleep_callbacks_registered` | gauge | 无 | 0 / 1 | 回调是否已注册 **[推断]** |
| `ionbridge_light_sleep_duration_seconds_total` | counter | 无 | s | 累计轻睡眠时长 |
| `ionbridge_light_sleep_last_duration_seconds` | gauge | 无 | s | 最近一次轻睡眠时长 |
| `ionbridge_light_sleep_max_duration_seconds` | gauge | 无 | s | 本次启动以来最长的单次轻睡眠时长 **[推断]** |
| `ionbridge_light_sleep` | gauge | `state="sleep"` / `state="awake"` | 0～1 | 实测两条时间占比之和约为 1；按睡眠 / 唤醒占比使用 **[推断]** |
| `ionbridge_light_sleep_inhibited` | gauge | 无 | 0 / 1 | 当前是否阻止进入轻睡眠 **[推断]** |
| `ionbridge_light_sleep_wakeup_total` | counter | `code` | 次 | 按原始唤醒原因代码累计；实机输出 `code="0"` 至 `code="31"` |

唤醒代码目前没有随指标暴露文字说明。消费者应保留原始代码，并等官方实现或文档补齐后再映射。

### UART 与内部通信

| 指标 | 类型 | 单位 | 含义 |
|---|---|---|---|
| `ionbridge_uart_rx_overrun_total` | counter | 次 | UART 接收溢出次数 |
| `ionbridge_uart_parser_reset_total` | counter | 次 | UART 解析器重置次数 |
| `ionbridge_uart_resend_total` | counter | 次 | UART 重发次数 |
| `ionbridge_uart_sent_total` | counter | 次 | UART 发送总次数 |
| `ionbridge_uart_sent_failed_total` | counter | 次 | UART 发送失败次数 |
| `ionbridge_uart_stream_drop_total` | counter | 次 | UART 流数据丢弃次数 |
| `ionbridge_remote_interrupts_total` | counter | 次 | 远端 / 协处理器中断累计次数；“remote”的准确对象尚未由当前公开实现核实 |

这些 counter 会在设备重启后归零。告警应使用 `rate()` / `increase()`，不要直接对累计值设固定阈值。例如：

```promql
increase(ionbridge_uart_sent_failed_total[10m]) > 0
```

### MQTT

| 指标 | 类型 | 单位 / 取值 | 含义 |
|---|---|---|---|
| `ionbridge_mqtt_connected` | gauge | 0 / 1 | 当前 MQTT 是否连接 |
| `ionbridge_mqtt_connection_total` | counter | 次 | MQTT 建连累计次数 |
| `ionbridge_mqtt_message_tx_total` | counter | 条 | MQTT 已发送消息累计数 |
| `ionbridge_mqtt_message_rx_total` | counter | 条 | MQTT 已接收消息累计数 |

### FreeRTOS 任务

| 指标 | 类型 | 标签 | 单位 | 含义 |
|---|---|---|---|---|
| `ionbridge_task_stack_watermark_bytes` | gauge | `task` | bytes | 任务栈历史最低剩余量；越小越接近栈耗尽 |
| `ionbridge_task_cpu_percent` | gauge | `task` | % | 任务 CPU 使用率，取值按 0～100 解读 |

任务集合会随型号、编译选项和固件版本变化。实机样本包含 `httpd`、`IDLE`、`main`、`animation`、`port_mgr`、`mqtt_task`、`telemetry`、`mdns`、`uart_rx`、`uart_parse`、`pwr_alloc` 等；不要把完整任务列表写死为协议的一部分。

### Wi-Fi

| 指标 | 类型 | 标签 | 单位 | 含义 |
|---|---|---|---|---|
| `ionbridge_wifi_signal` | gauge | `ssid`, `bssid`, `channel` | dBm | 当前 Wi-Fi RSSI；JSON 字段名为 `rssi` **[源码+实机]** |

该指标的标签会泄露无线网络名称和接入点 MAC 地址，也可能在漫游或换网时产生新时间序列。若无需这些标签，可在 Prometheus 抓取时删除：

```yaml
metric_relabel_configs:
  - regex: 'ssid|bssid'
    action: labeldrop
```

## `/metrics.json` 字段

JSON 端点比 Prometheus 端点提供更完整的端口上下文，适合 StickS3 这类直接拉取并显示的客户端。

### 顶层结构

```json
{
  "ports": [],
  "system": {},
  "tasks": [],
  "wifi": {}
}
```

### `ports[]`

| 字段 | 类型 | 单位 / 取值 | 说明 |
|---|---|---|---|
| `id` | number | 0 起始 | 端口编号 |
| `active` | boolean | — | 端口已启用 / 可用；不表示已有负载连接 |
| `state` | string | 状态名 | `ACTIVE`、`ATTACHED` 等，比 Prometheus 数字枚举更适合 UI |
| `port_type` | string | `A` / `C` | 物理端口类型 |
| `attached` | boolean | — | 是否连接负载 |
| `charging_duration_seconds` | number | s | 当前充电会话持续时间 |
| `fc_protocol` | number | 协议枚举 | 与 `ionbridge_port_fc_protocol` 相同 |
| `current` | number | mA | 输出电流 |
| `voltage` | number | mV | 输出电压 |
| `vin_value` | number | mV | 端口控制芯片测得的输入电压 **[源码]** |
| `session_id` | number | — | 当前充电会话 ID |
| `session_charge` | number | µWs | 当前会话累计电量；硬件原始单位是微瓦秒，不是微瓦时 **[源码]** |
| `power_budget` | number | W | 当前端口功率预算 **[源码]** |
| `pd_status` | object / null | — | PD 详情；非 PD、未连接或功能未开放时可为 `null` |

换算会话电量：

```text
Wh  = session_charge / 3,600,000,000
mWh = session_charge / 3,600,000
```

`session_charge` 的单位由官方源码注释明确说明，见 [`components/chip_data_types/include/data_types.h`](https://github.com/ifanrx/IonBridge/blob/2372580e728ab4309634594cc38bda38b8d2d362/components/chip_data_types/include/data_types.h)。

### `system`

| 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|
| `chip` | string | — | ESP 芯片型号 |
| `cores` | number | 个 | CPU 核数 |
| `cpu_freq_mhz` | number | MHz | CPU 频率 |
| `idf_version` | string | — | ESP-IDF 版本 |
| `app_version` | string | — | ESP32 应用固件版本 |
| `boot_time_seconds` | number | s；语义待确认 | 实机值与 Prometheus 的 uptime 相同 |
| `reset_reason` | number | 原始枚举 | 复位原因 |
| `free_heap` | number | bytes | 当前空闲堆内存 |

### `tasks[]`

| 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|
| `name` | string | — | FreeRTOS 任务名 |
| `stack_watermark_bytes` | number | bytes | 栈历史最低剩余量 |
| `cpu_percent` | number | % | CPU 使用率，JSON 保留的精度高于 Prometheus 文本输出 |

### `wifi`

| 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|
| `ssid` | string | — | Wi-Fi 网络名，分享前需脱敏 |
| `bssid` | string | — | 接入点 MAC，分享前需脱敏 |
| `channel` | number | 信道号 | 当前信道 |
| `rssi` | number | dBm | 当前信号强度 |

## Prometheus 抓取示例

```yaml
scrape_configs:
  - job_name: ionbridge
    scrape_interval: 15s
    scrape_timeout: 5s
    metrics_path: /metrics
    static_configs:
      - targets:
          - '<mdns-hostname>.local'
    metric_relabel_configs:
      - regex: 'ssid|bssid'
        action: labeldrop
```

建议的基础查询：

```promql
# 抓取是否成功
up{job="ionbridge"}

# 每个端口功率（W）
ionbridge_port_voltage * ionbridge_port_current / 1e6

# 10 分钟内 UART 重发率
rate(ionbridge_uart_resend_total[10m])

# MQTT 断线
ionbridge_mqtt_connected == 0

# Wi-Fi 信号过弱
ionbridge_wifi_signal < -75

# 任务栈余量过低；阈值需按任务分别校准
ionbridge_task_stack_watermark_bytes < 512
```

## 已知歧义与后续核实清单

1. `ionbridge_boot_time_seconds` 在实机上与 uptime 同值并同步增长，名称与行为不一致。
2. 轻睡眠相关指标的完整实现尚未出现在当前公开主分支；其中布尔语义和 `light_sleep{state}` 的“占比”解释仍属推断。
3. `ionbridge_remote_interrupts_total` 的 remote 具体指向尚未确认。
4. `ionbridge_light_sleep_wakeup_total{code}` 的 0～31 映射尚未公开。
5. `/metrics.json` 的 `pd_status` 完整对象结构尚未在当前实机状态中捕获。
6. CP02、PA768s 及其他固件版本是否提供相同端点和字段，需要分别实测。
7. 当前 Prometheus 输出只有 `#TYPE`，没有 `#HELP`；客户端不应依赖帮助文本。

## 来源

- 官方开源入口：[go.ifanr.com/ionbridge-open-source](https://go.ifanr.com/ionbridge-open-source)
- 官方仓库：[ifanrx/IonBridge](https://github.com/ifanrx/IonBridge)
- 官方产品说明：[AI 小电拼 Mirror](https://thecandysign.com/cocan/zh/)
- 实机端点：CP02s / Ultra，ESP32 应用 2.1.22，采样日期 2026-09-20
