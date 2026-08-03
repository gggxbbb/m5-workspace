// w96p-remote.ino — W96P 风扇 BLE client 功能演示（无 UI，串口输出 115200）
// 流程：扫描 → 连接第一个候选 → 读全部状态 → 把 client 的每个功能过一遍（改动型操作都做复原）
#include <w96p_client.h>
#include <M5Unified.h>

static w96p::Client cli;
static volatile bool g_conn = false;
static volatile bool g_snapDirty = false;
static w96p::Snapshot g_snap;

// loop() 泵：等 ms 毫秒，期间驱动 client 队列/轮询
static void pump(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) { M5.update(); cli.update(); delay(5); }
}
// 等一个有效快照
static bool waitSnap(uint32_t timeoutMs = 6000) {
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        pump(50);
        if (g_snapDirty) { g_snapDirty = false; return true; }
    }
    return false;
}
static void printSnap(const char* tag) {
    Serial.printf("[%s] speed %d%% nat:%d turbo %us timer %umin\n", tag,
                  g_snap.speed, g_snap.natureOn, unsigned(g_snap.turboRemainS), unsigned(g_snap.timerRemainS / 60));
    Serial.printf("  bat %umV %dmA rem %umWh %dC | mot %umA %s %umV | vbus %umV sta %d\n",
                  unsigned(g_snap.battery.voltageMv), int(g_snap.battery.currentMa),
                  unsigned(g_snap.battery.rcapMwh), int(g_snap.battery.tempC),
                  unsigned(g_snap.motor.currentMa), g_snap.motor.block ? "BLOCK" : "ok",
                  unsigned(g_snap.motor.voltageMv),
                  unsigned(g_snap.power.vbusMv), int(g_snap.power.powSta));
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.println("\n=== W96P client demo ===");

    cli.begin({
        [](bool c)   { g_conn = c; Serial.println(c ? "[event] connected" : "[event] disconnected"); },
        [](const w96p::Snapshot& s) { g_snap = s; g_snapDirty = true; },
        [](const char*) {},
    });

    // 1. 扫描
    Serial.println(">> scan 8s (filter: service FFF0)");
    cli.startScan(8);
    pump(9000);
    for (int i = 0; i < cli.foundCount(); i++) {
        const w96p::Client::Found* f = &cli.foundList()[i];
        Serial.printf("  %d %-16s %ddB\n", i + 1, f->name[0] ? f->name : f->addr, f->rssi);
    }
    if (cli.foundCount() == 0) { Serial.println("!! no fan found, halt"); return; }

    // 2. 连接
    cli.stopScan();
    Serial.println(">> connect #1");
    cli.connectIndex(0);
    pump(3000);
    if (!g_conn) { Serial.println("!! connect failed, halt"); return; }

    // 3. 初始快照
    waitSnap();
    printSnap("initial");

    // 4. 档位 0-4 演示（走完回 1 档）
    for (uint8_t g = 1; g <= 4; g++) {
        Serial.printf(">> setPower(%d)\n", g);
        cli.setPower(g);
        pump(1200); waitSnap(2000); printSnap("gear");
    }

    // 5. 无级调速（50% → 回读 → 复原）
    uint8_t origSpeed = g_snap.speed;
    Serial.println(">> setSpeed(50)");
    cli.setSpeed(50);
    pump(1200); waitSnap(2000); printSnap("speed50");
    Serial.printf(">> setSpeed(%d) restore\n", origSpeed);
    cli.setSpeed(origSpeed);
    pump(1200);

    // 6. 定时：1 分钟 → 取消
    Serial.println(">> setTimerMinutes(1)");
    cli.setTimerMinutes(1);
    pump(1200); waitSnap(2000); printSnap("timer1m");
    Serial.println(">> setTimerMinutes(0) cancel");
    cli.setTimerMinutes(0);
    pump(1200);

    // 7. 自然风：读曲线 → 原样写回+保存 → 开 → 关
    uint8_t curve[128]; uint8_t pts; uint32_t total;
    if (cli.readNatureMeta(pts, total)) Serial.printf(">> nature meta: %u points, total %u\n", pts, total);
    if (cli.readNatureCurve(curve)) {
        Serial.printf(">> curve[0..7] = %d %d %d %d %d %d %d %d ...\n",
                      curve[0], curve[1], curve[2], curve[3], curve[4], curve[5], curve[6], curve[7]);
    }
    Serial.println(">> natureCurveDefault() + on -> off");
    cli.natureCurveRestore();
    pump(400);
    cli.setNatureWind(true);
    pump(1500); waitSnap(2000); printSnap("nature on");
    cli.setNatureWind(false);
    pump(1200);

    // 8. Turbo：开 → 读倒计时 → 关
    Serial.println(">> setTurbo(true)");
    cli.setTurbo(true);
    pump(1500); waitSnap(2000); printSnap("turbo on");
    Serial.println(">> setTurbo(false)");
    cli.setTurbo(false);
    pump(1200);

    // 9. 灯光 1-4 各亮一下，停在 4
    for (uint8_t lv = 1; lv <= 4; lv++) {
        Serial.printf(">> setLight(%d)\n", lv);
        cli.setLight(lv);
        pump(800);
    }

    // 10. 杂项：休眠延时/减档模式/档位校准（写默认值 = 无实际变更）
    Serial.println(">> setShutdownDelay(0) / setGearDownMode(0) / setSpeedCalib(default)");
    cli.setShutdownDelay(0);
    cli.setGearDownMode(0);
    cli.setSpeedCalib(w96p::kProfileW96P.gearDefaults);
    pump(1200);

    // 11. 电池容量：读回 capacityMwh 原样写回（演示 BAT_CAP= 但不变更）
    Serial.printf(">> setBatteryCap(%u) same-value\n", unsigned(g_snap.battery.capacityMwh));
    cli.setBatteryCap(g_snap.battery.capacityMwh);
    pump(1000);

    // 12. 电源状态：读 → POW_C_* 原状态写回（反逻辑演示）
    Serial.printf(">> pow switches: cOut=%d cIn=%d cHi=%d -> same-value write\n",
                  g_snap.power.cOutEnabled, g_snap.power.cInEnabled, g_snap.power.cHiEnabled);
    cli.setPowSwitch("POW_C_OUT", g_snap.power.cOutEnabled);
    cli.setPowSwitch("POW_C_IN",  g_snap.power.cInEnabled);
    cli.setPowSwitch("POW_C_HI",  g_snap.power.cHiEnabled);
    pump(1200);

    // 13. 快充配置：读 16B 打印 → 原值写回一个寄存器（读-改-写演示）
    w96p::PowerConfig pc;
    if (cli.readPowerConfig(pc)) {
        Serial.printf(">> powerConfig: lvl %d ver %d sink %d src %d coreTemp %d\n",
                      pc.powLevel, pc.powVer, pc.powSink, pc.powSrc, pc.coreTemp);
        Serial.printf("  regs 1A=%02X 1C=%02X 1D=%02X 1E=%02X 2A=%02X 2B=%02X 2C=%02X\n",
                      pc.r1A, pc.r1C, pc.r1D, pc.r1E, pc.r2A, pc.r2B, pc.r2C);
        Serial.println(">> setPowRegister(6, same) read-modify-write demo");
        cli.setPowRegister(6, pc.r1A);
        pump(1000);
    } else {
        Serial.println("!! readPowerConfig failed");
    }

    waitSnap(2000);
    printSnap("final");
    Serial.println("=== demo done ===");
}

void loop() {
    M5.update();
    cli.update();
}
