#!/usr/bin/env python3
"""
MQTT协议单元测试
测试中科协议JSON格式登录功能
"""

import unittest
from dataclasses import dataclass
import json


# ========== 常量定义 ==========
ZK_MQTT_SERVER_IP = "47.120.15.220"
ZK_MQTT_SERVER_PORT = "1883"
ZK_MQTT_TOPIC_PREFIX = "MS/"
ZK_MQTT_CLIENT_IDX = 0
ZK_MQTT_VERSION = 4
ZK_MQTT_SUB_QOS = 1
ZK_MQTT_PUB_QOS = 1
ZK_MQTT_RETAIN = 0
ZK_MQTT_WILL_QOS = 1
ZK_MQTT_WILL_RETAIN = 0
ZK_MQTT_ENABLE_WILL = 0
ZK_PROT_ID = 100
ZK_CLASS = "MS-SLC-01"
ZK_PRODUCT_TYPE = "iX7-075SC028-4G"
ZK_SV_REPT = "rept"
ZK_CT_LOGIN = "L"
ZK_CT_HEARTBEAT = "H"


# ========== CRC16-MODBUS计算 ==========
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


# ========== 密码生成 ==========
def generate_password(imei: str) -> str:
    """从IMEI生成密码，按3-1-2顺序拼接"""
    seg0 = imei[0:5].encode("ascii")
    seg1 = imei[5:10].encode("ascii")
    seg2 = imei[10:15].encode("ascii")
    p0 = (~crc16_modbus(seg0)) & 0xFFFF
    p1 = (~crc16_modbus(seg1)) & 0xFFFF
    p2 = (~crc16_modbus(seg2)) & 0xFFFF
    # 按3-1-2顺序拼接
    return f"{p2:04X}{p0:04X}{p1:04X}"


# ========== 数据结构 ==========
@dataclass
class ZkMqttConfig:
    imei: str
    client_id: str
    username: str
    password: str
    sub_topic: str
    pub_topic: str
    will_topic: str
    sub_upgrade_topic: str
    pub_upgrade_topic: str
    will_payload: str
    client_idx: int = ZK_MQTT_CLIENT_IDX
    mqtt_version: int = ZK_MQTT_VERSION
    sub_qos: int = ZK_MQTT_SUB_QOS
    pub_qos: int = ZK_MQTT_PUB_QOS
    retain: int = ZK_MQTT_RETAIN
    will_qos: int = ZK_MQTT_WILL_QOS
    will_retain: int = ZK_MQTT_WILL_RETAIN


@dataclass
class ZkDevInfo:
    protId: int = ZK_PROT_ID
    clas: str = ZK_CLASS
    prottp: str = ZK_PRODUCT_TYPE
    hver: int = 100
    sver: int = 100


@dataclass
class ZkMdlInfo:
    mver: str = "BC28GJAR01A01"
    iccid: str = "00000000000000000000"


@dataclass
class ZkGisInfo:
    lng: int = 120000000
    lat: int = 30000000
    zone: int = 8


@dataclass
class ZkDimInfo:
    cns: int = 1
    polar: int = 0
    dlmt: int = 1000
    ulmt: int = 9000
    rti: int = 0
    rtPwr: int = 200


@dataclass
class ZkSenseInfo:
    di: int = 1
    sBri: int = 80
    sBriTm: int = 5


@dataclass
class ZkDeviceConfig:
    dev_info: ZkDevInfo = None
    mdl_info: ZkMdlInfo = None
    gis_info: ZkGisInfo = None
    dim_info: ZkDimInfo = None
    sense_info: ZkSenseInfo = None

    def __post_init__(self):
        if self.dev_info is None:
            self.dev_info = ZkDevInfo()
        if self.mdl_info is None:
            self.mdl_info = ZkMdlInfo()
        if self.gis_info is None:
            self.gis_info = ZkGisInfo()
        if self.dim_info is None:
            self.dim_info = ZkDimInfo()
        if self.sense_info is None:
            self.sense_info = ZkSenseInfo()


@dataclass
class ZkLoginResponse:
    sn: str = ""
    tm: str = ""
    sv: str = ""
    id: str = ""
    ct: str = ""


# ========== 配置构建 ==========
def build_mqtt_config(imei: str) -> ZkMqttConfig:
    """构建MQTT配置"""
    if len(imei) != 15 or not imei.isdigit():
        raise ValueError("IMEI must be 15 digits")
    will_payload = make_will_packet(imei)
    return ZkMqttConfig(
        imei=imei,
        client_id=imei,
        username=imei,
        password=generate_password(imei),
        sub_topic=f"MS/{imei}/plt2dev",
        pub_topic=f"MS/{imei}/dev2plt",
        will_topic=f"MS/{imei}/offline",
        sub_upgrade_topic=f"MS/{imei}/pcp2dev",
        pub_upgrade_topic=f"MS/{imei}/dev2pcp",
        will_payload=will_payload,
    )


# ========== JSON登录包生成 ==========
def make_login_packet(imei: str, tm: str = "2024-04-02 08:30:30",
                      device_config: ZkDeviceConfig = None) -> str:
    """生成JSON格式登录包"""
    if device_config is None:
        device_config = ZkDeviceConfig()

    login_data = {
        "SN": imei,
        "TM": tm,
        "SV": ZK_SV_REPT,
        "ID": "000001",
        "CT": ZK_CT_LOGIN,
        "DT": {
            "DevInfo": {
                "protId": device_config.dev_info.protId,
                "clas": device_config.dev_info.clas,
                "prottp": device_config.dev_info.prottp,
                "hver": device_config.dev_info.hver,
                "sver": device_config.dev_info.sver
            },
            "MdlInfo": {
                "mver": device_config.mdl_info.mver,
                "iccid": device_config.mdl_info.iccid
            },
            "Gis": {
                "lng": device_config.gis_info.lng,
                "lat": device_config.gis_info.lat,
                "zone": device_config.gis_info.zone
            },
            "Dim": [{
                "cns": device_config.dim_info.cns,
                "polar": device_config.dim_info.polar,
                "dlmt": device_config.dim_info.dlmt,
                "ulmt": device_config.dim_info.ulmt,
                "rti": device_config.dim_info.rti,
                "rtPwr": device_config.dim_info.rtPwr
            }],
            "Sense": {
                "di": device_config.sense_info.di,
                "sBri": device_config.sense_info.sBri,
                "sBriTm": device_config.sense_info.sBriTm
            }
        }
    }
    return json.dumps(login_data, separators=(',', ':'))


def make_will_packet(imei: str, tm: str = "2024-04-02 08:30:30") -> str:
    """生成标准JSON遗嘱payload"""
    return json.dumps({
        "SN": imei,
        "TM": tm,
        "SV": ZK_SV_REPT,
        "ID": "000000",
        "CT": ZK_CT_HEARTBEAT,
    }, separators=(',', ':'))


# ========== 登录响应解析 ==========
def parse_login_response(json_str: str) -> tuple:
    """
    解析登录响应
    返回: (success, response)
    """
    try:
        data = json.loads(json_str)
        response = ZkLoginResponse(
            sn=data.get("SN", ""),
            tm=data.get("TM", ""),
            sv=data.get("SV", ""),
            id=data.get("ID", ""),
            ct=data.get("CT", "")
        )
        # 检查是否是登录响应
        success = response.ct == ZK_CT_LOGIN
        return success, response
    except json.JSONDecodeError:
        return False, None


# ========== AT命令构建 ==========
def build_qmt_commands(cfg: ZkMqttConfig):
    """构建AT命令"""
    msgid = 1
    commands = {
        "recv_mode": f'AT+QMTCFG="recv/mode",{cfg.client_idx},0,0\r\n',
        "session": f'AT+QMTCFG="session",{cfg.client_idx},1\r\n',
        "version": f'AT+QMTCFG="version",{cfg.client_idx},{cfg.mqtt_version}\r\n',
        "open": f'AT+QMTOPEN={cfg.client_idx},"{ZK_MQTT_SERVER_IP}",{ZK_MQTT_SERVER_PORT}\r\n',
        "conn": f'AT+QMTCONN={cfg.client_idx},"{cfg.client_id}","{cfg.username}","{cfg.password}"\r\n',
        "sub": f'AT+QMTSUB={cfg.client_idx},1,"{cfg.sub_topic}",{cfg.sub_qos}\r\n',
        "sub_upgrade": f'AT+QMTSUB={cfg.client_idx},2,"{cfg.sub_upgrade_topic}",{cfg.sub_qos}\r\n',
        "publish": f'AT+QMTPUBEX={cfg.client_idx},{msgid},{cfg.pub_qos},{cfg.retain},"{cfg.pub_topic}",128\r\n',
    }
    if ZK_MQTT_ENABLE_WILL:
        escaped_will_payload = cfg.will_payload.replace("\\", "\\\\").replace('"', '\\"')
        commands["will"] = f'AT+QMTCFG="will",{cfg.client_idx},1,{cfg.will_qos},{cfg.will_retain},"{cfg.will_topic}","{escaped_will_payload}"\r\n'
    return commands


# ========== Mock服务器 ==========
class MockQmtServer:
    def __init__(self):
        self.responses = {
            "version": "OK",
            "open": "+QMTOPEN: 0,0",
            "conn": "+QMTCONN: 0,0,0",
            "sub": "+QMTSUB: 0,1,0",
            "sub_upgrade": "+QMTSUB: 0,2,0",
            "publish": "+QMTPUBEX: 0,1,0",
        }

    def reply(self, stage: str, ok=True):
        if ok:
            return self.responses[stage]
        if stage == "conn":
            return "+QMTCONN: 0,5,5"
        if stage == "open":
            return "+QMTOPEN: 0,2"
        return "ERROR"


# ========== 单元测试 ==========
class TestMqttProtocol(unittest.TestCase):

    def test_密码生成(self):
        """测试密码生成算法"""
        # 测试用例来自协议文档 (使用CRC16-CCITT)
        imei = "864294053651521"
        password = generate_password(imei)
        # Modbus密码: BD9D0EE1D3E1
        self.assertEqual(password, "BD9D0EE1D3E1")

    def test_真实IMEI密码生成(self):
        """测试真实IMEI密码生成"""
        imei = "860608074646596"
        password = generate_password(imei)
        self.assertEqual(len(password), 12)
        self.assertTrue(all(c in '0123456789ABCDEF' for c in password))

    def test_MQTT配置构建(self):
        """测试MQTT配置构建"""
        cfg = build_mqtt_config("860608074646596")
        self.assertEqual(cfg.imei, "860608074646596")
        self.assertEqual(cfg.client_id, "860608074646596")
        self.assertEqual(cfg.sub_topic, "MS/860608074646596/plt2dev")
        self.assertEqual(cfg.pub_topic, "MS/860608074646596/dev2plt")
        self.assertEqual(cfg.sub_upgrade_topic, "MS/860608074646596/pcp2dev")
        self.assertEqual(cfg.pub_upgrade_topic, "MS/860608074646596/dev2pcp")
        self.assertEqual(cfg.will_topic, "MS/860608074646596/offline")
        self.assertEqual(cfg.pub_qos, 1)
        self.assertEqual(cfg.sub_qos, 1)
        self.assertEqual(cfg.will_qos, 1)

    def test_登录包生成(self):
        """测试JSON登录包生成"""
        imei = "860608074646596"
        login_packet = make_login_packet(imei)
        data = json.loads(login_packet)

        self.assertEqual(data["SN"], imei)
        self.assertEqual(data["SV"], "rept")
        self.assertEqual(data["CT"], "L")
        self.assertIn("DevInfo", data["DT"])
        self.assertIn("MdlInfo", data["DT"])
        self.assertIn("Gis", data["DT"])
        self.assertIn("Dim", data["DT"])
        self.assertIn("Sense", data["DT"])

    def test_登录包格式(self):
        """测试登录包JSON格式正确性"""
        imei = "860608074646596"
        login_packet = make_login_packet(imei)

        # 验证是压缩JSON（没有多余空格，但时间字段本身的空格是正常的）
        data = json.loads(login_packet)
        # 检查没有换行符
        self.assertNotIn("\n", login_packet)

        # 验证必要字段
        required_fields = ["SN", "TM", "SV", "ID", "CT", "DT"]
        for field in required_fields:
            self.assertIn(field, data)

    def test_遗嘱payload格式(self):
        imei = "860608074646596"
        will_payload = make_will_packet(imei)
        data = json.loads(will_payload)

        self.assertEqual(data["SN"], imei)
        self.assertEqual(data["SV"], "rept")
        self.assertEqual(data["ID"], "000000")
        self.assertEqual(data["CT"], "H")
        self.assertNotIn("DT", data)

    def test_登录响应解析成功(self):
        """测试登录响应解析-成功"""
        response_json = '{"SN":"860608074646596","TM":"2024-04-02 08:30:30","SV":"rept","ID":"000001","CT":"L"}'
        success, response = parse_login_response(response_json)

        self.assertTrue(success)
        self.assertEqual(response.sn, "860608074646596")
        self.assertEqual(response.ct, "L")

    def test_登录响应解析失败(self):
        """测试登录响应解析-非登录响应"""
        response_json = '{"SN":"860608074646596","TM":"2024-04-02 08:30:30","SV":"rept","ID":"000001","CT":"C"}'
        success, response = parse_login_response(response_json)

        self.assertFalse(success)

    def test_设备信息配置(self):
        """测试设备信息可配置"""
        custom_dev_info = ZkDevInfo(
            protId=200,
            clas="MS-SLC-02",
            prottp="iX7-075SC029-4G",
            hver=101,
            sver=102
        )
        device_config = ZkDeviceConfig(dev_info=custom_dev_info)
        login_packet = make_login_packet("860608074646596",
                                          device_config=device_config)
        data = json.loads(login_packet)

        self.assertEqual(data["DT"]["DevInfo"]["protId"], 200)
        self.assertEqual(data["DT"]["DevInfo"]["clas"], "MS-SLC-02")

    def test_AT命令格式(self):
        """测试AT命令格式"""
        cfg = build_mqtt_config("860608074646596")
        cmds = build_qmt_commands(cfg)

        self.assertIn("AT+QMTOPEN", cmds["open"])
        self.assertEqual(cmds["open"], 'AT+QMTOPEN=0,"47.120.15.220",1883\r\n')
        self.assertIn("860608074646596", cmds["conn"])
        self.assertIn("MS/860608074646596/plt2dev", cmds["sub"])
        self.assertIn("MS/860608074646596/pcp2dev", cmds["sub_upgrade"])
        self.assertNotIn("will", cmds)
        self.assertIn("QMTPUBEX=0,1,1,0", cmds["publish"])

    def test_正常登录流程(self):
        """测试完整登录流程"""
        cfg = build_mqtt_config("860608074646596")
        cmds = build_qmt_commands(cfg)
        mock = MockQmtServer()

        self.assertEqual(mock.reply("version"), "OK")
        self.assertEqual(mock.reply("open"), "+QMTOPEN: 0,0")
        self.assertEqual(mock.reply("conn"), "+QMTCONN: 0,0,0")
        self.assertEqual(mock.reply("sub"), "+QMTSUB: 0,1,0")
        self.assertEqual(mock.reply("sub_upgrade"), "+QMTSUB: 0,2,0")
        self.assertEqual(mock.reply("publish"), "+QMTPUBEX: 0,1,0")
        self.assertTrue(cmds["open"].startswith("AT+QMTOPEN=0"))
        self.assertIn(cfg.password, cmds["conn"])

    def test_认证失败错误码(self):
        """测试认证失败错误码"""
        mock = MockQmtServer()
        self.assertEqual(mock.reply("conn", ok=False), "+QMTCONN: 0,5,5")

    def test_IMEI格式校验(self):
        """测试IMEI格式校验"""
        with self.assertRaises(ValueError):
            build_mqtt_config("123")  # 太短

        with self.assertRaises(ValueError):
            build_mqtt_config("abcdefghijk123")  # 非数字


if __name__ == "__main__":
    unittest.main()
