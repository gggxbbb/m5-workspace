// sticks3-remote.ino — W96P 风扇遥控器 (M5StickS3)
// 交互设计: w96p-remote/docs/sticks3-remote-design.md (设计冻结稿, 本文档为准)
// BLE client: lib/w96p (w96p_client.h) — 本文件不修改 client 层
//
// 采信原则(设计 §1): 只呈现电压/电流, 不做 SOC 推算, 不显示任何温度。
#include <M5Unified.h>
#include <w96p_client.h>
#include <esp_mac.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ============================== 编译开关 ==============================
// 1 = 无 BLE/无风扇的界面彩排模式: 填充假快照 + 假扫描列表, 全部界面可导航
#ifndef W96P_MOCK
#define W96P_MOCK 0
#endif

// ============================== 常量(真机调参) ==============================
struct Vec3 { float x, y, z; };

// TODO-calibrate: 设备长轴 L 在设备(IMU)坐标系中的方向(硬件固定)。
// 真机标定(2026-08-03 imu-calib): 驱动轴系 ≠ 官方图, 绕 Z 旋转 90°:
//   驱动 +X = 图 -Y(朝 USB 端), 驱动 +Y = 图 +X(设备右侧), 驱动 +Z = 图 +Z
// 长轴(朝设备顶端) = -X_driver。注意加速度计静止时读支撑力方向("上")。
static constexpr Vec3    DEVICE_LONG_AXIS   = { -1.0f, 0.0f, 0.0f };

static constexpr float    G_TO_MS2          = 9.80665f;  // BMI270 accel 单位为 g, 转 m/s²
static constexpr float    ROLL_TRIGGER_DEG  = 25.0f;     // 翻滚: 触发角(绕长轴, 设计 §4.1 v3)
static constexpr float    ROLL_REARM_DEG    = 10.0f;     // 翻滚: 回中重武装角
static constexpr uint32_t ROLL_REARM_MS     = 150;       // 翻滚: 回中确认时长
static constexpr uint32_t SHAKE_PITCH_COOLDOWN_MS = 600; // 换挡后 PWM 冷却(翻滚附带俯仰不调速)
static constexpr uint32_t GEAR_DISP_MS      = 800;       // 换挡后手势屏显示 GEAR 顶替 PWM 时长
static constexpr float    PITCH_DEADZONE_DEG= 8.0f;      // 举/摆: 死区(设计 §4.2)
static constexpr float    PITCH_FULL_DEG    = 45.0f;     // 举/摆: 满倾角度
static constexpr float    SPEED_RATE_PCT_S  = 30.0f;     // 举/摆: 满倾速率 %/s
static constexpr uint32_t SPEED_WRITE_MIN_MS= 150;       // setSpeed 写节流
static constexpr uint8_t  SPEED_WRITE_MIN_PCT = 2;       // setSpeed 最小变化
static constexpr float    EMA_ALPHA         = 0.3f;      // 低通系数
static constexpr uint32_t GESTURE_HOLD_MS   = 400;       // BtnA 按住进手势
static constexpr uint32_t BTNB_LONG_MS      = 800;       // BtnB 长按 panic 回看板
static constexpr uint16_t TURBO_TOTAL_S     = 199;       // 原型期默认(设计 §7.5, 后续读 FFF8)
static constexpr uint32_t SCAN_SECONDS      = 8;
static constexpr uint32_t CONNECT_GIVEUP_MS = 12000;     // CONNECTING 超时 → 离线看板
static constexpr uint8_t  CONN_MAX_DEV_ROWS = 6;         // 连接管理页最多显示设备数(屏幕高度限制)
static constexpr uint32_t GEAR_FLASH_MS     = 150;       // 手势换档色块时长

// 配色语义(设计 §5.0, RGB565)
static constexpr uint16_t C_GREEN = 0x07E0;  // 正常/在线/开启
static constexpr uint16_t C_GREY  = 0x4208;  // 关闭/无数据/提示
static constexpr uint16_t C_CYAN  = 0x07FF;  // 可调值/强调
static constexpr uint16_t C_ORANGE= 0xFD20;  // 警告/高能(Turbo)
static constexpr uint16_t C_RED   = 0xF800;  // 报警/错误
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_BLACK = 0x0000;

static constexpr int SCR_W = 135, SCR_H = 240;

// ============================== 状态与数据 ==============================
enum Screen : uint8_t { SCR_CONNECTING, SCR_DASHBOARD, SCR_MENU, SCR_ADJUST,
                        SCR_GESTURE, SCR_TURBO_DASH, SCR_DETAILS, SCR_CONN_MGMT };
static Screen scr = SCR_CONNECTING;
static bool   dirty = true;              // 脏标记: 仅脏时重绘(设计 §6)

static w96p::Client   cli;
static w96p::Snapshot snap;              // client 500ms 轮询快照(mock 时为假数据)
static uint8_t fwMarker = 0;             // DFU 读到的固件版本(major*10+minor)
static bool    fwValid = false;

static bool online        = false;
static bool manualOffline = false;       // 手动断开=true, 抑制自动重扫
static bool autoConnect   = false;       // 初次连接/意外掉线后: 发现即连
static bool pendingConnect= false;       // 连接进行中(失败 → "连接失败")
static char connMsg[16]   = "";          // "扫描中…"/"连接中…"/"连接失败"
static uint32_t scanUntilMs = 0;
static uint32_t connectStartMs = 0;

// BLE 事件(回调里只置标志, 主循环处理 — onConn/onFound 可能来自 BLE 任务上下文)
static volatile bool evtConnPending = false;
static volatile bool evtConnValue   = false;
static volatile bool evtFoundPending= false;

static char connectedName[32] = "";
static char connectedAddr[18] = "";
static int  connectedRssi     = 0;

// 菜单(设计 §3 循环顺序)
enum MenuType : uint8_t { M_PERCENT, M_MINUTES, M_TOGGLE, M_LIGHT, M_VIEW, M_BACK };
enum ConnKind : uint8_t { CI_RESCAN, CI_DISCONN, CI_DEVICE, CI_BACK };
struct MenuItem { const char* name; MenuType type; };
static const MenuItem menu[8] = {
    { "风速",   M_PERCENT }, { "定时",   M_MINUTES }, { "自然风", M_TOGGLE },
    { "Turbo",  M_TOGGLE  }, { "灯光",   M_LIGHT   }, { "状态详情", M_VIEW },
    { "连接管理", M_VIEW   }, { "返回",   M_BACK    },
};
static int menuIdx   = 0;
static int adjustVal = 0;                // 调节态暂存(保存才下发)
static int detailsPage = 0;              // 0=实时 1=设备
static int connSel   = 0;

// 快照没有的本地状态(GEA/LGT 无回读特征, 只跟踪本机下发值)
static uint8_t gearEst  = 0;             // 0=未知
static uint8_t lightEst = 0xFF;          // 0xFF=未知
static uint8_t gearSpeeds[4] = { 10, 35, 70, 100 };  // 档位校准转速%, 连接时经 FFF7 更新
static bool    calibValid = false;

// 手势换档/到头闪屏
static uint16_t flashColor = 0;
static uint32_t flashUntilMs = 0;

// ============================== Mock 数据 ==============================
#if W96P_MOCK
static w96p::Client::Found mockFound[2] = {
    { "AA:BB:CC:00:3F:2A", "W96P-3F2A", -58 },
    { "AA:BB:CC:00:7B:11", "W96P-7B11", -71 },
};
static void mockFillSnapshot() {
    snap = {};
    snap.valid = true;
    snap.speed = 65;
    snap.battery.voltageMv = 3950; snap.battery.currentMa = -412;
    snap.motor.currentMa = 320; snap.motor.block = false; snap.motor.voltageMv = 5100;
    snap.power.vbusMv = 5020; snap.power.vbusMa = 1200;
    snap.power.powC = 1; snap.power.powSta = 2;
    snap.power.cOutEnabled = snap.power.cInEnabled = snap.power.cHiEnabled = true;
    snap.timerRemainS = 0; snap.natureOn = 0; snap.turboRemainS = 0;
    snap.updatedMs = millis();
    online = true;
    strcpy(connectedName, "W96P-3F2A");
    strcpy(connectedAddr, mockFound[0].addr);
    connectedRssi = mockFound[0].rssi;
}
#endif

// ============================== BLE 写封装(mock/真机统一入口) ==============================
static void fanSetPower(uint8_t gear) {
#if W96P_MOCK
    gearEst = gear;                      // 风扇无档位记忆: 0(off)是显式状态(2026-08-03 裁定)
    if (gear == 0) snap.speed = 0;
    else if (snap.speed == 0) snap.speed = w96p::kProfileW96P.gearDefaults[gear - 1];
#else
    if (cli.setPower(gear)) gearEst = gear;
#endif
    dirty = true;
}
static void fanSetSpeed(uint8_t pct) {
#if W96P_MOCK
    snap.speed = pct;
#else
    cli.setSpeed(pct);
#endif
    dirty = true;
}
static void fanSetTimerMin(uint16_t m) {
#if W96P_MOCK
    snap.timerRemainS = m * 60;
#else
    cli.setTimerMinutes(m);
#endif
    dirty = true;
}
static void fanSetNature(bool on) {
#if W96P_MOCK
    snap.natureOn = on ? 1 : 0;
#else
    cli.setNatureWind(on);
#endif
    dirty = true;
}
static void fanSetTurbo(bool on) {
#if W96P_MOCK
    snap.turboRemainS = on ? TURBO_TOTAL_S : 0;
#else
    cli.setTurbo(on);
#endif
    dirty = true;
}
static void fanSetLight(uint8_t lv) {
#if W96P_MOCK
#else
    cli.setLight(lv);
#endif
    lightEst = lv;  // 灯光无回读, 只信本机下发值
    dirty = true;
}

// ---- 扫描/连接封装 ----
static int foundN() {
#if W96P_MOCK
    return 2;
#else
    return cli.foundCount();
#endif
}
static const w96p::Client::Found* foundAt(int i) {
#if W96P_MOCK
    return &mockFound[i];
#else
    return &cli.foundList()[i];
#endif
}
static void doStartScan() {
    strcpy(connMsg, "扫描中…");
    scanUntilMs = millis() + (W96P_MOCK ? 1500 : SCAN_SECONDS * 1000);
#if !W96P_MOCK
    cli.startScan(SCAN_SECONDS);
#endif
    dirty = true;
}
static void doConnectIndex(int i) {
    const w96p::Client::Found* f = foundAt(i);
    strcpy(connMsg, "连接中…");
    pendingConnect = true;
#if W96P_MOCK
    online = true; manualOffline = false; pendingConnect = false; connMsg[0] = 0;
    strncpy(connectedName, f->name, sizeof(connectedName) - 1);
    strncpy(connectedAddr, f->addr, sizeof(connectedAddr) - 1);
    connectedRssi = f->rssi;
    mockFillSnapshot();
    strncpy(connectedName, f->name, sizeof(connectedName) - 1);
    scr = SCR_DASHBOARD;
#else
    cli.stopScan();
    if (!cli.connectIndex(i)) { pendingConnect = false; strcpy(connMsg, "连接失败"); }
#endif
    dirty = true;
}
static void doDisconnect() {
#if W96P_MOCK
    online = false; snap.valid = false;
    connectedName[0] = connectedAddr[0] = 0;
#else
    cli.disconnect();
#endif
    manualOffline = true;
    connMsg[0] = 0;
    dirty = true;
}

// ============================== 手势(双通道, 体感参考系, 设计 §4) ==============================
static Vec3 vsub(Vec3 a, Vec3 b)  { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static float vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 vcross(Vec3 a, Vec3 b){ return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float vlen(Vec3 a)         { return sqrtf(vdot(a, a)); }
static Vec3 vnorm(Vec3 a)         { float l = vlen(a); return l > 1e-6f ? Vec3{ a.x / l, a.y / l, a.z / l } : Vec3{ 0, 0, 0 }; }

static struct {
    Vec3 g0, down, right, fwd;   // 进入时建立的体感基(m/s² 与单位向量)
    bool basisValid;             // 退化(竖直握持)且无历史基 → false, 显示提示
    float pBase;                 // g0·fwd (常量, 俯仰量度基准)
    float gLen;                  // |g0|
    Vec3 lp;                     // 低通后的当前重力
    float emaP;                  // 俯仰量度 EMA
    float speedF;                // 速率模式浮点风速
    bool     rollArmed;          // 翻滚武装(滞回: 触发后须回中到 ROLL_REARM_DEG 内持续 150ms)
    uint32_t settleStartMs;      // 回中计时起点(0=未开始)
    uint32_t pitchSuppressUntilMs; // 甩后 PWM 冷却截止
    uint32_t gearDispUntilMs;    // 甩后 GEAR 顶替显示截止
    int8_t   lastShakeGear;      // 最后一次甩到的档位(顶替显示用)
    uint32_t speedSyncMs;        // 换挡后等待设备回读真实转速的起始时刻(0=无挂起)
    uint8_t lastSent; uint32_t lastSentMs;
    uint32_t lastTickMs;
} g;
static Vec3 lastRight, lastFwd, lastDown;   // 退化情形沿用上一次的基
static bool hasLastBasis = false;

static Vec3 readAccelMs2() {
    M5.Imu.update();
    auto d = M5.Imu.getImuData();
    return { d.accel.x * G_TO_MS2, d.accel.y * G_TO_MS2, d.accel.z * G_TO_MS2 };
}

static void enterGesture() {
    // 进入瞬间: 100ms 滑动均值取重力向量 g0 (体感"下")
    Vec3 sum = { 0, 0, 0 };
    for (int i = 0; i < 10; i++) {
        Vec3 a = readAccelMs2();
        sum.x += a.x; sum.y += a.y; sum.z += a.z;
        delay(10);
    }
    g.g0 = { sum.x / 10, sum.y / 10, sum.z / 10 };
    g.gLen = vlen(g.g0);
    g.down = vnorm(g.g0);

    if (fabsf(vdot(DEVICE_LONG_AXIS, g.down)) > 0.95f) {
        // 竖直握持退化: cross 无定义 → 沿用上一次的基; 首次则提示
        if (hasLastBasis) {
            g.down = lastDown; g.right = lastRight; g.fwd = lastFwd;
            g.basisValid = true;
        } else {
            g.basisValid = false;
        }
    } else {
        // 体感基(静止时 accel 读"上"): right = cross(L, up) 指向用户右手
        // fwd = cross(up, right) —— 真机实测(2026-08-03) cross(right,up) 上举反向, 取反
        g.right = vnorm(vcross(DEVICE_LONG_AXIS, g.down));
        g.fwd   = vnorm(vcross(g.down, g.right));
        g.basisValid = true;
        lastDown = g.down; lastRight = g.right; lastFwd = g.fwd;
        hasLastBasis = true;
    }
    g.pBase = vdot(g.g0, g.fwd);
    g.lp = g.g0;
    g.emaP = 0;
    g.speedF = snap.valid ? snap.speed : 0;
    g.rollArmed = true;
    g.settleStartMs = 0;
    g.pitchSuppressUntilMs = 0;
    g.gearDispUntilMs = 0;
    g.lastShakeGear = 0;
    g.speedSyncMs = 0;
    g.lastSent = snap.valid ? snap.speed : 0;
    g.lastSentMs = 0;
    g.lastTickMs = millis();
    flashUntilMs = 0;

    // Stage 1 调试: 校准值串口打印(设计 §7.3, 用于标定 DEVICE_LONG_AXIS)
    Serial.printf("[gesture] g0=(%.2f,%.2f,%.2f) |g|=%.2f down=(%.2f,%.2f,%.2f)\n",
                  g.g0.x, g.g0.y, g.g0.z, g.gLen, g.down.x, g.down.y, g.down.z);
    Serial.printf("[gesture] right=(%.2f,%.2f,%.2f) fwd=(%.2f,%.2f,%.2f) basisValid=%d\n",
                  g.right.x, g.right.y, g.right.z, g.fwd.x, g.fwd.y, g.fwd.z, (int)g.basisValid);

    scr = SCR_GESTURE;
    dirty = true;
}

static void exitGesture() {
    // 补发终值(设计 §4: 松手时风扇保持当前值)
    uint8_t finalPct = (uint8_t)(g.speedF + 0.5f);
    if (finalPct != g.lastSent) fanSetSpeed(finalPct);
    scr = SCR_DASHBOARD;
    dirty = true;
}

static void gestureTick() {
    Vec3 a = readAccelMs2();
    uint32_t now = millis();
    float dt = (now - g.lastTickMs) / 1000.0f;
    g.lastTickMs = now;
    if (!g.basisValid) { dirty = true; return; }

    // 换挡后挂起的转速再同步: 轮询值必须等于档位校准值才落袋(防读到换挡前的陈旧
    // FFF3 覆盖正确值), 超 2s 等不到则接受现值兜底(2026-08-03 真机反馈)
    if (g.speedSyncMs != 0 && snap.valid && g.lastShakeGear >= 0 && g.lastShakeGear <= 4) {
        uint8_t expect = g.lastShakeGear == 0 ? 0 : gearSpeeds[g.lastShakeGear - 1];
        if (snap.speed == expect || now - g.speedSyncMs >= 2000) {
            g.speedF = snap.speed;
            g.lastSent = snap.speed;
            g.speedSyncMs = 0;
        }
    }

    // --- 重力低通(两个通道共用) ---
    g.lp.x += EMA_ALPHA * (a.x - g.lp.x);
    g.lp.y += EMA_ALPHA * (a.y - g.lp.y);
    g.lp.z += EMA_ALPHA * (a.z - g.lp.z);

    // --- 通道1: 翻滚换挡(绕长轴; 重力在 right 轴投影 → 翻滚角, 滞回状态机) ---
    // 向左翻(左侧下沉): 测得"上"偏向右侧 → lp·right > 0 → 降档; 向右翻 → 升档
    float rollSin = constrain(vdot(g.lp, g.right) / g.gLen, -1.0f, 1.0f);
    float rollDeg = asinf(rollSin) * 57.2958f;
    if (!g.rollArmed) {
        if (fabsf(rollDeg) < ROLL_REARM_DEG) {
            if (g.settleStartMs == 0) g.settleStartMs = now;
            if (now - g.settleStartMs >= ROLL_REARM_MS) g.rollArmed = true;
        } else {
            g.settleStartMs = 0;
        }
    }
    if (g.rollArmed && fabsf(rollDeg) > ROLL_TRIGGER_DEG) {
        int gear = gearEst;    // 风扇无记忆: 0(off)是序列起点, 范围 0-4(2026-08-03 裁定)
        int next = gear + (rollDeg > 0 ? -1 : 1);           // 左翻降/右翻升
        if (next >= 0 && next <= 4) {
            fanSetPower((uint8_t)next);
            flashColor = (next > gear) ? C_CYAN : C_ORANGE; // 升档闪青/降档闪橙
            g.lastShakeGear = (int8_t)next;
            // 档位校准值即真实转速(0档=0): 立即覆盖本地(轮询 resync 仍作兜底)
            uint8_t spd = next == 0 ? 0 : gearSpeeds[next - 1];
            g.speedF = spd;
            g.lastSent = spd;
        } else {
            flashColor = C_RED;                             // 到头闪红
            g.lastShakeGear = (int8_t)gear;
        }
        flashUntilMs = now + GEAR_FLASH_MS;
        g.rollArmed = false;                                // 缴械, 等回中重武装
        g.settleStartMs = 0;
        g.pitchSuppressUntilMs = now + SHAKE_PITCH_COOLDOWN_MS;  // 翻滚附带俯仰不调速
        g.gearDispUntilMs = now + GEAR_DISP_MS;                  // 手势屏顶替显示 GEAR
        g.speedSyncMs = now;   // 换挡后等轮询回读真实 PWM
        Serial.printf("[gesture] roll %.1f deg gear %d->%d\n", rollDeg, gear, next);
    }

    // --- 通道2: 举/摆(速率模式调速, 体感俯仰稳态) ---
    if (now < g.pitchSuppressUntilMs) { dirty = true; return; }   // 换挡后冷却
    if (g.pitchSuppressUntilMs != 0) {   // 冷却刚结束: 重定基线, 翻滚带入的俯仰不残留
        g.pBase = vdot(g.lp, g.fwd);
        g.emaP = 0;
        g.pitchSuppressUntilMs = 0;
    }
    float p = vdot(g.lp, g.fwd) - g.pBase;                  // 俯仰量度(m/s² 投影)
    g.emaP += EMA_ALPHA * (p - g.emaP);

    float deadzone = sinf(PITCH_DEADZONE_DEG * 0.0174533f) * g.gLen;
    float full     = sinf(PITCH_FULL_DEG * 0.0174533f) * g.gLen;
    if (fabsf(g.emaP) > deadzone) {
        float rate = (g.emaP / full) * SPEED_RATE_PCT_S;    // %/s
        g.speedF += rate * dt;
        if (g.speedF < 0) g.speedF = 0;
        if (g.speedF > 100) g.speedF = 100;
        uint8_t pct = (uint8_t)(g.speedF + 0.5f);
        if (abs((int)pct - (int)g.lastSent) >= SPEED_WRITE_MIN_PCT
            && now - g.lastSentMs >= SPEED_WRITE_MIN_MS) {
            fanSetSpeed(pct);
            g.lastSent = pct; g.lastSentMs = now;
        }
    }
    dirty = true;   // 手势屏实时刷新
}

// ============================== 按键分发(设计 §6) ==============================
static bool fanIsOn() { return online && snap.valid && snap.speed > 0; }

static int currentValOf(int idx) {
    switch (menu[idx].type) {
    case M_PERCENT: return snap.valid ? snap.speed : 0;
    case M_MINUTES: return snap.timerRemainS / 60;
    case M_TOGGLE:  return (idx == 2) ? (snap.natureOn ? 1 : 0) : (snap.turboRemainS > 0 ? 1 : 0);
    case M_LIGHT:   return lightEst == 0xFF ? 0 : lightEst;
    default:        return 0;
    }
}
static int stepDown(MenuType t, int v) {
    switch (t) {
    case M_PERCENT: return v >= 5 ? v - 5 : 0;
    case M_MINUTES: return v >= 30 ? v - 30 : 0;
    case M_TOGGLE:  return !v;
    case M_LIGHT:   return (v + 1) % 5;    // 短按循环 0-4
    default:        return v;
    }
}
static int stepUp(MenuType t, int v) {
    switch (t) {
    case M_PERCENT: return v <= 95 ? v + 5 : 100;
    case M_MINUTES: return v <= 450 ? v + 30 : 480;
    case M_TOGGLE:  return !v;
    case M_LIGHT:   return (v + 4) % 5;
    default:        return v;
    }
}
static void commitAdjust() {
    switch (menu[menuIdx].type) {
    case M_PERCENT: fanSetSpeed((uint8_t)adjustVal); break;
    case M_MINUTES: fanSetTimerMin((uint16_t)adjustVal); break;
    case M_TOGGLE:  (menuIdx == 2) ? fanSetNature(adjustVal) : fanSetTurbo(adjustVal); break;
    case M_LIGHT:   fanSetLight((uint8_t)adjustVal); break;
    default: break;
    }
}

// 连接管理动态列表: [重新扫描] [断开(在线时)] [设备*n] [返回]
static int connItemCount() { return 1 + (online ? 1 : 0) + min(foundN(), (int)CONN_MAX_DEV_ROWS) + 1; }
static ConnKind connItemAt(int sel, int& devIdx) {
    devIdx = -1;
    if (sel == 0) return CI_RESCAN;
    int i = sel - 1;
    if (online) { if (i == 0) return CI_DISCONN; i--; }
    int nd = min(foundN(), (int)CONN_MAX_DEV_ROWS);
    if (i < nd) { devIdx = i; return CI_DEVICE; }
    return CI_BACK;
}

static void enterDetails() {
    detailsPage = 0;
#if W96P_MOCK
    fwMarker = 17; fwValid = true;
#else
    fwValid = online && cli.readFwVersion(fwMarker);   // DFU 版本查询(marker=major*10+minor)
#endif
    scr = SCR_DETAILS;
    dirty = true;
}

static void dispatchButtons() {
    switch (scr) {
    case SCR_DASHBOARD:
    case SCR_TURBO_DASH: {
        // 连击语义互斥: 等序列超时确认, 恰好 2 下=开关, 恰好 3 下=Turbo(设计 §1)
        if (M5.BtnA.wasDecideClickCount()) {
            uint8_t n = M5.BtnA.getClickCount();
            if (n == 2) fanSetPower(fanIsOn() ? 0 : 1);
            else if (n == 3) fanSetTurbo(snap.turboRemainS == 0);
        }
        if (M5.BtnA.pressedFor(GESTURE_HOLD_MS)) { enterGesture(); break; }
        if (M5.BtnB.wasClicked()) { scr = (scr == SCR_TURBO_DASH) ? SCR_DASHBOARD : SCR_MENU; dirty = true; }
        break;
    }
    case SCR_MENU:
        if (M5.BtnB.wasClicked()) { menuIdx = (menuIdx + 1) % 8; dirty = true; }
        if (M5.BtnB.pressedFor(BTNB_LONG_MS)) { scr = SCR_DASHBOARD; dirty = true; break; }
        if (M5.BtnA.wasClicked() || M5.BtnA.wasHold()) {
            const MenuItem& it = menu[menuIdx];
            if (it.type == M_BACK) scr = SCR_DASHBOARD;
            else if (it.type == M_VIEW && menuIdx == 5) { enterDetails(); break; }
            else if (it.type == M_VIEW && menuIdx == 6) { scr = SCR_CONN_MGMT; connSel = 0; connMsg[0] = 0; }
            else { adjustVal = currentValOf(menuIdx); scr = SCR_ADJUST; }
            dirty = true;
        }
        break;
    case SCR_ADJUST:
        if (M5.BtnA.wasClicked()) { adjustVal = stepDown(menu[menuIdx].type, adjustVal); dirty = true; }
        if (M5.BtnA.wasHold())    { adjustVal = stepUp  (menu[menuIdx].type, adjustVal); dirty = true; }
        if (M5.BtnB.wasClicked()) { commitAdjust(); scr = SCR_MENU; dirty = true; }      // 保存返回
        if (M5.BtnB.pressedFor(BTNB_LONG_MS)) { scr = SCR_DASHBOARD; dirty = true; }     // 放弃
        break;
    case SCR_DETAILS:
        if (M5.BtnA.wasClicked()) { detailsPage = (detailsPage + 1) % 3; dirty = true; }
        if (M5.BtnB.wasClicked()) { scr = SCR_MENU; dirty = true; }
        if (M5.BtnB.pressedFor(BTNB_LONG_MS)) { scr = SCR_DASHBOARD; dirty = true; }
        break;
    case SCR_CONN_MGMT:
        if (M5.BtnB.wasClicked()) { connSel = (connSel + 1) % connItemCount(); dirty = true; }
        if (M5.BtnB.pressedFor(BTNB_LONG_MS)) { scr = SCR_DASHBOARD; dirty = true; break; }
        if (M5.BtnA.wasClicked()) {
            int devIdx;
            switch (connItemAt(connSel, devIdx)) {
            case CI_RESCAN:  doStartScan(); break;
            case CI_DISCONN: doDisconnect(); break;
            case CI_BACK:    scr = SCR_DASHBOARD; break;
            case CI_DEVICE:  doConnectIndex(devIdx); break;
            }
            dirty = true;
        }
        break;
    case SCR_GESTURE:
        if (M5.BtnA.wasReleased()) exitGesture();   // 松开: 补终值回看板
        break;
    default: break;
    }
}

// ============================== 渲染(全部走 M5.Display + 画布, 设计 §5) ==============================
static M5Canvas canvas(&M5.Display);
static char buf[64];

static void bleDot(int cx, int cy) {
    if (online) canvas.fillCircle(cx, cy, 5, C_GREEN);
    else        canvas.drawCircle(cx, cy, 5, C_RED);
}
static void txt(int x, int y, const char* s, uint16_t c, const lgfx::IFont* f = &fonts::efontCN_16) {
    canvas.setFont(f);
    canvas.setTextColor(c, C_BLACK);
    canvas.drawString(s, x, y);
}
static void txtR(int xr, int y, const char* s, uint16_t c, const lgfx::IFont* f = &fonts::efontCN_16) {
    canvas.setFont(f);
    canvas.setTextColor(c, C_BLACK);
    canvas.drawString(s, xr - canvas.textWidth(s), y);
}
static void txtC(int y, const char* s, uint16_t c, const lgfx::IFont* f = &fonts::efontCN_16) {
    canvas.setFont(f);
    canvas.setTextColor(c, C_BLACK);
    canvas.drawCentreString(s, SCR_W / 2, y);
}
static void bar(int x, int y, int w, int h, int pct, uint16_t c) {
    canvas.drawRect(x, y, w, h, C_GREY);
    int fw = (w - 2) * constrain(pct, 0, 100) / 100;
    if (fw > 0) canvas.fillRect(x + 1, y + 1, fw, h - 2, c);
}
static uint16_t batVoltColor() {
    if (!snap.valid) return C_GREY;
    if (snap.battery.voltageMv < 3300) return C_RED;     // <3.3V 红
    if (snap.battery.voltageMv < 3500) return C_ORANGE;  // <3.5V 橙
    return C_WHITE;
}
// "BAT 3.95V  -412mA" — 只呈现电压/电流, 无 SOC 无温度(设计 §1)
static void drawBatRow(int y) {
    txt(4, y, "BAT", C_GREY, &fonts::efontCN_12);
    if (!snap.valid) { txt(34, y, "--", C_GREY, &fonts::efontCN_12); return; }
    snprintf(buf, sizeof(buf), "%.2fV", snap.battery.voltageMv / 1000.0f);
    txt(34, y, buf, batVoltColor(), &fonts::efontCN_12);
    snprintf(buf, sizeof(buf), "%dmA", (int)snap.battery.currentMa);
    // 充电中电流值变绿(设计 §5.0)
    txtR(SCR_W - 4, y, buf, snap.battery.currentMa > 0 ? C_GREEN : C_WHITE, &fonts::efontCN_12);
}
static void drawMotRow(int y, bool withVolt) {
    txt(4, y, "MOT", C_GREY, &fonts::efontCN_12);
    if (!snap.valid) { txt(34, y, "--", C_GREY, &fonts::efontCN_12); return; }
    if (withVolt) snprintf(buf, sizeof(buf), "%umA %.1fV", (unsigned)snap.motor.currentMa, snap.motor.voltageMv / 1000.0f);
    else          snprintf(buf, sizeof(buf), "%umA", (unsigned)snap.motor.currentMa);
    txt(34, y, buf, C_WHITE, &fonts::efontCN_12);
    if (snap.motor.block) {   // 堵转: 红字反白(红底)
        canvas.setFont(&fonts::efontCN_12);
        const char* t = "BLOCK!";
        int tw = canvas.textWidth(t);
        canvas.fillRect(SCR_W - 4 - tw - 2, y - 1, tw + 4, 14, C_RED);
        canvas.setTextColor(C_BLACK, C_RED);
        canvas.drawString(t, SCR_W - 4 - tw, y);
    } else {
        txtR(SCR_W - 4, y, "ok", C_GREEN, &fonts::efontCN_12);
    }
}
static void drawBusRow(int y) {
    txt(4, y, "BUS", C_GREY, &fonts::efontCN_12);
    if (!snap.valid || snap.power.vbusMa == 0x7FFF || snap.power.vbusMv < 500) {
        txt(34, y, "--", C_GREY, &fonts::efontCN_12); return;
    }
    snprintf(buf, sizeof(buf), "%.2fV", snap.power.vbusMv / 1000.0f);
    txt(34, y, buf, C_WHITE, &fonts::efontCN_12);
    const char* st = snap.power.powSta == 1 ? "CHG" : snap.power.powSta == 2 ? "DCHG" : "--";
    txtR(SCR_W - 4, y, st, snap.power.powSta == 1 ? C_GREEN : C_WHITE, &fonts::efontCN_12);
}

// StickS3 自身电量(M5PM1, 5s 缓存; 充电中绿色带+号)
static void drawS3Bat() {
    static uint32_t lastMs = 0;
    static int lvl = -1; static bool chg = false;
    if (lastMs == 0 || millis() - lastMs > 5000) {
        lastMs = millis();
        lvl = M5.Power.getBatteryLevel();
        chg = M5.Power.isCharging();
    }
    if (lvl < 0) snprintf(buf, sizeof(buf), "S3 --");
    else snprintf(buf, sizeof(buf), "S3 %s%d%%", chg ? "+" : "", lvl);
    uint16_t c = chg ? C_GREEN : lvl < 0 ? C_GREY : lvl <= 10 ? C_RED : lvl <= 30 ? C_ORANGE : C_WHITE;
    txtR(SCR_W - 4, 4, buf, c, &fonts::efontCN_12);
}

static void renderDashboard() {
    txt(4, 2, "W96P", C_WHITE);
    bleDot(104, 10);
    txt(114, 4, "BLE", C_GREY, &fonts::efontCN_12);
    drawS3Bat();

    // 大字号当前风速(青色; 自然风时进度条变绿)
    if (snap.valid) snprintf(buf, sizeof(buf), "%d%%", snap.speed);
    else            snprintf(buf, sizeof(buf), "--");
    txtC(26, buf, snap.valid ? C_CYAN : C_GREY, &fonts::Font6);
    bar(6, 70, SCR_W - 12, 10, snap.valid ? snap.speed : 0, snap.natureOn ? C_GREEN : C_CYAN);

    snprintf(buf, sizeof(buf), "NAT:%s", !snap.valid ? "--" : snap.natureOn ? "on" : "off");
    txt(4, 90, buf, snap.valid && snap.natureOn ? C_GREEN : C_GREY, &fonts::efontCN_12);
    snprintf(buf, sizeof(buf), "TMR:%s", snap.timerRemainS ? "" : " --");
    if (snap.timerRemainS) snprintf(buf, sizeof(buf), "TMR: %umin", (snap.timerRemainS + 59) / 60);
    txtR(SCR_W - 4, 90, buf, snap.timerRemainS ? C_WHITE : C_GREY, &fonts::efontCN_12);

    snprintf(buf, sizeof(buf), "LGT:%s", lightEst == 0xFF ? "--" : "");
    if (lightEst != 0xFF) snprintf(buf, sizeof(buf), "LGT:%d", lightEst);
    txt(4, 108, buf, lightEst == 0xFF ? C_GREY : C_WHITE, &fonts::efontCN_12);
    snprintf(buf, sizeof(buf), "GEA: %s", "--");
    if (gearEst) snprintf(buf, sizeof(buf), "GEA: %d", gearEst);
    txtR(SCR_W - 4, 108, buf, gearEst ? C_WHITE : C_GREY, &fonts::efontCN_12);

    drawBatRow(132);
    drawMotRow(150, false);
    drawBusRow(168);

    txt(4,  200, "A按住:手势 2x:开关", C_GREY, &fonts::efontCN_12);
    txt(4,  218, "3x:Turbo  B:菜单",  C_GREY, &fonts::efontCN_12);
}

static void renderTurboDash() {
    // 标题橙底黑字条(设计 §5.0)
    canvas.fillRect(0, 0, SCR_W, 22, C_ORANGE);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(C_BLACK, C_ORANGE);
    canvas.drawCentreString("** TURBO **", SCR_W / 2 - 10, 2);
    bleDot(122, 11);

    unsigned t = snap.turboRemainS;
    snprintf(buf, sizeof(buf), "%02u:%02u", t / 60, t % 60);
    txtC(34, buf, C_ORANGE, &fonts::Font6);
    bar(6, 78, SCR_W - 12, 10, TURBO_TOTAL_S ? (int)(t * 100 / TURBO_TOTAL_S) : 0, C_ORANGE);

    drawBatRow(106);
    drawMotRow(124, true);

    txtC(160, "3xA:退出Turbo", C_GREY, &fonts::efontCN_12);
}

static void renderMenu() {
    txt(4, 2, "MENU", C_WHITE);
    snprintf(buf, sizeof(buf), "%d/8", menuIdx + 1);
    txtR(SCR_W - 4, 4, buf, C_GREY, &fonts::efontCN_12);

    for (int i = 0; i < 8; i++) {
        int y = 30 + i * 20;
        bool cur = (i == menuIdx);
        bool adjustable = menu[i].type != M_VIEW && menu[i].type != M_BACK;
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 20, C_WHITE);   // 光标行反白
        uint16_t fg = cur ? C_BLACK : (adjustable ? C_WHITE : C_GREY);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(fg, cur ? C_WHITE : C_BLACK);
        snprintf(buf, sizeof(buf), "%s %s", cur ? ">" : " ", menu[i].name);
        canvas.drawString(buf, 4, y);
        // 当前值(青色)
        const char* v = nullptr;
        switch (menu[i].type) {
        case M_PERCENT: snprintf(buf, sizeof(buf), "%d%%", snap.valid ? snap.speed : 0); v = buf; break;
        case M_MINUTES: snprintf(buf, sizeof(buf), "%s", snap.timerRemainS ? "" : "off");
                        if (snap.timerRemainS) snprintf(buf, sizeof(buf), "%umin", (snap.timerRemainS + 59) / 60);
                        v = buf; break;
        case M_TOGGLE:  v = (i == 2 ? snap.natureOn : snap.turboRemainS > 0) ? "on" : "off"; break;
        case M_LIGHT:   snprintf(buf, sizeof(buf), "%s", lightEst == 0xFF ? "--" : "");
                        if (lightEst != 0xFF) snprintf(buf, sizeof(buf), "%d", lightEst);
                        v = buf; break;
        default: break;
        }
        if (v) {
            canvas.setTextColor(cur ? C_BLACK : C_CYAN, cur ? C_WHITE : C_BLACK);
            canvas.drawString(v, SCR_W - 4 - canvas.textWidth(v), y);
        }
    }
    txt(4, 204, "A:调节  B:下一项", C_GREY, &fonts::efontCN_12);
    txt(4, 222, "B长按:回看板",     C_GREY, &fonts::efontCN_12);
}

static void renderAdjust() {
    const MenuItem& it = menu[menuIdx];
    txtC(10, it.name, C_WHITE);

    switch (it.type) {
    case M_PERCENT: snprintf(buf, sizeof(buf), "%d %%", adjustVal); break;
    case M_MINUTES: snprintf(buf, sizeof(buf), "%d min", adjustVal); break;
    case M_TOGGLE:  snprintf(buf, sizeof(buf), "%s", adjustVal ? "on" : "off"); break;
    case M_LIGHT:   snprintf(buf, sizeof(buf), "%d", adjustVal); break;
    default: buf[0] = 0; break;
    }
    txtC(48, buf, C_CYAN, &fonts::Font4);

    switch (it.type) {
    case M_PERCENT: txtC(120, "A短按:-5%  A长按:+5%", C_GREY, &fonts::efontCN_12); break;
    case M_MINUTES: txtC(120, "A短按:-30  A长按:+30", C_GREY, &fonts::efontCN_12);
                    txtC(138, "0 = 取消定时", C_GREY, &fonts::efontCN_12); break;
    case M_TOGGLE:  txtC(120, "A:切换", C_GREY, &fonts::efontCN_12); break;
    case M_LIGHT:   txtC(120, "A短按:循环0-4", C_GREY, &fonts::efontCN_12); break;
    default: break;
    }
    txtC(170, "B:保存返回",      C_GREY, &fonts::efontCN_12);
    txtC(188, "B长按:放弃",      C_GREY, &fonts::efontCN_12);
}

static void renderGesture() {
    txt(4, 2, "GESTURE", C_WHITE);
    bleDot(104, 10);
    txt(114, 4, "BLE", C_GREY, &fonts::efontCN_12);

    if (!g.basisValid) {
        txtC(100, "请稍倾斜握持", C_ORANGE);
        txtC(140, "松开 BtnA 退出", C_GREY, &fonts::efontCN_12);
        return;
    }
    // 换挡后 GEAR 顶替 PWM 显示(2026-08-03 真机反馈)
    if (millis() < g.gearDispUntilMs && g.lastShakeGear >= 0) {
        snprintf(buf, sizeof(buf), "GEAR %d", g.lastShakeGear);
        txtC(32, buf, C_ORANGE, &fonts::Font4);
        bar(6, 66, SCR_W - 12, 8, g.lastShakeGear == 0 ? 0 : gearSpeeds[g.lastShakeGear - 1], C_ORANGE);
    } else {
        uint8_t pct = (uint8_t)(g.speedF + 0.5f);
        snprintf(buf, sizeof(buf), "%d %%", pct);
        txtC(32, buf, C_CYAN, &fonts::Font4);
        bar(6, 66, SCR_W - 12, 8, pct, C_CYAN);
    }

    // 调试信息(原型期保留, 设计 §5.5)
    float deg = 0;
    if (g.gLen > 1e-3f) {
        float r = constrain(g.emaP / g.gLen, -1.0f, 1.0f);
        deg = asinf(r) * 57.2958f;
    }
    snprintf(buf, sizeof(buf), "gear:%d  ang:%.0f", gearEst, deg);
    txtC(88, buf, C_GREY, &fonts::efontCN_12);

    // 换档/到头色块提示(150ms)
    if (millis() < flashUntilMs) {
        canvas.fillRect(6, 112, SCR_W - 12, 20, flashColor);
    } else {
        txtC(114, "翻:换档  举/摆:调速", C_GREY, &fonts::efontCN_12);
    }
    txtC(140, "松开 BtnA 确认", C_GREY, &fonts::efontCN_12);
}

static void renderDetails() {
    if (detailsPage == 0) {
        txt(4, 2, "STATUS 实时", C_CYAN, &fonts::efontCN_12);
        txtR(SCR_W - 4, 2, "1/3", C_GREY, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "SPD %s   GEA %s", snap.valid ? "" : "--", gearEst ? "" : "--");
        if (snap.valid && gearEst) snprintf(buf, sizeof(buf), "SPD %d%%  GEA %d", snap.speed, gearEst);
        else if (snap.valid)       snprintf(buf, sizeof(buf), "SPD %d%%  GEA --", snap.speed);
        else if (gearEst)          snprintf(buf, sizeof(buf), "SPD --   GEA %d", gearEst);
        else                       snprintf(buf, sizeof(buf), "SPD --   GEA --");
        txt(4, 26, buf, C_WHITE, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "NAT %s TUR %s",
                 !snap.valid ? "--" : snap.natureOn ? "on" : "off",
                 !snap.valid ? "--" : snap.turboRemainS > 0 ? "on" : "off");
        txt(4, 44, buf, C_WHITE, &fonts::efontCN_12);
        char tmr[12], lgt[8];
        if (snap.timerRemainS) snprintf(tmr, sizeof(tmr), "%um", (snap.timerRemainS + 59) / 60);
        else                   snprintf(tmr, sizeof(tmr), "--");
        if (lightEst != 0xFF)  snprintf(lgt, sizeof(lgt), "%d", lightEst);
        else                   snprintf(lgt, sizeof(lgt), "--");
        snprintf(buf, sizeof(buf), "TMR %s LGT %s", tmr, lgt);
        txt(4, 62, buf, C_WHITE, &fonts::efontCN_12);
        canvas.drawFastHLine(0, 80, SCR_W, C_GREY);
        drawBatRow(86);
        canvas.drawFastHLine(0, 102, SCR_W, C_GREY);
        drawMotRow(108, true);
        // BUS 电流(页 1 特有)
        txt(4, 126, "BUS", C_GREY, &fonts::efontCN_12);
        if (snap.valid && snap.power.vbusMa != 0x7FFF && snap.power.vbusMv >= 500) {
            snprintf(buf, sizeof(buf), "%.2fV %.1fA", snap.power.vbusMv / 1000.0f, snap.power.vbusMa / 1000.0f);
            txt(34, 126, buf, C_WHITE, &fonts::efontCN_12);
        } else txt(34, 126, "--", C_GREY, &fonts::efontCN_12);
        // 功率(W96P = 电机电压 × 电机电流, ble-protocol.md §5.3)
        txt(4, 144, "PWR", C_GREY, &fonts::efontCN_12);
        if (snap.valid && snap.motor.voltageMv > 0) {
            float w = snap.motor.voltageMv / 1000.0f * (snap.motor.currentMa / 1000.0f);
            snprintf(buf, sizeof(buf), "%.2fW", w);
            txt(34, 144, buf, C_WHITE, &fonts::efontCN_12);
        } else txt(34, 144, "--", C_GREY, &fonts::efontCN_12);
    } else if (detailsPage == 2) {
        // ---- 第 3 页: StickS3 自身信息 ----
        txt(4, 2, "STATUS S3", C_CYAN, &fonts::efontCN_12);
        txtR(SCR_W - 4, 2, "3/3", C_GREY, &fonts::efontCN_12);

        // 电池(M5PM1)
        bool chg = M5.Power.isCharging();
        snprintf(buf, sizeof(buf), "BAT %d%%  %.2fV %s",
                 M5.Power.getBatteryLevel(), M5.Power.getBatteryVoltage() / 1000.0f, chg ? "+" : "");
        txt(4, 26, buf, chg ? C_GREEN : C_WHITE, &fonts::efontCN_12);

        // 内存/PSRAM
        snprintf(buf, sizeof(buf), "HEAP %uK free", unsigned(ESP.getFreeHeap() / 1024));
        txt(4, 44, buf, C_WHITE, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "PSRAM %uK/%uK", unsigned(ESP.getFreePsram() / 1024), unsigned(ESP.getPsramSize() / 1024));
        txt(4, 62, buf, C_WHITE, &fonts::efontCN_12);

        // 主频/运行时长
        uint32_t upS = millis() / 1000;
        snprintf(buf, sizeof(buf), "CPU %uMHz  UP %u:%02u", ESP.getCpuFreqMHz(), unsigned(upS / 60), unsigned(upS % 60));
        txt(4, 80, buf, C_WHITE, &fonts::efontCN_12);

        // MAC + 固件编译时间
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(buf, sizeof(buf), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        txt(4, 98, buf, C_GREY, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "FW %s %s", __DATE__, __TIME__);
        txt(4, 116, buf, C_GREY, &fonts::efontCN_12);

        // 实时 IMU(手势调试用)
        Vec3 a = readAccelMs2();
        snprintf(buf, sizeof(buf), "ACC %+.2f %+.2f %+.2f", a.x, a.y, a.z);
        txt(4, 134, buf, C_CYAN, &fonts::efontCN_12);
    } else {
        txt(4, 2, "STATUS 设备", C_CYAN, &fonts::efontCN_12);
        txtR(SCR_W - 4, 2, "2/3", C_GREY, &fonts::efontCN_12);
        const char* pc = snap.power.powC == 1 ? "in-C" : snap.power.powC == 2 ? "out-C" : "--";
        const char* ps = snap.power.powSta == 1 ? "CHG" : snap.power.powSta == 2 ? "DCHG" : "--";
        snprintf(buf, sizeof(buf), "POW %s   %s", snap.valid ? pc : "--", snap.valid ? ps : "--");
        txt(4, 26, buf, C_WHITE, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "C.out %s C.in %s",
                 snap.valid && snap.power.cOutEnabled ? "ON" : "off",
                 snap.valid && snap.power.cInEnabled ? "ON" : "off");
        txt(4, 44, buf, snap.valid ? C_GREEN : C_GREY, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "HV   %s", snap.valid && snap.power.cHiEnabled ? "ON" : "off");
        txt(4, 62, buf, snap.valid && snap.power.cHiEnabled ? C_GREEN : C_GREY, &fonts::efontCN_12);
        canvas.drawFastHLine(0, 80, SCR_W, C_GREY);
        snprintf(buf, sizeof(buf), "DEV %s", connectedName[0] ? connectedName : "--");
        txt(4, 86, buf, C_WHITE, &fonts::efontCN_12);
        if (connectedName[0]) {
            snprintf(buf, sizeof(buf), "%ddB", connectedRssi);
            txtR(SCR_W - 4, 86, buf, C_GREY, &fonts::efontCN_12);
        }
        snprintf(buf, sizeof(buf), "MAC %s", connectedAddr[0] ? connectedAddr : "--");
        txt(4, 104, buf, C_GREY, &fonts::efontCN_12);
        if (fwValid) {
            snprintf(buf, sizeof(buf), "FW  v%u.%u", fwMarker / 10, fwMarker % 10);
            txt(4, 122, buf, C_WHITE, &fonts::efontCN_12);
        } else {
            txt(4, 122, "FW  --", C_GREY, &fonts::efontCN_12);
        }
    }
    txtC(210, "A:翻页  B:返回菜单", C_GREY, &fonts::efontCN_12);
}

static void renderConnMgmt() {
    txt(4, 2, "CONNECT", C_WHITE);
    bleDot(92, 10);
    txt(102, 4, online ? "在线" : "离线", online ? C_GREEN : C_RED, &fonts::efontCN_12);
    snprintf(buf, sizeof(buf), "当前: %s", online && connectedName[0] ? connectedName : "--");
    txt(4, 24, buf, C_GREY, &fonts::efontCN_12);

    int n = connItemCount();
    int y = 46;
    for (int i = 0; i < n && y < 200; i++) {
        bool cur = (i == connSel);
        int devIdx;
        ConnKind k = connItemAt(i, devIdx);
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 18, C_WHITE);
        uint16_t fg = cur ? C_BLACK : C_WHITE;
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(fg, cur ? C_WHITE : C_BLACK);
        switch (k) {
        case CI_RESCAN:  snprintf(buf, sizeof(buf), "%s 重新扫描", cur ? ">" : " "); break;
        case CI_DISCONN: snprintf(buf, sizeof(buf), "%s 断开连接", cur ? ">" : " "); break;
        case CI_BACK:    snprintf(buf, sizeof(buf), "%s 返回", cur ? ">" : " "); break;
        case CI_DEVICE: {
            const w96p::Client::Found* f = foundAt(devIdx);
            bool isConn = online && strcmp(f->name, connectedName) == 0;
            snprintf(buf, sizeof(buf), "%s %s%s", cur ? ">" : " ",
                     f->name[0] ? f->name : f->addr, isConn ? "*" : "");
            break;
        }
        }
        canvas.drawString(buf, 4, y);
        if (k == CI_DEVICE) {
            const w96p::Client::Found* f = foundAt(devIdx);
            snprintf(buf, sizeof(buf), "%ddB", f->rssi);
            canvas.drawString(buf, SCR_W - 4 - canvas.textWidth(buf), y);
        }
        y += 18;
        // 分隔线: 操作项与设备列表之间 / 设备列表与返回之间
        int ops = 1 + (online ? 1 : 0);
        if (i == ops - 1 || i == ops + min(foundN(), (int)CONN_MAX_DEV_ROWS) - 1) {
            canvas.drawFastHLine(4, y - 3, SCR_W - 8, C_GREY);
        }
    }
    if (connMsg[0]) txtC(202, connMsg, C_ORANGE, &fonts::efontCN_12);
    else if (millis() < scanUntilMs) txtC(202, "扫描中…", C_ORANGE, &fonts::efontCN_12);
    txt(4, 222, "B:下一项  A:执行", C_GREY, &fonts::efontCN_12);
}

static void renderConnecting() {
    txtC(60, "W96P", C_WHITE);
    txtC(100, online ? "连接中…" : "扫描中…", C_CYAN);
    txtC(140, "未找到将进入离线看板", C_GREY, &fonts::efontCN_12);
}

static void render() {
    if (!dirty) return;
    dirty = false;
    canvas.fillScreen(C_BLACK);
    switch (scr) {
    case SCR_CONNECTING: renderConnecting(); break;
    case SCR_DASHBOARD:  renderDashboard();  break;
    case SCR_MENU:       renderMenu();       break;
    case SCR_ADJUST:     renderAdjust();     break;
    case SCR_GESTURE:    renderGesture();    break;
    case SCR_TURBO_DASH: renderTurboDash();  break;
    case SCR_DETAILS:    renderDetails();    break;
    case SCR_CONN_MGMT:  renderConnMgmt();   break;
    }
    canvas.pushSprite(0, 0);
}

// ============================== BLE 事件处理(主循环上下文) ==============================
static void handleEvents() {
    if (evtConnPending) {
        bool c = evtConnValue;
        evtConnPending = false;
        online = c;
        dirty = true;
        if (c) {
            manualOffline = false;                 // 任意手动连接成功复位(设计 §5.6)
            pendingConnect = false;
            connMsg[0] = 0;
            if (scr == SCR_CONNECTING || scr == SCR_CONN_MGMT) scr = SCR_DASHBOARD;
#if !W96P_MOCK
            // 连接即读档位校准转速(FFF7), 换挡后本地立即可知真实 PWM(2026-08-03 反馈)
            uint8_t cal[4];
            if (cli.readSpeedCalib(cal)) { memcpy(gearSpeeds, cal, 4); calibValid = true; }
#endif
        } else {
            snap.valid = false;
            if (pendingConnect) {                  // 手动连接失败, 留在本页
                pendingConnect = false;
                strcpy(connMsg, "连接失败");
            } else if (!manualOffline) {           // 意外掉线 → 自动重扫重连
                autoConnect = true;
                doStartScan();
            }
        }
    }
    if (evtFoundPending) {
        evtFoundPending = false;
        dirty = true;                              // 扫描结果随到随刷新
        if (autoConnect && !online) {              // 发现即连(初次/意外掉线)
            autoConnect = false;
            doConnectIndex(0);
        }
    }
    // CONNECTING 超时 → 离线看板(设计 §2)
    if (scr == SCR_CONNECTING && !online && !pendingConnect
        && millis() - connectStartMs > CONNECT_GIVEUP_MS) {
        autoConnect = false;
        scr = SCR_DASHBOARD;
        dirty = true;
    }
    // 扫描结束清提示
    if (connMsg[0] && strcmp(connMsg, "扫描中…") == 0 && millis() > scanUntilMs) {
        connMsg[0] = 0;
        dirty = true;
    }
    if (connSel >= connItemCount()) { connSel = 0; dirty = true; }
}

// ============================== 入口 ==============================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);   // 必须显式: M5Unified 0.2.19 的 serial_baudrate 默认 0, M5.begin 不初始化 Serial
    // IR 发射管挂在 L3A 轨常供电, G46 悬空会被驱动电路拉成常亮——显式关断
    // (极性 [未确认]: 先按低电平关断, 若仍亮则改 HIGH)
    pinMode(46, OUTPUT);
    digitalWrite(46, LOW);
    M5.Display.setRotation(0);
    M5.Display.setBrightness(128);
    canvas.setPsram(true);   // StickS3 有 8MB PSRAM
    canvas.createSprite(SCR_W, SCR_H);

    Serial.println("\n=== W96P StickS3 remote ===");
#if W96P_MOCK
    Serial.println("[mock] W96P_MOCK=1, BLE disabled, fake snapshot");
    mockFillSnapshot();
    scr = SCR_DASHBOARD;
#else
    cli.begin({
        [](bool c) { evtConnValue = c; evtConnPending = true; },
        [](const w96p::Snapshot& s) { snap = s; dirty = true; },
        [](const char*) { evtFoundPending = true; },
    });
    autoConnect = true;
    connectStartMs = millis();
    doStartScan();
#endif
    dirty = true;
}

void loop() {
    M5.update();             // 铁律: 每轮刷新按键状态
    dispatchButtons();       // 按键优先: 在 BLE 阻塞前响应
#if !W96P_MOCK
    cli.update();            // BLE 写队列 + 分摊轮询(每次最多一个 GATT 读)
#endif

    handleEvents();
    if (scr == SCR_GESTURE) gestureTick();

    // turboRemainS>0 自动切 TurboDash 样式(设计 §2)
    if (snap.valid && snap.turboRemainS > 0 && scr == SCR_DASHBOARD) { scr = SCR_TURBO_DASH; dirty = true; }
    if (snap.turboRemainS == 0 && scr == SCR_TURBO_DASH) { scr = SCR_DASHBOARD; dirty = true; }

    // 连接后首个快照: 风扇已在转 → 按转速就近推断当前档位(FFF1 只写无回读, 2026-08-03 反馈)
    static bool gearSeeded = false;
    if (!gearSeeded && snap.valid) {
        gearSeeded = true;
        if (gearEst == 0 && snap.speed > 0) {
            int best = 0;
            for (int i = 1; i < 4; i++)
                if (abs((int)snap.speed - (int)gearSpeeds[i]) < abs((int)snap.speed - (int)gearSpeeds[best])) best = i;
            gearEst = (uint8_t)(best + 1);
            Serial.printf("[w96p] gear seeded from speed %d%% -> GEAR %d\n", snap.speed, gearEst);
        }
    }
    if (!online) gearSeeded = false;   // 断线后重新播种

    // S3 信息页(第 3 页)有实时数据: 500ms 节流重绘
    if (scr == SCR_DETAILS && detailsPage == 2) {
        static uint32_t lastS3Ms = 0;
        if (millis() - lastS3Ms > 500) { lastS3Ms = millis(); dirty = true; }
    }

    render();
    delay(5);
    static uint32_t hb = 0;   // 临时: 串口通路诊断(事后移除)
    if (millis() - hb > 1000) { hb = millis(); Serial.println("[hb]"); }
}
