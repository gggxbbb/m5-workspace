#include "LiangziModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int gChecks = 0;

void check(bool cond, const char* msg, int line) {
  ++gChecks;
  if (!cond) {
    std::fprintf(stderr, "FAIL [line %d]: %s\n", line, msg);
    std::exit(1);
  }
}

#define CHECK(c) check((c), #c, __LINE__)
#define CHECK_NEAR(a, b, eps) check(std::fabs((a) - (b)) <= (eps), #a " ~= " #b, __LINE__)

void testDateTime() {
  // 2026-08-18 is Tuesday (wday == 2)
  const int64_t ep = liangzi::beijingDateTimeToEpoch(2026, 8, 18, 9, 19, 16);
  liangzi::BeijingDateTime dt;
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(dt.year == 2026);
  CHECK(dt.month == 8);
  CHECK(dt.day == 18);
  CHECK(dt.hour == 9);
  CHECK(dt.minute == 19);
  CHECK(dt.second == 16);
  CHECK(dt.wday == 2);
  CHECK(dt.minuteOfDay == 9 * 60 + 19);

  // Verify 2027-02-05 (Chinese New Year Eve) is Friday (wday == 5)
  const int64_t cny = liangzi::beijingDateTimeToEpoch(2027, 2, 5, 10, 0, 0);
  liangzi::epochToBeijingDateTime(cny, dt);
  CHECK(dt.year == 2027 && dt.month == 2 && dt.day == 5 && dt.wday == 5);
}

void testHolidaysAndSchedule() {
  liangzi::PeakSchedule def;
  liangzi::BeijingDateTime dt;

  // 2026 regular Thursday 15:30 -> peak
  int64_t ep = liangzi::beijingDateTimeToEpoch(2026, 9, 24, 15, 30, 0);
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kPeak);
  CHECK(liangzi::secondsToNextSwitch(def, ep) == (2 * 3600 + 30 * 60));

  // 2026 National Day Oct 1..7 -> holiday off-peak; countdown jumps to Oct 8 09:00
  ep = liangzi::beijingDateTimeToEpoch(2026, 10, 1, 0, 0, 0);
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kOffPeakHoliday);
  CHECK(liangzi::secondsToNextSwitch(def, ep) == (7LL * 86400LL + 9LL * 3600LL));

  // 2027 Spring Festival Feb 5..13 + Feb 14 Sunday adjusted workday -> 10 full off-peak days!
  ep = liangzi::beijingDateTimeToEpoch(2027, 2, 5, 0, 0, 0);
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kOffPeakHoliday);
  CHECK(liangzi::secondsToNextSwitch(def, ep) == (10LL * 86400LL + 9LL * 3600LL));

  // Adjusted weekend workdays in 2027 must stay Weekend Off-Peak (DeepSeek only charges peak Mon-Fri)
  const int adjWorkWeekends[][4] = {
      {2027, 1, 31, 0},
      {2027, 2, 14, 0},
      {2027, 5, 8, 6},
      {2027, 9, 26, 0},
      {2027, 10, 9, 6},
  };
  for (const auto& w : adjWorkWeekends) {
    const int64_t we = liangzi::beijingDateTimeToEpoch(w[0], w[1], w[2], 10, 30, 0);
    liangzi::epochToBeijingDateTime(we, dt);
    CHECK(dt.wday == w[3]);
    CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kOffPeakWeekend);
  }

  // 2027 standalone Dragon Boat (6/9 Wed) & Mid-Autumn (9/15 Wed)
  ep = liangzi::beijingDateTimeToEpoch(2027, 6, 9, 10, 0, 0);
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kOffPeakHoliday);

  ep = liangzi::beijingDateTimeToEpoch(2027, 9, 15, 16, 0, 0);
  liangzi::epochToBeijingDateTime(ep, dt);
  CHECK(liangzi::classifyPhase(def, true, dt) == liangzi::PhaseKind::kOffPeakHoliday);

  // Custom cross-midnight range: 22:00..06:00 on Friday -> stops cleanly at Saturday 00:00
  liangzi::PeakSchedule night;
  night.customCount = 1;
  night.custom[0] = {22 * 60, 6 * 60};
  const int64_t fri23 = liangzi::beijingDateTimeToEpoch(2026, 8, 21, 23, 0, 0);  // Friday
  liangzi::epochToBeijingDateTime(fri23, dt);
  CHECK(dt.wday == 5);
  CHECK(liangzi::classifyPhase(night, true, dt) == liangzi::PhaseKind::kPeak);
  CHECK(liangzi::secondsToNextSwitch(night, fri23) == 3600);
}

void testBalanceParsing() {
  // Standard HTTP 200 JSON with split
  {
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n\r\n"
        "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\","
        "\"total_balance\":\"110.00\",\"granted_balance\":\"10.00\","
        "\"topped_up_balance\":\"100.00\"}]}";
    liangzi::BalanceParseResult res;
    CHECK(liangzi::parseDeepSeekBalanceHttp(raw, std::strlen(raw), res));
    CHECK(res.ok && res.httpStatus == 200 && res.isAvailable);
    CHECK(res.currency == "CNY");
    CHECK(res.hasSplit);
    CHECK_NEAR(res.totalBalance, 110.0, 1e-6);
    CHECK_NEAR(res.grantedBalance, 10.0, 1e-6);
    CHECK_NEAR(res.toppedUpBalance, 100.0, 1e-6);
  }

  // HTTP 200 Transfer-Encoding: chunked + prefers CNY over USD
  {
    const char* body =
        "{\"is_available\":false,\"balance_infos\":["
        "{\"currency\":\"USD\",\"total_balance\":\"2.50\",\"granted_balance\":\"0.50\",\"topped_up_balance\":\"2.00\"},"
        "{\"currency\":\"CNY\",\"total_balance\":\"18.25\",\"granted_balance\":\"0.00\",\"topped_up_balance\":\"18.25\"}"
        "]}";
    char raw[512];
    std::snprintf(raw, sizeof(raw),
                  "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%s\r\n0\r\n\r\n",
                  std::strlen(body), body);
    liangzi::BalanceParseResult res;
    CHECK(liangzi::parseDeepSeekBalanceHttp(raw, std::strlen(raw), res));
    CHECK(res.ok && !res.isAvailable && res.currency == "CNY");
    CHECK_NEAR(res.totalBalance, 18.25, 1e-6);
    CHECK(res.hasSplit);
  }

  // Missing split fields -> ok=true, hasSplit=false
  {
    const char* js =
        "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"USD\",\"total_balance\":\"42.10\"}]}";
    liangzi::BalanceParseResult res;
    CHECK(liangzi::parseDeepSeekBalanceJson(js, std::strlen(js), res));
    CHECK(res.ok && !res.hasSplit && res.currency == "USD");
    CHECK_NEAR(res.totalBalance, 42.10, 1e-6);
  }

  // HTTP 504 Gateway Timeout preserves status 504
  {
    const char* raw = "HTTP/1.1 504 Gateway Timeout\r\nContent-Length: 0\r\n\r\n";
    liangzi::BalanceParseResult res;
    CHECK(!liangzi::parseDeepSeekBalanceHttp(raw, std::strlen(raw), res));
    CHECK(!res.ok && res.httpStatus == 504);
  }

  // Malformed JSON rejected
  {
    const char* raw = "HTTP/1.1 200 OK\r\n\r\n{bad json";
    liangzi::BalanceParseResult res;
    CHECK(!liangzi::parseDeepSeekBalanceHttp(raw, std::strlen(raw), res));
    CHECK(!res.ok && res.httpStatus == 200);
  }
}

void testHistoryAndRwr() {
  liangzi::BalanceHistory h;
  for (int i = 0; i < 50; ++i) {
    const double val = 120.0 - i * 0.5;
    h.push(liangzi::BalanceSample{val, 0.0, val, static_cast<uint32_t>(i * 15000), true, (i % 3) == 0});
  }
  CHECK(h.size() == 40);
  // Oldest kept sample is i=10 (115.0), newest is i=49 (95.5)
  CHECK_NEAR(h.at(0).total, 115.0, 1e-6);
  CHECK_NEAR(h.at(39).total, 95.5, 1e-6);
  CHECK_NEAR(h.minTotal(), 95.5, 1e-6);
  CHECK_NEAR(h.maxTotal(), 115.0, 1e-6);
  CHECK_NEAR(h.sessionDelta(), -19.5, 1e-6);
  CHECK(h.burnCount() > 0);

  // RWR boundaries (ADR-0004)
  CHECK(liangzi::evaluateRwrBalanceUpdate(true, false, -1.0, 100.0) == liangzi::RwrAction::kNoChange);
  CHECK(liangzi::evaluateRwrBalanceUpdate(true, false, 100.0, 99.991) == liangzi::RwrAction::kNoChange);
  CHECK(liangzi::evaluateRwrBalanceUpdate(true, false, 100.0, 99.98) == liangzi::RwrAction::kStartMissile);
  CHECK(liangzi::evaluateRwrBalanceUpdate(true, true, 99.98, 99.98) == liangzi::RwrAction::kStopMissile);
  CHECK(liangzi::evaluateRwrBalanceUpdate(true, true, 99.98, 99.97) == liangzi::RwrAction::kNoChange);
  CHECK(liangzi::evaluateRwrBalanceUpdate(false, true, 99.98, 99.50) == liangzi::RwrAction::kStopMissile);
}

}  // namespace

int main() {
  testDateTime();
  testHolidaysAndSchedule();
  testBalanceParsing();
  testHistoryAndRwr();
  std::printf("PASS: LiangziModel native-test (%d assertions)\n", gChecks);
  return 0;
}
