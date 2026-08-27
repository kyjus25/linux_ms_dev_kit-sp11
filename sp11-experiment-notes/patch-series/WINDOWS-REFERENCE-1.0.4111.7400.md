# Windows Qualcomm USB4 reference

Source: official Surface Pro 11 ARM64 driver bundle
`SurfacePro11_ARM_Win11_26100_26.041.12746.0.msi`

SHA-256: `0c3966bb6f3d39673ae3d2bbd785967d36db149e6c0fa8baa5fa3abd4ccd249b`

The extracted Qualcomm components identify the Windows implementation as:

- `QcUsb4Bus8380.sys`: USB4 dynamic enumeration bus, ACPI device `QCOM0C6D`
- `QcUsb4Filter8380.sys`: lower filter for `USB4\\QCOM0CD10001`
- `qcusbcucsi8380.sys`: Qualcomm USB Type-C/UCSI driver, ACPI device `QCOM0CA4`
- `UCS0.bin`: platform resource description consumed by the Windows Type-C driver
- `SurfaceRetimer0_128.3.6.0.bin`: signed Port 0 onboard-retimer firmware
- `SurfaceRetimer1_128.3.6.0.bin`: signed Port 1 onboard-retimer firmware

The bundle contains no standalone Qualcomm USB4 host-router runtime firmware image.
The host-router behavior is in the closed Windows drivers and platform firmware.
The `.sys` files are therefore reference artifacts, not Linux firmware modules.

## Firmware ownership clarification

The bundle's `SurfaceUpdate/proextadsp/qcadsp8380.mbn` is installed by
`surfacepro_ext_adsp8380.inf` as the Audio DSP image. It is an ELF32 Qualcomm
DSP6 image whose embedded version string identifies
`ADSP.HT.5.9-00810-HAMOA-1`. It contains USB-PD/PMIC symbols because the remote
Audio DSP participates in Type-C policy, but it is not a host-router firmware
payload. The Windows USB4 bus/filter drivers do not reference it as their
firmware image.

The installed Linux copy is byte-for-byte identical to the extracted Windows
image (SHA-256:
`921870a839ee2aba647b04598d62ed96f3d2d5dfbb2499fc842f9a6ff0e0da13`). It must
remain owned by `remoteproc_adsp`; it must not be repurposed by the USB4 driver.

Implication for Linux work:

1. Preserve the known-good `usb4-phy-v4` boot path.
2. Do not port guessed register writes from the Windows binaries.
3. Prefer matching the verified ACPI device/resource model and existing upstream
   PMIC GLINK/UCSI handshakes.
4. Treat the retimer `.bin` files as firmware-update payloads only; do not load
   them as runtime host-router firmware without a verified Linux consumer.
