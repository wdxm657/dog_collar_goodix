#!/usr/bin/env python3
"""
ChelseaA_OS BLE 健康设备数据解析上位机

基于 PyQt5 + bleak 的跨平台蓝牙低功耗健康数据解析工具。
支持 ChelseaA_OS (GR5526 + GH3038) 设备的 HR/HRV/SpO2/ADT/NADT 数据实时显示和命令控制。

使用方法:
    pip install -r requirements.txt
    python main.py

依赖:
    - PyQt5 >= 5.15
    - bleak >= 0.21
"""

import sys
import os

# 将项目根目录加入 Python 路径
root_dir = os.path.dirname(os.path.abspath(__file__))
if root_dir not in sys.path:
    sys.path.insert(0, root_dir)

from main_window import main

if __name__ == "__main__":
    main()
