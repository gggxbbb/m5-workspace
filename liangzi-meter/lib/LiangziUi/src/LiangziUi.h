#pragma once

#include <M5GFX.h>
#include <cstdint>
#include <string>
#include "LiangziAnim.h"
#include "LiangziModel.h"

namespace liangzi {

class LiangziUi {
 public:
  static constexpr int kPageCount = 3;

  bool begin(lgfx::LovyanGFX* target, int width, int height, bool usePsram = true);
  bool onResize(int width, int height);

  void setPage(int page, uint32_t nowMs);
  void nextPage(uint32_t nowMs);
  int page() const { return page_; }

  void triggerPulse(uint32_t nowMs);
  void showToast(const char* text, uint32_t nowMs, uint32_t durationMs = 1500);

  void render(const UiModel& model, uint32_t nowMs, bool pushToTarget = true);
  uint32_t recommendedFrameIntervalMs(const UiModel& model, uint32_t nowMs) const;

  const lgfx::LGFX_Sprite& canvas() const { return canvas_; }
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  bool allocateCanvas(int width, int height);
  bool isPortrait() const { return height_ > width_; }
  uint64_t computeFingerprint(const UiModel& model, uint32_t nowMs) const;

  void clipToStage(int x, int y, int w, int h, int stageTop, int stageBottom);
  void restoreStageClip(int stageTop, int stageBottom);

  void drawTopBar(const UiModel& model, uint32_t nowMs);
  void drawFooter(const UiModel& model, uint32_t nowMs);
  void drawHoldOverlay(uint8_t curRotation, float progress);
  void drawToastOverlay(uint32_t nowMs);

  void drawWifiIcon(int x, int y, bool connected, int rssi, uint32_t nowMs);
  int drawBatteryPill(int xRight, int y, int pct, bool charging);
  void drawTopAccentCard(int x, int y, int w, int h, uint32_t accent, uint32_t border, bool glow);
  void drawTimeline24h(int x, int y, int w, int h, const UiModel& model, bool showTicks);
  void drawSplitBar(int x, int y, int w, int h, const UiModel& model, float reveal);
  void drawSparkline(int x, int y, int w, int h, const UiModel& model, bool showMinMax);
  void drawSetupCard(int x, int y, int w, int h, uint32_t nowMs);
  void drawTruncatedRight(const char* text, int leftBound, int rightX, int y, uint32_t color,
                          const lgfx::IFont* font);

  void drawPortraitPage0(int ox, const UiModel& model, uint32_t nowMs);
  void drawPortraitPage1(int ox, const UiModel& model);
  void drawPortraitPage2(int ox, const UiModel& model);

  void drawLandscapePage0(int ox, const UiModel& model, uint32_t nowMs);
  void drawLandscapePage1(int ox, const UiModel& model);
  void drawLandscapePage2(int ox, const UiModel& model);

  const lgfx::IFont* hero36Ptr() const;
  const lgfx::IFont* hero30Ptr() const;
  const lgfx::IFont* cn12Ptr() const;
  const lgfx::IFont* meta10Ptr() const;
  const lgfx::IFont* num16Ptr() const;
  const lgfx::IFont* num22Ptr() const;

  lgfx::LovyanGFX* target_ = nullptr;
  lgfx::LGFX_Sprite canvas_;
  bool usePsram_ = true;
  int width_ = 135;
  int height_ = 240;

  lgfx::PointerWrapper hero36Wrap_;
  lgfx::PointerWrapper hero30Wrap_;
  lgfx::PointerWrapper cn12Wrap_;
  lgfx::PointerWrapper meta10Wrap_;
  lgfx::PointerWrapper num16Wrap_;
  lgfx::PointerWrapper num22Wrap_;

  lgfx::BFFfont hero36Font_;
  lgfx::BFFfont hero30Font_;
  lgfx::BFFfont cn12Font_;
  lgfx::BFFfont meta10Font_;
  lgfx::BFFfont num16Font_;
  lgfx::BFFfont num22Font_;

  bool hero36Loaded_ = false;
  bool hero30Loaded_ = false;
  bool cn12Loaded_ = false;
  bool meta10Loaded_ = false;
  bool num16Loaded_ = false;
  bool num22Loaded_ = false;

  int page_ = 0;
  uint32_t lastStepMs_ = 0;
  uint32_t pulseUntilMs_ = 0;
  uint32_t toastUntilMs_ = 0;
  std::string toastText_;
  uint32_t lastRevision_ = 0xFFFFFFFFu;
  PhaseKind lastPhase_ = PhaseKind::kUnsynced;
  uint64_t lastIdleHash_ = 0;
  bool forceDirty_ = true;

  EasingValue pageScroll_{0.0f, 200};
  EasingValue splitReveal_{1.0f, 320};
};

}  // namespace liangzi
