"""
IMU 数据离线回放测试工具

从 imu_data_*.csv 读取录制的 IMU 采样数据，
在独立窗口中用 3D 立方体回放，用于调试显示平滑度。

用法:
    python imu_test.py                   # 打开文件选择对话框
    python imu_test.py imu_data.csv      # 直接指定文件
"""

import sys
import csv
import math
import os

from PyQt5.QtCore import Qt, QTimer, QRectF, QPointF
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QSlider, QFileDialog, QFrame,
)
from PyQt5.QtGui import (
    QPainter, QPainterPath, QColor, QPen, QBrush, QFont,
)

from imu_widget import Cube3DCanvas


# ======================================================================
# 回放控制组件
# ======================================================================
class PlaybackController(QFrame):
    """回放控制: 加载 CSV, 播放/暂停, 速度, 进度"""

    def __init__(self, canvas: Cube3DCanvas, parent=None):
        super().__init__(parent)
        self._canvas = canvas

        # 数据
        self._samples = []          # [(timestamp, ax, ay, az), ...]
        self._playing = False
        self._play_index = 0
        self._speed = 1.0           # 倍速
        self._timer_interval = 40   # base = 40ms (25Hz)
        self._file_path = ""

        self._build_ui()
        self._init_timer()

    def _build_ui(self):
        self.setStyleSheet("""
            PlaybackController {
                background-color: #1e1e2e;
                border: 1px solid #313244;
                border-radius: 8px;
            }
            QPushButton {
                background-color: #313244; color: #cdd6f4;
                border: 1px solid #45475a; border-radius: 4px;
                padding: 6px 14px; font-size: 13px;
            }
            QPushButton:hover { background-color: #45475a; }
            QPushButton:disabled { color: #585b70; }
            QLabel { color: #cdd6f4; }
            QSlider::groove:horizontal {
                height: 6px; background: #313244; border-radius: 3px;
            }
            QSlider::handle:horizontal {
                background: #89b4fa; width: 14px; margin: -4px 0;
                border-radius: 7px;
            }
            QSlider::sub-page:horizontal {
                background: #89b4fa; border-radius: 3px;
            }
        """)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)

        # ---- 文件信息 ----
        self._file_label = QLabel("📂 未加载文件")
        self._file_label.setStyleSheet("color: #a6adc8; font-size: 12px;")
        layout.addWidget(self._file_label)

        # ---- 控制按钮行 ----
        btn_row = QHBoxLayout()

        self._btn_load = QPushButton("📂 加载 CSV")
        self._btn_load.clicked.connect(self._on_load)
        btn_row.addWidget(self._btn_load)

        self._btn_play = QPushButton("▶ 播放")
        self._btn_play.setEnabled(False)
        self._btn_play.clicked.connect(self._toggle_play)
        btn_row.addWidget(self._btn_play)

        self._btn_stop = QPushButton("⏹ 停止")
        self._btn_stop.setEnabled(False)
        self._btn_stop.clicked.connect(self._stop)
        btn_row.addWidget(self._btn_stop)

        btn_row.addStretch()

        # 速度
        speed_label = QLabel("速度:")
        speed_label.setStyleSheet("color: #a6adc8; font-size: 12px;")
        btn_row.addWidget(speed_label)

        self._speed_combo = QPushButton("1.0×")
        self._speed_combo.setFixedWidth(60)
        self._speed_combo.clicked.connect(self._cycle_speed)
        btn_row.addWidget(self._speed_combo)

        layout.addLayout(btn_row)

        # ---- 进度条 ----
        self._progress_slider = QSlider(Qt.Horizontal)
        self._progress_slider.setRange(0, 1000)
        self._progress_slider.valueChanged.connect(self._on_seek)
        layout.addWidget(self._progress_slider)

        # ---- 状态行 ----
        status_row = QHBoxLayout()
        self._frame_label = QLabel("帧: 0 / 0")
        self._frame_label.setStyleSheet("color: #585b70; font-size: 11px;")
        status_row.addWidget(self._frame_label)

        self._rate_label = QLabel("-- FPS")
        self._rate_label.setStyleSheet("color: #585b70; font-size: 11px;")
        status_row.addWidget(self._rate_label)

        self._info_label = QLabel("总时长: --")
        self._info_label.setStyleSheet("color: #585b70; font-size: 11px;")
        status_row.addWidget(self._info_label)

        status_row.addStretch()
        layout.addLayout(status_row)

    def _init_timer(self):
        """初始化回放定时器"""
        self._timer = QTimer()
        self._timer.timeout.connect(self._tick)
        self._timer_interval = 40  # 25Hz base
        self._timer.start(self._timer_interval)

    # ============== 文件加载 ==============

    def load_file(self, path: str):
        """加载 CSV 文件"""
        if not os.path.exists(path):
            return

        self._stop()
        self._samples.clear()

        try:
            with open(path, "r", encoding="utf-8") as f:
                reader = csv.reader(f)
                header = next(reader, None)  # skip header
                for row in reader:
                    if len(row) >= 4:
                        ts = int(row[0])
                        ax, ay, az = float(row[1]), float(row[2]), float(row[3])
                        self._samples.append((ts, ax, ay, az))
        except Exception as e:
            self._info_label.setText(f"加载失败: {e}")
            return

        if not self._samples:
            self._info_label.setText("文件无有效数据")
            return

        # 按时间戳排序
        self._samples.sort(key=lambda s: s[0])

        self._file_path = path
        fname = os.path.basename(path)
        self._file_label.setText(f"📂 {fname} ({len(self._samples)} 帧)")

        duration_s = (self._samples[-1][0] - self._samples[0][0]) / 1000.0
        if duration_s < 1:
            duration_s = len(self._samples) * 0.04  # 估算

        self._frame_label.setText(f"帧: 0 / {len(self._samples)}")
        self._info_label.setText(f"总时长: {duration_s:.1f}s")

        self._progress_slider.setValue(0)
        self._play_index = 0
        self._btn_play.setEnabled(True)
        self._btn_play.setText("▶ 播放")
        self._playing = False
        self._canvas.set_orientation(0, 0)

        print(f"[Test] 已加载 {len(self._samples)} 帧, 时长 ~{duration_s:.1f}s")

    # ============== 回放控制 ==============

    def _toggle_play(self):
        if self._playing:
            self._pause()
        else:
            self._play()

    def _play(self):
        if self._play_index >= len(self._samples):
            self._play_index = 0
        self._playing = True
        self._btn_play.setText("⏸ 暂停")
        self._btn_stop.setEnabled(True)
        print(f"[Test] 播放  index={self._play_index}")

    def _pause(self):
        self._playing = False
        self._btn_play.setText("▶ 播放")

    def _stop(self):
        self._playing = False
        self._play_index = 0
        self._btn_play.setText("▶ 播放")
        self._btn_stop.setEnabled(False)
        self._progress_slider.setValue(0)
        self._frame_label.setText(f"帧: 0 / {len(self._samples)}")
        self._canvas.set_orientation(0, 0)
        self._rate_label.setText("-- FPS")

    def _cycle_speed(self):
        speeds = [0.25, 0.5, 1.0, 2.0, 4.0]
        idx = speeds.index(self._speed) if self._speed in speeds else 2
        self._speed = speeds[(idx + 1) % len(speeds)]
        self._speed_combo.setText(f"{self._speed:.1f}×")

    def _on_seek(self, value):
        if not self._samples:
            return
        ratio = value / 1000.0
        idx = int(ratio * len(self._samples))
        idx = max(0, min(idx, len(self._samples) - 1))
        self._play_index = idx
        self._frame_label.setText(f"帧: {idx} / {len(self._samples)}")

        # 立即显示当前帧
        ts, ax, ay, az = self._samples[idx]
        self._display_sample(ax, ay, az)

    # ============== 定时器驱动 ==============

    def _tick(self):
        """定时器 tick — 按速度播放下一帧"""
        if not self._playing or not self._samples:
            return

        if self._play_index >= len(self._samples):
            self._pause()
            self._info_label.setText("✅ 回放完成")
            self._rate_label.setText("-- FPS")
            return

        ts, ax, ay, az = self._samples[self._play_index]
        self._display_sample(ax, ay, az)

        # 更新进度
        total = len(self._samples)
        self._play_index += 1
        self._frame_label.setText(f"帧: {self._play_index} / {total}")
        self._progress_slider.setValue(int(self._play_index / total * 1000))

        # FPS 标记 (每帧都在更新, 固定显示 25Hz × speed)
        fps = 25 * self._speed
        self._rate_label.setText(f"{fps:.0f} FPS")

    def _display_sample(self, ax, ay, az):
        """解算姿态并更新 3D 画布"""
        denom = math.sqrt(ay * ay + az * az)
        pitch = math.degrees(math.atan2(-ax, denom)) if denom > 1e-6 else 0.0
        roll = math.degrees(math.atan2(ay, az)) if abs(az) > 1e-6 else 0.0
        self._canvas.set_orientation(pitch, roll)


# ======================================================================
# 主窗口
# ======================================================================
class ImuTestWindow(QMainWindow):
    """IMU 离线回放测试窗口"""

    def __init__(self, file_path=""):
        super().__init__()
        self.setWindowTitle("IMU 离线回放测试")
        self.setMinimumSize(700, 650)

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # 3D 姿态画布 (上半部分)
        self._canvas = Cube3DCanvas()
        self._canvas.setMinimumHeight(350)
        layout.addWidget(self._canvas, 1)

        # 回放控制 (下半部分)
        self._controller = PlaybackController(self._canvas)
        layout.addWidget(self._controller)

        # 样式
        self.setStyleSheet("""
            QMainWindow { background-color: #11111b; }
            QLabel { color: #cdd6f4; }
        """)

        # 自动加载文件
        if file_path:
            self._controller.load_file(file_path)


def main():
    """启动离线回放测试工具"""
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # 命令行参数: 可直接传 CSV 文件路径
    file_path = sys.argv[1] if len(sys.argv) > 1 else ""

    window = ImuTestWindow(file_path)
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
