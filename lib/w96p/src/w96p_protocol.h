// w96p_protocol.h — Witrn W96P/W66D 风扇 BLE 协议层（纯 C++，无 Arduino/NimBLE 依赖）
// 移植自 w96p-control/docs/ble-protocol.md（与 packages/sdk/src/ble/parsers.ts 逐字段核对）
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace w96p {

// ---------- UUID (128-bit 字符串形式，NimBLE 可直接构造) ----------
namespace uuid {
inline const char* kSvcMain   = "0000fff0-0000-1000-8000-00805f9b34fb"; // 主控
inline const char* kSvcPower  = "0000ffd0-0000-1000-8000-00805f9b34fb"; // 电源
inline const char* kSvcNature = "0000ffe0-0000-1000-8000-00805f9b34fb"; // 自然风
inline const char* kSvcBleCfg = "0000ffc0-0000-1000-8000-00805f9b34fb"; // 蓝牙配置
// 主控 FFF0
inline const char* kPower         = "0000fff1-0000-1000-8000-00805f9b34fb"; // W: 00关 01-04档
inline const char* kTimer         = "0000fff2-0000-1000-8000-00805f9b34fb"; // RW u16BE 秒
inline const char* kFanSpeed      = "0000fff3-0000-1000-8000-00805f9b34fb"; // RW u8 %
inline const char* kNatureWind    = "0000fff4-0000-1000-8000-00805f9b34fb"; // RW 0/1
inline const char* kShutdownDelay = "0000fff5-0000-1000-8000-00805f9b34fb"; // RW u16BE 秒
inline const char* kGearDownMode  = "0000fff6-0000-1000-8000-00805f9b34fb"; // RW 0逐级/1直停
inline const char* kSpeedCalib    = "0000fff7-0000-1000-8000-00805f9b34fb"; // RW 4×u8 %
inline const char* kTurboTime     = "0000fff8-0000-1000-8000-00805f9b34fb"; // RW u16BE 秒(v1.3仅1B)
inline const char* kTurboMode     = "0000fffc-0000-1000-8000-00805f9b34fb"; // RW 0/1 (v1.6+，原FFF9)
inline const char* kLight         = "0000fffa-0000-1000-8000-00805f9b34fb"; // W 0临关/1-4亮度
inline const char* kTurboCountdown= "0000fffb-0000-1000-8000-00805f9b34fb"; // R u16BE 秒
// 蓝牙配置 FFC0
inline const char* kBleName       = "0000ffc1-0000-1000-8000-00805f9b34fb"; // RW UTF-8 ≤17B / BLE_SN
// 电源 FFD0（写为 ASCII "KEY=VALUE,"）
inline const char* kBatteryInfo   = "0000ffd1-0000-1000-8000-00805f9b34fb"; // R 30B / W BAT_CAP=,BAT_CLR=
inline const char* kPowerStatus   = "0000ffd2-0000-1000-8000-00805f9b34fb"; // R ≥11B / W POW_C_OUT/IN/HI,POW_CLR
inline const char* kMotorInfo     = "0000ffd3-0000-1000-8000-00805f9b34fb"; // R ≥4B
inline const char* kPowerConfig   = "0000ffd4-0000-1000-8000-00805f9b34fb"; // R ≥16B / W POW_XX=<byte>,
// 自然风 FFE0
inline const char* kNatureSum     = "0000ffe1-0000-1000-8000-00805f9b34fb"; // R u8 点数
inline const char* kNatureTime    = "0000ffe2-0000-1000-8000-00805f9b34fb"; // R u32BE 总时长
inline const char* kNatureCurve   = "0000ffe3-0000-1000-8000-00805f9b34fb"; // RW 128×u8 %
inline const char* kNatureCtrl    = "0000ffe4-0000-1000-8000-00805f9b34fb"; // W 01保存/02恢复默认
} // namespace uuid

// ---------- 机型档案 ----------
struct Profile {
    bool     parseMotorFull;             // W96P=true(电流+堵转+电压) / W66D=false(仅电流)
    bool     motorPowerUsesMotorVoltage; // W96P=true / W66D=false(用电池电压)
    uint8_t  gearDefaults[4];            // 档位默认风速
    uint32_t defaultCapacityMwh;         // 默认电池容量
};
extern const Profile kProfileW96P;  // {true,  true,  {10,35,70,100}, 17200}
extern const Profile kProfileW66D;  // {false, false, {30,50,70,100}, 17200} —— 未知设备回退走 W66D

// ---------- 读取数据结构 ----------
struct BatteryInfo {          // FFD1，30 字节
    uint16_t voltageMv; int16_t currentMa;      // 正=充电 负=放电
    uint32_t capacityMwh, chgMwh, dchgMwh, rcapMwh;
    int16_t  tempC;
    uint32_t chgTimeS, dchgTimeS;
};
struct PowerStatus {          // FFD2，≥11 字节
    uint32_t vbusMv; int16_t vbusMa;            // vbusMa==0x7FFF → VBUS 未接入
    uint8_t  powC;                              // 0无 1C口输入 2C口输出
    uint8_t  powSta;                            // 0停止 1充电 2放电
    bool     cOutEnabled, cInEnabled, cHiEnabled; // 已从反逻辑(0=使能)转成正逻辑
};
struct MotorInfo {            // FFD3，≥4 字节
    uint16_t currentMa;
    bool     block;                             // (raw & 0xF7) == 1
    uint16_t voltageMv;                         // >20000 视为脏数据 → 0
};
struct PowerConfig {          // FFD4，≥16 字节（位域原样保留，语义见 ble-protocol.md §5.4）
    uint8_t powLevel, powVer, powSink, powSrc;
    int16_t coreTemp;
    uint8_t r1A, r1C, r1D, r1E, r2A, r2B, r2C;  // 寄存器字节，写回必须读-改-写
};

// ---------- 解析（输入原始字节，长度不足返回 false） ----------
bool parseBatteryInfo(const uint8_t* d, size_t n, BatteryInfo& out);
bool parsePowerStatus(const uint8_t* d, size_t n, PowerStatus& out);
bool parseMotorInfo(const uint8_t* d, size_t n, const Profile& p, MotorInfo& out);
bool parsePowerConfig(const uint8_t* d, size_t n, PowerConfig& out);

// ---------- 大端读写（小端主机：memcpy + bswap，编译器优化为 load+rotate） ----------
inline uint16_t u16be(const uint8_t* d) { uint16_t v; memcpy(&v, d, 2); return __builtin_bswap16(v); }
inline uint32_t u32be(const uint8_t* d) { uint32_t v; memcpy(&v, d, 4); return __builtin_bswap32(v); }
inline void putU16be(uint8_t* d, uint16_t v) { v = __builtin_bswap16(v); memcpy(d, &v, 2); }
inline void putU32be(uint8_t* d, uint32_t v) { v = __builtin_bswap32(v); memcpy(d, &v, 4); }

// ---------- 命令构造（写入值填入 buf，返回长度） ----------
size_t buildPower(uint8_t gear);                 // 0关 1-4档 → 1B
size_t buildSpeed(uint8_t pct, uint8_t* buf);    // 0-100 → 1B
size_t buildTimer(uint16_t seconds, uint8_t* buf);   // 0取消 → 2B BE
size_t buildU8(uint8_t v, uint8_t* buf);         // 自然风/减档/Turbo开关 → 1B
size_t buildU16be(uint16_t v, uint8_t* buf);     // 休眠延时/Turbo时间 → 2B BE
size_t buildSpeedCalib(const uint8_t pct4[4], uint8_t* buf); // → 4B
// 电源服务 ASCII 命令："KEY=VALUE,"（末尾逗号）。返回写入长度（不含 \0）。
size_t buildAsciiCmd(char* buf, size_t cap, const char* key, long value);
// 位域读-改-写工具：保留掩码外其余位
inline uint8_t rmw(uint8_t reg, uint8_t mask, uint8_t bits) { return (reg & ~mask) | (bits & mask); }

// ---------- 默认自然风曲线（128 点，协议 §4.3） ----------
extern const uint8_t kDefaultNatureCurve[128];

// ---------- 通信规则常量（协议 §8） ----------
inline constexpr uint32_t kPollIntervalMs   = 500;
inline constexpr uint32_t kNatureMutexDelayMs = 100;  // 关自然风→调档/调速间隔
inline constexpr uint8_t  kWriteRetries     = 3;
inline constexpr uint32_t kWriteRetryDelayMs = 200;

} // namespace w96p
