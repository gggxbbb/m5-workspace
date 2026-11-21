#include "LiangziUi.h"
#include "LiangziFonts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liangzi {
namespace {

constexpr uint32_t kBgBase = 0x090D12;
constexpr uint32_t kCardBg = 0x121A23;
constexpr uint32_t kWellBg = 0x0D141C;
constexpr uint32_t kCardBorder = 0x1F2E3D;
constexpr uint32_t kPillBg = 0x182330;
constexpr uint32_t kTextPrimary = 0xEEF4F8;
constexpr uint32_t kTextSec = 0xB8C7D6;
constexpr uint32_t kTextMuted = 0x7A8FA3;
constexpr uint32_t kPeakAmber = 0xFBBF24;   // ADR-0003: 梁文峰
constexpr uint32_t kOffEmerald = 0x4ADE80;  // ADR-0003: 梁文谷
constexpr uint32_t kWarnRed = 0xF87171;     // Only for low balance / RWR burn alert / error
constexpr uint32_t kCyan = 0x38BDF8;
constexpr uint32_t kViolet = 0xA78BFA;

inline uint16_t to565(uint32_t rgb) {
  return lgfx::color565(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                        static_cast<uint8_t>((rgb >> 8) & 0xFF),
                        static_cast<uint8_t>(rgb & 0xFF));
}

inline uint64_t fnvHashBytes(uint64_t h, const void* ptr, size_t len) {
  const auto* p = static_cast<const uint8_t*>(ptr);
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

template <typename T>
inline uint64_t fnvHashVal(uint64_t h, const T& v) {
  return fnvHashBytes(h, &v, sizeof(T));
}

const char* weekdayCn(int wday) {
  static constexpr const char* kDays[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  return (wday >= 0 && wday < 7) ? kDays[wday] : "未知";
}

const char* currencySymbol(const std::string& cur) {
  if (cur == "USD") return "$";
  return "¥";
}

struct PhaseVisual {
  const char* topPill;
  const char* subLabel;
  const char* dayTag;
  const char* heroText;
  uint32_t color;
  bool isPeak;
  bool synced;
};

PhaseVisual describePhase(const UiModel& m) {
  if (!m.configured) {
    return {"未配置", "等待配置", "未配置", "未配置", kTextMuted, false, false};
  }
  if (!m.timeSynced || m.phaseKind == PhaseKind::kUnsynced) {
    return {"未同步", "等待NTP", "待同步", "时间未同步", kTextMuted, false, false};
  }
  switch (m.phaseKind) {
    case PhaseKind::kPeak:
      return {"高峰 2x", "当前高峰 · 2x", "工作日", "梁文峰", kPeakAmber, true, true};
    case PhaseKind::kOffPeakHoliday:
      return {"假日 1x", "法定假日 · 1x", "节假日谷", "梁文谷", kOffEmerald, false, true};
    case PhaseKind::kOffPeakWeekend:
      return {"周末 1x", "周末全天 · 1x", "周末全天", "梁文谷", kOffEmerald, false, true};
    case PhaseKind::kOffPeakWorkday:
    default:
      return {"低谷 1x", "当前低谷 · 1x", "工作日", "梁文谷", kOffEmerald, false, true};
  }
}

void formatCountdown(int64_t secs, char* out, size_t outSize) {
  if (secs < 0) {
    std::snprintf(out, outSize, "--:--:--");
    return;
  }
  const int64_t h = secs / 3600;
  const int64_t m = (secs % 3600) / 60;
  const int64_t s = secs % 60;
  if (h >= 100) {
    std::snprintf(out, outSize, "%lldh%02lldm", static_cast<long long>(h), static_cast<long long>(m));
  } else {
    std::snprintf(out, outSize, "%02lld:%02lld:%02lld", static_cast<long long>(h),
                  static_cast<long long>(m), static_cast<long long>(s));
  }
}

void formatPeakRangesText(const PeakSchedule& sched, char* out, size_t outSize) {
  if (sched.customCount <= 0) {
    std::snprintf(out, outSize, "09-12 · 14-18");
    return;
  }
  std::string acc;
  for (int i = 0; i < sched.customCount && i < 2; ++i) {
    if (i > 0) acc += " · ";
    const PeakRange r = sched.custom[i];
    char seg[20];
    std::snprintf(seg, sizeof(seg), "%02d:%02d-%02d:%02d", r.startMin / 60, r.startMin % 60,
                  r.endMin / 60, r.endMin % 60);
    acc += seg;
  }
  if (sched.customCount > 2) acc += "…";
  std::snprintf(out, outSize, "%s", acc.c_str());
}

}  // namespace

bool LiangziUi::allocateCanvas(int width, int height) {
  width_ = width;
  height_ = height;
  canvas_.deleteSprite();
  canvas_.setPsram(usePsram_);
  canvas_.setColorDepth(16);
  if (!canvas_.createSprite(width_, height_)) {
    canvas_.setPsram(false);
    if (!canvas_.createSprite(width_, height_)) return false;
  }
  canvas_.setTextWrap(false);
  forceDirty_ = true;
  return true;
}

bool LiangziUi::begin(lgfx::LovyanGFX* target, int width, int height, bool usePsram) {
  target_ = target;
  usePsram_ = usePsram;
  if (!allocateCanvas(width, height)) return false;

  hero36Wrap_.set(liangzi::fonts::kFontHero36, liangzi::fonts::kFontHero36Size);
  hero30Wrap_.set(liangzi::fonts::kFontHero30, liangzi::fonts::kFontHero30Size);
  cn12Wrap_.set(liangzi::fonts::kFontCn12, liangzi::fonts::kFontCn12Size);
  meta10Wrap_.set(liangzi::fonts::kFontMeta10, liangzi::fonts::kFontMeta10Size);
  num16Wrap_.set(liangzi::fonts::kFontNum16, liangzi::fonts::kFontNum16Size);
  num22Wrap_.set(liangzi::fonts::kFontNum22, liangzi::fonts::kFontNum22Size);

  hero36Loaded_ = hero36Font_.loadFont(&hero36Wrap_);
  hero30Loaded_ = hero30Font_.loadFont(&hero30Wrap_);
  cn12Loaded_ = cn12Font_.loadFont(&cn12Wrap_);
  meta10Loaded_ = meta10Font_.loadFont(&meta10Wrap_);
  num16Loaded_ = num16Font_.loadFont(&num16Wrap_);
  num22Loaded_ = num22Font_.loadFont(&num22Wrap_);

  forceDirty_ = true;
  return true;
}

bool LiangziUi::onResize(int width, int height) {
  if (width == width_ && height == height_) {
    forceDirty_ = true;
    return true;
  }
  pageScroll_.snap(static_cast<float>(page_));
  return allocateCanvas(width, height);
}

void LiangziUi::setPage(int page, uint32_t nowMs) {
  const int clamped = ((page % kPageCount) + kPageCount) % kPageCount;
  page_ = clamped;
  pageScroll_.setTarget(static_cast<float>(clamped));
  pillPos_.setTarget(static_cast<float>(clamped));
  pillWidth_.snap(18.0f);
  pillWidth_.setTarget(13.0f);
  if (clamped == 1) {
    splitReveal_.start(0.08f, 1.0f, nowMs, 320);
  }
  forceDirty_ = true;
}

void LiangziUi::nextPage(uint32_t nowMs) {
  setPage((page_ + 1) % kPageCount, nowMs);
}

void LiangziUi::triggerPulse(uint32_t nowMs) {
  pulseUntilMs_ = nowMs + 650u;
  forceDirty_ = true;
}

void LiangziUi::showToast(const char* text, uint32_t nowMs, uint32_t durationMs) {
  toastText_ = text ? text : "";
  toastUntilMs_ = nowMs + durationMs;
  forceDirty_ = true;
}

const lgfx::IFont* LiangziUi::hero36Ptr() const {
  return hero36Loaded_ ? static_cast<const lgfx::IFont*>(&hero36Font_) : cn12Ptr();
}
const lgfx::IFont* LiangziUi::hero30Ptr() const {
  return hero30Loaded_ ? static_cast<const lgfx::IFont*>(&hero30Font_) : cn12Ptr();
}
const lgfx::IFont* LiangziUi::cn12Ptr() const {
  return cn12Loaded_ ? static_cast<const lgfx::IFont*>(&cn12Font_) : &lgfx::fonts::efontCN_12;
}
const lgfx::IFont* LiangziUi::meta10Ptr() const {
  return meta10Loaded_ ? static_cast<const lgfx::IFont*>(&meta10Font_) : &lgfx::fonts::Font0;
}
const lgfx::IFont* LiangziUi::num16Ptr() const {
  return num16Loaded_ ? static_cast<const lgfx::IFont*>(&num16Font_) : meta10Ptr();
}
const lgfx::IFont* LiangziUi::num22Ptr() const {
  return num22Loaded_ ? static_cast<const lgfx::IFont*>(&num22Font_) : num16Ptr();
}

void LiangziUi::clipToStage(int x, int y, int w, int h, int stageTop, int stageBottom) {
  const int l = std::max(0, x);
  const int t = std::max(stageTop, y);
  const int r = std::min(width_, x + w);
  const int b = std::min(stageBottom, y + h);
  canvas_.setClipRect(l, t, std::max(0, r - l), std::max(0, b - t));
}

void LiangziUi::restoreStageClip(int stageTop, int stageBottom) {
  canvas_.setClipRect(0, stageTop, width_, std::max(0, stageBottom - stageTop));
}

void LiangziUi::drawWifiIcon(int x, int y, bool connected, int rssi, uint32_t nowMs) {
  const int cx = x + 6;
  const int cy = y + 9;
  const int bars = connected ? (rssi >= -62 ? 3 : (rssi >= -76 ? 2 : 1)) : 0;
  for (int i = 0; i < 3; ++i) {
    const uint32_t c = (i < bars) ? kCyan : 0x253344;
    const int r = 3 + i * 2;
    canvas_.drawArc(cx, cy, r, r, 225, 315, to565(c));
  }
  const uint32_t dotCol = connected ? kCyan : kWarnRed;
  const float breath = connected ? 1.0f : (0.4f + 0.5f * std::sin(nowMs * 0.005f));
  canvas_.fillCircle(cx, cy, 1, to565(mixRgb(kBgBase, dotCol, breath)));
  if (!connected) {
    canvas_.drawLine(x + 1, y + 8, x + 11, y + 1, to565(kWarnRed));
  }
}

int LiangziUi::drawBatteryPill(int xRight, int y, int pct, bool charging) {
  char pctStr[10];
  if (pct >= 0) std::snprintf(pctStr, sizeof(pctStr), "%d%%", std::min(100, pct));
  else std::snprintf(pctStr, sizeof(pctStr), "--");

  canvas_.setFont(meta10Ptr());
  const int tw = canvas_.textWidth(pctStr);
  const int iconW = charging ? 16 : 12;
  const int pillW = iconW + tw + 6;
  const int px = xRight - pillW;

  const uint32_t bg = charging ? 0x0E241C : kPillBg;
  const uint32_t br = charging ? mixRgb(kOffEmerald, kCardBorder, 0.45f) : kCardBorder;
  canvas_.fillSmoothRoundRect(px, y, pillW, 12, 3, to565(bg));
  canvas_.drawRoundRect(px, y, pillW, 12, 3, to565(br));

  // Battery shell
  const int bx = px + 3;
  const int by = y + 3;
  const uint32_t shellCol = charging ? kOffEmerald : kTextMuted;
  canvas_.drawRoundRect(bx, by, 9, 6, 1, to565(shellCol));
  canvas_.fillRect(bx + 9, by + 1, 1, 4, to565(shellCol));
  if (pct >= 0) {
    const int clamped = std::min(100, std::max(0, pct));
    const int fw = std::max(1, (7 * clamped) / 100);
    const uint32_t fillCol = charging ? kOffEmerald : (clamped <= 20 ? kWarnRed : kTextSec);
    canvas_.fillRect(bx + 1, by + 1, fw, 4, to565(fillCol));
  }

  int tx = bx + 11;
  if (charging) {
    // Crisp 4px vector lightning bolt beside battery
    canvas_.fillTriangle(tx + 3, y + 2, tx, y + 6, tx + 3, y + 6, to565(kOffEmerald));
    canvas_.fillTriangle(tx + 1, y + 5, tx + 4, y + 5, tx + 1, y + 10, to565(kOffEmerald));
    tx += 4;
  }

  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(charging ? kOffEmerald : kTextPrimary));
  canvas_.drawString(pctStr, tx, y + 1);
  return px;
}

void LiangziUi::drawTopBar(const UiModel& model, uint32_t nowMs) {
  const PhaseVisual pv = describePhase(model);
  const bool mslFlash = model.mslAlertActive && (((nowMs / 320u) & 1u) == 0u);
  const uint32_t pillCol = mslFlash ? kWarnRed : pv.color;
  const char* pillLabel = mslFlash ? "RWR扣费" : pv.topPill;

  canvas_.setFont(cn12Ptr());
  const int tw = canvas_.textWidth(pillLabel);
  const int pillW = std::max(48, tw + 14);
  const int px = isPortrait() ? 5 : 6;
  const int py = 2;

  canvas_.fillSmoothRoundRect(px, py, pillW, 13, 6, to565(kPillBg));
  canvas_.drawRoundRect(px, py, pillW, 13, 6,
                        to565(model.mslAlertActive ? mixRgb(kWarnRed, kCardBorder, 0.35f) : kCardBorder));
  canvas_.fillSmoothCircle(px + 6, py + 6, 2, to565(pillCol));
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(mslFlash ? kWarnRed : kTextSec));
  canvas_.drawString(pillLabel, px + 11, py);

  // Right telemetry: Wi-Fi + Battery capsule (dynamic spacing prevents collisions)
  const int battX = drawBatteryPill(width_ - (isPortrait() ? 5 : 6), 2, model.batteryPct, model.charging);
  const int wifiX = battX - 14;
  drawWifiIcon(wifiX, 2, model.configured && model.wifiConnected, model.wifiRssi, nowMs);

  // Landscape center live Beijing clock + date
  if (!isPortrait()) {
    char clk[32];
    if (model.configured && model.timeSynced) {
      std::snprintf(clk, sizeof(clk), "%02d:%02d:%02d %02d-%02d %s", model.hour, model.minute,
                    model.second, model.month, model.day, weekdayCn(model.wday));
    } else if (!model.configured) {
      std::snprintf(clk, sizeof(clk), "--:--:--  待配置");
    } else {
      std::snprintf(clk, sizeof(clk), "--:--:-- 等待同步");
    }
    canvas_.setFont(cn12Ptr());
    const int cw = canvas_.textWidth(clk);
    const int maxClkX = wifiX - 3 - cw;
    const int cx = std::max(px + pillW + 3, std::min(maxClkX, (width_ - cw) / 2));
    canvas_.setTextColor(to565((model.configured && model.timeSynced) ? kTextPrimary : kTextMuted));
    canvas_.drawString(clk, cx, 2);
  }

  // Top 2px scanning cyan shimmer while Core 0 query is running or KEY1 pulse active
  const bool pulsing = (nowMs < pulseUntilMs_);
  if (model.queryBusy || pulsing) {
    const int sx = model.queryBusy
                       ? (-42 + static_cast<int>(((nowMs % 750u) * (width_ + 42)) / 750u))
                       : (width_ - static_cast<int>(((pulseUntilMs_ - nowMs) * (width_ + 42)) / 650u));
    for (int i = 0; i < 42; ++i) {
      const int x = sx + i;
      if (x < 0 || x >= width_) continue;
      const float d = 1.0f - std::fabs(static_cast<float>(i - 21) / 21.0f);
      canvas_.fillRect(x, 0, 1, 2, to565(mixRgb(kBgBase, kCyan, d * d)));
    }
  }
}

void LiangziUi::drawTopAccentCard(int x, int y, int w, int h, uint32_t accent, uint32_t border,
                                  bool glow) {
  canvas_.fillSmoothRoundRect(x, y, w, h, 7, to565(kCardBg));
  if (accent != kCardBorder) {
    // 2px accent strip nested cleanly inside top rounded edge
    canvas_.fillRect(x + 6, y + 1, std::max(0, w - 12), 2, to565(accent));
    canvas_.fillRect(x + 4, y + 3, std::max(0, w - 8), 2,
                     to565(mixRgb(accent, kCardBg, glow ? 0.78f : 0.90f)));
  }
  canvas_.drawRoundRect(x, y, w, h, 7, to565(border));
}

void LiangziUi::drawTimeline24h(int x, int y, int w, int h, const UiModel& model, bool showTicks) {
  const int r = std::max(2, h / 2);
  canvas_.fillSmoothRoundRect(x, y, w, h, r, to565(kWellBg));
  const bool holidayOrWknd = model.phaseKind == PhaseKind::kOffPeakHoliday ||
                             model.phaseKind == PhaseKind::kOffPeakWeekend;
  if (model.configured && model.timeSynced) {
    const uint32_t trackCol = holidayOrWknd ? mixRgb(kOffEmerald, kWellBg, 0.72f)
                                            : mixRgb(kOffEmerald, kWellBg, 0.84f);
    canvas_.fillRect(x + 2, y + 1, std::max(0, w - 4), std::max(1, h - 2), to565(trackCol));

    const int n = model.schedule.rangeCount();
    const uint32_t bandCol = holidayOrWknd ? mixRgb(kOffEmerald, kWellBg, 0.56f) : kPeakAmber;
    for (int i = 0; i < n; ++i) {
      const PeakRange rg = model.schedule.rangeAt(i);
      auto drawSeg = [&](int sMin, int eMin) {
        if (eMin <= sMin) return;
        const int sx = x + 1 + (sMin * (w - 2)) / 1440;
        const int ex = x + 1 + (eMin * (w - 2)) / 1440;
        const int sw = std::max(2, ex - sx);
        canvas_.fillRect(sx, y + 1, sw, std::max(1, h - 2), to565(bandCol));
      };
      if (rg.endMin <= rg.startMin) {
        drawSeg(rg.startMin, 1440);
        drawSeg(0, rg.endMin);
      } else {
        drawSeg(rg.startMin, rg.endMin);
      }
    }

    const int mod = std::min(1439, std::max(0, model.hour * 60 + model.minute));
    const int cx = std::min(x + w - 2, std::max(x + 2, x + (mod * w) / 1440));
    canvas_.fillRect(cx, y - 1, 2, h + 2, to565(kTextPrimary));
    const uint32_t dotCol = (model.phaseKind == PhaseKind::kPeak) ? kPeakAmber : kOffEmerald;
    canvas_.fillCircle(cx, y + h / 2, 2, to565(dotCol));
  }

  if (showTicks) {
    canvas_.setFont(meta10Ptr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.setTextDatum(lgfx::top_left);
    static constexpr const char* kTicks[5] = {"00", "06", "12", "18", "24"};
    for (int i = 0; i < 5; ++i) {
      const int tw = canvas_.textWidth(kTicks[i]);
      int tx = x + (i * w) / 4 - tw / 2;
      if (i == 0) tx = x;
      else if (i == 4) tx = x + w - tw;
      canvas_.drawString(kTicks[i], tx, y + h + 2);
    }
  }
}

void LiangziUi::drawSplitBar(int x, int y, int w, int h, const UiModel& model, float reveal) {
  canvas_.fillSmoothRoundRect(x, y, w, h, h / 2, to565(kWellBg));
  if (!model.balanceValid || !model.hasSplit) return;
  const double sum = model.grantedBalance + model.toppedUpBalance;
  if (sum <= 0.0001) return;

  const int innerW = static_cast<int>(std::lround(static_cast<float>(w - 2) * reveal));
  if (innerW <= 0) return;

  const double ratio = std::min(1.0, std::max(0.0, model.grantedBalance / sum));
  int gw = static_cast<int>(std::lround(innerW * ratio));
  if (model.grantedBalance > 0.005 && gw == 0) gw = 2;
  int tw = std::max(0, innerW - gw);
  if (gw > 0) {
    canvas_.fillRect(x + 1, y + 1, gw, std::max(1, h - 2), to565(kViolet));
  }
  if (tw > 0) {
    const int gx = x + 1 + gw + (gw > 0 ? 1 : 0);
    const int avail = std::max(0, (x + 1 + innerW) - gx);
    if (avail > 0) {
      canvas_.fillRect(gx, y + 1, std::min(tw, avail), std::max(1, h - 2), to565(kCyan));
    }
  }
}

void LiangziUi::drawSparkline(int x, int y, int w, int h, const UiModel& model, bool showMinMax) {
  canvas_.fillSmoothRoundRect(x, y, w, h, 5, to565(kWellBg));
  canvas_.drawRoundRect(x, y, w, h, 5, to565(0x17222E));

  const size_t n = model.history ? model.history->size() : 0;
  if (n < 2) {
    canvas_.setFont(cn12Ptr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.setTextDatum(lgfx::top_left);
    const char* msg = model.configured ? "等待第2次采样" : "未配置";
    canvas_.drawString(msg, x + (w - canvas_.textWidth(msg)) / 2, y + h / 2 - 6);
    return;
  }

  const double minV = model.history->minTotal();
  const double maxV = model.history->maxTotal();
  const int padX = 6;
  const int padY = 8;
  const int innerW = w - padX * 2;
  const int innerH = h - padY * 2;

  if (maxV - minV <= 0.005) {
    const int midY = y + h / 2 + 4;
    canvas_.drawFastHLine(x + padX, midY, innerW, to565(kOffEmerald));
    canvas_.fillCircle(x + w - padX, midY, 2, to565(kOffEmerald));
    canvas_.setFont(cn12Ptr());
    canvas_.setTextColor(to565(kOffEmerald));
    canvas_.setTextDatum(lgfx::top_left);
    const char* msg = "余额持平";
    canvas_.drawString(msg, x + (w - canvas_.textWidth(msg)) / 2, midY - 16);
    return;
  }

  const bool lowBal = model.balanceValid && (model.totalBalance < static_cast<double>(model.balanceWarn));
  const uint32_t lineRgb = (lowBal || model.mslAlertActive) ? kWarnRed : kCyan;

  float prevX = 0.0f;
  float prevY = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const auto& s = model.history->at(i);
    const float px = static_cast<float>(x + padX) +
                     (static_cast<float>(i) / static_cast<float>(n - 1)) * innerW;
    const float norm = static_cast<float>((s.total - minV) / (maxV - minV));
    const float py = static_cast<float>(y + padY + innerH) -
                     std::min(1.0f, std::max(0.0f, norm)) * static_cast<float>(innerH);
    if (i > 0) {
      const int x0 = static_cast<int>(std::floor(prevX));
      const int x1 = static_cast<int>(std::ceil(px));
      for (int sx = x0; sx <= x1; ++sx) {
        const float u = (x1 == x0) ? 0.0f : static_cast<float>(sx - x0) / static_cast<float>(x1 - x0);
        const int topY = static_cast<int>(std::lround(prevY + (py - prevY) * u));
        for (int sy = topY; sy <= y + h - 3; ++sy) {
          const float f = static_cast<float>(sy - topY) / static_cast<float>(std::max(1, (y + h - 3) - topY));
          canvas_.drawPixel(sx, sy, to565(mixRgb(lineRgb, kWellBg, 0.72f + 0.28f * f)));
        }
      }
      canvas_.drawWideLine(static_cast<int32_t>(std::lround(prevX)),
                           static_cast<int32_t>(std::lround(prevY)),
                           static_cast<int32_t>(std::lround(px)),
                           static_cast<int32_t>(std::lround(py)),
                           1.6f, to565(lineRgb));
    }
    prevX = px;
    prevY = py;
  }

  // Overlay burn dots & terminal marker after line segments
  for (size_t i = 0; i < n; ++i) {
    const auto& s = model.history->at(i);
    const int px = static_cast<int>(std::lround(
        static_cast<float>(x + padX) + (static_cast<float>(i) / static_cast<float>(n - 1)) * innerW));
    const float norm = static_cast<float>((s.total - minV) / (maxV - minV));
    const int py = static_cast<int>(std::lround(
        static_cast<float>(y + padY + innerH) - std::min(1.0f, std::max(0.0f, norm)) * innerH));
    if (s.decreased) {
      canvas_.fillCircle(px, py, 2, to565(kWarnRed));
    }
    if (i == n - 1) {
      canvas_.fillCircle(px, py, 2, to565(kTextPrimary));
    }
  }

  if (showMinMax) {
    char b[24];
    canvas_.setFont(meta10Ptr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.setTextDatum(lgfx::top_left);
    std::snprintf(b, sizeof(b), "%.2f", maxV);
    canvas_.drawString(b, x + 4, y + 2);
    std::snprintf(b, sizeof(b), "%.2f", minV);
    canvas_.drawString(b, x + 4, y + h - 11);
  }
}

void LiangziUi::drawSetupCard(int x, int y, int w, int h, uint32_t nowMs) {
  drawTopAccentCard(x, y, w, h, kCyan, kCardBorder, false);
  const int cx = x + w / 2;
  const int cy = y + h / 2;
  const float breath = 0.5f + 0.5f * std::sin(nowMs * 0.004f);
  canvas_.drawCircle(cx, cy - 22, 13 + static_cast<int>(breath * 2.0f),
                     to565(mixRgb(kCardBg, kCyan, 0.45f + 0.45f * breath)));
  canvas_.setFont(meta10Ptr());
  canvas_.setTextColor(to565(kCyan));
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.drawString("USB", cx - canvas_.textWidth("USB") / 2, cy - 27);

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextPrimary));
  const char* t1 = "等待上位机配置";
  canvas_.drawString(t1, cx - canvas_.textWidth(t1) / 2, cy - 2);

  canvas_.setTextColor(to565(kTextMuted));
  const char* t2 = isPortrait() ? "连接USB打开上位机" : "请连接 USB 打开桌面端配置";
  canvas_.drawString(t2, cx - canvas_.textWidth(t2) / 2, cy + 14);

  const char* t3 = "按KEY2查看系统诊断";
  canvas_.drawString(t3, cx - canvas_.textWidth(t3) / 2, cy + 29);
}

void LiangziUi::drawTruncatedRight(const char* text, int leftBound, int rightX, int y,
                                     uint32_t color, const lgfx::IFont* font) {
  canvas_.setFont(font);
  std::string s = text ? text : "";
  const int maxW = std::max(8, rightX - leftBound);
  if (canvas_.textWidth(s.c_str()) > maxW) {
    // UTF-8 safe trimming by codepoint lead bytes
    while (!s.empty()) {
      size_t cut = s.size() - 1;
      while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
      s.resize(cut);
      std::string cand = s + "...";
      if (canvas_.textWidth(cand.c_str()) <= maxW) {
        s = cand;
        break;
      }
    }
  }
  canvas_.setTextColor(to565(color));
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.drawString(s.c_str(), rightX - canvas_.textWidth(s.c_str()), y);
}

// ---------- Portrait Pages (135 x 240) ----------

void LiangziUi::drawPortraitPage0(int ox, const UiModel& model, uint32_t nowMs) {
  if (!model.configured) {
    drawSetupCard(ox + 5, 37, 125, 180, nowMs);
    return;
  }

  const PhaseVisual pv = describePhase(model);
  const bool mslFlash = model.mslAlertActive && (((nowMs / 320u) & 1u) == 0u);

  // Upper Peak Hero Card: x=5, y=37, w=125, h=97
  drawTopAccentCard(ox + 5, 37, 125, 97, pv.color, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.drawString(pv.subLabel, ox + 11, 42);

  if (model.alertEnabled) {
    const char* rTag = model.mslAlertActive ? "MSL!" : (pv.isPeak ? "15s" : "RWR");
    const uint32_t rCol = model.mslAlertActive ? kWarnRed : pv.color;
    canvas_.setFont(meta10Ptr());
    const int rw = canvas_.textWidth(rTag) + 6;
    canvas_.fillSmoothRoundRect(ox + 124 - rw, 42, rw, 12, 3, to565(mixRgb(rCol, kCardBg, 0.82f)));
    canvas_.setTextColor(to565(rCol));
    canvas_.drawString(rTag, ox + 124 - rw + 3, 43);
  }

  // Giant Peak/Offpeak Title (梁文峰 / 梁文谷) — strictly ADR-0003 Amber / Emerald
  if (pv.synced) {
    canvas_.setFont(hero36Ptr());
    const int hw = canvas_.textWidth(pv.heroText);
    const int hx = ox + 67 - hw / 2;
    canvas_.setTextColor(to565(0x05070A));
    canvas_.drawString(pv.heroText, hx + 1, 58);
    canvas_.setTextColor(to565(pv.color));
    canvas_.drawString(pv.heroText, hx, 57);
  } else {
    canvas_.setFont(cn12Ptr());
    const int hw = canvas_.textWidth(pv.heroText);
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(pv.heroText, ox + 67 - hw / 2, 70);
  }

  // Countdown row
  char cbuf[20];
  formatCountdown(pv.synced ? model.countdownSec : -1, cbuf, sizeof(cbuf));
  canvas_.setFont(cn12Ptr());
  const int lblW = canvas_.textWidth("剩余");
  canvas_.setFont(num16Ptr());
  const int numW = canvas_.textWidth(cbuf);
  const int totW = lblW + 4 + numW;
  const int sx = ox + 67 - totW / 2;
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("剩余", sx, 97);
  canvas_.setFont(num16Ptr());
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(cbuf, sx + lblW + 4, 96);

  // 24h timeline bar inside Peak Card
  drawTimeline24h(ox + 12, 117, 111, 6, model, false);
  canvas_.setFont(meta10Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("00", ox + 12, 124);
  canvas_.drawString("12", ox + 67 - canvas_.textWidth("12") / 2, 124);
  canvas_.drawString("24", ox + 123 - canvas_.textWidth("24"), 124);

  // Lower Balance Card: x=5, y=138, w=125, h=79
  const bool lowBal = model.balanceValid && (model.totalBalance < static_cast<double>(model.balanceWarn));
  const bool redState = lowBal || (model.balanceValid && !model.balanceAvailable);
  const uint32_t balBorder = mslFlash ? kWarnRed : (redState ? mixRgb(kWarnRed, kCardBorder, 0.25f) : kCardBorder);
  drawTopAccentCard(ox + 5, 138, 125, 79, redState ? kWarnRed : kCyan, balBorder, mslFlash);

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("可用余额", ox + 11, 143);

  // Badge: 旧 / delta
  char badge[16] = "";
  uint32_t badgeCol = kTextMuted;
  if (model.balanceExpired) {
    std::snprintf(badge, sizeof(badge), "旧");
    badgeCol = kPeakAmber;
  } else if (model.balanceValid && model.hasDelta && std::fabs(model.lastDelta) > 0.005) {
    std::snprintf(badge, sizeof(badge), "%+.2f", model.lastDelta);
    badgeCol = (model.lastDelta < 0.0) ? kWarnRed : kOffEmerald;
  }
  if (badge[0]) {
    const bool isCn = model.balanceExpired;
    canvas_.setFont(isCn ? cn12Ptr() : meta10Ptr());
    const int bw = canvas_.textWidth(badge) + 8;
    const int bx = ox + 124 - bw;
    canvas_.fillSmoothRoundRect(bx, 143, bw, 13, 3, to565(mixRgb(badgeCol, kCardBg, 0.80f)));
    canvas_.setTextColor(to565(badgeCol));
    canvas_.drawString(badge, bx + 4, isCn ? 143 : 144);
  }

  // Hero balance numeral
  char bbuf[28];
  if (!model.balanceValid) {
    std::snprintf(bbuf, sizeof(bbuf), "--");
  } else {
    std::snprintf(bbuf, sizeof(bbuf), "%s%.2f", currencySymbol(model.currency), model.totalBalance);
  }
  canvas_.setFont(num22Ptr());
  if (canvas_.textWidth(bbuf) > 113) canvas_.setFont(num16Ptr());
  const int bW = canvas_.textWidth(bbuf);
  const uint32_t bCol = !model.balanceValid ? kTextMuted : (lowBal ? kWarnRed : kTextPrimary);
  canvas_.setTextColor(to565(bCol));
  canvas_.drawString(bbuf, ox + 67 - bW / 2, 160);

  // Split bar & sub-labels
  drawSplitBar(ox + 12, 187, 111, 5, model, splitReveal_.value());
  canvas_.setFont(cn12Ptr());
  if (model.balanceValid && model.hasSplit) {
    char gl[24], tr[24];
    std::snprintf(gl, sizeof(gl), "赠%.2f", model.grantedBalance);
    std::snprintf(tr, sizeof(tr), "充%.2f", model.toppedUpBalance);
    if (canvas_.textWidth(gl) + canvas_.textWidth(tr) > 106) {
      std::snprintf(gl, sizeof(gl), "赠%.1f", model.grantedBalance);
      std::snprintf(tr, sizeof(tr), "充%.1f", model.toppedUpBalance);
    }
    canvas_.setTextColor(to565(kViolet));
    canvas_.drawString(gl, ox + 12, 198);
    canvas_.setTextColor(to565(kCyan));
    canvas_.drawString(tr, ox + 123 - canvas_.textWidth(tr), 198);
  } else {
    char thr[28];
    std::snprintf(thr, sizeof(thr), "告警阈值 %s%.0f", currencySymbol(model.currency), model.balanceWarn);
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(thr, ox + 67 - canvas_.textWidth(thr) / 2, 198);
  }
}

void LiangziUi::drawPortraitPage1(int ox, const UiModel& model) {
  // Card A: 资金构成 (x=5, y=37, w=125, h=76)
  drawTopAccentCard(ox + 5, 37, 125, 76, kViolet, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextSec));
  canvas_.drawString("资金构成", ox + 11, 43);

  char statusTag[24];
  if (!model.configured) std::snprintf(statusTag, sizeof(statusTag), "--");
  else std::snprintf(statusTag, sizeof(statusTag), "%s·%s", model.currency.c_str(),
                     model.balanceAvailable ? "可用" : "停用");
  canvas_.setTextColor(to565(!model.configured ? kTextMuted : (model.balanceAvailable ? kOffEmerald : kWarnRed)));
  canvas_.drawString(statusTag, ox + 123 - canvas_.textWidth(statusTag), 43);

  char v1[24], v2[24];
  if (model.balanceValid && model.hasSplit) {
    std::snprintf(v1, sizeof(v1), "%s%.2f", currencySymbol(model.currency), model.grantedBalance);
    std::snprintf(v2, sizeof(v2), "%s%.2f", currencySymbol(model.currency), model.toppedUpBalance);
  } else {
    std::snprintf(v1, sizeof(v1), "--");
    std::snprintf(v2, sizeof(v2), "--");
  }
  canvas_.fillSmoothCircle(ox + 13, 64, 2, to565(kViolet));
  canvas_.setTextColor(to565(kViolet));
  canvas_.drawString("赠金(优先)", ox + 18, 58);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(v1, ox + 123 - canvas_.textWidth(v1), 58);

  canvas_.fillSmoothCircle(ox + 13, 79, 2, to565(kCyan));
  canvas_.setTextColor(to565(kCyan));
  canvas_.drawString("充值余额", ox + 18, 73);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(v2, ox + 123 - canvas_.textWidth(v2), 73);

  drawSplitBar(ox + 12, 89, 111, 6, model, splitReveal_.value());

  char thStr[24];
  std::snprintf(thStr, sizeof(thStr), "阈值 %s%.0f", currencySymbol(model.currency), model.balanceWarn);
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(thStr, ox + 12, 98);
  const bool lowBal = model.balanceValid && (model.totalBalance < static_cast<double>(model.balanceWarn));
  const char* lvl = !model.balanceValid ? "--" : (lowBal ? "低于阈值" : "水位正常");
  canvas_.setTextColor(to565(!model.balanceValid ? kTextMuted : (lowBal ? kWarnRed : kOffEmerald)));
  canvas_.drawString(lvl, ox + 123 - canvas_.textWidth(lvl), 98);

  // Card B: 消耗趋势 (x=5, y=117, w=125, h=100)
  drawTopAccentCard(ox + 5, 117, 125, 100, kCyan, kCardBorder, false);
  canvas_.setTextColor(to565(kTextSec));
  canvas_.drawString("消耗趋势(40)", ox + 11, 123);

  const size_t n = model.history ? model.history->size() : 0;
  char sBuf[20];
  uint32_t sCol = kTextMuted;
  if (n >= 2) {
    const double dlt = model.history->sessionDelta();
    if (std::fabs(dlt) <= 0.005) {
      std::snprintf(sBuf, sizeof(sBuf), "持平");
      sCol = kOffEmerald;
    } else {
      std::snprintf(sBuf, sizeof(sBuf), "%+.2f", dlt);
      sCol = (dlt < 0.0) ? kWarnRed : kOffEmerald;
    }
  } else {
    std::snprintf(sBuf, sizeof(sBuf), "--");
  }
  canvas_.setTextColor(to565(sCol));
  canvas_.drawString(sBuf, ox + 123 - canvas_.textWidth(sBuf), 123);

  drawSparkline(ox + 10, 138, 115, 62, model, false);

  char nBuf[20];
  std::snprintf(nBuf, sizeof(nBuf), "采样 %u次", static_cast<unsigned>(n));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(nBuf, ox + 11, 202);
  const char* pollStr = (model.alertEnabled && model.phaseKind == PhaseKind::kPeak) ? "15s监控" : "5m轮询";
  canvas_.drawString(pollStr, ox + 123 - canvas_.textWidth(pollStr), 202);
}

void LiangziUi::drawPortraitPage2(int ox, const UiModel& model) {
  const PhaseVisual pv = describePhase(model);
  // Card A: 今日排班 (x=5, y=37, w=125, h=62)
  drawTopAccentCard(ox + 5, 37, 125, 62, pv.color, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextSec));
  canvas_.drawString("今日排班", ox + 11, 42);
  canvas_.setTextColor(to565(pv.color));
  canvas_.drawString(pv.dayTag, ox + 123 - canvas_.textWidth(pv.dayTag), 42);

  drawTimeline24h(ox + 11, 58, 113, 9, model, true);

  char schedBuf[40];
  if (model.phaseKind == PhaseKind::kOffPeakHoliday || model.phaseKind == PhaseKind::kOffPeakWeekend) {
    std::snprintf(schedBuf, sizeof(schedBuf), "全天低谷 · 1x");
  } else {
    char rng[28];
    formatPeakRangesText(model.schedule, rng, sizeof(rng));
    std::snprintf(schedBuf, sizeof(schedBuf), "高峰 %s", rng);
  }
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(schedBuf, ox + 67 - canvas_.textWidth(schedBuf) / 2, 82);

  // Card B: 系统诊断 (x=5, y=103, w=125, h=114)
  drawTopAccentCard(ox + 5, 103, 125, 114, kCardBorder, kCardBorder, false);
  const int rX = ox + 123;
  const int vWifi = ox + 38;
  const int vLeft = ox + 32;
  int ry = 109;

  // Row 1: WiFi (efontCN_12 fallback for arbitrary UTF-8 SSID)
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("WiFi", ox + 11, ry);
  if (model.wifiConnected) {
    char wVal[48];
    std::snprintf(wVal, sizeof(wVal), "%s(%d)", model.ssid.c_str(), model.wifiRssi);
    drawTruncatedRight(wVal, vWifi, rX, ry, kTextPrimary, &lgfx::fonts::efontCN_12);
  } else {
    drawTruncatedRight(model.configured ? "断线" : "未配置", vWifi, rX, ry, kWarnRed, cn12Ptr());
  }
  ry += 17;

  // Row 2: IP
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("IP", ox + 11, ry);
  drawTruncatedRight(model.wifiConnected ? model.ip.c_str() : "-", vLeft, rX, ry, kTextSec, cn12Ptr());
  ry += 17;

  // Row 3: NTP
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("NTP", ox + 11, ry);
  drawTruncatedRight(model.ntp.c_str(), vLeft, rX, ry, kTextSec, cn12Ptr());
  ry += 17;

  // Row 4: RWR
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("RWR", ox + 11, ry);
  const char* rwrVal = model.alertEnabled ? (pv.isPeak ? "开·15s监控" : "开·待机") : "关闭·5m";
  drawTruncatedRight(rwrVal, vLeft, rX, ry, model.alertEnabled ? kPeakAmber : kTextMuted, cn12Ptr());
  ry += 17;

  // Row 5: HTTP + Threshold
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("HTTP", ox + 11, ry);
  char httpBuf[32];
  std::snprintf(httpBuf, sizeof(httpBuf), "%d · 阈值¥%.0f", model.httpStatus, model.balanceWarn);
  drawTruncatedRight(model.httpStatus > 0 ? httpBuf : "-", vLeft, rX, ry,
                     model.httpStatus == 200 ? kOffEmerald : (model.httpStatus > 0 ? kWarnRed : kTextMuted),
                     cn12Ptr());
  ry += 17;

  // Row 6: Rotation & FW
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("方向", ox + 11, ry);
  char rotBuf[32];
  std::snprintf(rotBuf, sizeof(rotBuf), "%u°%s · v%s", static_cast<unsigned>(model.rotation * 90),
                model.autoRotate ? "自动" : "固定", model.fwVersion.c_str());
  drawTruncatedRight(rotBuf, vLeft, rX, ry, kTextMuted, cn12Ptr());
}

// ---------- Landscape Pages (240 x 135) ----------

void LiangziUi::drawLandscapePage0(int ox, const UiModel& model, uint32_t nowMs) {
  if (!model.configured) {
    drawSetupCard(ox + 6, 19, 228, 97, nowMs);
    return;
  }

  const PhaseVisual pv = describePhase(model);
  const bool mslFlash = model.mslAlertActive && (((nowMs / 320u) & 1u) == 0u);

  // ADR-0005: Keep Peak hero card on the USB-port side:
  //   rotation == 1 (90°, USB right) -> Peak right (123), Balance left (6)
  //   rotation == 3 (270°, USB left) -> Peak left (6), Balance right (123)
  const bool peakRight = (model.rotation == 1);
  const int peakX = ox + (peakRight ? 123 : 6);
  const int balX = ox + (peakRight ? 6 : 123);
  const int cw = 111;
  const int ch = 97;

  // ---- Peak Card (111x97) ----
  drawTopAccentCard(peakX, 19, cw, ch, pv.color, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(pv.subLabel, peakX + 7, 24);

  if (model.alertEnabled) {
    const char* rTag = model.mslAlertActive ? "MSL" : (pv.isPeak ? "15s" : "RWR");
    const uint32_t rCol = model.mslAlertActive ? kWarnRed : pv.color;
    canvas_.setFont(meta10Ptr());
    const int rw = canvas_.textWidth(rTag) + 6;
    canvas_.fillSmoothRoundRect(peakX + cw - 6 - rw, 24, rw, 12, 3, to565(mixRgb(rCol, kCardBg, 0.82f)));
    canvas_.setTextColor(to565(rCol));
    canvas_.drawString(rTag, peakX + cw - 6 - rw + 3, 25);
  }

  if (pv.synced) {
    canvas_.setFont(hero30Ptr());
    const int hw = canvas_.textWidth(pv.heroText);
    const int hx = peakX + cw / 2 - hw / 2;
    canvas_.setTextColor(to565(0x05070A));
    canvas_.drawString(pv.heroText, hx + 1, 41);
    canvas_.setTextColor(to565(pv.color));
    canvas_.drawString(pv.heroText, hx, 40);
  } else {
    canvas_.setFont(cn12Ptr());
    const int hw = canvas_.textWidth(pv.heroText);
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(pv.heroText, peakX + cw / 2 - hw / 2, 48);
  }

  char cbuf[20];
  formatCountdown(pv.synced ? model.countdownSec : -1, cbuf, sizeof(cbuf));
  canvas_.setFont(cn12Ptr());
  const int lblW = canvas_.textWidth("剩余");
  canvas_.setFont(num16Ptr());
  const int numW = canvas_.textWidth(cbuf);
  const int totW = lblW + 3 + numW;
  const int sx = peakX + cw / 2 - totW / 2;
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("剩余", sx, 74);
  canvas_.setFont(num16Ptr());
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(cbuf, sx + lblW + 3, 73);

  drawTimeline24h(peakX + 8, 94, cw - 16, 6, model, false);
  canvas_.setFont(meta10Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("00", peakX + 8, 102);
  canvas_.drawString("12", peakX + cw / 2 - canvas_.textWidth("12") / 2, 102);
  canvas_.drawString("24", peakX + cw - 8 - canvas_.textWidth("24"), 102);

  // ---- Balance Card (111x97) ----
  const bool lowBal = model.balanceValid && (model.totalBalance < static_cast<double>(model.balanceWarn));
  const bool redState = lowBal || (model.balanceValid && !model.balanceAvailable);
  const uint32_t balBorder = mslFlash ? kWarnRed : (redState ? mixRgb(kWarnRed, kCardBorder, 0.25f) : kCardBorder);
  drawTopAccentCard(balX, 19, cw, ch, redState ? kWarnRed : kCyan, balBorder, mslFlash);

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("可用余额", balX + 7, 24);

  char badge[16] = "";
  uint32_t badgeCol = kTextMuted;
  if (model.balanceExpired) {
    std::snprintf(badge, sizeof(badge), "旧");
    badgeCol = kPeakAmber;
  } else if (model.balanceValid && model.hasDelta && std::fabs(model.lastDelta) > 0.005) {
    std::snprintf(badge, sizeof(badge), "%+.2f", model.lastDelta);
    badgeCol = (model.lastDelta < 0.0) ? kWarnRed : kOffEmerald;
  }
  if (badge[0]) {
    const bool isCn = model.balanceExpired;
    canvas_.setFont(isCn ? cn12Ptr() : meta10Ptr());
    const int bw = canvas_.textWidth(badge) + 8;
    const int bx = balX + cw - 6 - bw;
    canvas_.fillSmoothRoundRect(bx, 24, bw, 12, 3, to565(mixRgb(badgeCol, kCardBg, 0.80f)));
    canvas_.setTextColor(to565(badgeCol));
    canvas_.drawString(badge, bx + 4, isCn ? 24 : 25);
  }

  char bbuf[28];
  if (!model.balanceValid) {
    std::snprintf(bbuf, sizeof(bbuf), "--");
  } else {
    std::snprintf(bbuf, sizeof(bbuf), "%s%.2f", currencySymbol(model.currency), model.totalBalance);
  }
  canvas_.setFont(num22Ptr());
  if (canvas_.textWidth(bbuf) > cw - 12) canvas_.setFont(num16Ptr());
  const int bW = canvas_.textWidth(bbuf);
  const uint32_t bCol = !model.balanceValid ? kTextMuted : (lowBal ? kWarnRed : kTextPrimary);
  canvas_.setTextColor(to565(bCol));
  canvas_.drawString(bbuf, balX + cw / 2 - bW / 2, 46);

  drawSplitBar(balX + 8, 76, cw - 16, 5, model, splitReveal_.value());
  canvas_.setFont(cn12Ptr());
  if (model.balanceValid && model.hasSplit) {
    char gl[24], tr[24];
    std::snprintf(gl, sizeof(gl), "赠%.2f", model.grantedBalance);
    std::snprintf(tr, sizeof(tr), "充%.2f", model.toppedUpBalance);
    if (canvas_.textWidth(gl) + canvas_.textWidth(tr) > cw - 18) {
      std::snprintf(gl, sizeof(gl), "赠%.1f", model.grantedBalance);
      std::snprintf(tr, sizeof(tr), "充%.1f", model.toppedUpBalance);
    }
    canvas_.setTextColor(to565(kViolet));
    canvas_.drawString(gl, balX + 8, 85);
    canvas_.setTextColor(to565(kCyan));
    canvas_.drawString(tr, balX + cw - 8 - canvas_.textWidth(tr), 85);
  } else {
    const char* waitTxt = model.balanceValid ? "无拆分字段" : "等待查询";
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(waitTxt, balX + cw / 2 - canvas_.textWidth(waitTxt) / 2, 85);
  }

  char subBuf[28];
  if (model.httpStatus > 0) {
    std::snprintf(subBuf, sizeof(subBuf), "阈值¥%.0f · HTTP%d", model.balanceWarn, model.httpStatus);
  } else {
    std::snprintf(subBuf, sizeof(subBuf), "告警阈值 ¥%.0f", model.balanceWarn);
  }
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(subBuf, balX + cw / 2 - canvas_.textWidth(subBuf) / 2, 100);
}

void LiangziUi::drawLandscapePage1(int ox, const UiModel& model) {
  // Left Split Card: x=6, y=19, w=111, h=97
  drawTopAccentCard(ox + 6, 19, 111, 97, kViolet, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextSec));
  canvas_.drawString("构成", ox + 12, 24);

  char st[20];
  if (!model.configured) std::snprintf(st, sizeof(st), "--");
  else std::snprintf(st, sizeof(st), "%s·%s", model.currency.c_str(),
                     model.balanceAvailable ? "可用" : "停用");
  canvas_.setTextColor(to565(!model.configured ? kTextMuted : (model.balanceAvailable ? kOffEmerald : kWarnRed)));
  canvas_.drawString(st, ox + 111 - canvas_.textWidth(st), 24);

  char v1[20], v2[20];
  if (model.balanceValid && model.hasSplit) {
    std::snprintf(v1, sizeof(v1), "%s%.2f", currencySymbol(model.currency), model.grantedBalance);
    std::snprintf(v2, sizeof(v2), "%s%.2f", currencySymbol(model.currency), model.toppedUpBalance);
  } else {
    std::snprintf(v1, sizeof(v1), "--");
    std::snprintf(v2, sizeof(v2), "--");
  }
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kViolet));
  canvas_.drawString("赠金优先", ox + 12, 41);
  canvas_.setFont(meta10Ptr());
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(v1, ox + 111 - canvas_.textWidth(v1), 42);

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kCyan));
  canvas_.drawString("充值余额", ox + 12, 58);
  canvas_.setFont(meta10Ptr());
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(v2, ox + 111 - canvas_.textWidth(v2), 59);

  drawSplitBar(ox + 12, 77, 99, 6, model, splitReveal_.value());

  const bool lowBal = model.balanceValid && (model.totalBalance < static_cast<double>(model.balanceWarn));
  char thBuf[28];
  std::snprintf(thBuf, sizeof(thBuf), "阈值¥%.0f · %s", model.balanceWarn,
                !model.balanceValid ? "--" : (lowBal ? "告警" : "正常"));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(!model.balanceValid ? kTextMuted : (lowBal ? kWarnRed : kTextMuted)));
  canvas_.drawString(thBuf, ox + 12, 94);

  // Right Trend Card: x=123, y=19, w=111, h=97
  drawTopAccentCard(ox + 123, 19, 111, 97, kCyan, kCardBorder, false);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextSec));
  canvas_.drawString("消耗(40)", ox + 129, 24);

  const size_t n = model.history ? model.history->size() : 0;
  char sBuf[16];
  uint32_t sCol = kTextMuted;
  if (n >= 2) {
    const double dlt = model.history->sessionDelta();
    if (std::fabs(dlt) <= 0.005) {
      std::snprintf(sBuf, sizeof(sBuf), "持平");
      sCol = kOffEmerald;
    } else {
      std::snprintf(sBuf, sizeof(sBuf), "%+.2f", dlt);
      sCol = (dlt < 0.0) ? kWarnRed : kOffEmerald;
    }
  } else {
    std::snprintf(sBuf, sizeof(sBuf), "--");
  }
  canvas_.setTextColor(to565(sCol));
  canvas_.drawString(sBuf, ox + 228 - canvas_.textWidth(sBuf), 24);

  drawSparkline(ox + 129, 40, 99, 56, model, true);

  char nBuf[20];
  std::snprintf(nBuf, sizeof(nBuf), "采样%u次", static_cast<unsigned>(n));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(nBuf, ox + 129, 99);
  const char* pollStr = (model.alertEnabled && model.phaseKind == PhaseKind::kPeak) ? "15s" : "5m";
  canvas_.drawString(pollStr, ox + 228 - canvas_.textWidth(pollStr), 99);
}

void LiangziUi::drawLandscapePage2(int ox, const UiModel& model) {
  const PhaseVisual pv = describePhase(model);
  // Top Schedule Card: x=6, y=19, w=228, h=44
  drawTopAccentCard(ox + 6, 19, 228, 44, pv.color, kCardBorder, false);
  char titleBuf[40];
  std::snprintf(titleBuf, sizeof(titleBuf), "今日排班 · %s", pv.dayTag);
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(pv.color));
  canvas_.drawString(titleBuf, ox + 12, 23);

  char rngBuf[32];
  if (model.phaseKind == PhaseKind::kOffPeakHoliday || model.phaseKind == PhaseKind::kOffPeakWeekend) {
    std::snprintf(rngBuf, sizeof(rngBuf), "全天低谷 · 1x");
  } else {
    char tmp[28];
    formatPeakRangesText(model.schedule, tmp, sizeof(tmp));
    std::snprintf(rngBuf, sizeof(rngBuf), "高峰 %s", tmp);
  }
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(rngBuf, ox + 228 - canvas_.textWidth(rngBuf), 23);

  drawTimeline24h(ox + 12, 39, 216, 8, model, true);

  // Bottom Left System Card: x=6, y=67, w=111, h=49
  drawTopAccentCard(ox + 6, 67, 111, 49, kCyan, kCardBorder, false);
  const int lR = ox + 111;
  const int lWifiLeft = ox + 37;
  const int lValLeft = ox + 28;
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("WiFi", ox + 12, 72);
  if (model.wifiConnected) {
    char wVal[48];
    std::snprintf(wVal, sizeof(wVal), "%s(%d)", model.ssid.c_str(), model.wifiRssi);
    drawTruncatedRight(wVal, lWifiLeft, lR, 72, kTextPrimary, &lgfx::fonts::efontCN_12);
  } else {
    drawTruncatedRight(model.configured ? "断线" : "未配置", lWifiLeft, lR, 72, kWarnRed, cn12Ptr());
  }

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("IP", ox + 12, 85);
  drawTruncatedRight(model.wifiConnected ? model.ip.c_str() : "-", ox + 28, lR, 86, kTextSec, meta10Ptr());

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("NTP", ox + 12, 98);
  drawTruncatedRight(model.ntp.c_str(), ox + 36, lR, 99, kTextSec, meta10Ptr());

  // Bottom Right System Card: x=123, y=67, w=111, h=49
  drawTopAccentCard(ox + 123, 67, 111, 49, kViolet, kCardBorder, false);
  const int rR = ox + 228;
  const int rValLeft = ox + 156;
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("RWR", ox + 129, 72);
  const char* rwrTxt = model.alertEnabled ? (pv.isPeak ? "开·15s" : "开·待机") : "关闭·5m";
  drawTruncatedRight(rwrTxt, rValLeft, rR, 72, model.alertEnabled ? kPeakAmber : kTextMuted, cn12Ptr());

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("HTTP", ox + 129, 85);
  char hBuf[28];
  std::snprintf(hBuf, sizeof(hBuf), "%d · ¥%.0f", model.httpStatus, model.balanceWarn);
  drawTruncatedRight(model.httpStatus > 0 ? hBuf : "-", rValLeft, rR, 85,
                     model.httpStatus == 200 ? kOffEmerald : (model.httpStatus > 0 ? kWarnRed : kTextMuted),
                     cn12Ptr());

  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("方向", ox + 129, 98);
  char rotBuf[28];
  std::snprintf(rotBuf, sizeof(rotBuf), "%u°%s %s", static_cast<unsigned>(model.rotation * 90),
                model.autoRotate ? "自动" : "固定", model.fwVersion.c_str());
  drawTruncatedRight(rotBuf, rValLeft, rR, 98, kTextMuted, cn12Ptr());
}

void LiangziUi::drawFooter(const UiModel& model, uint32_t nowMs) {
  const bool port = isPortrait();
  const int yTop = port ? 221 : 119;
  const int barH = port ? 14 : 13;
  const int aX = port ? 5 : 6;
  const int aW = port ? 34 : 44;
  const bool pulsing = (nowMs < pulseUntilMs_);

  canvas_.fillSmoothRoundRect(aX, yTop, aW, barH, 3,
                              to565(pulsing ? mixRgb(kCyan, kPillBg, 0.72f) : kPillBg));
  canvas_.drawRoundRect(aX, yTop, aW, barH, 3, to565(pulsing ? kCyan : kCardBorder));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(pulsing ? kCyan : kTextSec));
  const char* aLbl = port ? "A 刷" : "A 刷新";
  canvas_.drawString(aLbl, aX + (aW - canvas_.textWidth(aLbl)) / 2, yTop);

  // Morphing 3-page indicator pills
  int widths[kPageCount];
  int totalW = (kPageCount - 1) * 4;
  for (int i = 0; i < kPageCount; ++i) {
    const float active = std::max(0.0f, 1.0f - std::fabs(pageScroll_.value() - static_cast<float>(i)));
    widths[i] = 4 + static_cast<int>(std::lround(active * 10.0f));
    totalW += widths[i];
  }
  int px = (width_ - totalW) / 2;
  const int pillY = yTop + (barH - 4) / 2;
  for (int i = 0; i < kPageCount; ++i) {
    const float active = std::max(0.0f, 1.0f - std::fabs(pageScroll_.value() - static_cast<float>(i)));
    const uint32_t c = mixRgb(0x2A394A, kCyan, active);
    canvas_.fillSmoothRoundRect(px, pillY, widths[i], 4, 2, to565(c));
    px += widths[i] + 4;
  }

  // Right [B] button + refresh age on landscape
  const int bW = port ? 34 : 26;
  const int bX = width_ - (port ? 5 : 6) - bW;
  if (!port && model.configured && model.balanceValid) {
    char ageBuf[16];
    const uint32_t ageS = (nowMs - model.lastFetchMs) / 1000u;
    if (ageS >= 60u) std::snprintf(ageBuf, sizeof(ageBuf), "%lum前", static_cast<unsigned long>(ageS / 60u));
    else std::snprintf(ageBuf, sizeof(ageBuf), "%lus前", static_cast<unsigned long>(ageS));
    canvas_.setFont(cn12Ptr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(ageBuf, bX - 4 - canvas_.textWidth(ageBuf), yTop);
  }
  canvas_.fillSmoothRoundRect(bX, yTop, bW, barH, 3, to565(kPillBg));
  canvas_.drawRoundRect(bX, yTop, bW, barH, 3, to565(kCardBorder));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextColor(to565(kTextSec));
  const char* bLbl = port ? "B 页" : "B";
  canvas_.drawString(bLbl, bX + (bW - canvas_.textWidth(bLbl)) / 2, yTop);

  // Bottom 2px auto-refresh progress bar
  float prog = 0.0f;
  if (model.queryBusy) {
    prog = 1.0f;
  } else if (model.configured && model.wifiConnected && model.refreshIntervalMs > 0) {
    const uint32_t elapsed = nowMs - model.lastFetchMs;
    prog = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(model.refreshIntervalMs));
  }
  canvas_.fillRect(0, height_ - 2, width_, 2, to565(0x141D28));
  const int pw = static_cast<int>(std::lround(static_cast<float>(width_) * prog));
  if (pw > 0) {
    const uint32_t barCol = model.queryBusy
                                ? kCyan
                                : ((model.alertEnabled && model.phaseKind == PhaseKind::kPeak) ? kPeakAmber : kCyan);
    canvas_.fillRect(0, height_ - 2, pw, 2, to565(mixRgb(barCol, kCardBg, 0.28f)));
  }
}

void LiangziUi::drawHoldOverlay(uint8_t curRotation, float progress) {
  if (progress <= 0.01f) return;
  const float p = std::min(1.0f, progress);
  const int bw = 96;
  const int bh = 74;
  const int bx = (width_ - bw) / 2;
  const int by = (height_ - bh) / 2;
  canvas_.fillSmoothRoundRect(bx, by, bw, bh, 12, to565(kBgBase));
  canvas_.drawRoundRect(bx, by, bw, bh, 12, to565(kCardBorder));

  const int cx = width_ / 2;
  const int cy = height_ / 2;
  canvas_.fillArc(cx, cy, 26, 30, 0, 360, to565(kCardBorder));
  canvas_.fillArc(cx, cy, 26, 30, 270.0f, 270.0f + 360.0f * p, to565(kCyan));

  char lbl[20];
  std::snprintf(lbl, sizeof(lbl), "旋至%u°", static_cast<unsigned>(((curRotation + 1u) & 3u) * 90u));
  canvas_.setFont(cn12Ptr());
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(lbl, cx - canvas_.textWidth(lbl) / 2, cy - 6);
}

void LiangziUi::drawToastOverlay(uint32_t nowMs) {
  if (toastText_.empty() || nowMs >= toastUntilMs_) return;
  canvas_.setFont(cn12Ptr());
  const int tw = std::min(width_ - 16, std::max(72, static_cast<int>(canvas_.textWidth(toastText_.c_str())) + 18));
  const int th = 20;
  const int tx = (width_ - tw) / 2;
  const int ty = isPortrait() ? 39 : 22;
  canvas_.fillSmoothRoundRect(tx, ty, tw, th, 6, to565(0x14202E));
  canvas_.drawRoundRect(tx, ty, tw, th, 6, to565(kCyan));
  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(toastText_.c_str(), (width_ - canvas_.textWidth(toastText_.c_str())) / 2, ty + 3);
}

uint64_t LiangziUi::computeFingerprint(const UiModel& model, uint32_t nowMs) const {
  uint64_t h = 14695981039346656037ULL;
  h = fnvHashVal(h, width_);
  h = fnvHashVal(h, height_);
  h = fnvHashVal(h, page_);
  h = fnvHashVal(h, model.configured);
  h = fnvHashVal(h, model.wifiConnected);
  h = fnvHashVal(h, model.wifiRssi);
  h = fnvHashBytes(h, model.ssid.data(), model.ssid.size());
  h = fnvHashBytes(h, model.ip.data(), model.ip.size());
  h = fnvHashBytes(h, model.ntp.data(), model.ntp.size());
  h = fnvHashVal(h, model.timeSynced);
  h = fnvHashVal(h, model.year);
  h = fnvHashVal(h, model.month);
  h = fnvHashVal(h, model.day);
  h = fnvHashVal(h, model.wday);
  h = fnvHashVal(h, model.hour);
  h = fnvHashVal(h, model.minute);
  h = fnvHashVal(h, model.second);
  h = fnvHashVal(h, model.phaseKind);
  h = fnvHashVal(h, model.countdownSec);
  h = fnvHashVal(h, model.schedule.customCount);
  h = fnvHashVal(h, model.balanceValid);
  h = fnvHashVal(h, model.balanceExpired);
  h = fnvHashVal(h, model.balanceAvailable);
  h = fnvHashVal(h, model.queryBusy);
  h = fnvHashVal(h, model.httpStatus);
  h = fnvHashVal(h, model.totalBalance);
  h = fnvHashVal(h, model.grantedBalance);
  h = fnvHashVal(h, model.toppedUpBalance);
  h = fnvHashVal(h, model.hasSplit);
  h = fnvHashVal(h, model.balanceWarn);
  h = fnvHashVal(h, model.lastDelta);
  h = fnvHashVal(h, model.hasDelta);
  const uint32_t ageSec = (nowMs - model.lastFetchMs) / 1000u;
  h = fnvHashVal(h, ageSec);
  const uint32_t barPx = (model.configured && model.wifiConnected && model.refreshIntervalMs > 0)
                             ? static_cast<uint32_t>(std::min<uint64_t>(
                                   width_, (uint64_t(nowMs - model.lastFetchMs) * width_) / model.refreshIntervalMs))
                             : 0u;
  h = fnvHashVal(h, barPx);
  h = fnvHashVal(h, model.alertEnabled);
  h = fnvHashVal(h, model.mslAlertActive);
  h = fnvHashVal(h, model.batteryPct);
  h = fnvHashVal(h, model.charging);
  h = fnvHashVal(h, model.rotation);
  h = fnvHashVal(h, model.autoRotate);
  h = fnvHashVal(h, model.holdBProgress);
  h = fnvHashVal(h, model.dataRevision);
  const bool toastOn = (!toastText_.empty() && nowMs < toastUntilMs_);
  h = fnvHashVal(h, toastOn);
  return h;
}

void LiangziUi::render(const UiModel& model, uint32_t nowMs, bool pushToTarget) {
  const float dt = lastStepMs_ ? static_cast<float>(nowMs - lastStepMs_) / 1000.0f : 0.016f;
  lastStepMs_ = nowMs;

  if (model.dataRevision != lastRevision_) {
    lastRevision_ = model.dataRevision;
    splitReveal_.start(0.12f, 1.0f, nowMs, 320);
    forceDirty_ = true;
  }
  if (model.phaseKind != lastPhase_) {
    lastPhase_ = model.phaseKind;
    forceDirty_ = true;
  }

  const bool moving = pageScroll_.step(dt) | pillPos_.step(dt) | pillWidth_.step(dt);
  const bool easing = splitReveal_.isMoving();
  splitReveal_.update(nowMs);

  const bool activeFx = moving || easing || model.queryBusy || model.mslAlertActive ||
                        (nowMs < pulseUntilMs_) || (model.holdBProgress > 0.01f) ||
                        (!toastText_.empty() && nowMs < toastUntilMs_) || !model.configured ||
                        (model.configured && !model.wifiConnected);

  const uint64_t fp = computeFingerprint(model, nowMs);
  if (!activeFx && !forceDirty_ && fp == lastIdleHash_) {
    return;
  }
  lastIdleHash_ = fp;
  forceDirty_ = false;

  canvas_.fillSprite(to565(kBgBase));
  drawTopBar(model, nowMs);

  if (isPortrait()) {
    // Portrait Date & Time strip: y=19..33
    canvas_.fillSmoothRoundRect(5, 19, 125, 15, 4, to565(kWellBg));
    char dStr[24], tStr[16];
    if (model.configured && model.timeSynced) {
      std::snprintf(dStr, sizeof(dStr), "%02d-%02d %s", model.month, model.day, weekdayCn(model.wday));
      std::snprintf(tStr, sizeof(tStr), "%02d:%02d:%02d", model.hour, model.minute, model.second);
    } else if (!model.configured) {
      std::snprintf(dStr, sizeof(dStr), "出厂未配置");
      std::snprintf(tStr, sizeof(tStr), "--:--:--");
    } else {
      std::snprintf(dStr, sizeof(dStr), "等待同步");
      std::snprintf(tStr, sizeof(tStr), "--:--:--");
    }
    canvas_.setFont(cn12Ptr());
    canvas_.setTextDatum(lgfx::top_left);
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(dStr, 10, 20);
    canvas_.setTextColor(to565((model.configured && model.timeSynced) ? kTextPrimary : kTextMuted));
    canvas_.drawString(tStr, 125 - canvas_.textWidth(tStr), 20);
  }

  const int stageTop = isPortrait() ? 36 : 18;
  const int stageBottom = isPortrait() ? 219 : 118;
  restoreStageClip(stageTop, stageBottom);

  const float baseOff = -pageScroll_.value() * static_cast<float>(width_);
  for (int p = 0; p < kPageCount; ++p) {
    const int ox = static_cast<int>(std::lround(baseOff + static_cast<float>(p * width_)));
    if (ox <= -width_ || ox >= width_) continue;
    if (isPortrait()) {
      if (p == 0) drawPortraitPage0(ox, model, nowMs);
      else if (p == 1) drawPortraitPage1(ox, model);
      else drawPortraitPage2(ox, model);
    } else {
      if (p == 0) drawLandscapePage0(ox, model, nowMs);
      else if (p == 1) drawLandscapePage1(ox, model);
      else drawLandscapePage2(ox, model);
    }
  }
  canvas_.clearClipRect();

  drawFooter(model, nowMs);
  drawHoldOverlay(model.rotation, model.holdBProgress);
  drawToastOverlay(nowMs);

  if (pushToTarget && target_) {
    canvas_.pushSprite(target_, 0, 0);
  }
}

uint32_t LiangziUi::recommendedFrameIntervalMs(const UiModel& model, uint32_t nowMs) const {
  if (model.queryBusy || model.mslAlertActive || nowMs < pulseUntilMs_ ||
      model.holdBProgress > 0.01f || (!toastText_.empty() && nowMs < toastUntilMs_)) {
    return 25;
  }
  if (!model.configured || !model.wifiConnected) return 35;
  if (pageScroll_.isMoving() || pillPos_.isMoving() || pillWidth_.isMoving() ||
      splitReveal_.isMoving()) {
    return 25;
  }
  return 500;
}

}  // namespace liangzi
