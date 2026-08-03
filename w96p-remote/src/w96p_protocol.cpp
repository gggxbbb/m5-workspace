// w96p_protocol.cpp — 协议层实现，字段偏移与 packages/sdk/src/ble/parsers.ts 一致
#include "w96p_protocol.h"
#include <cstdio>

namespace w96p {

const Profile kProfileW96P = { true,  true,  {10, 35, 70, 100}, 17200 };
const Profile kProfileW66D = { false, false, {30, 50, 70, 100}, 17200 };

// 线格式打包结构：memcpy 整体拷入，再 bswap 多字节字段
struct __attribute__((packed)) BatteryWire {
    uint16_t voltageMv; int16_t currentMa;
    uint32_t capacityMwh, chgMwh, dchgMwh, rcapMwh;
    int16_t  tempC;
    uint32_t chgTimeS, dchgTimeS;
};
static_assert(sizeof(BatteryWire) == 30, "wire layout drift");

bool parseBatteryInfo(const uint8_t* d, size_t n, BatteryInfo& o) {
    if (n < sizeof(BatteryWire)) return false;
    BatteryWire w; memcpy(&w, d, sizeof(w));
    o.voltageMv   = __builtin_bswap16(w.voltageMv);
    o.currentMa   = int16_t(__builtin_bswap16(w.currentMa));
    o.capacityMwh = __builtin_bswap32(w.capacityMwh);
    o.chgMwh      = __builtin_bswap32(w.chgMwh);
    o.dchgMwh     = __builtin_bswap32(w.dchgMwh);
    o.rcapMwh     = __builtin_bswap32(w.rcapMwh);
    o.tempC       = int16_t(__builtin_bswap16(w.tempC));
    o.chgTimeS    = __builtin_bswap32(w.chgTimeS);
    o.dchgTimeS   = __builtin_bswap32(w.dchgTimeS);
    return true;
}

struct __attribute__((packed)) PowerStatusWire {
    uint32_t vbusMv; int16_t vbusMa;
    uint8_t  powC, powSta, cOut, cIn, cHi;
};
static_assert(sizeof(PowerStatusWire) == 11, "wire layout drift");

bool parsePowerStatus(const uint8_t* d, size_t n, PowerStatus& o) {
    if (n < sizeof(PowerStatusWire)) return false;
    PowerStatusWire w; memcpy(&w, d, sizeof(w));
    o.vbusMv      = __builtin_bswap32(w.vbusMv);
    o.vbusMa      = int16_t(__builtin_bswap16(w.vbusMa)); // 0x7FFF=VBUS未接入，语义留给调用方
    o.powC        = w.powC;
    o.powSta      = w.powSta;
    o.cOutEnabled = w.cOut == 0;   // 反逻辑：0=使能
    o.cInEnabled  = w.cIn  == 0;
    o.cHiEnabled  = w.cHi  == 0;
    return true;
}

bool parseMotorInfo(const uint8_t* d, size_t n, const Profile& p, MotorInfo& o) {
    if (n < 2) return false;
    o.currentMa = u16be(d + 0);
    if (!p.parseMotorFull) { o.block = false; o.voltageMv = 0; return true; }
    const uint8_t rawBlock = n > 2 ? d[2] : 0;
    o.block = (rawBlock & 0xF7) == 1;
    uint16_t v = n >= 2 ? u16be(d + (n - 2)) : 0;
    o.voltageMv = v > 20000 ? 0 : v;   // 脏数据校验
    return true;
}

bool parsePowerConfig(const uint8_t* d, size_t n, PowerConfig& o) {
    if (n < 16) return false;
    // 前 6 字节连续，拷进来再处理；寄存器字节按偏移单取
    memcpy(&o.powLevel, d, 5);
    memcpy(&o.coreTemp, d + 4, 2); o.coreTemp = int16_t(__builtin_bswap16(o.coreTemp));
    o.r1A = d[6];  o.r1C = d[7];  o.r1D = d[8];  o.r1E = d[9];
    o.r2A = d[13]; o.r2B = d[14]; o.r2C = d[15];
    return true;
}

size_t buildPower(uint8_t gear) { return gear > 4 ? 4 : gear; }

size_t buildSpeed(uint8_t pct, uint8_t* buf) {
    buf[0] = pct > 100 ? 100 : pct;
    return 1;
}
size_t buildTimer(uint16_t seconds, uint8_t* buf) { putU16be(buf, seconds); return 2; }
size_t buildU8(uint8_t v, uint8_t* buf) { buf[0] = v; return 1; }
size_t buildU16be(uint16_t v, uint8_t* buf) { putU16be(buf, v); return 2; }
size_t buildSpeedCalib(const uint8_t pct4[4], uint8_t* buf) {
    for (int i = 0; i < 4; i++) buf[i] = pct4[i] > 100 ? 100 : pct4[i];
    return 4;
}

size_t buildAsciiCmd(char* buf, size_t cap, const char* key, long value) {
    int w = snprintf(buf, cap, "%s=%ld,", key, value);   // 协议：末尾逗号
    return w > 0 && size_t(w) < cap ? size_t(w) : 0;
}

const uint8_t kDefaultNatureCurve[128] = {
    55,48,40,33,28,22,21,26,33,41,48,54,58,60,61,58,52,45,37,30,24,20,25,33,40,48,53,57,60,60,56,51,
    43,36,29,23,21,28,37,47,56,63,68,71,72,71,67,62,54,46,36,29,23,20,27,37,48,57,64,69,73,74,76,78,
    80,82,84,86,88,90,89,87,83,77,70,62,53,43,34,27,21,20,26,32,38,43,47,49,50,48,44,38,33,27,24,20,
    21,26,31,37,42,46,48,47,42,36,31,27,23,20,22,27,33,39,44,47,48,46,41,36,30,26,23,20,22,27,33,38
};

} // namespace w96p
