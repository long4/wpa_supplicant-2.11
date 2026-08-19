/*
 * SPDX-FileCopyrightText: Copyright (c) 2026-2030 OMNI CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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

#ifndef INCLUDED_COMMON_H
#define INCLUDED_COMMON_H

#include <oeth_type.h>
#include <osi_common.h>

struct osi_core_priv_data;

/**
 * @brief osi_lock_init - Initialize lock to unlocked state.
 *
 * @note
 * Algorithm:
 *  - Set lock to unlocked state.
 *
 * @param[in] lock - Pointer to lock to be initialized
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
static inline void osi_lock_init(u32 *lock)
{
	*lock = OSI_UNLOCKED;
}

/**
 * @brief osi_lock_irq_enabled - Spin lock. Busy loop till lock is acquired.
 *
 * @note
 * Algorithm:
 *  - Atomic compare and swap operation till lock is held.
 *
 * @param[in] lock - Pointer to lock to be acquired.
 *
 * @note
 *  - Does not disable irq. Do not call this API to acquire any
 *    lock that is shared between top/bottom half. It will result in deadlock.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 */
static inline void osi_lock_irq_enabled(u32 *lock)
{
	/* __sync_val_compare_and_swap(lock, old value, new value) returns the
	 * old value if successful.
	 */
	while (__sync_val_compare_and_swap(lock, OSI_UNLOCKED, OSI_LOCKED) !=
	      OSI_UNLOCKED) {
		/* Spinning.
		 * Will deadlock if any ISR tried to lock again.
		 */
	}
}

/**
 * @brief osi_unlock_irq_enabled - Release lock.
 *
 * @note
 * Algorithm:
 *  - Atomic compare and swap operation to release lock.
 *
 * @param[in] lock - Pointer to lock to be released.
 *
 * @note
 *  - Does not disable irq. Do not call this API to release any
 *    lock that is shared between top/bottom half.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 */
static inline void osi_unlock_irq_enabled(u32 *lock)
{
	if (__sync_val_compare_and_swap(lock, OSI_LOCKED, OSI_UNLOCKED) !=
	    OSI_LOCKED) {
		/* Do nothing. Already unlocked */
	}
}

/**
 * @brief osi_readl - Read a memory mapped register.
 *
 * @param[in] addr: Memory mapped address.
 *
 * @pre Physical address has to be memory mapped.
 *
 * @return Data from memory mapped register - success.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: Yes
 * - De-initialization: Yes
 */
static inline u32 osi_readl(void *addr)
{
	return *(volatile u32 *)addr;
}

/**
 * @brief osi_writel - Write to a memory mapped register.
 *
 * @param[in] val:  Value to be written.
 * @param[in] addr: Memory mapped address.
 *
 * @pre Physical address has to be memory mapped.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: Yes
 * - De-initialization: Yes
 */
static inline void osi_writel(u32 val, void *addr)
{
	*(volatile u32 *)addr = val;
}

/**
 * @brief osi_readla - Read a memory mapped register.
 *
 * @ note
 * The difference between osi_readla & osi_readl is osi_core argument.
 * In case of ethernet server, osi_core used to define policy for each VM.
 * In case of non virtualization osi_core argument is ignored.
 *
 * @param[in] priv: Priv address.
 * @param[in] addr: Memory mapped address.
 *
 * @note Physical address has to be memmory mapped.
 *
 * @return Data from memory mapped register - success.
 */
static inline u32 osi_readla(OSI_UNUSED void *priv, void *addr)
{
	(void)priv; // unused
	return *(volatile u32 *)addr;
}

/**
 *
 * @ note
 * @brief osi_writela - Write to a memory mapped register.
 * The difference between osi_writela & osi_writel is osi_core argument.
 * In case of ethernet server, osi_core used to define policy for each VM.
 * In case of non virtualization osi_core argument is ignored.
 *
 * @param[in] priv: Priv address.
 * @param[in] val:  Value to be written.
 * @param[in] addr: Memory mapped address.
 *
 * @note Physical address has to be memmory mapped.
 */
static inline void osi_writela(OSI_UNUSED void *priv, u32 val, void *addr)
{
	(void)priv; // unused
	*(volatile u32 *)addr = val;
}

/**
 * @brief validate_mac_ver_update_chans - Validates mac version and update chan
 *
 * @param[in] mac: MAC HW type.
 * @param[in] mac_ver: MAC version read.
 * @param[out] num_max_chans: Maximum channel number.
 * @param[out] l_mac_ver: local mac version.
 *
 * @note MAC has to be out of reset.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 *
 * @retval 0 - for not Valid MAC
 * @retval 1 - for Valid MAC
 */
static inline s32 validate_mac_ver_update_chans(u32 mac,
						    u32 mac_ver,
						    u32 *num_max_chans,
						    u32 *l_mac_ver)
{
	const u32 max_dma_chan[OSI_MAX_MAC_IP_TYPES] = {
		OSI_EQOS_MAX_NUM_CHANS,
		OSI_MGBE_T23X_MAX_NUM_CHANS,
		OSI_MGBE_MAX_NUM_CHANS
	};
	s32 ret;

	switch (mac_ver) {
#ifndef OSI_STRIPPED_LIB
	case OSI_EQOS_MAC_5_00:
		*num_max_chans = OSI_EQOS_XP_MAX_CHANS;
		*l_mac_ver = MAC_CORE_VER_TYPE_EQOS;
		ret = 1;
		break;
#endif /* !OSI_STRIPPED_LIB */
	case OSI_EQOS_MAC_5_30:
	case OSI_EQOS_MAC_5_40:
		*num_max_chans = OSI_EQOS_MAX_NUM_CHANS;
		*l_mac_ver = MAC_CORE_VER_TYPE_EQOS_5_30;
		ret = 1;
		break;
	case OSI_MGBE_MAC_3_10:
	//TBD: T264 uFPGA reports mac version 3.2
	case OSI_MGBE_MAC_3_20:
	case OSI_MGBE_MAC_4_20:
#ifndef OSI_STRIPPED_LIB
	case OSI_MGBE_MAC_4_00:
#endif /* !OSI_STRIPPED_LIB */
		//TBD: T264 number of dma channels?
		*num_max_chans = max_dma_chan[mac];
		*l_mac_ver = MAC_CORE_VER_TYPE_MGBE;
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

/**
 * @brief osi_memcpy - osi memcpy
 *
 * @param[out] dest: destination pointer
 * @param[in] src: source pointer
 * @param[in] n: number bytes of source
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 */
static inline void osi_memcpy(void *dest, const void *src, u64 n)
{
	char *cdest = dest;
	const char *csrc = src;
	u64 i = 0;

	for (i = 0; i < n; i++) {
		cdest[i] = csrc[i];
	}
}

static inline s32 osi_memcmp(const void *dest, const void *src, s32 n)
{
	const char *const cdest = dest;
	const char *const csrc = src;
	s32 ret = 0;
	s32 i;

	for (i = 0; i < n; i++) {
		if (csrc[i] < cdest[i]) {
			ret = -1;
			goto fail;
		} else if (csrc[i] > cdest[i]) {
			ret = 1;
			goto fail;
		} else {
			/* Do Nothing */
		}
	}
fail:
	return ret;
}
#endif
