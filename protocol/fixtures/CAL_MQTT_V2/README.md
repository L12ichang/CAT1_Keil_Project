# CAL_MQTT_V2 shared golden fixtures

These files are the byte-for-byte exchange contract between the firmware and
the desktop client. The client repository must copy the four JSON files and
`SHA256SUMS` without reformatting them. Online `contextVersion` is 1 even
though firmware Flash and client sidecar formats may have other versions.

The table fixture is exactly 198 bytes. `tableCrc32` and
`profileBindingCrc32` use CRC32/IEEE. Binding input fields are encoded in the
fixed big-endian order defined by CAL_MQTT_V2; `modelCode` is excluded from
the CRC but is still compared byte-for-byte with the selected build profile.

The response objects intentionally contain the frozen fields that a consumer
must validate. Firmware may include additional diagnostic fields outside
those objects, but it must not emit the removed flat context fields or the
ambiguous `nonzeroOutputAllowed` capability.

`RAW_50W_SNAPSHOT.json` freezes the device-snapshot exchange separately from
the local DC5200 serial protocol. A RAW request carries no DC5200 frame or
direction. `outputCurrentMa` is mA, `outputPower01W` is 0.1 W, and
`adc.ioutRaw` is the device output-current ADC count. `available=true` is valid
only with a fresh ADC/PWM snapshot; unavailable or stale data must return a
nonzero result and `ack=false` rather than placeholder zero measurements.
