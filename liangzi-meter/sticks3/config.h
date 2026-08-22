// liangzi-meter: 设备配置结构 + NVS 持久化
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#define CFG_NS "lzcfg"

#define DEFAULT_NTP "ntp.aliyun.com"

// 官方峰谷时段默认值（北京时间 UTC+8）：高峰 9:00-12:00 / 14:00-18:00（工作日）
// 2026-08-23 起：周末（周六、周日）全天不区分峰谷，统一按低谷价收取调用费用
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

// 屏幕旋转方向（M5GFX rotation 0..3，见 ADR-0005）：
// 0=竖屏正常  1=顺时针90°（横屏）  2=180°  3=逆时针90°（横屏）
#define DEFAULT_SCREEN_ROTATION 0

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

// 是否周末（wday 为 localtime_r 的 tm_wday：0=周日 6=周六）
inline bool isWeekend(int wday) { return wday == 0 || wday == 6; }

inline bool inPeakWindow(const Config &c, int wday, int minuteOfDay) {
  // 周末全天低谷（官方 2026-08-23 起：周六/周日不再区分峰谷）
  if (isWeekend(wday)) return false;
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

// 距下一次峰谷边界切换的秒数（0..7 天）；返回 -1 表示不可计算（时间未同步）。
// 周末全天低谷无边界，仅工作日存在边界；遍历未来 8 天（含今天）取最早未来边界。
inline long secondsToNextSwitch(const Config &c, time_t now, int wday) {
  if (now <= 0) return -1;
  long dayStart = now - ((now + 8 * 3600) % 86400);  // 北京当日 0 点（epoch）
  long best = 86400L * 8;
  for (int d = 0; d < 8; d++) {
    int wd = (wday + d) % 7;
    if (isWeekend(wd)) continue;  // 周末无峰谷边界
    long base = dayStart + d * 86400L;
    int n = peakRangeCount(c);
    for (int i = 0; i < n; i++) {
      PeakRange r = peakRangeAt(c, i);
      long b1 = base + r.startMin * 60L;
      long b2 = base + r.endMin * 60L;
      if (b1 > now && b1 - now < best) best = b1 - now;
      if (b2 > now && b2 - now < best) best = b2 - now;
    }
  }
  if (best >= 86400L * 8) best = 86400L;  // 兜底（一周内必有工作日，理论不可达）
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
  c.alertEnabled = prefs.getBool("alert", false);
  int rot = prefs.getInt("rot", DEFAULT_SCREEN_ROTATION);
  c.screenRotation = (rot >= 0 && rot <= 3) ? (uint8_t)rot : DEFAULT_SCREEN_ROTATION;
  c.autoRotate = prefs.getBool("autorot", false);
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
