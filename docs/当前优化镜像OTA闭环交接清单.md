# 当前优化镜像 OTA 闭环交接清单

更新时间：2026-07-10 14:35 Asia/Shanghai

本清单只针对当前 Release-MinSize 优化镜像，用于上传 OTA 服务器后的最终 live OTA 闭环。当前仓库没有发现服务器上传脚本、上传 API、认证 token 或账号信息；`tools/ota_test/mqtt_ota_guarded_validate.py` 只能在下发前校验 URL 内容并执行受保护的 MQTT OTA 发布。

## 当前镜像身份

| Item                | Value                                                                |
| ------------------- | -------------------------------------------------------------------- |
| APP image           | `output/release_minsize_final_cat1.bin`                            |
| OTA package         | `tools/ota_test/out/release_minsize_ota_20260710_025602.bin`       |
| Size                | `71648` bytes                                                      |
| Header raw size     | `0x000117E0`                                                       |
| Header checksum     | `0x0070A0EE`                                                       |
| Calculated checksum | `0x0070A0EE`                                                       |
| Device type         | `0x0003`                                                           |
| Max APP/OTA size    | `0x1C000` bytes                                                    |
| SHA256              | `B2A289F9E6215F099A92A1F9F3D3FFA4F1BA694A8BBD3B5DEDBBB4EC6AEE132B` |

Header inspection command:

```powershell
python tools\ota_test\inspect_ota_bin.py output\release_minsize_final_cat1.bin --device-type 0x0003 --max-size 0x1C000
```

整体证据复核命令：

```powershell
python tools\ota_test\release_minsize_acceptance_audit.py
```

最新输出为 `tools/ota_test/logs/release_minsize_acceptance_audit_20260710_143812.json`，结论 `summary=pass`。该版本已对基线/Release/Debug 构建产物、构建、串口、当前镜像 live OTA、带载 BL0942、代表性 MQTT/J-Link 实机日志做语义检查，不再只是检查文件存在。

## 当前服务器 URL 状态

当前已验证 URL：

```text
http://47.120.15.220:3915/system/mediaInfo/download/1525141717341429760
```

preflight-only 日志：

```text
tools/ota_test/logs/mqtt_guarded_ota_20260710_142754.jsonl
```

结论：可用于当前优化镜像 OTA。该 URL 返回 `71648` bytes，header checksum `0x0070A0EE`，SHA256 `B2A289F9E6215F099A92A1F9F3D3FFA4F1BA694A8BBD3B5DEDBBB4EC6AEE132B`，与当前 Release-MinSize 镜像完全一致。

旧 URL `http://47.120.15.220:3915/system/mediaInfo/download/1524861027026722816` 仍保留为防误下发证据：`tools/ota_test/logs/mqtt_guarded_ota_20260710_091835.jsonl` 显示其返回 93212-byte 旧镜像，guarded runner 已安全中止。

## preflight 命令

已用当前 URL 执行：

```powershell
python tools\ota_test\mqtt_ota_guarded_validate.py `
  --imei 864512081541939 `
  --expected-image output\release_minsize_final_cat1.bin `
  --url "http://47.120.15.220:3915/system/mediaInfo/download/1525141717341429760" `
  --preflight-only `
  --timeout 60 `
  --download-timeout 60
```

JSONL 已出现 `match: true` 和 `preflight_match_no_publish`，允许进入 live OTA 发布。

## live OTA 发布命令

preflight 匹配后已执行：

```powershell
python tools\ota_test\mqtt_ota_guarded_validate.py `
  --imei 864512081541939 `
  --expected-image output\release_minsize_final_cat1.bin `
  --url "http://47.120.15.220:3915/system/mediaInfo/download/1525141717341429760" `
  --timeout 900 `
  --download-timeout 60
```

实测证据：

1. `tools/ota_test/logs/mqtt_guarded_ota_20260710_142819.jsonl`。
2. MQTT OTA ack `SV=ota`, `CT=W`, `ER=0`。
3. OTA progress 上报 `progress=100`。
4. 设备重启后重新登录，登录包 `sver=22`。
5. offline=0，无 OTA error。

## OTA 后 J-Link 读回验证

live OTA 后新增只读 verify helper：

```powershell
.\tools\ota_test\jlink_verify.ps1 `
  -Image output\release_minsize_final_cat1.bin `
  -Address 0x08008000 `
  -ResetMode ResetPin `
  -ProbeAfterRunMs 12000
```

本次 OTA 后已尝试运行两次，但 J-Link Commander 返回 `Cannot connect to the probe/programmer`，因此没有形成新的 OTA 后逐字节读回证据。既有 Release 烧录/verify 和 reset-pin APP handoff 证据仍有效；如需要 OTA 后额外逐字节确认，恢复 J-Link USB 后重跑上述命令。

## BL0942 带载复测命令

接入电子负载后已运行带范围约束的遥测检查。当前协议上报使用原始缩放值，实测命令为：


```powershell
python tools\ota_test\mqtt_bl0942_telemetry_check.py `
  --imei 864512081541939 `
  --samples 6 `
  --interval 10 `
  --timeout 180 `
  --expect-range v=2000:2600 `
  --expect-range c=50:5000 `
  --expect-range p=1:1500 `
  --require-nonzero v `
  --require-nonzero c `
  --require-nonzero p
```

通过日志：`tools/ota_test/logs/mqtt_release_minsize_bl0942_telemetry_20260710_143039.jsonl`。6 次样本中 `v=2311..2312`、`c=264..265`、`p=56`，v/c/p 均非零，offline=0。

## 原生日出日落 type=2 决策

当前 Release-MinSize 按目标关闭设备端天文计算 `ZK_ENABLE_SUNRISE_PLAN=0`。如果平台接受“平台计算具体时间点后下发普通计划”，本轮普通计划定时调光已有实机 PASS 证据。如果必须支持原生 type=2 下发和设备端触发，需要重新打开设备端天文计算或补充对应兼容策略，并重新构建、烧录、map 对比和实机验证。
