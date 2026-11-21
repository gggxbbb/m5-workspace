// liangzi-meter: 设备配置结构 + NVS 持久化
#pragma once

#include <Arduino.h>
#include <LiangziModel.h>
#include <Preferences.h>

#define CFG_NS "lzcfg"
#define DEFAULT_NTP "ntp.aliyun.com"
#define MAX_PEAK_RANGES liangzi::kMaxPeakRanges
#define DEFAULT_BALANCE_WARN 10.0f

// 屏幕旋转方向（M5GFX rotation 0..3，见 ADR-0005）：
// 0=竖屏正常  1=顺时针90°（横屏）  2=180°  3=逆时针90°（横屏）
#define DEFAULT_SCREEN_ROTATION 0

using PeakRange = liangzi::PeakRange;

struct Config {
  String ssid;
  String password;
  String ntp = DEFAULT_NTP;
  String apiKey;
  float balanceWarn = DEFAULT_BALANCE_WARN;
  PeakRange peak[MAX_PEAK_RANGES];
  int peakCount = 0;  // 0 = 使用官方默认
  bool alertEnabled = false;  // 提示音功能总开关（默认关，见 ADR-0004）
  uint8_t screenRotation = DEFAULT_SCREEN_ROTATION;  // 默认屏幕方向（开机方向）
  bool autoRotate = false;  // 重力感应旋屏开关（默认关，见 ADR-0005）

  bool hasWifi() const { return ssid.length() > 0; }
};

inline liangzi::PeakSchedule toPeakSchedule(const Config &c) {
  liangzi::PeakSchedule s;
  s.customCount = c.peakCount;
  for (int i = 0; i < c.peakCount && i < MAX_PEAK_RANGES; ++i) {
    s.custom[i] = c.peak[i];
  }
  return s;
}

inline int peakRangeCount(const Config &c) {
  return toPeakSchedule(c).rangeCount();
}

inline PeakRange peakRangeAt(const Config &c, int i) {
  return toPeakSchedule(c).rangeAt(i);
}

constexpr bool isChinaPublicHoliday(int year, int month, int day) {
  return liangzi::isChinaPublicHoliday(year, month, day);
}

constexpr bool isWeekend(int wday) {
  return liangzi::isWeekend(wday);
}

constexpr bool isOfficialPeakDay(int year, int month, int day, int wday) {
  return liangzi::isOfficialPeakDay(year, month, day, wday);
}

inline bool inPeakWindow(const Config &c, int year, int month, int day, int wday,
                         int minuteOfDay) {
  return liangzi::inPeakWindow(toPeakSchedule(c), year, month, day, wday, minuteOfDay);
}

inline long secondsToNextSwitch(const Config &c, time_t now) {
  return static_cast<long>(liangzi::secondsToNextSwitch(toPeakSchedule(c), static_cast<int64_t>(now)));
}

// ---------- NVS 持久化 ----------

inline void cfgLoad(Config &c, Preferences &prefs) {
  prefs.begin(CFG_NS, true);
  c.ssid = prefs.getString("ssid", "");
  c.password = prefs.getString("pass", "");
  c.ntp = prefs.getString("ntp", DEFAULT_NTP);
  c.apiKey = prefs.getString("apikey", "");
  c.balanceWarn = prefs.getFloat("bwarn", DEFAULT_BALANCE_WARN);
  c.alertEnabled = prefs.getBool("alert", false);
  int rot = prefs.getInt("rot", DEFAULT_SCREEN_ROTATION);
  c.screenRotation = (rot >= 0 && rot <= 3) ? (uint8_t)rot : DEFAULT_SCREEN_ROTATION;
  c.autoRotate = prefs.getBool("autorot", false);
  String ranges = prefs.getString("ranges", "");
  c.peakCount = 0;
  if (ranges.length() > 0) {
    int n = 0;
    int pos = 0;
    while (n < MAX_PEAK_RANGES) {
      int comma1 = ranges.indexOf(',', pos);
      int comma2 = ranges.indexOf(',', comma1 + 1);
      if (comma1 < 0 || comma2 < 0) break;
      c.peak[n].startMin = ranges.substring(pos, comma1).toInt();
      c.peak[n].endMin = ranges.substring(comma1 + 1, comma2).toInt();
      n++;
      pos = comma2 + 1;
    }
    c.peakCount = n;
  }
  prefs.end();
}

inline void cfgSave(const Config &c, Preferences &prefs) {
  prefs.begin(CFG_NS, false);
  prefs.putString("ssid", c.ssid);
  prefs.putString("pass", c.password);
  prefs.putString("ntp", c.ntp);
  prefs.putString("apikey", c.apiKey);
  prefs.putFloat("bwarn", c.balanceWarn);
  prefs.putBool("alert", c.alertEnabled);
  prefs.putInt("rot", c.screenRotation);
  prefs.putBool("autorot", c.autoRotate);
  if (c.peakCount > 0) {
    String ranges;
    for (int i = 0; i < c.peakCount; i++) {
      if (i > 0) ranges += ",";
      ranges += String(c.peak[i].startMin) + "," + String(c.peak[i].endMin);
    }
    prefs.putString("ranges", ranges);
  } else {
    prefs.remove("ranges");
  }
  prefs.end();
}
