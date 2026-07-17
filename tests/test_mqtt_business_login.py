#!/usr/bin/env python3
"""
MQTT业务登录测试脚本
基于中科协议实现完整登录流程：
1. MQTT连接
2. 订阅主题
3. 发送JSON格式登录包
4. 等待平台响应
"""

import time
import json
import sys
from datetime import datetime, timezone, timedelta

try:
    import paho.mqtt.client as mqtt
except ImportError:
    import paho.mqtt.client as mqtt

# MQTT服务器配置
MQTT_SERVER_IP = "47.120.15.220"
MQTT_SERVER_PORT = 1883

# 真实设备IMEI
REAL_IMEI = "860608074646596"


def crc16_modbus(data: bytes) -> int:
    """CRC16-Modbus (poly=0xA001, init=0xFFFF), matches C code crc16_modbus_get()"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def generate_password(imei: str) -> str:
    """
    生成MQTT密码
    将IMEI分成3段，每段5字节，计算CRC16-Modbus后取反
    按第3段、第1段、第2段顺序拼接
    """
    seg0 = imei[0:5].encode("ascii")
    seg1 = imei[5:10].encode("ascii")
    seg2 = imei[10:15].encode("ascii")

    # 计算CRC16-Modbus并取反
    p0 = (~crc16_modbus(seg0)) & 0xFFFF
    p1 = (~crc16_modbus(seg1)) & 0xFFFF
    p2 = (~crc16_modbus(seg2)) & 0xFFFF

    # 按3-1-2顺序拼接
    return f"{p2:04X}{p0:04X}{p1:04X}"


def get_current_time() -> str:
    """获取当前UTC+8时间字符串"""
    tz = timezone(timedelta(hours=8))
    now = datetime.now(tz)
    return now.strftime("%Y-%m-%d %H:%M:%S")


def make_login_packet(imei: str) -> str:
    """
    构造JSON格式登录包
    参考：中科协议 第60-63页
    """
    login_data = {
        "SN": imei,
        "TM": get_current_time(),
        "SV": "rept",
        "ID": "000001",
        "CT": "L",
        "DT": {
            "DevInfo": {
                "protId": 100,
                "clas": "MS-SLC-01",
                "prottp": "iX7-075SC028-4G",
                "hver": 100,
                "sver": 100
            },
            "MdlInfo": {
                "mver": "BC28GJAR01A01",
                "iccid": "00000000000000000000"
            },
            "Gis": {
                "lng": 120000000,
                "lat": 30000000,
                "zone": 8
            },
            "Dim": [{
                "cns": 1,
                "polar": 0,
                "dlmt": 1000,
                "ulmt": 9000,
                "rti": 0,
                "rtPwr": 200
            }],
            "Sense": {
                "di": 1,
                "sBri": 80,
                "sBriTm": 5
            }
        }
    }

    # 压缩JSON格式（无空格）
    return json.dumps(login_data, separators=(',', ':'))


def parse_login_response(payload: bytes) -> dict:
    """
    解析登录响应
    预期格式：
    {
        "SN": "设备IMEI",
        "TM": "时间",
        "SV": "rept",
        "ID": "000001",
        "CT": "L"
    }
    """
    result = {"success": False, "raw": None, "error": None}

    try:
        text = payload.decode('utf-8')
        result["raw"] = text
        data = json.loads(text)

        # 检查必要字段
        # 代码中登录响应SV="rept", CT="L"；兼容平台可能返回SV="prop"
        if data.get("CT") == "L" and data.get("SV") in ("rept", "prop"):
            result["success"] = True
            result["sn"] = data.get("SN")
            result["tm"] = data.get("TM")
            result["id"] = data.get("ID")
        else:
            result["error"] = f"非登录响应: SV={data.get('SV')}, CT={data.get('CT')}"

    except json.JSONDecodeError as e:
        result["error"] = f"JSON解析失败: {e}"
    except Exception as e:
        result["error"] = f"解析错误: {e}"

    return result


# 测试状态
test_state = {
    "mqtt_connected": False,
    "subscribed": False,
    "login_response": None,
    "all_messages": [],
}


def on_connect(client, userdata, flags, reason_code, properties):
    """MQTT连接回调"""
    if reason_code == 0:
        test_state["mqtt_connected"] = True
        print(f"[SUCCESS] MQTT连接成功")
        # 订阅主题
        sub_topic = f"MS/{REAL_IMEI}/plt2dev"
        client.subscribe(sub_topic, qos=1)
        print(f"[INFO] 已订阅主题: {sub_topic}")
    else:
        print(f"[FAILED] MQTT连接失败, reason_code={reason_code}")


def on_subscribe(client, userdata, mid, reason_codes, properties):
    """订阅成功回调"""
    test_state["subscribed"] = True
    print(f"[SUCCESS] 订阅成功 (mid={mid})")

    # 发送JSON登录包
    login_json = make_login_packet(REAL_IMEI)
    pub_topic = f"MS/{REAL_IMEI}/dev2plt"

    print(f"[INFO] 发送登录包到: {pub_topic}")
    print(f"[INFO] 登录包内容:")
    print(json.dumps(json.loads(login_json), indent=2, ensure_ascii=False))

    client.publish(pub_topic, login_json, qos=0)
    print(f"[INFO] 登录包已发送，等待平台响应...")


def on_message(client, userdata, msg):
    """消息接收回调"""
    print(f"\n[INFO] 收到消息")
    print(f"  主题: {msg.topic}")
    print(f"  QoS: {msg.qos}")

    try:
        text = msg.payload.decode('utf-8')
        print(f"  内容: {text}")

        # 尝试解析JSON
        try:
            data = json.loads(text)
            print(f"  JSON: {json.dumps(data, indent=4, ensure_ascii=False)}")

            # 检查是否是登录响应 (CT=L 表示登录响应)
            if data.get("CT") == "L":
                test_state["login_response"] = {
                    "success": True,
                    "sn": data.get("SN"),
                    "tm": data.get("TM"),
                    "id": data.get("ID"),
                    "sv": data.get("SV")
                }
                print(f"\n[SUCCESS] 业务登录成功! 平台已确认登录")
            else:
                test_state["all_messages"].append(data)
                print(f"  消息类型: SV={data.get('SV')}, CT={data.get('CT')}")

        except json.JSONDecodeError:
            print(f"  非JSON格式")
            test_state["all_messages"].append({"raw": text})

    except Exception as e:
        print(f"  解析失败: {e}")
        test_state["all_messages"].append({"error": str(e)})


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """断开连接回调"""
    print(f"[INFO] 断开连接, reason_code={reason_code}")


def test_mqtt_business_login():
    """测试MQTT业务登录"""
    password = generate_password(REAL_IMEI)

    print("=" * 60)
    print("MQTT业务登录测试 (中科协议)")
    print("=" * 60)
    print(f"服务器: {MQTT_SERVER_IP}:{MQTT_SERVER_PORT}")
    print(f"IMEI: {REAL_IMEI}")
    print(f"密码: {password}")
    print(f"Client ID: {REAL_IMEI}")
    print(f"订阅主题: MS/{REAL_IMEI}/plt2dev (QoS=1)")
    print(f"发布主题: MS/{REAL_IMEI}/dev2plt")
    print("=" * 60)

    # 创建MQTT客户端
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=REAL_IMEI)
    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    # 设置登录凭证
    client.username_pw_set(REAL_IMEI, password)

    print("[INFO] 正在连接服务器...")

    try:
        client.connect(MQTT_SERVER_IP, MQTT_SERVER_PORT, keepalive=60)
    except Exception as e:
        print(f"[FAILED] 连接失败: {e}")
        return False

    # 启动网络循环
    client.loop_start()

    # 等待登录响应
    timeout = 30  # 30秒超时
    start_time = time.time()

    while time.time() - start_time < timeout:
        if test_state["login_response"] is not None:
            break
        time.sleep(0.1)

    client.loop_stop()
    client.disconnect()

    return test_state["login_response"] is not None and test_state["login_response"].get("success", False)


if __name__ == "__main__":
    success = test_mqtt_business_login()
    print("\n" + "=" * 60)
    if success:
        print("测试结果: MQTT业务登录成功")
        print(f"设备SN: {test_state['login_response']['sn']}")
        print(f"平台时间: {test_state['login_response']['tm']}")
        print(f"请求ID: {test_state['login_response']['id']}")
        sys.exit(0)
    else:
        print("测试结果: MQTT业务登录失败")
        if test_state["all_messages"]:
            print(f"收到的消息数: {len(test_state['all_messages'])}")
        sys.exit(1)
