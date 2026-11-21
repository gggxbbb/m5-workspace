#pragma once

#include <M5GFX.h>
#include <MinecraftMotd.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "MotdAnim.h"

namespace motd {

struct TelemetrySample {
  int32_t pongMs = -1;
  int32_t online = -1;
  bool complete = false;
  uint32_t atMs = 0;
};

class TelemetryHistory {
 public:
  static constexpr size_t kCapacity = 40;

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  void push(const TelemetrySample& sample) {
    const size_t idx = (head_ + count_) % kCapacity;
    if (count_ < kCapacity) {
      samples_[idx] = sample;
      ++count_;
    } else {
      samples_[head_] = sample;
      head_ = (head_ + 1) % kCapacity;
    }
  }

  size_t size() const { return count_; }

  const TelemetrySample& at(size_t i) const {
    return samples_[(head_ + i) % kCapacity];
  }

  float uptimePercent() const {
    if (count_ == 0) return 0.0f;
    size_t ok = 0;
    for (size_t i = 0; i < count_; ++i) {
      if (at(i).complete) ++ok;
    }
    return 100.0f * static_cast<float>(ok) / static_cast<float>(count_);
  }

  int32_t minPong() const {
    int32_t best = -1;
    for (size_t i = 0; i < count_; ++i) {
      const auto& s = at(i);
      if (s.complete && s.pongMs >= 0 && (best < 0 || s.pongMs < best)) best = s.pongMs;
    }
    return best;
  }

  int32_t maxPong() const {
    int32_t best = -1;
    for (size_t i = 0; i < count_; ++i) {
      const auto& s = at(i);
      if (s.complete && s.pongMs >= 0 && s.pongMs > best) best = s.pongMs;
    }
    return best;
  }

  int32_t avgPong() const {
    int64_t sum = 0;
    size_t valid = 0;
    for (size_t i = 0; i < count_; ++i) {
      const auto& s = at(i);
      if (s.complete && s.pongMs >= 0) {
        sum += s.pongMs;
        ++valid;
      }
    }
    return valid ? static_cast<int32_t>(sum / static_cast<int64_t>(valid)) : -1;
  }

 private:
  std::array<TelemetrySample, kCapacity> samples_{};
  size_t head_ = 0;
  size_t count_ = 0;
};

struct UiModel {
  bool configured = false;
  bool wifiConnected = false;
  bool busy = false;
  bool charging = false;
  int batteryPct = -1;
  uint16_t rotationDeg = 90;
  uint32_t wifiAttemptAt = 0;
  uint32_t finishedAt = 0;
  uint32_t refreshMs = 30000;
  float holdBProgress = 0.0f;  // 0..1 while long-pressing B
  std::string host;
  uint16_t port = 25565;
  int32_t protocol = 47;
  const minecraft::Result* result = nullptr;
  uint32_t resultRevision = 0;
  int32_t playerDelta = 0;
  uint32_t playerDeltaAt = 0;
  const TelemetryHistory* history = nullptr;
};

class MotdUi {
 public:
  static constexpr int kWidth = 240;
  static constexpr int kHeight = 135;
  static constexpr int kPageCount = 3;

  bool begin(lgfx::LovyanGFX* target, bool usePsram = true);

  void setPage(int page, uint32_t nowMs);
  void nextPage(uint32_t nowMs);
  int page() const { return page_; }

  void triggerPulse(uint32_t nowMs);
  void resetMotdCycle(uint32_t nowMs);

  void render(const UiModel& model, uint32_t nowMs, bool pushToTarget = true);
  uint32_t recommendedFrameIntervalMs(const UiModel& model, uint32_t nowMs) const;

  const lgfx::LGFX_Sprite& canvas() const { return canvas_; }
  uint32_t accentColor() const { return accentColor_; }

 private:
  void updateIconCache(const UiModel& model);
  void drawGrassBlockIcon();
  void extractAccentFromIcon();

  void drawTopBar(const UiModel& model, uint32_t nowMs);
  void drawFooter(const UiModel& model, uint32_t nowMs);
  void drawHoldOverlay(float progress);

  void drawPage0(int ox, const UiModel& model, uint32_t nowMs);
  void drawPage1(int ox, const UiModel& model, uint32_t nowMs);
  void drawPage2(int ox, const UiModel& model);
  void drawOfflineOrSetup(int ox, const UiModel& model, uint32_t nowMs);

  void clipToStage(int x, int y, int w, int h);
  void restoreStageClip();

  void drawMcPingBars(int x, int y, int32_t pongMs, bool statusOk, bool busy, uint32_t nowMs);
  void drawWifiIcon(int x, int y, bool connected, uint32_t nowMs);
  void drawBatteryIcon(int x, int y, int pct, bool charging);

  int measureMotdLineWidth(const minecraft::Line& line);
  void drawMotdLine(const minecraft::Line& line, int xStart, int y, int clipLeft, int clipRight,
                    uint32_t elapsedMs, uint32_t obfSeed, bool& scrolledLeft, bool& clippedRight);
  void applyHorizontalFadeMasks(int x, int y, int w, int h, uint32_t bgRgb, bool fadeLeft, bool fadeRight,
                                int fadeWidth = 10);

  const lgfx::IFont* metaFontPtr() const;
  const lgfx::IFont* titleFontPtr() const;
  const lgfx::IFont* numFontPtr() const;

  lgfx::LovyanGFX* target_ = nullptr;
  lgfx::LGFX_Sprite canvas_;
  lgfx::LGFX_Sprite iconCanvas_;

  lgfx::PointerWrapper metaWrap_;
  lgfx::PointerWrapper titleWrap_;
  lgfx::PointerWrapper numWrap_;
  lgfx::BFFfont metaFont_;
  lgfx::BFFfont titleFont_;
  lgfx::BFFfont numFont_;
  bool metaLoaded_ = false;
  bool titleLoaded_ = false;
  bool numLoaded_ = false;

  int page_ = 0;
  uint32_t lastStepMs_ = 0;
  uint32_t animStartMs_ = 0;
  uint32_t pulseUntilMs_ = 0;
  uint32_t cachedRevision_ = 0xFFFFFFFFu;
  uint32_t accentColor_ = 0x4ADE80;

  size_t motdPageIdx_ = 0;
  uint32_t motdPageSwitchMs_ = 0;

  SpringValue pageScroll_{0.0f, 0.28f, 0.15f};
  SpringValue motdScrollY_{0.0f, 0.34f, 0.10f};
  SpringValue iconScale_{1.0f, 0.30f, 0.28f};
  EasingValue waterfallReveal_{1.0f, 320};
};

}  // namespace motd
