# ChelseaA_OS BLE 接口文档

> 本文档描述 `ble_app_chelsea_a_freertos` 项目的 BLE 蓝牙接口，
> 用于指导 APP 端、嵌入式移植端对接。
>
> 对应 SDK: GOODIX GR5526 BLE Stack
> 设备名称: `ChelseaA_OS`
> 生成日期: 2026-06-15

---

## 目录

1. [概述](#1-概述)
2. [通用约定](#2-通用约定)
3. [广播配置](#3-广播配置)
4. [HEALTH 服务（自定义）](#4-health-服务自定义)
5. [心率服务 HRS（标准）](#5-心率服务-hrs标准)
6. [OTA 服务 OTAS（自定义）](#6-ota-服务-otas自定义)
7. [GH RPC 协议（应用层）](#7-gh-rpc-协议应用层)
8. [工作模式](#8-工作模式)
9. [APP 对接流程](#9-app-对接流程)
10. [错误码](#10-错误码)

---

## 1. 概述

ChelseaA_OS 是一款基于 GOODIX GR5526 BLE SoC 的健康腕带设备。

**BLE 角色:** Peripheral（从机）

**注册的服务列表:**

| 服务 | UUID 类型 | UUID | 来源文件 | 说明 |
|------|-----------|------|----------|------|
| HEALTH Service | 128-bit | `0E190000-0010-0080-0080-5F9B34FB` | `profiles/health/` | 自定义数据通道，承载 GH 健康协议 |
| Heart Rate Service (HRS) | 16-bit (SIG) | `0x180D` | SDK `profiles/hrs/` | 标准心率服务 |
| OTA Service (OTAS) | 128-bit | `A6ED0401-D344-460A-8075-B9E8EC90D71B` | SDK `profiles/otas/` | DFU 固件升级 |

**调用方:** 手机 APP（iOS/Android），通过 BLE GATT 连接交互。

**数据流向:**

```
手机 APP ←→ BLE GATT ←→ HEALTH Service ←→ GH RPC Protocol ←→ GH3036 SDK
                                                 ↓
                                           HRS Service (心率标准广播)
```

---

## 2. 通用约定

### 2.1 版本

| 项目 | 值 |
|------|-----|
| BLE 协议版本 | 5.2 |
| GATT MTU 默认 | 23 字节（连接后协商，建议 ≥ 247） |
| ATT MTU 最大值 | 512 字节（GR5526 限制） |
| LE PHY | 1M（默认） |

### 2.2 连接参数

| 参数 | 值 | 单位 |
|------|-----|------|
| 最小连接间隔 | 320 (200 ms) | ×1.25 ms |
| 最大连接间隔 | 520 (325 ms) | ×1.25 ms |
| Slave Latency | 0 | 个事件 |
| Supervision Timeout | 400 (4 s) | ×10 ms |

### 2.3 安全

- 配对: **关闭**（`ble_gap_pair_enable(false)`）
- 加密: 无
- 所有特征值权限: **无安全认证**（`BLE_GATTS_PERM_UNSEC`）

### 2.4 编码与字节序

- BLE 协议栈使用 **Little-Endian**（LSB first）
- UUID 在 ATT 数据库中同样按 LSB first 存储
- 广播数据中 UUID 也按 LSB first 排列

### 2.5 数据长度

| 服务 | 最大单包长度 | 说明 |
|------|-------------|------|
| HEALTH TX (Notify) | 247 字节 | 上行数据通道 |
| HEALTH RX (Write) | 247 字节 | 下行数据通道 |
| OTAS RX/TX | 244 字节 | DFU 数据传输 |
| HRS Measurement | 20 字节 | 标准心率测量 |

---

## 3. 广播配置

### 3.1 广播参数

| 参数 | 值 |
|------|-----|
| 广播类型 | `ADV_IND`（可连接、可扫描） |
| 发现模式 | General Discoverable |
| 广播频道 | 37, 38, 39 |
| 过滤策略 | 允许任何人扫描和连接 |
| 间隔（慢速） | 160 (100 ms) |
| 地址类型 | Static Random |

### 3.2 广播数据包

#### Advertising Data

| 偏移 | 长度 | 内容 | AD Type |
|------|------|------|---------|
| 0 | 1 | 0x11 (17) | 段长度 |
| 1 | 1 | 0x07 | `BLE_GAP_AD_TYPE_COMPLETE_LIST_128_BIT_UUID` |
| 2 | 16 | OTA Service UUID (见下方) | 完整 128-bit UUID 列表 |

OTA Service UUID（广播格式，LSB first）:
```
A6 ED 04 01 44 D3 0A 46 80 75 B9 E8 EC 90 D7 1B
```

| 偏移 | 长度 | 内容 | AD Type |
|------|------|------|---------|
| 18 | 1 | 0x05 (5) | 段长度 |
| 19 | 1 | 0xFF | `BLE_GAP_AD_TYPE_MANU_SPECIFIC_DATA` |
| 20 | 2 | 0xF7 0x04 | GOODIX Company ID (0x04F7) |
| 22 | 2 | 0x02 0x03 | GOODIX 自定义数据 |

#### Scan Response Data

| 偏移 | 长度 | 内容 |
|------|------|------|
| 0 | 1 | 0x0C (12) | 段长度 |
| 1 | 1 | 0x09 | `BLE_GAP_AD_TYPE_COMPLETE_NAME` |
| 2 | 11 | `ChelseaA_OS` | 设备完整名称 |

### 3.3 广播启动

APP 连接断开后，设备会自动重新开启广播（见 `app_disconnected_handler`）。

---

## 4. HEALTH 服务（自定义）

### 4.1 服务定义

| 属性 | 值 |
|------|-----|
| 服务 UUID | `0000190E-0010-0080-0080-5F9B34FB` (128-bit) |
| 服务类型 | Primary Service |
| 文件 | `Src/profiles/health/health.c/.h` |

### 4.2 特征值

| 特征值 | UUID | 属性 | 权限 | 最大长度 | 说明 |
|--------|------|------|------|---------|------|
| TX Characteristic | `00000003-0010-0080-0080-5F9B34FB` | **Notify** | 无安全认证 | 247 字节 | 设备→手机：GH 协议数据上行 |
| RX Characteristic | `00000004-0010-0080-0080-5F9B34FB` | **Write / Write Without Response** | 无安全认证 | 247 字节 | 手机→设备：GH 协议数据下行 |

### 4.3 GATT 属性表

| 索引 | 属性类型 | UUID | 权限 | 值存储 |
|------|---------|------|------|--------|
| 0 | Primary Service Declaration | `0000190E-...` | Read | Stack |
| 1 | Characteristic Declaration (TX) | 0x2803 | Read | Stack |
| 2 | TX Value | `00000003-...` | Notify | **User** |
| 3 | Client Characteristic Configuration (TX CCCD) | 0x2902 | Read+Write | Stack |
| 4 | Characteristic Declaration (RX) | 0x2803 | Read | Stack |
| 5 | RX Value | `00000004-...` | Write+WriteCmd | **User** |

### 4.4 事件回调

`health_evt_handler` 向应用层报告以下事件:

| 事件 | 触发条件 | 说明 |
|------|---------|------|
| `HEALTH_EVT_RX_DATA_RECEIVED` | 收到手机写入 RX 特征值 | 数据在 `p_evt->p_data`，长度在 `p_evt->length` |
| `HEALTH_EVT_TX_DATA_SENT` | TX Notify 发送完成 | 可发下一包 |
| `HEALTH_EVT_TX_PORT_OPENED` | 手机使能 TX CCCD Notify | 表示手机已准备好接收数据 |
| `HEALTH_EVT_TX_PORT_CLOSED` | 手机禁能 TX CCCD Notify | 表示手机停止接收 |
| `HEALTH_EVT_TX_FLOW_OFF` | 流控关闭（预留） | 当前未使用 |
| `HEALTH_EVT_TX_FLOW_ON` | 流控开启（预留） | 当前未使用 |

### 4.5 使用方式

**步骤 1:** 手机订阅 TX Notify（写入 CCCD 0x0001）
**步骤 2:** 设备收到 `HEALTH_EVT_TX_PORT_OPENED`，确认通信就绪
**步骤 3:** 手机写入 RX 特征值，设备收到 `HEALTH_EVT_RX_DATA_RECEIVED`
**步骤 4:** 设备处理数据后，通过 TX Notify 将响应/数据发送给手机

### 4.6 数据帧结构（透传 GH 协议）

HEALTH Service 不关心数据内容，仅做**透传**。实际数据内容为 GH RPC 协议帧。

**上行帧**（设备 → 手机, via TX Notify）:

| 帧头 | 协议数据 | 说明 |
|------|---------|------|
| `0xAA 0x11` | ... (GH RPC Payload, ≤ 238 字节) | GHRPC 协议帧 |

**下行帧**（手机 → 设备, via RX Write）:

| 帧头 | 协议数据 | 说明 |
|------|---------|------|
| `0xAA 0x11` | ... (GH RPC Payload, ≤ 238 字节) | GHRPC 协议帧 |

> 详情见 [§7 GH RPC 协议](#7-gh-rpc-协议应用层)。

---

## 5. 心率服务 HRS（标准）

### 5.1 服务定义

| 属性 | 值 |
|------|-----|
| 服务 UUID | `0x180D` (16-bit SIG) |
| 服务类型 | Primary Service |
| 文件 | SDK `platform/gr5526_sdk/components/profiles/hrs/` |

### 5.2 特征值

| 特征值 | UUID | 属性 | 权限 | 说明 |
|--------|------|------|------|------|
| Heart Rate Measurement | `0x2A37` | **Notify** | 无安全认证 | 心率测量值 |
| Body Sensor Location | `0x2A38` | Read | 无安全认证 | 传感器位置（手指） |
| Heart Rate Control Point | `0x2A39` | Write | 无安全认证 | 控制点（重置能耗） |

### 5.3 心率测量值格式

**Flags 字节:**

| Bit | 含义 |
|-----|------|
| 0 | 心率值格式: 0=uint8, 1=uint16 |
| 1-2 | 传感器接触状态: 00=不支持, 01=支持未检测到, 11=支持且检测到 |
| 3 | 能耗扩展字段存在 |
| 4-7 | RR 间隔个数 |

**数据格式:**

| 字段 | 长度 | 说明 |
|------|------|------|
| Flags | 1 字节 | 如上定义 |
| Heart Rate Value | 1 或 2 字节 | 心率值（uint8 或 uint16，取决于 Flags bit0） |
| Energy Expended | 2 字节 | 累计能耗（可选，此项目中未使用） |
| RR-Interval x N | 2 字节 × N | RR 间隔（单位: 1/1024 秒，可选） |

### 5.4 数据源

| 数据 | 来源 | 触发方式 |
|------|------|---------|
| 心率值 (HR) | GH3036 HR 算法结果 (`hba_out`) | `hrs_heart_rate_measurement_send(conn_idx, hr, false)` |
| RR 间隔 (HRV) | GH3036 HRV 算法结果 (`rri[]`) | `hrs_rr_interval_add(rri)`，每 25 帧累积发送 |

### 5.5 传感器位置

当前配置为 `HRS_SENS_LOC_FINGER`（手指）。

---

## 6. OTA 服务 OTAS（自定义）

### 6.1 服务定义

| 属性 | 值 |
|------|-----|
| 服务 UUID | `A6ED0401-D344-460A-8075-B9E8EC90D71B` (128-bit) |
| 服务类型 | Primary Service |
| 版本 | 0x02 |
| 文件 | SDK `platform/gr5526_sdk/components/profiles/otas/` |

### 6.2 特征值

| 特征值 | UUID (128-bit) | 属性 | 说明 |
|--------|---------------|------|------|
| OTAS TX | `A6ED0402-...` (18B7) | **Notify** | DFU 数据发送 |
| OTAS RX | `A6ED0403-...` (18B8) | **Write Without Response** | DFU 数据接收 |
| OTAS Control Point | `A6ED0404-...` (18B9) | **Indicate + Write** | DFU 控制命令 |

### 6.3 DFU 流程

1. 手机写入 Control Point → 发送 `OTAS_CTRL_PT_OP_DFU_ENTER` (0x474F4F44) 进入 DFU 模式
2. 设备切换到 DFU 任务
3. 手机通过 RX 写入固件数据
4. 设备通过 TX Notify 返回状态
5. 完成后重启

---

## 7. GH RPC 协议（应用层）

> GH RPC 协议是运行在 HEALTH Service 之上的应用层协议，用于手机 APP 与 GH3036 SDK 之间的命令/数据交互。

### 7.1 协议概述

| 属性 | 值 |
|------|-----|
| 载体 | HEALTH Service (TX/RX) |
| 帧头 | `0xAA 0x11` (2 字节) |
| 最大帧长 | 240 字节 (GHRPC_FRAME_SIZE) |
| 传输方式 | 无连接可靠传输（带重试） |
| 包类型 | 单帧/多帧拆分 |

### 7.2 逐帧字节格式

#### 7.2.1 帧头

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 1 | Frame Header[0] | 固定 `0xAA` |
| 1 | 1 | Frame Header[1] | 固定 `0x11` |
| 2 | n | Payload | GH RPC 协议数据 |

> 注: 帧头 2 字节之后为编码后的协议数据，编码格式详见 7.2.2 节。

#### 7.2.2 协议编码格式

GHRPC 协议使用自描述打包格式（`gh_package.h`），每个参数前有一个 **TypeHeader**：

```
TypeHeader (1 byte):
┌─────────┬─────────┬─────────┬─────────┐
│ pack_type(2) │reserve(5)│ flag(1)  │
└─────────┴─────────┴─────────┴─────────┘

pack_type:
  00 = DOUBLE (浮点/数组)
  01 = UNSIGNED (无符号整数)
  10 = SIGNED (有符号整数)
  11 = PACK (外部类型, 如 RPCPoint)
```

#### 7.2.3 多帧拆分

当数据超过单帧最大长度时，GHRPC 将数据拆分为多个子帧发送。子帧之间通过协议帧中的 `split` 位标识。
接收方在收到拆分包时，若超过 `PASS_MESSAGE_KEEP_TIME` (10 次) 仍未收齐，则丢弃并报错 `GHRPC_ERROR_LOSE_FRAME`。

### 7.3 RPC 命令列表

通过 `gh_rpc_functions.h` 注册，所有命令通过字符串 key 匹配。

#### 7.3.1 设备控制类

| 命令 Key | 参数格式 | 返回格式 | 说明 |
|----------|---------|---------|------|
| `GH3X_GetVersion` | `<u8>` (uchVerType) | `<u8*>` (版本字符串) | 获取固件/芯片/BLE 版本 |
| `GHSetWorkModeCmd` | `<u8>` (uchWorkMode) | 无 | 设置工作模式（0=在线/1=离线/2=MPT） |
| `GH3X_ChipCtrl` | `<u8>` (uchCtrlType) | 无 | 芯片控制（复位/唤醒/休眠） |
| `gh_timestamp_set` | `<u32>` (ts) | 无 | 设置时间戳 |
| `gh_time_set` | `<u32><d8>` (ts + hour_offset) | 无 | 设置时间（含时区） |
| `get_chip_link_status` | `<u8>` (type) | `<d8*>` (状态数据) | 读取芯片连接状态 |

#### 7.3.2 寄存器操作类

| 命令 Key | 参数格式 | 返回格式 | 说明 |
|----------|---------|---------|------|
| `GH3X_RegsWriteCmd` | `<u16*>` (reg-value 数组) | 无 | 批量写寄存器 |
| `GH3X_RegsReadCmd` | `<u16><d32>` (地址+长度) | `<u16*>` (寄存器值) | 批量读寄存器 |
| `GH3X_RegBitFieldWriteCmd` | `<u16><u8><u8><u16>` (地址+lsb+msb+值) | 无 | 写寄存器位域 |
| `GH3X_RegsBitFieldWriteCmd` | `<u16*>` (位域数组) | 无 | 批量写位域 |
| `GH3X_RegsListWriteCmd` | `<u16*>` (配置表数组) | 无 | 加载配置表 |
| `download_config` | `<u8>` (uchStage) | 无 | 下载配置阶段 |

#### 7.3.3 功能控制类

| 命令 Key | 参数格式 | 返回格式 | 说明 |
|----------|---------|---------|------|
| `GH3X_SwFunctionCmd` | `<u32><u8>` (功能模式+控制类型) | 无 | 开关健康功能 |
| `gh_low_power_cmd` | `<u32><u8>` (功能模式+控制类型) | 无 | 低功耗控制 |
| `FW` | `<u8*>` (固件数据) | `<u8*>` (响应) | GH3036 固件升级 |

#### 7.3.4 工厂测试类

| 命令 Key | 参数格式 | 返回格式 | 说明 |
|----------|---------|---------|------|
| `F_SetMode` | `<u8>` (uchTestMode) | 无 | 设置工厂测试模式 |
| `F_GetMode` | `<u8>` (uchTestMode) | `<u16*>` (测试数据) | 获取工厂测试数据 |

### 7.4 优先级队列

GH RPC 数据包通过优先级队列处理:

| 优先级 | 适用数据 | 定义 |
|--------|---------|------|
| **HIGH** | 手机发来的控制命令 | `OSAL_EVENT_PRIORITY_HIGH` = 2 |
| **LOW** | 算法结果上行数据 | `OSAL_EVENT_PRIORITY_LOW` = 1 |

判断逻辑: 数据帧如果以 `0xAA 0x11 ... 0x9A 0x47 0x5D`（CMD=0x479A5D）为特征，视为 LOW 优先级；其余为 HIGH 优先级。

### 7.5 通信可靠性

| 机制 | 参数 |
|------|------|
| 发送重试次数 | `OSAL_EVENT_MAX_RETRY_COUNT` = 3 |
| 重试间隔 | 10 ms |
| 重试策略 | 插回队列头部（`send_front`） |

---

## 8. 工作模式

设备支持三种工作模式，通过 `GHSetWorkModeCmd` 切换:

| 模式 | 值 | 说明 |
|------|-----|------|
| **MCU 在线模式 (Online)** | 0 | 正常工作模式，RTC tick 1000ms，GH SDK 持续采集和处理 |
| **MCU 离线模式 (Offline)** | 1 | 预留（当前未实现区分） |
| **MPT 量产测试模式** | 2 | 工厂模式，RTC tick 200ms，启动 `factory_init()` |

### 8.1 健康功能控制

通过应用层 `health_start_event_send` / `health_stop_event_send` 控制各功能的启停:

| 功能 | Mode Bit | 按键触发 | 算法 | BLE 输出 |
|------|---------|---------|------|---------|
| HR + HRV | `GH_FUNCTION_HR \| GH_FUNCTION_HRV` | SW1 单击/双击 | HR + HRV | HRS Notify |
| SPO2 | `GH_FUNCTION_SPO2` | SW2 单击/双击 | SPO2 | Health TX |
| TEST1 | `GH_FUNCTION_TEST1` | SW3 单击/双击 | 测试模式 | Health TX |
| ADT | `GH_FUNCTION_ADT` | 自动（移动检测） | 佩戴检测 | 事件通知 |
| NADT | `GH_FUNCTION_GNADT` | 自动 | 非接触佩戴检测 | 事件通知 |

---

## 9. APP 对接流程

### 9.1 标准连接流程

```
手机 APP                                ChelseaA_OS 设备
   │                                           │
   │─── 扫描（发现 "ChelseaA_OS" 广播）──────→│
   │                                           │
   │─── 发起连接 ────────────────────────────→│
   │                                           │
   │←── 连接成功 (BLE_GAPC_EVT_CONNECTED) ─────│
   │                                           │
   │─── 发现服务 ────────────────────────────→│
   │                                           │
   │←── 发现 HEALTH / HRS / OTAS 服务 ────────│
   │                                           │
   │─── 写入 HEALTH TX CCCD = 0x0001 ────────→│  ← 关键步骤！使能 Notify
   │                                           │
   │←── HEALTH_EVT_TX_PORT_OPENED ─────────────│
   │                                           │
   │─── 开始正常通信 ─────────────────────────→│
```

### 9.2 数据通信流程

```
手机 APP                                    设备
   │                                          │
   │─── RX Write (0xAA 0x11 + CMD) ───────→  │  发送命令
   │                                          │──→ GHRPC_process()
   │                                          │──→ 执行命令/启动/停止功能
   │                                          │
   │←── TX Notify (0xAA 0x11 + RESP) ────────│  命令响应
   │                                          │
   │       ... (功能运行中) ...
   │                                          │
   │←── TX Notify (0xAA 0x11 + FRAME) ────────│  算法结果上行（周期性 Notify）
   │←── TX Notify (0xAA 0x11 + FRAME) ────────│
```

### 9.3 配置建议

| 参数 | 建议值 | 说明 |
|------|--------|------|
| MTU 请求 | ≥ 247 字节 | 否则单包数据受限 |
| PHY | 1M（默认） | 未请求 2M |
| 连接间隔 | 200ms ~ 325ms | 设备端固定范围 |
| GATT Write 类型 | **Write Request** (有响应) | RX 特征值配置 |
| 并发连接数 | ≤ 5 | 设备最大支持 |

### 9.4 断线重连

1. 设备检测到连接断开 (`BLE_GAPC_EVT_DISCONNECTED`)
2. 设置 `ble_connect_state = 0`，通知 DFU 模块
3. 自动重新开始广播 (`ble_gap_adv_start`)
4. APP 侧应在检测到断开后重新发起扫描和连接

---

## 10. 错误码

### 10.1 BLE Stack 通用错误码

以下为 SDK 常用错误码 (`ble_error.h`):

| 错误码 | 值 | 含义 | 建议动作 |
|--------|-----|------|---------|
| `SDK_SUCCESS` | 0 | 成功 | - |
| `SDK_ERR_POINTER_NULL` | 0x1001 | 参数为空指针 | 检查传入指针 |
| `SDK_ERR_INVALID_PARAM` | 0x1002 | 参数无效 | 检查参数范围 |
| `SDK_ERR_INVALID_CONN_IDX` | 0x1007 | 连接索引无效 | 确认连接状态 |
| `SDK_ERR_INVALID_ADV_IDX` | 0x100B | 广播索引无效 | 确认广播索引范围 |
| `SDK_ERR_NO_RESOURCES` | 0x1013 | 资源不足 | 增加堆内存或重试 |
| `SDK_ERR_DISALLOWED` | 0x1014 | 操作不允许 | 检查当前状态 |
| `SDK_ERR_NTF_DISABLED` | 0x1015 | Notify 未使能 | 检查 CCCD |
| `SDK_ERR_INVALID_HANDLE` | 0x1016 | 句柄无效 | 检查数据库注册 |

### 10.2 HEALTH 服务特有错误

| 错误码 | 含义 | 建议动作 |
|--------|------|---------|
| `BLE_ATT_ERR_INVALID_HANDLE` | 属性句柄无效 | 检查属性表一致性 |

### 10.3 GHRPC 协议错误码

| 错误码 | 含义 | 建议动作 |
|--------|------|---------|
| `GHRPC_ERROR_FORMAT_ERROR` | 协议格式错误 | 检查参数编码格式 |
| `GHRPC_ERROR_KEY_OVER_MAX_SIZE` | Key 超长 (>32) | 缩短 Key 名称 |
| `GHRPC_ERROR_NOT_UNDER_INVOKE` | 不识别的命令 Key | 检查命令 Key 拼写 |
| `GHRPC_ERROR_SEND_FAIL` | 发送失败 | 检查 BLE 连接状态 |
| `GHRPC_ERROR_MEMORY_NOT_ENOUGH` | 内存不足 | 增加协议缓冲区 |
| `GHRPC_ERROR_LOSE_FRAME` | 拆分包丢帧超时 | 确保通信连续 |

---

## 附录 A: UUID 速查表

| 名称 | UUID | 类型 |
|------|------|------|
| HEALTH Service | `0000190E-0010-0080-0080-5F9B34FB` | 128-bit Custom |
| HEALTH TX | `00000003-0010-0080-0080-5F9B34FB` | 128-bit Custom |
| HEALTH RX | `00000004-0010-0080-0080-5F9B34FB` | 128-bit Custom |
| HRS Service | `0x180D` | 16-bit SIG |
| Heart Rate Measurement | `0x2A37` | 16-bit SIG |
| Body Sensor Location | `0x2A38` | 16-bit SIG |
| Heart Rate Control Point | `0x2A39` | 16-bit SIG |
| OTAS Service | `A6ED0401-D344-460A-8075-B9E8EC90D71B` | 128-bit Custom |
| OTAS TX | `A6ED0402-D344-460A-8075-B9E8EC90D71B` | 128-bit Custom |
| OTAS RX | `A6ED0403-D344-460A-8075-B9E8EC90D71B` | 128-bit Custom |
| OTAS Control Point | `A6ED0404-D344-460A-8075-B9E8EC90D71B` | 128-bit Custom |

## 附录 B: RPC 命令 Key 汇总

```c
"GH3X_GetVersion"        // 获取版本
"GH3X_RegsWriteCmd"      // 写寄存器
"GH3X_RegsReadCmd"       // 读寄存器
"GH3X_RegBitFieldWriteCmd" // 写寄存器位域
"GH3X_ChipCtrl"          // 芯片控制
"download_config"        // 下载配置
"GH3X_RegsListWriteCmd"  // 批量写配置表
"GH3X_SwFunctionCmd"     // 功能开关
"gh_low_power_cmd"       // 低功耗控制
"FW"                     // 固件升级
"GH3X_RegsBitFieldWriteCmd" // 批量写位域
"GHSetWorkModeCmd"       // 设置工作模式
"get_chip_link_status"   // 获取连接状态
"gh_timestamp_set"       // 设置时间戳
"gh_time_set"            // 设置时间(含时区)
"F_SetMode"              // 设置工厂测试模式
"F_GetMode"              // 获取工厂测试数据
```

## 附录 C: 版本获取类型

`GH3X_GetVersion` 命令的 `uchVerType` 参数:

| 值 | 宏定义 | 含义 |
|-----|--------|------|
| 0x01 | `UPROTOCOL_GET_VER_TYPE_FW_VER` | 固件版本 |
| 0x03 | `UPROTOCOL_GET_VER_TYPE_VIRTUAL_REG_VER` | 虚拟寄存器版本 |
| 0x04 | `UPROTOCOL_GET_VER_TYPE_BOOTLOADER_VER` | Bootloader 版本 |
| 0x05 | `UPROTOCOL_GET_VER_TYPE_PROTOCOL_VER` | 协议版本 |
| 0x06 | `UPROTOCOL_GET_VER_TYPE_FUNC_SUPPORT` | 功能支持信息 |
| 0x07 | `UPROTOCOL_GET_VER_TYPE_DRV_VER` | 驱动版本 |
| 0x08 | `UPROTOCOL_GET_VER_TYPE_CHIP_VER` | 芯片版本（芯片 UID） |
| 0x09 | `UPROTOCOL_GET_VER_TYPE_BLE_VER` | BLE 版本（efuse + MAC） |
| 0x0A | `UPROTOCOL_GET_VER_TYPE_DEMO_VER` | Demo 版本 |
| 0x20 | `UPROTOCOL_GET_VER_TYPE_ALGO_VER` | 算法版本 |

## 附录 D: 相关源文件清单

| 文件路径 | 职责 |
|---------|------|
| `Src/user/user_app.c` | BLE 事件分发、服务注册、广播、连接管理 |
| `Src/user/user_app.h` | BLE 事件处理函数声明、堆区定义 |
| `Src/user/main.c` | 协议栈初始化、按键事件（启停各功能） |
| `Src/profiles/health/health.c` | HEALTH 自定义服务实现 |
| `Src/profiles/health/health.h` | HEALTH 服务 API、UUID 定义 |
| `Src/gh3036sdk/gh_protocol_user_for_gr5526.c` | GH RPC 协议线程、优先级队列事件处理 |
| `components/gh3036_sdk/gh_protocol/gh_rpccore.h` | GHRPC 核心接口定义 |
| `components/gh3036_sdk/gh_protocol/gh_rpc_functions.h` | RPC 命令封装（所有命令注册在此） |
| `components/gh3036_sdk/gh_protocol/gh_package.h` | 协议打包/解包格式定义 |
| `components/gh3036_sdk/gh_protocol/gh_data_package.h` | 数据帧格式定义 |
| `platform/gr5526_sdk/components/sdk/ble.h` | BLE SDK 入口头文件 |
| `platform/gr5526_sdk/components/sdk/ble_event.h` | BLE 事件 ID 枚举、事件结构体 |
| `platform/gr5526_sdk/components/sdk/ble_gapm.h` | GAP 管理 API（广播、扫描、连接） |
| `platform/gr5526_sdk/components/sdk/ble_gapc.h` | GAP 连接控制 API |
| `platform/gr5526_sdk/components/sdk/ble_gatts.h` | GATT 服务器 API |
| `platform/gr5526_sdk/components/profiles/hrs/hrs.h` | 心率服务 API |
| `platform/gr5526_sdk/components/profiles/otas/otas.h` | OTA DFU 服务 API |
| `Src/config/custom_config.h` | BLE 资源配置（最大连接数、广播数等） |
