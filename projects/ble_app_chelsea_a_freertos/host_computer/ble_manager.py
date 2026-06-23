"""
BLE 管理器 - 基于 bleak 的蓝牙低功耗通信

负责扫描、连接、服务发现的 BLE 通信层。
"""

import asyncio
from typing import Optional, Callable

from bleak import BleakScanner, BleakClient
from bleak.backends.device import BLEDevice
from bleak.backends.characteristic import BleakGATTCharacteristic

# ----- UUID 定义 (来自 firmware health.h) -----
HEALTH_SERVICE_UUID = "0000190e-0000-1000-8000-00805f9b34fb"

# HEALTH 自定义服务
HEALTH_SERVICE_128 = "0000190e-0000-1000-8000-00805f9b34fb"
HEALTH_TX_UUID     = "00000003-0000-1000-8000-00805f9b34fb"
HEALTH_RX_UUID     = "00000004-0000-1000-8000-00805f9b34fb"
HEALTH_LOG_UUID    = "00000005-0000-1000-8000-00805f9b34fb"

# 调试日志服务
LMS_SERVICE_UUID = "a6ed0b01-0000-4000-8000-00805f9b34fb"
LMS_DATA_UUID    = "a6ed0b03-0000-4000-8000-00805f9b34fb"

DEVICE_NAME = "ChelseaA_OS"

SCAN_TIMEOUT = 5.0


class BleManager:
    """BLE 管理器"""

    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None

        # 特征句柄缓存
        self._health_tx_handle: Optional[int] = None
        self._health_rx_handle: Optional[int] = None
        self._health_log_handle: Optional[int] = None
        self._lms_data_handle: Optional[int] = None

        # 通知回调
        self.on_health_data = None  # Callable[[bytes], None]
        self.on_log_data = None     # Callable[[bytes], None] — ble_printf 文本日志
        self.on_lms_data = None     # Callable[[bytes], None]
        self.on_log = None          # Callable[[str], None]
        self.on_connection_changed = None  # Callable[[bool], None]

    def _log(self, msg: str):
        if self.on_log:
            self.on_log(msg)

    # ----- 扫描 -----
    async def scan(self, timeout: float = SCAN_TIMEOUT) -> list:
        """扫描 BLE 设备并返回发现的设备列表"""
        devices = []
        self._log(f"开始扫描 (超时 {timeout}s)...")

        def callback(d, ad):
            if d.name and (DEVICE_NAME in d.name):
                devices.append(d)
                self._log(f"发现: {d.name} [{d.address}]")

        scanner = BleakScanner(detection_callback=callback)
        await scanner.start()
        await asyncio.sleep(timeout)
        await scanner.stop()

        self._log(f"扫描完成, 发现 {len(devices)} 个 {DEVICE_NAME} 设备")
        return devices

    # ----- 连接 -----
    async def connect(self, device: BLEDevice):
        """连接到指定 BLE 设备"""
        self.device = device
        self._log(f"正在连接到 {device.name} [{device.address}]...")

        self.client = BleakClient(device, timeout=30.0,
                                  disconnected_callback=self._on_disconnected)
        try:
            await self.client.connect()
            self._log("已连接, 正在发现服务...")
            await self._discover_services()
            self._log("服务发现完成, 注册通知...")
            await self._subscribe_notifications()
            self._log("通知注册完成")
            if self.on_connection_changed:
                self.on_connection_changed(True)
        except Exception as e:
            self._log(f"连接失败: {e}")
            self.client = None
            raise

    def _on_disconnected(self, client):
        """BLE 意外断线回调"""
        self._log("⚠ 设备断开连接 (意外断线)")
        self.client = None
        self.device = None
        if self.on_connection_changed:
            self.on_connection_changed(False)

    # ----- 断开 -----
    async def disconnect(self):
        """断开 BLE 连接"""
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        self.client = None
        self.device = None
        self._log("已断开连接")
        if self.on_connection_changed:
            self.on_connection_changed(False)

    @property
    def is_connected(self) -> bool:
        return self.client is not None and self.client.is_connected

    # ----- 服务发现 -----
    async def _discover_services(self):
        """发现并缓存特征句柄"""
        if not self.client:
            return

        for service in self.client.services:
            for char in service.characteristics:
                uuid = char.uuid.lower()
                if uuid == HEALTH_TX_UUID.lower():
                    self._health_tx_handle = char.handle
                    self._log(f"  HEALTH TX 特征: {uuid}")
                elif uuid == HEALTH_RX_UUID.lower():
                    self._health_rx_handle = char.handle
                    self._log(f"  HEALTH RX 特征: {uuid}")
                elif uuid == HEALTH_LOG_UUID.lower():
                    self._health_log_handle = char.handle
                    self._log(f"  HEALTH LOG 特征: {uuid}")
                elif uuid == LMS_DATA_UUID.lower():
                    self._lms_data_handle = char.handle
                    self._log(f"  LMS 数据特征: {uuid}")

        if self._health_tx_handle:
            self._log(f"  [OK] HEALTH TX 已找到 (handle={self._health_tx_handle})")
        else:
            self._log("  [WARN] HEALTH TX 未找到!")

    # ----- 通知订阅 -----
    async def _subscribe_notifications(self):
        """订阅所有需要通知的特征"""
        if not self.client:
            return

        # 订阅 HEALTH TX 通知 (GH RPC 数据帧 + 文本日志)
        if self._health_tx_handle:
            await self.client.start_notify(
                self._health_tx_handle,
                self._on_health_notification
            )
            self._log("  已订阅 HEALTH TX")

        # 订阅 HEALTH LOG TX 通知 (ble_printf 文本日志)
        if self._health_log_handle:
            await self.client.start_notify(
                self._health_log_handle,
                self._on_log_notification
            )
            self._log("  已订阅 HEALTH LOG TX")

        # 可选: 订阅 LMS 日志通知
        if self._lms_data_handle:
            try:
                await self.client.start_notify(
                    self._lms_data_handle,
                    self._on_lms_notification
                )
                self._log("  已订阅 LMS 数据")
            except Exception:
                self._log("  [WARN] 无法订阅 LMS, 跳过")

    # ----- 通知回调 -----
    def _on_health_notification(self, sender: int, data: bytearray):
        """HEALTH TX GH RPC 数据帧通知回调"""
        if self.on_health_data:
            self.on_health_data(bytes(data))

    def _on_log_notification(self, sender: int, data: bytearray):
        """HEALTH LOG TX 文本日志通知回调 (ble_printf)"""
        if self.on_log_data:
            self.on_log_data(bytes(data))

    def _on_lms_notification(self, sender: int, data: bytearray):
        """LMS 日志通知回调"""
        if self.on_lms_data:
            self.on_lms_data(bytes(data))

    # ----- 发送命令 -----
    async def send_command(self, data: bytes) -> bool:
        """通过 HEALTH RX 特征发送命令数据"""
        if not self.is_connected or self._health_rx_handle is None:
            self._log("无法发送: 未连接或 HEALTH RX 未找到")
            return False
        try:
            await self.client.write_gatt_char(
                self._health_rx_handle,
                data,
                response=True
            )
            self._log(f"发送 OK: {len(data)} 字节")
            return True
        except Exception as e:
            self._log(f"发送失败: {e}")
            return False
