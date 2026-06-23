"""
健康蓝牙设备数据解析上位机 - PyQt5 主界面

基于 bleak + PyQt5 的 BLE 健康设备数据解析上位机。
支持 ChelseaA_OS 设备的 HR/HRV/SpO2/ADT/NADT 数据实时显示与命令控制。
"""

import sys
import asyncio
from datetime import datetime
from typing import Optional

from PyQt5.QtCore import (
    Qt, QTimer, pyqtSignal, QObject, QThread,
)
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QComboBox, QLabel, QTextEdit, QPlainTextEdit,
    QGroupBox, QGridLayout,
    QCheckBox, QLineEdit, QSplitter, QFrame,
    QStatusBar,
)
from PyQt5.QtGui import QFont, QTextCursor, QColor, QPalette

from bleak.backends.device import BLEDevice

from ble_manager import BleManager
from protocol.gh_rpc import (
    parse_rpc_frame, parse_data_frame, unwrap_g_key_payload,
    FuncId,
    build_sw_function_cmd, build_get_version_cmd, build_set_work_mode_cmd,
)

# ======================================================================
# 异步事件循环线程
# ======================================================================
class AsyncWorker(QObject):
    """在单独线程中运行 asyncio 事件循环"""

    finished = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self._running = False

    def start(self):
        self._running = True
        try:
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
            self.loop.run_forever()
        finally:
            if self.loop:
                self.loop.close()
            self.finished.emit()

    def stop(self):
        self._running = False
        if self.loop:
            self.loop.call_soon_threadsafe(self.loop.stop)

    def run_coro(self, coro):
        """在工作线程的事件循环中执行协程"""
        if self.loop and self.loop.is_running():
            return asyncio.run_coroutine_threadsafe(coro, self.loop)
        return None


# ======================================================================
# 健康数据卡片组件
# ======================================================================
class HealthCard(QFrame):
    """单个健康数据展示卡片"""

    def __init__(self, title: str, unit: str, icon: str = "", parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setStyleSheet("""
            HealthCard {
                background-color: #1e1e2e;
                border: 1px solid #313244;
                border-radius: 8px;
                padding: 8px;
            }
        """)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(4)

        # 标题行: 图标 + 名称
        title_row = QHBoxLayout()
        icon_label = QLabel(icon)
        icon_label.setStyleSheet("font-size: 18px;")
        title_row.addWidget(icon_label)

        name_label = QLabel(title)
        name_label.setStyleSheet("color: #a6adc8; font-size: 12px;")
        title_row.addWidget(name_label)
        title_row.addStretch()
        layout.addLayout(title_row)

        # 数值
        self.value_label = QLabel("--")
        self.value_label.setStyleSheet("color: #cdd6f4; font-size: 28px; font-weight: bold;")
        self.value_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.value_label)

        # 单位
        self.unit_label = QLabel(unit)
        self.unit_label.setStyleSheet("color: #585b70; font-size: 12px;")
        self.unit_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.unit_label)

    def set_value(self, value, unit: str = None):
        self.value_label.setText(str(value))
        if unit:
            self.unit_label.setText(unit)


# ======================================================================
# 主窗口
# ======================================================================
class MainWindow(QMainWindow):
    """健康蓝牙设备上位机主窗口"""

    # 跨线程信号
    update_hr_signal = pyqtSignal(object)
    update_hrv_signal = pyqtSignal(object)
    update_spo2_signal = pyqtSignal(object)
    update_adt_signal = pyqtSignal(object)
    update_nadt_signal = pyqtSignal(object)
    append_rpc_log_signal = pyqtSignal(str, str)
    append_device_log_signal = pyqtSignal(str)
    append_host_log_signal = pyqtSignal(str)
    scan_complete_signal = pyqtSignal(object)
    connection_changed_signal = pyqtSignal(bool)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("ChelseaA_OS BLE 健康数据上位机")
        self.setMinimumSize(1000, 720)

        # BLE 管理器
        self.ble = BleManager()
        self.ble.on_health_data = self._on_health_data
        self.ble.on_log_data = self._on_log_data
        self.ble.on_lms_data = self._on_lms_data
        self.ble.on_log = self._on_ble_log
        self.ble.on_connection_changed = self._on_ble_connection_changed

        # 异步工作线程
        self.async_worker = AsyncWorker()
        self.async_thread = QThread()
        self.async_worker.moveToThread(self.async_thread)
        self.async_thread.started.connect(self.async_worker.start)
        self.async_thread.finished.connect(self.async_worker.deleteLater)
        self.async_thread.start()

        # 信号连接
        self.update_hr_signal.connect(self._update_hr_ui)
        self.update_hrv_signal.connect(self._update_hrv_ui)
        self.update_spo2_signal.connect(self._update_spo2_ui)
        self.update_adt_signal.connect(self._update_adt_ui)
        self.update_nadt_signal.connect(self._update_nadt_ui)
        self.append_rpc_log_signal.connect(self._append_rpc_log_ui)
        self.append_device_log_signal.connect(self._append_device_log_ui)
        self.append_host_log_signal.connect(self._append_host_log_ui)
        self.scan_complete_signal.connect(self._on_scan_complete)
        self.connection_changed_signal.connect(self._on_connection_changed_ui)

        # 扫描结果缓存
        self._scanned_devices = []

        # 构建 UI
        self._build_ui()

        # 状态定时器
        self._conn_check_timer = QTimer()
        self._conn_check_timer.timeout.connect(self._check_connection)
        self._conn_check_timer.start(1000)

    # ==================================================================
    # UI 构建
    # ==================================================================
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(6)

        # --- 主题风格 ---
        self.setStyleSheet("""
            QMainWindow { background-color: #11111b; }
            QLabel { color: #cdd6f4; }
            QGroupBox {
                color: #89b4fa;
                border: 1px solid #313244;
                border-radius: 8px;
                margin-top: 12px;
                padding: 12px 8px 8px 8px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 6px;
            }
            QPushButton {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 4px;
                padding: 6px 14px;
                font-size: 13px;
                min-height: 24px;
            }
            QPushButton:hover {
                background-color: #45475a;
                border-color: #585b70;
            }
            QPushButton:pressed {
                background-color: #585b70;
            }
            QPushButton:disabled {
                background-color: #1e1e2e;
                color: #585b70;
                border-color: #313244;
            }
            QPushButton#btn_scan {
                background-color: #1e66f5;
                border-color: #2a7afa;
            }
            QPushButton#btn_scan:hover { background-color: #2a7afa; }
            QPushButton#btn_connect {
                background-color: #40a02b;
                border-color: #50b03b;
            }
            QPushButton#btn_connect:hover { background-color: #50b03b; }
            QPushButton#btn_disconnect {
                background-color: #d20f39;
                border-color: #e21f49;
            }
            QPushButton#btn_disconnect:hover { background-color: #e21f49; }
            QPushButton#btn_cmd {
                background-color: #8839ef;
                border-color: #a04ef0;
                min-height: 28px;
                font-size: 12px;
            }
            QPushButton#btn_cmd:hover { background-color: #a04ef0; }
            QPushButton#btn_stop {
                background-color: #d20f39;
                border-color: #e21f49;
                min-height: 28px;
                font-size: 12px;
            }
            QPushButton#btn_stop:hover { background-color: #e21f49; }
            QPushButton#btn_version {
                background-color: #04a5e5;
                border-color: #15b5f5;
                min-height: 28px;
                font-size: 12px;
            }
            QPushButton#btn_version:hover { background-color: #15b5f5; }
            QComboBox {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 4px;
                padding: 4px 8px;
                min-height: 26px;
            }
            QComboBox:disabled { color: #585b70; }
            QComboBox::drop-down { border: none; width: 20px; }
            QComboBox QAbstractItemView {
                background-color: #313244;
                color: #cdd6f4;
                selection-background-color: #45475a;
            }
            QTextEdit, QPlainTextEdit {
                background-color: #1e1e2e;
                color: #cdd6f4;
                border: 1px solid #313244;
                border-radius: 4px;
                font-family: "Consolas", "Courier New", monospace;
                font-size: 12px;
            }
            QCheckBox {
                color: #a6adc8;
                spacing: 4px;
            }
            QCheckBox::indicator {
                width: 16px;
                height: 16px;
                border-radius: 3px;
                border: 1px solid #585b70;
            }
            QCheckBox::indicator:checked {
                background-color: #40a02b;
                border-color: #50b03b;
            }
            QLineEdit {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 4px;
                padding: 4px 8px;
            }
            QStatusBar {
                background-color: #181825;
                color: #a6adc8;
            }
            QTabWidget::pane {
                background-color: #1e1e2e;
                border: 1px solid #313244;
                border-radius: 4px;
            }
            QTabBar::tab {
                background-color: #181825;
                color: #a6adc8;
                padding: 6px 16px;
                border: 1px solid #313244;
                border-bottom: none;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
            }
            QTabBar::tab:selected {
                background-color: #1e1e2e;
                color: #89b4fa;
            }
        """)

        # ============== 顶部工具栏 ==============
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        self.btn_scan = QPushButton("🔍 扫描")
        self.btn_scan.setObjectName("btn_scan")
        self.btn_scan.clicked.connect(self._on_scan_click)
        toolbar.addWidget(self.btn_scan)

        self.device_combo = QComboBox()
        self.device_combo.setMinimumWidth(260)
        self.device_combo.setPlaceholderText("未发现设备")
        toolbar.addWidget(self.device_combo)

        self.btn_connect = QPushButton("🔗 连接")
        self.btn_connect.setObjectName("btn_connect")
        self.btn_connect.clicked.connect(self._on_connect_click)
        self.btn_connect.setEnabled(False)
        toolbar.addWidget(self.btn_connect)

        self.btn_disconnect = QPushButton("⛔ 断开")
        self.btn_disconnect.setObjectName("btn_disconnect")
        self.btn_disconnect.clicked.connect(self._on_disconnect_click)
        self.btn_disconnect.setEnabled(False)
        toolbar.addWidget(self.btn_disconnect)

        toolbar.addStretch()

        self.conn_status_label = QLabel("🔴 未连接")
        self.conn_status_label.setStyleSheet("color: #f38ba8; font-weight: bold; font-size: 13px;")
        toolbar.addWidget(self.conn_status_label)

        main_layout.addLayout(toolbar)

        # ============== 主内容区 ==============
        splitter = QSplitter(Qt.Horizontal)

        # --- 左侧: 健康数据显示 ---
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 4, 0)
        left_layout.setSpacing(6)

        # 健康数据卡片
        health_group = QGroupBox("📈 健康数据")
        health_grid = QGridLayout(health_group)
        health_grid.setSpacing(8)

        self.card_hr = HealthCard("心率 (HR)", "bpm", "❤")
        health_grid.addWidget(self.card_hr, 0, 0)

        self.card_hrv = HealthCard("心率变异性 (HRV)", "ms", "📊")
        health_grid.addWidget(self.card_hrv, 0, 1)

        self.card_spo2 = HealthCard("血氧 (SpO₂)", "%", "🫁")
        health_grid.addWidget(self.card_spo2, 0, 2)

        # 佩戴/接触状态
        status_frame = QFrame()
        status_frame.setStyleSheet("""
            QFrame { background-color: #1e1e2e; border: 1px solid #313244; border-radius: 6px; }
        """)
        status_layout = QHBoxLayout(status_frame)
        status_layout.setContentsMargins(12, 8, 12, 8)

        self.contact_label = QLabel("📡 Contact: --")
        self.contact_label.setStyleSheet("color: #a6adc8; font-size: 13px;")
        status_layout.addWidget(self.contact_label)

        self.wear_label = QLabel("📿 Wear: --")
        self.wear_label.setStyleSheet("color: #a6adc8; font-size: 13px;")
        status_layout.addWidget(self.wear_label)

        self.adt_label = QLabel("📌 ADT: --")
        self.adt_label.setStyleSheet("color: #a6adc8; font-size: 13px;")
        status_layout.addWidget(self.adt_label)

        health_grid.addWidget(status_frame, 1, 0, 1, 3)
        left_layout.addWidget(health_group)

        # RPC 消息日志
        rpc_group = QGroupBox("💬 RPC 消息")
        rpc_layout = QVBoxLayout(rpc_group)
        self.rpc_log = QTextEdit()
        self.rpc_log.setReadOnly(True)
        self.rpc_log.document().setMaximumBlockCount(500)
        rpc_layout.addWidget(self.rpc_log)
        left_layout.addWidget(rpc_group)

        splitter.addWidget(left_widget)

        # --- 右侧: 命令面板 ---
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(4, 0, 0, 0)
        right_layout.setSpacing(6)

        cmd_group = QGroupBox("⌨ 命令")
        cmd_layout = QVBoxLayout(cmd_group)
        cmd_layout.setSpacing(8)

        # 功能按钮
        btn_grid = QGridLayout()
        btn_grid.setSpacing(6)

        self.btn_hr_hrv = QPushButton("▶ HR+HRV")
        self.btn_hr_hrv.setObjectName("btn_cmd")
        self.btn_hr_hrv.clicked.connect(lambda: self._send_sw_command(0x000A, 0))  # HR(0x0002)|HRV(0x0008)
        self.btn_hr_hrv.setEnabled(False)
        btn_grid.addWidget(self.btn_hr_hrv, 0, 0)

        self.btn_spo2 = QPushButton("▶ SpO₂")
        self.btn_spo2.setObjectName("btn_cmd")
        self.btn_spo2.clicked.connect(lambda: self._send_sw_command(0x0004, 0))  # SpO2
        self.btn_spo2.setEnabled(False)
        btn_grid.addWidget(self.btn_spo2, 0, 1)

        self.btn_adt = QPushButton("▶ ADT")
        self.btn_adt.setObjectName("btn_cmd")
        self.btn_adt.clicked.connect(lambda: self._send_sw_command(0x0001, 0))
        self.btn_adt.setEnabled(False)
        btn_grid.addWidget(self.btn_adt, 0, 2)

        self.btn_stop = QPushButton("■ 停止")
        self.btn_stop.setObjectName("btn_stop")
        self.btn_stop.clicked.connect(lambda: self._send_sw_command(0xFFFFFFFF, 1))
        self.btn_stop.setEnabled(False)
        btn_grid.addWidget(self.btn_stop, 1, 0)

        self.btn_version = QPushButton("ℹ 版本")
        self.btn_version.setObjectName("btn_version")
        self.btn_version.clicked.connect(lambda: self._send_command_async(build_get_version_cmd(0x01)))
        self.btn_version.setEnabled(False)
        btn_grid.addWidget(self.btn_version, 1, 1)

        self.mode_label = QLabel("模式: 在线")
        self.mode_label.setStyleSheet("color: #a6adc8; font-size: 12px;")
        btn_grid.addWidget(self.mode_label, 1, 2)

        cmd_layout.addLayout(btn_grid)

        # 自定义命令
        custom_row = QHBoxLayout()
        self.custom_cmd_input = QLineEdit()
        self.custom_cmd_input.setPlaceholderText(
            "GH3X_SwFunctionCmd 0x0006 0  # 启动 HR+HRV"
        )
        self.custom_cmd_input.returnPressed.connect(self._on_custom_cmd)
        custom_row.addWidget(self.custom_cmd_input, 1)

        self.btn_send_custom = QPushButton("发送")
        self.btn_send_custom.setObjectName("btn_cmd")
        self.btn_send_custom.clicked.connect(self._on_custom_cmd)
        self.btn_send_custom.setEnabled(False)
        custom_row.addWidget(self.btn_send_custom)
        cmd_layout.addLayout(custom_row)

        # 提示文字
        hint = QLabel("💡 提示: 设备按 SW1=心率, SW2=血氧, SW3=测试, ADT=自动模式")
        hint.setStyleSheet("color: #585b70; font-size: 11px; padding: 4px 0;")
        hint.setWordWrap(True)
        cmd_layout.addWidget(hint)

        cmd_layout.addStretch()

        right_layout.addWidget(cmd_group)

        splitter.addWidget(right_widget)
        splitter.setSizes([600, 400])
        main_layout.addWidget(splitter, 1)

        # ============== 调试选项 ==============
        debug_row = QHBoxLayout()
        self.debug_raw_check = QCheckBox("🔬 显示原始 RPC 数据帧")
        self.debug_raw_check.setStyleSheet("color: #a6adc8; font-size: 12px;")
        debug_row.addWidget(self.debug_raw_check)
        debug_row.addStretch()
        main_layout.addLayout(debug_row)

        # ============== 底部日志区域 ==============
        bottom_splitter = QSplitter(Qt.Vertical)

        # 设备日志
        dev_log_group = QGroupBox("📟 设备日志")
        dev_layout = QVBoxLayout(dev_log_group)
        self.device_log = QPlainTextEdit()
        self.device_log.setReadOnly(True)
        self.device_log.setMaximumBlockCount(300)
        dev_layout.addWidget(self.device_log)
        bottom_splitter.addWidget(dev_log_group)

        # 主机日志
        host_log_group = QGroupBox("🖥 主机日志")
        host_layout = QVBoxLayout(host_log_group)
        self.host_log = QPlainTextEdit()
        self.host_log.setReadOnly(True)
        self.host_log.setMaximumBlockCount(300)
        host_layout.addWidget(self.host_log)
        bottom_splitter.addWidget(host_log_group)

        bottom_splitter.setSizes([150, 150])
        main_layout.addWidget(bottom_splitter)

        # 状态栏
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("就绪 - 点击「🔍 扫描」搜索设备")

    # ==================================================================
    # 扫描/连接/断开
    # ==================================================================
    def _on_scan_click(self):
        self.btn_scan.setEnabled(False)
        self.btn_scan.setText("⏳ 扫描中...")
        self.status_bar.showMessage("正在扫描 BLE 设备...")
        self._log_host("开始扫描...")

        self.async_worker.run_coro(self._scan_async())

    async def _scan_async(self):
        try:
            devices = await self.ble.scan(timeout=3.0)
            # 去重
            seen = set()
            unique = []
            for d in devices:
                if d.address not in seen:
                    seen.add(d.address)
                    unique.append(d)
            self.scan_complete_signal.emit((unique, None))
        except Exception as e:
            self.scan_complete_signal.emit(([], str(e)))

    def _on_scan_complete(self, result):
        devices, error = result
        self.btn_scan.setEnabled(True)
        self.btn_scan.setText("🔍 扫描")

        if error:
            self._log_host(f"扫描失败: {error}")
            self.status_bar.showMessage(f"扫描失败: {error}")
            return

        self._scanned_devices = devices
        self.device_combo.clear()
        for d in devices:
            name = d.name or "Unknown"
            self.device_combo.addItem(f"{name} [{d.address}]", d)

        if devices:
            self.device_combo.setCurrentIndex(0)
            self.btn_connect.setEnabled(True)
            self.status_bar.showMessage(f"发现 {len(devices)} 个设备, 选择后点击连接")
        else:
            self.device_combo.setPlaceholderText("未发现设备")
            self.btn_connect.setEnabled(False)
            self.status_bar.showMessage("未发现设备, 请确认设备已开机并广播")

    def _on_connect_click(self):
        idx = self.device_combo.currentIndex()
        if idx < 0 or idx >= len(self._scanned_devices):
            return
        device = self.device_combo.currentData()
        self.btn_connect.setEnabled(False)
        self.btn_disconnect.setEnabled(False)
        self.device_combo.setEnabled(False)
        self.btn_scan.setEnabled(False)
        self.status_bar.showMessage(f"正在连接 {device.name}...")
        self._log_host(f"连接中: {device.name} [{device.address}]")

        self.async_worker.run_coro(self._connect_async(device))

    async def _connect_async(self, device):
        try:
            await self.ble.connect(device)
        except Exception as e:
            self._log_host(f"连接失败: {e}")
            self.connection_changed_signal.emit(False)

    def _on_disconnect_click(self):
        self._log_host("正在断开连接...")
        self.async_worker.run_coro(self.ble.disconnect())

    def _on_connection_changed_ui(self, connected: bool):
        self.btn_connect.setEnabled(not connected and len(self._scanned_devices) > 0)
        self.btn_disconnect.setEnabled(connected)
        self.btn_scan.setEnabled(not connected)
        self.device_combo.setEnabled(not connected)
        self.btn_hr_hrv.setEnabled(connected)
        self.btn_spo2.setEnabled(connected)
        self.btn_adt.setEnabled(connected)
        self.btn_stop.setEnabled(connected)
        self.btn_version.setEnabled(connected)
        self.btn_send_custom.setEnabled(connected)

        if connected:
            self.conn_status_label.setText("🟢 已连接")
            self.conn_status_label.setStyleSheet("color: #a6e3a1; font-weight: bold; font-size: 13px;")
            self.status_bar.showMessage("已连接 - 可通过命令或设备按键启动监测")
            self._log_host("连接成功! ✓")

        else:
            self.conn_status_label.setText("🔴 未连接")
            self.conn_status_label.setStyleSheet("color: #f38ba8; font-weight: bold; font-size: 13px;")
            self.status_bar.showMessage("已断开连接")

    def _check_connection(self):
        """定期检查连接状态"""
        if self.ble.is_connected:
            pass  # 连接状态正常

    # ==================================================================
    # ==================================================================
    # 命令发送
    # ==================================================================
    def _send_sw_command(self, mode: int, ctrl: int):
        """发送功能开关命令"""
        cmd = build_sw_function_cmd(mode, ctrl)
        names = {0x000A: "HR+HRV", 0x0004: "SpO₂", 0x0001: "ADT", 0xFFFFFFFF: "全部"}
        action = "▶ 启动" if ctrl == 0 else "■ 停止"
        name = names.get(mode, f"0x{mode:04X}")
        self._log_host(f"{action} {name}")
        self.async_worker.run_coro(self._send_cmd_async(cmd, name))

    def _send_command_async(self, cmd: bytes):
        """发送自定义命令帧"""
        self.async_worker.run_coro(self._send_cmd_async(cmd, "自定义"))

    async def _send_cmd_async(self, cmd: bytes, label: str = ""):
        """异步发送命令到设备"""
        ok = await self.ble.send_command(cmd)
        if ok:
            self._log_host(f"命令发送成功: {label} ({len(cmd)}B)")
        else:
            self._log_host(f"命令发送失败: {label}")

    def _on_custom_cmd(self):
        """处理自定义命令输入"""
        text = self.custom_cmd_input.text().strip()
        if not text:
            return

        # 去除注释
        if "#" in text:
            text = text.split("#")[0].strip()

        parts = text.split()
        if not parts:
            return

        try:
            cmd_name = parts[0]
            args = [int(x, 0) for x in parts[1:]]

            if cmd_name == "GH3X_SwFunctionCmd" and len(args) >= 2:
                cmd = build_sw_function_cmd(args[0], args[1])
                self._log_host(f"自定义: {cmd_name}(0x{args[0]:X}, {args[1]})")
                self.async_worker.run_coro(self._send_cmd_async(cmd, cmd_name))
            elif cmd_name == "GH3X_GetVersion" and len(args) >= 1:
                cmd = build_get_version_cmd(args[0])
                self._log_host(f"自定义: {cmd_name}(0x{args[0]:02X})")
                self.async_worker.run_coro(self._send_cmd_async(cmd, cmd_name))
            else:
                self._log_host(f"未知命令或参数错误: {text}")
        except (ValueError, IndexError) as e:
            self._log_host(f"命令解析错误: {e}")

    # ==================================================================
    # BLE 数据回调 (在 asyncio 线程中调用)
    # ==================================================================
    def _on_health_data(self, data: bytes):
        """HEALTH TX GH RPC 数据帧回调

        HEALTH TX 通道仅承载 GH RPC 二进制帧 (AA 11 开头),
        ble_printf 文本日志已分离至独立的 HEALTH LOG TX 通道 (UUID 00000005-...).
        """
        # 尝试解析 GH RPC 帧
        frame = parse_rpc_frame(data)
        if frame:
            key = frame["key"]
            payload = frame["payload"]

            if key == "G":
                # "G" 键载荷外有 <u8*> 格式信封: [TypeHdr][len][data...]
                raw_bytes = unwrap_g_key_payload(payload)
                if raw_bytes is None:
                    hex_str = payload.hex()
                    spaced = " ".join(hex_str[i:i+32] for i in range(0, len(hex_str), 32))
                    self.append_rpc_log_signal.emit(
                        f"[GH RPC] G键信封格式错误 ({len(payload)} bytes) {spaced}",
                        "parse_err"
                    )
                    return

                # 调试: 显示原始数据流 (复选框勾选时)
                if self.debug_raw_check.isChecked():
                    hex_str = raw_bytes.hex()
                    # 每 32 个 hex 字符(16 bytes)加空格
                    spaced = " ".join(hex_str[i:i+32] for i in range(0, len(hex_str), 32))
                    self.append_rpc_log_signal.emit(
                        f"[hex] ({len(raw_bytes)}B) {spaced}", "raw"
                    )

                # 解析数据帧字节流 (可含多帧, 差分编码)
                df = parse_data_frame(raw_bytes)
                if df:
                    func_id = df.get("function_id")
                    algo = df.get("algo_results", {})
                    fid = df.get("frame_id", 0)
                    func_name = df.get("func_name", "?")

                    if func_id == FuncId.HR:
                        self.update_hr_signal.emit(algo)
                    elif func_id == FuncId.HRV:
                        self.update_hrv_signal.emit(algo)
                    elif func_id == FuncId.SPO2:
                        self.update_spo2_signal.emit(algo)
                    elif func_id == FuncId.ADT:
                        self.update_adt_signal.emit(algo)
                    elif func_id in (FuncId.GNADT, FuncId.IRNADT):
                        self.update_nadt_signal.emit(algo)

                    log = f"[{func_name}] frame={fid}"
                    if algo:
                        log += " " + str(algo)
                    self.append_rpc_log_signal.emit(log, func_name or "?")
                else:
                    msg = f"[GH RPC] 数据帧解析失败 ({len(raw_bytes)} bytes)"
                    if self.debug_raw_check.isChecked():
                        hex_str = raw_bytes.hex()
                        spaced = " ".join(hex_str[i:i+32] for i in range(0, len(hex_str), 32))
                        msg += " " + spaced
                    self.append_rpc_log_signal.emit(msg, "parse_err")
            else:
                # 非 "G" 键 → 命令响应
                try:
                    resp = payload.decode('ascii', errors='replace').strip()
                    if resp:
                        self.append_rpc_log_signal.emit(f"[{key}] {resp}", "rsp")
                except Exception:
                    self.append_rpc_log_signal.emit(
                        f"[{key}] payload={payload.hex()}", "rsp"
                    )
        else:
            # 非 RPC 非文本 → 原始 hex
            self.append_rpc_log_signal.emit(
                f"[hex] {len(data)} bytes: {data[:48].hex()}", "raw"
            )

    def _on_log_data(self, data: bytes):
        """HEALTH LOG TX 文本日志回调 (ble_printf)"""
        if data:
            try:
                text = data.decode('ascii', errors='replace').strip()
                if text:
                    self.append_device_log_signal.emit(text)
            except Exception:
                pass

    def _on_lms_data(self, data: bytes):
        """LMS 日志数据回调"""
        if data:
            try:
                text = data.decode('ascii', errors='replace').strip()
                if text:
                    self.append_device_log_signal.emit(text)
            except Exception:
                pass

    def _on_ble_log(self, msg: str):
        """BLE 管理器日志回调"""
        self.append_host_log_signal.emit(msg)

    def _on_ble_connection_changed(self, connected: bool):
        """BLE 连接状态变化回调"""
        self.connection_changed_signal.emit(connected)

    # ==================================================================
    # UI 更新 (在 Qt 主线程中)
    # ==================================================================
    def _update_hr_ui(self, data: dict):
        """更新心率 UI (来自 GH RPC 数据帧)"""
        hr = data.get("hr", 0)
        score = data.get("score", 0)
        if hr is not None and hr > 0:
            self.card_hr.set_value(hr, "bpm")
            self.card_hr.setStyleSheet("""
                HealthCard { background-color: #1e1e2e; border: 1px solid #f38ba8;
                             border-radius: 8px; padding: 8px; }
            """)
        if score is not None:
            self.contact_label.setText(f"📡 Score: {score}")

    def _update_hrv_ui(self, data: dict):
        """更新 HRV UI"""
        rri = data.get("rri", [])
        confidence = data.get("confidence", 0)

        if rri:
            avg_rri = sum(rri) / len(rri)
            self.card_hrv.set_value(f"{avg_rri:.0f}", "ms")
            self.card_hrv.setStyleSheet("""
                HealthCard { background-color: #1e1e2e; border: 1px solid #cba6f7;
                             border-radius: 8px; padding: 8px; }
            """)

    def _update_spo2_ui(self, data: dict):
        """更新血氧 UI"""
        spo2 = data.get("spo2", 0)
        valid = data.get("valid_level", 0)
        hb = data.get("hb_mean", 0)

        if spo2 and spo2 > 0:
            self.card_spo2.set_value(f"{spo2:.1f}", "%")
            self.card_spo2.setStyleSheet("""
                HealthCard { background-color: #1e1e2e; border: 1px solid #89b4fa;
                             border-radius: 8px; padding: 8px; }
            """)

    def _update_adt_ui(self, data: dict):
        """更新 ADT UI"""
        wear_evt = data.get("wear_event")
        det_status = data.get("det_status")
        ctr = data.get("ctr")

        evt_map = {0: "无事件", 1: "佩戴", 2: "摘下", 3: "未知"}
        status_map = {0: "未检测", 1: "未佩戴", 2: "已佩戴"}

        if wear_evt is not None:
            self.adt_label.setText(f"📌 ADT: {evt_map.get(wear_evt, str(wear_evt))}")
            if wear_evt == 2:
                self.adt_label.setStyleSheet("color: #f38ba8; font-size: 13px;")
            elif wear_evt == 1:
                self.adt_label.setStyleSheet("color: #a6e3a1; font-size: 13px;")

    def _update_nadt_ui(self, data: dict):
        """更新 NADT UI"""
        status = data.get("wear_status", "unknown")
        confidence = data.get("confidence", 0)

        self.wear_label.setText(f"📿 NADT: {status} (conf={confidence})")
        if "wear_off" in status or "non_living" in status:
            self.wear_label.setStyleSheet("color: #f38ba8; font-size: 13px;")
        elif "wear_on" in status:
            self.wear_label.setStyleSheet("color: #a6e3a1; font-size: 13px;")
        else:
            self.wear_label.setStyleSheet("color: #a6adc8; font-size: 13px;")

    def _append_rpc_log_ui(self, msg: str, tag: str = ""):
        """追加 RPC 消息日志"""
        ts = datetime.now().strftime("%H:%M:%S")
        color_map = {
            "HR": "#f38ba8",
            "HRV": "#cba6f7",
            "SpO2": "#89b4fa",
            "ADT": "#f9e2af",
            "G-NADT": "#94e2d5",
            "IR-NADT": "#74c7ec",
            "parse_err": "#f38ba8",
            "rsp": "#a6e3a1",
        }
        color = color_map.get(tag, "#cdd6f4")
        self.rpc_log.append(f'<span style="color: #585b70;">[{ts}]</span> '
                            f'<span style="color: {color};">{msg}</span>')

    def _append_device_log_ui(self, msg: str):
        """追加设备日志"""
        ts = datetime.now().strftime("%H:%M:%S")
        self.device_log.appendPlainText(f"[{ts}] {msg}")

    def _append_host_log_ui(self, msg: str):
        """追加主机日志"""
        ts = datetime.now().strftime("%H:%M:%S")
        self.host_log.appendPlainText(f"[{ts}] {msg}")

    def _log_host(self, msg: str):
        """跨线程安全地记录主机日志"""
        self.append_host_log_signal.emit(msg)

    # ==================================================================
    # 窗口关闭
    # ==================================================================
    def closeEvent(self, event):
        """窗口关闭时清理资源"""
        self._conn_check_timer.stop()

        # 先断开 BLE（此时事件循环仍在运行）
        if self.ble.is_connected and self.async_worker.loop:
            try:
                future = asyncio.run_coroutine_threadsafe(
                    self.ble.disconnect(), self.async_worker.loop
                )
                future.result(timeout=3.0)
            except Exception:
                pass

        # 然后停止事件循环和线程
        self.async_worker.stop()
        self.async_thread.quit()
        self.async_thread.wait(2000)

        event.accept()


# ======================================================================
# 主入口
# ======================================================================
def main():
    """启动上位机应用"""
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # 深色配色
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor("#11111b"))
    palette.setColor(QPalette.WindowText, QColor("#cdd6f4"))
    palette.setColor(QPalette.Base, QColor("#1e1e2e"))
    palette.setColor(QPalette.AlternateBase, QColor("#313244"))
    palette.setColor(QPalette.ToolTipBase, QColor("#313244"))
    palette.setColor(QPalette.ToolTipText, QColor("#cdd6f4"))
    palette.setColor(QPalette.Text, QColor("#cdd6f4"))
    palette.setColor(QPalette.Button, QColor("#313244"))
    palette.setColor(QPalette.ButtonText, QColor("#cdd6f4"))
    palette.setColor(QPalette.BrightText, QColor("#f38ba8"))
    palette.setColor(QPalette.Link, QColor("#89b4fa"))
    palette.setColor(QPalette.Highlight, QColor("#45475a"))
    palette.setColor(QPalette.HighlightedText, QColor("#cdd6f4"))
    app.setPalette(palette)

    window = MainWindow()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
