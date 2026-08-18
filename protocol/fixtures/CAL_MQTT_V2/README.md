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
