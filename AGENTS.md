# Repository Guidelines

## What This Repo Is

Not a single code project — an **M5Stack Arduino workspace**. This directory is the project root; each subdirectory is an independent Arduino project. Currently it holds a curated knowledge base (`kb/`) and one compile-verified sketch (`espnow-smoke/`).

Target hardware (both ESP32-S3, both 8MB Flash):

| Device | FQBN (m5stack:esp32 core 3.3.8) | Key facts |
|---|---|---|
| M5StickS3 (K150) | `m5stack:esp32:m5stack_sticks3` | 8MB OPI PSRAM, M5PM1 PMU (**not** AXP192), ST7789P3 135×240, BMI270 |
| Cardputer-Adv (K132-Adv) | `m5stack:esp32:m5stack_cardputer` | **No PSRAM**, shares FQBN with Cardputer v1.0/v1.1 (lib auto-detects), TCA8418 keyboard, microSD, ST7789V2 240×135 |

## The Knowledge Base Is Load-Bearing — Read Before Writing Code

`kb/` (all Chinese markdown, official-docs-sourced) is the authoritative reference. Consult in this order:

1. **`kb/demos-*.md`** — how the official examples do it (20 pages digested: 10 per device)
2. **`kb/lib-*.md`** — exact API signatures verified against locally installed library source
3. **`kb/m5stick-s3.md` / `kb/cardputer-adv.md`** — pin tables, hardware pitfalls
4. **`kb/README.md`** — index + 7 "vibe coding iron rules"
5. Anything not covered: state it as `[INFERENCE]` / `[未确认]`, never invent APIs

## Rules That Break Builds/Behavior If Violated

- **One `begin()`**: `M5Cardputer.begin()` internally calls `M5.begin()` — never call both
- **`M5.update()` / `M5Cardputer.update()` must be called every `loop()`** — buttons/keyboard silently die otherwise
- **Speaker ↔ Mic are mutually exclusive**: `Speaker.end()` before `Mic.begin()` and vice versa
- **Never instantiate `M5GFX` directly under M5Unified** — use `M5.Display`; `M5Canvas` defaults `psram=true`, call `setPsram(false)` on Cardputer-Adv
- **StickS3 Grove/Hat 5V output is OFF by default** — `M5.Power.setExtOutput(true)` or peripherals get no power; StickS3 PMU is M5PM1 (I2C 0x6E), all AXP192 sample code from old StickC tutorials is wrong here
- **ESP-NOW on core 3.x changed callback signatures** — send cb takes `const wifi_tx_info_t*`, recv cb takes `const esp_now_recv_info_t*`; most online tutorials are stale (see `kb/esp-now.md`)
- **Cardputer-Adv cannot read charging status** (no PMU, G10 ADC voltage only); StickS3 can (via PM1 GPIO0)

## Build & Verify

arduino-cli 1.5.1 and pio 6.1.19 are on user PATH (reopen terminal if missing). Verification = **compile as smoke test**; there are no unit tests, no CI.

```bash
# StickS3
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 .\MySketch

# Cardputer / Cardputer-Adv (add partition option when firmware > 1.25MB)
arduino-cli compile --fqbn m5stack:esp32:m5stack_cardputer .\MySketch
arduino-cli compile --fqbn m5stack:esp32:m5stack_cardputer --board-options PartitionScheme=default_8MB .\MySketch

# Upload (device must be in download mode)
arduino-cli upload --fqbn <FQBN> -p COMx .\MySketch
```

Partition default differs per board: StickS3 → `default_8MB` (3.2MB app), Cardputer → `default` (1.25MB app). "Sketch too big" on Cardputer almost always means you forgot `PartitionScheme=default_8MB`.

Download mode: **StickS3** = hold side reset ~2s until green LED blinks. **Cardputer-Adv** = power switch OFF, hold G0, plug USB-C, release.

Locally installed libraries (API KBs match these exact versions): M5Unified 0.2.19, M5GFX 0.2.26, M5PM1 1.0.7, M5Cardputer 1.1.1, at `C:\Users\gameg\Documents\Arduino\libraries\` — read source there when KB is ambiguous.

## Repo Conventions

- New projects = new subdirectory at root; they get git-tracked automatically
- `.gitignore`: `M5Burner-v3-beta-win-x64/` (flashing tool, not project content), build artifacts (`build/`, `*.bin`, `*.elf`, …), `.pio/`
- `.gitattributes`: `* text=auto`, `*.ino` forced LF (Windows editors must not inject CRLF)
- Working OS is Windows; shell is POSIX-flavored (bash) — cmd built-ins like `if exist` fail
- Keep KB files in Chinese, code comments may be English/Chinese; update `kb/README.md` index when adding KB files

## Project Layout (fixed convention)

A **project** is a root subdirectory containing per-target sketch branches and a project-local library:

```
<project>/
├── lib/<name>/            ← project-local Arduino library (library.properties + src/)
├── <branch>/<branch>.ino  ← one sketch per target/form factor; folder name == .ino name
│   (e.g. w96p-remote/{demo, sticks3, cardputer}/)
├── tools/<tool>/<tool>.ino ← debug/calibration utilities (e.g. tools/imu-calib)
└── docs/                  ← design docs
```

- **`lib/` (repo root) = workspace-shared libraries** spanning multiple projects; project-only libraries live in `<project>/lib/`, never in root `lib/`
- Compile with `--libraries` (repeatable, project lib first):

```bash
arduino-cli compile --libraries ./<project>/lib --libraries ./lib --fqbn <FQBN> <project>/<branch>
```

- Rule: sketch code includes shared headers as `<lib_header.h>` (angle brackets), never relative paths into another branch

## Agent skills

本工作区按**项目自治 + 根部兜底**组织 agent 配置：每个 `<project>/` 可以有自己的 `AGENTS.md`、`CONTEXT.md`、`docs/adr/`、`.scratch/` issue tracker；根部文件管跨项目约定。项目级文件存在时优先于根部。

### Issue tracker

逐项目本地 markdown：`<project>/.scratch/<feature>/`，一票一文件，`Status:` 行记状态。See `docs/agents/issue-tracker.md`.

### Triage labels

默认五角色（`needs-triage` / `needs-info` / `ready-for-agent` / `ready-for-human` / `wontfix`），经 `Status:` 行落地。See `docs/agents/triage-labels.md`.

### Domain docs

项目级优先（`<project>/CONTEXT.md` + `<project>/docs/adr/`），根部 `CONTEXT.md` + `docs/adr/` 兜底，硬件事实以 `kb/` 为权威。See `docs/agents/domain.md`.
