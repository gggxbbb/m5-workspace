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
            // 连接即读档位校准转速(FFF7), 换挡后本地立即可知真实 PWM(2026-08-03 反馈)
            uint8_t cal[4];
            if (cli.readSpeedCalib(cal)) { memcpy(gearSpeeds, cal, 4); calibValid = true; }
            // 连接即读 Turbo 时长(FFF8), TurboDash 进度条用真值
            cli.readTurboTime(turboTotalS);
            // 连接即查固件版本与序列号(DFU, 2026-08-03 反馈)
            fwValid = cli.readFwVersion(fwMarker);
            snValid = cli.readSn(snValue);
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

