#pragma once
#include <Arduino.h>

// ---- 拆分时抽出的共享类型(多 tab 自动原型生成需要类型先可见) ----
enum Screen : uint8_t { SCR_CONNECTING, SCR_DASHBOARD, SCR_MENU, SCR_ADJUST,
                        SCR_GESTURE, SCR_TURBO_DASH, SCR_DETAILS, SCR_CONN_MGMT,
                        SCR_SETTINGS, SCR_POW, SCR_CALIB };

// 菜单(设计 §3 循环顺序 + 设置子菜单)
enum MenuType : uint8_t { M_PERCENT, M_MINUTES, M_TOGGLE, M_LIGHT, M_VIEW, M_BACK, M_SUBMENU, M_SECONDS, M_POWPAGE, M_CALIBPAGE };
enum ConnKind : uint8_t { CI_RESCAN, CI_DISCONN, CI_DEVICE, CI_BACK };
// 调节目标(替代菜单下标, 主菜单/子菜单统一)
enum EditTarget : uint8_t { ET_NONE, ET_SPEED, ET_TIMER, ET_NATURE, ET_LIGHT,
                            ET_TURBOTIME, ET_SHUTDOWN, ET_GEARDOWN, ET_BLESN };
struct MenuItem { const char* name; MenuType type; EditTarget target; };
