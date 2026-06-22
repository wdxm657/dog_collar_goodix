# REASONIX.md — L-EVK-T2-GH3038Q

## Stack

- **Language**: C (ARM Cortex-M4F, FPUv4)
- **MCU**: Goodix GR5526 (BLE 5.x SoC)
- **Co-processor**: Goodix GH3036 (health monitoring AFE — HR/HRV/SPO2)
- **RTOS**: FreeRTOS (integrated via GR5526 SDK)
- **Build systems**: Keil MDK v5 (ARMCC 5.06u7) primary; CMake for ESP-IDF / Zephyr
- **BLE stack**: Goodix GR5526 SDK BLE profiles (`components/libraries/profiles/`)
- **Sensor drivers**: Accelerometer (SC7A22H), AFE (GHAFEC), HRM (SLMEMS)
- **Submodules**: 6 git submodules (see `.gitmodules`) — all from Gitee

## Layout

| Directory | Contents |
|-----------|----------|
| `platform/gr5526_sdk/` | Goodix GR5526 SDK — drivers, BLE stack, FreeRTOS port, TinyUSB, mbedTLS |
| `platform/boards/` | Board-level config (pin mux, clock, GPIO) |
| `components/core_framework/` | Thread/message/event framework, health/IMU/PHA queues |
| `components/gh3036_sdk/` | GH3036 health algorithm + driver SDK (RPC, HAL, app layer) |
| `components/sensors/` | Sensor driver wrappers (SC7A22H, GHAFEC, SLMEMS, LIS2DW12) |
| `components/os_adapter/` | OS abstraction layer (FreeRTOS/Zephyr impl of tasks, queues, timers, semaphores) |
| `components/factory/` | Factory test / production test module |
| `projects/ble_app_chelsea_a_freertos/` | Main health-monitoring app project (Keil MDK) |
| `config/` | GH3036 algorithm config files (HR/HRV/SPO2/NADT/PPG noise profiles) |

## Commands

No package.json / Makefile at root — build is **Keil MDK v5 only** for the main app:

- Open `projects/ble_app_chelsea_a_freertos/Keil_5/ble_app_chelsea_a_freertos.uvprojx` in µVision
- Target name: `chelsea_a_exclusive`
- ARM Compiler: ARMCC V5.06 update 7 (build 960)
- Debug: Segger J-Link (`JLinkSettings.ini` present)
- CMake builds available for components (`components/*/CMakeLists.txt`) targeting ESP-IDF or Zephyr

## Conventions

- **Header guards**: `__GH_*_H__` pattern (e.g. `__GH_APP_THREAD_H__`)
- **Prefixes**: `app_` for core_framework, `gh_` / `ghal_` / `gha_` for GH3036 SDK, `osal_` for OS abstraction
- **Copyright**: Goodix (2019) on SDK code; XiaoPb on component-layer code
- **Commit style**: Conventional Commits configured in `.vscode/settings.json` (scopes: `sdk`, `components`)
- **Doxygen**: Standard `@file`, `@brief`, `@attention` blocks on all sources
- **Licenses**: MIT (component layer), Goodix proprietary (SDK layer)

## Watch out for

- **6 git submodules** — `git clone --recursive` or `git submodule update --init` is required; missing submodules cause build failures
- **Keil .uvprojx is the only build entry** — no GCC/Makefile at project root; component-level CMakeLists are for ESP-IDF/Zephyr, not the app
- `Objects/` and `Listings/` are build artifacts (gitignored); `.uvguix.*` and `JLink*.txt/ini` are user-local (gitignored)
- `platform/gr5526_sdk/` contains 3rd-party libs (TinyUSB, mbedTLS, FatFS, LVGL, Unity) — don't edit those directly
- GH3036 algorithm config files in `config/` are sensor-calibration-specific; changing them affects HR/SPO2 accuracy
