# ADR-0005：重力感应旋屏 + 默认屏幕方向配置

- 日期：2026-08-21
- 状态：已接受

## 背景

liangzi-meter 是桌面摆件，固定竖屏（`setRotation(0)`）。用户希望设备被拿起/翻转/侧放时显示自动跟随重力方向旋转（4 个方向），同时上位机可指定**开机默认屏幕方向**与**旋屏总开关**，且默认行为保持不变（固定竖屏，不引入意外旋转）。

## 决策

1. **配置**：协议 `config` 新增两个字段，固件 NVS 持久化：
   - `screen_rotation`（int 0..3）：开机默认方向，`0=竖屏正常 / 1=顺时针90°(横屏) / 2=180° / 3=逆时针90°(横屏)`，非法值回退 0。
   - `auto_rotate`（bool，默认 false）：重力感应旋屏总开关。**关闭 = 完全保持现状**（固定 `screen_rotation` 方向）。
2. **判定**：`M5.Imu.update()` + `getAccel`（单位 g），仅用加速度 X/Y 平面。主导轴阈值 0.55g、交叉轴上限 0.35g（姿态倾斜时不切换），**同一目标方向连续确认 3 次**（间隔 500ms，共 1.5s）才切换——防抖。
3. **轴映射（真机标定）**：引用同工作区 w96p-remote 项目 `imu-calib` 工具的三姿势实测（2026-08-03，见 `w96p-remote/docs/sticks3-remote-design.md`）：M5Unified 的 BMI270 驱动轴系 ≠ 官方 IMU 图，绕 Z 旋转 90°——**驱动 +X = 朝 USB 端（设备底部）、驱动 +Y = 设备右侧、驱动 +Z = 屏幕外**。设备长轴（顶部/摄像头）= −X_driver；静止时加速度计读比力（指向上方）。则：
   - 顶朝上 `ax<−0.55` → rotation 0；顶朝下 `ax>+0.55` → rotation 2
   - 顶朝右 `ay<−0.55` → rotation 3；顶朝左 `ay>+0.55` → rotation 1
   - （用户实测 2026-08-21 两轮修正：① 旧版按官方图轴系推理导致「顶朝上显示 90°」；② 横屏左右反 → 对调为「顶朝右 → 3 / 顶朝左 → 1」。M5GFX rotation 1 = 内容顶朝左、rotation 3 = 内容顶朝右。）
4. **旋转应用**：`M5.Display.setRotation(rot)` 后 **canvas 必须 deleteSprite 重建**（宽高随方向互换 135×240 ↔ 240×135），并置 `fullRedraw`。峰谷大字 `peakSprite` 尺寸固定（108×36）与方向无关，无需重建。
5. **横屏布局**：渲染函数按 `canvas.width() > canvas.height()` 分支。横屏（240×135）主页面 = **左右分栏**：峰谷大字靠设备底部（USB 端）一侧（rotation 1 → 右栏 / rotation 3 → 左栏），时间/倒计时/余额在另一栏；详情页 = 标题+返回 / 余额大字 / 状态 / 2 列×3 行详情（WiFi+IP、时段+电量、方向+固件），SSID 截断 8 字符防溢出右列。
5. **开机顺序**：`cfgLoad` 之后才 `setRotation(cfg.screenRotation)` 并创建 canvas（尺寸跟随方向）；`M5.begin()` 默认已初始化 IMU（`M5Unified.cpp:2866`），无需手动 `Imu.begin()`，用 `M5.Imu.isEnabled()` 兜底（IMU 不可用时旋屏静默禁用）。
6. **状态上报**：`state` 回显 `rotation`（当前方向）与 `auto_rotate`；详情页新增「方向 N° 自动/固定」行。

## 涉及范围

- 固件：`sticks3/config.h`（字段 + NVS key `rot`/`autorot`）、`sticks3/sticks3.ino`（`manageRotation` / `applyRotation` / 协议 / setup 顺序）
- 上位机：`host/src/liangzi_meter/config.py`、`ui.py`（屏幕方向下拉框 + 旋屏开关 + 状态区方向行）
- 协议：`config` 新增 `screen_rotation` / `auto_rotate`，`state` 回显（ADR-0002 更新）

## 后果

- **正面**：手持/翻转时屏幕始终正向；上位机可固化桌面摆放方向；默认关闭保证老用户无感。
- **负面（接受）**：横屏为紧凑布局（省略提示行/NTP 行），非竖屏完整版；IMU 每 500ms 采样一次增加极少量 I2C 负载；旋转瞬间有一帧全量重绘（1.5s 防抖内不会频繁触发）。
- 向后兼容：旧固件忽略未知字段；新固件缺失字段回退默认（固定竖屏）。

## 备选方案

- **陀螺仪角速度判转**：响应更快但需要积分/阈值调参，静止漂移问题多，且摆件场景只需「最终姿态」，加速度方向足够，否决。
- **四方向独立可配映射**：过度设计——真机标定后按 w96p 数据修正两行映射即可。
- **旋转时重建 peakSprite**：无必要，其尺寸与方向无关，否决。
