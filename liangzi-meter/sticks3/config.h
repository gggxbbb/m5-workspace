// liangzi-meter: 设备配置结构 + NVS 持久化
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#define CFG_NS "lzcfg"

#define DEFAULT_NTP "ntp.aliyun.com"

// DeepSeek 官方峰谷规则（北京时间 UTC+8）：
// 周一至周五（不含中国法定节假日）9:00-12:00 / 14:00-18:00 为高峰，其余为低谷。
// 来源：https://api-docs.deepseek.com/zh-cn/quick_start/pricing/
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

// 国务院办公厅《2026 年部分节假日安排的通知》（国办发明电〔2025〕7号）。
// 来源：https://www.gov.cn/gongbao/2025/issue_12406/content_7048922.html
// DeepSeek 排除“中国法定节假日”，因此这里记录通知中的完整放假日期区间；
// 调休形成的周末工作日仍是低谷，因为 DeepSeek 明确只把周一至周五列为高峰候选日。
constexpr bool isChinaPublicHoliday(int year, int month, int day) {
  const int monthDay = month * 100 + day;
  return year == 2026 &&
         ((monthDay >= 101 && monthDay <= 103) ||      // 元旦
          (monthDay >= 215 && monthDay <= 223) ||      // 春节
          (monthDay >= 404 && monthDay <= 406) ||      // 清明节
          (monthDay >= 501 && monthDay <= 505) ||      // 劳动节
          (monthDay >= 619 && monthDay <= 621) ||      // 端午节
          (monthDay >= 925 && monthDay <= 927) ||      // 中秋节
          (monthDay >= 1001 && monthDay <= 1007));     // 国庆节
}

// wday 为 localtime_r 的 tm_wday：0=周日，6=周六。
constexpr bool isWeekend(int wday) { return wday == 0 || wday == 6; }

constexpr bool isOfficialPeakDay(int year, int month, int day, int wday) {
  return !isWeekend(wday) && !isChinaPublicHoliday(year, month, day);
}

static_assert(isChinaPublicHoliday(2026, 2, 23), "春节末日必须是法定节假日");
static_assert(!isChinaPublicHoliday(2026, 2, 24), "春节后首日不应是法定节假日");
static_assert(!isOfficialPeakDay(2026, 10, 1, 4), "国庆节全天必须是低谷");
static_assert(isOfficialPeakDay(2026, 9, 24, 4), "普通周四必须可进入高峰");

inline bool inPeakWindow(const Config &c, int year, int month, int day, int wday,
                         int minuteOfDay) {
  if (!isOfficialPeakDay(year, month, day, wday)) return false;
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

// 距下一次峰谷边界切换的秒数；返回 -1 表示不可计算（时间未同步）。
// 周末和法定节假日无边界。16 天窗口覆盖 2026 年最长的春节连续低谷区间。
inline long secondsToNextSwitch(const Config &c, time_t now) {
  if (now <= 0) return -1;
  long dayStart = now - ((now + 8 * 3600) % 86400);  // 北京当日 0 点（epoch）
  const int lookaheadDays = 16;
  long best = 86400L * lookaheadDays;
  for (int d = 0; d < lookaheadDays; d++) {
    long base = dayStart + d * 86400L;
    time_t localNoon = base + 12 * 3600L;  // 避免边界附近的日期换算歧义
    struct tm dayTm;
    localtime_r(&localNoon, &dayTm);
    int year = dayTm.tm_year + 1900;
    int month = dayTm.tm_mon + 1;
    if (!isOfficialPeakDay(year, month, dayTm.tm_mday, dayTm.tm_wday)) continue;
    int n = peakRangeCount(c);
    for (int i = 0; i < n; i++) {
      PeakRange r = peakRangeAt(c, i);
      long b1 = base + r.startMin * 60L;
      long b2 = base + r.endMin * 60L;
      if (b1 > now && b1 - now < best) best = b1 - now;
      if (b2 > now && b2 - now < best) best = b2 - now;
    }
  }
  if (best >= 86400L * lookaheadDays) return -1;
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
