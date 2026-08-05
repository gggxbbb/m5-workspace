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

// ============================== 常量(真机调参) ==============================
#define APP_VERSION "1.0.0"   // 状态页 S3 显示; 发版手动递增
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

#include "types.h"
// ============================== 状态与数据 ==============================
static Screen scr = SCR_CONNECTING;
static bool   dirty = true;              // 脏标记: 仅脏时重绘(设计 §6)

static w96p::Client   cli;
static w96p::Snapshot snap;              // client 500ms 轮询快照
static uint8_t fwMarker = 0;             // DFU 读到的固件版本(major*10+minor), 连接时查
static bool    fwValid = false;
static uint32_t snValue = 0;             // DFU 序列号, 连接时查
static bool    snValid = false;

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
static int  linkRssi          = 0;    // 实时链路 RSSI(1s 轮询, 0=未知)

static const MenuItem menu[8] = {
    { "风速",   M_PERCENT, ET_SPEED  }, { "定时",   M_MINUTES, ET_TIMER  }, { "自然风", M_TOGGLE, ET_NATURE },
    { "灯光",   M_LIGHT,   ET_LIGHT  }, { "设置",  M_SUBMENU, ET_NONE },
    { "状态详情", M_VIEW,   ET_NONE   }, { "连接管理", M_VIEW,   ET_NONE   }, { "返回",   M_BACK,   ET_NONE },
};
static const MenuItem settingsMenu[7] = {
    { "Turbo时间", M_SECONDS,   ET_TURBOTIME }, { "休眠延时", M_SECONDS,   ET_SHUTDOWN  },
    { "减档模式", M_TOGGLE,    ET_GEARDOWN  }, { "BLE_SN",  M_TOGGLE,    ET_BLESN    },
    { "电源开关", M_POWPAGE,   ET_NONE      }, { "档位校准", M_CALIBPAGE, ET_NONE      },
    { "返回",     M_BACK,      ET_NONE      },
};
static int menuIdx   = 0;
static int settingsIdx = 0;
static EditTarget editTarget = ET_NONE;
static MenuType   editType   = M_PERCENT;   // 当前 ADJUST 的步进类型
static int adjustVal = 0;                // 调节态暂存(保存才下发)
// 设置项缓存(-1=未读; 进设置页时读取)
static int setShutdownS = -1, setGearDown = -1, setBleSn = -1;
static uint16_t turboTotalS = 199;       // 连接时读 FFF8 更新
static uint8_t calBuf[4];                // 校准编辑缓冲
static int calSel = 0, powSel = 0;
static int detailsPage = 0;              // 0=实时 1=设备 2=S3
static int connSel   = 0;

// 快照没有的本地状态(GEA/LGT 无回读特征, 只跟踪本机下发值)
static uint8_t gearEst  = 0;             // 0=未知
static uint8_t lightEst = 0xFF;          // 0xFF=未知
static uint8_t gearSpeeds[4] = { 10, 35, 70, 100 };  // 档位校准转速%, 连接时经 FFF7 更新
static bool    calibValid = false;

// 手势换档/到头闪屏
static uint16_t flashColor = 0;
static uint32_t flashUntilMs = 0;

// 渲染共享(定义放主 tab: setup 里也要用 canvas)
static M5Canvas canvas(&M5.Display);
static char buf[64];

// ============================== Mock 数据 ==============================

static int foundN() {
    return cli.foundCount();
}
static const w96p::Client::Found* foundAt(int i) {
    return &cli.foundList()[i];
}
static void doStartScan() {
    strcpy(connMsg, "扫描中…");
    scanUntilMs = millis() + SCAN_SECONDS * 1000;
    cli.startScan(SCAN_SECONDS);
    dirty = true;
}
static int pendingConnIdx = -1;   // 延迟连接: 先渲染一帧"连接中…"再进阻塞 connect

static void doConnectIndex(int i) {
    const w96p::Client::Found* f = foundAt(i);
    if (f) {   // 真机也要记连接目标(原只有 mock 分支记录 → 状态页 DEV/MAC 永远 --)
        strncpy(connectedName, f->name, sizeof(connectedName) - 1);
        strncpy(connectedAddr, f->addr, sizeof(connectedAddr) - 1);
        connectedRssi = f->rssi;
    }
    strcpy(connMsg, "连接中…");
    pendingConnect = true;
    pendingConnIdx = i;            // 实际 connect 在 loop render 之后执行
    cli.stopScan();
    dirty = true;
}
static void doDisconnect() {
    cli.disconnect();
    manualOffline = true;
    connMsg[0] = 0;
    dirty = true;
}

// ============================== 按键分发(设计 §6) ==============================
static bool fanIsOn() { return online && snap.valid && snap.speed > 0; }

static int currentValOf(EditTarget t) {
    switch (t) {
    case ET_SPEED:     return snap.valid ? snap.speed : 0;
    case ET_TIMER:     return snap.timerRemainS / 60;
    case ET_NATURE:    return snap.natureOn ? 1 : 0;
    case ET_LIGHT:     return lightEst == 0xFF ? 0 : lightEst;
    case ET_TURBOTIME: return turboTotalS;
    case ET_SHUTDOWN:  return setShutdownS >= 0 ? setShutdownS : 0;
    case ET_GEARDOWN:  return setGearDown >= 0 ? setGearDown : 0;
    case ET_BLESN:     return setBleSn >= 0 ? setBleSn : 0;
    default:           return 0;
    }
}
static int stepDown(MenuType t, int v) {
    switch (t) {
    case M_PERCENT: return v >= 5 ? v - 5 : 0;
    case M_MINUTES: return v >= 30 ? v - 30 : 0;
    case M_SECONDS: return v >= 10 ? v - 10 : 0;
    case M_TOGGLE:  return !v;
    case M_LIGHT:   return (v + 1) % 5;    // 短按循环 0-4
    default:        return v;
    }
}
static int stepUp(MenuType t, int v) {
    switch (t) {
    case M_PERCENT: return v <= 95 ? v + 5 : 100;
    case M_MINUTES: return v <= 450 ? v + 30 : 480;
    case M_SECONDS: return v <= 590 ? v + 10 : 600;
    case M_TOGGLE:  return !v;
    case M_LIGHT:   return (v + 4) % 5;
    default:        return v;
    }
}
static void commitAdjust() {
    switch (editTarget) {
    case ET_SPEED:     fanSetSpeed((uint8_t)adjustVal); break;
    case ET_TIMER:     fanSetTimerMin((uint16_t)adjustVal); break;
    case ET_NATURE:    fanSetNature(adjustVal); break;
    case ET_LIGHT:     fanSetLight((uint8_t)adjustVal); break;
    case ET_TURBOTIME: if (cli.setTurboTime((uint16_t)adjustVal)) turboTotalS = adjustVal == 0 ? 199 : adjustVal; break;
    case ET_SHUTDOWN:  if (cli.setShutdownDelay((uint16_t)adjustVal)) setShutdownS = adjustVal; break;
    case ET_GEARDOWN:  if (cli.setGearDownMode((uint8_t)adjustVal)) setGearDown = adjustVal; break;
    case ET_BLESN:     if (cli.setBleSn(adjustVal != 0)) setBleSn = adjustVal; break;
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
    // 真机的版本/SN 在连接时已查询(handleEvents)
    scr = SCR_DETAILS;
    dirty = true;
}

static void enterSettings() {
    settingsIdx = 0;
    // 进设置页读当前值(每次进入重读, 保持新鲜; 失败留旧值/--)
    uint16_t u16; uint8_t u8;
    if (online && cli.readShutdownDelay(u16))  setShutdownS = u16;
    if (online && cli.readGearDownMode(u8))    setGearDown = u8;
    // BLE_SN 开关态 = 广播名是否带 '_': 开 SN 广播时名字为 W96P_{SN}, 关时仅 W96P(实测 2026-08-05)
    if (online) setBleSn = strchr(connectedName, '_') ? 1 : 0;
    scr = SCR_SETTINGS;
    dirty = true;
}

// BtnB 双向遍历: decided 语义互斥——恰好 1 下=正向, 恰好 2 下=反向(2026-08-03 反馈)
// 注意: 判定有序列超时(~300ms), 正向遍历也带此延迟
static int8_t btnBNav() {
    if (M5.BtnB.wasDecideClickCount()) {
        uint8_t n = M5.BtnB.getClickCount();
        if (n == 1) return 1;
        if (n == 2) return -1;
    }
    return 0;
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
        // BtnB 全局只走 btnBNav(decided): 与菜单共用判定窗, 防同一次点击被两屏重复消费(2026-08-05 bug)
        if (btnBNav() == 1) {
            if (scr == SCR_TURBO_DASH) scr = SCR_DASHBOARD;
            else { scr = SCR_MENU; menuIdx = 0; }   // 看板进菜单回到第一项
            dirty = true;
        }
        break;
    }
    case SCR_MENU: {
        int8_t nav = btnBNav();
        if (nav) { menuIdx = (menuIdx + nav + 8) % 8; dirty = true; }
        if (M5.BtnA.wasClicked() || M5.BtnA.wasHold()) {
            const MenuItem& it = menu[menuIdx];
            if (it.type == M_BACK) scr = SCR_DASHBOARD;
            else if (it.type == M_SUBMENU) { enterSettings(); break; }
            else if (it.type == M_VIEW && menuIdx == 5) { enterDetails(); break; }
            else if (it.type == M_VIEW && menuIdx == 6) { scr = SCR_CONN_MGMT; connSel = 0; connMsg[0] = 0; }
            else {
                editTarget = it.target;
                editType   = it.type;
                adjustVal  = currentValOf(it.target);
                scr = SCR_ADJUST;
            }
            dirty = true;
        }
        break;
    }
    case SCR_SETTINGS: {
        int8_t nav = btnBNav();
        if (nav) { settingsIdx = (settingsIdx + nav + 7) % 7; dirty = true; }
        if (M5.BtnA.wasClicked() || M5.BtnA.wasHold()) {
            const MenuItem& it = settingsMenu[settingsIdx];
            if (it.type == M_BACK) scr = SCR_MENU;
            else if (it.type == M_POWPAGE)   { powSel = 0; scr = SCR_POW; }
            else if (it.type == M_CALIBPAGE) { memcpy(calBuf, gearSpeeds, 4); calSel = 0; scr = SCR_CALIB; }
            else {
                editTarget = it.target;
                editType   = it.type;
                adjustVal  = currentValOf(it.target);
                scr = SCR_ADJUST;
            }
            dirty = true;
        }
        break;
    }
    case SCR_POW: {
        int8_t nav = btnBNav();
        if (nav) { powSel = (powSel + nav + 4) % 4; dirty = true; }   // 3 开关 + 返回
        if (M5.BtnA.wasClicked()) {
            if (powSel == 3) { scr = SCR_SETTINGS; dirty = true; break; }   // 返回项
            if (!snap.valid) break;
            // 三开关: 当前态取自快照, 写反逻辑由 client 转换
            static const char* keys[3] = { "POW_C_OUT", "POW_C_IN", "POW_C_HI" };
            bool cur = powSel == 0 ? snap.power.cOutEnabled : powSel == 1 ? snap.power.cInEnabled : snap.power.cHiEnabled;
            cli.setPowSwitch(keys[powSel], !cur);
            dirty = true;
        }
        break;
    }
    case SCR_CALIB: {
        int8_t nav = btnBNav();
        if (nav) { calSel = (calSel + nav + 6) % 6; dirty = true; }   // 4 档 + 保存 + 放弃
        if (M5.BtnA.wasClicked()) {
            if (calSel < 4) { calBuf[calSel] = calBuf[calSel] >= 5 ? calBuf[calSel] - 5 : 0; }
            else if (calSel == 4) {   // 保存
                if (cli.setSpeedCalib(calBuf)) memcpy(gearSpeeds, calBuf, 4);
                scr = SCR_SETTINGS;
            } else {                  // 放弃
                scr = SCR_SETTINGS;
            }
            dirty = true;
        }
        if (M5.BtnA.wasHold() && calSel < 4) { calBuf[calSel] = calBuf[calSel] <= 95 ? calBuf[calSel] + 5 : 100; dirty = true; }
        break;
    }
    case SCR_ADJUST: {
        if (M5.BtnA.wasClicked()) { adjustVal = stepDown(editType, adjustVal); dirty = true; }
        if (M5.BtnA.wasHold())    { adjustVal = stepUp  (editType, adjustVal); dirty = true; }
        Screen back = editTarget >= ET_TURBOTIME ? SCR_SETTINGS : SCR_MENU;
        int8_t nav = btnBNav();
        if (nav == 1)  { commitAdjust(); scr = back; dirty = true; }   // 1xB: 保存返回
        if (nav == -1) { scr = back; dirty = true; }                   // 2xB: 放弃返回
        break;
    }
    case SCR_DETAILS:
        if (M5.BtnA.wasClicked()) { detailsPage = (detailsPage + 1) % 3; dirty = true; }
        if (btnBNav() == 1) { scr = SCR_MENU; dirty = true; }
        break;
    case SCR_CONN_MGMT: {
        int8_t nav = btnBNav();
        if (nav) { connSel = (connSel + nav + connItemCount()) % connItemCount(); dirty = true; }
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
    }
    case SCR_GESTURE:
        if (M5.BtnA.wasReleased()) exitGesture();   // 松开: 补终值回看板
        break;
    default: break;
    }
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
    cli.begin({
        [](bool c) { evtConnValue = c; evtConnPending = true; },
        [](const w96p::Snapshot& s) { snap = s; dirty = true; },
        [](const char*) { evtFoundPending = true; },
    });
    autoConnect = true;
    connectStartMs = millis();
    doStartScan();
    dirty = true;
}

void loop() {
    M5.update();             // 铁律: 每轮刷新按键状态
    dispatchButtons();       // 按键优先: 在 BLE 阻塞前响应
    cli.update();            // BLE 写队列 + 分摊轮询(每次最多一个 GATT 读)

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

    // 延迟执行阻塞连接: 让"连接中…"先上屏(connect 阻塞期间无法重绘)
    if (pendingConnIdx >= 0) {
        int i = pendingConnIdx;
        pendingConnIdx = -1;
        if (!cli.connectIndex(i)) { pendingConnect = false; strcpy(connMsg, "连接失败"); dirty = true; }
    }
    delay(5);
    // (无周期任务: 渲染由 dirty 驱动, BLE 事件由 handleEvents 分发)
}

