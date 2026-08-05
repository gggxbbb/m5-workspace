// w96p_client.cpp — W96P 风扇 NimBLE client 实现（NimBLE-Arduino 2.5.x / ESP32-S3）
// 协议行为规则见 docs/ble-protocol.md §8：
//   §8.1  写入串行化（FIFO 队列，update() 逐个执行）
//   §8.2  自然风互斥（调档/调速前先写 NATURE_WIND=00，间隔 100ms）
//   §8.3  写队列非空时跳过 500ms 轮询
//   §8.6  写失败重试 3×200ms；NATURE_CURVE 读取额外重试 2 次
//   §8.10 连接后缓存全部特征
//   §8.11 断开时清理缓存与队列
#include "w96p_client.h"
#include "w96p_dfu.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <cstdio>

namespace w96p {

namespace {
constexpr int     kMaxFound      = 16;   // 扫描候选上限
constexpr int     kMaxQueue      = 16;   // 写队列上限
constexpr size_t  kMaxWriteBytes = 128 + 2;
constexpr uint32_t kConnectTimeoutMs = 5000;
} // namespace

// ---------- 写队列任务 ----------
struct WriteJob {
    char     svc[40];                       // 服务 UUID（调试用，查找按特征 UUID）
    char     chr[40];                       // 特征 UUID
    uint8_t  bytes[kMaxWriteBytes];
    uint8_t  len;
    bool     isAscii;                       // 电源服务 ASCII "KEY=VALUE," 命令
    bool     noResp;                        // writeWithoutResponse（FFF3/FFE3）
    uint8_t  attempts;                      // 已尝试次数
    uint32_t notBeforeMs;                   // 自然风互斥/重试延时
};

// ---------- PIMPL ----------
struct Client::Impl {
    Cb cb;

    // 扫描
    NimBLEScan* scan = nullptr;
    Found       found[kMaxFound]{};
    NimBLEAddress foundAddr[kMaxFound];     // 与 found[] 平行，connectIndex 用
    int         foundN = 0;
    bool        scanning = false;

    // 连接
    NimBLEClient* client = nullptr;
    bool          isConn = false;           // 逻辑连接态（callback 维护 + update() 轮询兜底）
    Snapshot      snap{};
    uint32_t      lastPollMs = 0;

    // 特征缓存（§8.10）
    struct CharSlot { const char* chrUuid; NimBLERemoteCharacteristic* chr; };
    CharSlot slots[21] = {
        {uuid::kPower, nullptr},         {uuid::kTimer, nullptr},
        {uuid::kFanSpeed, nullptr},      {uuid::kNatureWind, nullptr},
        {uuid::kShutdownDelay, nullptr}, {uuid::kGearDownMode, nullptr},
        {uuid::kSpeedCalib, nullptr},    {uuid::kTurboMode, nullptr},
        {uuid::kLight, nullptr},         {uuid::kTurboCountdown, nullptr},
        {uuid::kTurboTime, nullptr},
        {uuid::kBleName, nullptr},
        {uuid::kBatteryInfo, nullptr},   {uuid::kPowerStatus, nullptr},
        {uuid::kMotorInfo, nullptr},
        {uuid::kNatureSum, nullptr},     {uuid::kNatureTime, nullptr},
        {uuid::kNatureCurve, nullptr},   {uuid::kNatureCtrl, nullptr},
        {uuid::kDfuWrite, nullptr},      {uuid::kDfuNotify, nullptr},
    };

    // 写队列（环形 FIFO）
    WriteJob queue[kMaxQueue];
    int      qHead = 0, qCount = 0;
    uint32_t nextAttemptMs = 0;             // 队首任务下次可执行时间

    // ---------- DFU（版本/SN 查询；FEE2 notify → 帧解析） ----------
    dfu::FrameParser dfuParser;
    volatile bool fwVerPending = false;
    volatile bool snPending = false;
    uint8_t fwVerMarker = 0;
    uint32_t snValue = 0;

    void onDfuNotify(const uint8_t* data, size_t len) {
        uint8_t payload[250];
        for (size_t i = 0; i < len; i++) {
            size_t n = dfuParser.feed(data[i], payload);
            if (n >= 2) {
                Serial.printf("[w96p] dfu rx type=%u len=%u\n", payload[0] & 0x7F, (unsigned)n);  // 临时诊断
                if ((payload[0] & 0x7F) == dfu::DATA_VERSION) {
                    fwVerMarker = payload[1];       // marker = major*10 + minor
                    fwVerPending = true;
                } else if ((payload[0] & 0x7F) == dfu::DATA_SN && n >= 5) {
                    memcpy(&snValue, payload + 1, 4);   // little-endian uint32
                    snPending = true;
                }
            }
        }
    }

    // 发送控制命令并等响应（bit7=ACK）
    bool dfuRoundtrip(uint8_t ctrl, volatile bool& flag, uint32_t timeoutMs) {
        NimBLERemoteCharacteristic* chr = findChar(uuid::kDfuWrite);
        if (!chr) { Serial.println("[w96p] dfu: FEE1 not found"); return false; }
        uint8_t frame[8];
        size_t n = dfu::buildFrame(frame, 0, &ctrl, 1);
        flag = false;
        if (!chr->writeValue(frame, uint16_t(n), true)) { Serial.println("[w96p] dfu: write fail"); return false; }
        uint32_t t0 = millis();
        while (millis() - t0 < timeoutMs) {
            if (flag) return true;
            delay(10);
        }
        Serial.printf("[w96p] dfu: ctrl 0x%02X timeout\n", ctrl);
        return false;
    }

    // ---- NimBLE 回调对象（必须是成员，生命周期覆盖整个连接） ----
    struct ScanCb : NimBLEScanCallbacks {
        Impl* self;
        explicit ScanCb(Impl* s) : self(s) {}
        void onResult(const NimBLEAdvertisedDevice* d) override { self->onScanResult(d); }
        void onScanEnd(const NimBLEScanResults&, int) override { self->scanning = false; }
    } scanCb{this};

    struct ClientCb : NimBLEClientCallbacks {
        Impl* self;
        explicit ClientCb(Impl* s) : self(s) {}
        void onConnect(NimBLEClient*) override {}
        void onDisconnect(NimBLEClient*, int reason) override {
            self->handleDisconnect();       // §8.11
        }
    } clientCb{this};

    // ---------- 扫描 ----------
    void onScanResult(const NimBLEAdvertisedDevice* d) {
        if (foundN >= kMaxFound) return;
        if (!d->isAdvertisingService(NimBLEUUID(uuid::kSvcMain))) return;  // 按 FFF0 过滤

        const std::string addrStr = d->getAddress().toString();
        for (int i = 0; i < foundN; i++) {                                 // 按地址去重
            if (addrStr == found[i].addr) return;
        }
        Found& f = found[foundN];
        strncpy(f.addr, addrStr.c_str(), sizeof(f.addr) - 1);
        f.addr[sizeof(f.addr) - 1] = '\0';
        const std::string name = d->getName();
        strncpy(f.name, name.c_str(), sizeof(f.name) - 1);
        f.name[sizeof(f.name) - 1] = '\0';
        f.rssi = d->getRSSI();
        foundAddr[foundN] = d->getAddress();
        foundN++;
        if (cb.onFound) cb.onFound(f.name);
    }

    // ---------- 特征缓存 ----------
    NimBLERemoteCharacteristic* findChar(const char* chrUuid) const {
        for (const auto& s : slots) {
            if (strcmp(s.chrUuid, chrUuid) == 0) return s.chr;
        }
        return nullptr;
    }

    void clearChars() {
        for (auto& s : slots) s.chr = nullptr;
    }

    void cacheChars() {
        clearChars();
        if (!client) return;
        // 每个特征所属服务（协议 §7 总览表）
        struct Map { const char* chr; const char* svc; };
        static const Map kMap[] = {
            {uuid::kPower, uuid::kSvcMain},         {uuid::kTimer, uuid::kSvcMain},
            {uuid::kFanSpeed, uuid::kSvcMain},      {uuid::kNatureWind, uuid::kSvcMain},
            {uuid::kShutdownDelay, uuid::kSvcMain}, {uuid::kGearDownMode, uuid::kSvcMain},
            {uuid::kSpeedCalib, uuid::kSvcMain},    {uuid::kTurboMode, uuid::kSvcMain},
            {uuid::kLight, uuid::kSvcMain},         {uuid::kTurboCountdown, uuid::kSvcMain},
            {uuid::kTurboTime, uuid::kSvcMain},
            {uuid::kBleName, uuid::kSvcBleCfg},
            {uuid::kBatteryInfo, uuid::kSvcPower},  {uuid::kPowerStatus, uuid::kSvcPower},
            {uuid::kMotorInfo, uuid::kSvcPower},
            {uuid::kNatureSum, uuid::kSvcNature},   {uuid::kNatureTime, uuid::kSvcNature},
            {uuid::kNatureCurve, uuid::kSvcNature}, {uuid::kNatureCtrl, uuid::kSvcNature},
            {uuid::kDfuWrite, uuid::kSvcDfu},       {uuid::kDfuNotify, uuid::kSvcDfu},
        };
        for (const auto& m : kMap) {
            NimBLERemoteService* svc = client->getService(NimBLEUUID(m.svc));
            if (!svc) continue;
            for (auto& s : slots) {
                if (strcmp(s.chrUuid, m.chr) == 0) {
                    s.chr = svc->getCharacteristic(NimBLEUUID(m.chr));
                    break;
                }
            }
        }
    }

    // ---------- 断开清理（§8.11） ----------
    void handleDisconnect() {
        const bool was = isConn;
        isConn = false;
        clearChars();
        qHead = 0; qCount = 0;              // 丢弃未完成的写任务
        nextAttemptMs = 0;
        snap.valid = false;
        if (was && cb.onConn) cb.onConn(false);
    }

    // ---------- 写队列 ----------
    bool enqueue(const char* svcUuid, const char* chrUuid,
                 const uint8_t* data, uint8_t len, bool isAscii) {
        if (!isConn || !client || !client->isConnected()) return false;
        if (qCount >= kMaxQueue || len > kMaxWriteBytes) return false;
        WriteJob& j = queue[(qHead + qCount) % kMaxQueue];
        strncpy(j.svc, svcUuid, sizeof(j.svc) - 1); j.svc[sizeof(j.svc) - 1] = '\0';
        strncpy(j.chr, chrUuid, sizeof(j.chr) - 1); j.chr[sizeof(j.chr) - 1] = '\0';
        memcpy(j.bytes, data, len);
        j.len      = len;
        j.isAscii  = isAscii;
        // 协议：FFF3/FFE3 优先 writeWithoutResponse
        j.noResp   = strcmp(chrUuid, uuid::kFanSpeed) == 0 || strcmp(chrUuid, uuid::kNatureCurve) == 0;
        j.attempts = 0;
        j.notBeforeMs = 0;
        qCount++;
        return true;
    }

    // 自然风互斥（§8.2）：调速/调档前若自然风开着，先关并延时 100ms
    void natureMutexGuard(uint32_t& delayMs) {
        delayMs = 0;
        if (snap.valid && snap.natureOn) {
            uint8_t off = 0;
            if (enqueue(uuid::kSvcMain, uuid::kNatureWind, &off, 1, false)) {
                delayMs = kNatureMutexDelayMs;
            }
        }
    }

    void processQueue() {
        if (qCount == 0 || !isConn) return;
        WriteJob& j = queue[qHead];
        const uint32_t now = millis();
        // 无符号差值比较，millis() 回绕安全
        if (int32_t(now - j.notBeforeMs) < 0 || int32_t(now - nextAttemptMs) < 0) return;

        NimBLERemoteCharacteristic* chr = findChar(j.chr);
        bool ok = chr && chr->writeValue(j.bytes, j.len, !j.noResp);
        if (ok) {
            qHead = (qHead + 1) % kMaxQueue;
            qCount--;
            nextAttemptMs = 0;
            return;
        }
        if (++j.attempts >= kWriteRetries) {    // 重试耗尽，丢弃（§8.6）
            qHead = (qHead + 1) % kMaxQueue;
            qCount--;
            nextAttemptMs = 0;
        } else {
            nextAttemptMs = now + kWriteRetryDelayMs;
        }
    }

    // ---------- 轮询（§8.3） ----------
    // 读到的值保存在调用方的 NimBLEAttValue 中（readValue 返回的对象析构会释放缓冲区，
    // 不能返回内部指针）
    static bool readChar(NimBLERemoteCharacteristic* chr, NimBLEAttValue& v) {
        if (!chr || !chr->canRead()) return false;
        v = chr->readValue();
        return v.size() > 0;
    }

    void poll() {
        NimBLEAttValue v;

        if (readChar(findChar(uuid::kBatteryInfo), v))
            parseBatteryInfo(v.data(), v.size(), snap.battery);
        if (readChar(findChar(uuid::kPowerStatus), v))
            parsePowerStatus(v.data(), v.size(), snap.power);
        if (readChar(findChar(uuid::kMotorInfo), v))
            parseMotorInfo(v.data(), v.size(), kProfileW96P, snap.motor);   // 原型目标 W96P
        if (readChar(findChar(uuid::kFanSpeed), v) && v.size() >= 1)
            snap.speed = v.data()[0];
        if (readChar(findChar(uuid::kTimer), v) && v.size() >= 2)
            snap.timerRemainS = u16be(v.data());
        if (readChar(findChar(uuid::kNatureWind), v) && v.size() >= 1)
            snap.natureOn = v.data()[0];
        if (readChar(findChar(uuid::kTurboCountdown), v) && v.size() >= 2)
            snap.turboRemainS = u16be(v.data());

        snap.valid = true;
        snap.updatedMs = millis();
        lastPollMs = snap.updatedMs;
        if (cb.onSnapshot) cb.onSnapshot(snap);
    }

    // ---------- 分摊轮询 ----------
    // 7 个特征每轮 update() 只读 1 个（round-robin），单个 GATT RTT 的阻塞取代
    // 7 连读的长阻塞——手持遥控器场景下按键不被饿死。完整周期 ≈ kPollIntervalMs。
    uint8_t  pollSlot = 0;
    uint32_t lastSlotMs = 0;

    void pollStep() {
        constexpr uint32_t kSlotMs = kPollIntervalMs / 7;
        uint32_t now = millis();
        if (now - lastSlotMs < kSlotMs) return;
        lastSlotMs = now;

        NimBLEAttValue v;
        bool done = false;
        switch (pollSlot) {
        case 0: if (readChar(findChar(uuid::kBatteryInfo), v))
                    parseBatteryInfo(v.data(), v.size(), snap.battery); break;
        case 1: if (readChar(findChar(uuid::kPowerStatus), v))
                    parsePowerStatus(v.data(), v.size(), snap.power); break;
        case 2: if (readChar(findChar(uuid::kMotorInfo), v))
                    parseMotorInfo(v.data(), v.size(), kProfileW96P, snap.motor); break;
        case 3: if (readChar(findChar(uuid::kFanSpeed), v) && v.size() >= 1)
                    snap.speed = v.data()[0]; break;
        case 4: if (readChar(findChar(uuid::kTimer), v) && v.size() >= 2)
                    snap.timerRemainS = u16be(v.data()); break;
        case 5: if (readChar(findChar(uuid::kNatureWind), v) && v.size() >= 1)
                    snap.natureOn = v.data()[0]; break;
        case 6: if (readChar(findChar(uuid::kTurboCountdown), v) && v.size() >= 2)
                    snap.turboRemainS = u16be(v.data());
                done = true; break;
        }
        pollSlot = (pollSlot + 1) % 7;
        if (done) {   // 完整周期结束才发快照
            snap.valid = true;
            snap.updatedMs = now;
            lastPollMs = now;
            if (cb.onSnapshot) cb.onSnapshot(snap);
        }
    }
};

// ================= 公共 API =================

void Client::begin(Cb cb) {
    if (!impl_) impl_ = new Impl();
    impl_->cb = std::move(cb);
    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("w96p-remote");
    }
    NimBLEScan* scan = NimBLEDevice::getScan();
    impl_->scan = scan;
    scan->setScanCallbacks(&impl_->scanCb, false);  // 自己按地址去重，不用库去重
    scan->setInterval(100);
    scan->setWindow(100);
    scan->setActiveScan(true);                      // 需要扫描响应里的设备名
}

void Client::startScan(uint32_t seconds) {
    if (!impl_ || !impl_->scan) return;
    Impl& im = *impl_;
    if (im.scan->isScanning()) im.scan->stop();
    im.foundN = 0;
    im.scan->clearResults();
    // NimBLE 2.5.x start() 时长单位为毫秒，非阻塞（结果走回调）
    im.scanning = im.scan->start(seconds * 1000, false, true);
}

void Client::stopScan() {
    if (!impl_ || !impl_->scan) return;
    impl_->scan->stop();
    impl_->scanning = false;
}

int Client::foundCount() const { return impl_ ? impl_->foundN : 0; }
const Client::Found* Client::foundList() const { return impl_ ? impl_->found : nullptr; }

bool Client::connectIndex(int i) {
    if (!impl_ || i < 0 || i >= impl_->foundN) return false;
    Impl& im = *impl_;
    stopScan();

    // 旧 client 一律销毁重建，避免携带过期服务缓存
    if (im.client) {
        if (im.client->isConnected()) im.client->disconnect();
        NimBLEDevice::deleteClient(im.client);
        im.client = nullptr;
    }
    im.handleDisconnect();

    NimBLEClient* c = NimBLEDevice::createClient();
    if (!c) {
        if (im.cb.onConn) im.cb.onConn(false);
        return false;
    }
    c->setClientCallbacks(&im.clientCb, false);
    c->setConnectTimeout(kConnectTimeoutMs);
    im.client = c;

    if (!c->connect(im.foundAddr[i])) {             // 阻塞连接，超时 5s
        NimBLEDevice::deleteClient(c);
        im.client = nullptr;
        if (im.cb.onConn) im.cb.onConn(false);
        return false;
    }

    im.isConn = true;
    im.cacheChars();                                // §8.10
    // 订阅 FEE2 版本响应（DFU 查询用）
    if (NimBLERemoteCharacteristic* dfuNtf = im.findChar(uuid::kDfuNotify)) {
        Impl* self = &im;
        dfuNtf->subscribe(true, [self](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
            self->onDfuNotify(d, n);
        });
    }
    im.lastPollMs = 0;                              // 立即触发首轮轮询
    if (im.cb.onConn) im.cb.onConn(true);
    return true;
}

void Client::disconnect() {
    if (!impl_) return;
    if (impl_->client && impl_->client->isConnected()) {
        impl_->client->disconnect();                // onDisconnect 回调里做清理
    }
    impl_->handleDisconnect();                      // 兜底：回调异常时也保证状态一致
}

bool Client::connected() const {
    return impl_ && impl_->isConn && impl_->client && impl_->client->isConnected();
}

void Client::update() {
    if (!impl_) return;
    Impl& im = *impl_;

    // 断线兜底检测（onDisconnect 正常会先触发，这里只补漏）
    if (im.isConn && (!im.client || !im.client->isConnected())) {
        im.handleDisconnect();
    }
    if (!im.isConn) return;

    im.processQueue();                              // §8.1 串行写

    // §8.3：写队列非空时跳过轮询；否则每次 update 最多一个读（按键不被饿死）
    if (im.qCount == 0) {
        im.pollStep();
    }
}

// ---------- 写入 API（全部入队，update() 串行执行） ----------

bool Client::setPower(uint8_t gear) {
    if (!impl_) return false;
    uint32_t delayMs;
    impl_->natureMutexGuard(delayMs);               // §8.2
    const uint8_t b = uint8_t(buildPower(gear));
    if (!impl_->enqueue(uuid::kSvcMain, uuid::kPower, &b, 1, false)) return false;
    impl_->queue[(impl_->qHead + impl_->qCount - 1) % kMaxQueue].notBeforeMs = millis() + delayMs;
    return true;
}

bool Client::setSpeed(uint8_t pct) {
    if (!impl_) return false;
    uint32_t delayMs;
    impl_->natureMutexGuard(delayMs);               // §8.2
    uint8_t b;
    buildSpeed(pct, &b);
    if (!impl_->enqueue(uuid::kSvcMain, uuid::kFanSpeed, &b, 1, false)) return false;
    impl_->queue[(impl_->qHead + impl_->qCount - 1) % kMaxQueue].notBeforeMs = millis() + delayMs;
    return true;
}

bool Client::setTimerMinutes(uint16_t min) {
    if (!impl_) return false;
    if (min > 480) min = 480;                       // 协议范围 0~480 分钟
    uint8_t b[2];
    buildTimer(uint16_t(min * 60), b);              // buildTimer 接收秒
    return impl_->enqueue(uuid::kSvcMain, uuid::kTimer, b, 2, false);
}

bool Client::setNatureWind(bool on) {
    if (!impl_) return false;
    uint8_t b;
    buildU8(on ? 1 : 0, &b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kNatureWind, &b, 1, false);
}

bool Client::natureCurveDefault() {
    if (!impl_) return false;
    // 写 128 点默认曲线（writeWithoutResponse 优先），再 FFE4=01 保存
    if (!impl_->enqueue(uuid::kSvcNature, uuid::kNatureCurve, kDefaultNatureCurve, 128, false))
        return false;
    uint8_t save = 0x01;
    return impl_->enqueue(uuid::kSvcNature, uuid::kNatureCtrl, &save, 1, false);
}

bool Client::natureCurveRestore() {
    if (!impl_) return false;
    uint8_t b = 0x02;                               // 恢复默认
    return impl_->enqueue(uuid::kSvcNature, uuid::kNatureCtrl, &b, 1, false);
}

bool Client::setTurbo(bool on) {
    if (!impl_) return false;
    uint8_t b;
    buildU8(on ? 1 : 0, &b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kTurboMode, &b, 1, false);
}

bool Client::setLight(uint8_t level) {
    if (!impl_) return false;
    uint8_t b;
    buildU8(level > 4 ? 4 : level, &b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kLight, &b, 1, false);
}

bool Client::setShutdownDelay(uint16_t sec) {
    if (!impl_) return false;
    if (sec >= 1 && sec <= 9) sec = 10;             // 协议：1~9 自动修正为 10
    uint8_t b[2];
    buildU16be(sec, b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kShutdownDelay, b, 2, false);
}

bool Client::setGearDownMode(uint8_t mode) {
    if (!impl_) return false;
    uint8_t b;
    buildU8(mode ? 1 : 0, &b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kGearDownMode, &b, 1, false);
}

bool Client::setSpeedCalib(const uint8_t pct4[4]) {
    if (!impl_ || !pct4) return false;
    uint8_t b[4];
    buildSpeedCalib(pct4, b);
    return impl_->enqueue(uuid::kSvcMain, uuid::kSpeedCalib, b, 4, false);
}

// ---- 电源服务 ASCII 命令（§8.4："KEY=VALUE," 末尾逗号） ----

bool Client::setBatteryCap(uint32_t mwh) {
    if (!impl_) return false;
    char cmd[24];
    const size_t n = buildAsciiCmd(cmd, sizeof(cmd), "BAT_CAP", long(mwh));
    return n && impl_->enqueue(uuid::kSvcPower, uuid::kBatteryInfo,
                               reinterpret_cast<const uint8_t*>(cmd), uint8_t(n), true);
}

bool Client::batteryClear() {
    if (!impl_) return false;
    char cmd[16];
    const size_t n = buildAsciiCmd(cmd, sizeof(cmd), "BAT_CLR", 0);
    return n && impl_->enqueue(uuid::kSvcPower, uuid::kBatteryInfo,
                               reinterpret_cast<const uint8_t*>(cmd), uint8_t(n), true);
}

bool Client::setPowSwitch(const char* key, bool enable) {
    if (!impl_ || !key) return false;
    char cmd[24];
    // §5.2 反逻辑：使能写 0，关闭写 1
    const size_t n = buildAsciiCmd(cmd, sizeof(cmd), key, enable ? 0 : 1);
    return n && impl_->enqueue(uuid::kSvcPower, uuid::kPowerStatus,
                               reinterpret_cast<const uint8_t*>(cmd), uint8_t(n), true);
}

bool Client::powerClear() {
    if (!impl_) return false;
    char cmd[16];
    const size_t n = buildAsciiCmd(cmd, sizeof(cmd), "POW_CLR", 0);
    return n && impl_->enqueue(uuid::kSvcPower, uuid::kPowerStatus,
                               reinterpret_cast<const uint8_t*>(cmd), uint8_t(n), true);
}

bool Client::setBleSn(bool on) {
    if (!impl_) return false;
    char cmd[16];
    const size_t n = buildAsciiCmd(cmd, sizeof(cmd), "BLE_SN", on ? 1 : 0);
    return n && impl_->enqueue(uuid::kSvcBleCfg, uuid::kBleName,
                               reinterpret_cast<const uint8_t*>(cmd), uint8_t(n), true);
}

// ---------- 单次阻塞读取（§8.6 重试规则） ----------

namespace {
// 阻塞读 + 重试：attempts 次，间隔 kWriteRetryDelayMs
bool blockingRead(NimBLERemoteCharacteristic* chr, int attempts,
                  const uint8_t*& data, size_t& n, NimBLEAttValue& keep) {
    for (int i = 0; i < attempts; i++) {
        if (chr && chr->canRead()) {
            keep = chr->readValue();
            if (keep.size() > 0) {
                data = keep.data();
                n    = keep.size();
                return true;
            }
        }
        if (i + 1 < attempts) delay(kWriteRetryDelayMs);
    }
    return false;
}
} // namespace

bool Client::readFwVersion(uint8_t& marker) {
    if (!impl_ || !connected()) return false;
    uint8_t ctrl = dfu::CTRL_GET_VERSION | 0x80;
    if (!impl_->dfuRoundtrip(ctrl, impl_->fwVerPending, 800)) return false;
    marker = impl_->fwVerMarker;
    return true;
}

bool Client::readSn(uint32_t& sn) {
    if (!impl_ || !connected()) return false;
    uint8_t ctrl = dfu::CTRL_GET_SN | 0x80;
    if (!impl_->dfuRoundtrip(ctrl, impl_->snPending, 800)) return false;
    sn = impl_->snValue;
    return true;
}

bool Client::readNatureCurve(uint8_t out128[128]) {
    if (!connected() || !out128) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    // §8.6：128 字节读取额外重试 2 次
    if (!blockingRead(impl_->findChar(uuid::kNatureCurve), kWriteRetries + 2, d, n, keep))
        return false;
    if (n < 128) return false;
    memcpy(out128, d, 128);
    return true;
}

bool Client::readNatureMeta(uint8_t& points, uint32_t& totalTime) {
    if (!connected()) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kNatureSum), kWriteRetries, d, n, keep) || n < 1)
        return false;
    points = d[0];
    if (!blockingRead(impl_->findChar(uuid::kNatureTime), kWriteRetries, d, n, keep) || n < 4)
        return false;
    totalTime = u32be(d);
    return true;
}

bool Client::readSpeedCalib(uint8_t out4[4]) {
    if (!connected() || !out4) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kSpeedCalib), kWriteRetries, d, n, keep) || n < 4)
        return false;
    memcpy(out4, d, 4);
    return true;
}

bool Client::readTurboTime(uint16_t& sec) {
    if (!connected()) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kTurboTime), kWriteRetries, d, n, keep) || n < 1)
        return false;
    sec = n >= 2 ? u16be(d) : d[0];   // v1.3 单字节, v1.5+ 两字节
    if (sec == 0) sec = 199;          // 0 = 默认 199s
    return true;
}

bool Client::setTurboTime(uint16_t sec) {
    if (!impl_) return false;
    uint8_t buf[2];
    const size_t n = buildU16be(sec, buf);
    return impl_->enqueue(uuid::kSvcMain, uuid::kTurboTime, buf, uint8_t(n), false);
}

bool Client::readShutdownDelay(uint16_t& sec) {
    if (!connected()) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kShutdownDelay), kWriteRetries, d, n, keep) || n < 2)
        return false;
    sec = u16be(d);
    return true;
}

bool Client::readGearDownMode(uint8_t& mode) {
    if (!connected()) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kGearDownMode), kWriteRetries, d, n, keep) || n < 1)
        return false;
    mode = d[0];
    return true;
}

bool Client::readBleSn(bool& enabled) {
    if (!connected()) return false;
    const uint8_t* d = nullptr;
    size_t n = 0;
    NimBLEAttValue keep;
    if (!blockingRead(impl_->findChar(uuid::kBleName), kWriteRetries, d, n, keep) || n < 1)
        return false;
    // FFC1 读回 ASCII 文本, 含 "BLE_SN=1" 即开启(web SDK readBleSn 同款判定)
    enabled = false;
    for (size_t i = 0; i + 7 <= n; i++) {
        if (memcmp(d + i, "BLE_SN=", 7) == 0) {
            enabled = (i + 7 < n) && d[i + 7] == '1';
            return true;
        }
        if (memcmp(d + i, "BLE_SN1", 7) == 0) { enabled = true; return true; }
    }
    enabled = false;
    return true;
}

} // namespace w96p
