# CAT.1 8008000 干净工程

本目录是从 `/Users/mac/Desktop/keil project/source_code` 抽离出来的 CAT.1 中科协议 App 工程。

## 工程入口

- Keil 工程：`MDK-ARM-8008000/project.uvprojx`
- App 起始地址：`0x08008000`
- App 安全结束地址：`0x08024000`

## 保留内容

- `Core/`：CAT.1、PWM、电参采集、OTA、中科 MQTT/JSON 协议和实际编译依赖。
- `Drivers/`：当前 Keil 工程引用的 HAL/CMSIS 依赖。
- `docs/`：协议、内存、OTA、烧录和迁移验证文档。
- `tests/`、`tools/`：主机侧协议回归和 Keil App 镜像检查工具。

## 清理规则

- 不保留 Keil 历史输出产物，例如 `out/`、`Objects/`、`Listings/`。
- 不保留未编译 DALI/485 协议转换代码、USB Device middleware、CMSIS NN/Core_A/RTOS 示例包。
- `hw_uart2` 当前用于 BL0942 电参采集，不代表启用 485 业务。
- `hw_gateway/app_active/http_active` 仍被 CAT.1 AT 状态机依赖，先保留兼容。

## 验证

```bash
python3 -m unittest discover -s tests -v
bash tests/run_mqtt_protocol_tests.sh
python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness
```

正式烧录前必须在 Keil 中重新 Rebuild，再运行不带 `--skip-freshness` 的镜像检查。
