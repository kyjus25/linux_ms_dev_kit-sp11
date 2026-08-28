// SPDX-License-Identifier: GPL-2.0
/*
 * Microsoft Surface G6 IPTS compatibility for Denali systems.
 */

#include <linux/device.h>
#include <linux/g6ts_heat.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>

#include "mshw0485_denali.h"

#define G6TS_RAW_SIDEBAND_REPORT_07	0x07
#define G6TS_RAW_HEAT_REPORT_0B		0x0b
#define G6TS_RAW_HEAT_REPORT_0C		0x0c
#define G6TS_RAW_HEAT_REPORT_0D		0x0d
#define G6TS_RAW_HEAT_REPORT_1A		0x1a
#define G6TS_RAW_SIDEBAND_REPORT_6E	0x6e
#define G6TS_SP11_VENDOR_ID		0x045eU
#define G6TS_SP11_X1E_PRODUCT_ID	0x0c83U
#define G6TS_SP11_X1P_PRODUCT_ID	0x0c80U
#define G6TS_SP11_VERSION_ID		0x0004U

static bool g6ts_ipts_shim = true;
module_param_named(ipts_shim, g6ts_ipts_shim, bool, 0444);
MODULE_PARM_DESC(ipts_shim, "Expose the IPTS-compatible hidraw shim device");

static bool host_fault_recovery = true;
module_param(host_fault_recovery, bool, 0444);
MODULE_PARM_DESC(host_fault_recovery,
		 "Recover with a cold re-enumeration after an IRQ transport/protocol fault");

static bool ready_quiesce = true;
module_param(ready_quiesce, bool, 0444);
MODULE_PARM_DESC(ready_quiesce,
		 "Ignore one invalid trailing header after the ready GPIO deasserts");

static unsigned int g6ts_ipts_contact_on_energy = 3200000;
module_param_named(ipts_contact_on_energy, g6ts_ipts_contact_on_energy,
		   uint, 0444);
MODULE_PARM_DESC(ipts_contact_on_energy,
		 "Pressure antenna energy that opens contact (P4-P8 corpus)");

static unsigned int g6ts_ipts_contact_off_energy = 1200000;
module_param_named(ipts_contact_off_energy, g6ts_ipts_contact_off_energy,
		   uint, 0444);
MODULE_PARM_DESC(ipts_contact_off_energy,
		 "Pressure antenna energy that closes contact after two low cycles");

static unsigned int g6ts_ipts_stale_ms = 1000;
module_param_named(ipts_stale_ms, g6ts_ipts_stale_ms, uint, 0444);
MODULE_PARM_DESC(ipts_stale_ms,
		 "Emit a stylus lift after this many ms without a complete cycle");

/*
 * Fabricated IPTS report descriptor.  The stock iptsd recognizes a device by
 * exact predicates: a one-byte feature report with vendor usage 0xff00:0xc8
 * (mode setting), an input report carrying digitizer usages 0x0d:0x56 (scan
 * time) and 0x0d:0x61 (gesture data), a feature report with usage 0x0d:0x63
 * (metadata), and exactly one of a touchscreen (0x0d:0x04) or touchpad
 * (0x0d:0x05) application collection.  The gesture data field is oversized so
 * it also defines the read buffer iptsd allocates; every injected report is
 * padded to the full declared length because the HID core rejects input
 * events shorter than the declared report size.
 */
static const u8 g6ts_ipts_report_descriptor[] = {
	0x05, 0x0d,		/* Usage Page (Digitizers) */
	0x09, 0x04,		/* Usage (Touchscreen) */
	0xa1, 0x01,		/* Collection (Application) */
	0x85, 0x01,		/*   Report ID (1): touch data */
	0x09, 0x56,		/*   Usage (Scan Time) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x26, 0xff, 0xff,	/*   Logical Maximum (65535) */
	0x75, 0x10,		/*   Report Size (16) */
	0x95, 0x01,		/*   Report Count (1) */
	0x81, 0x02,		/*   Input (Data,Var,Abs) */
	0x09, 0x61,		/*   Usage (Gesture Data) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255) */
	0x75, 0x08,		/*   Report Size (8) */
	0x96, 0xdc, 0x05,	/*   Report Count (1500) */
	0x81, 0x02,		/*   Input (Data,Var,Abs) */
	0x06, 0x00, 0xff,	/*   Usage Page (Vendor Defined 0xff00) */
	0x09, 0xc8,		/*   Usage (0xc8) */
	0x85, 0x02,		/*   Report ID (2): mode setting */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x25, 0x01,		/*   Logical Maximum (1) */
	0x75, 0x08,		/*   Report Size (8) */
	0x95, 0x01,		/*   Report Count (1) */
	0xb1, 0x02,		/*   Feature (Data,Var,Abs) */
	0x05, 0x0d,		/*   Usage Page (Digitizers) */
	0x09, 0x63,		/*   Usage (0x63) */
	0x85, 0x03,		/*   Report ID (3): metadata */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255) */
	0x75, 0x08,		/*   Report Size (8) */
	0x96, 0x70, 0x00,	/*   Report Count (112) */
	0xb1, 0x02,		/*   Feature (Data,Var,Abs) */
	0xc0,			/* End Collection */
};

static_assert(sizeof(g6ts_ipts_report_descriptor) ==
	      MSHW0485_DENALI_REPORT_DESCRIPTOR_SIZE);

/*
 * Metadata feature report content served for report ID 3: a hid::Frame of
 * type 0x02 (metadata) wrapping the 105-byte metadata frame that stock iptsd
 * parses.  Rows/columns are the HEAT antenna grid; width/height are the G6
 * HIMETRIC extents (100 units/mm).  The identity transform keeps the solver
 * axes aligned with the panel.
 */
static const u8 g6ts_ipts_metadata_feature[113] = {
	0x03,			/* report ID */
	0x70, 0x00, 0x00, 0x00,	/* hid::Frame size = 112 */
	0x00,			/* hid::Frame reserved */
	0x02,			/* hid::Frame type = metadata */
	0x00,			/* hid::Frame reserved */
	0x2e, 0x00, 0x00, 0x00,	/* rows = 46 */
	0x44, 0x00, 0x00, 0x00,	/* columns = 68 */
	0xfc, 0x6a, 0x00, 0x00,	/* width = 27388 (mm * 100) */
	0x52, 0x47, 0x00, 0x00,	/* height = 18258 (mm * 100) */
	0x01,			/* unknown_byte */
	0x00, 0x00, 0x80, 0x3f,	/* transform xx = 1.0 */
	0x00, 0x00, 0x00, 0x00,	/* transform yx = 0 */
	0x00, 0x00, 0x00, 0x00,	/* transform tx = 0 */
	0x00, 0x00, 0x00, 0x00,	/* transform xy = 0 */
	0x00, 0x00, 0x80, 0x3f,	/* transform yy = 1.0 */
	0x00, 0x00, 0x00, 0x00,	/* transform ty = 0 */
	/* unknown[16] floats remain zero */
};

#define G6TS_IPTS_REPORT_ID_TOUCH	0x01
#define G6TS_IPTS_REPORT_ID_MODE	0x02
#define G6TS_IPTS_REPORT_ID_METADATA	0x03

/* Wire length of every injected report: report ID plus all declared fields. */
#define G6TS_IPTS_REPORT_LEN		(1 + 2 + 1500)

#define G6TS_IPTS_MAX_VECTORS		16
#define G6TS_IPTS_VECTOR_LEN		48
#define G6TS_IPTS_COMPONENTS		9
#define G6TS_IPTS_MIN_POSITION_AMP	64

/* IPTS report frame and DFT window type codes consumed by stock iptsd. */
#define G6TS_IPTS_FRAME_REPORTS		0xff
#define G6TS_IPTS_FRAME_METADATA	0x02
#define G6TS_IPTS_DFT_METADATA		0x5f
#define G6TS_IPTS_DFT_WINDOW		0x5c
#define G6TS_IPTS_DFT_POSITION		0x06
#define G6TS_IPTS_DFT_PRESSURE		0x0b
#define G6TS_IPTS_WINDOW_SEQUENCE	1

#define G6TS_IPTS_CYCLE_WINDOW_NS	30000000ull
/* Pressure rows consumed by the stock pressure estimator. */
#define G6TS_IPTS_PRESSURE_ROWS		6

/* One HEAT antenna vector, laid out identically to the IPTS DFT row. */
struct mshw0485_denali_vector {
	u32 frequency;
	u32 magnitude;
	s16 real[G6TS_IPTS_COMPONENTS];
	s16 imag[G6TS_IPTS_COMPONENTS];
	s8 first;
	s8 last;
	s8 mid;
	s8 zero;
};

struct mshw0485_denali_banks {
	u8 count;
	struct mshw0485_denali_vector x[G6TS_IPTS_MAX_VECTORS];
	struct mshw0485_denali_vector y[G6TS_IPTS_MAX_VECTORS];
};

struct mshw0485_denali_part {
	bool present;
	u16 len;
	u8 content[G6TS_HEAT_MAX_CONTENT_SIZE];
};

struct mshw0485_denali {
	struct device *parent;
	/* Serializes HID lifetime, cycle assembly, and stale-lift work. */
	struct mutex lock;
	struct hid_device *hid;
	bool ready;
	u8 *inject_buf;
	struct delayed_work stale_work;
	u8 mode;

	/* HEAT cycle bundler state (report 0x0c anchors the cycle). */
	struct mshw0485_denali_part parts[5];
	u64 first_ts_ns;

	/* Contact detector state. */
	bool contact;
	u8 off_run;

	/* Cycle scratch. */
	struct mshw0485_denali_banks position;
	struct mshw0485_denali_banks pressure;
	u8 detection[16];
	bool has_position;
	bool has_pressure;
	bool has_detection;

	u32 group_counter;
	u64 last_cycle_ns;
	u64 cycles;
	u64 lifts;
	u64 incomplete_cycles;
	u64 unanchored_records;
	u64 sideband_records;
	u64 dropped_cycles;
	u64 position_found;
	u64 pressure_found;
	u64 inject_errors;
};

/*
 * IPTS compatibility shim: a second HID device whose report descriptor is a
 * fabricated, minimal IPTS touchscreen.  Complete HEAT cycles are bundled
 * here with the semantics of the g6-pen processor (ADR0059/ADR0060: report
 * 0x0c anchors, six observed orderings, 30 ms window, opaque sideband) and
 * serialized into IPTS DftMetadata + DftWindow report frames, so the stock,
 * unmodified iptsd can consume the G6 digitizer through its ordinary hidraw
 * path.  Contact gating reuses the capture-derived pressure antenna energy
 * detector with the 0x62 detection veto.
 */

static void g6ts_ipts_parse_vector(struct mshw0485_denali_vector *v,
				   const u8 *entry)
{
	u8 i;

	v->frequency = get_unaligned_le32(entry);
	v->magnitude = get_unaligned_le32(entry + 4);
	for (i = 0; i < G6TS_IPTS_COMPONENTS; i++) {
		v->real[i] = (s16)get_unaligned_le16(entry + 8 + i * 2);
		v->imag[i] = (s16)get_unaligned_le16(entry + 26 + i * 2);
	}
	v->first = (s8)entry[44];
	v->last = (s8)entry[45];
	v->mid = (s8)entry[46];
	v->zero = (s8)entry[47];
}

/* Stable frequency order: the pressure estimator reads bins in order. */
static void g6ts_ipts_sort_vectors(struct mshw0485_denali_vector *v, u8 n)
{
	u8 i;

	for (i = 1; i < n; i++) {
		struct mshw0485_denali_vector key = v[i];
		s8 j = i - 1;

		while (j >= 0 && v[j].frequency > key.frequency) {
			v[j + 1] = v[j];
			j--;
		}
		v[j + 1] = key;
	}
}

/* Validate the fixed nested-record section of a HEAT part. */
static bool g6ts_ipts_section(const struct mshw0485_denali_part *part, size_t *end)
{
	u32 section_length;

	if (!part->present || part->len < 17)
		return false;

	section_length = get_unaligned_le32(part->content + 9);
	if (section_length > (u32)part->len - 9 - 4)
		return false;
	if (get_unaligned_le16(part->content + 13) != 0xff00 ||
	    part->content[15] != 0)
		return false;

	*end = 9 + 4 + section_length;
	return true;
}

static bool g6ts_ipts_fill_banks(const u8 *payload, u16 payload_len,
				 struct mshw0485_denali_banks *banks)
{
	u8 count = payload[4];
	u8 bank, vector;

	if (!count || count > G6TS_IPTS_MAX_VECTORS)
		return false;
	if (payload[6] != 1 || payload[7] != 1 || payload[9] > 15)
		return false;
	if (payload_len != 12 + (u16)count * 2 * G6TS_IPTS_VECTOR_LEN)
		return false;

	banks->count = count;
	for (bank = 0; bank < 2; bank++) {
		for (vector = 0; vector < count; vector++) {
			g6ts_ipts_parse_vector(bank == 0 ? &banks->x[vector] :
						       &banks->y[vector],
					       payload + 12 +
					       (bank * count + vector) *
					       G6TS_IPTS_VECTOR_LEN);
		}
	}
	return true;
}

/*
 * Position banks: the first region-1, channel-6, 8-vector record of the 0x0c
 * part, kept in firmware slot order (slot 0 position transmitter, slot 1
 * tilt transmitter, as expected by the stock DFT solver).
 */
static void g6ts_ipts_scan_position(struct mshw0485_denali *ip)
{
	const struct mshw0485_denali_part *part = &ip->parts[0];
	size_t end, pos;

	ip->has_position = false;
	if (!g6ts_ipts_section(part, &end))
		return;

	pos = 16;
	while (pos + 4 <= end) {
		const u8 *rec = part->content + pos;
		u16 payload_len = get_unaligned_le16(rec + 2);
		size_t next = pos + 4 + payload_len;

		if (next > end)
			break;

		if (rec[0] == 0x5c && !rec[1] && payload_len >= 12 &&
		    rec[4 + 4] == 8 && rec[4 + 5] == 1 && rec[4 + 9] == 6 &&
		    g6ts_ipts_fill_banks(rec + 4, payload_len, &ip->position)) {
			ip->has_position = true;
			return;
		}

		pos = next;
	}
}

/*
 * Pressure banks: region 4 (channel 7) of the first 0x0b part, reordered
 * into frequency-bin order, plus the 0x62 detection record that follows the
 * pressure window (byte 11 vetoes contact on ~90% of hover cycles).
 */
static void g6ts_ipts_scan_pressure(struct mshw0485_denali *ip)
{
	const struct mshw0485_denali_part *part = &ip->parts[1];
	size_t end, pos;

	ip->has_pressure = false;
	ip->has_detection = false;
	if (!g6ts_ipts_section(part, &end))
		return;

	pos = 16;
	while (pos + 4 <= end) {
		const u8 *rec = part->content + pos;
		u16 payload_len = get_unaligned_le16(rec + 2);
		size_t next = pos + 4 + payload_len;

		if (next > end)
			break;

		if (rec[0] == 0x5c && !rec[1] && payload_len >= 12) {
			const u8 *payload = rec + 4;

			if (payload[4] >= G6TS_IPTS_PRESSURE_ROWS &&
			    payload[5] == 4 && payload[9] == 7 &&
			    g6ts_ipts_fill_banks(payload, payload_len,
						 &ip->pressure)) {
				size_t det = next;

				g6ts_ipts_sort_vectors(ip->pressure.x,
						       ip->pressure.count);
				g6ts_ipts_sort_vectors(ip->pressure.y,
						       ip->pressure.count);
				ip->has_pressure = true;

				while (det + 4 <= end) {
					const u8 *d = part->content + det;
					u16 dlen = get_unaligned_le16(d + 2);
					size_t dnext = det + 4 + dlen;

					if (dnext > end)
						break;
					if (d[0] == 0x62 && dlen == 16) {
						memcpy(ip->detection, d + 4, 16);
						ip->has_detection = true;
						break;
					}
					det = dnext;
				}
				return;
			}
		}

		pos = next;
	}
}

/*
 * Rotate one antenna row onto the phase of its center component using 15-bit
 * fixed point, so the center component becomes (A, 0) with A the saturated
 * center amplitude.  The stock DFT solver raises the two neighbor
 * projections to a negative exponent.  A negative projection produces NaN
 * and a zero projection produces infinity; either is misread as a stylus
 * lift.  Keep non-empty neighbors strictly positive after rotation.  Also
 * raise a valid center to the stock iptsd amplitude floor while preserving
 * its phase and the component ratios.  Truly empty neighbors stay zero so
 * iptsd can retain its off-screen edge handling.
 */
static void g6ts_ipts_normalize_row(struct mshw0485_denali_vector *v)
{
	s32 re_c = v->real[4];
	s32 im_c = v->imag[4];
	u64 norm2 = (u64)((s64)re_c * re_c) + (u64)((s64)im_c * im_c);
	u64 norm2sq;
	u32 norm;
	u32 a;
	s64 cq, sq;
	int i;

	if (!norm2)
		return;

	norm = int_sqrt(norm2);
	norm2sq = (u64)norm * norm;
	a = clamp_t(u32, norm, G6TS_IPTS_MIN_POSITION_AMP, S16_MAX);
	cq = (s64)re_c * a * 32768 / (s64)norm2sq;
	sq = (s64)im_c * a * 32768 / (s64)norm2sq;

	for (i = 0; i < G6TS_IPTS_COMPONENTS; i++) {
		bool empty = !v->real[i] && !v->imag[i];
		s64 re, im;

		if (i == 4) {
			v->real[4] = (s16)a;
			v->imag[4] = 0;
			continue;
		}

		re = ((s64)v->real[i] * cq + (s64)v->imag[i] * sq +
		      (1 << 14)) >> 15;
		im = ((s64)v->imag[i] * cq - (s64)v->real[i] * sq +
		      (1 << 14)) >> 15;

		if (re < 1)
			re = empty ? 0 : 1;
		else if (re > S16_MAX)
			re = S16_MAX;
		if (im < S16_MIN)
			im = S16_MIN;
		else if (im > S16_MAX)
			im = S16_MAX;

		v->real[i] = (s16)re;
		v->imag[i] = (s16)im;
	}
}

static size_t g6ts_ipts_append_window(u8 *out, size_t at, u8 dft_type,
				      u32 ts_ms,
				      const struct mshw0485_denali_banks *banks,
				      u8 rows, bool zero_magnitude,
				      bool normalize)
{
	size_t payload_at = at + 4;
	u8 bank, i, j;

	out[at] = G6TS_IPTS_DFT_WINDOW;
	out[at + 1] = 0;
	/* Report frame payload size is filled in below. */
	at += 4;

	put_unaligned_le32(ts_ms, out + at);
	at += 4;
	out[at++] = rows;
	out[at++] = G6TS_IPTS_WINDOW_SEQUENCE;
	memset(out + at, 0, 3);
	at += 3;
	out[at++] = dft_type;
	memset(out + at, 0, 2);
	at += 2;

	for (bank = 0; bank < 2; bank++) {
		for (i = 0; i < rows; i++) {
			struct mshw0485_denali_vector row = {};

			/* NULL banks serializes all-zero rows (lift path). */
			if (banks && i < banks->count) {
				const struct mshw0485_denali_vector *vectors =
					bank == 0 ? banks->x : banks->y;

				row = vectors[i];
			}
			/* Only slots 0/1 feed the stock position/tilt solver. */
			if (normalize && i < 2)
				g6ts_ipts_normalize_row(&row);
			if (zero_magnitude)
				row.magnitude = 0;

			put_unaligned_le32(row.frequency, out + at);
			put_unaligned_le32(row.magnitude, out + at + 4);
			for (j = 0; j < G6TS_IPTS_COMPONENTS; j++) {
				put_unaligned_le16((u16)row.real[j],
						   out + at + 8 + j * 2);
				put_unaligned_le16((u16)row.imag[j],
						   out + at + 26 + j * 2);
			}
			out[at + 44] = (u8)row.first;
			out[at + 45] = (u8)row.last;
			out[at + 46] = (u8)row.mid;
			out[at + 47] = (u8)row.zero;
			at += G6TS_IPTS_VECTOR_LEN;
		}
	}

	put_unaligned_le16(at - payload_at, out + payload_at - 2);
	return at;
}

static void g6ts_ipts_inject(struct mshw0485_denali *ip, size_t len)
{
	int ret;

	if (!ip->ready || !ip->inject_buf || len > G6TS_IPTS_REPORT_LEN)
		return;

	/*
	 * Pad to the declared report length; the HID core rejects shorter
	 * input events, and iptsd's parser ignores the trailing zeros.
	 */
	memset(ip->inject_buf + len, 0, G6TS_IPTS_REPORT_LEN - len);
	ret = hid_input_report(ip->hid, HID_INPUT_REPORT, ip->inject_buf,
			       G6TS_IPTS_REPORT_LEN, 1);
	if (ret)
		ip->inject_errors++;
}

static void g6ts_ipts_emit_cycle(struct mshw0485_denali *ip)
{
	u32 energy = 0, ts_ms;
	bool veto = false;
	size_t at = 0, frame_at;
	u8 *out = ip->inject_buf;

	/*
	 * Contact detector: pressure antenna maximum energy plus the 0x62
	 * detection veto, entered immediately and left on a veto or two
	 * consecutive low-energy cycles (P4-P8 corpus thresholds).
	 */
	if (ip->has_pressure) {
		const struct mshw0485_denali_banks *p = &ip->pressure;
		u8 i;

		for (i = 0; i < p->count && i < G6TS_IPTS_MAX_VECTORS; i++) {
			if (p->x[i].magnitude > energy)
				energy = p->x[i].magnitude;
			if (p->y[i].magnitude > energy)
				energy = p->y[i].magnitude;
		}
		veto = ip->has_detection && ip->detection[11] != 0;
	}
	if (!ip->contact) {
		if (!veto && energy >= g6ts_ipts_contact_on_energy)
			ip->contact = true;
	} else if (veto) {
		ip->contact = false;
		ip->off_run = 0;
	} else if (energy <= g6ts_ipts_contact_off_energy) {
		if (++ip->off_run >= 2) {
			ip->contact = false;
			ip->off_run = 0;
		}
	} else {
		ip->off_run = 0;
	}

	if (!ip->has_position) {
		ip->dropped_cycles++;
		return;
	}

	ts_ms = (u32)min_t(u64, ktime_get_ns() / NSEC_PER_MSEC, U32_MAX);

	/*
	 * Wire layout: report ID plus the 2-byte ReportHeader timestamp;
	 * the iptsd parser skips exactly these three bytes before reading
	 * the HID frame.
	 */
	out[at++] = G6TS_IPTS_REPORT_ID_TOUCH;
	memset(out + at, 0, 2);
	at += 2;

	frame_at = at;
	at += 7; /* hid::Frame header, size filled in below. */
	out[frame_at + 5] = G6TS_IPTS_FRAME_REPORTS;

	/* DftMetadata binds the group counter to the window sequence. */
	out[at++] = G6TS_IPTS_DFT_METADATA;
	out[at++] = 0;
	put_unaligned_le16(16, out + at);
	at += 2;
	put_unaligned_le32(ip->group_counter, out + at);
	at += 4;
	out[at++] = G6TS_IPTS_WINDOW_SEQUENCE;
	out[at++] = G6TS_IPTS_DFT_POSITION;
	memset(out + at, 0, 10);
	at += 10;

	/*
	 * Pressure window first: updating contact before the position window
	 * makes the contact state observable in this cycle's emitted sample.
	 * Zero magnitudes keep contact released while the window stays
	 * present; the stock pressure estimator ignores them.
	 */
	if (ip->has_pressure)
		at = g6ts_ipts_append_window(out, at, G6TS_IPTS_DFT_PRESSURE,
					     ts_ms, &ip->pressure,
					     G6TS_IPTS_PRESSURE_ROWS,
					     !ip->contact, false);

	at = g6ts_ipts_append_window(out, at, G6TS_IPTS_DFT_POSITION, ts_ms,
				     &ip->position, ip->position.count, false,
				     true);

	put_unaligned_le32(at - frame_at, out + frame_at);

	g6ts_ipts_inject(ip, at);
	ip->cycles++;
	ip->group_counter++;
	ip->last_cycle_ns = ktime_get_ns();
	schedule_delayed_work(&ip->stale_work,
			      msecs_to_jiffies(g6ts_ipts_stale_ms));
}

/*
 * Serialize a zero-magnitude position window, forcing the stock solver to
 * lift the stylus.  Used on transport boundaries and when HEAT cycles stop
 * arriving while the stylus is still reported as in proximity.
 */
static void g6ts_ipts_emit_lift(struct mshw0485_denali *ip)
{
	size_t at = 0, frame_at;
	u32 ts_ms;
	u8 *out = ip->inject_buf;

	ts_ms = (u32)min_t(u64, ktime_get_ns() / NSEC_PER_MSEC, U32_MAX);

	out[at++] = G6TS_IPTS_REPORT_ID_TOUCH;
	memset(out + at, 0, 2);
	at += 2;

	frame_at = at;
	at += 7;
	out[frame_at + 5] = G6TS_IPTS_FRAME_REPORTS;

	out[at++] = G6TS_IPTS_DFT_METADATA;
	out[at++] = 0;
	put_unaligned_le16(16, out + at);
	at += 2;
	memset(out + at, 0, 4);
	at += 4;
	out[at++] = G6TS_IPTS_WINDOW_SEQUENCE;
	out[at++] = G6TS_IPTS_DFT_POSITION;
	memset(out + at, 0, 10);
	at += 10;

	at = g6ts_ipts_append_window(out, at, G6TS_IPTS_DFT_POSITION, ts_ms,
				     NULL, 8, true, true);

	put_unaligned_le32(at - frame_at, out + frame_at);

	g6ts_ipts_inject(ip, at);
	ip->lifts++;
}

static void g6ts_ipts_clear_cycle(struct mshw0485_denali *ip, bool incomplete)
{
	u8 i;

	if (incomplete) {
		for (i = 0; i < 5; i++) {
			if (ip->parts[i].present) {
				ip->incomplete_cycles++;
				break;
			}
		}
	}
	memset(ip->parts, 0, sizeof(ip->parts));
	ip->first_ts_ns = 0;
}

static void g6ts_ipts_process_cycle(struct mshw0485_denali *ip)
{
	g6ts_ipts_scan_position(ip);
	g6ts_ipts_scan_pressure(ip);
	if (ip->has_position)
		ip->position_found++;
	if (ip->has_pressure)
		ip->pressure_found++;
	g6ts_ipts_emit_cycle(ip);
}

/*
 * Bundle one raw HEAT record into the pending cycle.  The bundler is fed
 * synchronously from the IRQ thread, so records cannot be lost between the
 * transport and here; resets, suspends, and transport faults arrive through
 * g6ts_ipts_boundary() instead of sequence or generation bookkeeping.
 */
static void g6ts_ipts_feed_locked(struct mshw0485_denali *ip, u8 report_id,
				  const u8 *content, u16 content_len)
{
	s8 index = -1;
	u64 now;
	u8 i;

	if (!ip || !ip->ready || !ip->inject_buf)
		return;

	now = ktime_get_ns();

	switch (report_id) {
	case G6TS_RAW_SIDEBAND_REPORT_07:
	case G6TS_RAW_SIDEBAND_REPORT_6E:
		ip->sideband_records++;
		return;
	case G6TS_RAW_HEAT_REPORT_0C:
		index = 0;
		break;
	case G6TS_RAW_HEAT_REPORT_0B:
		index = ip->parts[1].present ? 4 : 1;
		break;
	case G6TS_RAW_HEAT_REPORT_0D:
		index = 2;
		break;
	case G6TS_RAW_HEAT_REPORT_1A:
		index = 3;
		break;
	default:
		return;
	}

	/* Discard pending cycles that outran the 30 ms window. */
	for (i = 0; i < 5; i++) {
		if (ip->parts[i].present && ip->first_ts_ns &&
		    now - ip->first_ts_ns > G6TS_IPTS_CYCLE_WINDOW_NS) {
			g6ts_ipts_clear_cycle(ip, true);
			break;
		}
	}

	if (report_id == G6TS_RAW_HEAT_REPORT_0C) {
		for (i = 0; i < 5; i++) {
			if (ip->parts[i].present) {
				g6ts_ipts_clear_cycle(ip, true);
				break;
			}
		}
	} else if (!ip->parts[0].present) {
		ip->unanchored_records++;
		return;
	}

	if (ip->parts[index].present) {
		g6ts_ipts_clear_cycle(ip, true);
		ip->unanchored_records++;
		return;
	}

	if (content_len > G6TS_HEAT_MAX_CONTENT_SIZE)
		return;

	ip->parts[index].present = true;
	ip->parts[index].len = content_len;
	memcpy(ip->parts[index].content, content, content_len);
	if (report_id == G6TS_RAW_HEAT_REPORT_0C)
		ip->first_ts_ns = now;

	for (i = 0; i < 5; i++) {
		if (!ip->parts[i].present)
			return;
	}

	g6ts_ipts_process_cycle(ip);
	g6ts_ipts_clear_cycle(ip, false);
}

static void g6ts_ipts_boundary_locked(struct mshw0485_denali *ip)
{
	if (!ip->ready)
		return;

	/* A boundary can interrupt a cycle at any point: lift immediately. */
	g6ts_ipts_clear_cycle(ip, true);
	g6ts_ipts_emit_lift(ip);
}

static void g6ts_ipts_stale_work(struct work_struct *work)
{
	struct delayed_work *delayed = to_delayed_work(work);
	struct mshw0485_denali *ip = container_of(delayed, struct mshw0485_denali,
					    stale_work);

	mutex_lock(&ip->lock);
	if (ip->last_cycle_ns &&
	    ktime_get_ns() - ip->last_cycle_ns >
	    (u64)g6ts_ipts_stale_ms * NSEC_PER_MSEC)
		g6ts_ipts_emit_lift(ip);
	mutex_unlock(&ip->lock);
}

/*
 * Minimal HID low-level transport for the shim: feature reports are served
 * from driver memory (metadata, mode) and input reports are injected
 * directly from the cycle bundler.
 */
static int g6ts_ipts_hid_start(struct hid_device *hdev)
{
	return 0;
}

static void g6ts_ipts_hid_stop(struct hid_device *hdev)
{
}

static int g6ts_ipts_hid_open(struct hid_device *hdev)
{
	return 0;
}

static void g6ts_ipts_hid_close(struct hid_device *hdev)
{
}

static int g6ts_ipts_hid_parse(struct hid_device *hdev)
{
	int ret;

	ret = hid_parse_report(hdev, g6ts_ipts_report_descriptor,
			       sizeof(g6ts_ipts_report_descriptor));
	if (ret)
		return ret;

	/* hid_parse_report only fills dev_rdesc; hidraw serves rdesc. */
	hdev->rdesc = hdev->dev_rdesc;
	hdev->rsize = hdev->dev_rsize;
	return 0;
}

static int g6ts_ipts_hid_raw_request(struct hid_device *hdev,
				     unsigned char reportnum, u8 *buf,
				     size_t len, unsigned char rtype,
				     int reqtype)
{
	struct mshw0485_denali *ip = hid_get_drvdata(hdev);

	if (rtype != HID_FEATURE_REPORT)
		return -ENOSYS;

	if (reqtype == HID_REQ_GET_REPORT &&
	    reportnum == G6TS_IPTS_REPORT_ID_METADATA) {
		if (len > sizeof(g6ts_ipts_metadata_feature))
			len = sizeof(g6ts_ipts_metadata_feature);
		memcpy(buf, g6ts_ipts_metadata_feature, len);
		return len;
	}

	if (reportnum != G6TS_IPTS_REPORT_ID_MODE)
		return -ENOSYS;

	if (reqtype == HID_REQ_GET_REPORT) {
		if (len > 2)
			len = 2;
		buf[0] = G6TS_IPTS_REPORT_ID_MODE;
		if (len == 2)
			buf[1] = ip->mode;
		return len;
	}

	if (reqtype == HID_REQ_SET_REPORT) {
		/*
		 * Streaming ownership stays with the panel driver; the mode
		 * byte is only bookkeeping for the feature readback.
		 */
		if (len >= 2)
			ip->mode = buf[1];
		return len;
	}

	return -ENOSYS;
}

static const struct hid_ll_driver g6ts_ipts_hid_ll_driver = {
	.start = g6ts_ipts_hid_start,
	.stop = g6ts_ipts_hid_stop,
	.open = g6ts_ipts_hid_open,
	.close = g6ts_ipts_hid_close,
	.parse = g6ts_ipts_hid_parse,
	.raw_request = g6ts_ipts_hid_raw_request,
	.max_buffer_size = G6TS_IPTS_REPORT_LEN,
};

bool mshw0485_denali_enabled(void)
{
	return g6ts_ipts_shim;
}

bool mshw0485_denali_host_fault_recovery(void)
{
	return host_fault_recovery;
}

bool mshw0485_denali_ready_quiesce(void)
{
	return ready_quiesce;
}

struct mshw0485_denali *mshw0485_denali_create(struct device *parent)
{
	struct mshw0485_denali *ip;

	ip = kzalloc_obj(*ip);
	if (!ip)
		return ERR_PTR(-ENOMEM);
	ip->inject_buf = kmalloc(G6TS_IPTS_REPORT_LEN, GFP_KERNEL);
	if (!ip->inject_buf) {
		kfree(ip);
		return ERR_PTR(-ENOMEM);
	}
	ip->parent = parent;
	mutex_init(&ip->lock);
	INIT_DELAYED_WORK(&ip->stale_work, g6ts_ipts_stale_work);
	return ip;
}

void mshw0485_denali_destroy(struct mshw0485_denali *ip)
{
	struct hid_device *hid;

	if (!ip)
		return;
	cancel_delayed_work_sync(&ip->stale_work);
	mutex_lock(&ip->lock);
	hid = ip->hid;
	ip->hid = NULL;
	ip->ready = false;
	mutex_unlock(&ip->lock);

	if (hid) {
		hid_hw_stop(hid);
		hid_destroy_device(hid);
	}
	kfree(ip->inject_buf);
	kfree(ip);
}

void mshw0485_denali_destroy_action(void *data)
{
	mshw0485_denali_destroy(data);
}

int mshw0485_denali_register(struct mshw0485_denali *ip, u16 product_id)
{
	struct hid_device *hid;
	int ret;

	if (!ip)
		return -ENODEV;
	if (product_id != G6TS_SP11_X1E_PRODUCT_ID &&
	    product_id != G6TS_SP11_X1P_PRODUCT_ID)
		return -ENODEV;

	mutex_lock(&ip->lock);
	if (ip->ready) {
		ret = 0;
		goto out_unlock;
	}

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto out_unlock;
	}

	hid->version = G6TS_SP11_VERSION_ID;
	hid->vendor = G6TS_SP11_VENDOR_ID;
	hid->product = product_id;
	hid->bus = BUS_SPI;
	hid->dev.parent = ip->parent;
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X",
		 "Microsoft Surface G6 IPTS", G6TS_SP11_VENDOR_ID,
		 product_id);
	hid->ll_driver = &g6ts_ipts_hid_ll_driver;
	hid_set_drvdata(hid, ip);

	ret = hid_add_device(hid);
	if (ret) {
		hid_destroy_device(hid);
		dev_warn(ip->parent,
			 "failed to add the G6 IPTS shim device: %d\n", ret);
		goto out_unlock;
	}

	/*
	 * hid_add_device binds a hid driver whose connect creates the hidraw
	 * node; a second hid_hw_start here would create a duplicate.
	 */
	ip->hid = hid;
	ip->ready = true;
	dev_info(ip->parent,
		 "G6 IPTS shim registered (fabricated %zu-byte report descriptor)\n",
		 sizeof(g6ts_ipts_report_descriptor));

out_unlock:
	mutex_unlock(&ip->lock);
	return ret;
}

bool mshw0485_denali_ready(struct mshw0485_denali *ip)
{
	bool ready;

	if (!ip)
		return false;
	mutex_lock(&ip->lock);
	ready = ip->ready;
	mutex_unlock(&ip->lock);
	return ready;
}

void mshw0485_denali_feed(struct mshw0485_denali *ip, u8 report_id,
			  const u8 *content, u16 content_len)
{
	if (!ip)
		return;
	mutex_lock(&ip->lock);
	g6ts_ipts_feed_locked(ip, report_id, content, content_len);
	mutex_unlock(&ip->lock);
}

void mshw0485_denali_boundary(struct mshw0485_denali *ip)
{
	if (!ip)
		return;
	mutex_lock(&ip->lock);
	g6ts_ipts_boundary_locked(ip);
	mutex_unlock(&ip->lock);
}

ssize_t mshw0485_denali_format_stats(struct mshw0485_denali *ip, char *buf)
{
	ssize_t len;

	if (!ip)
		return sysfs_emit(buf, "ready 0\n");

	mutex_lock(&ip->lock);
	len = sysfs_emit(buf,
			 "ready %d\n"
			 "contact %d\n"
			 "cycles %llu\n"
			 "lifts %llu\n"
			 "position_found %llu\n"
			 "pressure_found %llu\n"
			 "inject_errors %llu\n"
			 "dropped_cycles %llu\n"
			 "incomplete_cycles %llu\n"
			 "unanchored_records %llu\n"
			 "sideband_records %llu\n",
			 ip->ready, ip->contact, ip->cycles, ip->lifts,
			 ip->position_found, ip->pressure_found,
			 ip->inject_errors, ip->dropped_cycles,
			 ip->incomplete_cycles, ip->unanchored_records,
			 ip->sideband_records);
	mutex_unlock(&ip->lock);
	return len;
}

ssize_t mshw0485_denali_read_report_descriptor(char *buf, loff_t off,
					       size_t count)
{
	size_t len = sizeof(g6ts_ipts_report_descriptor);
	size_t avail;

	if (off >= (loff_t)len)
		return 0;
	avail = min_t(size_t, count, len - (size_t)off);
	memcpy(buf, g6ts_ipts_report_descriptor + off, avail);
	return avail;
}
