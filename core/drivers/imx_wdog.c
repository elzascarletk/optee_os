// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright 2017 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <drivers/imx_wdog.h>
#include <io.h>
#include <keep.h>
#include <kernel/dt.h>
#include <kernel/generic_boot.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <mm/core_mmu.h>
#include <mm/core_memprot.h>
#include <util.h>

static bool ext_reset;
static vaddr_t wdog_base;

void imx_wdog_restart(void)
{
	uint32_t val;

	if (!wdog_base) {
		EMSG("No wdog mapped\n");
		panic();
	}

	if (ext_reset)
		val = 0x14;
	else
		val = 0x24;

	DMSG("val %x\n", val);

	write16(val, wdog_base + WCR_OFF);
	dsb();

	if (read16(wdog_base + WDT_WCR) & WDT_WCR_WDE) {
		write16(WDT_SEQ1, wdog_base + WDT_WSR);
		write16(WDT_SEQ2, wdog_base + WDT_WSR);
	}

	write16(val, wdog_base + WCR_OFF);
	write16(val, wdog_base + WCR_OFF);

	while (1)
		;
}
KEEP_PAGER(imx_wdog_restart);

static TEE_Result imx_wdog_init(void)
{
	enum teecore_memtypes mtype;
	void *fdt;
	paddr_t pbase;
	vaddr_t vbase;
	ssize_t sz;
	int off;
	int st;
	uint32_t i;

#ifdef CFG_MX7
	static const char * const wdog_path[] = {
		"/soc/aips-bus@30000000/wdog@30280000",
		"/soc/aips-bus@30000000/wdog@30290000",
		"/soc/aips-bus@30000000/wdog@302a0000",
		"/soc/aips-bus@30000000/wdog@302b0000",
	};
#else
	static const char * const wdog_path[] = {
		"/soc/aips-bus@02000000/wdog@020bc000",
		"/soc/aips-bus@02000000/wdog@020c0000",
	};
#endif

	fdt = get_dt_blob();
	if (!fdt) {
		EMSG("No DTB\n");
		return TEE_ERROR_NOT_SUPPORTED;
	}

	/* search the first usable wdog */
	for (i = 0; i < ARRAY_SIZE(wdog_path); i++) {
		off = fdt_path_offset(fdt, wdog_path[i]);
		if (off < 0)
			continue;

		st = _fdt_get_status(fdt, off);
		if (st & DT_STATUS_OK_SEC)
			break;
	}

	if (i == ARRAY_SIZE(wdog_path))
		return TEE_ERROR_ITEM_NOT_FOUND;

	DMSG("path: %s\n", wdog_path[i]);

	ext_reset = dt_have_prop(fdt, off, "fsl,ext-reset-output");

	pbase = _fdt_reg_base_address(fdt, off);
	if (pbase == (paddr_t)-1)
		return TEE_ERROR_ITEM_NOT_FOUND;

	sz = _fdt_reg_size(fdt, off);
	if (sz < 0)
		return TEE_ERROR_ITEM_NOT_FOUND;

	if ((st & DT_STATUS_OK_SEC) && !(st & DT_STATUS_OK_NSEC))
		mtype = MEM_AREA_IO_SEC;
	else
		mtype = MEM_AREA_IO_NSEC;

	/*
	 * Check to see whether it has been mapped using
	 * register_phys_mem or not.
	 */
	vbase = (vaddr_t)phys_to_virt(pbase, mtype);
	if (!vbase) {
		if (!core_mmu_add_mapping(mtype, pbase, sz)) {
			EMSG("Failed to map %zu bytes at PA 0x%"PRIxPA,
			     (size_t)sz, pbase);
			return TEE_ERROR_GENERIC;
		}
	}

	vbase = (vaddr_t)phys_to_virt(pbase, mtype);
	if (!vbase) {
		EMSG("Failed to get VA for PA 0x%"PRIxPA, pbase);
		return TEE_ERROR_GENERIC;
	}

	wdog_base = vbase;

	return TEE_SUCCESS;
}
driver_init(imx_wdog_init);
