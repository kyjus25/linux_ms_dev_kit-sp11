# Qualcomm Linux USB4 reference — 2026-08-25

This note records the public Qualcomm Linux material used for the next phase
of the Surface Pro 11 USB4 investigation. It does not authorize a new boot or
host-router activation.

## What is public

- Qualcomm's X1E/Hamoa QMP combo-PHY series adds a distinct USB4 PHY mode and
  TBT3 submode. While that PHY is active, Type-C mode switching is owned by the
  USB4 host router; the ordinary Linux Type-C mux path must not fight it.
- Qualcomm's non-PCIe NHI prepwork separates the generic Thunderbolt NHI code
  from PCIe assumptions so a platform/MMIO host router can use it.
- Qualcomm's host-router DT binding describes the required MMIO, clocks,
  resets, power, interconnect, PHY, IOMMU, mode-switch, and interrupt resources.

## What is not public yet

The corresponding Qualcomm host-router platform driver, including its
microcontroller/mailbox initialization and platform-specific setup, is still
described by Qualcomm as work in progress. The local
`drivers/thunderbolt/qcom_usb4_hr.c` is therefore only a scaffold and must not
be activated on the Surface.

## New Surface firmware evidence

The official Surface UEFI image contains an embedded Hamoa device tree whose
memory map reserves:

```text
0x15500000..0x15800000  mem-label = "USB4"
```

The public Qualcomm X1E host-router example begins at `0x15600000`, so the
Surface firmware reservation corroborates the address family and shows that a
3 MiB USB4 region exists in platform firmware. This is evidence for future
resource mapping only; it does not establish the complete X1P register map,
interrupt wiring, firmware protocol, or safe initialization sequence. The
embedded Hamoa tree also contains generic PTN3222 and PS8830 repeater entries,
but those entries are not a substitute for a Surface-specific Linux binding.

The same firmware tree declares a `NAMEDNODE_USB4` with three SID mappings:

```text
0x03000000 -> 0x00001440
0x03000001 -> 0x00001480
0x03000002 -> 0x000014c0
```

These are useful evidence for the host-router IOMMU wiring, but they are not a
complete Linux `iommus` specifier by themselves. The Windows package also
identifies the host-router stack as `ACPI\\QCOM0C6D` plus a
`USB4\\QCOM0CD10001` filter device; the current Linux boot uses device tree,
so those ACPI IDs do not appear in the live device model.

## Windows control-plane evidence

The extracted `QcUsb4Filter8380.sys` contains strings for PHY/link and mailbox
operations, including `phy_inject_command`, `phy_get_reg`, `MBOX`,
`Starting USB4 FW`, and `Router ready`. The Surface bundle does not contain a
standalone Qualcomm host-router firmware image alongside that driver. This
supports the working hypothesis that the Linux port needs to communicate with
firmware already provisioned or made available by platform firmware, while
implementing the router's control protocol; it does not justify treating the
Audio-DSP `qcadsp8380.mbn` as host-router firmware.

Further static inspection found a Qualcomm DSP-style raw image embedded in the
filter driver's `.data` section: file offset `0x4c000` through the end of
`.data` at `0x55e00` (40,448 bytes, SHA-256
`f0febffdb9bc276563b36e38b1ac10d2381e1de5ef997d1183cc2a54346995f`, entropy
6.728). The region begins on a 0x1000 boundary with non-ARM DSP-looking
instructions and is followed by the USB4 firmware/link-manager strings. A
preceding region around `0x4b800` contains a hash/signature-like block and
tables. Near the end of `.data`, at `0x55a10`, there is a Qualcomm image
metadata block containing `Qualcomm, Inc.`, `SC8380`, and a large digest-like
region; this makes the embedded object more likely to be signed Qualcomm
firmware than ordinary driver data. It still has no ELF/MBN/QCOM magic, so the
exact Qualcomm container/header and load protocol remain unproven. Do not
extract, install, or upload this candidate image yet.

## Important X1E architecture detail

Qualcomm's public discussion indicates that, on X1E, the remote Audio DSP
performs the USB-C entry checks and VDM/SOP exchanges. Linux receives a trimmed
altmode notification and then coordinates the QMP PHY, any onboard retimer,
and the USB4 host router. Therefore, a host-router driver cannot safely invent
the missing partner altmode notification; PMIC-GLINK/UCSI firmware handoff is a
separate prerequisite.

## Surface decision

The known-good LCD boot path remains authoritative:

- kernel: `/boot/vmlinuz-7.2.0-usb4-phy-v4`
- DTB: `/boot/sp11-v4-x1p.dtb`
- initrd ABI: `7.2.0-jg-0sp11v10-qcom-x1e`

No new host-router node, overlay, module parameter, firmware payload, or
register sequence should be added to that boot entry until all Surface-specific
resources and the real Qualcomm driver are available. Any future experiment
must have its own GRUB entry and exact matching initramfs.

## Sources

- https://patchew.org/linux/20260728-topic-usb4phy-v2-0-5d9dd5149ec7%40oss.qualcomm.com/
- https://patchew.org/linux/20260515-topic-usb4._5Fnonpcie._5Fprepwork-v4-0-5c818378243e%40oss.qualcomm.com/
- https://patchew.org/linux/20250916-topic-qcom._5Fusb4._5Fbindings-v1-1-943ecb2c0fa7%40oss.qualcomm.com/
- https://lkml.iu.edu/2509.2/03977.html
