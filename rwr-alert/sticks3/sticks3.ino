// sticks3.ino — War Thunder RWR 复刻(M5StickS3 移植版)
// 上游: https://github.com/142428525/esp32-rwr-alert (ESP32 + SH1106 128x64 + U8g2)
// 移植差异:
//   屏幕   SH1106 128x64 (U8g2)      -> ST7789 240x135 横屏 (M5GFX 双缓冲)
//   输入   电容触摸 GPIO4/GPIO13      -> KEY1(G11)/KEY2(G12) 物理按键
//   发声   GPIO14 + LEDC 直驱蜂鸣器   -> ES8311 板载扬声器 (M5.Speaker)
//   随机源 analogRead(RANDSEED_PIN)   -> esp_random()
// 交互(保持上游语义):
//   KEY1 按住 = 敌跟踪(LOCK);KEY2 按住 = 敌导弹(MSL);KEY1+KEY2 同按 = 敌导弹
//   空闲时 1% 概率随机触发雷达告警(RADAR,单发提示音)

#include <M5Unified.h>
#include "consts.h"

enum BuzType {
  BUZ_NONE,
  BUZ_RADAR,
  BUZ_LOCK,
  BUZ_MSL
};

// ---- 雷达布局:上游圆心 (96,31) r29 -> 新圆心 (150,67) r55,斜线坐标缩放 55/29 ----
static constexpr int CX = 150, CY = 67, R = 55, R_INNER = 8;
static constexpr int R_ALERT_IN = R - 12;          // 告警粗环内径(环宽 12)
static constexpr int16_t SCALE_NUM = 19, SCALE_DEN = 10; // 坐标缩放系数 1.9
static constexpr int OCX = 96, OCY = 31;           // 上游圆心
static constexpr uint16_t CLR_DIM = 0x4A69;        // 暗灰提示

M5Canvas canvas(&M5.Display);

bool blink_flag = false;
int8_t blink_cnt = 0;
BuzType buz_ty = BUZ_NONE;
TaskHandle_t buz_task = NULL;

// 换算后的斜线坐标
int16_t SLSH_X[SLSH_POSLEN], SLSH_Y[SLSH_POSLEN];
int16_t SRSH_X[SLSH_POSLEN], SRSH_Y[SLSH_POSLEN];

void do_buz(void*);
void stop_buz();
void beep(double, uint32_t);

// 触发告警:先设类型再建任务,任务启动即读到正确类型
void trigger(BuzType ty) {
  buz_ty = ty;
  blink_flag = true;
  blink_cnt = 0;
  if (buz_task == NULL) {
    xTaskCreate(do_buz, "do_buz", 4096, NULL, 1, &buz_task);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);       // 240x135 横屏
  M5.Display.setBrightness(160);
  M5.Speaker.setVolume(96);        // 电池供电安全音量(<75%)

  canvas.setPsram(true);           // StickS3 8MB PSRAM
  canvas.createSprite(M5.Display.width(), M5.Display.height());

  randomSeed(esp_random());

  // 斜线坐标按新圆心换算(保留上游 consts.h 原始数据)
  for (uint8_t i = 0; i < SLSH_POSLEN; i++) {
    SLSH_X[i] = CX + (int16_t)(SLSH_LX[i] - OCX) * SCALE_NUM / SCALE_DEN;
    SLSH_Y[i] = CY + (int16_t)(SLSH_LY[i] - OCY) * SCALE_NUM / SCALE_DEN;
    SRSH_X[i] = CX + (int16_t)(SLSH_RX[i] - OCX) * SCALE_NUM / SCALE_DEN;
    SRSH_Y[i] = CY + (int16_t)(SLSH_RY[i] - OCY) * SCALE_NUM / SCALE_DEN;
  }
}

void loop() {
  M5.update();

  bool key1 = M5.BtnA.isPressed();   // KEY1 = 敌跟踪
  bool key2 = M5.BtnB.isPressed();   // KEY2 = 敌导弹

  if (key1) {
    trigger(key2 ? BUZ_MSL : BUZ_LOCK);
  } else if (key2) {
    trigger(BUZ_MSL);
  } else if (buz_task != NULL && buz_ty != BUZ_RADAR) {
    stop_buz();                    // 松手停止 LOCK/MSL
  } else if (random(100) < 1) {    // 空闲 1% 随机雷达告警
    trigger(BUZ_RADAR);
  }

  render();
  delay(50);
}

// 2x2 斜线像素;right=false "\" ,right=true "/"
inline void drawSlash(int16_t x, int16_t y, bool right, uint16_t color) {
  if (right) {
    canvas.drawPixel(x + 1, y, color);
    canvas.drawPixel(x, y + 1, color);
  } else {
    canvas.drawPixel(x, y, color);
    canvas.drawPixel(x + 1, y + 1, color);
  }
}

void drawAlertLabel(const char* s, uint16_t color) {
  constexpr int w = 78, h = 34;
  int x = 8, y = (M5.Display.height() - h) / 2;
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::efontCN_16);
  canvas.setTextColor(color);
  canvas.drawRoundRect(x, y, w, h, 6, color);
  canvas.drawString(s, x + w / 2, y + h / 2);
}

void drawHint() {
  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::efontCN_12);
  canvas.setTextColor(CLR_DIM);
  canvas.drawString("KEY1 敌跟踪", 8, 22);
  canvas.drawString("KEY2 敌导弹", 8, 42);
}

void render() {
  canvas.fillSprite(TFT_BLACK);

  bool alert = blink_flag && blink_cnt >= 0;
  uint16_t ac = (buz_ty == BUZ_MSL) ? TFT_RED
               : (buz_ty == BUZ_LOCK) ? TFT_YELLOW
               : TFT_WHITE;                          // 告警色

  if (alert) {
    // 粗环(挖空)
    canvas.fillCircle(CX, CY, R, ac);
    canvas.fillCircle(CX, CY, R_ALERT_IN, TFT_BLACK);
    // 全量斜线
    for (uint8_t i = 0; i < SLSH_POSLEN; i++) {
      drawSlash(SLSH_X[i], SLSH_Y[i], false, ac);
      drawSlash(SRSH_X[i], SRSH_Y[i], true, ac);
    }
    if (buz_ty == BUZ_LOCK) drawAlertLabel("敌跟踪", ac);
    else if (buz_ty == BUZ_MSL) drawAlertLabel("敌导弹", ac);
  } else {
    // 平时:四角斜线 + 操作提示
    for (uint8_t i = 0; i < SLSH_POSDELI; i++) {
      drawSlash(SLSH_X[i], SLSH_Y[i], false, TFT_WHITE);
      drawSlash(SRSH_X[i], SRSH_Y[i], true, TFT_WHITE);
    }
    drawHint();
  }

  // 静态雷达要素
  canvas.drawCircle(CX, CY, R, TFT_WHITE);           // 外圈
  canvas.drawCircle(CX, CY, R_INNER, TFT_WHITE);     // 内圈
  canvas.drawLine(CX - 2, CY, CX + 2, CY, TFT_WHITE);        // 中心十字
  canvas.drawLine(CX, CY - 2, CX, CY + 2, TFT_WHITE);
  canvas.drawLine(CX - 53, CY, CX - 48, CY, TFT_WHITE);      // 四方向短线
  canvas.drawLine(CX + 49, CY, CX + 54, CY, TFT_WHITE);
  canvas.drawLine(CX, CY - 53, CX, CY - 48, TFT_WHITE);
  canvas.drawLine(CX, CY + 49, CX, CY + 54, TFT_WHITE);

  canvas.pushSprite(0, 0);
}

void do_buz(void* params) {
  for (;;) {
    switch (buz_ty) {
      case BUZ_RADAR:
        beep(NOTE_MIDL, 101);
        stop_buz();                  // RADAR 单发,响完自停
        break;
      case BUZ_LOCK:
        beep(NOTE_HIGH, 215);
        beep(NOTE_LOW1, 215);
        break;
      case BUZ_MSL:
        beep(NOTE_HIGH, 83);
        beep(NOTE_LOW2, 83);
        break;
      default:
        vTaskDelay(50 / portTICK_PERIOD_MS);
        break;
    }
  }
}

void beep(double freq, uint32_t ms) {
  M5.Speaker.tone(freq);
  vTaskDelay(ms / portTICK_PERIOD_MS);
  M5.Speaker.stop();
}

void stop_buz() {
  if (buz_task == NULL) return;
  buz_ty = BUZ_NONE;
  M5.Speaker.stop();
  TaskHandle_t t = buz_task;
  buz_task = NULL;                   // 先清句柄再删任务,自删也安全
  blink_flag = false;
  blink_cnt = 0;
  if (t) vTaskDelete(t);
}
