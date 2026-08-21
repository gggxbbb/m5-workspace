# rwr-alert — War Thunder RWR 复刻

基于 [esp32-rwr-alert](https://github.com/142428525/esp32-rwr-alert) 移植到 **M5StickS3**。

## 硬件映射(上游 → StickS3)

| 上游 | StickS3 | 说明 |
|---|---|---|
| SH1106 128×64 (U8g2) | ST7789 240×135 横屏 (M5GFX) | `setRotation(1)`,M5Canvas 双缓冲防闪烁 |
| 电容触摸 GPIO4 | KEY1 (G11) | `M5.BtnA` = 敌跟踪 |
| 电容触摸 GPIO13 | KEY2 (G12) | `M5.BtnB` = 敌导弹 |
| 触摸 GPIO4+13 同触 | KEY1+KEY2 同按 | = 敌导弹(上游语义) |
| GPIO14 + LEDC 蜂鸣器 | ES8311 板载扬声器 | `M5.Speaker.tone()`,音量 96/255 |
| `analogRead` 随机种子 | `esp_random()` | |
| Ticker + FreeRTOS task | FreeRTOS task | RADAR 单发自停,不再用 Ticker |

## 交互

- **KEY1 按住** → 「敌跟踪」告警(黄环 + 高低双音,松手停止)
- **KEY2 按住** → 「敌导弹」告警(红环 + 急促双音,松手停止)
- 空闲时 **1% 概率**随机触发雷达告警(白环 + 单发提示音,响完自停)
- 界面:同心圆 + 中心十字 + 方位短线 + 斜线扫描标记,告警时粗环闪烁(4 帧亮 4 帧灭)

## 编译

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 ./rwr-alert/sticks3
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 -p COMx ./rwr-alert/sticks3
```

烧录后需手动按侧面复位键重启(自动硬复位不可靠,见 kb/m5stick-s3.md)。

## 布局换算

上游圆心 (96,31) r29 → 新圆心 (150,67) r55,斜线坐标按 `(新 = 中心 + (旧 - 旧中心) * 55/29)` 运行时换算(整数 19/10),保留 `consts.h` 原始数据。
