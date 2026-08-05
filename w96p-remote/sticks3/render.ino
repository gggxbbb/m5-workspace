// ============================== 渲染(全部走 M5.Display + 画布, 设计 §5) ==============================
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
    bar(6, 78, SCR_W - 12, 10, turboTotalS ? (int)(t * 100 / turboTotalS) : 0, C_ORANGE);

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
        bool adjustable = menu[i].type != M_VIEW && menu[i].type != M_BACK && menu[i].type != M_SUBMENU;
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 20, C_WHITE);   // 光标行反白
        uint16_t fg = cur ? C_BLACK : (adjustable ? C_WHITE : C_GREY);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(fg, cur ? C_WHITE : C_BLACK);
        snprintf(buf, sizeof(buf), "%s %s", cur ? ">" : " ", menu[i].name);
        canvas.drawString(buf, 4, y);
        // 当前值(青色)
        const char* v = nullptr;
        switch (menu[i].target) {
        case ET_SPEED: snprintf(buf, sizeof(buf), "%d%%", snap.valid ? snap.speed : 0); v = buf; break;
        case ET_TIMER: snprintf(buf, sizeof(buf), "%s", snap.timerRemainS ? "" : "off");
                        if (snap.timerRemainS) snprintf(buf, sizeof(buf), "%umin", (snap.timerRemainS + 59) / 60);
                        v = buf; break;
        case ET_NATURE: v = snap.natureOn ? "on" : "off"; break;
        case ET_LIGHT:  snprintf(buf, sizeof(buf), "%s", lightEst == 0xFF ? "--" : "");
                        if (lightEst != 0xFF) snprintf(buf, sizeof(buf), "%d", lightEst);
                        v = buf; break;
        default: break;
        }
        if (v) {
            canvas.setTextColor(cur ? C_BLACK : C_CYAN, cur ? C_WHITE : C_BLACK);
            canvas.drawString(v, SCR_W - 4 - canvas.textWidth(v), y);
        }
    }
    // 8 项腾出底部空间, 提示两行分开不溢出(2026-08-05 真机照片反馈)
    txt(4, 196, "A:调节  B:下一项", C_GREY, &fonts::efontCN_12);
    txt(4, 214, "2xB:上一项", C_GREY, &fonts::efontCN_12);
}

static void renderSettings() {
    txt(4, 2, "SETTINGS", C_WHITE);
    snprintf(buf, sizeof(buf), "%d/7", settingsIdx + 1);
    txtR(SCR_W - 4, 4, buf, C_GREY, &fonts::efontCN_12);

    for (int i = 0; i < 7; i++) {
        int y = 30 + i * 20;
        bool cur = (i == settingsIdx);
        bool adjustable = settingsMenu[i].type != M_BACK;
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 20, C_WHITE);
        uint16_t fg = cur ? C_BLACK : (adjustable ? C_WHITE : C_GREY);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(fg, cur ? C_WHITE : C_BLACK);
        snprintf(buf, sizeof(buf), "%s %s", cur ? ">" : " ", settingsMenu[i].name);
        canvas.drawString(buf, 4, y);
        const char* v = nullptr;
        switch (settingsMenu[i].target) {
        case ET_TURBOTIME: snprintf(buf, sizeof(buf), "%us", turboTotalS); v = buf; break;
        case ET_SHUTDOWN:  snprintf(buf, sizeof(buf), "%s", setShutdownS < 0 ? "--" : setShutdownS == 0 ? "never" : "");
                           if (setShutdownS > 0) snprintf(buf, sizeof(buf), "%ds", setShutdownS);
                           v = buf; break;
        case ET_GEARDOWN:  v = setGearDown < 0 ? "--" : setGearDown ? "直停" : "逐级"; break;
        case ET_BLESN:     v = setBleSn < 0 ? "--" : setBleSn ? "on" : "off"; break;
        default: break;
        }
        if (v) {
            canvas.setTextColor(cur ? C_BLACK : C_CYAN, cur ? C_WHITE : C_BLACK);
            canvas.drawString(v, SCR_W - 4 - canvas.textWidth(v), y);
        }
    }
    txt(4, 196, "A:进入/调节  B:下一项", C_GREY, &fonts::efontCN_12);
    txt(4, 214, "2xB:上一项", C_GREY, &fonts::efontCN_12);
}

static void renderPow() {
    txt(4, 2, "电源开关", C_WHITE, &fonts::efontCN_16);
    const char* names[4] = { "C口输出", "C口输入", "高压模式", "返回" };
    bool vals[3] = { snap.power.cOutEnabled, snap.power.cInEnabled, snap.power.cHiEnabled };
    for (int i = 0; i < 4; i++) {
        int y = 40 + i * 28;
        bool cur = (i == powSel);
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 22, C_WHITE);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(cur ? C_BLACK : (i == 3 ? C_GREY : C_WHITE), cur ? C_WHITE : C_BLACK);
        snprintf(buf, sizeof(buf), "%s %s", cur ? ">" : " ", names[i]);
        canvas.drawString(buf, 4, y);
        if (i < 3) {
            const char* v = snap.valid ? (vals[i] ? "on" : "off") : "--";
            canvas.setTextColor(cur ? C_BLACK : (snap.valid && vals[i]) ? C_GREEN : C_GREY, cur ? C_WHITE : C_BLACK);
            canvas.drawString(v, SCR_W - 4 - canvas.textWidth(v), y);
        }
    }
    txt(4, 170, "A:切换/返回  B:下一行", C_GREY, &fonts::efontCN_12);
    txt(4, 188, "2xB:上一行", C_GREY, &fonts::efontCN_12);
}

static void renderCalib() {
    txt(4, 2, "档位校准 %", C_WHITE, &fonts::efontCN_16);
    const char* rows[6] = { "1档", "2档", "3档", "4档", "保存", "放弃" };
    for (int i = 0; i < 6; i++) {
        int y = 34 + i * 24;
        bool cur = (i == calSel);
        if (cur) canvas.fillRect(0, y - 2, SCR_W, 22, C_WHITE);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(cur ? C_BLACK : C_WHITE, cur ? C_WHITE : C_BLACK);
        snprintf(buf, sizeof(buf), "%s %s", cur ? ">" : " ", rows[i]);
        canvas.drawString(buf, 4, y);
        if (i < 4) {
            snprintf(buf, sizeof(buf), "%d", calBuf[i]);
            canvas.setTextColor(cur ? C_BLACK : C_CYAN, cur ? C_WHITE : C_BLACK);
            canvas.drawString(buf, SCR_W - 4 - canvas.textWidth(buf), y);
        }
    }
    txt(4, 190, "A短:-5  A长:+5  B:下一行", C_GREY, &fonts::efontCN_12);
    txt(4, 208, "2xB:上一行", C_GREY, &fonts::efontCN_12);
}

static const char* editTargetName(EditTarget t) {
    switch (t) {
    case ET_SPEED: return "风速"; case ET_TIMER: return "定时"; case ET_NATURE: return "自然风";
    case ET_LIGHT: return "灯光"; case ET_TURBOTIME: return "Turbo时间";
    case ET_SHUTDOWN: return "休眠延时"; case ET_GEARDOWN: return "减档模式"; case ET_BLESN: return "BLE_SN";
    default: return "";
    }
}

static void renderAdjust() {
    txtC(10, editTargetName(editTarget), C_WHITE);

    switch (editType) {
    case M_PERCENT: snprintf(buf, sizeof(buf), "%d %%", adjustVal); break;
    case M_MINUTES: snprintf(buf, sizeof(buf), "%d min", adjustVal); break;
    case M_SECONDS:
        if (editTarget == ET_SHUTDOWN && adjustVal == 0) snprintf(buf, sizeof(buf), "never");
        else snprintf(buf, sizeof(buf), "%d s", adjustVal);
        break;
    case M_TOGGLE:
        if (editTarget == ET_GEARDOWN) snprintf(buf, sizeof(buf), "%s", adjustVal ? "直停" : "逐级");
        else snprintf(buf, sizeof(buf), "%s", adjustVal ? "on" : "off");
        break;
    case M_LIGHT:   snprintf(buf, sizeof(buf), "%d", adjustVal); break;
    default: buf[0] = 0; break;
    }
    txtC(48, buf, C_CYAN, &fonts::Font4);

    switch (editType) {
    case M_PERCENT: txtC(120, "A短按:-5%  A长按:+5%", C_GREY, &fonts::efontCN_12); break;
    case M_MINUTES: txtC(120, "A短按:-30  A长按:+30", C_GREY, &fonts::efontCN_12);
                    txtC(138, "0 = 取消定时", C_GREY, &fonts::efontCN_12); break;
    case M_SECONDS: txtC(120, "A短按:-10s  A长按:+10s", C_GREY, &fonts::efontCN_12);
                    if (editTarget == ET_TURBOTIME) txtC(138, "0 = 恢复默认199s", C_GREY, &fonts::efontCN_12);
                    if (editTarget == ET_SHUTDOWN)  txtC(138, "0 = 永不休眠", C_GREY, &fonts::efontCN_12);
                    break;
    case M_TOGGLE:  txtC(120, "A:切换", C_GREY, &fonts::efontCN_12); break;
    case M_LIGHT:   txtC(120, "A短按:循环0-4", C_GREY, &fonts::efontCN_12); break;
    default: break;
    }
    txtC(170, "B:保存返回",      C_GREY, &fonts::efontCN_12);
    txtC(188, "2xB:放弃",      C_GREY, &fonts::efontCN_12);
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

        // MAC + 版本 + 固件编译时间(版本/编译时间分两行, 单行溢出 2026-08-05 反馈)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(buf, sizeof(buf), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        txt(4, 98, buf, C_GREY, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "FW v%s", APP_VERSION);
        txt(4, 116, buf, C_WHITE, &fonts::efontCN_12);
        snprintf(buf, sizeof(buf), "%s %s", __DATE__, __TIME__);
        txt(4, 134, buf, C_GREY, &fonts::efontCN_12);

        // 实时 IMU(手势调试用)
        Vec3 a = readAccelMs2();
        snprintf(buf, sizeof(buf), "ACC %+.2f %+.2f %+.2f", a.x, a.y, a.z);
        txt(4, 152, buf, C_CYAN, &fonts::efontCN_12);
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
        // RSSI 单列一行(DEV 名开 SN 广播后很长): 在线用实时链路值, 离线回退扫描值
        int rssi = online && linkRssi ? linkRssi : connectedRssi;
        if (connectedName[0] && rssi) {
            snprintf(buf, sizeof(buf), "RSSI %ddB", rssi);
            txt(4, 104, buf, online ? C_CYAN : C_GREY, &fonts::efontCN_12);
        }
        snprintf(buf, sizeof(buf), "MAC %s", connectedAddr[0] ? connectedAddr : "--");
        txt(4, 122, buf, C_GREY, &fonts::efontCN_12);
        if (fwValid) {
            snprintf(buf, sizeof(buf), "FW  v%u.%u", fwMarker / 10, fwMarker % 10);
            txt(4, 140, buf, C_WHITE, &fonts::efontCN_12);
        } else {
            txt(4, 140, "FW  --", C_GREY, &fonts::efontCN_12);
        }
        if (snValid) {
            snprintf(buf, sizeof(buf), "SN  %u", unsigned(snValue));
            txt(4, 158, buf, C_WHITE, &fonts::efontCN_12);
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
            bool isConn = online && strcmp(f->addr, connectedAddr) == 0;   // 按 MAC 判: SN 关闭时多台都叫 W96P
            // 名字无 '_' (SN 广播关)时附 MAC 尾两位区分同名设备
            if (f->name[0] && !strchr(f->name, '_') && strlen(f->addr) >= 5)
                snprintf(buf, sizeof(buf), "%s %s %s%s", cur ? ">" : " ",
                         f->name, f->addr + strlen(f->addr) - 5, isConn ? "*" : "");
            else
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
    txt(4, 204, "B:下一项 2xB:上一项", C_GREY, &fonts::efontCN_12);
    txt(4, 222, "A:执行  B:遍历 2xB:反向", C_GREY, &fonts::efontCN_12);
}

static void renderConnecting() {
    txtC(60, "W96P", C_WHITE);
    txtC(100, pendingConnect ? "连接中…" : "扫描中…", C_CYAN);   // 原误判 online: 连接期间 online 还没置位
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
    case SCR_SETTINGS:   renderSettings();   break;
    case SCR_POW:        renderPow();        break;
    case SCR_CALIB:      renderCalib();      break;
    case SCR_ADJUST:     renderAdjust();     break;
    case SCR_GESTURE:    renderGesture();    break;
    case SCR_TURBO_DASH: renderTurboDash();  break;
    case SCR_DETAILS:    renderDetails();    break;
    case SCR_CONN_MGMT:  renderConnMgmt();   break;
    }
    canvas.pushSprite(0, 0);
}

