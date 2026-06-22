#!/usr/bin/env python3
"""
ChelseaA_OS BLE Health Monitor — PyQt5 上位机
================================================

用法:
    conda activate chelsea_ble
    python main.py

参考:
    docs/BLE_INTERFACE.md
    Src/profiles/health/health.h
    platform/gr5526_sdk/components/profiles/lms/lms.h
"""

from __future__ import annotations

import asyncio
import struct
import sys
import time

import qasync
from bleak import BleakClient, BleakScanner
from PyQt5 import QtCore, QtGui, QtWidgets

__version__ = "2.0.0"

# ==============================================================================
# 常量
# ==============================================================================

DEVICE_NAME = "ChelseaA_OS"

# UUID 均来自 Bleak 实际发现的设备 GATT 数据库
HEALTH_SVC_UUID = "0000190E-0000-1000-8000-00805F9B34FB"
HEALTH_TX_UUID  = "00000003-0000-1000-8000-00805F9B34FB"
HEALTH_RX_UUID  = "00000004-0000-1000-8000-00805F9B34FB"

HRS_SVC_UUID  = "0000180D-0000-1000-8000-00805F9B34FB"
HRM_CHAR_UUID = "00002A37-0000-1000-8000-00805F9B34FB"

LMS_SVC_UUID  = "A6ED0B01-D344-460A-8075-B9E8EC90D71B"
LMS_DATA_UUID = "A6ED0B03-D344-460A-8075-B9E8EC90D71B"

GH_FRAME_HEADER = b"\xAA\x11"

FUNC_NAMES = {
    0x0001: "ADT", 0x0002: "HR", 0x0004: "HRV",
    0x0020: "SPO2", 0x0080: "NADT",
}


# ==============================================================================
# 协议解析
# ==============================================================================

def parse_hrs(data: bytes) -> tuple[int, str, list[float]]:
    """解析标准心率测量 (0x2A37). 返回 (bpm, contact, [rr_ms, ...])."""
    flags = data[0]
    fmt_16bit = flags & 0x01
    contact = ["unsupported", "unsupported", "no_contact", "contact"][(flags >> 1) & 0x03]
    offset = 1
    hr = struct.unpack_from("<H", data, offset)[0] if fmt_16bit else data[offset]
    offset += 2 if fmt_16bit else 1
    if flags & 0x08:
        offset += 2
    rris = []
    for _ in range((flags >> 4) & 0x0F):
        if offset + 2 <= len(data):
            rris.append(struct.unpack_from("<H", data, offset)[0] / 1024.0 * 1000.0)
            offset += 2
    return hr, contact, rris


def _hex_dump(data: bytes) -> str:
    """调试用：将数据按 4 字节分组显示，最长 64 字节."""
    n = min(len(data), 64)
    words = [data[i:i+4].hex() for i in range(0, n, 4)]
    return " ".join(words)


def build_rpc_frame(cmd_key: str, args: list[int]) -> bytes:
    """构建 GH RPC 帧: 0xAA 0x11 + key\\0 + 小端 int32."""
    frame = bytearray(GH_FRAME_HEADER)
    frame.extend(cmd_key.encode("ascii"))
    frame.append(0x00)
    for a in args:
        frame.extend(struct.pack("<i", a))
    return bytes(frame)


# ==============================================================================
# BLE 管理器 (运行在主 asyncio 事件循环中)
# ==============================================================================

class BleManager:
    def __init__(self, callbacks: dict):
        self._client: BleakClient | None = None
        self._rx_uuid: str | None = None
        self._running = False
        self._cb = callbacks

    async def scan(self):
        seen = set()
        devices = []
        def _cb(d, adv):
            if adv.local_name and DEVICE_NAME in adv.local_name:
                if d.address not in seen:
                    seen.add(d.address)
                    devices.append((adv.local_name, d.address))
                    self.log(f"发现: {adv.local_name} [{d.address}]")

        self.log(f"扫描 '{DEVICE_NAME}' …")
        scanner = BleakScanner(_cb, scanning_mode="active")
        await scanner.start()
        await asyncio.sleep(8)
        await scanner.stop()
        self.log(f"扫描完成: {len(devices)} 台")
        return devices

    async def connect(self, address: str):
        self._client = BleakClient(address, timeout=30)
        await self._client.connect()
        self.log(f"已连接 | MTU={self._client.mtu_size}")

        try:
            for svc in self._client.services:
                su = svc.uuid.lower()
                self.log(f"  匹配服务: {svc.uuid}")

                if su == HEALTH_SVC_UUID.lower():
                    for ch in svc.characteristics:
                        cu = ch.uuid.lower()
                        if cu == HEALTH_TX_UUID.lower():
                            await self._client.start_notify(ch.uuid, self._on_health_tx)
                            self.log("  ✓ HEALTH TX (Notify)")
                        elif cu == HEALTH_RX_UUID.lower():
                            self._rx_uuid = ch.uuid
                            self.log("  ✓ HEALTH RX (Write)")

                elif su == HRS_SVC_UUID.lower():
                    for ch in svc.characteristics:
                        if ch.uuid.lower() == HRM_CHAR_UUID.lower():
                            await self._client.start_notify(ch.uuid, self._on_hrs)
                            self.log("  ✓ HRS Measurement (Notify)")

                elif su == LMS_SVC_UUID.lower():
                    for ch in svc.characteristics:
                        if ch.uuid.lower() == LMS_DATA_UUID.lower():
                            await self._client.start_notify(ch.uuid, self._on_lms)
                            self.log("  ✓ LMS DATA (Notify)")

        except Exception as e:
            self.log(f"订阅异常: {e}")
            import traceback
            self.log(traceback.format_exc())

        self.log(f"RX uuid = {self._rx_uuid}")
        # 所有订阅完成后再通知 UI 可以发命令了
        self._cb["connected"]()
        self._running = True
        while self._running and self._client and self._client.is_connected:
            await asyncio.sleep(0.5)

        if self._client and not self._client.is_connected:
            self.log("连接断开")
            self._on_cleanup()

    async def disconnect(self):
        self._running = False
        if self._client:
            try:
                await self._client.disconnect()
            except Exception:
                pass
        self._on_cleanup()

    async def send_command(self, key: str, *args: int):
        if not self._rx_uuid or not self._client:
            self.log("无法发送 — 未连接")
            return
        frame = build_rpc_frame(key, list(args))
        try:
            await self._client.write_gatt_char(self._rx_uuid, frame, response=False)
            self.log(f"已发送: {key} {list(args)}")
        except Exception as e:
            self.log(f"发送失败: {e}")

    # ── 通知回调 ────────────────────────────────────────────────────────

    def _on_hrs(self, sender, data):
        try:
            hr, contact, rris = parse_hrs(data)
            self._cb["hr"](hr, contact, rris)
        except Exception:
            pass

    def _on_health_tx(self, sender, data):
        self._cb["health_raw"](data)
        if data.startswith(GH_FRAME_HEADER):
            payload = data[2:]
            # 命令响应：找 \x00，且之前的字节全是可打印 ASCII 命令名
            null_idx = payload.find(b"\x00", 0, 64)
            if null_idx > 0 and all(32 <= b < 127 for b in payload[:null_idx]):
                key = payload[:null_idx].decode("ascii")
                hex_ = payload[null_idx + 1:].hex()[:48]
                self._cb["rpc"](f"[RPC] {key} | {hex_}")
                return
            # 数据帧：内容为 GH3036 私有 RPC 编码
            if self._cb.get("show_hex", lambda: False)():
                self._cb["rpc"](_hex_dump(payload))
        else:
            # 文本日志 (ble_printf 走的 HEALTH TX)
            try:
                txt = data.decode("utf-8", errors="replace").strip()
                if txt:
                    self._cb["log_line"](txt)
            except Exception:
                pass

    def _on_lms(self, sender, data):
        try:
            txt = data.decode("utf-8", errors="replace").strip()
            if txt:
                self._cb["log_line"](f"[LMS] {txt}")
        except Exception:
            pass

    # ── 内部 ────────────────────────────────────────────────────────────

    def _on_cleanup(self):
        self._client = None
        self._rx_uuid = None
        self.log("已断开")
        self._cb["disconnected"]()

    def log(self, msg):
        self._cb["log"](msg)


# ==============================================================================
# UI 组件
# ==============================================================================

class _MetricCard(QtWidgets.QFrame):
    def __init__(self, title, default, color):
        super().__init__()
        self.setFrameStyle(QtWidgets.QFrame.Box | QtWidgets.QFrame.Raised)
        self.setStyleSheet(f"QFrame {{ border:1px solid {color}; border-radius:6px; padding:8px; }}")
        ly = QtWidgets.QVBoxLayout(self)
        ly.setSpacing(2)
        ly.addWidget(QtWidgets.QLabel(f"<b>{title}</b>"))
        self._val = QtWidgets.QLabel(default)
        self._val.setStyleSheet(f"font-size:18pt; font-weight:bold; color:{color};")
        ly.addWidget(self._val)
    def set(self, text): self._val.setText(text)


class _LogBox(QtWidgets.QPlainTextEdit):
    def __init__(self, max_blocks=2000):
        super().__init__()
        self.setReadOnly(True)
        self.setMaximumBlockCount(max_blocks)
        self.setFont(QtGui.QFont("Consolas", 9))
    def add(self, msg):
        self.appendPlainText(msg)
        self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())


# ==============================================================================
# 主窗口
# ==============================================================================

class MainWindow(QtWidgets.QMainWindow):
    C = {"hr":"#e74c3c","hrv":"#9b59b6","spo2":"#3498db","contact":"#2ecc71","wear":"#f39c12"}

    def __init__(self):
        super().__init__()
        self._ble: BleManager | None = None
        self._build()
        self._blink = QtCore.QTimer(); self._blink.timeout.connect(self._tick)
        self._blink.start(200)

    def _build(self):
        self.setWindowTitle(f"ChelseaA_OS BLE Monitor v{__version__}")
        self.resize(960, 720)
        c = QtWidgets.QWidget(); self.setCentralWidget(c)
        r = QtWidgets.QVBoxLayout(c); r.setSpacing(4)

        # 工具栏
        bar = QtWidgets.QHBoxLayout()
        self._btn_scan = QtWidgets.QPushButton("🔍 扫描")
        self._combo = QtWidgets.QComboBox(); self._combo.setMinimumWidth(260)
        self._combo.setPlaceholderText("选择设备 …")
        self._btn_conn = QtWidgets.QPushButton("🔗 连接"); self._btn_conn.setEnabled(False)
        self._btn_disc = QtWidgets.QPushButton("⛔ 断开"); self._btn_disc.setEnabled(False)
        self._lbl_status = QtWidgets.QLabel("未连接"); self._lbl_status.setStyleSheet("color:gray; padding:2px;")
        for w in (self._btn_scan, self._combo, self._btn_conn, self._btn_disc):
            bar.addWidget(w)
        bar.addStretch(); bar.addWidget(self._lbl_status)
        r.addLayout(bar)

        # 内容
        spl = QtWidgets.QSplitter(QtCore.Qt.Vertical)
        tabs = QtWidgets.QTabWidget()
        tabs.addTab(self._tab_health(), "📈 健康数据")
        tabs.addTab(self._tab_cmd(), "⌨ 命令")
        spl.addWidget(tabs)

        # 底部日志
        log_tabs = QtWidgets.QTabWidget()
        self._log_dev = _LogBox(); log_tabs.addTab(self._log_dev, "📟 设备日志")
        self._log_host = _LogBox(500); log_tabs.addTab(self._log_host, "🖥 主机日志")
        spl.addWidget(log_tabs)
        spl.setSizes([420, 300])
        r.addWidget(spl)

        # 信号
        self._btn_scan.clicked.connect(lambda: asyncio.ensure_future(self._scan()))
        self._btn_conn.clicked.connect(lambda: asyncio.ensure_future(self._connect()))
        self._btn_disc.clicked.connect(lambda: asyncio.ensure_future(self._disconnect()))

    def _tab_health(self):
        w = QtWidgets.QWidget(); ly = QtWidgets.QVBoxLayout(w)
        g = QtWidgets.QGridLayout(); g.setSpacing(8)
        self._card_hr = _MetricCard("❤ HR", "-- bpm", self.C["hr"])
        self._card_hrv = _MetricCard("📊 HRV", "-- ms", self.C["hrv"])
        self._card_spo2 = _MetricCard("🫁 SpO2", "-- %", self.C["spo2"])
        self._card_contact = _MetricCard("📡 Contact", "--", self.C["contact"])
        self._card_wear = _MetricCard("📿 Wear", "--", self.C["wear"])
        g.addWidget(self._card_hr, 0, 0); g.addWidget(self._card_hrv, 0, 1)
        g.addWidget(self._card_spo2, 0, 2)
        g.addWidget(self._card_contact, 1, 0); g.addWidget(self._card_wear, 1, 1)
        ly.addLayout(g)
        hdr = QtWidgets.QHBoxLayout()
        hdr.addWidget(QtWidgets.QLabel("RPC 消息:"))
        hdr.addStretch()
        self._cb_hex = QtWidgets.QCheckBox("显示 HEX")
        self._cb_hex.setChecked(False)
        hdr.addWidget(self._cb_hex)
        ly.addLayout(hdr)
        self._log_rpc = _LogBox(500); ly.addWidget(self._log_rpc, 1)
        return w

    def _tab_cmd(self):
        w = QtWidgets.QWidget(); ly = QtWidgets.QVBoxLayout(w)
        q = QtWidgets.QHBoxLayout()
        for txt, key, *a in [
            ("▶ HR+HRV", "GH3X_SwFunctionCmd", 0x0006, 0),
            ("▶ SpO2",   "GH3X_SwFunctionCmd", 0x0020, 0),
            ("▶ ADT",    "GH3X_SwFunctionCmd", 0x0001, 0),
            ("■ 停止",   "GH3X_SwFunctionCmd", 0xFFFFFFFF, 1),
            ("ℹ 版本",   "GH3X_GetVersion", 0x01),
            ("模式:在线", "GHSetWorkModeCmd", 0),
        ]:
            b = QtWidgets.QPushButton(txt)
            b.clicked.connect(lambda _, k=key, args=a: self._cmd(k, *args))
            q.addWidget(b)
        q.addStretch(); ly.addLayout(q)
        r = QtWidgets.QHBoxLayout()
        r.addWidget(QtWidgets.QLabel("自定义:"))
        self._cmd_in = QtWidgets.QLineEdit()
        self._cmd_in.setPlaceholderText("e.g. GH3X_GetVersion 0x01")
        r.addWidget(self._cmd_in, 1)
        b = QtWidgets.QPushButton("发送"); b.clicked.connect(self._send_custom)
        r.addWidget(b)
        self._cmd_in.returnPressed.connect(self._send_custom)
        ly.addLayout(r)
        ly.addWidget(QtWidgets.QLabel(
            "<small><b>提示:</b> 设备按 SW1=心率 SW2=血氧 SW3=测试 可启停监测，"
            "或通过 BLE 发送 GH3X_SwFunctionCmd。<br>"
            "<b>参数:</b> 模式位掩码(0x0002=HR,0x0004=HRV,0x0020=SpO2,0x0001=ADT) + 控制(0=启动,1=停止)</small>"
        ), 1); ly.addWidget(self._build_auto_btns())
        ly.addStretch()
        return w

    def _build_auto_btns(self):
        """自动启动定时器控制设备进入监测模式."""
        gb = QtWidgets.QGroupBox("自动控制")
        ly = QtWidgets.QHBoxLayout(gb)
        self._auto_hr = QtWidgets.QCheckBox("HR+HRV 自动启动"); ly.addWidget(self._auto_hr)
        self._auto_spo2 = QtWidgets.QCheckBox("SpO2 自动启动"); ly.addWidget(self._auto_spo2)
        self._auto_adt = QtWidgets.QCheckBox("ADT 自动启动"); ly.addWidget(self._auto_adt)
        ly.addStretch()
        return gb

    def _cmd(self, key, *args):
        if self._ble:
            asyncio.ensure_future(self._ble.send_command(key, *args))

    def _send_custom(self):
        t = self._cmd_in.text().strip()
        if not t: return
        p = t.split()
        key = p[0]
        args = [int(x, 0) for x in p[1:]]
        self._cmd(key, *args)
        self._cmd_in.clear()

    # ── BLE 回调 ────────────────────────────────────────────────────────

    def _init_ble(self):
        self._ble = BleManager({
            "connected": self._on_connected,
            "disconnected": self._on_disconnected,
            "log": lambda m: self._log_host.add(f"[{time.strftime('%H:%M:%S')}] {m}"),
            "hr": self._on_hr,
            "rpc": self._on_rpc,
            "health_raw": lambda d: None,
            "log_line": lambda m: self._log_dev.add(f"[{time.strftime('%H:%M:%S')}] {m}"),
        })

    async def _scan(self):
        self._combo.clear()
        devs = await self._ble.scan()
        for name, addr in devs:
            self._combo.addItem(f"{name} [{addr}]", addr)
        self._btn_conn.setEnabled(bool(devs))

    async def _connect(self):
        a = self._combo.currentData()
        if not a: return
        self._btn_conn.setEnabled(False); self._btn_disc.setEnabled(True)
        self._lbl_status.setText("连接中 …")
        asyncio.ensure_future(self._ble.connect(a))

    async def _disconnect(self):
        self._btn_disc.setEnabled(False)
        if self._ble: await self._ble.disconnect()

    def _on_connected(self):
        self._lbl_status.setText("已连接")
        self._on_auto_start()

    def _on_disconnected(self):
        self._btn_conn.setEnabled(self._combo.count() > 0)
        self._btn_disc.setEnabled(False)
        self._lbl_status.setText("未连接")

    def _on_hr(self, bpm, contact, rris):
        self._card_hr.set(f"{bpm} bpm")
        self._card_contact.set(contact)
        if rris: self._card_hrv.set(f"{rris[-1]:.0f} ms")

    def _on_rpc(self, msg):
        self._log_rpc.add(f"[{time.strftime('%H:%M:%S')}] {msg}")

    def _on_auto_start(self):
        """连接后根据复选框自动发送启动命令."""
        cmds = []
        if self._auto_hr.isChecked(): cmds.append(("HR+HRV", 0x0006))
        if self._auto_spo2.isChecked(): cmds.append(("SpO2", 0x0020))
        if self._auto_adt.isChecked(): cmds.append(("ADT", 0x0001))
        for name, mode in cmds:
            self._cmd("GH3X_SwFunctionCmd", mode, 0)

    def _tick(self):
        """定时更新 UI."""
        pass


# ==============================================================================
# 入口
# ==============================================================================

def main():
    import qasync
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle("Fusion")
    loop = qasync.QEventLoop(app)
    asyncio.set_event_loop(loop)
    win = MainWindow(); win.show()
    win._init_ble()
    with loop: loop.run_forever()

if __name__ == "__main__":
    main()
