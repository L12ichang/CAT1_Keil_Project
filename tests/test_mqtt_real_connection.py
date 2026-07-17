#!/usr/bin/env python3
"""
MQTT实际连接测试脚本
测试与ZK MQTT服务器的登录连接是否成功
"""

import time
import sys

try:
    import paho.mqtt.client as mqtt
except ImportError:
    import paho.mqtt.client as mqtt

# MQTT服务器配置
MQTT_SERVER_IP = "47.120.15.220"
MQTT_SERVER_PORT = 1883

# 测试IMEI
TEST_IMEI = "868120257654321"


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
    """生成MQTT密码"""
    seg0 = imei[0:5].encode("ascii")
    seg1 = imei[5:10].encode("ascii")
    seg2 = imei[10:15].encode("ascii")
    p0 = (~crc16_modbus(seg0)) & 0xFFFF
    p1 = (~crc16_modbus(seg1)) & 0xFFFF
    p2 = (~crc16_modbus(seg2)) & 0xFFFF
    return f"{p2:04X}{p0:04X}{p1:04X}"


# 连接结果
connect_result = {"success": False, "error": None, "code": None}


def on_connect(client, userdata, flags, reason_code, properties):
    """连接回调"""
    connect_result["code"] = reason_code
    if reason_code == 0:
        connect_result["success"] = True
        print(f"[SUCCESS] MQTT登录成功!")
        print(f"  IMEI: {TEST_IMEI}")
        print(f"  密码: {generate_password(TEST_IMEI)}")
    else:
        connect_result["success"] = False
        error_messages = {
            1: "协议版本不支持",
            2: "客户端标识符无效",
            3: "服务器不可用",
            4: "用户名或密码错误",
            5: "未授权",
        }
        msg = error_messages.get(reason_code, f"未知错误码: {reason_code}")
        connect_result["error"] = msg
        print(f"[FAILED] MQTT登录失败: {msg}")


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """断开连接回调"""
    print(f"[INFO] 断开连接, reason_code={reason_code}")


def test_mqtt_login():
    """测试MQTT登录"""
    password = generate_password(TEST_IMEI)
    sub_topic = f"MS/{TEST_IMEI}/plt2dev"

    print("=" * 50)
    print("MQTT登录测试")
    print("=" * 50)
    print(f"服务器: {MQTT_SERVER_IP}:{MQTT_SERVER_PORT}")
    print(f"IMEI: {TEST_IMEI}")
    print(f"密码: {password}")
    print(f"订阅主题: {sub_topic}")
    print("=" * 50)

    # 创建MQTT客户端
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=TEST_IMEI)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    # 设置登录凭证
    client.username_pw_set(TEST_IMEI, password)

    print("[INFO] 正在连接服务器...")

    try:
        client.connect(MQTT_SERVER_IP, MQTT_SERVER_PORT, keepalive=60)
    except Exception as e:
        print(f"[FAILED] 连接失败: {e}")
        return False

    # 启动网络循环
    client.loop_start()

    # 等待连接结果
    timeout = 10
    start_time = time.time()
    while time.time() - start_time < timeout:
        if connect_result["code"] is not None:
            break
        time.sleep(0.1)

    client.loop_stop()
    client.disconnect()

    return connect_result["success"]


if __name__ == "__main__":
    success = test_mqtt_login()
    print("=" * 50)
    if success:
        print("测试结果: MQTT登录测试通过")
        sys.exit(0)
    else:
        print("测试结果: MQTT登录测试失败")
        sys.exit(1)
