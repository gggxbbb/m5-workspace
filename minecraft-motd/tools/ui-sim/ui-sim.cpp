#include <windows.h>
#include <M5GFX.h>
#include <MinecraftMotd.h>
#include <MotdUi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kW = motd::MotdUi::kWidth;
constexpr int kH = motd::MotdUi::kHeight;
constexpr int kScale = 2;

const char* kSurvivalFaviconB64 =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABkUlEQVR42u2bvUoDQRDHd/xIynDaiBK0CCI22gmKPoHGvIZ1Oh/AztpHSEgav95AEOy8RkQstAshaG1EtBLdLS4se7m7ML+p7gjZO47//H83c7NSqu98m9/4O1JzPGWUx4wxovsBCArQ/QjwAEEBeMBYL/DVWA/6/3T3QTcFBAoIHjBRFPisr1nnG7ubQevFzvls5xEFKH8TFN21gBRNAcOD1cScb65sBa1/OsITSu0nPEB1LRB6/yjA1wM+9mteOX90exF0g2fbh16eUG496+4ICR0hwQNypYCb89WlvVTXqy0upEoFFKDdA6BA1go4Xo4Sfz95fYcC1AIZ1gYoAApAAWoBKJBlhHMeCkABKFBkCrg9vNR7gi93UIBaAArkSIHKdc/8n7WNzX1i397NYd9wcz6+sa9XafecOWBlFBDeA/g2mC8Foqu+NYc/yhN8w835qNO3Z/5RADNCzAgVSgFzlwMrR11P8F6vO3D2+ghzgqpqAWFWeMIpMH/+FrjHT9g1xnsAHkBHCAXgAVBgPPEDyQ5iY6KED48AAAAASUVORK5CYII=";

const char* kNetherFaviconB64 =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABMUlEQVR42u2boQoCQRRFZ0Wz4BZBMAkmFcsGk8liUvBXNFv9CINVwWQxbTLYxCT4AZYVzIJrdTasDOzT0TmvDcyUw55Z7nuM125OY/VSUbRXaeX7gfqn/TnleAHAdQB515xP7kcBALh+B7jmPF8AAACg3wGmzsw6K23drekMl9ubtm4MStr6criLng8WQ7IACgBAMAuUWwVtHSacHPWKutPra6rTWZ8nC6AAAGSzwPHDTpue909kARQAgGQW+PZ//t155gIoAACj8qqVfmzizKQ+/ql+AFkABQCQbRawvR/AXAAFACCbBWzvB0Q7sgAKAEByLmB7P4C5AAoAQDYLhOeH1XfCfEMWQAEASGYB2/sBzAVQAABKdC7AewEUAADvBRTvBVAAAM5mAd4OowAA/qqeLdYxRI8Gw+kAAAAASUVORK5CYII=";

struct Fixture {
  std::string host;
  uint16_t port = 25565;
  int32_t protocol = 765;
  bool configured = true;
  bool wifiConnected = true;
  bool busy = false;
  bool charging = false;
  int batteryPct = 84;
  int32_t playerDelta = 0;
  minecraft::Result result;
  motd::TelemetryHistory history;
};

void populateHistory(motd::TelemetryHistory& h, int32_t baseMs, int jitter, const std::vector<int>& fails) {
  h.clear();
  for (int i = 0; i < 40; ++i) {
    bool isFail = std::find(fails.begin(), fails.end(), i) != fails.end();
    motd::TelemetrySample s;
    s.atMs = static_cast<uint32_t>(i * 30000);
    if (isFail) {
      s.complete = false;
      s.pongMs = -1;
      s.online = -1;
    } else {
      s.complete = true;
      int wave = static_cast<int>(std::sin(i * 0.42f) * jitter * 0.6f) + ((i * 17) % 11 - 5);
      s.pongMs = std::max<int32_t>(8, baseMs + wave);
      s.online = 14 + (i % 4);
    }
    h.push(s);
  }
}

std::vector<Fixture> buildFixtures() {
  std::vector<Fixture> list;

  // 0: Survival CN (Emerald favicon + CN/EN MOTD + bold/underline/§k + 8 players)
  {
    Fixture f;
    f.host = "mc.craft-studio.cn";
    f.port = 25565;
    f.protocol = 765;
    f.batteryPct = 84;
    f.playerDelta = 2;
    f.result.address = "203.0.113.42";
    f.result.dnsMs = 4;
    f.result.tcpMs = 16;
    f.result.statusMs = 28;
    f.result.pongMs = 31;
    f.result.stage = minecraft::Stage::Done;
    f.result.complete = true;
    const std::string json = std::string(R"({
      "version": {"name": "1.20.4", "protocol": 765},
      "players": {
        "online": 14,
        "max": 200,
        "sample": [
          {"name": "\u00a7aSteve", "id": "1"},
          {"name": "Alex", "id": "2"},
          {"name": "CSJ_Builder", "id": "3"},
          {"name": "RedstoneCat", "id": "4"},
          {"name": "CreeperLab", "id": "5"},
          {"name": "M5Player", "id": "6"},
          {"name": "NotchFan", "id": "7"},
          {"name": "NetherWalker", "id": "8"}
        ]
      },
      "description": {
        "text": "",
        "extra": [
          {"text": "* ", "color": "#FBBF24"},
          {"text": "CSJ's Survival", "color": "#4ADE80", "bold": true},
          {"text": " · ", "color": "#7E93A6"},
          {"text": "建筑与红石工艺服务器 第四季火热开荒中！\n", "color": "#E2E8F0"},
          {"text": "[公告] ", "color": "#38BDF8", "bold": true},
          {"text": "周末地狱交通网验收 ", "color": "#A7F3D0"},
          {"text": "123", "color": "#FBBF24", "obfuscated": true},
          {"text": " 欢迎萌新\n", "color": "#EEF4F8", "underlined": true},
          {"text": "> Live TPS: ", "color": "#7E93A6"},
          {"text": "20.0", "color": "#4ADE80", "bold": true},
          {"text": " | 原版纯净生电\n", "color": "#FDE68A"},
          {"text": "QQ Group: 84920144 · Java 1.20.4", "color": "#93C5FD"}
        ]
      },
      "favicon": ")") + kSurvivalFaviconB64 + R"("})";
    minecraft::parseStatusJson(json, f.result);
    populateHistory(f.history, 33, 12, {});
    list.push_back(std::move(f));
  }

  // 1: Nether RGB (Purple favicon + high count + hidden sample)
  {
    Fixture f;
    f.host = "play.hypixel-edge.net";
    f.port = 25565;
    f.protocol = 767;
    f.batteryPct = 62;
    f.charging = true;
    f.playerDelta = -12;
    f.result.address = "172.65.240.18";
    f.result.dnsMs = 12;
    f.result.tcpMs = 64;
    f.result.statusMs = 112;
    f.result.pongMs = 186;
    f.result.stage = minecraft::Stage::Done;
    f.result.complete = true;
    const std::string json = std::string(R"({
      "version": {"name": "1.8 - 1.21", "protocol": 767},
      "players": {"online": 42819, "max": 200000},
      "description": {
        "text": "",
        "extra": [
          {"text": "HYPIXEL ", "color": "#FBBF24", "bold": true},
          {"text": "NETWORK ", "color": "#F59E0B", "bold": true},
          {"text": "[1.8-1.21]  ", "color": "#94A3B8"},
          {"text": "SKYBLOCK 0.21!\n", "color": "#C084FC", "bold": true},
          {"text": "NEW ", "color": "#4ADE80", "bold": true},
          {"text": "BedWars + ", "color": "#E879F9"},
          {"text": "x3 Coins", "color": "#FBBF24", "underlined": true},
          {"text": "  Old Event ", "color": "#7E93A6"},
          {"text": "Ended", "color": "#64748B", "strikethrough": true}
        ]
      },
      "favicon": ")") + kNetherFaviconB64 + R"("})";
    minecraft::parseStatusJson(json, f.result);
    populateHistory(f.history, 182, 38, {14});
    list.push_back(std::move(f));
  }

  // 2: No Favicon -> Grass Block + Pong Error (partial status preserved)
  {
    Fixture f;
    f.host = "vanilla.home-lab.local";
    f.port = 25565;
    f.protocol = 765;
    f.batteryPct = 49;
    f.result.address = "192.168.1.88";
    f.result.dnsMs = 2;
    f.result.tcpMs = 9;
    f.result.statusMs = 24;
    f.result.pongMs = -1;
    f.result.stage = minecraft::Stage::Pong;
    f.result.complete = false;
    f.result.error = "Invalid Pong, disconnect or timeout";
    const std::string json = R"json({
      "version": {"name": "Paper 1.21.1", "protocol": 765},
      "players": {"online": 3, "max": 20, "sample": [{"name": "Dad"}, {"name": "M5StickS3"}, {"name": "Builder01"}]},
      "description": {
        "text": "",
        "extra": [
          {"text": "Home Vanilla Server", "color": "#4ADE80", "bold": true},
          {"text": " - Status OK\n", "color": "#EEF4F8"},
          {"text": "! Pong packet timed out (MOTD preserved)", "color": "#FCA5A5"}
        ]
      }
    })json";
    minecraft::parseStatusJson(json, f.result);
    populateHistory(f.history, 26, 8, {37, 38, 39});
    list.push_back(std::move(f));
  }

  // 3: TCP Error
  {
    Fixture f;
    f.host = "remote.overworld.org";
    f.port = 25565;
    f.protocol = 47;
    f.batteryPct = 18;
    f.result.address = "198.51.100.77";
    f.result.dnsMs = 18;
    f.result.tcpMs = -1;
    f.result.stage = minecraft::Stage::Tcp;
    f.result.statusReceived = false;
    f.result.complete = false;
    f.result.error = "Connection failed or timed out";
    populateHistory(f.history, 95, 25, {34, 35, 36, 37, 38, 39});
    list.push_back(std::move(f));
  }

  return list;
}

void writeBmp2x(const lgfx::LGFX_Sprite& sprite, const std::filesystem::path& path) {
  const int outW = kW * kScale;
  const int outH = kH * kScale;
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

  for (int sy = 0; sy < kH; ++sy) {
    for (int sx = 0; sx < kW; ++sx) {
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

motd::UiModel makeModel(const Fixture& f, uint32_t rev, uint32_t nowMs) {
  motd::UiModel m;
  m.configured = f.configured;
  m.wifiConnected = f.wifiConnected;
  m.busy = f.busy;
  m.charging = f.charging;
  m.batteryPct = f.batteryPct;
  m.rotationDeg = 90;
  m.wifiAttemptAt = 0;
  m.finishedAt = nowMs > 8000 ? nowMs - 8000 : 0;
  m.refreshMs = 30000;
  m.holdBProgress = 0.0f;
  m.host = f.host;
  m.port = f.port;
  m.protocol = f.protocol;
  m.result = &f.result;
  m.resultRevision = rev;
  m.playerDelta = f.playerDelta;
  m.playerDeltaAt = nowMs;
  m.history = &f.history;
  return m;
}

int runDump(const std::filesystem::path& outDir) {
  std::filesystem::create_directories(outDir);
  motd::MotdUi ui;
  if (!ui.begin(nullptr, false)) {
    std::fprintf(stderr, "MotdUi::begin failed\n");
    return 1;
  }

  auto fixtures = buildFixtures();
  auto stepAndDump = [&](const char* name, motd::UiModel& model, int page, uint32_t startMs, int frames, uint32_t stepMs) {
    ui.setPage(page, startMs);
    uint32_t t = startMs;
    for (int i = 0; i < frames; ++i) {
      ui.render(model, t, false);
      t += stepMs;
    }
    writeBmp2x(ui.canvas(), outDir / name);
  };

  // 1-3: Survival CN across all 3 pages (settled)
  {
    auto m = makeModel(fixtures[0], 1, 10000);
    stepAndDump("01_survival_card.bmp", m, 0, 10000, 32, 20);
    stepAndDump("02_survival_trend.bmp", m, 1, 11000, 32, 20);
    stepAndDump("03_survival_waterfall.bmp", m, 2, 12000, 32, 20);
  }
  // 4: Nether RGB (purple accent + charging + comma count)
  {
    auto m = makeModel(fixtures[1], 2, 14000);
    stepAndDump("04_nether_rgb.bmp", m, 0, 14000, 32, 20);
  }
  // 5: No Favicon Grass Block + Pong Error
  {
    auto m = makeModel(fixtures[2], 3, 16000);
    stepAndDump("05_noicon_pongerr.bmp", m, 0, 16000, 32, 20);
  }
  // 6: Long-Press Hold B Overlay + Busy top shimmer
  {
    auto m = makeModel(fixtures[0], 4, 18000);
    m.busy = true;
    m.holdBProgress = 0.68f;
    stepAndDump("06_hold_flip_arc.bmp", m, 0, 18000, 32, 20);
  }
  // 7: TCP Error Card
  {
    auto m = makeModel(fixtures[3], 5, 36000);
    stepAndDump("07_tcp_error.bmp", m, 0, 36000, 16, 20);
  }
  // 8: Setup View
  {
    Fixture setup;
    setup.configured = false;
    setup.wifiConnected = false;
    auto m = makeModel(setup, 6, 40000);
    m.result = nullptr;
    stepAndDump("08_setup.bmp", m, 0, 40000, 16, 20);
  }

  std::printf("Dumped 8 preview frames to %s\n", outDir.string().c_str());
  return 0;
}

// --- Live Interactive Win32 Preview Window ---
struct AppState {
  std::vector<Fixture> fixtures;
  size_t idx = 0;
  uint32_t rev = 1;
  bool charging = false;
  bool flipped = false;
  bool busy = false;
  uint32_t busyUntil = 0;
  uint32_t finishedAt = 0;
  uint32_t btnBDownAt = 0;
  bool btnBHoldFired = false;
  motd::MotdUi ui;
  std::vector<uint8_t> dibPixels;
};

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
      if (lParam & (1 << 30)) break;  // ignore auto-repeat
      if (wParam == '1' || wParam == '2' || wParam == '3' || wParam == '4') {
        app->idx = static_cast<size_t>(wParam - '1');
        ++app->rev;
        app->ui.resetMotdCycle(now);
      } else if (wParam == 'A') {
        app->ui.triggerPulse(now);
        app->busy = true;
        app->busyUntil = now + 900;
      } else if (wParam == 'B') {
        app->btnBDownAt = now;
        app->btnBHoldFired = false;
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
  app.fixtures = buildFixtures();
  app.dibPixels.resize(kW * kH * 4);
  if (!app.ui.begin(nullptr, false)) return 1;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"MotdUiSimClass";
  RegisterClassExW(&wc);

  RECT rect{0, 0, kW * kScale, kH * kScale};
  AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  HWND hwnd = CreateWindowExW(
      0, wc.lpszClassName,
      L"StickS3 MOTD UI Sim (2x)  [1-4: Presets | A: Ping | B: Page / Hold Flip | C: Charge]",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
      nullptr, nullptr, wc.hInstance, &app);
  ShowWindow(hwnd, SW_SHOW);

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = kW;
  bmi.bmiHeader.biHeight = -kH;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

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
      app.finishedAt = now;
    }

    float holdProg = 0.0f;
    if (app.btnBDownAt != 0 && !app.btnBHoldFired) {
      const uint32_t held = now - app.btnBDownAt;
      if (held >= 150) holdProg = std::min(1.0f, static_cast<float>(held - 150) / 550.0f);
      if (held >= 700) {
        app.btnBHoldFired = true;
        app.flipped = !app.flipped;
        holdProg = 0.0f;
      }
    }

    const auto& f = app.fixtures[app.idx];
    auto model = makeModel(f, app.rev, now);
    model.busy = app.busy;
    model.charging = app.charging || f.charging;
    model.rotationDeg = app.flipped ? 270 : 90;
    model.holdBProgress = holdProg;
    if (app.finishedAt > 0) model.finishedAt = app.finishedAt;

    app.ui.render(model, now, false);

    auto& spr = const_cast<lgfx::LGFX_Sprite&>(app.ui.canvas());
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const auto px = spr.readPixelRGB(x, y);
        const size_t dst = static_cast<size_t>((y * kW + x) * 4);
        app.dibPixels[dst + 0] = px.b;
        app.dibPixels[dst + 1] = px.g;
        app.dibPixels[dst + 2] = px.r;
        app.dibPixels[dst + 3] = 255;
      }
    }

    HDC hdc = GetDC(hwnd);
    StretchDIBits(hdc, 0, 0, kW * kScale, kH * kScale, 0, 0, kW, kH,
                  app.dibPixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, hdc);

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
