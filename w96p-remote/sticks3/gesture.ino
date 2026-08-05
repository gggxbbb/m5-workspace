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

