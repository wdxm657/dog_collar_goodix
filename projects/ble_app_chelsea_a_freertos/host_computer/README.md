# ChelseaA_OS BLE 上位机 — 操作文档

## 环境准备

```bash
cd projects/ble_app_chelsea_a_freertos/host_computer
conda env create -f environment.yml    # 首次
conda activate chelsea_ble
python main.py
```

## 界面说明

```
┌──────────────────────────────────────────────────────────────┐
│ [🔍 扫描] [设备下拉 ▼] [🔗 连接] [⛔ 断开]         未连接 │
├─────────────────────────────────┬────────────────────────────┤
│ 📈 健康数据                      │ ⌨ 命令                     │
│ ┌──────┬──────┬──────┐          │ ▶ HR+HRV ▶ SpO2 ▶ ADT     │
│ │❤ HR │ 📊HRV│🫁SpO2│          │ ■ 停止  ℹ 版本  模式:在线  │
│ │--bpm │--ms  │--%   │          │ 自定义: [______________]   │
│ ├──────┼──────┼──────┤          │                            │
│ │📡Contact│📿Wear │          │ 提示: 设备按 SW1=心率 ...  │
│ └──────┴──────┴──────┘          │ ☑ HR+HRV自动 ☑ SpO2自动 >│
│ RPC 消息:                       │ ☑ ADT自动                  │
│ [实时 RPC 日志]                  │                            │
├──────────────────────────────────────────────────────────────┤
│ 📟 设备日志                     │ 🖥 主机日志                 │
│ [ble_printf 输出]               │ [连接/订阅/发送状态]        │
└──────────────────────────────────────────────────────────────┘
```

## 连接设备

1. 给设备上电
2. 点击 **🔍 扫描**（默认扫描 8 秒）
3. 下拉框中选择 `ChelseaA_OS [XX:XX:XX:XX:XX:XX]`
4. 点击 **🔗 连接**

连接成功后状态栏显示 "已连接"。

## 开始接收数据

### 方式一：使用设备按键（推荐）

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

在 "⌨ 命令" 标签页：

| 按钮 | 作用 | 发送的命令 |
|------|------|-----------|
| ▶ HR+HRV | 启动心率监测 | `GH3X_SwFunctionCmd(0x0006, 0)` |
| ▶ SpO2 | 启动血氧监测 | `GH3X_SwFunctionCmd(0x0020, 0)` |
| ▶ ADT | 启动佩戴检测 | `GH3X_SwFunctionCmd(0x0001, 0)` |
| ■ 停止 | 停止所有监测 | `GH3X_SwFunctionCmd(0xFFFFFFFF, 1)` |
| ℹ 版本 | 查询固件版本 | `GH3X_GetVersion(0x01)` |

或自定义输入：
```
GH3X_SwFunctionCmd 0x0006 0     # 启动 HR+HRV，ctrl=0 表示开始
GH3X_SwFunctionCmd 0x0006 1     # 停止 HR+HRV，ctrl=1 表示停止
```

### 方式三：自动启动

勾选 "☑ HR+HRV 自动启动" 等复选框 → 则在**下次连接时自动发送命令启动对应监测**。

## 功能模式位掩码

| 功能 | 宏 | 值 |
|------|-----|-----|
| ADT（佩戴检测） | `GH_FUNCTION_ADT` | 0x0001 |
| HR（心率） | `GH_FUNCTION_HR` | 0x0002 |
| HRV（心率变异性） | `GH_FUNCTION_HRV` | 0x0004 |
| SpO2（血氧） | `GH_FUNCTION_SPO2` | 0x0020 |
| NADT（非接触佩戴检测） | `GH_FUNCTION_NADT` | 0x0080 |

组合用位或：`HR+HRV = 0x0002 | 0x0004 = 0x0006`

## 数据通道

| 通道 | UUID | 内容 |
|------|------|------|
| HRS 心率 | `0x2A37` (Notify) | 标准 BLE 心率测量值（BPM + RR 间隔） |
| HEALTH TX | `00000003-...` (Notify) | GH RPC 数据帧（算法结果：SpO2/ADT/NADT） |
| HEALTH RX | `00000004-...` (Write) | 发送 GH RPC 命令 |
| LMS DATA | `A6ED0B03-...` (Notify) | 调试日志（LMS 服务，可选） |

## GH RPC 命令速查

```
GH3X_SwFunctionCmd  <mode:u32> <ctrl:u8>   # 功能开关 (ctrl: 0=启动,1=停止)
GH3X_GetVersion     <type:u8>              # 获取版本 (0x01=FW,0x08=芯片)

模式值示例:
  HR      = 0x0002    HR+HRV   = 0x0006    SpO2     = 0x0020
  ADT     = 0x0001    NADT     = 0x0080    Stop All = 0xFFFFFFFF
```

## 日志说明

| 面板 | 内容 | 来源 |
|------|------|------|
| **📟 设备日志** | 设备 `ble_printf()` 输出 + LMS 通道 | 固件直接推送，不经 flash |
| **🖥 主机日志** | 扫描/连接/订阅/发送命令状态 | 上位机自身 |
| **RPC 消息** | GH RPC 的数据帧解码 + 命令响应 | HEALTH TX |
