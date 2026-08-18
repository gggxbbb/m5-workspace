// liangzi-meter: 设备配置结构 + NVS 持久化
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#define CFG_NS "lzcfg"

#define DEFAULT_NTP "ntp.aliyun.com"

// 官方峰谷时段默认值（2026-08-17 生效，UTC+8）：高峰 9:00-12:00 / 14:00-18:00
#define OFFICIAL_PEAK_MIN 9 * 60
#define OFFICIAL_PEAK_MAX 12 * 60
#define OFFICIAL_PEAK_MIN2 14 * 60
#define OFFICIAL_PEAK_MAX2 18 * 60

#define MAX_PEAK_RANGES 4

// 余额告警阈值（元）：余额低于该值显示红色
#define DEFAULT_BALANCE_WARN 10.0f

struct PeakRange {
  int startMin;  // 当日分钟数 0..1439
  int endMin;    // 结束分钟（若 endMin <= startMin 视为跨午夜）
};

struct Config {
  String ssid;
  String password;
  String ntp = DEFAULT_NTP;
  String apiKey;
  float balanceWarn = DEFAULT_BALANCE_WARN;
  PeakRange peak[MAX_PEAK_RANGES];
  int peakCount = 0;  // 0 = 使用官方默认

  bool hasWifi() const { return ssid.length() > 0; }
};

// 官方默认高峰区间（peakCount == 0 时使用）
inline int peakRangeCount(const Config &c) {
  return c.peakCount > 0 ? c.peakCount : 2;
}
inline PeakRange peakRangeAt(const Config &c, int i) {
  if (c.peakCount > 0) return c.peak[i];
  static const PeakRange official[2] = {
      {OFFICIAL_PEAK_MIN, OFFICIAL_PEAK_MAX},
      {OFFICIAL_PEAK_MIN2, OFFICIAL_PEAK_MAX2},
  };
  return official[i];
}

inline bool inPeakWindow(const Config &c, int minuteOfDay) {
  int n = peakRangeCount(c);
  for (int i = 0; i < n; i++) {
    PeakRange r = peakRangeAt(c, i);
    if (r.endMin <= r.startMin) {  // 跨午夜
      if (minuteOfDay >= r.startMin || minuteOfDay < r.endMin) return true;
    } else {
      if (minuteOfDay >= r.startMin && minuteOfDay < r.endMin) return true;
    }
  }
  return false;
}

// 距下一次峰谷边界切换的秒数（0..86400）；返回 -1 表示不可计算（时间未同步）
inline long secondsToNextSwitch(const Config &c, time_t now, int minuteOfDay) {
  if (now <= 0) return -1;
  time_t dayStart = now - ((now + 8 * 3600) % 86400);  // 北京当日 0 点（epoch）
  long best = 86400L;
  int n = peakRangeCount(c);
  for (int i = 0; i < n; i++) {
    PeakRange r = peakRangeAt(c, i);
    long b1 = dayStart + r.startMin * 60L;
    long b2 = dayStart + r.endMin * 60L;
    long d1 = b1 - now;
    long d2 = b2 - now;
    if (d1 <= 0) d1 += 86400;
    if (d2 <= 0) d2 += 86400;
    if (d1 < best) best = d1;
    if (d2 < best) best = d2;
  }
  return best;
}

// ---------- NVS 持久化 ----------

inline void cfgLoad(Config &c, Preferences &prefs) {
  prefs.begin(CFG_NS, true);
  c.ssid = prefs.getString("ssid", "");
  c.password = prefs.getString("pass", "");
  c.ntp = prefs.getString("ntp", DEFAULT_NTP);
  c.apiKey = prefs.getString("apikey", "");
  c.balanceWarn = prefs.getFloat("bwarn", DEFAULT_BALANCE_WARN);
  String ranges = prefs.getString("ranges", "");
  c.peakCount = 0;
  if (ranges.length() > 0) {
    // CSV: "start,end,start,end,..."
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
