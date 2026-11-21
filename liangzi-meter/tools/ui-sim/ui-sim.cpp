#include <windows.h>
#include <M5GFX.h>
#include <LiangziModel.h>
#include <LiangziUi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kScale = 2;

void populateDecliningHistory(liangzi::BalanceHistory& h, double endVal, bool peakDrops) {
  h.clear();
  double v = endVal + 3.24;
  for (int i = 0; i < 40; ++i) {
    const double drop = (i % 5 == 2) ? 0.14 : ((i % 7 == 4) ? 0.22 : ((i % 3 == 0) ? 0.07 : 0.0));
    v = std::max(endVal, v - drop);
    if (i == 39) v = endVal;
    const bool dec = peakDrops && (i == 34 || i == 38 || i == 39);
    h.push(liangzi::BalanceSample{v, 0.0, v, static_cast<uint32_t>(i * 15000), peakDrops, dec});
  }
}

void populateFlatHistory(liangzi::BalanceHistory& h, double val) {
  h.clear();
  for (int i = 0; i < 40; ++i) {
    h.push(liangzi::BalanceSample{val, 0.0, val, static_cast<uint32_t>(i * 300000), false, false});
  }
}

struct Scenario {
  liangzi::UiModel m;
  liangzi::BalanceHistory hist;
};

std::vector<Scenario> buildScenarios(uint32_t nowMs) {
  std::vector<Scenario> list;

  // 0: Peak Workday (梁文峰, 赠10 + 充100, RWR 15s, Chinese UTF-8 SSID fallback test)
  {
    Scenario s;
    populateDecliningHistory(s.hist, 110.00, true);
    auto& m = s.m;
    m.configured = true;
    m.wifiConnected = true;
    m.wifiRssi = -56;
    m.ssid = "工作室-5G";
    m.ip = "192.168.31.118";
    m.ntp = "ntp.aliyun.com";
    m.fwVersion = "1.4.0";
    m.timeSynced = true;
    m.year = 2026; m.month = 8; m.day = 18; m.wday = 2;
    m.hour = 15; m.minute = 32; m.second = 18;
    m.phaseKind = liangzi::PhaseKind::kPeak;
    m.countdownSec = 2 * 3600 + 27 * 60 + 42;
    m.balanceValid = true;
    m.balanceExpired = false;
    m.balanceAvailable = true;
    m.httpStatus = 200;
    m.currency = "CNY";
    m.totalBalance = 110.00;
    m.grantedBalance = 10.00;
    m.toppedUpBalance = 100.00;
    m.hasSplit = true;
    m.balanceWarn = 10.0f;
    m.lastDelta = -0.18;
    m.hasDelta = true;
    m.lastFetchMs = nowMs > 6000 ? nowMs - 6000 : 0;
    m.refreshIntervalMs = 15000;
    m.alertEnabled = true;
    m.mslAlertActive = false;
    m.batteryPct = 84;
    m.charging = true;
    m.rotation = 0;
    m.autoRotate = false;
    m.history = &s.hist;
    m.dataRevision = 1;
    list.push_back(std::move(s));
  }

  // 1: Low Balance + RWR Missile Active Alert (Peak MUST stay Amber, Balance Red + pulsing MSL)
  {
    Scenario s;
    populateDecliningHistory(s.hist, 6.82, true);
    auto& m = s.m;
    m.configured = true;
    m.wifiConnected = true;
    m.wifiRssi = -59;
    m.ssid = "Studio-5G";
    m.ip = "192.168.31.118";
    m.ntp = "ntp.aliyun.com";
    m.fwVersion = "1.4.0";
    m.timeSynced = true;
    m.year = 2026; m.month = 8; m.day = 19; m.wday = 3;
    m.hour = 10; m.minute = 42; m.second = 16;
    m.phaseKind = liangzi::PhaseKind::kPeak;
    m.countdownSec = 1 * 3600 + 17 * 60 + 44;
    m.balanceValid = true;
    m.balanceAvailable = true;
    m.httpStatus = 200;
    m.currency = "CNY";
    m.totalBalance = 6.82;
    m.grantedBalance = 0.00;
    m.toppedUpBalance = 6.82;
    m.hasSplit = true;
    m.balanceWarn = 10.0f;
    m.lastDelta = -0.45;
    m.hasDelta = true;
    m.lastFetchMs = nowMs > 4000 ? nowMs - 4000 : 0;
    m.refreshIntervalMs = 15000;
    m.alertEnabled = true;
    m.mslAlertActive = true;
    m.batteryPct = 72;
    m.charging = false;
    m.rotation = 0;
    m.history = &s.hist;
    m.dataRevision = 2;
    list.push_back(std::move(s));
  }

  // 2: National Day Holiday Off-Peak (梁文谷, 153h44m countdown, all-green 24h bar)
  {
    Scenario s;
    populateFlatHistory(s.hist, 84.50);
    auto& m = s.m;
    m.configured = true;
    m.wifiConnected = true;
    m.wifiRssi = -52;
    m.ssid = "Home_Net";
    m.ip = "10.0.0.48";
    m.fwVersion = "1.4.0";
    m.timeSynced = true;
    m.year = 2026; m.month = 10; m.day = 1; m.wday = 4;
    m.hour = 10; m.minute = 15; m.second = 30;
    m.phaseKind = liangzi::PhaseKind::kOffPeakHoliday;
    m.countdownSec = 153LL * 3600LL + 44LL * 60LL + 30LL;
    m.balanceValid = true;
    m.balanceAvailable = true;
    m.httpStatus = 200;
    m.currency = "CNY";
    m.totalBalance = 84.50;
    m.grantedBalance = 0.00;
    m.toppedUpBalance = 84.50;
    m.hasSplit = true;
    m.balanceWarn = 10.0f;
    m.hasDelta = false;
    m.lastFetchMs = nowMs > 95000 ? nowMs - 95000 : 0;
    m.refreshIntervalMs = 300000;
    m.alertEnabled = false;
    m.batteryPct = 91;
    m.charging = false;
    m.rotation = 0;
    m.history = &s.hist;
    m.dataRevision = 3;
    list.push_back(std::move(s));
  }

  // 3: Workday Off-Peak + Flatline History (21:18, 梁文谷, steady history)
  {
    Scenario s;
    populateFlatHistory(s.hist, 110.00);
    auto& m = s.m;
    m.configured = true;
    m.wifiConnected = true;
    m.wifiRssi = -60;
    m.ssid = "Studio-5G";
    m.ip = "192.168.31.118";
    m.fwVersion = "1.4.0";
    m.timeSynced = true;
    m.year = 2026; m.month = 8; m.day = 18; m.wday = 2;
    m.hour = 21; m.minute = 18; m.second = 45;
    m.phaseKind = liangzi::PhaseKind::kOffPeakWorkday;
    m.countdownSec = 11 * 3600 + 41 * 60 + 15;
    m.balanceValid = true;
    m.balanceAvailable = true;
    m.httpStatus = 200;
    m.currency = "CNY";
    m.totalBalance = 110.00;
    m.grantedBalance = 10.00;
    m.toppedUpBalance = 100.00;
    m.hasSplit = true;
    m.balanceWarn = 10.0f;
    m.alertEnabled = true;
    m.batteryPct = 68;
    m.charging = true;
    m.rotation = 1;
    m.history = &s.hist;
    m.dataRevision = 4;
    list.push_back(std::move(s));
  }

  // 4: Expired / HTTP 504 + Hold Arc state
  {
    Scenario s;
    populateDecliningHistory(s.hist, 94.20, false);
    auto& m = s.m;
    m.configured = true;
    m.wifiConnected = true;
    m.wifiRssi = -82;
    m.ssid = "MobileAP";
    m.ip = "172.20.10.5";
    m.fwVersion = "1.4.0";
    m.timeSynced = true;
    m.year = 2026; m.month = 8; m.day = 20; m.wday = 4;
    m.hour = 11; m.minute = 5; m.second = 22;
    m.phaseKind = liangzi::PhaseKind::kPeak;
    m.countdownSec = 54 * 60 + 38;
    m.balanceValid = true;
    m.balanceExpired = true;
    m.balanceAvailable = true;
    m.queryBusy = true;
    m.httpStatus = 504;
    m.currency = "CNY";
    m.totalBalance = 94.20;
    m.grantedBalance = 4.20;
    m.toppedUpBalance = 90.00;
    m.hasSplit = true;
    m.lastFetchMs = nowMs > 180000 ? nowMs - 180000 : 0;
    m.refreshIntervalMs = 15000;
    m.alertEnabled = true;
    m.batteryPct = 44;
    m.charging = false;
    m.rotation = 1;
    m.holdBProgress = 0.68f;
    m.history = &s.hist;
    m.dataRevision = 5;
    list.push_back(std::move(s));
  }

  // 5: Factory Setup Onboarding
  {
    Scenario s;
    auto& m = s.m;
    m.configured = false;
    m.wifiConnected = false;
    m.fwVersion = "1.4.0";
    m.batteryPct = 98;
    m.charging = true;
    m.rotation = 0;
    m.history = &s.hist;
    m.dataRevision = 6;
    list.push_back(std::move(s));
  }

  return list;
}

void writeBmp2x(const lgfx::LGFX_Sprite& sprite, int w, int h, const std::filesystem::path& path) {
  const int outW = w * kScale;
  const int outH = h * kScale;
  const int rowStride = ((outW * 3 + 3) / 4) * 4;
  const uint32_t imgSize = static_cast<uint32_t>(rowStride * outH);
  const uint32_t fileSize = 54u + imgSize;

  std::vector<uint8_t> buf(fileSize, 0);
  buf[0] = 'B'; buf[1] = 'M';
  buf[2] = fileSize & 0xFF; buf[3] = (fileSize >> 8) & 0xFF;
  buf[4] = (fileSize >> 16) & 0xFF; buf[5] = (fileSize >> 24) & 0xFF;
  buf[10] = 54;
  buf[14] = 40;
  buf[18] = outW & 0xFF; buf[19] = (outW >> 8) & 0xFF;
  buf[22] = outH & 0xFF; buf[23] = (outH >> 8) & 0xFF;
  buf[26] = 1;
  buf[28] = 24;

  for (int sy = 0; sy < h; ++sy) {
    for (int sx = 0; sx < w; ++sx) {
      const auto px = const_cast<lgfx::LGFX_Sprite&>(sprite).readPixelRGB(sx, sy);
      for (int dy = 0; dy < kScale; ++dy) {
        const int yBmp = (outH - 1) - (sy * kScale + dy);
        uint8_t* row = buf.data() + 54 + yBmp * rowStride;
        for (int dx = 0; dx < kScale; ++dx) {
          const int xOff = (sx * kScale + dx) * 3;
          row[xOff + 0] = px.b;
          row[xOff + 1] = px.g;
          row[xOff + 2] = px.r;
        }
      }
    }
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
}

int runDump(const std::filesystem::path& outDir) {
  std::filesystem::create_directories(outDir);
  liangzi::LiangziUi ui;
  if (!ui.begin(nullptr, 135, 240, false)) {
    std::fprintf(stderr, "LiangziUi::begin failed\n");
    return 1;
  }

  auto scenarios = buildScenarios(20000);
  for (auto& sc : scenarios) sc.m.history = &sc.hist;

  auto dumpFrame = [&](const char* name, int w, int h, uint8_t rot, liangzi::UiModel& m,
                       int page, uint32_t tStart, int frames, uint32_t dtMs) {
    m.rotation = rot;
    ui.onResize(w, h);
    ui.setPage(page, tStart);
    uint32_t t = tStart;
    for (int i = 0; i < frames; ++i) {
      ui.render(m, t, false);
      t += dtMs;
    }
    writeBmp2x(ui.canvas(), w, h, outDir / name);
  };

  // Portrait 135x240 (6 frames)
  {
    auto m0 = scenarios[0].m;
    dumpFrame("p01_peak_cockpit.bmp", 135, 240, 0, m0, 0, 10000, 32, 20);
    dumpFrame("p02_split_trend.bmp", 135, 240, 0, m0, 1, 11000, 32, 20);
    dumpFrame("p03_schedule_sys.bmp", 135, 240, 0, m0, 2, 12000, 32, 20);
  }
  {
    auto m1 = scenarios[1].m;
    dumpFrame("p04_low_rwr_alert.bmp", 135, 240, 0, m1, 0, 13500, 32, 20);  // final t=14120 -> (14120/320)=44 even -> mslFlash=true!
  }
  {
    auto m2 = scenarios[2].m;
    dumpFrame("p05_holiday_offpeak.bmp", 135, 240, 0, m2, 0, 16000, 32, 20);
  }
  {
    auto m5 = scenarios[5].m;
    dumpFrame("p06_setup_onboarding.bmp", 135, 240, 0, m5, 0, 18000, 16, 20);
  }

  // Landscape 240x135 (6 frames, testing rot 1 & rot 3 ADR-0005 mirror)
  {
    auto m0 = scenarios[0].m;
    dumpFrame("l01_rot1_peak_cockpit.bmp", 240, 135, 1, m0, 0, 20000, 32, 20);
    dumpFrame("l02_rot3_peak_mirror.bmp", 240, 135, 3, m0, 0, 21000, 32, 20);
    dumpFrame("l04_schedule_sys.bmp", 240, 135, 1, m0, 2, 22000, 32, 20);
  }
  {
    auto m3 = scenarios[3].m;
    dumpFrame("l03_flat_trend.bmp", 240, 135, 1, m3, 1, 24000, 32, 20);
  }
  {
    auto m4 = scenarios[4].m;
    dumpFrame("l05_stale_hold_arc.bmp", 240, 135, 1, m4, 0, 26000, 24, 20);
  }
  {
    auto m3 = scenarios[3].m;
    ui.onResize(240, 135);
    ui.showToast("自动旋屏已开", 28000, 2000);
    dumpFrame("l06_offpeak_toast.bmp", 240, 135, 3, m3, 0, 28100, 16, 20);
  }

  std::printf("Dumped 12 preview frames (6 portrait + 6 landscape) to %s\n",
              outDir.string().c_str());
  return 0;
}

struct AppState {
  std::vector<Scenario> scenarios;
  size_t idx = 0;
  uint8_t rot = 0;
  bool charging = true;
  bool autoRotate = false;
  bool busy = false;
  uint32_t busyUntil = 0;
  uint32_t btnBDownAt = 0;
  bool btnBHoldFired = false;
  liangzi::LiangziUi ui;
  std::vector<uint8_t> dibPixels;
  HWND hwnd = nullptr;
};

void resizeWinClient(AppState* app) {
  const int w = (app->rot == 1 || app->rot == 3) ? 240 : 135;
  const int h = (app->rot == 1 || app->rot == 3) ? 135 : 240;
  app->ui.onResize(w, h);
  app->dibPixels.resize(static_cast<size_t>(w) * h * 4);
  RECT rc{0, 0, w * kScale, h * kScale};
  AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  SetWindowPos(app->hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* app = reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  if (!app) return DefWindowProc(hwnd, msg, wParam, lParam);

  const uint32_t now = static_cast<uint32_t>(GetTickCount64());
  switch (msg) {
    case WM_KEYDOWN:
      if (lParam & (1 << 30)) break;
      if (wParam >= '1' && wParam <= '6') {
        app->idx = static_cast<size_t>(wParam - '1');
        app->scenarios[app->idx].m.history = &app->scenarios[app->idx].hist;
      } else if (wParam == 'A') {
        app->ui.triggerPulse(now);
        app->busy = true;
        app->busyUntil = now + 900;
      } else if (wParam == 'B') {
        app->btnBDownAt = now;
        app->btnBHoldFired = false;
      } else if (wParam == 'R') {
        app->rot = static_cast<uint8_t>((app->rot + 1) & 3);
        resizeWinClient(app);
      } else if (wParam == 'U') {
        app->autoRotate = !app->autoRotate;
        app->ui.showToast(app->autoRotate ? "自动旋屏已开" : "已关自动旋屏", now);
      } else if (wParam == 'C') {
        app->charging = !app->charging;
      } else if (wParam == VK_ESCAPE) {
        PostQuitMessage(0);
      }
      break;
    case WM_KEYUP:
      if (wParam == 'B' && app->btnBDownAt != 0) {
        if (!app->btnBHoldFired) app->ui.nextPage(now);
        app->btnBDownAt = 0;
      }
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

int runWindow() {
  AppState app;
  const uint32_t startNow = static_cast<uint32_t>(GetTickCount64());
  app.scenarios = buildScenarios(startNow);
  for (auto& sc : app.scenarios) sc.m.history = &sc.hist;
  if (!app.ui.begin(nullptr, 135, 240, false)) return 1;
  app.dibPixels.resize(135 * 240 * 4);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"LiangziUiSimClass";
  RegisterClassExW(&wc);

  RECT rect{0, 0, 135 * kScale, 240 * kScale};
  AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  app.hwnd = CreateWindowExW(
      0, wc.lpszClassName,
      L"liangzi-meter Sim [1-6: Scen | A: Refresh | B: Page/Hold Rot | R: Rotate | U: AutoRot]",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
      nullptr, nullptr, wc.hInstance, &app);
  ShowWindow(app.hwnd, SW_SHOW);

  MSG msg{};
  bool running = true;
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) { running = false; break; }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!running) break;

    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    if (app.busy && now >= app.busyUntil) {
      app.busy = false;
      app.scenarios[app.idx].m.lastFetchMs = now;
    }
    float holdProg = 0.0f;
    if (app.btnBDownAt != 0 && !app.btnBHoldFired) {
      const uint32_t held = now - app.btnBDownAt;
      if (held >= 150) holdProg = std::min(1.0f, static_cast<float>(held - 150) / 550.0f);
      if (held >= 700) {
        app.btnBHoldFired = true;
        holdProg = 0.0f;
        if (app.autoRotate) {
          app.ui.showToast("自动旋屏已开", now);
        } else {
          app.rot = static_cast<uint8_t>((app.rot + 1) & 3);
          resizeWinClient(&app);
        }
      }
    }

    auto model = app.scenarios[app.idx].m;
    model.history = &app.scenarios[app.idx].hist;
    model.queryBusy = app.busy;
    model.charging = app.charging;
    model.rotation = app.rot;
    model.autoRotate = app.autoRotate;
    model.holdBProgress = holdProg;
    app.ui.render(model, now, false);

    const int w = app.ui.width();
    const int h = app.ui.height();
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    auto& spr = const_cast<lgfx::LGFX_Sprite&>(app.ui.canvas());
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const auto px = spr.readPixelRGB(x, y);
        const size_t dst = static_cast<size_t>((y * w + x) * 4);
        app.dibPixels[dst + 0] = px.b;
        app.dibPixels[dst + 1] = px.g;
        app.dibPixels[dst + 2] = px.r;
        app.dibPixels[dst + 3] = 255;
      }
    }

    HDC hdc = GetDC(app.hwnd);
    StretchDIBits(hdc, 0, 0, w * kScale, h * kScale, 0, 0, w, h,
                  app.dibPixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(app.hwnd, hdc);
    Sleep(16);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--dump-dir") {
    return runDump(argv[2]);
  }
  return runWindow();
}
