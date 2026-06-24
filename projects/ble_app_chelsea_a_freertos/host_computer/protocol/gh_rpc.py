"""
GH RPC 协议解析器 - Goodix Health RPC Protocol Parser

实现了 GH (Goodix Health) RPC 数据帧的编码和解码。
"""

import struct
from enum import IntEnum
from typing import Optional


# ----- 帧格式常量 -----
FRAME_HEADER = bytes([0xAA, 0x11])

# ----- GH RPC 类型编码 -----
# GHRPC_publish("G", "<u8*>", rpcpoint) 的格式:
#   在 payload 最外层额外包裹了 <u8*> 格式头
#   TypeHeader: pack_type=1(UNSIGNED) | is_array=1 | width=3(8bit) | end=1 | split=0
#   = 0b_0_1_011_1_01 = 0x5D
#   然后 [length_byte] [raw_bytes...]
# ----- 功能 ID -----
class FuncId(IntEnum):
    ADT    = 0  # 佩戴检测
    HR     = 1  # 心率
    SPO2   = 2  # 血氧
    HRV    = 3  # 心率变异性
    GNADT  = 4  # 绿色光非接触佩戴检测
    IRNADT = 5  # 红外光非接触佩戴检测

FUNCTION_NAMES = {
    FuncId.ADT: "ADT",
    FuncId.HR: "HR",
    FuncId.SPO2: "SpO2",
    FuncId.HRV: "HRV",
    FuncId.GNADT: "G-NADT",
    FuncId.IRNADT: "IR-NADT",
}

# ----- 功能模式位掩码 -----
FUNC_MASK = {
    "ADT":   0x0001,
    "HR":    0x0002,
    "SPO2":  0x0004,
    "HRV":   0x0008,
    "GNADT": 0x0010,
    "IRNADT":0x0020,
    "ALL":   0xFFFFFFFF,
}


# ======================================================================
# 数据帧解析 (来自 HEALTH TX 通道的 GH RPC "G" 键数据)
#
# 注意: 固件端 gh_protocol_rawdata_to_bytes 使用差分编码:
#   - 首帧: 绝对值
#   - 后续帧: 与上一帧的差值 (zigzag 编码后写入)
#   且多个数据帧会累积到缓冲区 (BUFFER_BYTE_THRD=200B) 后一起发送。
#   因此一个 "G" 键载荷可能包含 N 个连续帧，需要逐个解析并累加差值。
# ======================================================================

def zigzag_decode(val: int) -> int:
    """ZigZag 解码: uint32 -> int32"""
    return (val >> 1) ^ -(val & 1)


# 单帧中单个字段的最大合理元素数 (gh_data_package.c 中各数组上限 MAX=32)
_MAX_FIELD_ELEMENTS = 64

def _decode_varint(data: bytes, pos: int) -> tuple:
    """解码单条变长整数 (LEB128 风格)

    返回: (new_pos, value)
    若 pos 未前进 → 数据耗尽 → 抛出 ValueError 防止死循环
    """
    if pos >= len(data):
        raise ValueError("varint: no data at pos=%d" % pos)
    start_pos = pos
    result = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        result |= (byte & 0x7F) << shift
        shift += 7
        pos += 1
        if not (byte & 0x80):
            break
    if pos == start_pos:
        raise ValueError("varint: pos did not advance")
    return pos, result


def _decode_int32_array(data: bytes, pos: int, count: int) -> tuple:
    """解码一组 zigzag 编码的 int32 值"""
    if count > _MAX_FIELD_ELEMENTS:
        raise ValueError("int32_array: count=%d exceeds max=%d" % (count, _MAX_FIELD_ELEMENTS))
    if count <= 0:
        return pos, []
    values = []
    for _ in range(count):
        new_pos, raw = _decode_varint(data, pos)
        if new_pos == pos:
            raise ValueError("int32_array: varint stuck at pos=%d" % pos)
        pos = new_pos
        values.append(zigzag_decode(raw))
    return pos, values


# ----- 包头部位域 -----
def parse_pack_header(val: int) -> dict:
    """解析 pack_header_t (32-bit 位域)"""
    return {
        "rawdata_en":   (val >> 0) & 1,
        "phy_value_en": (val >> 1) & 1,
        "gs_data_en":   (val >> 2) & 1,
        "flags_en":     (val >> 3) & 1,
        "alg_data_en":  (val >> 4) & 1,
        "agc_info_en":  (val >> 5) & 1,
        "timestamp_en": (val >> 6) & 1,
        "frameid_en":   (val >> 7) & 1,
        "func_id_en":   (val >> 8) & 1,
        "slot_cfg_en":  (val >> 9) & 1,
    }


# ----- 算法结果索引 (与 gh_data_package.c 一致) -----
class ADTIdx:
    WEAR_EVENT = 0
    DET_STATE  = 1
    CTR        = 2

class HRIdx:
    HBA_OUT       = 0   # 心率值 (BPM)
    VALID_SCORE   = 1   # 置信分数 (0-100)
    SNR           = 2   # 信噪比
    BLANK         = 3
    ACC_INFO      = 4   # 运动状态: 0=静止 1=行走 2=跑步
    REG_SCENCE    = 5   # 运动场景
    INPUT_SCENCE  = 6   # 输入场景

class SPO2Idx:
    FINAL_SPO2    = 0   # 血氧值 (*10000 格式, GH_ALGO_SPO2_OUT_COEF=10000)
    R_VAL         = 1
    CONFI_COEFF   = 2
    VALID_LEVEL   = 3
    HB_MEAN       = 4
    INVALID_FLAG  = 5

class HRVIdx:
    RRI0       = 0
    RRI1       = 1
    RRI2       = 2
    RRI3       = 3
    CONFIDENCE = 4
    VALID_NUM  = 5

class NADTIdx:
    WEAR_OFF_RES   = 0
    LIVE_BODY_CONF = 1


def _decode_single_data_frame(payload: bytes, pos: int, result: dict):
    """从 payload 的 pos 位置解码一个数据帧, 返回新的 pos 和是否成功

    result 会被更新为该帧的原始整数值 (绝对值或差值)。
    调用方负责累加差值。
    """
    # 1. pack_header (always 1 int32)
    pos, header_raw = _decode_varint(payload, pos)
    header = parse_pack_header(zigzag_decode(header_raw))
    result["header"] = header

    # 2. rawdata
    if header["rawdata_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, rawdata = _decode_int32_array(payload, pos, size)
        result["rawdata"] = rawdata

    # 3. phy_value
    if header["phy_value_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, phy_values = _decode_int32_array(payload, pos, size)
        result["phy_values"] = phy_values

    # 4. gs_data (accelerometer / gyro)
    if header["gs_data_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, gs_data = _decode_int32_array(payload, pos, size)
        result["gs_data"] = gs_data

    # 5. flags
    if header["flags_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, flags = _decode_int32_array(payload, pos, size)
        result["flags"] = flags

    # 6. algo_data (algorithm results)
    if header["alg_data_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, algo_data = _decode_int32_array(payload, pos, size)
        result["algo_raw"] = algo_data

    # 7. agc_info (AGC gain + LED current)
    if header["agc_info_en"]:
        pos, size = _decode_varint(payload, pos)
        size = zigzag_decode(size)
        pos, agc_info = _decode_int32_array(payload, pos, size)
        pos, agc_info_high = _decode_int32_array(payload, pos, size)
        result["agc_low"] = agc_info
        result["agc_high"] = agc_info_high

    # 8. timestamp
    if header["timestamp_en"]:
        pos, ts_low = _decode_varint(payload, pos)
        pos, ts_high = _decode_varint(payload, pos)
        ts = (zigzag_decode(ts_high) << 32) | (zigzag_decode(ts_low) & 0xFFFFFFFF)
        result["timestamp"] = ts

    # 9. frame_id (always present)
    pos, frame_id = _decode_varint(payload, pos)
    result["frame_id"] = zigzag_decode(frame_id)

    # 10. function_id (optional)
    if header["func_id_en"]:
        pos, fid = _decode_varint(payload, pos)
        result["function_id"] = zigzag_decode(fid)

    # 11. slot_cfg (optional)
    if header["slot_cfg_en"]:
        pos, slot = _decode_varint(payload, pos)
        result["slot_cfg"] = zigzag_decode(slot)

    return pos


def _accumulate_frame(result: dict, accum: dict):
    """将 result 帧的值合并到 accum 中

    根据 gh_data_package.c 的编码规则:
    - rawdata, phy_value, gs_data, timestamp → 差分编码, 需累加
    - algo_raw, flags, agc_info, frame_id, function_id → 绝对值, 直接替换
    """
    # ---- 差分字段 (diff → 累加) ----
    for key in ("rawdata", "phy_values", "gs_data"):
        if key in result:
            if key not in accum:
                accum[key] = list(result[key])
            else:
                for i, v in enumerate(result[key]):
                    accum[key][i] += v

    if "timestamp" in result:
        if "timestamp" not in accum:
            accum["timestamp"] = result["timestamp"]
        else:
            accum["timestamp"] += result["timestamp"]

    # ---- 绝对值字段 (直接替换) ----
    for key in ("flags", "algo_raw", "agc_low", "agc_high", "slot_cfg"):
        if key in result:
            accum[key] = result[key] if isinstance(result[key], list) else result[key]

    for key in ("frame_id", "function_id"):
        if key in result:
            accum[key] = result[key]


def parse_data_frame(payload: bytes) -> Optional[dict]:
    """
    解析 GH RPC "G" 键数据载荷

    支持:
    - 多帧解析: payload 可能包含 N 个连续编码的数据帧
    - 差分累加: 首帧为绝对值, 后续帧为差值(需累加到上一帧)

    返回:
        {
            "function_id": int,
            "func_name": str,
            "frame_id": int,
            "timestamp": int,
            "algo_results": {...},
            "gs_data": [...],
            ...
        }
        或 None (解析失败)
    """
    if not payload:
        return None

    result_accum = {}
    pos = 0

    while pos < len(payload):
        frame_raw = {}
        try:
            new_pos = _decode_single_data_frame(payload, pos, frame_raw)
        except (IndexError, ValueError):
            break  # 单帧解析失败 → 停止解析后续帧, 但保留已累积数据
        if new_pos == pos:
            break  # 无法前进 → 退出
        pos = new_pos

        # 累加 (处理差分)
        _accumulate_frame(frame_raw, result_accum)

    if not result_accum:
        return None

    # 提取算法结果
    func_id = result_accum.get("function_id")
    algo_raw = result_accum.get("algo_raw", [])

    if func_id is not None:
        result_accum["func_name"] = FUNCTION_NAMES.get(
            func_id, f"UNKNOWN({func_id})")
        if algo_raw:
            result_accum["algo_results"] = _decode_algo_results(
                func_id, algo_raw)

    return result_accum


def parse_individual_frames(payload: bytes) -> list:
    """
    逐帧解析 GH RPC "G" 键载荷，返回每帧独立数据（不累积到同一个 dict）。

    处理差分编码: 首帧 gs_data + timestamp 为绝对值，
    后续帧为差值 — 逐帧重建绝对值。

    Returns:
        list of dict, 每帧包含:
            - "gs_data": [ax, ay, az] (绝对加速度, 已累积)
            - "timestamp": int (毫秒, 已累积)
            - "frame_id": int
            - "function_id": int (可能缺失)
    """
    if not payload:
        return []

    frames = []
    running_gs = None   # 累积中的绝对 gs_data
    running_ts = None   # 累积中的绝对 timestamp (也是差分!)
    pos = 0

    while pos < len(payload):
        frame_raw = {}
        try:
            new_pos = _decode_single_data_frame(payload, pos, frame_raw)
        except (IndexError, ValueError):
            break
        if new_pos == pos:
            break
        pos = new_pos

        # ---- 累积 gs_data (差分→绝对) ----
        if "gs_data" in frame_raw:
            gs = list(frame_raw["gs_data"])
            if running_gs is None:
                running_gs = gs
            else:
                for i, v in enumerate(gs):
                    if i < len(running_gs):
                        running_gs[i] += v
                    else:
                        running_gs.append(v)

        # ---- 累积 timestamp (也是差分编码!) ----
        if "timestamp" in frame_raw:
            ts = frame_raw["timestamp"]
            if running_ts is None:
                running_ts = ts
            else:
                running_ts += ts

        # ---- 构建这一帧的输出 ----
        entry = {}
        if running_gs is not None:
            entry["gs_data"] = list(running_gs)
        if running_ts is not None:
            entry["timestamp"] = running_ts
        for key in ("frame_id", "function_id"):
            if key in frame_raw:
                entry[key] = frame_raw[key]

        frames.append(entry)

    return frames


def _decode_algo_results(func_id: int, data: list) -> dict:
    """根据功能 ID 解析算法结果"""
    results = {}
    if func_id == FuncId.ADT:
        if len(data) >= 3:
            results["wear_event"] = data[ADTIdx.WEAR_EVENT]
            results["det_status"] = data[ADTIdx.DET_STATE]
            results["ctr"] = data[ADTIdx.CTR]
    elif func_id == FuncId.HR:
        if len(data) >= 7:
            results["hr"] = data[HRIdx.HBA_OUT]
            results["score"] = data[HRIdx.VALID_SCORE]
            results["snr"] = data[HRIdx.SNR]
            results["acc_info"] = data[HRIdx.ACC_INFO]
            results["scene"] = data[HRIdx.REG_SCENCE]
            results["input_scene"] = data[HRIdx.INPUT_SCENCE]
    elif func_id == FuncId.SPO2:
        if len(data) >= 6:
            spo2_val = data[SPO2Idx.FINAL_SPO2]
            if spo2_val > 100000:
                results["spo2"] = spo2_val / 10000.0
            elif spo2_val > 100:
                results["spo2"] = spo2_val / 100.0
            else:
                results["spo2"] = float(spo2_val)
            results["r_val"] = data[SPO2Idx.R_VAL]
            results["confi_coeff"] = data[SPO2Idx.CONFI_COEFF]
            results["valid_level"] = data[SPO2Idx.VALID_LEVEL]
            results["hb_mean"] = data[SPO2Idx.HB_MEAN]
            results["invalid_flag"] = data[SPO2Idx.INVALID_FLAG]
    elif func_id == FuncId.HRV:
        if len(data) >= 6:
            results["rri"] = data[HRVIdx.RRI0:HRVIdx.RRI0 + min(data[HRVIdx.VALID_NUM], 4)]
            results["confidence"] = data[HRVIdx.CONFIDENCE]
            results["valid_num"] = data[HRVIdx.VALID_NUM]
    elif func_id in (FuncId.GNADT, FuncId.IRNADT):
        if len(data) >= 2:
            nadt_out = data[NADTIdx.WEAR_OFF_RES]
            status_map = {0: "normal", 1: "wear_on", 2: "wear_off", 3: "non_living"}
            results["wear_status_code"] = nadt_out & 0x3
            results["wear_status"] = status_map.get(nadt_out & 0x3, "unknown")
            results["suspected_off"] = (nadt_out >> 2) & 0x1
            results["confidence"] = data[NADTIdx.LIVE_BODY_CONF]
    return results


def is_text_log(data: bytes) -> Optional[str]:
    """检测数据是否为可打印的 ASCII 文本日志"""
    if not data:
        return None
    try:
        text = data.decode('ascii')
        if all(32 <= c < 127 or c in (9, 10, 13) for c in data):
            return text
    except Exception:
        pass
    return None


# ======================================================================
# GH RPC 帧解析 (从设备接收的原始帧)
# ======================================================================

def parse_rpc_frame(data: bytes) -> Optional[dict]:
    """
    解析 GH RPC 帧

    Frame 格式:
        [AA, 11]  (2 bytes, 帧头)
        [length]   (1 byte, 帧长度)
        [key_hdr]  (1 byte, 键类型头)
        [key_data] (N bytes, 键数据)
        [com_id]   (1 byte, 可选-安全模式)
        [frame_id] (1 byte, 可选-多帧)
        [params]   (N bytes, 参数数据)
        [crc]      (1 byte, 校验和)
    """
    if len(data) < 5 or data[:2] != FRAME_HEADER:
        return None

    try:
        declared_len = data[2]
        # 验证声明长度: length 字段从 key_header 开始到 crc 结束
        # 总帧长度 = 2(AA11) + 1(len) + declared_len
        if len(data) < 3 + declared_len:
            return None

        key_hdr = data[3]

        # 解析 key_header
        pack_type = key_hdr & 0x03
        is_array = (key_hdr >> 2) & 0x1
        width = (key_hdr >> 3) & 0x7
        secure = (key_hdr >> 6) & 0x1
        fin = (key_hdr >> 7) & 0x1

        pos = 4

        # 读取键数据
        if is_array:
            key_size = data[pos]
            pos += 1
            key = data[pos:pos + key_size].decode('ascii', errors='replace')
            pos += key_size
        else:
            key = chr(data[pos])
            pos += 1

        # 安全通讯 com_id
        if secure:
            com_id = data[pos]
            pos += 1

        # 帧 ID (多帧)
        if not fin:
            frag_id = data[pos]
            pos += 1

        # CRC 校验 (与 C 端 calCrc 一致: 对 key_header 及后续所有 body 字节求和)
        crc_body = data[3:-1]  # 从 key_header 到参数结束 (不含 crc 自身)
        expected_crc = sum(crc_body) & 0xFF
        actual_crc = data[-1]
        if expected_crc != actual_crc:
            return None

        # 参数数据
        payload = data[pos:-1]

        return {
            "key": key,
            "frame_len": declared_len,
            "secure": bool(secure),
            "fin": bool(fin),
            "payload": payload,
            "crc": actual_crc,
        }
    except (IndexError, ValueError):
        return None


# ======================================================================
# GH RPC 命令构建 (发送到设备)
# ======================================================================

def _zigzag_encode(val: int) -> int:
    """ZigZag 编码: int32 -> uint32"""
    return (val << 1) ^ (val >> 31)


def _encode_varint(val: int) -> bytes:
    """编码单条变长整数 (LEB128 风格)

    传入值应为非负 uint32; 负数会被 mask 为无符号 32-bit 防止无限循环。
    """
    val = val & 0xFFFFFFFF  # 强制转为无符号 32-bit
    buf = bytearray()
    while val > 0x7F:
        buf.append((val & 0x7F) | 0x80)
        val >>= 7
    buf.append(val & 0x7F)
    return bytes(buf)


# GH RPC 参数类型头 (TypeHeader) — 与 gh_package.c 一致
#   bit 0-1: pack_type (1=UNSIGNED, 2=SIGNED)
#   bit 2:   is_array
#   bit 3-5: width (2^width bytes, u32=5→4B, u16=4→2B, u8=3→1B)
#   bit 6:   end (1=last param)
#   bit 7:   split
# width = log2(bits), 例如 u32=32位 → 2^5=32 → width=5
_TH_U32_FIRST = 0x29  # <u32> first:  pack_type=1,w=5(32bit),end=0 → 0_0_101_0_01
_TH_U32_LAST  = 0x69  # <u32> last:   pack_type=1,w=5(32bit),end=1 → 0_1_101_0_01
_TH_U8_FIRST  = 0x19  # <u8>  first:  pack_type=1,w=3(8bit),end=0  → 0_0_011_0_01
_TH_U8_LAST   = 0x59  # <u8>  last:   pack_type=1,w=3(8bit),end=1  → 0_1_011_0_01


def _encode_param_u32(val: int, last: bool = False) -> bytes:
    """编码 uint32 参数 (含 TypeHeader + 4字节 LE)"""
    th = _TH_U32_LAST if last else _TH_U32_FIRST
    return bytes([th]) + struct.pack('<I', val)


def _encode_param_u8(val: int, last: bool = True) -> bytes:
    """编码 uint8 参数 (含 TypeHeader + 1字节)"""
    th = _TH_U8_LAST if last else _TH_U8_FIRST
    return bytes([th, val & 0xFF])


def _encode_param_d32(val: int, last: bool = True) -> bytes:
    """编码 int32 参数 (含 TypeHeader + 4字节 LE)"""
    th = 0x52 if last else 0x12  # <d32>: pack_type=2(SIGNED),width=2(4B),end=last/not
    return bytes([th]) + struct.pack('<i', val)


def _build_rpc_frame(key: str, params: bytes) -> bytes:
    """
    构建 GH RPC 帧

    Args:
        key: 函数键名 (如 "GH3X_SwFunctionCmd")
        params: 编码后的参数字节

    Returns:
        完整的 GH RPC 帧字节
    """
    key_bytes = key.encode('ascii')

    # key_header: pack_type=2(SIGNED), is_array=1(if not 1 char), width=3, secure=0, fin=1
    # C 端: datas->key_header.pack_type=GH_PRO_TYPE_SIGNED(=2)
    is_array = 1 if len(key_bytes) > 1 else 0
    key_hdr = (2 << 0) | (is_array << 2) | (3 << 3) | (0 << 6) | (1 << 7)

    # 构建帧体
    body = bytearray()
    body.append(key_hdr)

    if is_array:
        body.append(len(key_bytes))
        body.extend(key_bytes)
    else:
        body.extend(key_bytes)

    body.extend(params)

    # 计算 CRC
    crc = sum(body) & 0xFF

    # 完整帧
    # data->length = body size (不含 CRC, C 端 toFrameData 如此计算)
    frame = bytearray(FRAME_HEADER)
    frame_len = len(body)  # 不含 CRC
    frame.append(frame_len)
    frame.extend(body)
    frame.append(crc)

    return bytes(frame)


def build_sw_function_cmd(mode: int, ctrl: int) -> bytes:
    """
    构建 GH3X_SwFunctionCmd 命令帧

    Args:
        mode: 功能模式掩码 (如 0x0002=HR)
        ctrl: 控制 (0=启动, 1=停止)

    Returns:
        完整的 GH RPC 帧字节

    命令格式: GH3X_SwFunctionCmd(mode: u32, ctrl: u8)
    """
    params = _encode_param_u32(mode, last=False) + _encode_param_u8(ctrl, last=True)
    return _build_rpc_frame("GH3X_SwFunctionCmd", params)


def build_get_version_cmd(ver_type: int = 0x01) -> bytes:
    """
    构建 GH3X_GetVersion 命令帧

    Args:
        ver_type: 版本类型 (0x01=固件, 0x08=芯片)

    命令格式: GH3X_GetVersion(type: u8)
    """
    params = _encode_param_u8(ver_type, last=True)
    return _build_rpc_frame("GH3X_GetVersion", params)


def build_set_work_mode_cmd(mode: int) -> bytes:
    """
    构建 GHSetWorkModeCmd 命令帧

    Args:
        mode: 工作模式 (0=在线, 1=离线, 2=量产测试)
    """
    params = _encode_param_u8(mode, last=True)
    return _build_rpc_frame("GHSetWorkModeCmd", params)


def build_chip_ctrl_cmd(ctrl_type: int) -> bytes:
    """
    构建 GH3X_ChipCtrl 命令帧

    Args:
        ctrl_type: 控制类型
    """
    params = _encode_param_u8(ctrl_type, last=True)
    return _build_rpc_frame("GH3X_ChipCtrl", params)


# ======================================================================
# G 键载荷信封剥离
# ======================================================================

def unwrap_g_key_payload(payload: bytes) -> Optional[bytes]:
    """剥离 GH RPC "G" 键的 <u8*> 格式信封

    GHRPC_publish("G", "<u8*>", rpcpoint) 生成的载荷格式:
        [0x5D] [N] [data_0 ... data_N-1]   (end=1)
        或 [0x1D] [N] [data_0 ... data_N-1]  (end=0 variant)

    Returns:
        剥离后的数据帧字节流, 或 None(格式不匹配)
    """
    if not payload or len(payload) < 2:
        return None

    # 支持的 TypeHeader: 0x5D = end=1 | pack_type=1 | is_array=1 | width=3
    #                  0x1D = end=0 | pack_type=1 | is_array=1 | width=3
    #                  0xDD = end=1 | pack_type=3 | is_array=1 | width=3 (少见)
    if payload[0] in (0x5D, 0x1D, 0xDD):
        declared_len = payload[1]
        available = len(payload) - 2
        if available <= 0:
            return None
        # 固件有时声明的长度比实际多 1B, 取最小值
        data_len = min(declared_len, available)
        return payload[2:2 + data_len]

    # 非标准 TypeHeader → 无法解析
    return None


# ======================================================================
# 工具函数
# ======================================================================

def hex_dump(data: bytes, label: str = "") -> str:
    """生成十六进制转储字符串"""
    s = f"{label} ({len(data)} bytes):\n"
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        s += f"  {i:04X}: {hex_part:<48} {ascii_part}\n"
    return s
