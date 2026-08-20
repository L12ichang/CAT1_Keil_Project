# CAL_MQTT_V2 shared golden fixtures

> Status: legacy implementation compatibility evidence. These fixtures remain
> byte-for-byte inputs for V2 regression only. They do not define the target
> calibration protocol for `done/cat1-product-profile-cal-context-20260817`.
> The target specification is Protocol V3 with default SET_OUTCUR 893mA and
> default HWMAX 1400mA, as defined by
> `docs/CAT1_50W校准固件基线与上位机对接方案.md`,
> `docs/CAT1_50W固件与上位机联合审核清单.md`, and
> `docs/CAT1_50W文档口径说明.md`. A separate V3 fixture set is required before
> V3 can be claimed implemented or interoperable.

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
