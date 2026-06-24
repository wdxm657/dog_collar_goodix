"""
IMU 三维姿态可视化模块 (3D 透视立方体)

使用 QPainter + 手动 3D 透视投影绘制一个彩色立方体，
通过 pitch/roll 旋转实时反映设备姿态。
零额外依赖，纯数学投影。
"""

import math
from datetime import datetime
from PyQt5.QtCore import Qt, QTimer, QRectF, QPointF
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QFrame,
    QPlainTextEdit,
)
from PyQt5.QtGui import (
    QPainter, QPainterPath, QColor, QPen, QBrush, QFont,
)


# ======================================================================
# 3D 姿态立方体画布
# ======================================================================
class Cube3DCanvas(QWidget):
    """用 3D 彩色立方体显示设备姿态

    8 顶点 / 6 面长方体，施加 roll(绕 X) + pitch(绕 Y) 旋转后
    用透视投影绘制，面按 Z 排序保证遮挡正确。
    """

    # 立方体半边长 (归一化坐标)
    HALF_W = 0.6   # X
    HALF_H = 0.3   # Y
    HALF_D = 0.15  # Z

    # 6 个面的颜色 (RGBA)
    FACE_COLORS = [
        QColor(0x1E, 0x66, 0xF5, 180),   # 前面  (z=+D)  蓝
        QColor(0x04, 0x3C, 0xB5, 160),   # 后面  (z=-D)  深蓝
        QColor(0xE6, 0x45, 0x45, 170),   # 右面  (x=+W)  红
        QColor(0xC0, 0x30, 0x30, 150),   # 左面  (x=-W)  暗红
        QColor(0x40, 0xA0, 0x2B, 170),   # 上面  (y=+H)  绿
        QColor(0x2B, 0x80, 0x1B, 150),   # 下面  (y=-H)  暗绿
    ]

    # 边线色
    EDGE_COLOR = QColor(0xCD, 0xD6, 0xF4, 200)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(200, 200)

        # 姿态角 (度)
        self._pitch = 0.0
        self._roll = 0.0
        self._target_pitch = 0.0
        self._target_roll = 0.0

        # 预计算 3D 顶点 [-1..1] 坐标
        self._verts = self._make_verts()

        # 预计算 6 个面 (顶点索引列表)
        W, H, D = self.HALF_W, self.HALF_H, self.HALF_D
        self._faces = [
            (0, 1, 3, 2),   # 前面 z=+D
            (4, 5, 7, 6),   # 后面 z=-D
            (1, 5, 7, 3),   # 右面 x=+W
            (0, 4, 6, 2),   # 左面 x=-W
            (2, 3, 7, 6),   # 上面 y=+H
            (0, 1, 5, 4),   # 下面 y=-H
        ]

        # 每条边的两个顶点索引
        self._edges = [
            (0, 1), (1, 3), (3, 2), (2, 0),   # 前
            (4, 5), (5, 7), (7, 6), (6, 4),   # 后
            (0, 4), (1, 5), (3, 7), (2, 6),   # 连接
        ]

        # 平滑定时器
        self._smooth_timer = QTimer()
        self._smooth_timer.timeout.connect(self._smooth_tick)
        self._smooth_timer.start(16)  # ~60 FPS

    @staticmethod
    def _make_verts():
        """返回 8 个顶点，坐标使用归一化值 (后续应用 HALF_*)"""
        # 顶点按 (x, y, z) ∈ {-1, +1}³
        return [
            (-1, -1,  1),   # 0  前下左
            ( 1, -1,  1),   # 1  前下右
            (-1,  1,  1),   # 2  前上左
            ( 1,  1,  1),   # 3  前上右
            (-1, -1, -1),   # 4  后下左
            ( 1, -1, -1),   # 5  后下右
            (-1,  1, -1),   # 6  后上左
            ( 1,  1, -1),   # 7  后上右
        ]

    def set_orientation(self, pitch_deg: float, roll_deg: float):
        """设置目标姿态角"""
        self._target_pitch = max(-90.0, min(90.0, pitch_deg))
        self._target_roll = roll_deg

    def _smooth_tick(self):
        """逐帧插值平滑"""
        k = 0.35
        dp = self._target_pitch - self._pitch
        dr = self._target_roll - self._roll

        if dr > 180:
            dr -= 360
        elif dr < -180:
            dr += 360

        if abs(dp) < 0.01 and abs(dr) < 0.01:
            self._pitch = self._target_pitch
            self._roll = self._target_roll
        else:
            self._pitch += dp * k
            self._roll += dr * k

        self.update()

    def _rotate_point(self, x, y, z):
        """绕 X 轴 (roll) → 绕 Y 轴 (pitch) 旋转"""
        pitch_r = math.radians(self._pitch)
        roll_r = math.radians(self._roll)

        cp, sp = math.cos(pitch_r), math.sin(pitch_r)
        cr, sr = math.cos(roll_r), math.sin(roll_r)

        # Roll: 绕 X
        y1 = y * cr - z * sr
        z1 = y * sr + z * cr

        # Pitch: 绕 Y
        x2 = x * cp + z1 * sp
        z2 = -x * sp + z1 * cp

        return x2, y1, z2

    @staticmethod
    def _project(x, y, z, scale, cx, cy):
        """简单透视投影: 摄像机在 z=+4"""
        cam_z = 4.0
        factor = cam_z / (cam_z + z * 0.8)
        return cx + x * scale * factor, cy - y * scale * factor

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)

        w, h = self.width(), self.height()
        cx, cy = w / 2.0, h / 2.0
        scale = min(w, h) * 0.32

        # 1. 旋转所有顶点
        rotated = []
        for vx, vy, vz in self._verts:
            rx, ry, rz = self._rotate_point(
                vx * self.HALF_W,
                vy * self.HALF_H,
                vz * self.HALF_D,
            )
            px, py = self._project(rx, ry, rz, scale, cx, cy)
            rotated.append((px, py, rz))

        # 2. 计算每个面的平均 Z (用于排序)
        face_list = []
        for i, idxs in enumerate(self._faces):
            avg_z = sum(rotated[idx][2] for idx in idxs) / 4.0
            face_list.append((avg_z, i, idxs))

        # 3. 按 Z 从远到近排序 (Painter's Algorithm)
        face_list.sort(key=lambda x: x[0], reverse=True)

        # 4. 绘制面
        for avg_z, i, idxs in face_list:
            pts = [QPointF(rotated[idx][0], rotated[idx][1]) for idx in idxs]
            path = QPainterPath()
            path.moveTo(pts[0])
            for pt in pts[1:]:
                path.lineTo(pt)
            path.closeSubpath()

            color = self.FACE_COLORS[i]
            if avg_z > 0:
                # 靠近摄像机 → 更亮
                color = QColor(
                    min(255, color.red() + 20),
                    min(255, color.green() + 20),
                    min(255, color.blue() + 20),
                    color.alpha(),
                )
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(color))
            p.drawPath(path)

        # 5. 绘制边线
        p.setPen(QPen(self.EDGE_COLOR, 1.5))
        p.setBrush(Qt.NoBrush)
        for i, j in self._edges:
            p.drawLine(
                QPointF(rotated[i][0], rotated[i][1]),
                QPointF(rotated[j][0], rotated[j][1]),
            )

        # 6. 坐标轴指示 (右下角小标记)
        self._draw_axes(p, cx, cy, scale)

        # 7. 设备外框 (半透明圆角矩形背景)
        self._draw_status_bg(p, cx, cy, scale)

    def _draw_axes(self, p: QPainter, cx, cy, scale):
        """绘制世界坐标轴指示 (X=红, Y=绿, Z=蓝)"""
        axis_len = scale * 0.18
        ox, oy = cx + scale * 0.65, cy + scale * 0.65
        origin = QPointF(ox, oy)

        # 世界坐标轴向量 (在设备坐标系中观察)
        axes = [
            (1, 0, 0, QColor(0xF3, 0x8B, 0xA8)),  # X → 红
            (0, 1, 0, QColor(0xA6, 0xE3, 0xA1)),  # Y → 绿
            (0, 0, 1, QColor(0x89, 0xB4, 0xFA)),  # Z → 蓝
        ]
        p.setFont(QFont("Consolas", 7))

        for vx, vy, vz, color in axes:
            # 旋转轴向量
            rx, ry, _ = self._rotate_point(vx * axis_len, vy * axis_len, vz * axis_len)
            ex = ox + rx
            ey = oy - ry

            p.setPen(QPen(color, 2))
            p.drawLine(origin, QPointF(ex, ey))

            # 标签
            label = {1: "X", 2: "Y", 3: "Z"}[
                (1 if vx else (2 if vy else 3))
            ]
            p.setPen(QPen(color))
            p.drawText(QRectF(ex - 6, ey - 8, 12, 14), Qt.AlignCenter, label)

    def _draw_status_bg(self, p, cx, cy, scale):
        """绘制半透明背景圆角矩形"""
        r = scale * 1.08
        rect = QRectF(cx - r, cy - r, r * 2, r * 2)
        p.setPen(QPen(QColor(0x31, 0x32, 0x44, 0x80), 1))
        p.setBrush(QBrush(QColor(0x11, 0x11, 0x1B, 0x30)))
        p.drawRoundedRect(rect, 12, 12)


# ======================================================================
# IMU 综合显示组件
# ======================================================================
class ImuWidget(QFrame):
    """IMU 实时数据显示与可视化组件

    - 3D 姿态立方体 (Cube3DCanvas)
    - 采样队列 + 10Hz 逐帧回放
    - 实时加速度数值 (ax, ay, az)
    - 可复制的 IMU 日志
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setStyleSheet("""
            ImuWidget {
                background-color: #1e1e2e;
                border: 1px solid #313244;
                border-radius: 8px;
            }
        """)

        self._sample_queue = []
        self._max_queue = 100
        self._freshness_count = 0
        self._buffering = True          # 首次缓存满一包后再开始回放
        self._buffer_target = 5        # 缓存 开始播放

        self._build_ui()

        # 25Hz 逐帧回放 (匹配传感器采样率 ~40ms/帧)
        self._playback_timer = QTimer()
        self._playback_timer.timeout.connect(self._play_next_sample)
        self._playback_timer.start(40)

        # 新鲜度检查
        self._freshness_timer = QTimer()
        self._freshness_timer.timeout.connect(self._check_freshness)
        self._freshness_timer.start(1000)

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)

        # ---- 标题行 ----
        header = QHBoxLayout()
        title = QLabel("◈  IMU 3D 姿态")
        title.setStyleSheet("color: #89b4fa; font-size: 14px; font-weight: bold;")
        header.addWidget(title)
        header.addStretch()

        self._queue_label = QLabel("0")
        self._queue_label.setStyleSheet("color: #585b70; font-size: 11px;")
        self._queue_label.setToolTip("采样队列深度")
        header.addWidget(self._queue_label)

        self._status_led = QLabel("●")
        self._status_led.setStyleSheet("color: #f9e2af; font-size: 16px;")  # 黄=缓存中
        self._status_led.setToolTip("IMU 数据状态：黄色=缓存中, 绿色=回放中, 灰色=无数据")
        header.addWidget(self._status_led)

        layout.addLayout(header)

        # ---- 3D 姿态立方体 ----
        self._canvas = Cube3DCanvas()
        self._canvas.setMinimumHeight(220)
        layout.addWidget(self._canvas, 1)

        # ---- 数值面板 ----
        values_frame = QFrame()
        values_frame.setStyleSheet("""
            QFrame {
                background-color: #181825;
                border: 1px solid #313244;
                border-radius: 6px;
            }
        """)
        values_layout = QVBoxLayout(values_frame)
        values_layout.setContentsMargins(8, 6, 8, 6)
        values_layout.setSpacing(4)

        # 加速度
        acc_row = QHBoxLayout()
        self._ax_label = QLabel("AX: 0")
        self._ax_label.setStyleSheet(
            "color: #f38ba8; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        acc_row.addWidget(self._ax_label)

        self._ay_label = QLabel("AY: 0")
        self._ay_label.setStyleSheet(
            "color: #a6e3a1; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        acc_row.addWidget(self._ay_label)

        self._az_label = QLabel("AZ: 0")
        self._az_label.setStyleSheet(
            "color: #89b4fa; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        acc_row.addWidget(self._az_label)

        values_layout.addLayout(acc_row)

        # 姿态角
        angle_row = QHBoxLayout()
        self._pitch_label = QLabel("Pitch: 0.0°")
        self._pitch_label.setStyleSheet(
            "color: #f9e2af; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        angle_row.addWidget(self._pitch_label)

        self._roll_label = QLabel("Roll: 0.0°")
        self._roll_label.setStyleSheet(
            "color: #cba6f7; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        angle_row.addWidget(self._roll_label)

        self._magnitude_label = QLabel("|G|: 1.00")
        self._magnitude_label.setStyleSheet(
            "color: #a6adc8; font-size: 13px; font-weight: bold;"
            "font-family: 'Consolas', monospace;")
        angle_row.addWidget(self._magnitude_label)

        values_layout.addLayout(angle_row)
        layout.addWidget(values_frame)

        # ---- IMU 日志 (可复制) ----
        log_header = QHBoxLayout()
        log_title = QLabel("📋 IMU 日志")
        log_title.setStyleSheet("color: #a6adc8; font-size: 11px;")
        log_header.addWidget(log_title)
        log_header.addStretch()

        self._log_clear_btn = QLabel("✕ 清除")
        self._log_clear_btn.setStyleSheet(
            "color: #585b70; font-size: 11px; padding: 2px 6px;")
        self._log_clear_btn.setToolTip("清除日志")
        log_header.addWidget(self._log_clear_btn)

        layout.addLayout(log_header)

        self._log_display = QPlainTextEdit()
        self._log_display.setReadOnly(True)
        self._log_display.setMaximumBlockCount(50)
        self._log_display.setStyleSheet("""
            QPlainTextEdit {
                color: #585b70; font-size: 10px;
                font-family: 'Consolas', monospace;
                background-color: #11111b;
                border: 1px solid #313244;
                border-radius: 4px;
                padding: 4px 6px;
            }
        """)
        self._log_display.setMaximumHeight(100)
        self._log_display.appendPlainText("等待 IMU 数据...")
        layout.addWidget(self._log_display)

    # ==================================================================
    # 公共 API
    # ==================================================================

    def update_imu_batch(self, samples: list):
        """批量添加 IMU 采样
        samples: [(ax, ay, az, timestamp), ...]
        """
        self._sample_queue.extend(samples)
        self._sample_queue.sort(key=lambda s: s[3])
        if len(self._sample_queue) > self._max_queue:
            self._sample_queue = self._sample_queue[-self._max_queue:]
        self._queue_label.setText(str(len(self._sample_queue)))

        # 首次缓存满目标帧数后开始回放
        if self._buffering and len(self._sample_queue) >= self._buffer_target:
            self._buffering = False
            self._status_led.setStyleSheet("color: #40a02b; font-size: 16px;")
            self.append_log(f"缓存就绪, 开始 25Hz 回放 ({len(self._sample_queue)} 帧)")

    def append_log(self, text: str):
        """追加 IMU 日志"""
        ts = datetime.now().strftime("%H:%M:%S")
        self._log_display.appendPlainText(f"[{ts}] {text}")

    def reset(self):
        """重置显示"""
        self._sample_queue.clear()
        self._buffering = True
        self._queue_label.setText("0")
        self._canvas.set_orientation(0, 0)
        self._ax_label.setText("AX: 0")
        self._ay_label.setText("AY: 0")
        self._az_label.setText("AZ: 0")
        self._pitch_label.setText("Pitch: 0.0°")
        self._roll_label.setText("Roll: 0.0°")
        self._magnitude_label.setText("|G|: 1.00")
        self._status_led.setStyleSheet("color: #585b70; font-size: 16px;")
        self._log_display.clear()
        self._log_display.appendPlainText("等待 IMU 数据...")

    # ==================================================================
    # 内部
    # ==================================================================

    def _play_next_sample(self):
        """25Hz 回放 (缓存满后才开始弹出)"""
        if self._buffering or not self._sample_queue:
            return

        # 队列元素: (ax, ay, az, timestamp)
        ax, ay, az, timestamp = self._sample_queue.pop(0)
        self._queue_label.setText(str(len(self._sample_queue)))

        # 姿态解算
        denom = math.sqrt(ay * ay + az * az)
        pitch = math.degrees(math.atan2(-ax, denom)) if denom > 1e-6 else 0.0
        roll = math.degrees(math.atan2(ay, az)) if abs(az) > 1e-6 else 0.0
        mag = math.sqrt(ax * ax + ay * ay + az * az)

        self._canvas.set_orientation(pitch, roll)

        self._ax_label.setText(f"AX: {ax:+.0f}")
        self._ay_label.setText(f"AY: {ay:+.0f}")
        self._az_label.setText(f"AZ: {az:+.0f}")
        self._pitch_label.setText(f"Pitch: {pitch:+.1f}°")
        self._roll_label.setText(f"Roll: {roll:+.1f}°")
        self._magnitude_label.setText(f"|G|: {mag:.2f}")

        self._status_led.setStyleSheet("color: #40a02b; font-size: 16px;")
        self._freshness_count = 0

    def _check_freshness(self):
        self._freshness_count += 1
        if self._freshness_count > 3:
            self._status_led.setStyleSheet("color: #585b70; font-size: 16px;")
