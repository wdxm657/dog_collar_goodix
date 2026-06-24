"""
IMU 数据文件日志 — 将上位机收到的 IMU 采样写入 CSV

供 imu_test.py 离线回放调试使用。
"""

import os
import csv
from datetime import datetime


class ImuDataLogger:
    """IMU 采样 CSV 记录器

    路径: host_computer/imu_data_<日期>.csv
    格式: timestamp, ax, ay, az
    """

    def __init__(self):
        self._file = None
        self._writer = None
        self._count = 0

    def start(self):
        """打开日志文件并写入表头"""
        if self._file:
            return
        log_dir = os.path.dirname(os.path.abspath(__file__))
        fname = "imu_data_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
        path = os.path.join(log_dir, fname)
        self._file = open(path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(["timestamp", "ax", "ay", "az"])
        self._file.flush()
        print(f"[IMU Logger] → {path}")

    def write(self, ts: int, ax: float, ay: float, az: float):
        """写入一条采样记录"""
        if self._writer is not None:
            self._writer.writerow([ts, int(ax), int(ay), int(az)])
            self._count += 1
            if self._count % 50 == 0:
                self._file.flush()

    def stop(self):
        """关闭日志文件"""
        if self._file:
            self._file.flush()
            self._file.close()
            self._file = None
            self._writer = None
            print(f"[IMU Logger] 已保存 {self._count} 条记录")
            self._count = 0

    def is_active(self) -> bool:
        return self._file is not None
