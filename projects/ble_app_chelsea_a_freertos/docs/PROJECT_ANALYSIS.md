# ChelseaA_OS 项目分析文档

> 本文档基于 `ble_app_chelsea_a_freertos` 项目目录结构、源代码分析编写，
> 用于指导后续向其他硬件平台或 SDK 的移植开发工作。
>
> 生成日期: 2026-06-15

---

## 目录

1. [项目概述](#1-项目概述)
2. [硬件平台](#2-硬件平台)
3. [软件架构总览](#3-软件架构总览)
4. [目录结构详解](#4-目录结构详解)
5. [关键数据流](#5-关键数据流)
6. [任务与线程](#6-任务与线程)
7. [BLE 服务与协议](#7-ble-服务与协议)
8. [配置体系](#8-配置体系)
9. [依赖组件清单](#9-依赖组件清单)
10. [IO 引脚映射表](#10-io-引脚映射表)
11. [移植要点](#11-移植要点)

---

## 1. 项目概述

| 项目 | 说明 |
|------|------|
| **项目名称** | ChelseaA_OS |
| **MCU 平台** | GOODIX GR5526 (Cortex-M4F BLE SoC) |
| **健康传感器** | GOODIX GH3036 (光学 AFE) |
| **加速度计** | ST LIS2DW12 (I2C) |
| **RTOS** | FreeRTOS V10.4.0 |
| **构建工具** | Keil MDK (uvprojx) |
| **BLE 协议栈** | GOODIX GR5526 SDK BLE Stack |
| **算法覆盖** | ADT(佩戴检测), HR(心率), HRV(心率变异性), SPO2(血氧), GNADT(非接触式佩戴检测) |

### 1.1 BLE 设备信息

| 参数 | 值 |
|------|-----|
| 设备名称 | `ChelseaA_OS` |
| 广播 UUID | OTA Service (0x19, 0x0E UUID) |
| 制造商 ID | GOODIX (0x04F7) |
| 连接间隔 | 200ms ~ 325ms (320~520 × 0.625ms) |
| 最大连接数 | 5 |

---

## 2. 硬件平台

### 2.1 MCU: GOODIX GR5526

- 内核: ARM Cortex-M4F @ 96MHz（可通过 custom_config.h 选择 96/64/48/24/16MHz）
- Flash: 1MB（代码加载地址: 0x00220000）
- SRAM: 256KB
- PSRAM: 通过 OSPI 接口扩展（0x1C000000 起始, `GFX_MEM_SIZE`）
- BLE 5.2 双模

### 2.2 外设连接

| 外设 | 接口 | 引脚 | 备注 |
|------|------|------|------|
| GH3036 AFE | **SPI** (主用, SW CS) | CS:GPIOA_7, CLK:GPIOA_4, MOSI:GPIOA_5, MISO:GPIOA_6 | 有 I2C 回退机制 (I2C_ID_4, SCL:GPIOA_4, SDA:GPIOA_5) |
| GH3036 复位 | GPIO | AON_GPIO_10 | 硬复位使能 |
| GH3036 中断 | GPIO | AON_GPIO_0 | 上升沿触发 |
| LIS2DW12 加速度计 | I2C (ID_5) | SCL:GPIOA_8, SDA:GPIOA_9 | 地址: 0x19 |
| LIS2DW12 中断 | GPIO | AON_GPIO_2 | 上升沿触发(FIFO Watermark) |
| SW1 按键 | AON_GPIO_5 | UP 键 | 单机启停 HR/HRV |
| SW2 按键 | AON_GPIO_6 | DOWN 键 | 单机启停 SPO2 |
| SW3 按键 | AON_GPIO_7 | OK 键 | 单机启停 TEST1 / 长按模拟运动 |

---

## 3. 软件架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                       应用层 (Application)                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ main.c   │  │ user_app │  │ watcher  │  │ custom_config  │  │
│  │ (入口)   │  │ (BLE)    │  │ (监控)   │  │ (配置)         │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                  框架层 (Core Framework)                          │
│  ┌──────────┐  ┌──────────────┐  ┌────────┐  ┌───────────────┐  │
│  │ app_     │  │ app_thread_  │  │ app_   │  │ app_thread_   │  │
│  │ thread   │  │ health       │  │ mqueue │  │ imu/phas/     │  │
│  │          │  │              │  │        │  │ factory       │  │
│  └──────────┘  └──────────────┘  └────────┘  └───────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                GH3036 SDK 层 (Goodix Health SDK)                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ gh_app   │  │ gh_hal   │  │ gh_algo  │  │ gh_protocol    │  │
│  │ (应用)   │  │ (HAL)    │  │ (算法)   │  │ (RPC通信)      │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│             OS 抽象层 (OS Abstraction Layer)                       │
│  ┌─────────────────────┐  ┌──────────────────────────────────┐  │
│  │ osal (task/queue/   │  │ osal_event / osal_priority_queue │  │
│  │  mutex/sema/timer)  │  │ osal_psram (PSRAM management)    │  │
│  └─────────────────────┘  └──────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│               GOODIX GR5526 SDK 平台层                            │
│  ┌─────────────────────┐  ┌──────────────────────────────────┐  │
│  │ HAL (GPIO/SPI/I2C/  │  │ BLE Stack (GAP/GATT/Profiles)   │  │
│  │  UART/DMA/Timer/RTC) │  │ FreeRTOS / LVGL / mbedTLS      │  │
│  └─────────────────────┘  └──────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. 目录结构详解

### 4.1 主程序 `Src/user/` —— 应用入口与 BLE 管理

| 文件 | 职责 |
|------|------|
| `main.c` | 系统入口。初始化外设 → BLE 协议栈 → 创建任务(vStartTasks) → 启动调度器。包含按键事件处理（按键映射详见底部注释） |
| `user_app.c` / `.h` | BLE 应用核心：广播配置、连接管理、服务注册(health + hrs + dfu)、BLE 事件分发。设备名 "ChelseaA_OS" |
| `watcher.c` | 运行时统计监控任务。每 100ms 打印各任务 CPU 占用率 |
| `FreeRTOSConfig.h` | FreeRTOS 配置：抢占式调度，tick 1000Hz，堆 40KB，支持定时器/互斥量/计数信号量 |

### 4.2 `Src/config/` —— 应用配置

| 文件 | 职责 |
|------|------|
| `custom_config.h` | 芯片选型(SOC_GR5526)、系统时钟、BLE 资源(最大连接数/绑定数/广播数)、Flash/内存布局、日志开关、PMU校准等 |

### 4.3 `Src/platform/` —— 平台初始化

| 文件 | 职责 |
|------|------|
| `user_periph_setup.c` / `.h` | `app_periph_init()`: 板级初始化、DFU 端口、日志存储(Flash)、电源管理模式。提供 `gh_chip_version_get()` 和 `gh_ble_version_get()` |

### 4.4 `Src/profiles/health/` —— 自定义 BLE 健康服务

| 文件 | 职责 |
|------|------|
| `health.c` / `.h` | 128-bit UUID 自定义 GATT 服务。包含 TX 特性(Notify)和 RX 特性(Write)。用于承载 GH RPC 协议数据。最大数据长度 247 字节，最大 10 连接 |

**GATT 结构：**

```
HEALTH Service (UUID: 0x19, 0x0E...)
├── TX Characteristic (UUID: 0x03, 0x00...) → Notify
│   └── Client Characteristic Configuration (CCCD)
└── RX Characteristic (UUID: 0x04, 0x00...) → Write
```

### 4.5 `Src/sensor/lis2dw12/` —— 加速度计驱动

| 文件 | 职责 |
|------|------|
| `lis2dw12.c` / `.h` | ST LIS2DW12 加速度计驱动。I2C 接口，FIFO 模式(Stream-to-FIFO)，中断触发。提供 `imu_register_api()` 向框架注册 IMU 回调 |

**配置参数：**
- ODR: 25Hz (默认)
- Full Scale: ±4g
- FIFO Watermark: 25 采样点
- 中断: AON_GPIO_2 上升沿 (FIFO Threshold)

### 4.6 `Src/gh3036sdk/` —— GH3036 SDK 移植层

#### 4.6.1 GH3036 SDK 移植文件（主程序目录中的特化文件）

| 文件 | 职责 |
|------|------|
| `gh_app_user_for_gr5526.c` | 应用层回调实现：ISR 处理、配置下载、采样控制、数据发布（算法结果分发）、动作事件（佩戴/移动）发布。HR 结果通过 `hrs_heart_rate_measurement_send` 推送至标准 HRS 服务 |
| `gh_hal_user_for_gr5526.c` | HAL 层移植：SPI / I2C 初始化与读写、中断引脚初始化、硬件复位、时间戳获取(基于 RTC)、延时函数、互斥量 |
| `gh_protocol_user_for_gr5526.c` | 协议层移植：创建独立 RPC 线程(优先级10, 栈 512 words)，通过优先级队列收发 GH RPC 数据包，通过 HEALTH BLE Service 传输 |
| `gh_hal_io_config_for_gr5526.h` | 引脚配置头文件：SPI(CS/CLK/MOSI/MISO) 和 I2C(SCL/SDA) 两组引脚定义，支持软件 SPI 回退 |
| `gh_demo.c` / `.h` | SDK 初始化编排：尝试 SPI → 失败回退 I2C、配置切换、启停控制、efuse 读取。调用 `health_register_api()` 向框架注册健康回调 |
| `gh_gsensor_bridge.c` / `.h` | 加速度计数据桥接：将 IMU 线程的加速度数据转发给 GH SDK |
| `gh_reg_lists.c` / `.h` | 寄存器配置表：4 组配置(g_reg_list0~3)，由 `CONFIG_L_EVK_T2_GH3038Q` 宏选择不同的硬件参数 |

#### 4.6.2 SDK 注册流程

```
health_register_api()       → gh_demo.c (初始化/启停/ISR/配置切换)
imu_register_api()          → lis2dw12.c (加速度计操作)
imu_register_publish_hook() → gh_gsensor_bridge.c (数据桥接)
```

### 4.7 `Src/os_adapter/` —— OSAL 扩展

| 文件 | 职责 |
|------|------|
| `osal_event.c/h` | 事件结构体：类型、优先级、数据、重试计数 |
| `osal_priority_queue.c/h` | 优先级队列：高优先级(HIGH)和低优先级(LOW)两条链表，用信号量同步 |
| `osal_psram.c/h` | PSRAM 管理：初始化 OSPI、通过 `app_graphics_mem_*` 分配/释放 PSRAM，用于大缓冲区 |

---

## 5. 关键数据流

### 5.1 健康传感器数据流

```
GH3036 AFE (PPG/SPO2)
    │
    ├── SPI 中断 (AON_GPIO_0)  →  app_mqueue_send_event(MQ_EVENT_HEALTH_INT_UPDATE)
    │                              →  health_thread 处理 ISR
    │                              →  gh_hal_isr() 读取 FIFO
    │                              →  gh_demo_ghealth_data_set() 送入融合模块
    │
    ├── GH App Manager (融合模块)
    │   ├── 融合加速度数据 (来自 gh_gsensor_bridge)
    │   └── 运行算法 (HR/HRV/SPO2/ADT/NADT)
    │
    └── gh_demo_data_publish()
        ├── 算法结果 → gh_protocol_process() → RPC → BLE Health TX → 手机
        ├── HR 结果 → hrs_heart_rate_measurement_send() → 标准 HRS Service
        ├── HRV 结果 → hrs_rr_interval_add() → 标准 HRS Service
        ├── 佩戴事件 → health_wear_update_event_send()
        └── 数据日志 (DEBUG_LOG)
```

### 5.2 加速度计数据流

```
LIS2DW12 (I2C, 25Hz)
    │
    ├── FIFO Watermark 中断 (AON_GPIO_2 rising)
    │       → IMU 线程读取 FIFO
    │       → imu_data 数组
    │       → gh_gsensor_bridge_publish()
    │       → gh_demo_gsensor_data_set() → 融合模块
    │
    └── IMU 线程定时 (基于 RTC tick)
            → lis2dw12_read_data_api()
            → 同上链路
```

### 5.3 手机命令流（BLE → GH RPC）

```
手机 App
    │
    └── BLE Write → Health Service RX Characteristic
            │
            └── health_evt_handler(HEALTH_EVT_RX_DATA_RECEIVED)
                    └── gh_protocol_data_recevice()
                            └── osal_priority_queue(HIGH)
                                    └── gh_rpc_thread
                                            └── GHRPC_process()
                                                    └── 解析命令、设置参数、切换模式等
```

### 5.4 上行数据流（GH RPC → BLE）

```
GH App Manager (算法结果)
    │
    └── gh_protocol_process()
            │
            └── GHRPC 打包
                    └── gh_protocol_data_send()
                            └── osal_priority_queue(LOW)
                                    └── gh_rpc_thread
                                            └── health_tx_data_send(Notify)
```

---

## 6. 任务与线程

| 任务名称 | 栈大小 | 优先级 | 职责 |
|----------|--------|--------|------|
| `app_thread` | 6144 字节 | 默认 | **主线任务**：处理 RTC tick、健康事件(health)、IMU 事件(imu)、PHAS 事件(phas)、工厂测试事件(factory) |
| `dfu_schedule_task` | 256~2048 word | configMAX-2 | DFU 固件升级调度 |
| `gh_rpc` | 512 word | 10 | GH RPC 协议处理：从优先级队列收到事件并处理收/发 |
| `log_store_dump_task` | 512 word | configMAX-3 | 日志存储 (可选) |
| `Goodix_Watcher_Task` | - | - | 运行时统计：每 100ms 打印各任务 CPU 占用率 |
| IDLE | 最小 | 0 | FreeRTOS 空闲任务 |

### 6.1 事件驱动模型

系统使用 `app_mqueue` 事件队列驱动各模块：

```
RTC Tick (1s)
    → MQ_EVENT_RTC_TICK
    → app_thread 主循环
    → 分发给 health / imu / phas / factory 子模块
```

健康传感器中断 => `MQ_EVENT_HEALTH_INT_UPDATE`
工厂模式定时器 => `MQ_EVENT_IMU_TIMER`

---

## 7. BLE 服务与协议

### 7.1 注册的 BLE 服务

| 服务 | UUID | 说明 |
|------|------|------|
| HEALTH (自定义) | 128-bit: `0E190000-...-5F9B34FB` | GH 协议数据传输 (TX Notify + RX Write) |
| HRS (心率服务) | 0x180D | 标准心率服务，HR 结果推送 |
| OTAS (OTA 服务) | 128-bit | DFU 固件升级 |

### 7.2 GH 协议 (RPC)

- 基于自定义的 GHRPC 框架（`components/gh3036_sdk/gh_protocol/`）
- 通过 BLE HEALTH Service 透传
- 协议数据包通过优先级队列区分：
  - **HIGH 优先级**: 手机发来的控制命令（实时响应）
  - **LOW 优先级**: 算法结果上行数据（可容忍延迟）

---

## 8. 配置体系

### 8.1 配置层次

```
custom_config.h        → 芯片选型、内存布局、BLE 资源、时钟
gh_global_config.h     → GH SDK 功能开关 (HR/HRV/SPO2/NADT/ADT)
gh_hal_config.h        → GH HAL 配置 (接口类型 SPI/I2C、AGC、日志)
gh_reg_lists.c         → GH3036 寄存器配置表 (多组配置)
```

### 8.2 GH SDK 功能开关 (`gh_global_config.h`)

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `GH_FUNC_ADT_EN` | 1 | 佩戴检测 |
| `GH_FUNC_HR_EN` | 1 | 心率 |
| `GH_FUNC_SPO2_EN` | 1 | 血氧 |
| `GH_FUNC_HRV_EN` | 1 | 心率变异性 |
| `GH_FUNC_GNADT_EN` | 1 | 非接触佩戴检测(Green) |
| `GH_FUNC_IRNADT_EN` | 0 | 非接触佩戴检测(IR) |
| `GH_FUNC_TEST1_EN` | 1 | 测试模式1 |
| `GH_FUNC_TEST2_EN` | 1 | 测试模式2 |

### 8.3 算法版本选择

| 宏 | 默认值 | 可选值 |
|----|--------|--------|
| `GOODIX_HR_ALGO_VERISON` | EXCLUSIVE (4) | BASIC(1), MEDIUM(2), PREMIUM(3), EXCLUSIVE(4) |
| `GOODIX_SPO2_ALGO_VERISON` | EXCLUSIVE (4) | 同上 |

### 8.4 多组寄存器配置

`gh_reg_lists.c` 包含 4 组配置：
- `g_reg_list0`：默认配置 / `CONFIG_L_EVK_T2_GH3038Q` 配置
- `g_reg_list1`：配置1（不同 LED 选择/电流/RX 参数）
- `g_reg_list2`：配置2
- `g_reg_list3`：空配置（仅虚拟寄存器 0xFFFF=0x0004）

每组配置对应不同的采样参数和算法通道分配。通过 `gh_app_demo_cfg_switch(index)` 切换。

---

## 9. 依赖组件清单

> 路径前缀: `components/`

| 组件 | 路径 | 说明 | 移植必需 |
|------|------|------|----------|
| **core_framework** | `components/core_framework/` | 应用线程框架、消息队列、IMU/Health/PHAS/Factory 子模块 | **是** |
| **gh3036_sdk** | `components/gh3036_sdk/` | GH3036 健康传感器 SDK (算法+协议+HAL) | **是** |
| **os_adapter** | `components/os_adapter/` | OS 抽象层 (FreeRTOS/Zephyr) | **是** |
| **sensors** | `components/sensors/` | 传感器驱动 (使用 LIS2DW12) | **是** |
| **factory** | `components/factory/` | 工厂测试模块 | 可选 |
| platform/gr5526_sdk | `platform/gr5526_sdk/` | GOODIX GR5526 SDK (HAL/BLE/Profiles/FreeRTOS...) | **否**（目标平台替换） |

### 9.1 组件间接口依赖

```
core_framework
  ├── 依赖 os_adapter (osal_*)
  ├── 依赖传感器驱动 (imu_register_api / imu_register_publish_hook)
  └── 依赖健康驱动 (health_register_api / health_register_api_ctx)

gh3036_sdk
  ├── 依赖 os_adapter (osal_*)
  └── 需要 HAL 移植 (SPI/I2C/GPIO/Timer/RTC)

platform/gr5526_sdk (移植时需替换)
  ├── FreeRTOS (kernel + heap_4)
  ├── BLE Stack (ble_*.h)
  ├── HAL (app_io, app_spi, app_i2c, app_rtc, app_dma, app_gpiote, ...)
  ├── app_log / app_error / app_timer / app_scheduler / ...
  ├── board_SK (板级支持)
  ├── dfu_port / hal_flash (DFU和Flash操作)
  ├── graphics (显示相关，当前未启用)
  └── 外部库：LVGL, mbedTLS, TinyUSB, FAT FS, SEGGER RTT
```

---

## 10. IO 引脚映射表

| 功能 | 引脚 | 类型 | MUX | 备注 |
|------|------|------|-----|------|
| GH3036_CS | GPIOA_7 | AON? | MUX/GPIO | SPI 片选 (SW CS) |
| GH3036_CLK | GPIOA_4 | GPIOA | MUX_1 | SPI 时钟 |
| GH3036_MOSI | GPIOA_5 | GPIOA | MUX_1 | SPI MOSI |
| GH3036_MISO | GPIOA_6 | GPIOA | MUX_1 | SPI MISO |
| GH3036_RST | AON_GPIO_10 | AON | MUX | 硬件复位 |
| GH3036_INT | AON_GPIO_0 | AON | MUX | 中断(上升沿) |
| LIS2DW12_SCL | GPIOA_8 | GPIOA | MUX_0 | I2C 时钟 (I2C_ID_5) |
| LIS2DW12_SDA | GPIOA_9 | GPIOA | MUX_0 | I2C 数据 |
| LIS2DW12_INT | AON_GPIO_2 | AON | MUX | 中断(上升沿) |
| SW1 (UP) | AON_GPIO_5 | AON | - | HR/HRV 启停 |
| SW2 (DOWN) | AON_GPIO_6 | AON | - | SPO2 启停 |
| SW3 (OK) | AON_GPIO_7 | AON | - | TEST1 启停/运动模拟 |

---

## 11. 移植要点

### 11.1 移植到新 MCU 平台的核心步骤

#### 步骤 1: 替换平台层

将 `platform/gr5526_sdk/` 替换为目标 MCU 的 SDK，需提供以下等效功能：

**必选驱动：**
- [ ] GPIO 控制（`app_io_init/read/write`、`app_gpiote_init` 中断回调）
- [ ] SPI 主模式（`app_spi_init`、`app_spi_dma_transmit_async`、`app_spi_dma_receive_async`）
- [ ] I2C 主模式（`app_i2c_init`、`app_i2c_transmit_sync`、`app_i2c_receive_sync`）
- [ ] RTC / 日历（`app_rtc_init`、`app_rtc_get_time`、`app_rtc_setup_tick`）
- [ ] DMA（SPI TX/RX）
- [ ] 定时器 / 延时（`delay_ms`、`delay_us`）
- [ ] FreeRTOS 移植（`portable/GCC/ARM_CM4F` 或等效）
- [ ] BLE Stack 及 GATT API（`ble_gatts_*`、`ble_gap_*`、`ble_evt_handler` 注册）

**可选（但项目中使用）：**
- [ ] OSPI / PSRAM（`osal_psram.c` 依赖 `app_graphics_ospi`、`app_graphics_mem`）
- [ ] Flash 操作（`hal_flash`、日志存储）
- [ ] DFU 固件升级（`dfu_port`）

#### 步骤 2: 适配 OSAL

`components/os_adapter/` 通过 OSAL 抽象层包装 FreeRTOS API。

`os_impl/FreeRTOS/` 下是 FreeRTOS 的具体实现，移植时：
- 如果目标平台使用 FreeRTOS：直接复用 `os_impl/FreeRTOS/`
- 如果目标平台使用其他 RTOS：需要重新实现 `os_impl/<RTOS>/` 下的 API
- 如果目标平台是 Zephyr：已预制 `os_impl/zephyr/` 实现

**OSAL 核心 API 清单：**

| 类别 | 需要实现的函数 |
|------|---------------|
| Task | `osal_task_create/delete/suspend/resume/delay/delay_ms` |
| Queue | `osal_queue_create/delete/send/receive` |
| Mutex | `osal_mutex_create/delete/take/give` |
| Semaphore | `osal_sema_binary_create / countings_create / delete / take / give` |
| Timer | `osal_timer_create/delete/start/stop` |
| Heap | `osal_heap_malloc/free` |
| Log | `osal_log_*` |

#### 步骤 3: 适配 GH3036 HAL（`gh_hal_user_for_gr5526.c`）

重写以下函数，使用目标平台的驱动 API：

```c
gh_hal_spi_init()       // SPI 初始化 + DMA
gh_hal_spi_write()      // SPI 写 (同步/异步)
gh_hal_spi_read()       // SPI 读 (同步/异步) - SW CS 模式需要
gh_hal_spi_cs_ctrl()    // CS 引脚控制 (SW CS)
gh_hal_i2c_init()       // I2C 初始化
gh_hal_i2c_write()      // I2C 写
gh_hal_i2c_read()       // I2C 读
gh_hal_int_pin_init()   // 中断引脚初始化并注册回调
gh_hal_reset_pin_init() // 复位引脚初始化
gh_hal_reset_pin_ctrl() // 复位引脚控制
gh_hal_get_timestamp()  // 时间戳 (毫秒级, 基于 RTC)
gh_hal_delay_ms()       // 毫秒延时
gh_hal_delay_us()       // 微秒延时
```

#### 步骤 4: 适配加速度计驱动

- 将 `Src/sensor/lis2dw12/` 中的 I2C 驱动替换为目标 MCU 的 I2C API
- 如果使用不同型号的加速度计，需要按 `app_thread_imu.h` 中的 `imu_register_api_ctx()` 回调接口封装

**IMU 注册回调接口：**

```c
typedef struct {
    void (*init)(void);
    void (*start)(void);
    void (*stop)(void);
    int32_t (*read_data)(imu_data_t* data, uint8_t* len, uint64_t timestamp);
    void (*set_fs)(uint32_t fs);
    void (*set_dl)(uint32_t dl);
    void (*set_odr)(uint32_t odr);
    void (*set_fifo)(uint32_t fifo_watermark);
} imu_api_t;
```

#### 步骤 5: 适配 BLE 服务

- 确认目标平台 BLE Stack 支持自定义 128-bit UUID 服务
- 移植 `profiles/health/` 中的 GATT 服务（TX Notify + RX Write）
- 移植 HRS 标准心率服务
- 移植 OTAS (DFU) 服务
- 确认 ATT MTU 支持 ≥ 247 字节（HEALTH 最大数据长度）

#### 步骤 6: 配置调整

- 修改 `custom_config.h`：芯片类型、时钟、内存布局、BLE 资源参数
- 修改 `gh_hal_io_config_for_gr5526.h`：引脚映射
- 修改 `gh_hal_config.h`：接口类型(SPI/I2C)、采样参数
- 修改 `gh_reg_lists.c`：寄存器配置表（通常不需要修改）

### 11.2 常见移植问题及检查点

| 问题 | 检查点 |
|------|--------|
| SPI 通信失败 | 检查 SPI 极性(POLARITY_LOW)、相位(1EDGE)、时钟分频(16分频→6MHz@96MHz) |
| I2C 回退失败 | `GH_INTERFACE_TYPE` 切换、I2C 地址(0x38 >> 1 = 0x1C 7bit) |
| 中断不触发 | 检查 GPIO 中断配置(上升沿)、NVIC 使能 |
| 时间戳异常 | `gh_hal_get_timestamp()` 需返回基于 RTC 的毫秒级 UTC 时间戳 |
| BLE 传输失败 | MTU 需 ≥ 247、确认 Notify CCCD 已使能 |
| 算法不工作 | 确认 `gh_global_config.h` 中对应功能宏已启用(默认均已启用) |
| 内存不足 | `configTOTAL_HEAP_SIZE` 40KB + PSRAM 用于大缓冲区；GH SDK 算法内存池由 `GH_USE_DYNAMIC_ALGO_MEM` 控制 |

### 11.3 关键编译宏

| 宏 | 定义位置 | 说明 |
|----|----------|------|
| `SOC_GR5526` | custom_config.h | 芯片型号 |
| `OS_ADAPTER_RTOS_FREERTOS` | 编译器/Keil | 选择 FreeRTOS OSAL 实现 |
| `GH_CHIP_TYPE` | gh_global_config.h | GH3036 = 3 |
| `GH_INTERFACE_TYPE` | gh_hal_config.h | `GH_INTERFACE_SPI_SW_CS` / `GH_INTERFACE_I2C` |
| `GH_ISR_MODE` | gh_hal_config.h | `INTERRUPT_MODE` / `POLLING_MODE` |
| `CONFIG_L_EVK_T2_GH3038Q` | 编译器/Keil | 选择 GH3038Q 硬件配置表 |
| `APP_DRIVER_USE_ENABLE` | custom_config.h | 使能 APP 驱动层 |
| `APP_LOG_ENABLE` | custom_config.h | 使能日志 |

### 11.4 系统启动流程

```
main()
  ├── app_periph_init()          // 硬件初始化(board_init + DFU + 日志)
  ├── ble_stack_init()           // BLE 协议栈初始化 + 注册 ble_evt_handler
  ├── xTaskCreate(vStartTasks)   // 创建启动任务
  └── vTaskStartScheduler()      // 启动调度器

vStartTasks()
  ├── app_thread_init()          // 初始化主应用线程（含 health/imu/phas/factory 子模块）
  │   ├── health_register_api()  → gh_demo.c (健康SDK注册)
  │   ├── imu_register_api()     → lis2dw12.c (加速度计注册)
  │   └── gh_gsensor_bridge_init() (数据桥接初始化)
  ├── app_calendar_init()        // RTC 日历初始化
  ├── xTaskCreate(dfu_schedule)  // DFU 任务
  └── vTaskDelete(NULL)          // 删除自身

BLE 协议栈初始化完成
  └── ble_evt_handler(BLE_COMMON_EVT_STACK_INIT)
      └── ble_app_init()
          ├── services_init()    // 注册 HEALTH/HRS/OTAS 服务
          ├── gap_params_init()  // 设置广播参数
          └── ble_gap_adv_start() // 开始广播
```

---

## 附录 A: 关键数据结构依赖关系

```
app_thread_init()
  ├── app_mqueue_init()          // 主事件队列
  ├── health_thread_init()       // 健康子模块
  │   ├── health_register_api()  // 注册 GH SDK 回调
  │   └── 创建定时器、初始化 GH SDK
  ├── imu_thread_init()          // IMU 子模块
  │   ├── imu_register_api()     // 注册传感器驱动
  │   └── gh_gsensor_bridge_init() // 注册数据发布回调
  ├── phas_thread_init()         // PHAS 子模块 (GoMore?)
  └── factory_thread_init()      // 工厂测试子模块

gh_app_demo_init()
  ├── gh_demo_init()             // GH SDK 应用层初始化
  ├── gh_hal_i2c_switch_to_spi() // 尝试 SPI（失败回退 I2C）
  │   ├── gh_hal_spi_init()
  │   └── gh_hal_service_init()
  ├── gh_gsensor_bridge_init()   // 加速度计桥接
  ├── gh_protocol_init()         // RPC 协议初始化（创建 gh_rpc 线程）
  ├── 读取 efuse
  ├── gh_app_demo_cfg_switch(0)  // 下载配置0
  └── gh_demo_function_get()     // 获取支持的功能
```

## 附录 B: GH3036 寄存器空间概览

| 地址范围 | 说明 |
|----------|------|
| 0x0000-0x007F | 系统控制寄存器 |
| 0x0080-0x00FF | VCM/电源管理 |
| 0x0380-0x03FF | Slot 配置 / PPG 通道配置 |
| 0x0480-0x04FF | ADC / LED 驱动配置 |
| 0x0500-0x05FF | 中断使能 / 状态 |
| 0x0680-0x06FF | 接口配置 (SPI/I2C) |
| 0x1000-0x10FF | 工作模式 / 时间戳 |
| 0x1120-0x11FF | FIFO 间隔 |
| 0x1144 | GSENSOR_CTRL |
| 0x1280-0x129F | 各通道采样率(FS) |
| 0x1300-0x14FF | PPG 通道分配 / 数据类型 |
| 0x3400-0x34FF | AGC 配置 |
| 0x3600-0x36FF | LED 通道选择 |
| 0x75C0-0x75FF | HR 算法通道配置 |
| 0x78C0-0x78FF | HRV 算法通道配置 |
| 0x84C0-0x84FF | SPO2 算法通道配置 |
| 0x8DC0-0x8DFF | NADT 算法通道配置 |
| 0xFFFF | 虚拟寄存器（用于配置切换控制） |
