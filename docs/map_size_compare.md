# Map Size Compare

Baseline: `output/baseline_cat1.map`  
After first round: `output/release_minsize_final_cat1.map`

ROM = Code + RO-data + RW-data. RAM = RW-data + ZI-data.

| Metric/Object | Before bytes | After bytes | Delta bytes | Note |
|---|---:|---:|---:|---|
| Code | 87906 | 69154 | -18752 | From Keil Program Size |
| RO-data | 5098 | 2286 | -2812 | From Keil Program Size |
| RW-data | 1684 | 1612 | -72 | From Keil Program Size |
| ZI-data | 33684 | 32724 | -960 | From Keil Program Size |
| Total RO Size | 93004 | 71440 | -21564 | Code + RO-data |
| Total RW Size | 35368 | 34336 | -1032 | RW-data + ZI-data |
| Total ROM Size | 93212 | 71648 | -21564 | Code + RO-data + RW-data |
| mqtt_zk_protocol.o | 14191 | 11523 | -2668 | Logging removed in Release |
| ota.o | 12944 | 6261 | -6683 | OTA debug/raw diagnostics gated in Release |
| nbdriver.o | 8233 | 5532 | -2701 | AT/MQTT debug names/logging gated in Release |
| zk_property.o | 5522 | 5522 | 0 | Preserved |
| zk_work_plan.o | 4912 | 4622 | -290 | Device-side sunrise math disabled in Release |
| cjson.o | 4360 | 4360 | 0 | Deferred; receive/send JSON still depends on cJSON |
| app.o | 31 | 0 | -31 | Legacy App path excluded/dead-stripped in Release |
| protocol.o | 0 | 0 | 0 | Dead-stripped in Release |
| systemconfig.o | 0 | 0 | 0 | Dead-stripped in Release |
| json_protocol.o | 170 | 102 | -68 | Lightweight MQTT receive bridge remains |
| sys_bl0942.o | 1256 | 1088 | -168 | Float X-cap compensation disabled in Release |
| hw_uart3.o | 372 | 0 | -372 | UART3 APP log object removed from Release image |

Final Release-MinSize build log: `output/release_minsize_final_build.log`, 0 errors, 0 warnings.
