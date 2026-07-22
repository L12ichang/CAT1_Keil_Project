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
  --imei 864512081541939 --rated-current-ma 890 `
  --calibration-max-current-ma 890 `
  --enable-output --enable-high-power `
  --eload-port COM25 --eload-baud 9600 `
  --load-voltage-v 56 --power-limit-w 70 `
  --serial-port COM24 `
  --meter-id ELOAD-001 --meter-cal-date 2026-06-01
```

## PWM calibration v2 contract

- The station requires `requiredCurveVersion=2` and `storageFormatVersion=2` in `readInfo`. A v1 device is rejected before entering calibration.
- `--rated-current-ma` must equal device `ratedCurrentMa`. `--calibration-max-current-ma` defaults to rated current and must satisfy `rated <= CAL_MAX <= hardwareMax`.
- Search targets, all 21 preview targets, resume matching, reports, and curve CRC use CAL_MAX. Changing SET later does not redefine the stored curve.
- Curve CRC bytes are little-endian: `uint16 curveVersion(2) + uint16 pointCount(21) + uint32 calibrationMaxCurrentMa + 21*uint16 logicalPwm`.
- At CAL_MAX 890 mA: `[0,10,...,200] -> 0xA6948939`, `[0,1,...,20] -> 0x8C331965`, and `[i*i+3*i] -> 0xD6911EA9`.
- Resume manifest format 2 binds IMEI, context CRC, rated current, CAL_MAX, hardware maximum, PWM maximum, curve version, and storage format.
- `profileCrc` is a compatibility alias for v2 `contextCrc`; `legacyProfileCrc` is only used to recognize deployed v1 Flash records.

当前设备断电时不得运行上述校准命令；必须先确认电源、电子负载、硬件限流和通信均正常。

`--eload-port` 对接现有电子负载工具使用的协议：9600、8N1，固定发送只读命令
`MEAS:CURR?\n`，设备返回以 A 为单位的一行浮点数；校准脚本自动换算为 mA。
工站不提供自定义 SCPI 命令入口，避免误把控制命令发给电子负载。
默认每个 PWM 至少读取 24 点（100 ms 间隔），通过连续双窗口稳定性检查后才接受。
每次改变 PWM 后默认等待 2000 ms，再开始采样，与原电子负载工具的扫描等待时间一致。
`--serial-port` 是电源设备调试日志串口，不能与电子负载串口相同，也不是 J-Link 端口。
如果暂时不传 `--eload-port`，脚本仍保留原来的手工粘贴 mA 样本模式。
`--load-voltage-v` 与 `--power-limit-w` 必须成对使用，外部实测电流折算功率超过上限时立即中止。

中途因输入掉电等外部原因安全中止后，可重复传入一个或多个 `--resume-csv` 恢复已经验收的连续点。
每个 CSV 必须有同名 `.manifest.json`（格式 2），其中绑定 IMEI、`contextCrc`、额定电流、
CAL_MAX、硬件最大电流、逻辑 PWM 上限、曲线版本和存储格式；缺失或不匹配将拒绝恢复。
恢复时只相信 `measured_ma`，会按 manifest 中的 CAL_MAX 重算目标和误差，不会盲信 CSV 内的
`target_ma` 或 `error_ma`。

自动采样会逐样本检查：非零 PWM 必须得到有限且大于零的电流，且不得超过设备硬件最大电流和给定的
功率门限；0% 仅允许 `--leakage-max-ma` 范围内的小幅正/负偏置。越限或连续三次读数错误时，工站会
根据会话状态请求 `setPwm=0` 或 `setTestPercent=0`，随后由 `finally` 发送 `abort`。
少量复测漂移可用 `--curve-override 65=392` 形式修正指定点；修正后仍必须重新完成全点复测。

正式校准前可先单独验证电子负载串口；该模式不连接 MQTT，也不控制电源设备：

```powershell
python tools/current_calibration/calibration_station.py --mode eload-test --eload-port COM25
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
`uint16 version(2) + uint16 pointCount(21) + uint32 CAL_MAX + 21*uint16 logicalPwm`。
公共向量：

- `123456789` → `0xCBF43926`
- CAL_MAX=890、曲线 `[0,10,...,200]` → `0xA6948939`
- CAL_MAX=890、曲线 `[0,1,...,20]` → `0x8C331965`
- CAL_MAX=890、曲线 `[i*i+3*i for i=0..20]` → `0xD6911EA9`

旧 v1 曲线的 `HH+21H` CRC 和包含 SET 的 13 字段 profile CRC 仅供固件识别、迁移历史
Flash 记录；当前工站不得用它们上传或恢复曲线。

所有操作在 `tools/current_calibration/logs` 生成 JSONL、CSV、标准仪表原始样本和曲线报告。

## 本次软件验收中的实机项目

- J-Link 读取与烧录：`NOT_RUN_HARDWARE`
- MQTT 在线非零输出与全点校准：`NOT_RUN_HARDWARE`
- 实机重启/正常控制互斥，以及 exit、abort、超时关断：`NOT_RUN_HARDWARE`

这些项目必须在限流安全台架上另行执行；本次只运行不产生在线输出的软件测试与 Keil 构建。
