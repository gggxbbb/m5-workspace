// w96p_client.h — W96P 风扇 NimBLE client（ESP32-S3 / NimBLE-Arduino 2.5.x）
#pragma once
#include "w96p_protocol.h"
#include <functional>

namespace w96p {

// 聚合快照：500ms 轮询刷新；写入期间暂停（协议 §8.3）
struct Snapshot {
    bool        valid = false;
    BatteryInfo battery{};
    PowerStatus power{};
    MotorInfo   motor{};
    uint8_t     speed = 0;        // FFF3 回读 %
    uint16_t    timerRemainS = 0; // FFF2 回读剩余秒
    uint8_t     natureOn = 0;     // FFF4
    uint16_t    turboRemainS = 0; // FFFB
    uint32_t    updatedMs = 0;    // millis() 时间戳
};

class Client {
public:
    struct Cb {
        std::function<void(bool connected)>           onConn;      // 连接状态变化
        std::function<void(const Snapshot&)>          onSnapshot;  // 每轮轮询后
        std::function<void(const char* name)>         onFound;     // 扫描发现候选设备
    };

    // 扫描到的候选设备
    struct Found { char addr[18]; char name[32]; int rssi; };

    void begin(Cb cb);
    void startScan(uint32_t seconds = 8);   // 按 FFF0 服务过滤，结果经 onFound 回调 + foundList()
    void stopScan();
    int  foundCount() const;
    const Found* foundList() const;
    bool connectIndex(int i);               // 连接扫描结果第 i 个
    void disconnect();
    bool connected() const;
    void update();                          // loop() 中调用：驱动轮询/写队列

    // ---- 写入（自动排队串行化，协议 §8.1；失败重试 3×200ms；返回是否入队成功）----
    bool setPower(uint8_t gear);            // 0关 1-4档（自动处理自然风互斥，协议 §8.2）
    bool setSpeed(uint8_t pct);             // 0-100（同上互斥处理）
    bool setTimerMinutes(uint16_t min);     // 0取消，1-480
    bool setNatureWind(bool on);
    bool natureCurveDefault();              // 写默认曲线 + FFE4 保存
    bool natureCurveRestore();              // FFE4=02 恢复默认
    bool setTurbo(bool on);                 // FFFC
    bool setLight(uint8_t level);           // 0-4
    bool setShutdownDelay(uint16_t sec);
    bool setGearDownMode(uint8_t mode);
    bool setSpeedCalib(const uint8_t pct4[4]);
    bool setBatteryCap(uint32_t mwh);       // BAT_CAP=
    bool batteryClear();                    // BAT_CLR=0,
    bool setPowSwitch(const char* key, bool enable); // POW_C_OUT/POW_C_IN/POW_C_HI（内部转反逻辑）
    bool powerClear();                      // POW_CLR=0,
    bool setBleSn(bool on);                 // BLE_SN=1/0,

    // ---- 单次读取（阻塞，连接后有效；返回 false=失败）----
    bool readNatureCurve(uint8_t out128[128]);
    bool readNatureMeta(uint8_t& points, uint32_t& totalTime);
    bool readSpeedCalib(uint8_t out4[4]);   // FFF7 各档位校准转速 %，连接后读一次
    bool readFwVersion(uint8_t& marker);    // DFU 版本查询, marker = major*10+minor
    bool readSn(uint32_t& sn);              // DFU 序列号查询(LE uint32)
    bool readTurboTime(uint16_t& sec);      // FFF8 Turbo 时长(0→199 默认)
    bool setTurboTime(uint16_t sec);        // FFF8 写 Turbo 时长 1-600(0=恢复默认)
    bool readShutdownDelay(uint16_t& sec);  // FFF5 休眠延时(0=永不)
    bool readGearDownMode(uint8_t& mode);   // FFF6 0逐级/1直停
    bool readBleSn(bool& enabled);          // FFC1 文本解析 BLE_SN 状态

private:
    struct Impl;                            // PIMPL：NimBLE 类型不外泄到头文件
    Impl* impl_ = nullptr;
};

} // namespace w96p
