// ============================== BLE 写封装 ==============================
static void fanSetPower(uint8_t gear) {
    if (cli.setPower(gear)) gearEst = gear;
    dirty = true;
}
static void fanSetSpeed(uint8_t pct) {
    cli.setSpeed(pct);
    dirty = true;
}
static void fanSetTimerMin(uint16_t m) {
    cli.setTimerMinutes(m);
    dirty = true;
}
static void fanSetNature(bool on) {
    cli.setNatureWind(on);
    dirty = true;
}
static void fanSetTurbo(bool on) {
    cli.setTurbo(on);
    dirty = true;
}
static void fanSetLight(uint8_t lv) {
    cli.setLight(lv);
    lightEst = lv;  // 灯光无回读, 只信本机下发值
    dirty = true;
}

// ---- 扫描/连接封装 ----
