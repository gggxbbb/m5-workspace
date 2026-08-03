// imu-calib — Stage 1 IMU axis calibration debug tool (M5StickS3 / BMI270)
//
// Units (per M5Unified IMU_Class):
//   accel ax/ay/az : g   (1 g ≈ 9.80665 m/s^2)
//   gyro  gx/gy/gz : dps (degrees per second)
//
// Serial @115200, ~20 Hz CSV: ax,ay,az,gx,gy,gz,|a|
// BtnA (G11) short press prints "=== MARK ===" to mark gesture moments in log.
// Screen: live accel numbers + dot moving with tilt to visually identify axes.

#include <M5Unified.h>
#include <math.h>

static constexpr uint32_t PRINT_PERIOD_MS = 50;  // 20 Hz

static uint32_t lastPrint = 0;
static bool     headerPrinted = false;

static inline void drawUi(float ax, float ay, float az)
{
  auto& d = M5.Display;
  d.setTextSize(1);

  // numbers (overwrite in place)
  d.setCursor(6, 8);  d.printf("ax % .3f g ", ax);
  d.setCursor(6, 24); d.printf("ay % .3f g ", ay);
  d.setCursor(6, 40); d.printf("az % .3f g ", az);

  // tilt dot: x ~ ay, y ~ az, clamped to crosshair box (bottom half of screen)
  constexpr int cx = 135 / 2;
  constexpr int cy = 170;
  constexpr int half = 50;  // dot range: ±1 g -> ±50 px

  d.fillRect(0, 100, 135, 140, TFT_BLACK);
  d.drawFastHLine(cx - half, cy, half * 2 + 1, TFT_DARKGREY);
  d.drawFastVLine(cx, cy - half, half * 2 + 1, TFT_DARKGREY);

  int px = cx + (int)(ay * half);
  int py = cy - (int)(az * half);
  px = constrain(px, cx - half, cx + half);
  py = constrain(py, cy - half, cy + half);
  d.fillCircle(px, py, 5, TFT_GREEN);
}

void setup()
{
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  auto& d = M5.Display;
  d.setRotation(0);          // 135 wide x 240 tall
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextDatum(top_left);

  if (!M5.Imu.isEnabled()) {
    d.setCursor(6, 8);
    d.println("IMU NOT FOUND!");
    Serial.println("ERROR: IMU not enabled");
  }
}

void loop()
{
  M5.update();

  if (M5.BtnA.wasClicked()) {
    Serial.println("=== MARK ===");
  }

  if (!M5.Imu.isEnabled()) return;
  if (!M5.Imu.update()) return;

  auto imu = M5.Imu.getImuData();
  const float ax = imu.accel.x, ay = imu.accel.y, az = imu.accel.z;
  const float gx = imu.gyro.x,  gy = imu.gyro.y,  gz = imu.gyro.z;

  drawUi(ax, ay, az);

  const uint32_t now = millis();
  if (now - lastPrint >= PRINT_PERIOD_MS) {
    lastPrint = now;
    if (!headerPrinted) {
      headerPrinted = true;
      // accel unit: g ; gyro unit: dps
      Serial.println("ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,amag_g");
    }
    const float amag = sqrtf(ax * ax + ay * ay + az * az);
    Serial.printf("%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.4f\n",
                  ax, ay, az, gx, gy, gz, amag);
  }
}
