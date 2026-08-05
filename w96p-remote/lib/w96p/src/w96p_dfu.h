// w96p_dfu.h — W96P DFU 帧协议（仅版本查询用途；不含固件升级）
// 帧: HEAD(0x55) KEY(1B) LEN(2B LE) PAYLOAD(n) CRC8(1B)
// body(offset 2 起, 即 LEN+PAYLOAD) 与 CRC8_TABLE[key] XOR
// CRC8: init 0x89, 对 HEAD+KEY+明文LEN+明文PAYLOAD 计算
// 来源: w96p-control/packages/sdk/src/dfu/{crc8,packageProtocol}.ts
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace w96p { namespace dfu {

extern const uint8_t CRC8_TABLE[256];
inline constexpr uint8_t CRC8_INIT = 0x89;
inline constexpr uint8_t PACKAGE_HEAD = 0x55;
inline constexpr uint8_t CTRL_GET_VERSION = 0x0A;
inline constexpr uint8_t CTRL_GET_SN = 0x0F;
inline constexpr uint8_t DATA_VERSION = 4;
inline constexpr uint8_t DATA_SN = 10;

inline uint8_t updateCrc8(uint8_t crc, uint8_t byte) { return CRC8_TABLE[(crc ^ byte) & 0xFF]; }

// 构造一帧（key=0 调试模式）。buf ≥ 5+payloadLen，返回帧长。
// 加密范围: offset 2 起到帧尾(含 CRC)——与 SDK encryptBody 一致(2026-08-03 修正)
inline size_t buildFrame(uint8_t* buf, uint8_t key, const uint8_t* payload, uint8_t payloadLen) {
    buf[0] = PACKAGE_HEAD;
    buf[1] = key;
    buf[2] = payloadLen;
    buf[3] = 0;
    memcpy(buf + 4, payload, payloadLen);
    uint8_t crc = CRC8_INIT;
    for (size_t i = 0; i < 4 + payloadLen; i++) crc = updateCrc8(crc, buf[i]);
    buf[4 + payloadLen] = crc;
    const uint8_t mask = CRC8_TABLE[key];
    for (size_t i = 2; i < 5 + payloadLen; i++) buf[i] ^= mask;   // body+CRC 加密
    return 5 + payloadLen;
}

// 接收状态机：逐字节 feed，收到完整合法帧返回 payload 长度（否则 0）。
// out 容量需 ≥255。帧间 500ms 超时由调用方管理（调 reset()）。
class FrameParser {
public:
    void reset() { state_ = HEAD; n_ = 0; len_ = 0; key_ = 0; }

    size_t feed(uint8_t b, uint8_t* out) {
        switch (state_) {
        case HEAD:
            if (b == PACKAGE_HEAD) { state_ = KEY; }
            return 0;
        case KEY:
            key_ = b; state_ = LENLO; return 0;
        case LENLO:
            len_ = b ^ CRC8_TABLE[key_]; state_ = LENHI; return 0;
        case LENHI: {
            uint16_t hi = b ^ CRC8_TABLE[key_];
            len_ |= uint16_t(hi) << 8;
            if (len_ == 0 || len_ > 250) { reset(); return 0; }
            n_ = 0; state_ = PAYLOAD; return 0;
        }
        case PAYLOAD:
            out[n_++] = b ^ CRC8_TABLE[key_];
            if (n_ < len_) return 0;
            state_ = CRC; return 0;
        case CRC: {
            // 校验: HEAD+KEY+明文LEN+明文PAYLOAD; 收到的 CRC 字节也是加密的, 先解密
            uint8_t crc = CRC8_INIT;
            crc = updateCrc8(crc, PACKAGE_HEAD);
            crc = updateCrc8(crc, key_);
            crc = updateCrc8(crc, uint8_t(len_ & 0xFF));
            crc = updateCrc8(crc, uint8_t(len_ >> 8));
            for (uint16_t i = 0; i < len_; i++) crc = updateCrc8(crc, out[i]);
            state_ = HEAD;
            if (crc != (uint8_t)(b ^ CRC8_TABLE[key_])) { reset(); return 0; }
            uint16_t got = len_;
            reset();
            return got;
        }
        }
        reset();
        return 0;
    }
private:
    enum State : uint8_t { HEAD, KEY, LENLO, LENHI, PAYLOAD, CRC } state_ = HEAD;
    uint16_t n_ = 0, len_ = 0;
    uint8_t key_ = 0;
};

}} // namespace w96p::dfu
