/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2022, Unikraft GmbH and The Unikraft Authors.
 * Licensed under the BSD-3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 */

#include <uk/essentials.h>
#include <uk/arch/limits.h>
#include <uk/arch/types.h>
#include <uk/arch/paging.h>
#include <uk/plat/bootstrap.h>
#include <uk/plat/common/bootinfo.h>
#include <uk/plat/common/lcpu.h>
#include <uk/plat/common/memory.h>
#include <uk/plat/common/sections.h>
#include <kvm-x86/lxboot.h>

#define lxboot_crash(rc, msg, ...) ukplat_crash()

void _ukplat_entry(struct lcpu *lcpu, struct ukplat_bootinfo *bi);

static void
lxboot_init_cmdline(struct ukplat_bootinfo *bi, struct lxboot_params *bp)
{
	__u64 cmdline_addr;
	__sz cmdline_size;

	cmdline_addr = bp->ext_cmd_line_ptr;
	cmdline_addr <<= 32;
	cmdline_addr |= bp->hdr.cmd_line_ptr;

	cmdline_size = bp->hdr.cmd_line_size;

	if (unlikely(cmdline_addr == 0))
		lxboot_crash(-EINVAL, "Command line address is zero");

	if (cmdline_size == 0)
		return;

	bi->cmdline = cmdline_addr;
	bi->cmdline_len = cmdline_size;
}

static void
lxboot_init_initrd(struct ukplat_bootinfo *bi, struct lxboot_params *bp)
{
	__u64 initrd_addr;
	__sz initrd_size;
	struct ukplat_memregion_desc mrd = {0};
	int rc;

	initrd_addr = bp->ext_ramdisk_image;
	initrd_addr <<= 32;
	initrd_addr |= bp->hdr.ramdisk_image;

	initrd_size = bp->ext_ramdisk_size;
	initrd_size <<= 32;
	initrd_size |= bp->hdr.ramdisk_size;

	if (initrd_addr == 0 || initrd_size == 0)
		return;

	mrd.pbase = PAGE_ALIGN_DOWN(initrd_addr);
	mrd.vbase = mrd.pbase;
	mrd.pg_off = initrd_addr - mrd.pbase;
	mrd.len = initrd_size;
	mrd.type = UKPLAT_MEMRT_INITRD;
	mrd.pg_count = PAGE_COUNT(mrd.pg_off + initrd_size);
	mrd.flags = UKPLAT_MEMRF_READ;
#ifdef CONFIG_UKPLAT_MEMRNAME
	memcpy(mrd.name, "initrd", sizeof("initrd"));
#endif /* CONFIG_UKPLAT_MEMRNAME */

	rc = ukplat_memregion_list_insert(&bi->mrds, &mrd);
	if (unlikely(rc < 0))
		lxboot_crash(rc, "Unable to add ram mapping");
}

static void
lxboot_init_mem(struct ukplat_bootinfo *bi, struct lxboot_params *bp)
{
	struct ukplat_memregion_desc mrd = {0};
	struct lxboot_e820_entry *entry;
	__u64 start, end;
	int rc;
	__s8 i;

	for (i = 0; i < bp->e820_entries; i++) {
		entry = &bp->e820_table[i];

		/* Kludge: Don't add zero-page, because the platform code does
		 * not handle address zero well.
		 */
		start = MAX(entry->addr, PAGE_SIZE);
		end = entry->addr + entry->size;

		if (end <= start)
			continue;

		mrd.pbase = PAGE_ALIGN_DOWN(start);
		mrd.vbase = mrd.pbase; /* 1:1 mapping */
		mrd.pg_off = start - mrd.pbase;
		mrd.len = end - start;
		mrd.pg_count = PAGE_COUNT(mrd.pg_off + mrd.len);

		if (entry->type == LXBOOT_E820_TYPE_RAM) {
			mrd.type = UKPLAT_MEMRT_FREE;
			mrd.flags = UKPLAT_MEMRF_READ | UKPLAT_MEMRF_WRITE;

			/* Free memory regions have
			 * mrd.len == mrd.pg_count * PAGE_SIZE
			 */
			mrd.len = PAGE_ALIGN_UP(mrd.len + mrd.pg_off);
		} else {
			mrd.type = UKPLAT_MEMRT_RESERVED;
			mrd.flags = UKPLAT_MEMRF_READ;

			/* We assume that reserved regions cannot
			 * overlap with loaded modules.
			 */
		}

		rc = ukplat_memregion_list_insert(&bi->mrds, &mrd);
		if (unlikely(rc < 0))
			lxboot_crash(rc, "Unable to add ram mapping");

	}

	rc = ukplat_memregion_list_insert_legacy_hi_mem(&bi->mrds);
	if (unlikely(rc < 0))
		lxboot_crash(rc,
			     "Failed to insert legacy high memory region\n");
}

void lxboot_entry(struct lcpu *lcpu, struct lxboot_params *bp)
{
	struct ukplat_bootinfo *bi;

	bi = ukplat_bootinfo_get();
	if (unlikely(!bi))
		lxboot_crash(-EINVAL, "Incompatible or corrupted bootinfo");

	if (unlikely(bp->hdr.boot_flag != 0xAA55))
		lxboot_crash(-EINVAL, "Invalid magic number");

	lxboot_init_cmdline(bi, bp);
	lxboot_init_initrd(bi, bp);
	lxboot_init_mem(bi, bp);

	memcpy(bi->bootprotocol, "lxboot", sizeof("lxboot"));

	_ukplat_entry(lcpu, bi);
}
