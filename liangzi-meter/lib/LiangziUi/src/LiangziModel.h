#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace liangzi {

// DeepSeek official pricing peak hours (Beijing Time, UTC+8):
// Mon..Fri (excluding China public holidays): 09:00-12:00 & 14:00-18:00 (2x rate);
// all other hours, weekends, and China public holidays are off-peak (1x rate, 50% off).
// Verified against https://api-docs.deepseek.com/zh-cn/quick_start/pricing on 2026-11-21.
constexpr int kOfficialPeakStart1 = 9 * 60;
constexpr int kOfficialPeakEnd1 = 12 * 60;
constexpr int kOfficialPeakStart2 = 14 * 60;
constexpr int kOfficialPeakEnd2 = 18 * 60;
constexpr int kMaxPeakRanges = 4;
constexpr double kDefaultBalanceWarn = 10.0;
constexpr double kBalanceDecreaseEps = 0.01;

struct PeakRange {
  int startMin = 0;  // Minute of day [0..1439]
  int endMin = 0;    // End minute; if endMin <= startMin, treated as cross-midnight
};

struct PeakSchedule {
  PeakRange custom[kMaxPeakRanges]{};
  int customCount = 0;  // 0 = official default peak intervals

  int rangeCount() const { return customCount > 0 ? customCount : 2; }

  PeakRange rangeAt(int i) const {
    if (customCount > 0) return custom[i];
    static constexpr PeakRange kOfficial[2] = {
        {kOfficialPeakStart1, kOfficialPeakEnd1},
        {kOfficialPeakStart2, kOfficialPeakEnd2},
    };
    return kOfficial[i];
  }
};

// China State Council public holiday schedules:
// - 2026: 国办发明电〔2025〕7号
// - 2027: 国办发明电〔2026〕10号 (published 2026-11-05)
// Adjusted weekend workdays (调休周末) remain off-peak because DeepSeek only lists Mon-Fri
// as candidate peak days.
constexpr bool isChinaPublicHoliday(int year, int month, int day) {
  const int md = month * 100 + day;
  if (year == 2026) {
    return (md >= 101 && md <= 103) ||   // 元旦
           (md >= 215 && md <= 223) ||   // 春节
           (md >= 404 && md <= 406) ||   // 清明节
           (md >= 501 && md <= 505) ||   // 劳动节
           (md >= 619 && md <= 621) ||   // 端午节
           (md >= 925 && md <= 927) ||   // 中秋节
           (md >= 1001 && md <= 1007);   // 国庆节
  }
  if (year == 2027) {
    return (md >= 101 && md <= 103) ||   // 元旦: 1月1日-3日
           (md >= 205 && md <= 213) ||   // 春节: 2月5日-13日 (9天)
           (md >= 403 && md <= 405) ||   // 清明节: 4月3日-5日
           (md >= 501 && md <= 505) ||   // 劳动节: 5月1日-5日
           (md == 609) ||                // 端午节: 6月9日
           (md == 915) ||                // 中秋节: 9月15日
           (md >= 1001 && md <= 1007);   // 国庆节: 10月1日-7日
  }
  return false;
}

// wday: 0=Sunday, 1=Monday, ..., 6=Saturday
constexpr bool isWeekend(int wday) { return wday == 0 || wday == 6; }

constexpr bool isOfficialPeakDay(int year, int month, int day, int wday) {
  return !isWeekend(wday) && !isChinaPublicHoliday(year, month, day);
}

static_assert(isChinaPublicHoliday(2026, 2, 23), "2026 春节末日必须是法定节假日");
static_assert(!isChinaPublicHoliday(2026, 2, 24), "2026 春节后首日不应是法定节假日");
static_assert(!isOfficialPeakDay(2026, 10, 1, 4), "2026 国庆节全天必须是低谷");
static_assert(isOfficialPeakDay(2026, 9, 24, 4), "2026 普通周四必须可进入高峰");
static_assert(isChinaPublicHoliday(2027, 2, 13), "2027 春节末日必须是法定节假日");
static_assert(!isChinaPublicHoliday(2027, 2, 14), "2027-02-14 不在春节放假区间");
static_assert(!isOfficialPeakDay(2027, 2, 14, 0), "2027-02-14 周日调休仍属周末低谷");
static_assert(isChinaPublicHoliday(2027, 6, 9), "2027 端午节 6/9 必须是法定节假日");
static_assert(isChinaPublicHoliday(2027, 9, 15), "2027 中秋节 9/15 必须是法定节假日");
static_assert(!isOfficialPeakDay(2027, 10, 9, 6), "2027-10-09 周六调休仍属周末低谷");

enum class PhaseKind : uint8_t {
  kUnsynced = 0,
  kPeak = 1,
  kOffPeakWorkday = 2,
  kOffPeakWeekend = 3,
  kOffPeakHoliday = 4,
};

struct BeijingDateTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int wday = 4;  // 0=Sun..6=Sat
  int hour = 0;
  int minute = 0;
  int second = 0;
  int minuteOfDay = 0;
};

void epochToBeijingDateTime(int64_t epochSec, BeijingDateTime& out);
int64_t beijingDateTimeToEpoch(int year, int month, int day, int hour, int minute, int second);

bool inPeakWindow(const PeakSchedule& sched, int year, int month, int day, int wday,
                  int minuteOfDay);
bool isPeakAtEpoch(const PeakSchedule& sched, int64_t epochSec);
PhaseKind classifyPhase(const PeakSchedule& sched, bool timeSynced, const BeijingDateTime& dt);
int64_t secondsToNextSwitch(const PeakSchedule& sched, int64_t nowEpoch);

struct BalanceParseResult {
  bool ok = false;
  int httpStatus = 0;
  bool isAvailable = false;
  std::string currency;
  double totalBalance = 0.0;
  double grantedBalance = 0.0;
  double toppedUpBalance = 0.0;
  bool hasSplit = false;
};

bool parseDeepSeekBalanceJson(const char* json, size_t len, BalanceParseResult& out);
bool parseDeepSeekBalanceHttp(const char* raw, size_t len, BalanceParseResult& out);

struct BalanceSample {
  double total = 0.0;
  double granted = 0.0;
  double toppedUp = 0.0;
  uint32_t atMs = 0;
  bool inPeak = false;
  bool decreased = false;
};

class BalanceHistory {
 public:
  static constexpr size_t kCapacity = 40;

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  void push(const BalanceSample& s) {
    if (count_ < kCapacity) {
      samples_[(head_ + count_) % kCapacity] = s;
      ++count_;
    } else {
      samples_[head_] = s;
      head_ = (head_ + 1) % kCapacity;
    }
  }

  size_t size() const { return count_; }

  const BalanceSample& at(size_t i) const {
    return samples_[(head_ + i) % kCapacity];
  }

  double minTotal() const {
    if (count_ == 0) return 0.0;
    double best = at(0).total;
    for (size_t i = 1; i < count_; ++i) {
      if (at(i).total < best) best = at(i).total;
    }
    return best;
  }

  double maxTotal() const {
    if (count_ == 0) return 0.0;
    double best = at(0).total;
    for (size_t i = 1; i < count_; ++i) {
      if (at(i).total > best) best = at(i).total;
    }
    return best;
  }

  double sessionDelta() const {
    if (count_ < 2) return 0.0;
    return at(count_ - 1).total - at(0).total;
  }

  size_t burnCount() const {
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
      if (at(i).decreased) ++n;
    }
    return n;
  }

 private:
  std::array<BalanceSample, kCapacity> samples_{};
  size_t head_ = 0;
  size_t count_ = 0;
};

enum class RwrAction : uint8_t {
  kNoChange = 0,
  kStartMissile = 1,
  kStopMissile = 2,
};

// ADR-0004 deterministic RWR balance-decrease evaluation:
// triggered only when inPeakWatch && current < prev - eps;
// stops when leaving peakWatch or current >= prev (no decrease).
RwrAction evaluateRwrBalanceUpdate(bool inPeakWatch, bool mslActive, double prevBalance,
                                   double currentBalance, double eps = kBalanceDecreaseEps);

struct UiModel {
  bool configured = false;
  bool wifiConnected = false;
  int wifiRssi = -127;
  std::string ssid;
  std::string ip;
  std::string ntp = "ntp.aliyun.com";
  std::string fwVersion = "1.4.0";

  bool timeSynced = false;
  int year = 1970;
  int month = 1;
  int day = 1;
  int wday = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  PhaseKind phaseKind = PhaseKind::kUnsynced;
  int64_t countdownSec = -1;
  PeakSchedule schedule{};

  bool balanceValid = false;
  bool balanceExpired = false;
  bool balanceAvailable = true;
  bool queryBusy = false;
  int httpStatus = 0;
  std::string currency = "CNY";
  double totalBalance = 0.0;
  double grantedBalance = 0.0;
  double toppedUpBalance = 0.0;
  bool hasSplit = false;
  float balanceWarn = 10.0f;
  double lastDelta = 0.0;
  bool hasDelta = false;
  uint32_t lastFetchMs = 0;
  uint32_t refreshIntervalMs = 300000;

  bool alertEnabled = false;
  bool mslAlertActive = false;

  int batteryPct = -1;
  bool charging = false;
  uint8_t rotation = 0;
  bool autoRotate = false;
  float holdBProgress = 0.0f;  // 0..1 during long press KEY2

  const BalanceHistory* history = nullptr;
  uint32_t dataRevision = 0;
};

}  // namespace liangzi
