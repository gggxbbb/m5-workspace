#include <M5Unified.h>
#include <NimBLEDevice.h>

void setup() {
    Serial.begin(115200);
    Serial.println("[t2] bare serial OK");
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.println("[t2] after M5.begin");
    M5.Display.setRotation(0);
    M5.Display.fillScreen(BLACK);
    NimBLEDevice::init("t2");
    Serial.println("[t2] after NimBLE init");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    Serial.println("[t2] scan start");
    scan->start(3000, false);
    Serial.println("[t2] scan started");
}
void loop() {
    M5.update();
    static uint32_t t = 0;
    if (millis() - t > 1000) { t = millis(); Serial.println("[t2] alive"); }
}
