# Reproducing the installed kernel state (as of 2026-08-27)

The complete installed system state is snapshotted bit-exact in
`~/Documents/testing/installed-kernel-snapshot-20260827/` (with
SHA256SUMS, also mirrored to /boot/installed-snapshot-SHA256SUMS-20260827):

  vmlinuz-7.2.0-usb4-hr-x1p-test          the boot kernel (Aug 25 build)
  initrd.img-7.2.0-usb4-hr-x1p-test       initramfs (pre-gpu-fw-fix vintage;
                                          contains the OLD ps883x.ko/.zst and
                                          qcom modules are NOT in it - they
                                          load from rootfs)
  sp11-x1p-usb4-hr-activation.dtb         DTB: 3 interrupts + ucs0-mailbox
                                          reserved-memory
  qcom_usb4_hr.ko, thunderbolt.ko,        rootfs modules (current tree)
  ps883x.ko                               (NOTE: boot-time ps883x comes from
                                          the initrd copy instead)
  sp11-usb4-uc-fw.bin                     the UC firmware (from QcUsb4Filter)

## Guaranteed path (bit-exact)
Copy the snapshot files back to /boot and
/lib/modules/7.2.0-usb4-hr-x1p-test/kernel/... respectively, verify
against SHA256SUMS, boot the "sp11 usb4-hr activation" GRUB entry
(cmdline includes: rootdelay=45 clk_ignore_unused pd_ignore_unused
efi=novamap ignore_loglevel pstore.backend=ramoops crashkernel=2G-4G:320M,4G-32G:512M,32G-64G:1024M,64G-128G:2048M
arm64.nopauth systemd.tpm2_wait=0 - see /etc/grub.d/81_sp11_usb4_hr_activation).

## Source path (functionally identical)
- Repo at commit 804ca00cd40c (branch sp11/usb4-hr-x1p-experiment).
- vmlinuz: `make Image` at commit 0ff15dcef410 (pre-session state; the
  installed kernel predates this session's built-in-driver change, and
  qcom_usb4_hr is =m so it is not in the Image at all).
- DTB: `make qcom/x1p64100-microsoft-denali-usb4-test.dtb` at HEAD.
- Modules: `make M=drivers/thunderbolt modules` and
  `make M=drivers/usb/typec/mux modules` at HEAD (ps883x carries the
  benched reject_tbt param; harmless, default N).
- UC firmware: from the Windows payload
  (windows-driver/extracted/SurfaceUpdate/qcusb4filter/..) via the
  documented segmentation, or copy the snapshotted file.
- initrd: NOT cleanly regenerable (initramfs-tools rebuild broke boot
  once - see klog). Restore the preserved file.

## Runtime parameters after boot (all default-N benched experiments)
  /sys/module/qcom_usb4_hr/parameters/{fw_load_allow,arm_int,usbap_allow,skip_clamp,window0}
  /sys/module/ps883x/parameters/reject_tbt
  /sys/module/qcom_pmic_glink_altmode/parameters/{force_dp,nack_on_tbt_reject}
  (qcom_pmic_glink_altmode params only exist on the force_dp kernel in
   vmlinuz...bak-forcedp; the installed vmlinuz predates that patch)
