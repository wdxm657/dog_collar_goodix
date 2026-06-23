# ChelseaA_OS BLE 上位机 — 操作文档

基于 PyQt5 + bleak 的跨平台蓝牙低功耗健康数据解析上位机。

## 环境准备

```bash
# 安装依赖
cd projects/ble_app_chelsea_a_freertos/host_computer
pip install -r requirements.txt

# 启动上位机
python main.py
```

## 项目结构

```
host_computer/
├── main.py                # 入口文件
├── requirements.txt       # 依赖 (PyQt5, bleak)
├── README.md              # 本文件
├── ble_manager.py         # BLE 通信层 (扫描/连接/通知)
├── main_window.py         # PyQt5 图形界面 + 数据解析
└── protocol/
    ├── __init__.py
    └── gh_rpc.py          # GH RPC 协议编解码
```

## 界面说明

```
┌──────────────────────────────────────────────────────────────┐
│ [🔍 扫描] [设备下拉 ▼] [🔗 连接] [⛔ 断开]       🔴 未连接 │
├─────────────────────────────────┬────────────────────────────┤
│ 📈 健康数据                      │ ⌨ 命令                     │
│ ┌──────┬──────┬──────┐          │ ▶ HR+HRV ▶ SpO2 ▶ ADT     │
│ │❤ HR │ 📊HRV│🫁SpO2│          │ ■ 停止  ℹ 版本              │
│ │--bpm │--ms  │--%   │          │ 自定义: [______________]   │
│ ├──────┼──────┼──────┤          │                            │
│ │📡Contact│📿Wear │          │ 💡 提示信息                │
│ └──────┴──────┴──────┘          │ ☑ HR+HRV ☑ SpO2 ☑ ADT     │
│ 💬 RPC 消息                     │    (连接后自动启动)          │
│ [实时 RPC 解码日志]              │                            │
├──────────────────────────────────────────────────────────────┤
│ 📟 设备日志                     │ 🖥 主机日志                  │
│ [LMS/ble_printf 输出]           │ [扫描/连接/发送状态]         │
└──────────────────────────────────────────────────────────────┘
```

## 连接设备

1. 给设备上电，等待 BLE 广播
2. 点击 **🔍 扫描**（扫描约 3 秒）
3. 下拉框中选择 `ChelseaA_OS [XX:XX:XX:XX:XX:XX]`
4. 点击 **🔗 连接**

连接成功后状态指示灯变绿，显示 "🟢 已连接"。

## 开始接收数据

### 方式一：使用设备按键

| 按键 | 功能 | 数据输出 |
|------|------|----------|
| **SW1** | HR + HRV（心率/心率变异性） | HRS Notify + Health TX |
| **SW2** | SpO2（血氧） | Health TX |
| **SW3** | 测试模式 | Health TX |
| ADT/NADT | 自动运行（佩戴检测） | Health TX |

**操作流程：**
1. 设备上电，等待 BLE 广播启动
2. PC 扫描 → 连接 → 订阅（自动完成）
3. 按下设备上的 **SW1 按键**（单击或双击）
4. PC 界面的 HR 卡片 5-10 秒后开始显示心率值
5. 按下 SW2 → 血氧开始监测
6. 再次按下相同按键即可停止

### 方式二：通过 BLE 命令（上位机控制）

在右侧命令面板：

| 按钮 | 作用 | GH RPC 命令 |
|------|------|-------------|
| ▶ HR+HRV | 启动心率+HRV监测 | `GH3X_SwFunctionCmd(0x000A, 0)` |
| ▶ SpO₂ | 启动血氧监测 | `GH3X_SwFunctionCmd(0x0004, 0)` |
| ▶ ADT | 启动佩戴检测 | `GH3X_SwFunctionCmd(0x0001, 0)` |
| ■ 停止 | 停止所有监测 | `GH3X_SwFunctionCmd(0xFFFFFFFF, 1)` |
| ℹ 版本 | 查询固件版本 | `GH3X_GetVersion(0x01)` |

自定义命令框支持：
```
GH3X_SwFunctionCmd 0x000A 0     # 启动 HR+HRV，ctrl=0 表示开始
GH3X_SwFunctionCmd 0x000A 1     # 停止 HR+HRV，ctrl=1 表示停止
GH3X_GetVersion 0x01            # 获取固件版本
GH3X_GetVersion 0x08            # 获取芯片版本
```

### 方式三：自动启动

勾选 "☑ HR+HRV 自动启动" 等复选框，则在**下次连接时自动发送命令启动对应监测**。

## 功能模式位掩码

| 功能 | 宏 | 值 |
|------|-----|-----|
| ADT（佩戴检测） | `GH_FUNCTION_ADT` | 0x0001 |
| HR（心率） | `GH_FUNCTION_HR` | 0x0002 |
| SpO2（血氧） | `GH_FUNCTION_SPO2` | 0x0004 |
| HRV（心率变异性） | `GH_FUNCTION_HRV` | 0x0008 |
| NADT（非接触佩戴检测） | `GH_FUNCTION_NADT` | 0x0080 |

组合用位或：`HR+HRV = 0x0002 \| 0x0008 = 0x000A`

## 数据通道

| 通道 | UUID | 内容 |
|------|------|------|
| **HRS 心率** | `00002A37-0000-1000-8000-00805F9B34FB` (Notify) | 标准 BLE 心率测量值（BPM + RR 间隔） |
| **HEALTH TX** | `00000003-0000-1000-8000-00805F9B34FB` (Notify) | GH RPC 数据帧（算法结果：SpO2/ADT/NADT） |
| **HEALTH RX** | `00000004-0000-1000-8000-00805F9B34FB` (Write) | 发送 GH RPC 命令到设备 |
| **LMS DATA** | `A6ED0B03-0000-4000-8000-00805F9B34FB` (Notify) | 调试日志（可选，LMS 服务） |

## GH RPC 协议说明

### 帧格式

```
[0xAA, 0x11]  (2B 帧头)
[length]       (1B 帧长度)
[key_header]   (1B 类型头)
[key_data]     (N 字节键名, 如 "GH3X_SwFunctionCmd")
[params...]    (参数数据, 变长整数编码)
[crc]          (1B 校验和)
```

### 数据帧解析

算法结果数据帧使用键 `"G"`，其 payload 为 ZigZag 编码的变长整数字节流：

```
[pack_header]   ─ 32-bit 位域指明哪些字段存在
  ├ rawdata_en  ─ PPG 原始数据
  ├ phy_value_en─ 光电容积脉搏波
  ├ gs_data_en  ─ 加速度/陀螺仪 (3 轴)
  ├ flags_en    ─ LED/SA 标志
  ├ alg_data_en ─ 算法结果数组 ← 核心健康数据
  ├ agc_info_en ─ AGC 增益/电流信息
  ├ timestamp_en─ 时间戳
  └ func_id_en  ─ 功能 ID (HR/SpO2/HRV/ADT/NADT)
[frame_id]      ─ 帧计数
```

### 算法结果索引

| 功能 | 索引 0 | 索引 1 | 索引 2 | 索引 3 | 索引 4 | 索引 5 |
|------|--------|--------|--------|--------|--------|--------|
| HR | hba_out (bpm) | valid_score | snr | blank | acc_info | reg_scence |
| SpO2 | final_spo2 | r_val | confi_coeff | valid_level | hb_mean | invalid_flag |
| HRV | rri[0] | rri[1] | rri[2] | rri[3] | confidence | valid_num |
| ADT | wear_event | det_status | ctr | - | - | - |
| NADT | nadt_out | nadt_confi | - | - | - | - |

## GH RPC 命令速查

```
GH3X_SwFunctionCmd  <mode:u32> <ctrl:u8>   # 功能开关 (ctrl: 0=启动, 1=停止)
GH3X_GetVersion     <type:u8>              # 获取版本 (0x01=FW, 0x08=芯片)
GHSetWorkModeCmd    <mode:u8>              # 工作模式 (0=在线, 1=离线, 2=量产)
GH3X_ChipCtrl       <type:u8>              # 芯片控制 (0x5A=硬复位)

模式值示例:
  HR      = 0x0002    HR+HRV   = 0x000A    SpO2     = 0x0004
  ADT     = 0x0001    NADT     = 0x0080    Stop All = 0xFFFFFFFF
```

## 日志说明

| 面板 | 内容 | 来源 |
|------|------|------|
| **💬 RPC 消息** | GH RPC 数据帧解码结果 + 命令响应 | HEALTH TX 通道解析 |
| **📟 设备日志** | 设备 `ble_printf()` 输出 + LMS 调试通道 | LMS 服务 (可选) |
| **🖥 主机日志** | 扫描/连接/订阅/发送命令状态 | 上位机自身 |

## 代码架构

```
main.py                       ─ 入口
  └── main_window.py          ─ PyQt5 UI + 数据绑定
        ├── HealthCard        ─ 健康数据卡片组件
        ├── AsyncWorker       ─ asyncio 工作线程
        ├── parse_hrs_measurement()  ─ 标准 BLE HRS 解析
        └── MainWindow        ─ 主窗口 (信号/槽架构)
              └── ble_manager.py    ─ BLE 通信层 (bleak)
                    └── protocol/gh_rpc.py  ─ GH RPC 编解码
                          ├── build_sw_function_cmd()
                          ├── build_get_version_cmd()
                          ├── parse_rpc_frame()
                          └── parse_data_frame()
```

## 常见问题

**Q: 扫描不到设备？**
A: 确认设备已上电并处于广播状态（指示灯闪烁）。Windows 需确保蓝牙已开启且有 BLE 支持。

**Q: 连接后收不到数据？**
A: 检查设备日志面板是否有错误信息。确认已通过设备按键或上位机命令启动了对应功能。

**Q: HR 卡显示 --bpm？**
A: 算法需要 5-10 秒预热才能输出首次结果。确认设备已正确佩戴。

**Q: SpO2 数值异常？**
A: SpO2 算法需要稳定的信号。保持设备与皮肤良好接触，避免运动干扰。
