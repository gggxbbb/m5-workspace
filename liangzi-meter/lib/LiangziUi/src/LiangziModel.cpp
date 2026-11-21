#include "LiangziModel.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace liangzi {
namespace {

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  int64_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}

int64_t daysFromCivil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = static_cast<int64_t>(y) - era * 400;
  const int64_t doy = (153LL * (m + (m > 2 ? -3 : 9)) + 2LL) / 5LL + d - 1;
  const int64_t doe = yoe * 365LL + yoe / 4LL - yoe / 100LL + doy;
  return era * 146097LL + doe - 719468LL;
}

bool parseNumberField(JsonVariantConst v, double& out) {
  if (v.is<double>() || v.is<float>() || v.is<int64_t>()) {
    out = v.as<double>();
    return true;
  }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (!s || !*s) return false;
    char* endp = nullptr;
    const double val = std::strtod(s, &endp);
    if (endp == s) return false;
    out = val;
    return true;
  }
  return false;
}

bool containsHeaderIgnoreCase(const std::string& headers, const char* needle) {
  std::string lower = headers;
  for (char& ch : lower) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  std::string target = needle;
  for (char& ch : target) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return lower.find(target) != std::string::npos;
}

bool decodeChunkedBody(const char* data, size_t len, std::string& decoded) {
  decoded.clear();
  size_t pos = 0;
  while (pos < len) {
    size_t lineEnd = pos;
    while (lineEnd < len && data[lineEnd] != '\n') ++lineEnd;
    if (lineEnd >= len) break;
    size_t tokEnd = lineEnd;
    if (tokEnd > pos && data[tokEnd - 1] == '\r') --tokEnd;
    std::string hexStr(data + pos, tokEnd - pos);
    const size_t semi = hexStr.find(';');
    if (semi != std::string::npos) hexStr.resize(semi);
    if (hexStr.empty()) {
      pos = lineEnd + 1;
      continue;
    }
    char* endp = nullptr;
    const unsigned long chunkSize = std::strtoul(hexStr.c_str(), &endp, 16);
    if (endp == hexStr.c_str()) return false;
    pos = lineEnd + 1;
    if (chunkSize == 0) return !decoded.empty();
    if (pos + chunkSize > len) {
      decoded.append(data + pos, len - pos);
      return !decoded.empty();
    }
    decoded.append(data + pos, chunkSize);
    pos += chunkSize;
    if (pos < len && data[pos] == '\r') ++pos;
    if (pos < len && data[pos] == '\n') ++pos;
  }
  return !decoded.empty();
}

}  // namespace

void epochToBeijingDateTime(int64_t epochSec, BeijingDateTime& out) {
  const int64_t bj = epochSec + 8LL * 3600LL;
  const int64_t days = floorDiv(bj, 86400LL);
  const int64_t rem = bj - days * 86400LL;

  out.hour = static_cast<int>(rem / 3600LL);
  out.minute = static_cast<int>((rem % 3600LL) / 60LL);
  out.second = static_cast<int>(rem % 60LL);
  out.minuteOfDay = out.hour * 60 + out.minute;
  out.wday = static_cast<int>(((days + 4LL) % 7LL + 7LL) % 7LL);

  const int64_t z = days + 719468LL;
  const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
  const int64_t doe = z - era * 146097LL;
  const int64_t yoe = (doe - doe / 1460LL + doe / 36524LL - doe / 146096LL) / 365LL;
  int64_t y = yoe + era * 400LL;
  const int64_t doy = doe - (365LL * yoe + yoe / 4LL - yoe / 100LL);
  const int64_t mp = (5LL * doy + 2LL) / 153LL;
  const int64_t d = doy - (153LL * mp + 2LL) / 5LL + 1LL;
  const int64_t m = mp + (mp < 10LL ? 3LL : -9LL);
  y += (m <= 2LL ? 1LL : 0LL);

  out.year = static_cast<int>(y);
  out.month = static_cast<int>(m);
  out.day = static_cast<int>(d);
}

int64_t beijingDateTimeToEpoch(int year, int month, int day, int hour, int minute, int second) {
  const int64_t days = daysFromCivil(year, month, day);
  return days * 86400LL + hour * 3600LL + minute * 60LL + second - 8LL * 3600LL;
}

bool inPeakWindow(const PeakSchedule& sched, int year, int month, int day, int wday,
                  int minuteOfDay) {
  if (!isOfficialPeakDay(year, month, day, wday)) return false;
  const int n = sched.rangeCount();
  for (int i = 0; i < n; ++i) {
    const PeakRange r = sched.rangeAt(i);
    if (r.endMin <= r.startMin) {
      if (minuteOfDay >= r.startMin || minuteOfDay < r.endMin) return true;
    } else {
      if (minuteOfDay >= r.startMin && minuteOfDay < r.endMin) return true;
    }
  }
  return false;
}

bool isPeakAtEpoch(const PeakSchedule& sched, int64_t epochSec) {
  BeijingDateTime dt;
  epochToBeijingDateTime(epochSec, dt);
  return inPeakWindow(sched, dt.year, dt.month, dt.day, dt.wday, dt.minuteOfDay);
}

PhaseKind classifyPhase(const PeakSchedule& sched, bool timeSynced, const BeijingDateTime& dt) {
  if (!timeSynced) return PhaseKind::kUnsynced;
  if (isChinaPublicHoliday(dt.year, dt.month, dt.day)) return PhaseKind::kOffPeakHoliday;
  if (isWeekend(dt.wday)) return PhaseKind::kOffPeakWeekend;
  return inPeakWindow(sched, dt.year, dt.month, dt.day, dt.wday, dt.minuteOfDay)
             ? PhaseKind::kPeak
             : PhaseKind::kOffPeakWorkday;
}

int64_t secondsToNextSwitch(const PeakSchedule& sched, int64_t nowEpoch) {
  if (nowEpoch <= 0) return -1;
  BeijingDateTime dt;
  epochToBeijingDateTime(nowEpoch, dt);
  const int64_t bjTodayZeroEpoch = beijingDateTimeToEpoch(dt.year, dt.month, dt.day, 0, 0, 0);
  const int lookaheadDays = 16;
  const int64_t maxSpan = 86400LL * lookaheadDays;
  int64_t best = maxSpan;

  auto consider = [&](int64_t cand) {
    if (cand <= nowEpoch) return;
    const int64_t delta = cand - nowEpoch;
    if (delta >= best) return;
    if (isPeakAtEpoch(sched, cand) != isPeakAtEpoch(sched, cand - 1)) {
      best = delta;
    }
  };

  for (int d = 0; d < lookaheadDays; ++d) {
    const int64_t base = bjTodayZeroEpoch + static_cast<int64_t>(d) * 86400LL;
    consider(base);
    const int n = sched.rangeCount();
    for (int i = 0; i < n; ++i) {
      const PeakRange r = sched.rangeAt(i);
      consider(base + static_cast<int64_t>(r.startMin) * 60LL);
      consider(base + static_cast<int64_t>(r.endMin) * 60LL);
    }
  }
  return (best < maxSpan) ? best : -1;
}

bool parseDeepSeekBalanceJson(const char* json, size_t len, BalanceParseResult& out) {
  const int savedStatus = out.httpStatus;
  out = BalanceParseResult{};
  out.httpStatus = savedStatus;
  if (!json || len == 0) return false;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len);
  if (err) return false;
  if (!doc["is_available"].is<bool>()) return false;

  const JsonArrayConst infos = doc["balance_infos"].as<JsonArrayConst>();
  if (infos.isNull() || infos.size() == 0) return false;

  JsonObjectConst selected = infos[0].as<JsonObjectConst>();
  for (JsonVariantConst item : infos) {
    const char* cur = item["currency"] | "";
    if (std::strcmp(cur, "CNY") == 0) {
      selected = item.as<JsonObjectConst>();
      break;
    }
  }
  if (selected.isNull()) return false;

  double total = 0.0;
  if (!parseNumberField(selected["total_balance"], total)) return false;

  out.isAvailable = doc["is_available"].as<bool>();
  const char* curStr = selected["currency"] | "";
  out.currency = (curStr && *curStr) ? curStr : "CNY";
  out.totalBalance = total;

  double granted = 0.0;
  double topped = 0.0;
  const bool hasGranted = parseNumberField(selected["granted_balance"], granted);
  const bool hasTopped = parseNumberField(selected["topped_up_balance"], topped);
  if (hasGranted && hasTopped) {
    out.grantedBalance = granted;
    out.toppedUpBalance = topped;
    out.hasSplit = true;
  }
  out.ok = true;
  return true;
}

bool parseDeepSeekBalanceHttp(const char* raw, size_t len, BalanceParseResult& out) {
  out = BalanceParseResult{};
  if (!raw || len == 0) return false;

  std::string input(raw, len);
  size_t split = input.find("\r\n\r\n");
  size_t sepLen = 4;
  if (split == std::string::npos) {
    split = input.find("\n\n");
    sepLen = 2;
  }
  if (split == std::string::npos) return false;

  const std::string headers = input.substr(0, split);
  const size_t sp = headers.find(' ');
  if (sp != std::string::npos) {
    out.httpStatus = std::atoi(headers.c_str() + sp + 1);
  }
  if (out.httpStatus != 200) return false;

  const char* bodyPtr = raw + split + sepLen;
  const size_t bodyLen = len - (split + sepLen);
  std::string decoded;
  if (containsHeaderIgnoreCase(headers, "transfer-encoding:") &&
      containsHeaderIgnoreCase(headers, "chunked")) {
    if (decodeChunkedBody(bodyPtr, bodyLen, decoded)) {
      return parseDeepSeekBalanceJson(decoded.data(), decoded.size(), out);
    }
  }
  return parseDeepSeekBalanceJson(bodyPtr, bodyLen, out);
}

RwrAction evaluateRwrBalanceUpdate(bool inPeakWatch, bool mslActive, double prevBalance,
                                   double currentBalance, double eps) {
  if (!inPeakWatch) {
    return mslActive ? RwrAction::kStopMissile : RwrAction::kNoChange;
  }
  if (prevBalance < 0.0) return RwrAction::kNoChange;
  if (!mslActive && currentBalance < prevBalance - eps) {
    return RwrAction::kStartMissile;
  }
  if (mslActive && currentBalance >= prevBalance) {
    return RwrAction::kStopMissile;
  }
  return RwrAction::kNoChange;
}

}  // namespace liangzi
