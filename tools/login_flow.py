from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone


MQTT_TOPIC_PREFIX = "MS"
LOGIN_SERVICE = "rept"
LOGIN_COMMAND = "L"
HEARTBEAT_COMMAND = "H"
LOGIN_REQUEST_ID = "000001"
JSON_ID_FIRST_REPORT = 2
JSON_ID_MAX = 999999


@dataclass(frozen=True)
class TopicPermission:
    publish_topic: str
    subscribe_topic: str
    valid: bool
    reason: str


def crc16_modbus(data: bytes) -> int:
    """CRC16-Modbus (poly=0xA001, init=0xFFFF), matches C code crc16_modbus_get()"""
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def generate_password(imei: str) -> str:
    validate_imei(imei)
    segments = [imei[0:5].encode("ascii"), imei[5:10].encode("ascii"), imei[10:15].encode("ascii")]
    values = [(~crc16_modbus(segment)) & 0xFFFF for segment in segments]
    return f"{values[2]:04X}{values[0]:04X}{values[1]:04X}"


def validate_imei(imei: str) -> None:
    if len(imei) != 15 or not imei.isdigit():
        raise ValueError("IMEI必须是15位数字")


def next_json_id(current: int) -> int:
    if current < JSON_ID_FIRST_REPORT or current >= JSON_ID_MAX:
        return JSON_ID_FIRST_REPORT
    return current + 1


def get_topics(imei: str) -> tuple[str, str]:
    validate_imei(imei)
    return (
        f"{MQTT_TOPIC_PREFIX}/{imei}/dev2plt",
        f"{MQTT_TOPIC_PREFIX}/{imei}/plt2dev",
    )


def get_upgrade_topics(imei: str) -> tuple[str, str]:
    validate_imei(imei)
    return (
        f"{MQTT_TOPIC_PREFIX}/{imei}/dev2pcp",
        f"{MQTT_TOPIC_PREFIX}/{imei}/pcp2dev",
    )


def get_will_topic(imei: str) -> str:
    validate_imei(imei)
    return f"{MQTT_TOPIC_PREFIX}/{imei}/offline"


def get_current_time_text() -> str:
    timezone_cn = timezone(timedelta(hours=8))
    return datetime.now(timezone_cn).strftime("%Y-%m-%d %H:%M:%S")


def make_login_packet(imei: str, firmware_version: int = 100, hardware_version: int = 100, tm: str | None = None) -> str:
    payload = {
        "SN": imei,
        "TM": tm or get_current_time_text(),
        "SV": LOGIN_SERVICE,
        "ID": LOGIN_REQUEST_ID,
        "CT": LOGIN_COMMAND,
        "DT": {
            "DevInfo": {
                "protId": 100,
                "clas": "MS-SLC-01",
                "prottp": "iX7-075SC028-4G",
                "hver": hardware_version,
                "sver": firmware_version,
            },
            "MdlInfo": {
                "mver": "BC28GJAR01A01",
                "iccid": "00000000000000000000",
            },
            "Gis": {
                "lng": 120000000,
                "lat": 30000000,
                "zone": 8,
            },
            "Dim": [
                {
                    "cns": 1,
                    "polar": 0,
                    "dlmt": 1000,
                    "ulmt": 9000,
                    "rti": 0,
                    "rtPwr": 200,
                }
            ],
            "Sense": {
                "di": 1,
                "sBri": 80,
                "sBriTm": 5,
            },
        },
    }
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


def make_will_packet(imei: str, tm: str | None = None) -> str:
    validate_imei(imei)
    payload = {
        "SN": imei,
        "TM": tm or get_current_time_text(),
        "SV": LOGIN_SERVICE,
        "ID": "000000",
        "CT": HEARTBEAT_COMMAND,
    }
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


def make_heartbeat_packet(imei: str, message_id: str = "000002", tm: str | None = None) -> str:
    validate_imei(imei)
    if not message_id.isdigit() or len(message_id) > 6:
        raise ValueError("消息ID必须是1到6位数字")
    if int(message_id) < JSON_ID_FIRST_REPORT or int(message_id) > JSON_ID_MAX:
        raise ValueError("心跳消息ID必须在000002到999999范围内")
    payload = {
        "SN": imei,
        "TM": tm or get_current_time_text(),
        "SV": LOGIN_SERVICE,
        "ID": message_id.zfill(6),
        "CT": HEARTBEAT_COMMAND,
    }
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


def parse_login_response(payload: str) -> dict:
    result = {
        "success": False,
        "reason": "",
        "data": {},
    }
    try:
        data = json.loads(payload)
    except json.JSONDecodeError as error:
        result["reason"] = f"JSON解析失败: {error}"
        return result
    for field in ("SN", "TM", "SV", "ID", "CT"):
        if field not in data:
            result["reason"] = f"缺少字段: {field}"
            return result
    if data["CT"] != LOGIN_COMMAND:
        result["reason"] = f"命令类型错误: {data['CT']}"
        return result
    # 代码中登录响应SV="rept"，兼容平台可能返回"prop"
    if data["SV"] not in (LOGIN_SERVICE, "prop"):
        result["reason"] = f"服务类型错误: {data['SV']}"
        return result
    result["success"] = True
    result["data"] = data
    return result


def validate_topic_permissions(imei: str, publish_topic: str, subscribe_topic: str) -> TopicPermission:
    expected_publish, expected_subscribe = get_topics(imei)
    if publish_topic != expected_publish:
        return TopicPermission(publish_topic, subscribe_topic, False, "发布主题不匹配")
    if subscribe_topic != expected_subscribe:
        return TopicPermission(publish_topic, subscribe_topic, False, "订阅主题不匹配")
    if not publish_topic.startswith(f"{MQTT_TOPIC_PREFIX}/{imei}/"):
        return TopicPermission(publish_topic, subscribe_topic, False, "主题越权")
    if not subscribe_topic.startswith(f"{MQTT_TOPIC_PREFIX}/{imei}/"):
        return TopicPermission(publish_topic, subscribe_topic, False, "主题越权")
    return TopicPermission(publish_topic, subscribe_topic, True, "权限合法")
