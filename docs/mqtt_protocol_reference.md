# MQTT协议参考文档

## 1. 连接参数

### 服务器配置
- 服务器地址：`47.120.15.220`
- 端口：`1883`
- MQTT版本：4 (AT+QMTCFG="version",0,4)

### 鉴权参数
- ClientID：设备IMEI（15位）
- Username：设备IMEI（15位）
- Password：IMEI三段CRC16-Modbus取反后按3-1-2顺序拼接（12位十六进制）

### 密码生成算法
1. 将15位IMEI分成3段，每段5字节
2. 计算每段CRC16-MODBUS值（多项式：0xA001，初始值：0xFFFF）
3. 对每个CRC值取反
4. 按第3段、第1段、第2段顺序拼接为十六进制字符串

示例：
```
IMEI: 864294053651521
段1: 86429 → CRC16=0xF11E → 取反=0x0EE1
段2: 40536 → CRC16=0x2C1E → 取反=0xD3E1
段3: 51521 → CRC16=0x4262 → 取反=0xBD9D
密码: BD9D0EE1D3E1
```

## 2. MQTT主题

| 方向 | 主题格式 | QoS | 说明 |
|------|----------|-----|------|
| 平台→设备 | `MS/{IMEI}/plt2dev` | 1 | 平台下发消息 |
| 设备→平台 | `MS/{IMEI}/dev2plt` | 1 | 设备上报消息 |
| 升级下行 | `MS/{IMEI}/pcp2dev` | 1 | PCP升级命令 |
| 升级上行 | `MS/{IMEI}/dev2pcp` | 1 | 升级状态上报 |
| 遗嘱消息 | `MS/{IMEI}/offline` | - | 设备下线通知 |

## 3. 消息格式

### 顶层字段
所有MQTT业务载荷统一采用：
- `SN`：设备标识（IMEI）
- `TM`：时间（格式 `YYYY-MM-DD HH:MM:SS`）
- `SV`：服务类别（prop/ctrl/alam/plan/rept/rqst）
- `ID`：报文序号（6位递增）
- `CT`：报文类型（R/W/L/C/B/H/A）
- `DT`：业务数据
- `ER`：错误码（0成功，非0时无DT字段）

### 错误码
| 错误码 | 说明 |
|--------|------|
| 0 | 成功 |
| 1 | 命令不支持 |
| 2 | 数据类型错误 |
| 3 | 数据范围错误 |
| 4 | 字段属性错误 |
| 5 | 必要字段缺失 |
| 6 | 数组越界 |
| 7 | 数组为空 |
| 8 | 字符串长度错误 |
| 9 | 逻辑错误 |
| 10 | 参数存储错误 |
| 90-99 | OTA升级相关 |

## 4. 登录协议

### 登录上报 (CT=L)
```json
{
    "SN": "860608074512731",
    "TM": "2024-04-02 08:30:30",
    "SV": "rept",
    "ID": "000001",
    "CT": "L",
    "DT": {
        "DevInfo": {
            "protId": 100,
            "clas": "MS-SLC-01",
            "prodtp": "iX7-075SC028-4G",
            "hver": 100,
            "sver": 100
        },
        "MdlInfo": {
            "mver": "Cat1",
            "iccid": "8986xxxxxxxxxxxxxxx"
        }
    }
}
```

### 登录响应
平台返回相同SN且CT=L即表示登录成功，设备进入在线状态。

## 5. 心跳 (CT=H)

```json
{
    "SN": "860608074512731",
    "TM": "2024-04-02 08:30:30",
    "SV": "rept",
    "ID": "000002",
    "CT": "H"
}
```

心跳周期默认60秒，平台返回CT=H确认。

## 6. 控制命令 (SV=ctrl, CT=W)

### 回路控制
```json
{
    "SN": "860608074512731",
    "TM": "2024-04-02 08:30:30",
    "SV": "ctrl",
    "ID": "000003",
    "CT": "W",
    "DT": {
        "cnCtrl": [{
            "cns": 1,
            "bri": 80,
            "last": 0
        }]
    }
}
```

### 巡检
```json
{"SV":"ctrl","CT":"W","DT":{"DO":"patrol"}}
```

## 7. AT命令时序

```
1. AT+QMTCFG="version",0,4
2. AT+QMTOPEN=0,"47.120.15.220",1883
3. AT+QMTCONN=0,"{IMEI}","{IMEI}","{PASSWORD}"
4. AT+QMTSUB=0,1,"MS/{IMEI}/plt2dev",1
5. AT+QMTPUBEX=0,1,1,0,"MS/{IMEI}/dev2plt",{len}
```

## 8. 代码接口

### 头文件
- `Core/Src/LampProtocolLib/mqtt_zk_protocol.h`
- `Core/Src/LampProtocolLib/Json_Protocol.h`

### 主要函数
```c
void zk_mqtt_init(void);
void zk_mqtt_generate_password(const char *imei, char *password);
int zk_make_login_packet(char *buf, int buf_size);
int zk_parse_login_response(const char *json_str, zk_login_response_t *response);
```

## 9. 测试

### 单元测试
```bash
python3 -m unittest tests.test_mqtt_protocol_refactor tests.test_login_validation tests.test_mqtt_password_contract -v
```

### 协议参考向量验证
```bash
python3 - <<'PY'
from tools.login_flow import generate_password, make_login_packet, make_heartbeat_packet
import json
imei='864294053651521'
assert generate_password(imei) == 'BD9D0EE1D3E1'
login=json.loads(make_login_packet(imei, tm='2024-04-02 08:30:30'))
assert login['SN']==imei and login['CT']=='L'
hb=json.loads(make_heartbeat_packet(imei, tm='2024-04-02 08:30:30'))
assert hb['CT']=='H' and 'DT' not in hb
print('protocol reference vectors passed')
PY
```

## 10. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2024-04-02 | 初始版本 |
| 2.0 | 2026-05-14 | 更新服务器地址为47.120.15.220:1883 |
