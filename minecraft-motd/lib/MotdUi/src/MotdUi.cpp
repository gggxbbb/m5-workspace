#include "MotdUi.h"
#include "MotdFonts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace motd {
namespace {

constexpr uint32_t kBgBase = 0x090D12;
constexpr uint32_t kCardBg = 0x121A23;
constexpr uint32_t kWellBg = 0x0D141C;
constexpr uint32_t kCardBorder = 0x1F2E3D;
constexpr uint32_t kPillBg = 0x1A2634;
constexpr uint32_t kTextPrimary = 0xEEF4F8;
constexpr uint32_t kTextMuted = 0x7E93A6;
constexpr uint32_t kEmerald = 0x4ADE80;
constexpr uint32_t kGold = 0xFBBF24;
constexpr uint32_t kRedstone = 0xF87171;
constexpr uint32_t kOrange = 0xFB923C;
constexpr uint32_t kDiamond = 0x38BDF8;
constexpr uint32_t kViolet = 0xA78BFA;
constexpr uint32_t kBlue = 0x60A5FA;

constexpr int kIconSize = 44;

inline uint16_t to565(uint32_t rgb) {
  return lgfx::color565(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                        static_cast<uint8_t>((rgb >> 8) & 0xFF),
                        static_cast<uint8_t>(rgb & 0xFF));
}

bool parsePngSize(const std::vector<uint8_t>& png, uint32_t& w, uint32_t& h) {
  if (png.size() < 24) return false;
  static constexpr uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (std::memcmp(png.data(), kSig, 8) != 0) return false;
  if (png[12] != 'I' || png[13] != 'H' || png[14] != 'D' || png[15] != 'R') return false;
  w = (uint32_t(png[16]) << 24) | (uint32_t(png[17]) << 16) | (uint32_t(png[18]) << 8) | png[19];
  h = (uint32_t(png[20]) << 24) | (uint32_t(png[21]) << 16) | (uint32_t(png[22]) << 8) | png[23];
  return w > 0 && h > 0 && w <= 256 && h <= 256;
}

// Replace ASCII printable characters in-place for MC §k obfuscation effect.
std::string scrambleAscii(const std::string& input, uint32_t seed) {
  static constexpr char kPool[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*?@";
  static constexpr size_t kPoolLen = sizeof(kPool) - 1;
  std::string out = input;
  for (size_t i = 0; i < out.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(out[i]);
    if (ch > 0x20 && ch < 0x7F) {
      out[i] = kPool[(seed + i * 7u) % kPoolLen];
    }
  }
  return out;
}

std::string formatCommaNumber(int32_t n) {
  if (n < 0) return "--";
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%ld", static_cast<long>(n));
  std::string raw(buf);
  if (raw.size() <= 3) return raw;
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (i > 0 && (raw.size() - i) % 3 == 0) out.push_back(',');
    out.push_back(raw[i]);
  }
  return out;
}

uint32_t latencyColor(int32_t pongMs, bool statusOnly) {
  if (pongMs < 0) return statusOnly ? kRedstone : kTextMuted;
  if (pongMs < 150) return kEmerald;
  if (pongMs < 350) return kGold;
  if (pongMs < 700) return kOrange;
  return kRedstone;
}

}  // namespace

bool MotdUi::begin(lgfx::LovyanGFX* target, bool usePsram) {
  target_ = target;

  canvas_.setPsram(usePsram);
  canvas_.setColorDepth(16);
  if (!canvas_.createSprite(kWidth, kHeight)) return false;
  canvas_.setTextWrap(false);

  iconCanvas_.setPsram(usePsram);
  iconCanvas_.setColorDepth(16);
  if (!iconCanvas_.createSprite(kIconSize, kIconSize)) return false;

  metaWrap_.set(motd::fonts::kFontMeta10, motd::fonts::kFontMeta10Size);
  titleWrap_.set(motd::fonts::kFontTitle14, motd::fonts::kFontTitle14Size);
  numWrap_.set(motd::fonts::kFontNum18, motd::fonts::kFontNum18Size);

  metaLoaded_ = metaFont_.loadFont(&metaWrap_);
  titleLoaded_ = titleFont_.loadFont(&titleWrap_);
  numLoaded_ = numFont_.loadFont(&numWrap_);

  drawGrassBlockIcon();
  accentColor_ = kEmerald;
  return true;
}

void MotdUi::clipToStage(int x, int y, int w, int h) {
  constexpr int kStageX = 0;
  constexpr int kStageY = 18;
  constexpr int kStageR = kWidth;
  constexpr int kStageB = 120;
  const int left = std::max(kStageX, x);
  const int top = std::max(kStageY, y);
  const int right = std::min(kStageR, x + w);
  const int bottom = std::min(kStageB, y + h);
  canvas_.setClipRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

void MotdUi::restoreStageClip() {
  canvas_.setClipRect(0, 18, kWidth, 102);
}

const lgfx::IFont* MotdUi::metaFontPtr() const {
  return metaLoaded_ ? static_cast<const lgfx::IFont*>(&metaFont_)
                     : static_cast<const lgfx::IFont*>(&::fonts::efontCN_10);
}

const lgfx::IFont* MotdUi::titleFontPtr() const {
  return titleLoaded_ ? static_cast<const lgfx::IFont*>(&titleFont_)
                      : static_cast<const lgfx::IFont*>(&::fonts::efontCN_14);
}

const lgfx::IFont* MotdUi::numFontPtr() const {
  return numLoaded_ ? static_cast<const lgfx::IFont*>(&numFont_)
                    : static_cast<const lgfx::IFont*>(&::fonts::efontCN_16);
}

void MotdUi::setPage(int page, uint32_t nowMs) {
  const int clamped = ((page % kPageCount) + kPageCount) % kPageCount;
  if (clamped == page_) return;
  page_ = clamped;
  pageScroll_.start(pageScroll_.value(), static_cast<float>(page_), nowMs, 200);
  if (page_ == 2) {
    waterfallReveal_.start(0.08f, 1.0f, nowMs, 340);
  }
}

void MotdUi::nextPage(uint32_t nowMs) {
  setPage((page_ + 1) % kPageCount, nowMs);
}

void MotdUi::triggerPulse(uint32_t nowMs) {
  pulseUntilMs_ = nowMs + 450;
}

void MotdUi::resetMotdCycle(uint32_t nowMs) {
  animStartMs_ = nowMs;
  motdPageIdx_ = 0;
  motdPageSwitchMs_ = nowMs;
  motdScrollY_.snap(0.0f);
}

void MotdUi::drawGrassBlockIcon() {
  iconCanvas_.fillSprite(to565(kCardBg));
  static constexpr uint32_t kGrass[4] = {0x4ADE80, 0x22C55E, 0x16A34A, 0x86EFAC};
  static constexpr uint32_t kDirt[4] = {0x78350F, 0x92400E, 0x6B3410, 0xB45309};
  for (int y = 0; y < 11; ++y) {
    for (int x = 0; x < 11; ++x) {
      const bool grassRow = (y < 3) || (y == 3 && ((x * 7) % 3 != 0));
      const uint32_t c = grassRow ? kGrass[(x * 5 + y * 3) & 3] : kDirt[(x * 3 + y * 7) & 3];
      iconCanvas_.fillRect(x * 4, y * 4, 4, 4, to565(c));
    }
  }
  // Subtle top bevel highlight
  for (int x = 0; x < kIconSize; ++x) {
    auto px = iconCanvas_.readPixelRGB(x, 0);
    uint32_t base = (uint32_t(px.r) << 16) | (uint32_t(px.g) << 8) | px.b;
    iconCanvas_.fillRect(x, 0, 1, 3, to565(mixRgb(base, 0xFFFFFF, 0.16f)));
  }
}

void MotdUi::extractAccentFromIcon() {
  double rSum = 0, gSum = 0, bSum = 0, wSum = 0;
  for (int y = 6; y < kIconSize - 6; y += 2) {
    for (int x = 6; x < kIconSize - 6; x += 2) {
      const auto px = iconCanvas_.readPixelRGB(x, y);
      const float r = px.r / 255.0f;
      const float g = px.g / 255.0f;
      const float b = px.b / 255.0f;
      const float mx = std::max({r, g, b});
      const float mn = std::min({r, g, b});
      const float sat = (mx <= 0.001f) ? 0.0f : (mx - mn) / mx;
      const float lum = 0.5f * (mx + mn);
      if (lum > 0.16f && lum < 0.90f && sat > 0.22f) {
        const float w = sat * (1.0f - std::fabs(lum - 0.55f));
        rSum += px.r * w;
        gSum += px.g * w;
        bSum += px.b * w;
        wSum += w;
      }
    }
  }
  if (wSum < 0.1) {
    accentColor_ = kEmerald;
    return;
  }
  const uint32_t r = std::min<uint32_t>(255, static_cast<uint32_t>(rSum / wSum + 0.5));
  const uint32_t g = std::min<uint32_t>(255, static_cast<uint32_t>(gSum / wSum + 0.5));
  const uint32_t b = std::min<uint32_t>(255, static_cast<uint32_t>(bSum / wSum + 0.5));
  accentColor_ = (r << 16) | (g << 8) | b;
}

void MotdUi::updateIconCache(const UiModel& model) {
  if (model.resultRevision == cachedRevision_) return;
  cachedRevision_ = model.resultRevision;

  bool decoded = false;
  if (model.result && !model.result->faviconPng.empty()) {
    uint32_t pngW = 64, pngH = 64;
    if (parsePngSize(model.result->faviconPng, pngW, pngH)) {
      iconCanvas_.fillSprite(to565(kCardBg));
      const float sx = static_cast<float>(kIconSize) / static_cast<float>(pngW);
      const float sy = static_cast<float>(kIconSize) / static_cast<float>(pngH);
      decoded = iconCanvas_.drawPng(model.result->faviconPng.data(),
                                    static_cast<uint32_t>(model.result->faviconPng.size()),
                                    0, 0, kIconSize, kIconSize, 0, 0, sx, sy);
    }
  }

  if (!decoded) {
    drawGrassBlockIcon();
    accentColor_ = kEmerald;
  } else {
    extractAccentFromIcon();
  }

  // Round corners of 44x44 icon against card background (r = 6)
  const int r = 6;
  const uint16_t cardCol = to565(kCardBg);
  for (int y = 0; y < r; ++y) {
    for (int x = 0; x < r; ++x) {
      const float dx = static_cast<float>(r - x) - 0.5f;
      const float dy = static_cast<float>(r - y) - 0.5f;
      if (dx * dx + dy * dy > static_cast<float>(r * r)) {
        iconCanvas_.drawPixel(x, y, cardCol);
        iconCanvas_.drawPixel(kIconSize - 1 - x, y, cardCol);
        iconCanvas_.drawPixel(x, kIconSize - 1 - y, cardCol);
        iconCanvas_.drawPixel(kIconSize - 1 - x, kIconSize - 1 - y, cardCol);
      }
    }
  }

  // Subtle 1px inner-rounded border on iconCanvas_
  iconCanvas_.drawRoundRect(0, 0, kIconSize, kIconSize, r, to565(mixRgb(accentColor_, kCardBorder, 0.55f)));

  iconScale_.start(0.88f, 1.0f, lastStepMs_, 220);
  motdPageIdx_ = 0;
  motdPageSwitchMs_ = lastStepMs_;
  motdScrollY_.snap(0.0f);
  animStartMs_ = lastStepMs_;
  waterfallReveal_.start(0.08f, 1.0f, lastStepMs_, 320);
}

void MotdUi::drawMcPingBars(int x, int y, int32_t pongMs, bool statusOk, bool busy, uint32_t nowMs) {
  if (!busy && pongMs < 0 && statusOk) {
    canvas_.drawLine(x + 1, y + 2, x + 10, y + 9, to565(kRedstone));
    canvas_.drawLine(x + 1, y + 3, x + 9, y + 9, to565(kRedstone));
    canvas_.drawLine(x + 10, y + 2, x + 1, y + 9, to565(kRedstone));
    canvas_.drawLine(x + 9, y + 2, x + 1, y + 8, to565(kRedstone));
    return;
  }
  int bars = 5;
  uint32_t col = kEmerald;
  if (pongMs < 0) {
    bars = 0;
    col = kTextMuted;
  } else if (pongMs >= 1000) {
    bars = 1;
    col = kRedstone;
  } else if (pongMs >= 600) {
    bars = 2;
    col = kOrange;
  } else if (pongMs >= 300) {
    bars = 3;
    col = kGold;
  } else if (pongMs >= 150) {
    bars = 4;
    col = kEmerald;
  }
  const int scanIdx = static_cast<int>((nowMs / 130) % 5);
  for (int i = 0; i < 5; ++i) {
    const int bh = 3 + (i * 7) / 4;
    const int bx = x + i * 3;
    const int by = y + 10 - bh;
    const bool lit = busy ? (i == scanIdx) : (i < bars);
    const uint32_t c = busy ? (lit ? kDiamond : 0x22303F) : (lit ? col : 0x243242);
    canvas_.fillRect(bx, by, 2, bh, to565(c));
  }
}

void MotdUi::drawWifiIcon(int x, int y, bool connected, uint32_t nowMs) {
  const int cx = x + 6;
  const int cy = y + 9;
  const uint32_t dotCol = connected ? kEmerald : kRedstone;
  const uint32_t arcCol = connected ? kTextPrimary : kRedstone;
  const float pulse = connected ? 1.0f : (0.35f + 0.55f * std::sin(nowMs * 0.006f));
  const uint16_t c1 = to565(mixRgb(kBgBase, arcCol, pulse));
  canvas_.drawArc(cx, cy, 3, 4, 225, 315, c1);
  canvas_.drawArc(cx, cy, 6, 7, 225, 315, c1);
  canvas_.fillCircle(cx, cy, 1, to565(dotCol));
}

void MotdUi::drawBatteryIcon(int x, int y, int pct, bool charging) {
  const uint32_t frameCol = charging ? kEmerald : kTextMuted;
  canvas_.drawRoundRect(x, y + 1, 15, 8, 2, to565(frameCol));
  canvas_.fillRect(x + 15, y + 3, 2, 4, to565(frameCol));
  if (pct >= 0) {
    const int clamped = std::min(100, std::max(0, pct));
    const int fw = std::max(1, (11 * clamped) / 100);
    const uint32_t fillCol = charging ? kEmerald : (clamped <= 20 ? kRedstone : kTextPrimary);
    canvas_.fillRect(x + 2, y + 3, fw, 4, to565(fillCol));
  }
  if (charging) {
    // Crisp mini lightning bolt cutout
    canvas_.fillTriangle(x + 8, y + 1, x + 5, y + 5, x + 8, y + 5, to565(kBgBase));
    canvas_.fillTriangle(x + 7, y + 5, x + 10, y + 5, x + 7, y + 9, to565(kBgBase));
  }
}

void MotdUi::drawTopBar(const UiModel& model, uint32_t nowMs) {
  const char* label = "WAIT";
  uint32_t col = kTextMuted;
  if (!model.configured) {
    label = "SETUP";
    col = kRedstone;
  } else if (!model.wifiConnected) {
    const bool trying = (nowMs - model.wifiAttemptAt < 15000);
    label = trying ? "WIFI..." : "OFFLINE";
    col = trying ? kGold : kRedstone;
  } else if (model.busy) {
    label = "PINGING";
    col = kDiamond;
  } else if (model.result && model.result->complete) {
    label = "ONLINE";
    col = kEmerald;
  } else if (model.result && model.result->statusReceived) {
    label = "PONG ERR";
    col = kRedstone;
  } else if (model.result) {
    label = "ERROR";
    col = kRedstone;
  }

  canvas_.setFont(metaFontPtr());
  const int statusPillW = std::max(54, static_cast<int>(canvas_.textWidth(label)) + 20);
  canvas_.fillSmoothRoundRect(6, 3, statusPillW, 12, 6, to565(kPillBg));
  canvas_.drawRoundRect(6, 3, statusPillW, 12, 6, to565(mixRgb(col, kCardBorder, 0.55f)));

  const float breath = 0.55f + 0.45f * std::sin(nowMs * 0.005f);
  canvas_.fillSmoothCircle(13, 9, 2, to565(mixRgb(kPillBg, col, breath)));

  canvas_.setTextDatum(lgfx::top_left);
  canvas_.setTextColor(to565(col));
  canvas_.drawString(label, 19, 4);

  // Top 2px scanning diamond shimmer when busy or Btn A pulse
  const bool pulsing = (nowMs < pulseUntilMs_);
  if (model.busy || pulsing) {
    const int sx = model.busy
                       ? (-44 + static_cast<int>(((nowMs % 700) * (kWidth + 44)) / 700))
                       : (kWidth - static_cast<int>(((pulseUntilMs_ - nowMs) * (kWidth + 44)) / 450));
    for (int i = 0; i < 44; ++i) {
      const int x = sx + i;
      if (x < 0 || x >= kWidth) continue;
      const float d = 1.0f - std::fabs(static_cast<float>(i - 22) / 22.0f);
      const uint32_t c = mixRgb(kBgBase, kDiamond, d * d);
      canvas_.fillRect(x, 0, 1, 2, to565(c));
    }
  }

  // Right telemetry: Wi-Fi icon + battery capsule + %
  drawWifiIcon(174, 3, model.configured && model.wifiConnected, nowMs);
  char pctStr[12];
  if (model.batteryPct >= 0) std::snprintf(pctStr, sizeof(pctStr), "%d%%", model.batteryPct);
  else std::snprintf(pctStr, sizeof(pctStr), "--");

  canvas_.setFont(metaFontPtr());
  const int pw = canvas_.textWidth(pctStr);
  canvas_.setTextColor(to565(model.charging ? kEmerald : kTextMuted));
  canvas_.drawString(pctStr, kWidth - 6 - pw, 4);
  drawBatteryIcon(kWidth - 6 - pw - 20, 4, model.batteryPct, model.charging);
}

int MotdUi::measureMotdLineWidth(const minecraft::Line& line) {
  canvas_.setFont(&::fonts::efontCN_14);
  int w = 0;
  for (const auto& span : line) {
    const int baseW = canvas_.textWidth(span.text.c_str());
    w += baseW + ((span.style & minecraft::Style::kBold) && baseW > 0 ? 1 : 0);
  }
  return w;
}

void MotdUi::applyHorizontalFadeMasks(int x, int y, int w, int h, uint32_t bgRgb, bool fadeLeft, bool fadeRight,
                                      int fadeWidth) {
  if (!fadeLeft && !fadeRight) return;
  const int kFade = std::min(fadeWidth, w / 2);
  if (kFade <= 1) return;
  const uint8_t bgR = (bgRgb >> 16) & 0xFF;
  const uint8_t bgG = (bgRgb >> 8) & 0xFF;
  const uint8_t bgB = bgRgb & 0xFF;
  for (int dx = 0; dx < kFade; ++dx) {
    const float alpha = 1.0f - (static_cast<float>(dx) / static_cast<float>(kFade));
    const int lx = x + dx;
    const int rx = x + w - 1 - dx;
    for (int py = y; py < y + h; ++py) {
      if (fadeLeft && lx >= 0 && lx < kWidth) {
        const auto p = canvas_.readPixelRGB(lx, py);
        if (p.r != bgR || p.g != bgG || p.b != bgB) {
          const uint32_t orig = (uint32_t(p.r) << 16) | (uint32_t(p.g) << 8) | p.b;
          canvas_.drawPixel(lx, py, to565(mixRgb(orig, bgRgb, alpha)));
        }
      }
      if (fadeRight && rx >= 0 && rx < kWidth && rx != lx) {
        const auto p = canvas_.readPixelRGB(rx, py);
        if (p.r != bgR || p.g != bgG || p.b != bgB) {
          const uint32_t orig = (uint32_t(p.r) << 16) | (uint32_t(p.g) << 8) | p.b;
          canvas_.drawPixel(rx, py, to565(mixRgb(orig, bgRgb, alpha)));
        }
      }
    }
  }
}

void MotdUi::drawMotdLine(const minecraft::Line& line, int xStart, int y, int clipLeft, int clipRight,
                          uint32_t elapsedMs, uint32_t obfSeed, bool& scrolledLeft, bool& clippedRight) {
  const int clipW = clipRight - clipLeft;
  const int totalW = measureMotdLineWidth(line);
  int offset = 0;
  if (totalW > clipW) {
    const int overflow = totalW - clipW + 6;
    const uint32_t move = static_cast<uint32_t>(overflow) * 32u;
    const uint32_t cyc = elapsedMs % (move * 2u + 2800u);
    if (cyc > 1400u && cyc <= 1400u + move) {
      offset = static_cast<int>((cyc - 1400u) / 32u);
    } else if (cyc > 1400u + move && cyc <= 2800u + move) {
      offset = overflow;
    } else if (cyc > 2800u + move) {
      offset = overflow - static_cast<int>((cyc - 2800u - move) / 32u);
    }
  }
  scrolledLeft = (offset > 0);
  clippedRight = (totalW - offset > clipW);

  canvas_.setFont(&::fonts::efontCN_14);
  canvas_.setTextDatum(lgfx::top_left);
  int x = xStart - offset;
  for (size_t si = 0; si < line.size(); ++si) {
    const auto& span = line[si];
    const std::string text = (span.style & minecraft::Style::kObfuscated)
                                 ? scrambleAscii(span.text, obfSeed + static_cast<uint32_t>(si * 13u))
                                 : span.text;
    const bool isBold = (span.style & minecraft::Style::kBold) != 0;
    const int w = canvas_.textWidth(text.c_str()) + (isBold && !text.empty() ? 1 : 0);
    if (x + w >= clipLeft && x <= clipRight) {
      const uint16_t shadowCol = to565(scaleRgb(span.color, 0.24f));
      const uint16_t fgCol = to565(span.color);
      canvas_.setTextColor(shadowCol);
      canvas_.drawString(text.c_str(), x + 1, y + 1);
      if (isBold) canvas_.drawString(text.c_str(), x + 2, y + 1);
      canvas_.setTextColor(fgCol);
      canvas_.drawString(text.c_str(), x, y);
      if (isBold) canvas_.drawString(text.c_str(), x + 1, y);

      if (span.style & minecraft::Style::kUnderlined) {
        canvas_.fillRect(x, y + 14, w, 1, fgCol);
      }
      if (span.style & minecraft::Style::kStrikethrough) {
        canvas_.fillRect(x, y + 7, w, 1, fgCol);
      }
    }
    x += w;
  }
}

void MotdUi::drawOfflineOrSetup(int ox, const UiModel& model, uint32_t nowMs) {
  const int cx = ox + 6;
  const int cy = 19;
  const int cw = kWidth - 12;
  const int ch = 99;
  const bool connecting = model.configured && !model.wifiConnected && (nowMs - model.wifiAttemptAt < 15000);
  const bool hasError = (!connecting && (!model.wifiConnected || (model.result && !model.result->statusReceived)));
  canvas_.fillSmoothRoundRect(cx, cy, cw, ch, 7, to565(kCardBg));
  canvas_.drawRoundRect(cx, cy, cw, ch, 7, to565(hasError ? mixRgb(kRedstone, kCardBorder, 0.5f) : kCardBorder));

  const int mx = ox + kWidth / 2;
  if (!model.configured) {
    // Simple USB-C badge + clear setup text
    canvas_.fillSmoothRoundRect(mx - 16, cy + 16, 32, 16, 5, to565(kPillBg));
    canvas_.drawRoundRect(mx - 16, cy + 16, 32, 16, 5, to565(kDiamond));
    canvas_.fillRect(mx - 8, cy + 22, 16, 4, to565(kDiamond));

    canvas_.setFont(titleFontPtr());
    const char* t1 = "Configure via Desktop App";
    canvas_.setTextColor(to565(kTextPrimary));
    canvas_.drawString(t1, mx - canvas_.textWidth(t1) / 2, cy + 44);

    canvas_.setFont(metaFontPtr());
    const char* t2 = "Connect USB-C to PC and run motd_host";
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString(t2, mx - canvas_.textWidth(t2) / 2, cy + 64);
    return;
  }

  if (connecting) {
    const float phase = static_cast<float>((nowMs - model.wifiAttemptAt) % 1400u) / 1400.0f;
    for (int i = 0; i < 3; ++i) {
      const float p = std::fmod(phase + i * 0.33f, 1.0f);
      const int rad = 6 + static_cast<int>(p * 20.0f);
      const uint32_t col = mixRgb(kDiamond, kCardBg, p);
      canvas_.drawArc(mx, 64, rad, rad + 1, 225, 315, to565(col));
    }
    canvas_.fillSmoothCircle(mx, 64, 2, to565(kDiamond));

    canvas_.setFont(titleFontPtr());
    const char* t1 = "Connecting to Wi-Fi...";
    canvas_.setTextColor(to565(kTextPrimary));
    canvas_.drawString(t1, mx - canvas_.textWidth(t1) / 2, 74);

    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kTextMuted));
    const int hw = canvas_.textWidth(model.host.c_str());
    canvas_.drawString(model.host.c_str(), mx - std::min(hw, cw - 20) / 2, 92);
    return;
  }

  if (!model.wifiConnected) {
    canvas_.fillSmoothRoundRect(cx + 10, cy + 12, 46, 15, 4, to565(mixRgb(kRedstone, kCardBg, 0.78f)));
    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kRedstone));
    canvas_.drawString("WIFI", cx + 21, cy + 15);

    canvas_.setFont(titleFontPtr());
    canvas_.setTextColor(to565(kTextPrimary));
    clipToStage(cx + 62, cy + 12, cw - 72, 18);
    canvas_.drawString(model.host.c_str(), cx + 62, cy + 13);
    restoreStageClip();

    canvas_.setFont(titleFontPtr());
    canvas_.setTextColor(to565(0xFCA5A5));
    canvas_.drawString("Wi-Fi connection failed", cx + 10, cy + 40);

    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString("Press [A] to reconnect & retry probe", cx + 10, cy + 70);
    return;
  }

  if (model.result && !model.result->statusReceived) {
    const char* stage = minecraft::stageName(model.result->stage);
    canvas_.fillSmoothRoundRect(cx + 10, cy + 12, 54, 15, 4, to565(mixRgb(kRedstone, kCardBg, 0.78f)));
    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kRedstone));
    const int sw = canvas_.textWidth(stage);
    canvas_.drawString(stage, cx + 10 + (54 - sw) / 2, cy + 15);

    canvas_.setFont(titleFontPtr());
    canvas_.setTextColor(to565(kTextPrimary));
    clipToStage(cx + 70, cy + 12, cw - 80, 18);
    canvas_.drawString(model.host.c_str(), cx + 70, cy + 13);
    restoreStageClip();

    canvas_.setFont(&::fonts::efontCN_12);
    canvas_.setTextColor(to565(0xFCA5A5));
    canvas_.setTextWrap(true);
    clipToStage(cx + 10, cy + 36, cw - 20, 42);
    canvas_.setCursor(cx + 10, cy + 36);
    canvas_.print(model.result->error.c_str());
    restoreStageClip();
    canvas_.setTextWrap(false);

    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString("Press [A] to retry server probe", cx + 10, cy + 80);
    return;
  }

  // Waiting for first query after Wi-Fi connect
  canvas_.setFont(titleFontPtr());
  const char* msg = model.busy ? "Querying server..." : "Waiting for query...";
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(msg, mx - canvas_.textWidth(msg) / 2, cy + 40);
}

void MotdUi::drawPage0(int ox, const UiModel& model, uint32_t nowMs) {
  if (!model.configured || !model.wifiConnected || !model.result || !model.result->statusReceived) {
    drawOfflineOrSetup(ox, model, nowMs);
    return;
  }

  const auto& res = *model.result;
  const int cx = ox + 6;
  const int cy = 19;
  const int cw = kWidth - 12;
  const int ch = 52;

  canvas_.fillSmoothRoundRect(cx, cy, cw, ch, 7, to565(kCardBg));
  // 2px ambient top glow derived from Favicon accent color
  for (int x = cx + 6; x < cx + cw - 6; ++x) {
    const float t = static_cast<float>(x - cx) / static_cast<float>(cw);
    const float glow = (t < 0.55f) ? (0.35f + 0.65f * (t / 0.55f)) : (1.0f - 0.72f * ((t - 0.55f) / 0.45f));
    canvas_.fillRect(x, cy, 1, 2, to565(mixRgb(kCardBg, accentColor_, glow)));
  }
  canvas_.drawRoundRect(cx, cy, cw, ch, 7, to565(mixRgb(accentColor_, kCardBorder, 0.75f)));

  // 44x44 Favicon (pushed with AA zoom during pop-in, direct sprite when settled)
  const int ix = cx + 5;
  const int iy = cy + 4;
  if (iconScale_.isMoving()) {
    const float s = iconScale_.value();
    iconCanvas_.pushRotateZoomWithAA(&canvas_,
                                     static_cast<float>(ix + kIconSize / 2),
                                     static_cast<float>(iy + kIconSize / 2),
                                     0.0f, s, s);
  } else {
    iconCanvas_.pushSprite(&canvas_, ix, iy);
  }

  // Right-side ping bars + latency number
  const int tx = ix + kIconSize + 7;
  const bool pongErr = !res.complete;
  char latNum[16];
  if (res.pongMs >= 0) std::snprintf(latNum, sizeof(latNum), "%ld", static_cast<long>(res.pongMs));
  else if (pongErr) std::snprintf(latNum, sizeof(latNum), "ERR");
  else std::snprintf(latNum, sizeof(latNum), "--");

  canvas_.setFont(numFontPtr());
  const int latW = canvas_.textWidth(latNum);
  canvas_.setFont(metaFontPtr());
  const int unitW = (res.pongMs >= 0) ? (canvas_.textWidth("ms") + 2) : 0;
  const int rightReserve = 16 + latW + unitW + 4;

  // Host marquee inside available width
  const int hostMaxW = std::max(32, (cx + cw - 6) - tx - rightReserve);
  canvas_.setFont(titleFontPtr());
  const int hostW = canvas_.textWidth(model.host.c_str());
  int hostOff = 0;
  if (hostW > hostMaxW) {
    const int ov = hostW - hostMaxW + 4;
    const uint32_t period = static_cast<uint32_t>(ov) * 45u + 2400u;
    const uint32_t t = (nowMs - animStartMs_) % (period * 2u);
    if (t > 1200u && t < period - 1200u) {
      hostOff = static_cast<int>(((t - 1200u) * static_cast<uint32_t>(ov)) / (period - 2400u));
    } else if (t >= period - 1200u && t < period + 1200u) {
      hostOff = ov;
    } else if (t >= period + 1200u) {
      hostOff = ov - static_cast<int>(((t - period - 1200u) * static_cast<uint32_t>(ov)) / (period - 2400u));
    }
  }
  clipToStage(tx, cy + 4, hostMaxW, 17);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(model.host.c_str(), tx - hostOff, cy + 5);
  applyHorizontalFadeMasks(tx, cy + 4, hostMaxW, 16, kCardBg, hostOff > 0, (hostW - hostOff) > hostMaxW, 6);
  restoreStageClip();

  // Draw MC 5-bar signal + latency digits + "ms"
  const int latX = cx + cw - 6 - unitW - latW;
  drawMcPingBars(latX - 17, cy + 7, res.pongMs, res.statusReceived, model.busy, nowMs);
  const uint32_t latCol = latencyColor(res.pongMs, res.statusReceived);
  canvas_.setFont(numFontPtr());
  canvas_.setTextColor(to565(latCol));
  canvas_.drawString(latNum, latX, cy + 3);
  if (res.pongMs >= 0) {
    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString("ms", latX + latW + 2, cy + 8);
  }

  // Row 2 inside Hero Card: Player occupancy pill + delta badge + version tag
  const int py = cy + 26;
  std::string pStr = "-- / --";
  if (res.online >= 0) {
    pStr = formatCommaNumber(res.online) + " / " + formatCommaNumber(res.maximum);
  }
  canvas_.setFont(metaFontPtr());
  const int pStrW = canvas_.textWidth(pStr.c_str());
  const int pillW = std::min(104, std::max(48, pStrW + 10));
  canvas_.fillSmoothRoundRect(tx, py, pillW, 19, 4, to565(kPillBg));
  canvas_.drawRoundRect(tx, py, pillW, 19, 4, to565(kCardBorder));
  clipToStage(tx + 3, py + 2, pillW - 6, 12);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(pStr.c_str(), tx + 5, py + 3);
  restoreStageClip();

  // Mini occupancy bar inside player pill
  const int trackW = pillW - 10;
  canvas_.fillRect(tx + 5, py + 15, trackW, 2, to565(kWellBg));
  if (res.online >= 0 && res.maximum > 0) {
    const int fillW = std::min(trackW, std::max(2, static_cast<int>((int64_t(res.online) * trackW) / res.maximum)));
    canvas_.fillRect(tx + 5, py + 15, fillW, 2, to565(accentColor_));
  }

  int nextX = tx + pillW + 4;
  // Show player join/leave delta badge for 10 seconds (if version still has reasonable room)
  canvas_.setFont(metaFontPtr());
  const int vw = canvas_.textWidth(res.version.c_str());
  if (model.playerDelta != 0 && (nowMs - model.playerDeltaAt) < 10000u) {
    char dStr[16];
    std::snprintf(dStr, sizeof(dStr), "%s%ld", model.playerDelta > 0 ? "+" : "", static_cast<long>(model.playerDelta));
    const uint32_t dCol = model.playerDelta > 0 ? kEmerald : kRedstone;
    const int dw = canvas_.textWidth(dStr) + 6;
    if (nextX + dw + 4 + std::min(vw, 44) <= cx + cw - 6) {
      canvas_.fillSmoothRoundRect(nextX, py + 3, dw, 13, 3, to565(mixRgb(dCol, kCardBg, 0.80f)));
      canvas_.setTextColor(to565(dCol));
      canvas_.drawString(dStr, nextX + 3, py + 4);
      nextX += dw + 4;
    }
  }

  // Right-aligned version tag inside remaining width
  const int verAvail = std::max(18, (cx + cw - 6) - nextX);
  clipToStage(nextX, py + 3, verAvail, 14);
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(res.version.c_str(), (cx + cw - 6) - std::min(vw, verAvail), py + 5);
  restoreStageClip();

  // Lower MOTD Obsidian Well (y: 74..118)
  const int wx = ox + 6;
  const int wy = 74;
  const int ww = kWidth - 12;
  const int wh = 44;
  canvas_.fillSmoothRoundRect(wx, wy, ww, wh, 6, to565(kWellBg));
  canvas_.drawRoundRect(wx, wy, ww, wh, 6, to565(kCardBorder));

  const auto& lines = res.lines;
  if (!lines.empty()) {
    const size_t numPages = (lines.size() + 1) / 2;
    if (motdPageIdx_ >= numPages) {
      motdPageIdx_ = 0;
      motdScrollY_.snap(0.0f);
    }
    if (numPages > 1 && nowMs - motdPageSwitchMs_ >= 6500u) {
      motdPageIdx_ = (motdPageIdx_ + 1) % numPages;
      motdScrollY_.start(motdScrollY_.value(), static_cast<float>(motdPageIdx_ * 38), nowMs, 240);
      motdPageSwitchMs_ = nowMs;
    }

    const int innerX = wx + 6;
    const int innerW = ww - 16;
    const uint32_t obfSeed = nowMs / 65u;
    const uint32_t elapsed = nowMs - animStartMs_;

    clipToStage(innerX, wy + 3, innerW, wh - 6);
    for (size_t li = 0; li < lines.size(); ++li) {
      const int ly = wy + 6 + static_cast<int>(li / 2) * 38 + static_cast<int>(li % 2) * 18 -
                     static_cast<int>(std::lround(motdScrollY_.value()));
      if (ly <= wy - 16 || ly >= wy + wh) continue;
      bool fadeL = false, fadeR = false;
      drawMotdLine(lines[li], innerX, ly, innerX, innerX + innerW, elapsed,
                   obfSeed + static_cast<uint32_t>(li * 17u), fadeL, fadeR);
      applyHorizontalFadeMasks(innerX, std::max(wy + 3, ly), innerW, 16, kWellBg, fadeL, fadeR, 10);
    }
    restoreStageClip();

    if (numPages > 1) {
      const int dotTop = wy + wh / 2 - static_cast<int>(numPages - 1) * 3;
      for (size_t p = 0; p < numPages; ++p) {
        const bool cur = (p == motdPageIdx_);
        canvas_.fillCircle(wx + ww - 5, dotTop + static_cast<int>(p) * 6, cur ? 2 : 1,
                           to565(cur ? kTextPrimary : 0x2A394A));
      }
    }
  }
}

void MotdUi::drawPage1(int ox, const UiModel& model, uint32_t nowMs) {
  const int cx = ox + 6;
  const int cw = kWidth - 12;

  // Top card: 40-point Sparkline (y: 19..67)
  const int ty = 19;
  const int th = 48;
  canvas_.fillSmoothRoundRect(cx, ty, cw, th, 7, to565(kCardBg));
  canvas_.drawRoundRect(cx, ty, cw, th, 7, to565(kCardBorder));

  canvas_.setFont(metaFontPtr());
  const size_t nSamples = model.history ? model.history->size() : 0;
  char titleBuf[32];
  std::snprintf(titleBuf, sizeof(titleBuf), "TREND (%zu)", nSamples);
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(titleBuf, cx + 8, ty + 4);

  const int32_t minP = model.history ? model.history->minPong() : -1;
  const int32_t maxP = model.history ? model.history->maxPong() : -1;
  const int32_t avgP = model.history ? model.history->avgPong() : -1;
  const int32_t curP = (model.result && model.result->pongMs >= 0) ? model.result->pongMs : -1;
  char statsBuf[48];
  if (minP >= 0) {
    if (curP >= 0) std::snprintf(statsBuf, sizeof(statsBuf), "min %ld  avg %ld  now %ldms",
                                 static_cast<long>(minP), static_cast<long>(avgP), static_cast<long>(curP));
    else std::snprintf(statsBuf, sizeof(statsBuf), "min %ld  avg %ld  now ERR",
                       static_cast<long>(minP), static_cast<long>(avgP));
  } else {
    std::snprintf(statsBuf, sizeof(statsBuf), "no samples yet");
  }
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(statsBuf, cx + cw - 8 - canvas_.textWidth(statsBuf), ty + 4);

  if (nSamples >= 1) {
    const int px = cx + 8;
    const int py = ty + 18;
    const int pw = cw - 16;
    const int ph = 24;
    const int32_t lo = (minP >= 0) ? std::max<int32_t>(0, minP - 5) : 0;
    const int32_t hi = (maxP >= 0) ? std::max<int32_t>(lo + 10, maxP + 5) : 100;

    bool hasPrev = false;
    float prevX = 0, prevY = 0;
    const bool degraded = model.result && model.result->statusReceived && !model.result->complete;
    const uint32_t lineRgb = degraded ? kGold : kEmerald;

    for (size_t i = 0; i < nSamples; ++i) {
      const auto& s = model.history->at(i);
      const float x = (nSamples == 1)
                          ? static_cast<float>(px + pw)
                          : static_cast<float>(px) + (static_cast<float>(i) / static_cast<float>(nSamples - 1)) * pw;
      if (!s.complete || s.pongMs < 0) {
        // Dashed redstone marker for failed probe
        for (int dy = 0; dy < ph; dy += 4) {
          canvas_.fillRect(static_cast<int>(x), py + dy, 1, 2, to565(mixRgb(kRedstone, kCardBg, 0.35f)));
        }
        hasPrev = false;
        continue;
      }
      const float norm = static_cast<float>(s.pongMs - lo) / static_cast<float>(hi - lo);
      const float y = static_cast<float>(py + ph) - std::min(1.0f, std::max(0.0f, norm)) * static_cast<float>(ph);
      if (hasPrev) {
        // Soft vertical gradient fill under segment
        const int x0 = static_cast<int>(std::floor(prevX));
        const int x1 = static_cast<int>(std::ceil(x));
        for (int sx = x0; sx <= x1; ++sx) {
          const float u = (x1 == x0) ? 0.0f : static_cast<float>(sx - x0) / static_cast<float>(x1 - x0);
          const int topY = static_cast<int>(prevY + (y - prevY) * u);
          for (int sy = topY; sy <= py + ph; ++sy) {
            const float f = static_cast<float>(sy - topY) / static_cast<float>(std::max(1, (py + ph) - topY));
            canvas_.drawPixel(sx, sy, to565(mixRgb(lineRgb, kCardBg, 0.68f + 0.32f * f)));
          }
        }
        canvas_.drawWideLine(static_cast<int32_t>(std::lround(prevX)),
                             static_cast<int32_t>(std::lround(prevY)),
                             static_cast<int32_t>(std::lround(x)),
                             static_cast<int32_t>(std::lround(y)),
                             0.85f, to565(lineRgb));
      }
      prevX = x;
      prevY = y;
      hasPrev = true;
    }
    if (hasPrev) {
      canvas_.fillSmoothCircle(static_cast<int>(prevX), static_cast<int>(prevY), 2, to565(kTextPrimary));
    }
  }

  // Bottom card: Player Sample or Uptime fallback (y: 70..118)
  const int by = 70;
  const int bh = 48;
  canvas_.fillSmoothRoundRect(cx, by, cw, bh, 7, to565(kCardBg));
  canvas_.drawRoundRect(cx, by, cw, bh, 7, to565(kCardBorder));

  canvas_.setFont(metaFontPtr());
  char pHead[32];
  if (model.result && model.result->online >= 0) {
    std::snprintf(pHead, sizeof(pHead), "ONLINE SAMPLE (%ld)", static_cast<long>(model.result->online));
  } else {
    std::snprintf(pHead, sizeof(pHead), "PLAYER TELEMETRY");
  }
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(pHead, cx + 8, by + 4);

  const float uptime = model.history ? model.history->uptimePercent() : 0.0f;
  char upBuf[24];
  std::snprintf(upBuf, sizeof(upBuf), "Uptime %.1f%%", uptime);
  canvas_.setTextColor(to565(uptime >= 95.0f ? kEmerald : (uptime >= 75.0f ? kGold : kRedstone)));
  canvas_.drawString(upBuf, cx + cw - 8 - canvas_.textWidth(upBuf), by + 4);

  if (model.result && !model.result->playerSample.empty()) {
    const auto& players = model.result->playerSample;
    // Measure row wrap and paginate slowly if >2 rows
    struct PillItem { int x; int y; int w; size_t idx; };
    std::vector<PillItem> layout;
    int curX = 0;
    int curRow = 0;
    for (size_t i = 0; i < players.size(); ++i) {
      const int pw2 = std::min<int>(cw - 16, static_cast<int>(canvas_.textWidth(players[i].c_str())) + 10);
      if (curX > 0 && curX + pw2 > cw - 16) {
        curX = 0;
        ++curRow;
      }
      layout.push_back({curX, curRow * 14, pw2, i});
      curX += pw2 + 4;
    }
    const int totalRows = curRow + 1;
    const int rowPages = (totalRows + 1) / 2;
    const int activeRowPage = (rowPages > 1) ? static_cast<int>((nowMs / 4500u) % static_cast<uint32_t>(rowPages)) : 0;
    const int yShift = activeRowPage * 28;

    clipToStage(cx + 6, by + 17, cw - 12, 28);
    for (const auto& item : layout) {
      const int py2 = by + 18 + item.y - yShift;
      if (py2 < by + 10 || py2 > by + 44) continue;
      const int px2 = cx + 8 + item.x;
      canvas_.fillSmoothRoundRect(px2, py2, item.w, 12, 3, to565(kPillBg));
      canvas_.drawRoundRect(px2, py2, item.w, 12, 3, to565(kCardBorder));
      canvas_.setTextColor(to565(kTextPrimary));
      canvas_.drawString(players[item.idx].c_str(), px2 + 5, py2 + 1);
    }
    restoreStageClip();
  } else {
    canvas_.setFont(titleFontPtr());
    canvas_.setTextColor(to565(kTextMuted));
    canvas_.drawString("Sample list hidden by server", cx + 8, by + 23);
  }
}

void MotdUi::drawPage2(int ox, const UiModel& model) {
  const int cx = ox + 6;
  const int cw = kWidth - 12;

  // Top bar card: target IP + protocol + stacked horizontal waterfall (y: 19..47)
  const int ty = 19;
  const int th = 28;
  canvas_.fillSmoothRoundRect(cx, ty, cw, th, 6, to565(kCardBg));
  canvas_.drawRoundRect(cx, ty, cw, th, 6, to565(kCardBorder));

  canvas_.setFont(metaFontPtr());
  std::string targetStr = "--";
  if (model.result && !model.result->address.empty()) {
    targetStr = model.result->address + ":" + std::to_string(model.port);
  } else if (!model.host.empty()) {
    targetStr = model.host + ":" + std::to_string(model.port);
  }
  clipToStage(cx + 6, ty + 3, 142, 12);
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(targetStr.c_str(), cx + 7, ty + 4);
  restoreStageClip();

  char protoBuf[24];
  const int32_t proto = (model.result && model.result->protocol >= 0) ? model.result->protocol : model.protocol;
  std::snprintf(protoBuf, sizeof(protoBuf), "PROTO v%ld", static_cast<long>(proto));
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(protoBuf, cx + cw - 7 - canvas_.textWidth(protoBuf), ty + 4);

  struct StageInfo {
    const char* name;
    int32_t ms;
    uint32_t col;
    minecraft::Stage stage;
  };
  const minecraft::Result* r = model.result;
  const StageInfo stages[4] = {
      {"DNS", r ? r->dnsMs : -1, kViolet, minecraft::Stage::Dns},
      {"TCP", r ? r->tcpMs : -1, kBlue, minecraft::Stage::Tcp},
      {"STATUS", r ? r->statusMs : -1, kGold, minecraft::Stage::Status},
      {"PING/PONG", r ? r->pongMs : -1, kEmerald, minecraft::Stage::Pong},
  };

  int32_t total = 0;
  for (const auto& s : stages) {
    if (s.ms > 0) total += s.ms;
  }
  const int barX = cx + 7;
  const int barY = ty + 17;
  const int barW = cw - 46;
  const int barH = 6;
  canvas_.fillSmoothRoundRect(barX, barY, barW, barH, 3, to565(kWellBg));
  if (total > 0) {
    const int revW = static_cast<int>(std::lround(barW * waterfallReveal_.value()));
    clipToStage(barX, barY, std::max(1, revW), barH);
    int sx = barX;
    for (const auto& s : stages) {
      if (s.ms <= 0) continue;
      const int segW = std::max(3, static_cast<int>((int64_t(s.ms) * barW) / total));
      canvas_.fillRect(sx, barY, std::max(1, segW - 1), barH, to565(s.col));
      sx += segW;
    }
    restoreStageClip();
  }
  char totBuf[16];
  if (total > 0) std::snprintf(totBuf, sizeof(totBuf), "%ldms", static_cast<long>(total));
  else std::snprintf(totBuf, sizeof(totBuf), "--");
  canvas_.setTextColor(to565(kTextPrimary));
  canvas_.drawString(totBuf, cx + cw - 7 - canvas_.textWidth(totBuf), ty + 15);

  // 2x2 Diagnostic tiles (y: 50..118)
  const int tw = (cw - 5) / 2;
  const int th2 = 32;
  for (int idx = 0; idx < 4; ++idx) {
    const int colIdx = idx % 2;
    const int rowIdx = idx / 2;
    const int tx = cx + colIdx * (tw + 5);
    const int ty2 = 50 + rowIdx * (th2 + 5);
    const auto& s = stages[idx];
    const bool failed = (r != nullptr) && !r->complete && r->stage == s.stage;
    const uint32_t accent = failed ? kRedstone : s.col;

    canvas_.fillSmoothRoundRect(tx, ty2, tw, th2, 6, to565(kCardBg));
    canvas_.drawRoundRect(tx, ty2, tw, th2, 6, to565(failed ? kRedstone : kCardBorder));
    canvas_.fillSmoothRoundRect(tx + 4, ty2 + 6, 3, th2 - 12, 1, to565(accent));

    canvas_.setFont(metaFontPtr());
    canvas_.setTextColor(to565(failed ? kRedstone : kTextMuted));
    canvas_.drawString(s.name, tx + 11, ty2 + 4);

    canvas_.setFont(numFontPtr());
    char valBuf[20];
    if (failed) std::snprintf(valBuf, sizeof(valBuf), "FAIL");
    else if (s.ms >= 0) std::snprintf(valBuf, sizeof(valBuf), "%ld ms", static_cast<long>(s.ms));
    else std::snprintf(valBuf, sizeof(valBuf), "--");
    canvas_.setTextColor(to565(failed ? kRedstone : (s.ms >= 0 ? kTextPrimary : kTextMuted)));
    canvas_.drawString(valBuf, tx + 11, ty2 + 14);
  }
}

void MotdUi::drawFooter(const UiModel& model, uint32_t nowMs) {
  const int y = 122;
  canvas_.fillSmoothRoundRect(6, y, 42, 10, 3, to565(kPillBg));
  canvas_.setFont(metaFontPtr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString("A Ping", 11, y + 1);

  // Morphing 3-page indicator pills
  int widths[kPageCount];
  int totalW = (kPageCount - 1) * 4;
  for (int i = 0; i < kPageCount; ++i) {
    const float active = std::max(0.0f, 1.0f - std::fabs(pageScroll_.value() - static_cast<float>(i)));
    widths[i] = 4 + static_cast<int>(std::lround(active * 10.0f));
    totalW += widths[i];
  }
  int px = (kWidth - totalW) / 2;
  for (int i = 0; i < kPageCount; ++i) {
    const float active = std::max(0.0f, 1.0f - std::fabs(pageScroll_.value() - static_cast<float>(i)));
    const uint32_t c = mixRgb(0x2A394A, kTextPrimary, active);
    canvas_.fillSmoothRoundRect(px, y + 3, widths[i], 4, 2, to565(c));
    px += widths[i] + 4;
  }

  char rightBuf[24];
  if (model.busy) {
    std::snprintf(rightBuf, sizeof(rightBuf), "Querying");
  } else if (model.result) {
    const unsigned long ageS = static_cast<unsigned long>((nowMs - model.finishedAt) / 1000u);
    std::snprintf(rightBuf, sizeof(rightBuf), "%lus ago  [B]", ageS);
  } else {
    std::snprintf(rightBuf, sizeof(rightBuf), "%u deg  [B]", static_cast<unsigned>(model.rotationDeg));
  }
  canvas_.setFont(metaFontPtr());
  canvas_.setTextColor(to565(kTextMuted));
  canvas_.drawString(rightBuf, kWidth - 6 - canvas_.textWidth(rightBuf), y + 1);

  // 2px bottom auto-refresh countdown bar
  float prog = 0.0f;
  if (model.busy) {
    prog = 1.0f;
  } else if (model.configured && model.wifiConnected && model.refreshMs > 0) {
    const uint32_t elapsed = nowMs - model.finishedAt;
    prog = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(model.refreshMs));
  }
  canvas_.fillRect(0, kHeight - 2, kWidth, 2, to565(kCardBg));
  const int pw = static_cast<int>(std::lround(static_cast<float>(kWidth) * prog));
  if (pw > 0) {
    canvas_.fillRect(0, kHeight - 2, pw, 2, to565(model.busy ? kDiamond : mixRgb(accentColor_, kCardBg, 0.35f)));
  }
}

void MotdUi::drawHoldOverlay(float progress) {
  if (progress <= 0.01f) return;
  const float p = std::min(1.0f, progress);
  const int bx = kWidth / 2 - 48;
  const int by = kHeight / 2 - 38;
  canvas_.fillSmoothRoundRect(bx, by, 96, 76, 12, to565(kBgBase));
  canvas_.drawRoundRect(bx, by, 96, 76, 12, to565(kCardBorder));

  const int cx = kWidth / 2;
  const int cy = kHeight / 2;
  canvas_.fillArc(cx, cy, 26, 30, 0, 360, to565(kCardBorder));
  const float endDeg = 270.0f + 360.0f * p;
  canvas_.fillArc(cx, cy, 26, 30, 270.0f, endDeg, to565(kDiamond));

  canvas_.setFont(metaFontPtr());
  canvas_.setTextColor(to565(kTextPrimary));
  const char* lbl = "FLIP 180";
  canvas_.drawString(lbl, cx - canvas_.textWidth(lbl) / 2, cy - 5);
}

void MotdUi::render(const UiModel& model, uint32_t nowMs, bool pushToTarget) {
  lastStepMs_ = nowMs;

  updateIconCache(model);
  pageScroll_.update(nowMs);
  motdScrollY_.update(nowMs);
  iconScale_.update(nowMs);
  waterfallReveal_.update(nowMs);

  canvas_.fillSprite(to565(kBgBase));
  drawTopBar(model, nowMs);

  canvas_.setClipRect(0, 18, kWidth, 102);
  const float baseOff = -pageScroll_.value() * static_cast<float>(kWidth);
  for (int p = 0; p < kPageCount; ++p) {
    const int ox = static_cast<int>(std::lround(baseOff + static_cast<float>(p * kWidth)));
    if (ox <= -kWidth || ox >= kWidth) continue;
    if (p == 0) drawPage0(ox, model, nowMs);
    else if (p == 1) drawPage1(ox, model, nowMs);
    else drawPage2(ox, model);
  }
  canvas_.clearClipRect();

  drawFooter(model, nowMs);
  drawHoldOverlay(model.holdBProgress);

  if (pushToTarget && target_) {
    canvas_.pushSprite(target_, 0, 0);
  }
}

uint32_t MotdUi::recommendedFrameIntervalMs(const UiModel& model, uint32_t nowMs) const {
  if (model.busy || nowMs < pulseUntilMs_ || model.holdBProgress > 0.01f) return 25;
  if (model.configured && !model.wifiConnected && (nowMs - model.wifiAttemptAt < 15000u)) return 25;
  if (pageScroll_.isMoving() || motdScrollY_.isMoving() || iconScale_.isMoving() || waterfallReveal_.isMoving()) {
    return 25;
  }
  if (page_ == 0 && model.result && model.result->statusReceived) {
    const auto& res = *model.result;
    if (res.lines.size() > 2) return 25;
    for (const auto& line : res.lines) {
      int w = 0;
      for (const auto& span : line) {
        if (span.style & minecraft::Style::kObfuscated) return 25;
        w += static_cast<int>(span.text.size()) * 7;
      }
      if (w > 210) return 25;
    }
    if (model.host.size() > 16) return 25;
  }
  if (page_ == 1 && model.result && model.result->playerSample.size() > 6) return 100;
  return 500;
}

}  // namespace motd
