// SPDX-License-Identifier: MIT
/* SPDX-FileCopyrightText: Copyright (c) 2026-2030 OMNI CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <ivc_core.h>
#include "core_local.h"
#include "common.h"

/** core local data structure used within RM unit */
static struct core_local g_core[MAX_CORE_INSTANCES];

/**
 * @brief Function to validate input arguments of API.
 *
 * @param[in] osi_core: OSI Core private data structure.
 * @param[in] l_core: Core local private data structure.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: Yes
 * - De-initialization: Yes
 *
 * @retval 0 on Success
 * @retval -1 on Failure
 */
static inline s32 validate_if_args(struct osi_core_priv_data *const osi_core,
				       struct core_local *l_core)
{
	s32 ret = 0;

	if ((osi_core == OSI_NULL) || (l_core->if_init_done == OSI_DISABLE) ||
	    (l_core->magic_num != (u64)osi_core)) {
		ret = -1;
	}

	return ret;
}

struct osi_core_priv_data *osi_get_core(void)
{
	u32 i;
	struct osi_core_priv_data *osi_core = OSI_NULL;

	for (i = 0U; i < MAX_CORE_INSTANCES; i++) {
		if (g_core[i].if_init_done == OSI_ENABLE) {
			continue;
		}

		break;
	}

	if (i == MAX_CORE_INSTANCES) {
		goto fail;
	}

#ifdef OSI_RM_FTRACE
	ethernet_server_entry_log();
#endif
	g_core[i].magic_num = (u64)&g_core[i].osi_core;

	g_core[i].tx_ts_head.prev = &g_core[i].tx_ts_head;
	g_core[i].tx_ts_head.next = &g_core[i].tx_ts_head;
	g_core[i].pps_freq = OSI_DISABLE;

	osi_core = &g_core[i].osi_core;
	osi_memset(osi_core, 0, sizeof(struct osi_core_priv_data));
#ifdef OSI_RM_FTRACE
	ethernet_server_exit_log();
#endif
fail:
	return osi_core;
}

#ifdef FSI_EQOS_SUPPORT
s32 osi_release_core(struct osi_core_priv_data *osi_core)
{
	struct core_local *l_core = (struct core_local *)(void *)osi_core;
	s32 ret = 0;

	if (osi_core == OSI_NULL) {
		ret = -1;
		goto fail;
	}

	if (l_core->magic_num != (u64)osi_core){
		ret = -1;
		goto fail;
	}

	l_core->magic_num = 0ULL;
	l_core->if_init_done = OSI_DISABLE;

fail:
	return ret;

}

#endif /* FSI_EQOS_SUPPORT */

struct osi_core_priv_data *get_role_pointer(u32 role)
{
	u32 i;
	struct osi_core_priv_data *ret_ptr = OSI_NULL;

	/* Current approch to give pointer for 1st role */
	for (i = 0U; i < MAX_CORE_INSTANCES; i++) {
		if ((g_core[i].if_init_done == OSI_ENABLE) &&
		    (g_core[i].ether_m2m_role == role)) {
			ret_ptr = &g_core[i].osi_core;
			break;
		}
	}

	return ret_ptr;
}

static s32 validate_init_core_ops_args(struct osi_core_priv_data *const osi_core)
{
	struct core_local *l_core = (struct core_local *)(void *)osi_core;
	s32 ret = 0;

	if (osi_core == OSI_NULL) {
		ret = -1;
		goto fail;
	}

	if (osi_core->osd_ops.ops_log == OSI_NULL) {
		ret = -1;
		goto fail;
	}

	if (osi_core->use_virtualization > OSI_ENABLE) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_INVALID,
			     "CORE: wrong use_virtualization\n", 0ULL);
		ret = -1;
		goto fail;
	}

	if ((l_core->magic_num != (u64)osi_core) ||
	    (l_core->if_init_done == OSI_ENABLE)) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_INVALID,
			     "CORE: Invalid magic_num or if_init_done\n", 0ULL);
		ret = -1;
	}

fail:
	return ret;
}

s32 osi_init_core_ops(struct osi_core_priv_data *const osi_core)
{
	struct core_local *l_core = (struct core_local *)(void *)osi_core;
	typedef void (*init_core_ops_array)(struct if_core_ops *if_ops_p);
	init_core_ops_array i_lcore_ops[MAX_INTERFACE_OPS] = {
		hw_interface_init_core_ops,
		ivc_interface_init_core_ops
	};
	static struct if_core_ops if_ops[MAX_INTERFACE_OPS];
	s32 ret = 0;

	if (validate_init_core_ops_args(osi_core) < 0) {
		ret = -1;
		goto fail;
	}

	l_core->if_ops_p = &if_ops[osi_core->use_virtualization];
	i_lcore_ops[osi_core->use_virtualization](l_core->if_ops_p);

	ret = l_core->if_ops_p->if_init_core_ops(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_INVALID,
			     "if_init_core_ops failed\n", 0ULL);
		goto fail;
	}

	l_core->ts_lock = OSI_DISABLE;
	l_core->ether_m2m_role = osi_core->m2m_role;
	l_core->serv.count = SERVO_STATS_0;
	l_core->serv.drift = 0;
	l_core->serv.last_ppb = 0;
	osi_lock_init(&l_core->serv.m2m_lock);
#ifdef MACSEC_SUPPORT
	osi_lock_init(&osi_core->macsec_fpe_lock);
#endif /* MACSEC_SUPPORT */
	l_core->hw_init_successful = OSI_DISABLE;
	l_core->m2m_tsync = OSI_DISABLE;
	l_core->if_init_done = OSI_ENABLE;
	if ((osi_core->m2m_role == OSI_PTP_M2M_PRIMARY) ||
	    (osi_core->m2m_role == OSI_PTP_M2M_SECONDARY)) {
		l_core->m2m_tsync = OSI_ENABLE;
	} else {
		l_core->m2m_tsync = OSI_DISABLE;
	}

	if (osi_core->pps_frq <= OSI_MAX_PPS_HZ) {
		l_core->pps_freq = osi_core->pps_frq;
	} else {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_INVALID,
			     "invalid pps_frq\n", (u64)osi_core->pps_frq);
		ret = -1;
	}
fail:
	return ret;
}

s32 osi_write_phy_reg(struct osi_core_priv_data *const osi_core,
			  const u32 phyaddr, const u32 phyreg,
			  const u16 phydata)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

#ifdef OSI_RM_FTRACE
	ethernet_server_entry_log();
#endif
	ret = l_core->if_ops_p->if_write_phy_reg(osi_core, phyaddr, phyreg,
						 phydata);
#ifdef OSI_RM_FTRACE
	ethernet_server_exit_log();
#endif
fail:
	return ret;
}

#ifdef PHY_PROG
s32 osi_write_phy_reg_dt(struct osi_core_priv_data *const osi_core,
				const u32 phyaddr,
				const u32 macMdioForAddrReg,
				const u32 macMdioForDataReg)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

	ret = l_core->if_ops_p->if_write_phy_reg_dt(osi_core, phyaddr,
						 macMdioForAddrReg, macMdioForDataReg);
fail:
	return ret;
}
#endif /* PHY_PROG */

s32 osi_read_phy_reg(struct osi_core_priv_data *const osi_core,
			 const u32 phyaddr, const u32 phyreg)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

#ifdef OSI_RM_FTRACE
	ethernet_server_entry_log();
#endif
	ret = l_core->if_ops_p->if_read_phy_reg(osi_core, phyaddr, phyreg);
#ifdef OSI_RM_FTRACE
	ethernet_server_exit_log();
#endif
fail:
	return ret;
}

#ifdef PHY_PROG
s32 osi_read_phy_reg_dt(struct osi_core_priv_data *const osi_core,
					const u32 phyaddr,
					const u32 macMdioForAddrReg,
					const u32 macMdioForDataReg)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

	ret = l_core->if_ops_p->if_read_phy_reg_dt(osi_core, phyaddr,
						macMdioForAddrReg, macMdioForDataReg);

fail:
	return ret;
}
#endif /* PHY_PROG */

s32 osi_hw_core_init(struct osi_core_priv_data *const osi_core)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

#ifdef OSI_RM_FTRACE
	ethernet_server_entry_log();
#endif
	ret = l_core->if_ops_p->if_core_init(osi_core);
#ifdef OSI_RM_FTRACE
	ethernet_server_exit_log();
#endif
fail:
	return ret;
}

s32 osi_hw_core_deinit(struct osi_core_priv_data *const osi_core)
{
	s32 ret = -1;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;

#ifdef OSI_RM_FTRACE
	ethernet_server_entry_log();
#endif
	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

	ret = l_core->if_ops_p->if_core_deinit(osi_core);
fail:
#ifdef OSI_RM_FTRACE
	ethernet_server_exit_log();
#endif
	return ret;
}

#ifdef OSI_RM_FTRACE
s32 osi_handle_ioctl_count = 0;
#endif
s32 osi_handle_ioctl(struct osi_core_priv_data *osi_core,
			 struct osi_ioctl *data)
{
	struct core_local *l_core = (struct core_local *)(void *)osi_core;
	s32 ret = -1;

	if (validate_if_args(osi_core, l_core) < 0) {
		goto fail;
	}

	if (data == OSI_NULL) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_INVALID,
			     "CORE: Invalid argument\n", 0ULL);
		goto fail;
	}

#ifdef OSI_RM_FTRACE
	if ((osi_handle_ioctl_count % 1000 == 0)) {
		ethernet_server_entry_log();
	}
#endif
	ret = l_core->if_ops_p->if_handle_ioctl(osi_core, data);
fail:
#ifdef OSI_RM_FTRACE
	if ((osi_handle_ioctl_count++ % 1000 == 0)) {
		ethernet_server_exit_log();
	}
#endif
	return ret;
}
