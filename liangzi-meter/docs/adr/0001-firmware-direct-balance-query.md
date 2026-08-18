# ADR-0001：余额查询由固件直连 DeepSeek

- 日期：2026-08-18
- 状态：已接受

## 背景

设备需要显示账户余额，API key 由上位机配置。存在两条路径：

1. **固件直连**：API key 下发后存设备 NVS，固件自行 HTTPS 请求 `GET https://api.deepseek.com/user/balance`。
2. **上位机代理**：API key 只留存在上位机，由上位机定时查询余额并通过串口推送给固件显示。

## 决策

选择 **固件直连**。

- 设备在配置完成后可脱离上位机独立工作（桌面摆件场景，电脑不必常开）。
- 实现依赖：`WiFiClientSecure` + 内嵌根证书（DigiCert Global Root G2，证书链 api.deepseek.com → TrustAsia DV TLS RSA CA 2025 → DigiCert Global Root G2，2026-08-18 实测）+ ArduinoJson 解析。

## 后果

- **正面**：设备自包含；上位机只负责配置，无后台常驻查询任务。
- **负面（接受）**：
  - API key 以明文存于设备 NVS——USB 本地调试场景下攻击面可接受；上位机配置文件同样明文存于本机（`host/config.json`，已 gitignore）。
  - 需要维护根证书；证书轮换时需重刷固件。
  - 固件复杂度高于代理方案（TLS + JSON 解析）。
- **缓解**：余额刷新周期 5 分钟 + KEY1 手动刷新，避免频繁请求；查询失败保留上次数值并标记过期，不阻塞 UI。

## 备选方案

- 上位机代理：固件极简，但设备余额显示依赖上位机在线，不符合独立摆件定位，弃用。
