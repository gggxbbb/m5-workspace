// alert.h — 告警音播放器（移植自 rwr-alert 的敌跟踪/敌导弹提示音）
// 非阻塞实现：playAlert 触发，主循环每轮调 alertTick() 驱动，不建 task、不阻塞。
// 两种模式：
//   AL_LOCK  一次性：敌跟踪 高-低 双音（554.37Hz 215ms + 370Hz 215ms）×2 轮，播完自停
//   AL_MSL   持续：敌导弹 高-低 急促双音（554.37Hz 83ms + 392Hz 83ms）×3 轮/组，
//            循环播放（组间短静音），直到外部调 stopAlert()（如检测到余额不再减少）
#pragma once

#include <M5Unified.h>

enum AlertKind {
  AL_NONE,
  AL_LOCK,
  AL_MSL,
};

static const uint16_t LOCK_SEQ[][2] = { {554, 215}, {370, 215} };
static const uint16_t MSL_SEQ[][2]  = { {554, 83},  {392, 83} };
#define LOCK_LOOPS 2
#define MSL_LOOPS 3
#define ALERT_GAP_MS 150   // 轮/组之间的静音间隔

static AlertKind alertKind = AL_NONE;
static int alertStep = 0;
static int alertLoop = 0;
static uint32_t alertNextT = 0;

// 触发告警音（若正在播放则从头开始）
static void playAlert(AlertKind k) {
  if (k == AL_NONE) return;
  alertKind = k;
  alertStep = 0;
  alertLoop = 0;
  alertNextT = 0;
  M5.Speaker.stop();
}

// 立即停止（持续模式用：余额不再减少/离开高峰/开关关闭）
static void stopAlert() {
  alertKind = AL_NONE;
  M5.Speaker.stop();
}

// 每轮调用（主循环）
static void alertTick() {
  if (alertKind == AL_NONE) return;
  uint32_t now = millis();
  if (now < alertNextT) return;

  const uint16_t (*seq)[2];
  int loops;
  if (alertKind == AL_LOCK) { seq = LOCK_SEQ; loops = LOCK_LOOPS; }
  else                      { seq = MSL_SEQ;  loops = MSL_LOOPS; }

  if (alertStep < 2) {
    M5.Speaker.tone(seq[alertStep][0]);
    alertNextT = now + seq[alertStep][1];
    alertStep++;
  } else {
    M5.Speaker.stop();
    alertLoop++;
    if (alertKind == AL_LOCK && alertLoop >= loops) {
      alertKind = AL_NONE;   // 一次性播完自停
      return;
    }
    alertStep = 0;
    alertNextT = now + ALERT_GAP_MS;  // MSL 持续：短静音后下一组
  }
}
