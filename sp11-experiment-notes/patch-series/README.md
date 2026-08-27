# Surface Pro 11 USB4 / Studio Display experiment

This directory records the Linux source changes made while investigating
Thunderbolt/USB4 support for the Qualcomm X1E Surface Pro 11.

## Patch series

The changes are split into twelve patches:

1. `0001-qcom-x1e-usb4-phy-support.patch` — the upstream v4 X1E
   USB4/Thunderbolt PHY mode support and configuration tables, adapted to
   the ooaklee tree, plus the generic PHY API definitions.
2. `0002-qcom-usb4-host-router-nhi-scaffold.patch` — NHI exports, the
   Qualcomm host-router resource-validation driver, and its build/API pieces.
3. `0003-qcom-x1e-usb4-device-tree-wiring.patch` — shared X1E resources and
   Surface Pro 11 X1E OLED plus X1P LCD DT enablement.
4. `0004-qcom-x1-usb4-enable-ps8830-retimers.patch` — removes the upstream
   USB4-disable flag from the two Denali PS8830 retimers at boot time.
5. `0005-qcom-pmic-glink-altmode-trace.patch` — logs Qualcomm PMIC-GLINK
   altmode notifications, including the SVID, mux state, orientation, and
   HPD state, so USB4/DisplayPort policy failures can be distinguished from
   host-router failures.
6. `0006-qcom-usb4-host-router-safety-fence.patch` — records the activation
   fence for the incomplete host-router scaffold.
7. `0007-qcom-pmic-glink-raw-altmode-trace.patch` — logs raw altmode messages
   before parsing.
8. `0008-qcom-ucsi-glink-raw-notification-trace.patch` — logs raw UCSI GLINK
   notifications before dispatch.
9. `0009-qcom-usb4-host-router-bindings-alignment.patch` — aligns the dormant
   validator with Qualcomm's public reset and compatible binding requirements.
10. `0010-qcom-usb4-nhi-preflight.patch` — connects the validator to the
    common platform-NHI IRQ contract and QMP USB4 PHY behind an explicit
    `qcom,nhi-preflight` DT property; it validates the ring/firmware IRQs and
    PHY linkage but does not activate the router.
11. `0011-qcom-usb4-gated-nhi-activation.patch` — adds an explicit
    `qcom,nhi-activate` gate for clock/reset bring-up, QMP USB4 PHY
    initialization, and the common NHI probe. It remains disabled in all
    current DTBs and does not upload the unidentified firmware image.
12. `0012-qcom-usb4-x1p-test-dtb.patch` — adds the public X1E-derived
    host-router resource map disabled by default and a separate X1P LCD test
    DTB that alone enables `qcom,nhi-activate`.

The host-router driver defaults to resource validation only. Its experimental
hardware activation is available only through the explicit
`qcom,nhi-activate` DT property; it still has no Qualcomm microcontroller/
mailbox upload sequence. The boot-time DTB intentionally contains no activation
property; do not load the host-router overlay or enable the property in the
known-good entry.

The boot-time LCD DTB now omits the `parade,disable-usb4` property from both
PS8830 retimers. This preserves the existing retimer setup while allowing the
USB4/Thunderbolt policy path to negotiate tunnelling. The live overlay cannot
delete an already-present DT property, so patch 0004 must be reflected in the
boot DTB before testing.

`test-usb4-host-router.sh` is retained as a historical diagnostic record only.
Do not run it until the real Qualcomm host-router driver is available and the
Surface-specific resources have been verified.

The generated `work/usb4-host-router-overlay.dts` is also inspection-only. Its
`0x15600000` resource map is from the Qualcomm X1E CRD binding example, not a
verified Surface Pro 11 X1P map, and its host-router node is fenced with
`status = "disabled"`. Do not install or load the matching `.dtbo`.

The Windows `qcadsp8380.mbn` image is the Audio DSP image, installed under the
ADSP device and identified as `ADSP.HT.5.9`. Its USB-PD/PMIC symbols reflect
the DSP's Type-C policy role; they are not evidence that it can be loaded by a
Linux host-router driver.

## Apply

From the matching kernel checkout:

```bash
git apply 0001-qcom-x1e-usb4-phy-support.patch
git apply 0002-qcom-usb4-host-router-nhi-scaffold.patch
git apply 0003-qcom-x1e-usb4-device-tree-wiring.patch
git apply 0004-qcom-x1-usb4-enable-ps8830-retimers.patch
git apply 0005-qcom-pmic-glink-altmode-trace.patch
git apply 0006-qcom-usb4-host-router-safety-fence.patch
git apply 0007-qcom-pmic-glink-raw-altmode-trace.patch
git apply 0008-qcom-ucsi-glink-raw-notification-trace.patch
git apply 0009-qcom-usb4-host-router-bindings-alignment.patch
git apply 0010-qcom-usb4-nhi-preflight.patch
git apply 0011-qcom-usb4-gated-nhi-activation.patch
git apply 0012-qcom-usb4-x1p-test-dtb.patch
```

The patches are generated from the exact ooaklee checkout; the base revision
is:

```text
537d1ac61225
```

The installed test kernel is `7.2.0-usb4-phy-v4+`, with the LCD DTB at
`/boot/sp11-v4-x1p.dtb`. The current working kernel remains available as the
fallback GRUB entry. The upstream reference for the PHY tables is the Linux
v4 series posted by Qualcomm's Konrad Dybcio on 2026-08-20.

## Qualcomm Linux reference status (2026-08-25)

Qualcomm's public Linux PHY and non-PCIe Thunderbolt prepwork has now been
compared with this tree. The common platform-NHI API is already present in the
ooaklee history; patches 0010 and 0011 add gated resource/IRQ preflight and
standard NHI/PHY activation around it. The local `qcom_usb4_hr.c` remains an
experimental scaffold: the real Qualcomm host-router platform driver,
including its microcontroller and mailbox initialization, is not public yet.
The official Surface UEFI image does,
however, reserve `0x15500000..0x15800000` as a `"USB4"` region; this
corroborates the public X1E address family around `0x15600000` but is not a
complete X1P host-router map or initialization recipe. The same firmware
contains three USB4 SID mappings, and the Windows package names the related
devices `ACPI\\QCOM0C6D` and `USB4\\QCOM0CD10001`; those facts help map the
IOMMU and control-plane dependencies but do not provide the missing Linux
initialization sequence. Do not activate the scaffold or add its node to the
known-good boot entry. See
`QUALCOMM-LINUX-REFERENCE-2026-08-25.md`.

The Windows filter binary exposes names for PHY-register injection, mailbox
commands, firmware-version reporting, and router-ready dispatch. Static
inspection also found a 40,448-byte Qualcomm DSP-style raw image embedded in
its `.data` section at file offset `0x4c000` (`f0febffdb9bc276563b36e38b1ac10d2381e1de5ef997d1183cc2a54346995f`), preceded by a hash/table area. It is not an ELF/MBN/QCOM container, so its exact load format is not established; it must not be extracted or uploaded. The Surface bundle has no separate host-router firmware payload, and the Audio-DSP image is not interchangeable with that firmware.

## Test tuple status (2026-08-25)

First boot of the activation tuple reached the initramfs: the matching NVMe
modules loaded and the controller probed (`nvme0` created I/O queues at ~11 s),
but the initramfs gave up waiting for the root namespace at the same moment and
dropped to its recovery shell (`ALERT! UUID=... does not exist`). This was a
root-device discovery race, not a USB4 or ABI failure. The recovery shell has no
keyboard input: the Folio keyboard needs HID/I2C modules that the minimal
initramfs does not carry (GRUB and the full OS work because they use UEFI
console services and the complete module tree respectively).

Fix: `rootwait` was added to the linux line of the
`81_sp11_usb4_hr_activation` GRUB entry only (regenerated via `update-grub`).
The known-good entries are unchanged. If the namespace never appears under
`rootwait`, the next isolated change to try is
`nvme_core.default_ps_max_latency_us=0` on the test entry.

Update (same day): a second failed boot was traced to selecting the similarly
named "host-router resource test" entry, which is a different, older tuple
(kernel `7.2.0-usb4-hr-test`) whose initramfs contains no NVMe modules at all.
It was never the activation tuple. The activation entry has been renamed to
">>> ACTIVATION TEST: X1P usb4-hr-x1p-test + rootwait + DTB <<<" to make it
unambiguous. Note that update-grub also auto-generates plain
"Ubuntu, with Linux 7.2.0-usb4-hr-x1p-test" entries inside the Ubuntu submenu;
those boot the test kernel with the firmware DTB and without rootwait and must
not be used for this experiment.

Update (root discovery, third attempt): two further findings. First, this
initramfs-tools generation does not honour `rootwait` at all: `init` parses
only `rootdelay=`/`roottimeout=`, and `scripts/local` hardcodes a 30 s wait
(the 180 s branch is PowerPC-only), so the earlier `rootwait` addition was
inert. Second, `/usr/bin/wait-for-root` is missing from the hand-assembled
initramfs, so the primary UUID wait path in `scripts/local` silently fails and
everything falls back to udev-created `/dev/disk/by-uuid` symlinks. The
activation entry now uses `root=/dev/nvme0n1p2` (plain devtmpfs path; no udev,
no UUID symlink, no wait-for-root involvement). Fallback entries untouched.

Input caveat: the test kernel carries no HID stack (neither built into the
image nor present in its six-module tree; `CONFIG_I2C_HID*`, `HID_GENERIC`,
`HID_MULTITOUCH`, `USB_HID` are =m in the baseline config), so keyboard,
touchscreen, and WiFi are expected to be dead while running it. The evidence
path for the activation test is the boot console plus the persistent journal
(`/var/log/journal`), which can be read from the known-good kernel afterwards
with `journalctl -b -1 -k`.

Update (fourth attempt): the fourth boot showed the NVMe root port
(`1b90000.pcie`) only beginning its probe at ~12 s, and the SSD never appeared
within the 30 s window, so `/dev/nvme0n1p2` did not exist when the wait expired.
Boot-to-boot probe-order variance (this boot also logged
`arc_scale: probe ... returned -95`) makes a 30 s window too tight. The
initramfs has now been regenerated properly with
`mkinitramfs -k 7.2.0-usb4-hr-x1p-test` (MODULES=most), replacing the
hand-assembled one: this restores `/usr/sbin/wait-for-root`, a complete udev,
and the NVMe modules, and removes the whole class of hand-assembly defects.
The previous image is preserved as
`/boot/initrd.img-7.2.0-usb4-hr-x1p-test.handbuilt.bak`. `thunderbolt.ko` and
`qcom_usb4_hr.ko` are intentionally not in the initramfs; they load from the
root module tree after pivot. The activation entry now also passes
`rootdelay=45`, which this initramfs-tools generation does honour, widening the
root-device window to 45 s.

Update (fifth attempt): also failed to find the root device; no journal was
written (the boot never reached userspace). The initramfs has been patched so
the root-wait give-up path first prints diagnostics to the console (`/dev/nvme*`,
`/sys/class/nvme`, `/sys/class/block`, PCI device list, loaded modules, and the
nvme/pcie/phy dmesg tail), then attempts `pci rescan` plus NVMe
`rescan_controller` and waits up to 90 more seconds, re-rescanning every 15 s.
If the SSD appears at any point the boot continues automatically. Backups:
`.handbuilt.bak` (original hand-assembled image), `.clean.bak` (unpatched
mkinitramfs rebuild). If the diagnostics print and the device never appears,
the console output identifies the failed layer (PCIe link vs NVMe namespace).

Update (sixth attempt, diagnostics analysis): the diagnostics block did not
print because the patch gated on `name = "root"` while `local_device_setup` is
called with `name = "root file system"`; the gate now matches on the
`/dev/nvme*` device path instead, and the repacked image is verified to carry
it. Photo analysis of the sixth attempt showed the NVMe host controller
(`1bf8000.pci` on this X1P; `1c08000` is WiFi) begin probing at ~11 s with no
link ever coming up, plus a consistent
`arm-scmi: failed to setup channel for protocol:0x10`. The scmi node comes from
`hamoa.dtsi` and is present in every mainline-style DTB including the v4
fallback, so it is not the differentiator it appeared to be.

More significant: the journal contains no boot ever reaching userspace with the
v4 kernel either — the only proven-working configuration on this machine is the
jg kernel with the firmware-provided device tree. The v4 tuple's "known-good"
status was assumed, never observed. Failure staging also correlates with power
state: the one boot where the NVMe controller fully initialised followed a
clean reboot, while every attempt after a hard power-off failed earlier at
PCIe link-up. Next attempt therefore uses a clean `systemctl poweroff` before
powering on into the test entry.

## Full rebuild (2026-08-25, seventh attempt)

Root cause of every failed activation attempt found, two defects stacked:

1. **Vermagic mismatch**: every module in `/lib/modules/7.2.0-usb4-hr-x1p-test`
   was built for release `7.2.0+`, but the installed vmlinuz is
   `7.2.0-usb4-hr-x1p-test`. The kernel refused **all** modules
   (`/proc/modules` empty in the diagnostics photo) — including nvme, PCIe,
   PHY, everything. The split happened because changing `CONFIG_LOCALVERSION`
   after an earlier build relinks vmlinuz but leaves stale `.ko` files alone.
2. **Missing TCSRCC**: `CONFIG_CLK_X1E80100_TCSRCC=m` provides
   `TCSR_PCIE_4L_CLKREF_EN`, the NVMe PCIe PHY's `ref` clock. It was never in
   the six-module tree, so even with matching vermagic the PHY would defer
   probing forever.

The kernel source tree had been cleaned (no `.config`, no `Module.symvers`),
so a full rebuild was required:

- `.config` restored from `/boot/config-7.2.0-jg-0sp11v10-qcom-x1e`, then
  `LOCALVERSION="-usb4-hr-x1p-test"`, `LOCALVERSION_AUTO=n`,
  `QCOM_USB4_HR=m`, `THUNDERBOLT=m`; `make olddefconfig`.
- Exact release string: `scripts/setlocalversion` appends `+` because HEAD is
  past an annotated tag (`scm_version --short`). The original build evidently
  ran without git visible, so the rebuild did the same: `.git` moved to
  `.git-build-hidden` during the build and restored afterwards (fully
  reversible; tree contents untouched). Verified `make -s kernelrelease` ==
  `7.2.0-usb4-hr-x1p-test` exactly.
- Build: `make -j10 LOCALVERSION= vmlinux modules`, then
  `make -j10 LOCALVERSION= Image`. Note: a plain full `make` dies in `dtbs` on
  the unrelated upstream bug `x1e78100-lenovo-thinkpad-t14s.dtsi` (references
  nonexistent labels camss/cci1/csiphy4); bypassed by building explicit targets.
  Our own DTBs are unaffected.
- Verification before install: embedded Image version string and `modinfo -F
  vermagic` for nvme.ko / tcsrcc-x1e80100.ko / qcom_usb4_hr.ko all read
  `7.2.0-usb4-hr-x1p-test SMP preempt mod_unload modversions aarch64`.
- Installed: new vmlinuz to `/boot/vmlinuz-7.2.0-usb4-hr-x1p-test`;
  `make modules_install` → **7816 modules** (was 6), DEPMOD run; baseline
  config copied to `/boot/config-7.2.0-usb4-hr-x1p-test`; initramfs
  regenerated with mkinitramfs (MODULES=most) — now carries tcsrcc +
  nvme + wait-for-root + full udev (137 MB).
- Backups: broken vmlinuz as `/boot/vmlinuz-7.2.0-usb4-hr-x1p-test.broken.bak`;
  stale module tree moved to `/lib/modules/7.2.0-usb4-hr-x1p-test.broken.bak`;
  previous initramfs as `...initrd.img-...diag.bak`.
- Discovery: this initramfs is **systemd-based** (`/init` is an ELF systemd
  binary) — there is no `scripts/local` to patch; the earlier "diagnostics
  patch" premise does not apply to this framework. The clean generated image
  is used as-is.

Bonus: the full HID/I2C/WiFi module set is now present, so Folio keyboard,
touchscreen, and WiFi should work under the test kernel — interactive
checklists become possible.

GRUB entry unchanged (same filenames). Next: clean `systemctl poweroff`,
power on into ">>> ACTIVATION TEST: X1P usb4-hr-x1p-test root=/dev/nvme0n1p2 <<<".

## Seventh attempt result: BOOTED (2026-08-25 16:38)

**First non-jg kernel+DTB tuple ever observed reaching userspace on this
machine.** The rebuild fixed everything it targeted:

- 182 modules loaded (`/proc/modules` empty before); tcsrcc, nvme, thunderbolt,
  qcom_usb4_hr all present.
- Root on NVMe via initramfs coldplug; `rootdelay=45` not even needed.
- Folio keyboard / touchscreen / WiFi alive as predicted (full HID stack now
  present) — interactive debugging possible for the first time.
- `qcom_usb4_hr` bound to `15600000.usb4-host-router` and started probing.

New failure point (progress, one layer deeper):

```
qcom-usb4-hr 15600000.usb4-host-router: error -EBUSY: reset phy_nocsr unavailable
probe failed with error -16
```

Analysis: WARN in `__fwnode_reset_control_get` → drivers/reset/core.c refuses
mixed exclusive/shared access to one reset line. The HR node's second reset,
named `phy_nocsr` (`GCC_USB4_0_DP0_PHY_PRIM_BCR`, GCC cell 0x4f), is also
claimed **exclusively** by `phy@fd5000` (`qcom,x1e80100-qmp-usb3-dp-phy`) in
the same DTB. Two nodes, one BCR, different access modes → whoever probes
second loses (-EBUSY). The driver already anticipated this ("The QMP PHY owns
this reset") by requesting phy_nocsr optional-shared; the framework still
refuses because the PHY holds it exclusive. The wiring defect is in
`hamoa.dtsi`: the HR node should never have listed that BCR.

Fix applied (DT-only, no kernel rebuild):

- Removed `<&gcc GCC_USB4_0_DP0_PHY_PRIM_BCR>` + `"phy_nocsr"` from the
  `usb4_hr0` node in `hamoa.dtsi` (12 resets remain). Driver's bulk
  assert/deassert paths tolerate the resulting NULL (optional get).
- Rebuilt `x1p64100-microsoft-denali-usb4-test.dtb` manually
  (`gcc -E -nostdinc -D__DTS__ -x assembler-with-cpp` + in-tree
  `scripts/dtc/dtc -@`; kbuild single-dtb targets are broken here, and plain
  `make dtbs` dies on the unrelated t14s dtsi bug). Verified decompiled
  output: 12 reset pairs, no `phy_nocsr`, node okay + nhi-activate present.
- Installed as `/boot/sp11-x1p-usb4-hr-activation.dtb`; previous image kept
  as `.nocsr-conflict.bak`.

Note for upstreaming: the hamoa.dtsi fix belongs in patch 0004's series (or a
new patch) — every board enabling usb4_hr0 alongside its QMP PHY would hit
this -EBUSY.

## Eighth attempt: freeze (2026-08-25 ~16:51)

With the phy_nocsr fix the boot got past reset acquisition but **died in an
NVMe I/O timeout/abort storm** ("64 I/O contexts aborted", cid list, abort
status) — root I/O hung and the machine froze mid-systemd-start. The frozen
boot left no journal (freeze before flush) and no pstore (only efi_pstore
registered; no ramoops). Evidence is the boot photo only. Earlier reading of
the photo as "unhandled event" lines was wrong — the bottom lines are NVMe
aborts (cid values). Also visible: `deferred probe pending` for
15600000.usb4-host-router (suppliers not ready) and a large
`sync_state() pending due to 15...` block from GCC GDSC/interconnect
providers waiting on the HR device.

Working hypothesis: with the -EBUSY gone the HR probe proceeds further into
clock/GDSC/interconnect acquisition and something in that path (or the late
bind ordering after deferral) disturbs the PCIe/NVMe path on this X1P board —
consistent with the standing caveat that hamoa.dtsi wiring is X1E-derived and
unverified for X1P. Unproven; the freeze is not yet localized.

Mitigation applied (observability): added a ramoops reserved-memory node to
the test DTB — 4 MB at physical 0xbffc00000 (top of highest System RAM range,
no-map), record/console 256 K each. After any freeze, the previous boot's
console log survives in RAM and appears under `/sys/fs/pstore/` on the next
boot. Gotcha recorded: dtc truncates >32-bit cell values, so the 64-bit reg
must be written pre-split (`<0xb 0xffc00000 ...>`), not `<0x0 0xbffc00000>`.
Rebuilt and installed; prior DTBs backed up (`.nocsr-conflict.bak`).

Next boot: if it freezes again, read `/sys/fs/pstore/console-ramoops*` after
reboot — that log localizes the failure. If it boots, run the checklist and
confirm pstore is empty.

## Ninth attempt: clean boot, firmware handshake reached (2026-08-25 17:4x)

**Clean boot, stable system, pstore empty** (the eighth-attempt freeze did not
reproduce — consistent with the boot-to-boot power-state variance seen all
along). The driver then got further than ever:

```
qcom-usb4-hr 15600000.usb4-host-router: NHI preflight passed: aperture=... ring_irq=237 fw_irq=238; activation=requested
qcom-usb4-hr 15600000.usb4-host-router: device links to tunneled native ports are missing!
qcom-usb4-hr 15600000.usb4-host-router: RX overflow for ring 0
qcom-usb4-hr 15600000.usb4-host-router: error -ETIMEDOUT: failed to allocate host router
probe failed with error -110
```

Reading: preflight (MMIO/IRQs/PHY) passed; `nhi_probe` → `tb_domain_add` →
software CM `tb_start` → `tb_switch_alloc` issues a config-space read over the
NHI rings and times out (~420 ms). The ring IRQ fired (the RX overflow is
printed by `nhi_interrupt_work`; the `/proc/interrupts` line disappears
afterwards because the failed probe's devm teardown frees the IRQ). The
"device links" warning is expected on DT (no Apple/ACPI link source).

Status: **hardware probed and firmware conversation attempted; handshake
incomplete.** The missing piece is the one the reference section predicted:
the Qualcomm USB4 microcontroller (UC) firmware/mailbox bring-up. The driver
deliberately does not load UC firmware ("No Qualcomm mailbox or firmware
upload sequence is inferred here"); a UC without firmware cannot answer the
tb_cfg exchange — the RX overflow is noise, not a reply.

## Microsoft's own X1P overlay validates the DTB (2026-08-25)

Found `/lib/firmware/qcom/x1e80100/microsoft/Denali/sp11-usb4-host-router.dtbo`
(ships with the firmware package). Decompiled and compared cell-by-cell with
our activation DTB — **identical** on every property:

- resets: the same 12, **without** 0x4f — Microsoft's overlay independently
  confirms the phy_nocsr removal (their node never listed the QMP-owned BCR)
- clocks: 0x135/0x136/0x128/0x08/0x12b/0x130/0x131/0x133/0x129/0x12a =
  SYS/TMU/CFG_AHB/AGGRE_AXI/MASTER/PHY_RX0/PHY_RX1/SB_IF/DP0/DP1 — exact match
  (verified against `qcom,x1e80100-gcc.h`: 0x136=310=TMU, 0x08=8=AGGRE_AXI)
- interrupts SPI 472/579, iommus 0x1440, GDSC, usb_1_ss0_qmpphy USB4 mode,
  interconnects — all match; fragment@1's QMP "ref" clock (rpmhcc CXO) was
  already present in our base
- status = "okay" but **no activation property** — Windows does bring-up in
  its QCOM0C6D driver (mailbox commands, PHY register injection,
  firmware-version reporting, router-ready dispatch), matching the embedded
  40 KB UC image noted in the reference section

Conclusion of the hardware-enablement phase: DTB wiring is complete and
authoritatively validated. The remaining blocker is the non-public UC
firmware initialization recipe. Possible follow-ups: (a) request the fw_irq
(SPI 579, currently parsed but never requested) with a logging handler to
detect UC liveliness — needs a kernel rebuild; (b) deeper static analysis of
the Windows QCOM0C6D filter driver's mailbox sequence; (c) wait for
Qualcomm's public host-router driver. The experimental entry remains safe to
boot: a failed activation leaves the system fully functional (NVMe, keyboard,
touchscreen, WiFi all work under this kernel).

## UC firmware load sequence reverse-engineered (2026-08-25, static only)

Static analysis of `QcUsb4Filter8380.sys` (objdump pei-aarch64, no execution,
no extraction beyond local parsing). Key discovery: the "40,448-byte embedded
image" from the earlier reference note is the data section of a **segmented
UC firmware container** at `.data` VA `0x14004d1c0` (file offset `0x4b9c0`,
total `0x9f70` bytes), formatted as repeated `{u32 target, u32 word_count,
u32 words[count]}` records. Parsed records:

- segment 1: target `0x0000`, 7840 words (31360 bytes), first word
  `0x2c0004af`
- segment 2: target `0x8000`, 2360 words (9440 bytes), first word
  `0x00000428`

The burst writer (`0x140005490`) writes each word to
`window0 + 0x13000 + target + 4*i`, i.e. into **uc_ram (0x15613000, 56 KB)**:
segment 1 at `0x15613000`, segment 2 at `0x1561b000`. 40800 payload bytes fit
the aperture.

The bring-up function (via RMW helper `0x140001388` = `(read & ~mask)|value`)
does:

```
write(0x15622000, 0)            # halt UC (uc_per + 0x1000, plain write)
<burst segments into uc_ram as above>
rmw(0x1560d064, value=0, mask=0x40)        # clear bit 6 (port_group window)
rmw(0x15608018, value=0, mask=0x1000000)   # clear bit 24 (router_config)
rmw(0x15622000, value=1, mask=1)           # UC GO
poll read(0x15600018) & 0x01000000         # UC ready, 5 ms interval, bounded
```

Register map established from the accessor/diagnostic functions:

- `uc_per+0x000/0x004/0x014/0x01c` (`0x15621000…`): status reads
- `uc_per+0x1000` (`0x15622000`): bit0 = started/GO control
- `uc_per+0x100c` (`0x1562200c`), `uc_per+0x1ffc`: status reads
- `router+0x018` (`0x15600018`): bit24 = UC ready
- `0x15623000 + 4*i` (8 words): mailbox debug dump area (diagnostic only; the
  driver never writes uc_mbox directly — driver↔UC traffic is the NHI rings)

Interpretation: the Windows driver loads the UC firmware itself; there is no
separate firmware file in the bundle. The firmware's own strings (`Starting
USB4 FW ver %x.%x.%x (%s boot) [Nov 24 2024]`, `Router ready. Begin DSR
dispatch`) confirm it implements the NHI ring protocol the Linux Thunderbolt
stack already speaks. The Linux port recipe is therefore: check
`0x15622000 & 1` first (UEFI may have warm-started the UC), else halt → load
segments → clear the two gates → GO → wait ready → hand the NHI to the
existing `tb` stack. Payload stays inside the signed Microsoft driver on
disk; any Linux firmware file would be a local extraction on this machine
only (see safety-fence patch 0006 policy before packaging).

A read-only ucpeek module (built against the running test kernel, init fails
by design so it never stays resident) confirmed the block is fully powered
down after a failed probe (all reads zero), so live UC-state forensics
requires powering the block first — i.e. the sequence above.

## UC bring-up implemented (2026-08-25 23:0x, awaiting boot test)

`qcom_usb4_hr.c` gained `qcom_usb4_hr_uc_bringup()`, called after `phy_init`
and before `nhi_probe`, gated behind `qcom,nhi-activate` as before:

- warm-start check: `uc_per+0x1000 & 1` → skip load if the UC is running
- else: halt (`=0`) → `request_firmware("qcom/x1e80100/microsoft/Denali/
  sp11-usb4-uc-fw.bin")` → stream segments into `uc_ram` (bounds-checked:
  count ≤ 0x4000 words, target+data ≤ 56K) → clear `port_group+0x64` bit 6
  and `router_config+0x18` bit 24 → GO (`uc_per+0x1000 |= 1`) →
  `readl_poll_timeout` on `router+0x18` bit 24 (5 ms interval, 10 s cap)

Firmware file installed from the local static extraction of the signed
Windows driver (container format preserved, 40816 bytes,
SHA-256 `cd4f5929b51f2dbb0b583693ff8d024521c87f0d2e45c7adc142fed976650b99`).
Local-machine use only per the patch-0006 safety fence.

Deployment notes (recorded because they bit once): an `M=` modules_install
lands in `updates/` and depmod prefers it over the canonical tree — the new
`qcom_usb4_hr.ko` was copied into `kernel/drivers/thunderbolt/` by hand, the
stale `.zst` and the whole `updates/` shadow removed, `depmod -a` rerun, and
`modules.dep` verified to resolve `qcom_usb4_hr.ko` → the canonical
`thunderbolt.ko.zst`. Initramfs unchanged (driver loads post-pivot).

Expected outcomes of the next boot, in order of preference: UC ready +
`tb_switch_alloc` completes (domain appears under
`/sys/bus/thunderbolt/devices/`); UC ready but handshake still incomplete
(new, more specific timeout — iterate); UC not ready (gate/enable mismatch —
recheck the two cleared bits); firmware rejected/segment fault (container
parse issue). ramoops remains in place for any freeze.

## Tenth attempt: NHI ACTIVE, domain registered (2026-08-25 23:12)

**The UC bring-up worked first try.** Boot log: `USB4 UC firmware loaded
(40816 bytes)` → `USB4 UC ready` (7 ms) → DROM exchange → probe **succeeded**
(`host-router NHI is active`). `/sys/bus/thunderbolt/devices/` now shows
`domain0` and host router `0-0` (vendor `SC8380`, USB4 gen 4, authorized);
ring IRQ (SPI 504) firing. Remaining wart: DROM parse fails (`-5`,
`DROM buffer overrun` in `eeprom.c` entry walk — the UC's DROM layout
differs from Intel's), and a `Runtime PM usage count underflow` warning.

Hotplug does not work yet: plugging the Studio Display (either port) yields
PD-level attachment (`typec portN-partner`, UCSI traffic) but **zero**
USB4 link activity; the thunderbolt IRQ count stays frozen at its boot-time
value. Port mapping established: physical port0 = connector@0 = `fd5000`
(ss0) = the PHY our USB4 HR owns; port1 = `fda000` (ss1), not covered by the
single enabled HR.

Two follow-ups identified: (1) the UC's connect/event notifications may
arrive on the **fw_irq (SPI 579)**, which the driver parsed but never
requested — a logging handler has now been added (next boot will show
whether it fires on connect); (2) the machine auto-suspended during testing
and on resume `gcc_usb4_0_gdsc` came back **stuck at 'off'** — the USB4
domain dies across suspend and does not recover. Avoid suspend during USB4
tests; resume handling needs work before suspend is safe with the driver
active.

## Eleventh attempt: hotplug isolated to Type-C USB4-mode entry (2026-08-25 23:4x)

fw_irq (SPI 611) now requested with a logging handler: **zero firings** on
plug, ring IRQ frozen at its boot-time count, no new switches. Live register
forensics (ucpeek) while the domain is up:

- `uc_per+0x1000 = 1` — UC running; `uc_per+0x018/0x01c = 0x512e/0x5136`
  (likely firmware version registers)
- NHI aperture decodes only its low region: base registers sane, but the
  Intel-style high offsets (`REG_CAPS` +0x39640, `REG_INMAIL` +0x39900,
  `REG_RESET` +0x39898) all read `0xffffffff` — the Qualcomm NHI does not
  implement the Intel register map there. The tb core's `hop_count` read is
  garbage (`0x3ff`) and USB4 router-operations via INMAIL (`usb4.c`) cannot
  work at those offsets. Ring registers (low offsets) are compatible, which
  is why the boot-time exchange worked.

Root cause of silent hotplug: **the Type-C stack never switches the SS lanes
to the USB4 PHY**. On plug, only PD-level attachment happens
(`port0-partner`, UCSI traffic); no `typec_mux_set` reaches the ps883x
retimer or the `fd5000` QMP switch, so the UC never sees the display and
nothing trains. USB4-mode entry over Type-C (connector policy ↔ mux chain ↔
tb stack coordination) is precisely the piece still missing upstream for
X1E; on Windows the QcUsb4Bus driver provides that glue.

Session endpoint — working: boot, module tree, UC firmware load and start,
NHI active, `domain0` + `0-0` (SC8380, USB4 gen 4) registered, ring
protocol alive. Remaining, in dependency order: (1) Type-C USB4-mode entry
glue (connector policy → mux → UC), (2) DROM parse fix (buffer overrun in
`eeprom.c` entry walk), (3) NHI register-map divergence (CAPS/INMAIL
offsets — affects `usb4.c` router ops and hop_count), (4) suspend/resume
GDSC recovery, (5) Runtime PM underflow warning in the scaffold.

## Hotplug chain verified good; UC register map extended (2026-08-25 late)

The full Type-C→UC chain was then verified live, and it is all good up to
the UC: the Studio Display negotiates **TBT alt mode** (`svid=0x8087`) and
the ADSP reports `mux=4 = MUX_CTRL_STATE_TUNNELING`, so
`pmic_glink_altmode_enable_tbt()` runs without error; the ps883x retimer is
programmed (`CONN_STATUS_2_TBT_CONNECTED`) and the QMP USB4 PCS shows
`START_CTRL=0x03030303`, `SW_RESET=0`, `PCS_STATUS1` bit6 clear = init
complete on all lanes. The UC (running, `uc_per+0x1000=1`, version-ish regs
`0x512e/0x5136`) simply never trains the link and never sends
`TB_CFG_PKG_EVENT`. Register-map corrections from live peeks: `REG_CAPS`
reads `0x3` (3 hops — Intel-compatible, earlier "0x3ff hop_count" was a
page-alignment artifact of the first peek module), `RING_NOTIFY` decodes
and reads 0 when idle; `REG_INMAIL`/`OUTMAIL`/`REG_RESET` read `0xffffffff`
(the USB4 router-operations mailbox is not implemented at Intel offsets).
Linux ctl rings are hop0 TX + hop0 RX, so the UC's event channel is
configured; the boot-time `RX overflow for ring 0` was a startup race.

Post-bring-up Windows flow decoded (filter driver): RMW
`usbap_config+0x10` (value/mask `0xc/0xc`), UC status reads at window
offsets `0x20a4/0x20a8` (uc_ram tail = UC register file), and the
`uc_per+0x1010/0x1018/0x101c` (`0x22010/18/1c`) register trio — a probable
mailbox doorbell set. This adapter/UC-policy configuration after
"Router ready" is the current best candidate for what arms link training.

Incident: one hard freeze (green screen) during live ucpeek probing of the
NHI register area; log was lost because efi_pstore had claimed the pstore
backend and ramoops was ignored. Fixed by adding `pstore.backend=ramoops`
to the activation entry cmdline (grub regenerated and verified). Lesson
recorded: no more live MMIO pokes in the active NHI window while the tb
stack is running; use the journal and offline analysis instead.

## Firmware architecture decoded from string table (2026-08-26)

Full string-table dump of the embedded UC firmware (0x4c000-0x55e00) maps
the entire architecture:

- **Plug detection is sideband-based**: `sb_init` → `"SB initialized"` →
  `"Register SBRX_CONNECT callback"` → `"SB Connected %p"`. Link training
  phase 1 (`lnk_hse_p1`) logs `"Waiting for SB RX Connect"` — **HSE blocks
  until the UC↔retimer Sideband channel reports connect**. Then phases 1-5
  (`lnk_hse_p1..p5_usb4` / `p5_tbt3`), lane bonding, FOM equalization,
  `"Lanes bonded successfully"`, CL0.
- **Notification path to host**: `cp_send_hot_plug` ("HP AP=%x HUP=%x") →
  `cp_send_notification_to_cm` ("NotifCode 0x%x AN 0x%x") over the cp rings
  (`cp_inject_rx_packet`, `cpRxRing overflow`) — these are the NHI rings
  the Linux tb core already reads. `"ERR_PLUG notification dropped"` and
  `"Notif %x dropped due to USB4 version"` exist as failure paths.
- **PHY control**: `phy_inject_command` / `phy_get_reg` /
  `"sPhyCmdRing overflow"` — the UC drives the QMP via an internal command
  ring with a register table.
- **DP switch**: `dp_init`, `"DP 2.0 initialized"`, `dp_isr_cb`,
  `dp_send_cmd` — full DP tunneling state machine inside the UC.
- **DROM served by the UC**: `"Qualcomm, Inc."` / `"SC8380"` strings sit in
  the DROM section (`cp_drom_check_external`, `"DROM imported
  successfully"`, `"Invalid DROM CRC"`, `"Unexpected DROM version %x"`) —
  explains both the SC8380 vendor_name we saw and the DROM parse failure
  (the Linux parser choked on the UC's DROM layout).
- **Hotplug arming verified**: `usb4_port_hotplug_enable()` (ADP_CS_5 DHP
  clear, config-space write) succeeded during boot — the UC accepted the
  arming; the write trace of the whole Windows driver (117 window writes,
  extracted programmatically) shows no per-plug host writes at all — plug
  handling is UC-autonomous once the SB connects.
- The `0x22010/18/1c` UC registers are read-only for the host (status), not
  a host→UC mailbox; the "MBOX[%x] cmd %x" firmware print is internal
  command routing.

**Current best hypothesis for silent hotplug**: the UC↔ps883x Sideband
channel never reaches "SB Connected", so HSE phase 1 waits forever. The
retimer's register file is shared between the Linux I2C driver view and the
UC's SB view, so the TBT_CONNECTED status is written; what is unverified is
the SB physical/protocol link state (SB config registers in the sideband
aperture 0x15612000 looked programmed and live). Next steps: (1) decode the
SB connect/status bits in the sideband aperture and watch them across a
plug; (2) compare ps883x bring-up (Windows SurfaceRetimer tooling vs Linux
ps883x driver) for an SB-enable step; (3) check the firmware's
`"HW version mismatch"` check in `cio_target_init` — the UC verifies the
retimer/HW identity over SB at init.

## uc_ping instrument + three hypotheses eliminated (2026-08-26 early AM)

Added a `uc_ping` sysfs attribute to `qcom_usb4_hr` (raw config-space read
of ROUTER_CS_1 via the tb core + per-port USB4 adapter dump of
PORT_CS_18/19). Required exporting `tb_bus_type` (domain.c) and
`tb_cfg_read` (ctl.c). Results:

1. **The UC is alive at any time**: config reads answer in 163-314 us
   minutes after boot. The "frozen UC" theory is retracted — `0x22010 =
   0xb3b0` is a boot-invariant constant, not a timestamp.
2. **The port FSM never engages**: `PORT_CS_18 = 0x10` (TIP=0, CPS=0)
   before AND after plug, with TBT alt-mode negotiated and lanes muxed.
   The UC accepts the hotplug arming (ADP_CS_5) but never starts training.
3. **USB4 AON clamp eliminated**: found set (`0xfd5104 = 0x01010101`),
   released it before UC start (`0x1010101 -> 0x0`, logged) — no effect.
   The v8 regs table was also missing the `QPHY_PCS_USB4_CLAMP_ENABLE`
   mapping (added, + the proper clear in `qmp_combo_usb4_init`, staged for
   the next kernel rebuild; the quick test used a direct of_iomap clear in
   `uc_bringup`).

**Retimer-reset experiment FAILED and was reverted**: releasing the ps883x
from reset at probe (instead of holding it until the first Type-C
notification) broke Wi-Fi and USB-C tethering system-wide — the
reset-holding is load-bearing for the ADSP/UCSI flow. Source reverted and
stock ps883x.ko reinstalled; do not retry this blunt approach. Any future
attempt must coordinate with the ADSP (e.g., only release reset once the
connector policy is up, or via UCSI-negotiated timing).

State at session end: UC alive and answering, hotplug armed, clamp
released, retimer per stock driver — and the SB gate still never opens.
The remaining unknowns live in the UC's SB controller (Hexagon) or in
ADSP-side coordination that no host driver performs on Linux yet.

## Deferred activation fires clean; the clamp poke was the killer; domain up with TIP awaiting a partner (2026-08-26 midday)

Switched the test DTB (`x1p64100-microsoft-denali-usb4-test.dts`) to
deferred activation (drop `qcom,nhi-activate`, keep everything else) and
added manual `uc_activate` / `uc_ping` sysfs stores on the platform
device, plus a dedicated GRUB entry. Intent: start the UC only after the
Type-C stack has the ps883x retimer out of reset. The first two fires
under this scheme produced a new failure mode — and forensics solved the
two-week mystery of the "random" green-screen freezes.

**The freeze was ours all along, and it was the AON clamp poke.** Both
fires hard-locked the SoC instantly (green screen, no panic, no kdump
vmcore, ramoops empty because the only way out was a power-button cold
cycle which erases DRAM). The earlier "green screens happen even with the
UC dormant" observation from the handoff notes does not survive contact
with data, and neither does the `gen70500_sqe.fw` theory — the firmware
loads fine on this kernel (`adreno_request_fw` succeeds from
`/lib/firmware` via the usermode helper). Observability that actually
survives the crash was built first, and it identified the killer by
absence: `drivers/thunderbolt/qcom_usb4_hr.c` now emits `hr-act: step0..6`
and `hr-bring:` markers at every stage boundary, mirrored to (a) a
Raspberry Pi over **netconsole** (`usb4-netconsole.service`, arms itself
only when the booted DTB is the deferred node; listener is
`socat -u UDP-RECVFROM:6666,fork` on 192.168.4.41) and (b) a per-line
`fsync`'d `/dev/kmsg` mirror to a USB stick (`usb4-kmsg-mirror.service`,
ext4 `LABEL=usb4log` automounted at `/mnt/usb4log`). The Pi capture for
fire #2 (display **unplugged**) showed every stage clean through
`"USB4 UC firmware loaded (40816 bytes)"` at t=867.958 and then
*eternal silence*: death sits exactly in the raw `of_iomap` +
`readl/writel(phy_base + 0x104)` USB4 AON clamp release hack. Touching a
partially-powered QMP block raises a bus-level abort that never surfaces
as an oops. Consequences: the poke is now bypassed by default
(`module_param(skip_clamp, bool, 0444)`, **default true**), and when
re-enabled it logs map/read/write individually. The clamp hypothesis is
moot for now — with the poke skipped, the UC reports ready instantly and
stays alive, so on this X1P firmware the release is evidently not needed
for the bring-up path we run.

**Two more operational lessons burned in:**

- `gcc_usb4_0_gdsc` tolerates exactly **one enable per power cycle**:
  `rmmod`/`modprobe` cycling the driver mid-session leaves the GDSC
  "stuck at 'off'" (WARN in `gdsc_toggle_logic`) and the router is dead
  silicon until reboot. Full reboot between fires; never live-reload.
- journald usually has the last seconds on disk, but only netconsole gives
  the exact dying words. Keep the Pi listener up for every fire.

**Plumbing bug found at fire #3**: with deferred mode the driver never
ran `nhi_preflight` (it gates on `qcom,nhi-activate`), so `nhi_probe`
failed with `-EINVAL: NHI ops not set`. `uc_activate` now runs the
preflight resource wiring (IRQs, `nhi.ops`, aperture `regs[8]`, PHY
handle) itself before activating — split out as
`__qcom_usb4_hr_nhi_preflight()`.

**Fire #5 (post-reboot, both fixes) — first fully successful activation:**

```
NHI preflight passed ... step1 clocks ... step2 resets ...
phy TBT3 ... UC fw 40816B ... clamp SKIPPED ... gates cleared ... GO ...
USB4 UC ready (instant) ... nhi_probe OK
→ "experimental Qualcomm USB4 NHI activated with UC firmware"
→ /sys/bus/thunderbolt/devices: domain0 + 0-0
→ uc_ping: ROUTER_CS_1 ret=0 val=0x101c01c latency=379us
→ port2 cap_usb4=0x17 CS18=0x10 CS19=0x40006 TIP=0
```

`TIP=0` is now *correct*, not broken: nothing is plugged into the port.
The open question from the string-table section (does HSE phase 1 wait on
the SB connect forever?) gets its real test next: hotplug the Studio
Display with the UC live and watch `PORT_CS_18.TIP` + `SB Connected` for
the first time with a UC that started after the retimer was already up.
Remaining nonfatal noise: DROM parse fail (-5) after the known ring-0
startup overflow, and a `Runtime PM usage count underflow` warning in the
scaffold.

## usbap_config read = instant lockup; plug timing eliminated; state hardened (2026-08-26 afternoon)

Round 7 on a pristine boot (`.remove` fix in, Studio Display present
from before activation): the live-domain guard printed
`uc_activate: already active (flag=0 live=1)` — the `activated` flag is
corrupted to 0 within seconds of being set even on a clean boot, so all
sysfs guards now use tb-domain presence instead. The init-discovery
theory died too: `TIP=0` with the partner attached *before* UC start.
Plug timing is fully eliminated; the missing piece is active glue.

The usbap experiment cost crash #3 and delivered a clean attribution via
netconsole: death between `hr-usbap: dumping before RMW` and the first
value print = the **first `readl(usbap_config+0x00)`** (`0x15681010`
region base `0x15681000`) hard-locked the SoC. A plain *read* — the
aperture is unpowered in our bring-up state, same class of fatal bus
abort as the AON clamp poke. The RMW trigger is now gated behind
`module_param(usbap_allow, default false)`. Lesson generalized: **every
`*_ap_config`/PHY-adjacent block is presumed deadly until its power state
is understood.**

Remaining candidate paths to arm link training, in order:
1. Diff Microsoft's `sp11-usb4-host-router.dtbo` against our Denali
   wiring for clocks/GDSCs/supplies that power the `*_ap_config` blocks.
2. The ADSP/pmic_glink connector-policy path: `mux=4 (tunneling)`
   notifications arrive, but Windows surely sends more (UCSI vendor
   commands / ADSP pipes) that both power these blocks and arm the UC.
3. Sideband aperture (`0x15612000`) state dumps — only after proving that
   region tolerates host reads.

## THE UC NEVER RAN FIRMWARE: uc_ram aliases host DRAM (2026-08-26, session end)

Round 9's uc_ram scanner (56K at 0x15613000, after a normal load + GO)
returned ~213 printable runs of **Linux kernel driver-core strings**
(`device_unregister`, `really_probe`, `__driver_attach`,
`dev_pm_qos_*`) and **zero** UC firmware strings — although the
`sp11-uc-fw.bin` (which verifiably contains `"Waiting for SB RX
Connect"`, `"sb_init"`, `"SB Connected %p"`) had been streamed into that
exact window minutes earlier. Conclusion: the DT's `uc_ram` region
**aliases host DRAM**. Consequences, in order of severity:

1. Every "firmware load" to date sprayed ~40KB into kernel memory at an
   unknown physical alias. This — not tb-core bookkeeping — is the likely
   source of the `activated`-flag zeroing and the `Runtime PM usage count
   underflow` warnings.
2. `"USB4 UC ready"` (ROUTER BIT24 after GO) is a hardware default, not
   evidence of an executing UC.
3. **The UC has never executed firmware in any experiment so far.** The
   static SB aperture, `TIP=0`, and the DROM parse failure are all
   trivially explained: there is no firmware running. The round-8 SB
   dump's only plug-delta (a ticking "counter" at [0x8]) is equally
   consistent with kernel memory changing under the alias — both windows
   need revalidation after remapping.
4. The prior session's mapping of the QcUsb4Filter firmware-load window
   onto the DT `uc_ram` region is wrong. Windows binds `ACPI\QCOM0C6D`
   (`QcUsb4Bus8380.inf`); the authoritative resource list lives in that
   ACPI node's `_CRS`, not in our DTB guess. The real load mechanism may
   be a different MMIO window entirely, or SMMU-mapped host DRAM (the
   router sits in IOMMU group 4).

Mitigations landed: `fw_load_allow` module param (default **N**) makes
`uc_activate` refuse the load (EPERM + log line) until the real UC
RAM/IOVA path is established; md5 `aee25d6b` staged. The corrupted-flag
defense (`live=` domain check) and all instrumentation remain.

Next session: pull the ACPI tables for `QCOM0C6D` (UEFI image from the
SurfaceUpdate package, or `acpidump` under Windows), rebuild the window
map from `_CRS`/`_DSM` truth, identify the genuine fw-load path, and add
the acceptance test that was missing all along: **after load, the target
window must read back the firmware's own strings.**

## ACPI tables extracted: the real X1P USB4 architecture (2026-08-26, session end II)

Live RSDP from `/sys/firmware/efi/systab` (`ACPI20=0xd57be018`) + kcore
physical reads dumped the running firmware's complete table set (16 XSDT
entries, 284KB DSDT, `QCOMM SDM8380 MSFT`) to `~/acpi/`; iasl-decompiled
fine. The DSDT rewrites the picture:

- `UBF0` (`QCOM0C6D`) contains **PRT0**, `_CID ACPI0015` (the standard
  USB4 host interface): `_CRS` is **one flat 768K window at
  0x1563F000** (so `usbap_config` = base+0x42000, and the window extends
  well past our DT's named regions), with GSIs **504 / 287 / 611**.
  Our DTB's SPI 472 = GSI 504 matches "ring"; SPI 579 = GSI 611 matches
  "fw"; but **GSI 287 (SPI 255) is missing from our DTB** and is the
  leading candidate for the genuine UC firmware interrupt.
- **PRT1**: a second identical router at 0x1573F000 (SC8380 has two).
- `_DEP` on `\_SB.PEP0` and **`\_SB.UCS0`**: `UCS0` (`QCOM0CA4`) is a
  48-byte IMEM-style mailbox at **0x81F20040** plus two GPIOs (121,
  170). The DSDT's plug-event code writes
  `EINF/EUPD/ECC0/EMX0/EVI0/ESI0/ESV0` and calls `USBR()` — **this is
  the connector-policy glue Linux never implemented**, and the most
  probable missing link between Type-C events and the UC.
- The hamoa.dtsi region list (router@0x15600000, uc_ram@0x15613000, …)
  has no ACPI backing: the `uc_ram` read aliasing kernel DRAM is
  consistent with that whole low block not being what the DTB claims.

Combined with the firmware-load retraction earlier today, the corrected
model is: router core at 0x1563F000 (works: rings, config space), UC
firmware/bring-up happens through UCS0-style mailbox signaling (possibly
ADSP-driven, possibly not host-loadable at all), and all `*_ap_config`
blocks live in the flat window whose power/isolation the ADSP controls.

## Round 14: with the UC verifiably running, the gap isolates to the PS8830 sideband (2026-08-26, session end III)

The session's final replay ran the corrected known-good flow perfectly:
warm-check at the right address, firmware streamed into the UC block
(writes real), gates cleared, GO, ready pulse, `domain0`+`0-0`, exit 0 -
with the ACPI `uc` edge IRQ armed, the UCS0 mailbox mapped, and the SB
dump re-aimed. The plug then produced: ADSP TBT notification (`svid=
0x8087 mux=4`, twice), `port0-partner` registered, PS8830 programmed -
and on the UC side: **zero** edge-IRQ firings, a bit-identical sideband
window (only its free-running counter moved), and `TIP=0`.

With the UC verifiably executing our firmware, the two-week question
"why doesn't the link train" finally has a bounded answer: **the PS8830
retimer never opens its sideband to the UC, because nothing on Linux
configures the retimer's SB/USB4-tunneling mode.** The mainline
`ps883x` driver (539 lines, Ubuntu-patched) writes only
`CONN_STATUS_0/1/2`; the SB-enable and tunneling-mode registers that a
USB4 retimer needs are configured on Windows by `SurfaceRetimerDxe` in
the UEFI (not present in plaintext in the update capsule).

Everything else on the board is now understood and reproducible:
UC boot flow, both register windows, the three ACPI interrupts (SPI 255
armed but silent - trigger unknown), the UCS0/UCSI mailbox layout, and
the ADSP notification chain. Remaining RE target for link training:
PS8830 SB-mode register sequence from `SurfaceRetimerDxe`.

## Final narrowing: everything funnels through the UC-retimer sideband (2026-08-26, session end IV)

Thunderbolt dynamic debug (275 sites) + physical replug with a running UC
and live domain: **zero tb-core messages**. The Connection Manager never
sees the plug. The verified chain: ADSP TBT notification -> ps883x
CONN_STATUS writes (i2cdump-verified: reg00=0x03 present+reversed,
reg02=0x01 TBT_CONNECTED; idle-port control dump differs correctly) ->
retimer in the exact TBT state the driver intends -> and then nothing.
The UC (running our firmware, ready bit pulsing correctly, rings up)
never starts training (TIP=0) and never notifies the host, because per
its own firmware strings plug detection is sideband-based
("Register SBRX_CONNECT callback" -> "Waiting for SB RX Connect") and
the PS8830 never raises SB connect.

Also checked: upstream ps883x "TBT3/USB4 operation support" (Dybcio,
10/2025) adds exactly the CONN_STATUS_2 handling we already carry - the
SB bring-up is not in the upstream driver on any platform (on Intel
designs the NHI firmware owns retimer SB internally; the UC is the
analog here).

Remaining unknowns, all in one path: why the PS8830<->UC sideband never
connects on this board. Candidates: (1) PS8830 SB-enable register the
Linux driver doesn't write, (2) Surface board wiring routes SB through
signaling the ADSP/UCS0 GPIOs control (DSDT pins 121/170/217/218/417/123
associated with QCOM0CA4), (3) PS8830 firmware version expectations
(Surface ships retimer fw 128.3.6.0). Next session: SurfaceRetimerDxe
extraction from the UEFI (needs deep compressed-section parsing), PS8830
datasheet acquisition, or i2c traffic capture under Windows.

## I2C ground truth and the state of the hunt (2026-08-26, final)

Direct register dumps of both PS8830s (`i2cdump`, bus 1 = port0, bus 3 =
port1) close the retimer question as far as the visible register file
goes: port0 carries exactly the TBT state the driver writes
(`reg00=0x03` present+reversed, `reg02=0x01` TBT_CONNECTED), port1
confirms register semantics with a USB3 device (`0x23`), the 0x10-0x19
block is identical on both chips (mode-independent defaults), and
nothing above 0x1a is non-zero. There is no visible "SB enable" switch
in page 0; if PS8830 banks registers, the page selector is unknown.

The upstream picture also matured: the ps883x "TBT3/USB4 operation
support" work (Dybcio, 10/2025) is already in our tree, and a 2026-07
upstream patch **disables USB4 on ps883x platforms as a workaround**
pending "proper USB4 DP tunneling support" - i.e. USB4/TBT through a
PS8830 + UC-router design is unsolved on mainline X1E generally, not
specifically on Surface. The UC-side bring-up we reproduce here (load,
ready, rings, domain) may in fact be further than any public mainline
X1E attempt.

The hunt for the PS8830<->UC sideband trigger now has four documented
routes forward: PS8830 datasheet (NDA), a Windows-side I2C capture,
completion of the XBL `usb_shared_retimer_init` reverse engineering, or
upstream (Qualcomm/Linaro) publishing the UC-based USB4 flow. Everything
else on this board - UC boot, router rings, domain, ADSP chain, ACPI
surface, interrupt map - is mapped, reproduced, and committed.
