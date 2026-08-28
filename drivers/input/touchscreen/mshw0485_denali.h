/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MSHW0485_DENALI_H
#define _MSHW0485_DENALI_H

#include <linux/types.h>

struct device;
struct mshw0485_denali;

#define MSHW0485_DENALI_REPORT_DESCRIPTOR_SIZE 71

bool mshw0485_denali_enabled(void);
bool mshw0485_denali_host_fault_recovery(void);
bool mshw0485_denali_ready_quiesce(void);
struct mshw0485_denali *mshw0485_denali_create(struct device *parent);
void mshw0485_denali_destroy(struct mshw0485_denali *denali);
void mshw0485_denali_destroy_action(void *data);
int mshw0485_denali_register(struct mshw0485_denali *denali, u16 product_id);
bool mshw0485_denali_ready(struct mshw0485_denali *denali);
void mshw0485_denali_feed(struct mshw0485_denali *denali, u8 report_id,
			  const u8 *content, u16 content_len);
void mshw0485_denali_boundary(struct mshw0485_denali *denali);
ssize_t mshw0485_denali_format_stats(struct mshw0485_denali *denali,
				     char *buf);
ssize_t mshw0485_denali_read_report_descriptor(char *buf, loff_t off,
					       size_t count);

#endif /* _MSHW0485_DENALI_H */
