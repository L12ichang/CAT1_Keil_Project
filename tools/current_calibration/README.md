# 电流校准工站工具

安全只读检查：

```powershell
python tools/current_calibration/calibration_station.py --mode info --imei 864512081541939
```

零输出与协议幂等测试不会产生非零 PWM：

```powershell
python tools/current_calibration/calibration_station.py --mode smoke --imei 864512081541939
python tools/current_calibration/calibration_station.py --mode protocol --imei 864512081541939
python tools/current_calibration/calibration_station.py --mode fuzz --imei 864512081541939
python tools/current_calibration/calibration_station.py --mode regression --imei 864512081541939
```

完整 21 点流程必须连接标准仪表和限流负载。非零输出、25% 以上输出和最终写 Flash 分别由
`--enable-output`、`--enable-high-power`、`--commit` 独立解锁。未加 `--commit` 时全点预览后自动 abort。

```powershell
python tools/current_calibration/calibration_station.py --mode calibrate `
  --imei 864512081541939 --rated-current-ma 2700 `
  --enable-output --enable-high-power --serial-port COM23 `
  --meter-id DMM-001 --meter-cal-date 2026-06-01
```

确认设备输出为零后只读导出 A/B 槽：

```powershell
python tools/current_calibration/calibration_station.py --mode jlink-read --imei 864512081541939
```

J-Link 烧录模式会先通过 MQTT 校准状态确认输出为零，并固定使用 reset-pin、
`0x08008000` APP 基址和 112 KB 大小检查。由于现有产品复位策略会执行上电输出，只有在限流安全台架上才可显式解锁：

```powershell
python tools/current_calibration/calibration_station.py --mode jlink-flash `
  --imei 864512081541939 --firmware MDK-ARM-8008000/out/cat1.bin `
  --allow-reset-output
```

CRC 均采用 CRC-32/ISO-HDLC：多项式 `0x04C11DB7`（反射实现 `0xEDB88320`）、
初值和结果异或均为 `0xFFFFFFFF`，输入按小端序列化。曲线输入为
`uint16 version + uint16 pointCount + 21*uint16 logicalPwm`。公共向量：

- `123456789` → `0xCBF43926`
- 曲线 `[0,10,...,200]` → `0x35206DBC`
- 曲线 `[0,1,...,20]` → `0x1F87FDE0`
- 曲线 `[i*i+3*i for i=0..20]` → `0x4525FA2C`

profile 输入为 13 个小端 `uint32`：格式版本、SID、MID、DRV_VERSION、SET_OUTCUR、
HWMAX_OUTCUR、OUTPUT_CUR_SENSOR、OP_PWM_OFFSET、TIM1 PSC、TIM1 ARR、PWM2 模式、
极性、逻辑最大值。示例 `[1,1,4,2,2700,4700,30,0,71,999,2,1,1000]`
的 CRC 为 `0x62FE9B2D`。

所有操作在 `tools/current_calibration/logs` 生成 JSONL、CSV、标准仪表原始样本和曲线报告。

## 本次软件验收中的实机项目

- J-Link 读取与烧录：`NOT_RUN_HARDWARE`
- MQTT 在线非零输出与全点校准：`NOT_RUN_HARDWARE`
- 实机重启/正常控制互斥，以及 exit、abort、超时关断：`NOT_RUN_HARDWARE`

这些项目必须在限流安全台架上另行执行；本次只运行不产生在线输出的软件测试与 Keil 构建。
