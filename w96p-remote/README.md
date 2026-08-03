# w96p-remote — W96P 风扇 BLE 遥控器

用 M5 设备（ESP32-S3）通过 BLE 控制 Witrn W96P/W66D 风扇。协议移植自 [w96p-control](https://github.com/gggxbbb/w96p-control)（web 版）的逆向文档 `docs/ble-protocol.md`，**不含 DFU**。

## 布局

```
w96p-remote/
├── lib/w96p/              ← 项目库：纯 C++ 协议层 + NimBLE client（协议 §8 规则全落实）
├── demo/demo.ino          ← 功能走查 demo：连上风扇把 client 所有功能过一遍（串口输出）
├── sticks3/sticks3.ino    ← StickS3 遥控器主程序（双键 + IMU 手势 + 彩屏 UI，8 界面状态机）
├── cardputer/             ← Cardputer-Adv 分支（占位，待实施）
├── tools/imu-calib/       ← IMU 轴向校准工具（串口 CSV + 屏幕十字点 + MARK 键）
└── docs/                  ← 设计文档（sticks3-remote-design.md，冻结状态）
```

## 状态

| 部分 | 状态 |
|---|---|
| 协议层 / client 库 | ✅ 完成，双 FQBN 编译验证；未上真机 |
| demo | ✅ 编译通过，待真机跑（串口 115200） |
| sticks3 遥控器 | ✅ 编译通过（含 MOCK 模式），待真机联调：IMU 轴向/阈值、中文缺字、连接时序 |
| cardputer 分支 | 🔲 未开工 |

## 编译

```bash
# StickS3 遥控器（MOCK 模式加 --build-property compiler.cpp.extra_flags=-DW96P_MOCK=1）
arduino-cli compile --libraries ./w96p-remote/lib --fqbn m5stack:esp32:m5stack_sticks3 w96p-remote/sticks3

# demo / 校准工具
arduino-cli compile --libraries ./w96p-remote/lib --fqbn m5stack:esp32:m5stack_sticks3 w96p-remote/demo
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 w96p-remote/tools/imu-calib
```

## 真机联调路径（设计文档 §调试）

1. **demo** 验证 client 层 vs 真风扇（零 UI）
2. **tools/imu-calib** 标定手势轴向与阈值
3. **sticks3 MOCK 模式** 过 UI/按键/中文字体（零 BLE）
4. 合体全流程

数据采信原则（用户裁定）：电池只信电压/电流（固件容量字段是占位数据）；不做 SOC 推算；不显示温度（PMIC 结温非电池温度）。详见 `docs/sticks3-remote-design.md` §1。
