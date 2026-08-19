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

#ifndef INCLUDED_CORE_LOCAL_H
#define INCLUDED_CORE_LOCAL_H

#include <osi_core.h>
#ifdef MACSEC_SUPPORT
#include <osi_macsec.h>
#endif /* MACSEC_SUPPORT */
#include "common.h"

/**
 * @brief Maximum number of OSI core instances.
 */
#ifndef MAX_CORE_INSTANCES
#define MAX_CORE_INSTANCES	10U
#endif

/**
 * @brief Maximum number of interface operations.
 */
#define MAX_INTERFACE_OPS	2U

#define CHAN_START_POSITION 6U
#define PKT_ID_CNT	((u32)1 << CHAN_START_POSITION)
/**
 * @brief Maximum number of timestamps stored in OSI from HW FIFO.
 */
 //TBD: does it change for T264?
#define MAX_TX_TS_CNT		(PKT_ID_CNT * OSI_MGBE_MAX_NUM_CHANS)

/**
 * @brief FIFO size helper macro
 */
#define FIFO_SZ(x)		((((x) * 1024U) / 256U) - 1U)

/**
 * @brief Dynamic configuration helper macros.
 */
#define DYNAMIC_CFG_L3_L4	OSI_BIT(0)
#define DYNAMIC_CFG_AVB		OSI_BIT(2)
#define DYNAMIC_CFG_L2		OSI_BIT(3)
#define DYNAMIC_CFG_L2_IDX	3U
#define DYNAMIC_CFG_RXCSUM	OSI_BIT(4)
#define DYNAMIC_CFG_PTP		OSI_BIT(7)
#define DYNAMIC_CFG_EST		OSI_BIT(8)
#define DYNAMIC_CFG_FPE		OSI_BIT(9)
#define DYNAMIC_CFG_FRP		OSI_BIT(10)
#ifdef HSI_SUPPORT
#define DYNAMIC_CFG_HSI		OSI_BIT(11)
#endif /* HSI_SUPPORT */

#ifndef OSI_STRIPPED_LIB
#define DYNAMIC_CFG_FC		OSI_BIT(1)
#define DYNAMIC_CFG_VLAN	OSI_BIT(5)
#define DYNAMIC_CFG_EEE		OSI_BIT(6)
#define DYNAMIC_CFG_FC_IDX	1U
#define DYNAMIC_CFG_VLAN_IDX	5U
#define DYNAMIC_CFG_EEE_IDX	6U
#define DYNAMIC_CFG_PTP_IDX	7U
#endif /* !OSI_STRIPPED_LIB */

#define DYNAMIC_CFG_L3_L4_IDX	0U
#define DYNAMIC_CFG_AVB_IDX	2U
#define DYNAMIC_CFG_L2_IDX	3U
#define DYNAMIC_CFG_RXCSUM_IDX	4U
#define DYNAMIC_CFG_EST_IDX	8U
#define DYNAMIC_CFG_FPE_IDX	9U
#define DYNAMIC_CFG_FRP_IDX	10U
#ifdef HSI_SUPPORT
#define DYNAMIC_CFG_HSI_IDX	11U
#endif /* HSI_SUPPORT */
#define OSI_SUSPENDED		OSI_BIT(0)


/**
 * interface core ops
 */
struct if_core_ops {
	/** Interface function called to initialize MAC and MTL registers */
	s32 (*if_core_init)(struct osi_core_priv_data *const osi_core);
	/** Interface function called to deinitialize MAC and MTL registers */
	s32 (*if_core_deinit)(struct osi_core_priv_data *const osi_core);
	/** Interface function called to write into a PHY reg over MDIO bus */
	s32 (*if_write_phy_reg)(struct osi_core_priv_data *const osi_core,
				    const u32 phyaddr,
				    const u32 phyreg,
				    const u16 phydata);
	/** Interface function called to read a PHY reg over MDIO bus from DT*/
	s32 (*if_read_phy_reg)(struct osi_core_priv_data *const osi_core,
					const u32 phyaddr,
					const u32 phyreg);
#ifdef PHY_PROG
	/** Interface function called to program PHY from DTB enteries*/
	s32 (*if_write_phy_reg_dt)(struct osi_core_priv_data *const osi_core,
					const u32 phyaddr,
					const u32 macMdioForAddrReg,
					const u32 macMdioForDataReg);
	/** Interface function called to program PHY from DTB enteries*/
	s32 (*if_read_phy_reg_dt)(struct osi_core_priv_data *const osi_core,
					const u32 phyaddr,
					const u32 macMdioForAddrReg,
					const u32 macMdioForDataReg);
#endif /* PHY_PROG */
	/** Initialize Interface core operations */
	s32 (*if_init_core_ops)(struct osi_core_priv_data *const osi_core);
	/** Interface function called to handle runtime commands */
	s32 (*if_handle_ioctl)(struct osi_core_priv_data *osi_core,
				   struct osi_ioctl *data);
};

/**
 * @brief Initialize MAC & MTL core operations.
 */
struct core_ops {
	/** Called to initialize MAC and MTL registers */
	s32 (*core_init)(struct osi_core_priv_data *const osi_core);
	/** Called to handle common interrupt */
	void (*handle_common_intr)(struct osi_core_priv_data *const osi_core);
	/** Called to do pad caliberation */
	s32 (*pad_calibrate)(struct osi_core_priv_data *const osi_core);
	/** Called to update MAC address 1-127 */
	s32 (*update_mac_addr_low_high_reg)(
				struct osi_core_priv_data *const osi_core,
				const struct osi_filter *filter);
	/** Called to configure L3L4 filter */
	s32 (*config_l3l4_filters)(struct osi_core_priv_data *const osi_core,
				       u32 filter_no,
				       const struct osi_l3_l4_filter *const l3_l4);
	/** Called to adjust the mac time */
	s32 (*adjust_mactime)(struct osi_core_priv_data *const osi_core,
				  const u32 sec,
				  const u32 nsec,
				  const u32 neg_adj,
				  const u32 one_nsec_accuracy);
	/** Called to update MMC counter from HW register */
	void (*read_mmc)(struct osi_core_priv_data *const osi_core);
	/** Called to write into a PHY reg over MDIO bus */
	s32 (*write_phy_reg)(struct osi_core_priv_data *const osi_core,
				 const u32 phyaddr,
				 const u32 phyreg,
				 const u16 phydata);
	/** Called to read from a PHY reg over MDIO bus */
	s32 (*read_phy_reg)(struct osi_core_priv_data *const osi_core,
				const u32 phyaddr,
				const u32 phyreg);
#ifdef PHY_PROG
	/** Called to write into a PHY reg over MDIO bus when read from DT*/
	s32 (*write_phy_reg_dt)(struct osi_core_priv_data *const osi_core,
				const u32 phyaddr,
				const u32 macMdioForAddrReg,
				const u32 macMdioForDataReg);
	/** Called to read from a PHY reg over MDIO bus when read from DT */
	s32 (*read_phy_reg_dt)(struct osi_core_priv_data *const osi_core,
				const u32 phyaddr,
				const u32 macMdioForAddrReg,
				const u32 macMdioForDataReg);
#endif /* PHY_PROG */
	/** Called to get HW features */
	void (*get_hw_features)(struct osi_core_priv_data *const osi_core,
				struct osi_hw_features *hw_feat);
#ifndef OSI_STRIPPED_LIB
	/** Called to read reg */
	u32 (*read_reg)(struct osi_core_priv_data *const osi_core,
			     const s32 reg);
	/** Called to write reg */
	u32 (*write_reg)(struct osi_core_priv_data *const osi_core,
			      const u32 val,
			      const s32 reg);
#endif
#if defined MACSEC_SUPPORT && !defined OSI_STRIPPED_LIB
	/** Called to read macsec reg */
	u32 (*read_macsec_reg)(struct osi_core_priv_data *const osi_core,
				    const s32 reg);
	/** Called to write macsec reg */
	u32 (*write_macsec_reg)(struct osi_core_priv_data *const osi_core,
				     const u32 val,
				     const s32 reg);
#endif /*  MACSEC_SUPPORT */
#ifdef MACSEC_SUPPORT
	void (*macsec_config_mac)(struct osi_core_priv_data *const osi_core,
				  const u32 enable);
#endif /*  MACSEC_SUPPORT */
	s32 (*config_coe_buf)(struct osi_core_priv_data *const osi_core,
				  struct osi_mgbe_coe mgbe_coe);
#ifndef OSI_STRIPPED_LIB
	/** Called to configure the MTL to forward/drop tx status */
	s32 (*config_tx_status)(struct osi_core_priv_data *const osi_core,
				    const u32 tx_status);
	/** Called to configure the MAC rx crc */
	s32 (*config_rx_crc_check)(
				     struct osi_core_priv_data *const osi_core,
				     const u32 crc_chk);
	/** Called to configure the MAC flow control */
	s32 (*config_flow_control)(
				     struct osi_core_priv_data *const osi_core,
				     const u32 flw_ctrl);
	/** Called to enable/disable HW ARP offload feature */
	s32 (*config_arp_offload)(struct osi_core_priv_data *const osi_core,
				      const u32 enable,
				      const u8 *ip_addr);
	/** Called to configure HW PTP offload feature */
	s32 (*config_ptp_offload)(struct osi_core_priv_data *const osi_core,
				  struct osi_pto_config *const pto_config);
	/** Called to configure VLAN filtering */
	s32 (*config_vlan_filtering)(
				     struct osi_core_priv_data *const osi_core,
				     const u32 filter_enb_dis,
				     const u32 perfect_hash_filtering,
				     const u32 perfect_inverse_match);
	/** Called to configure EEE Tx LPI */
	void (*configure_eee)(struct osi_core_priv_data *const osi_core,
			      const u32 tx_lpi_enabled,
			      const u32 tx_lpi_timer);
	/** Called to configure MAC in loopback mode */
	s32 (*config_mac_loopback)(
				struct osi_core_priv_data *const osi_core,
				const u32 lb_mode);
	/** Called to configure RSS for MAC */
	s32 (*config_rss)(struct osi_core_priv_data *osi_core, const struct osi_core_rss *rss);
	/** Called to get RSS parameters from MAC */
	s32 (*get_rss)(struct osi_core_priv_data *osi_core, struct osi_core_rss *rss);
	/** Called to configure the PTP RX packets Queue */
	s32 (*config_ptp_rxq)(struct osi_core_priv_data *const osi_core,
				  const u32 rxq_idx,
				  const u32 enable);
#endif /* !OSI_STRIPPED_LIB */
	/** Called to set av parameter */
	s32 (*set_avb_algorithm)(struct osi_core_priv_data *const osi_core,
				     const struct osi_core_avb_algorithm *const avb);
	/** Called to get av parameter */
	s32 (*get_avb_algorithm)(struct osi_core_priv_data *const osi_core,
				     struct osi_core_avb_algorithm *const avb);
	/** Called to configure FRP engine */
	s32 (*config_frp)(struct osi_core_priv_data *const osi_core,
			      const u32 enabled);
	/** Called to update FRP Instruction Table entry */
	s32 (*update_frp_entry)(struct osi_core_priv_data *const osi_core,
				    const u32 pos,
				    struct osi_core_frp_data *const data);
	/** Called to update FRP NVE and  */
	void (*update_frp_nve)(struct osi_core_priv_data *const osi_core, const u32 nve);
	/** Called to get RCHList index */
	s32 (*get_rchlist_index)(struct osi_core_priv_data *const osi_core,
				     u8 const *mac_addr);
	/** Called to free RCHLIST index */
	void (*free_rchlist_index)(struct osi_core_priv_data *const osi_core,
				   const s32 rch_indx);
#ifdef HSI_SUPPORT
	/** Interface function called to initialize HSI */
	s32 (*core_hsi_configure)(struct osi_core_priv_data *const osi_core,
				   const u32 enable);
#ifdef OMNI_TEST
	/** Interface function called to inject error */
	s32 (*core_hsi_inject_err)(struct osi_core_priv_data *const osi_core,
				       const u32 error_code);
#endif
#endif
};

/**
 * @brief constant values for drift MAC to MAC sync.
 */
/* No longer needed since DRIFT CAL is not used */
#define	I_COMPONENT_BY_10	3LL
#define	P_COMPONENT_BY_10	7LL
#define	WEIGHT_BY_10		10LL
#define	MAX_FREQ_POS		250000000LL
#define	MAX_FREQ_NEG		-250000000LL
#define SERVO_STATS_0		0U
#define SERVO_STATS_1		1U
#define SERVO_STATS_2		2U
#define OSI_NSEC_PER_SEC_SIGNED	1000000000LL

#define ETHER_NSEC_MASK		0x7FFFFFFFU

/**
 * @brief servo data structure.
 */
struct core_ptp_servo {
	/** Offset/drift array to maintain current and last value */
	s64 offset[2];
	/** Target MAC HW time counter array to maintain current and last
	 *  value
	 */
	s64 local[2];
	/* Servo state. initialized with 0. This states are used to monitor
	 * if there is sudden change in offset */
	u32 count;
	/* Accumulated freq drift */
	s64 drift;
	/* P component */
	s64 const_p;
	/* I component */
	s64 const_i;
	/* Last know ppb */
	s64 last_ppb;
	/* MAC to MAC locking to access HW time register within OSI calls */
	u32 m2m_lock;
};

/**
 * @brief AVB dynamic config storage structure
 */
struct core_avb {
	/** Represend whether AVB config done or not */
	u32 used;
	/** AVB data structure */
	struct osi_core_avb_algorithm avb_info;
};

/**
 * @brief VLAN dynamic config storage structure
 */
struct core_vlan {
	/** VID to be stored */
	u32 vid;
	/** Represens whether VLAN config done or not */
	u32 used;
};

/**
 * @brief L2 filter dynamic config storage structure
 */
struct core_l2 {
	u32 used;
	struct osi_filter filter;
};

/**
 * @brief Dynamic config storage structure
 */
struct dynamic_cfg {
	u32 flags;
	/** L3_L4 filters */
	struct osi_l3_l4_filter l3_l4[OSI_MGBE_MAX_L3_L4_FILTER_T264];
	/** flow control */
	u32 flow_ctrl;
	/** AVB */
	struct core_avb avb[OSI_MGBE_MAX_NUM_QUEUES];
	/** RXCSUM */
	u32 rxcsum;
	/** VLAN arguments storage */
	struct core_vlan vlan[VLAN_NUM_VID];
	/** LPI parameters storage */
	u32 tx_lpi_enabled;
	u32 tx_lpi_timer;
	/** PTP information storage */
	u32 ptp;
	/** EST information storage */
	struct osi_est_config est;
	/** FPE information storage */
	struct osi_fpe_config fpe;
	/** L2 filter storage */
	struct osi_filter l2_filter;
	/** L2 filter configuration */
	struct core_l2 l2[EQOS_MAX_MAC_ADDRESS_FILTER];
#ifdef HSI_SUPPORT
	/** HSI state */
	u32 hsi_en_dis;
#endif /* HSI_SUPPORT */
};

/**
 * @brief Core local data structure.
 */
struct core_local {
	/** OSI Core data variable */
	struct osi_core_priv_data osi_core;
	/** Core local operations variable */
	struct core_ops *ops_p;
	/** interface core local operations variable */
	struct if_core_ops *if_ops_p;
	/** Address of MACsec HW operations structure */
	struct osi_macsec_core_ops *macsec_ops;
	/** structure to store tx time stamps */
	struct osi_core_tx_ts ts[MAX_TX_TS_CNT];
	/** Flag to represent infterface initialization done or not */
	u32 if_init_done;
	/** Magic number to validate osi core pointer */
	u64 magic_num;
	/** This is the head node for PTP packet ID queue */
	struct osi_core_tx_ts tx_ts_head;
	/** Maximum number of queues/channels */
	u32 num_max_chans;
	/** GCL depth supported by HW */
	u32 gcl_dep;
	/** Max GCL width (time + gate) value supported by HW */
	u32 gcl_width_val;
	/** TS lock */
	u32 ts_lock;
	/** Controller mac to mac role */
	u32 ether_m2m_role;
	/** Servo structure */
	struct core_ptp_servo serv;
	/** HW comeout from reset successful OSI_ENABLE else OSI_DISABLE */
	u32 hw_init_successful;
	/** Dynamic MAC to MAC time sync control for secondary interface */
	u32 m2m_tsync;
	/** control pps output signal */
	u32 pps_freq;
	/** Time interval mask for GCL entry */
	u32 ti_mask;
	/** Hardware dynamic configuration context */
	struct dynamic_cfg cfg;
	/** Hardware dynamic configuration state */
	u32 state;
	/** XPCS Lane bringup/Block lock status */
	u32 lane_status;
	/** XPCS power up status */
	u32 lane_powered_up;
	/** Exact MAC used across SOCs 0:Legacy EQOS, 1:Orin EQOS, 2:Orin MGBE */
	u32 l_mac_ver;
#if defined(L3L4_WILDCARD_FILTER)
	/** l3l4 wildcard filter configured (OSI_ENABLE) / not configured (OSI_DISABLE) */
	u32 l3l4_wildcard_filter_configured;
#endif /* L3L4_WILDCARD_FILTER */
	/** Hardware features */
	struct osi_hw_features hw_features;
};

/**
 * @brief update_counter_u_local - Increment u32 counter
 *
 * @param[out] value: Pointer to value to be incremented.
 * @param[in] incr: increment value
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
static inline void update_counter_u_local(u32 *value, OSI_UNUSED u32 incr)
{
	(void)incr;
	*value = (((*value) & ((u32)INT_MAX)) + 1U) & (u32)INT_MAX;
}

/**
 * @brief eqos_init_core_ops - Initialize EQOS core operations.
 *
 * @param[in] ops: Core operations pointer.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
void eqos_init_core_ops(struct core_ops *ops);

/**
 * @brief mgbe_init_core_ops - Initialize MGBE core operations.
 *
 * @param[in] ops: Core operations pointer.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
void mgbe_init_core_ops(struct core_ops *ops);

/**
 * @brief ivc_init_macsec_ops - Initialize macsec core operations.
 *
 * @param[in] macsecops: Macsec operations pointer.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
void ivc_init_macsec_ops(void *macsecops);

/**
 * @brief hw_interface_init_core_ops - Initialize HW interface functions.
 *
 * @param[in] if_ops_p: interface core operations pointer.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
void hw_interface_init_core_ops(struct if_core_ops *if_ops_p);

/**
 * @brief ivc_interface_init_core_ops - Initialize IVC interface functions
 *
 * @param[in] if_ops_p: interface core operations pointer.
 *
 * @note
 * API Group:
 * - Initialization: Yes
 * - Run time: No
 * - De-initialization: No
 */
void ivc_interface_init_core_ops(struct if_core_ops *if_ops_p);

/**
 * @brief get osi pointer for PTP primary/sec interface
 *
 * @note
 * Algorithm:
 *  - Returns OSI core data structure corresponding to mac-to-mac PTP
 *    role.
 *
 * @pre OSD layer should use this as first API to get osi_core pointer and
 * use the same in remaning API invocation for mac-to-mac time sync.
 *
 * @note
 * Traceability Details:
 *
 * @note
 * Classification:
 * - Interrupt: No
 * - Signal handler: No
 * - Thread safe: No
 * - Required Privileges: None
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval valid and unique osi_core pointer on success
 * @retval NULL on failure.
 */
struct osi_core_priv_data *get_role_pointer(u32 role);

/**
 * @brief
 * Description: osi_update_stats_counter - update value by increment passed
 * as parameter
 *
 * @param[in] last_value: last value of stat counter
 *   * Range: 0 to UINT64_MAX
 * @param[in] incr: increment value
 *   * Range: 0 to UINT64_MAX
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt handler: Yes
 *  - Signal handler: Yes
 *  - Thread safe: No
 *  - Async/Sync: Sync
 *  - Required Privileges: None
 * - API Group:
 *  - Initialization: No
 *  - Run time: Yes
 *  - De-initialization: No
 *
 * @pre
 *  - MAC needs to be out of reset and proper clocks need to be configured.
 *  - DMA HW init need to be completed successfully, see osi_hw_dma_init
 *
 * @retval 0 on sucess
 * @retval -1 on failure
 */
#ifndef DOXYGEN_ICD
/**
 *
 * Traceability Details:
 * - SWUD_ID: NET_SWUD_TAG_OETHCL_016
 * - SWUD_ID: NET_SWUD_TAG_OETHRM_042
 **/
#else
/**
 *
 * @dir
 *  - forward
 */
#endif
static inline u64 osi_update_stats_counter(u64 last_value,
						u64 incr)
{
	return ((last_value & (u64)OSI_LLONG_MAX) + (incr & (u64)OSI_LLONG_MAX));
}
/**
 * @addtogroup Generic helper MACROS
 *
 * @brief These are Generic helper macros used at various places.
 * @{
 */
/* RETRY_COUNT should be atleast MIN_USLEEP_10US
 * so that RETRY_COUNT/MIN_USLEEP_10US will result in
 * atleast 1 iteration.
 */
#define RETRY_COUNT	1000U
#define RETRY_DELAY	1U
#define OSI_DELAY_2US		2U
#define OSI_DELAY_4US		4U
#define OSI_DELAY_10US		10U
#ifndef OSI_STRIPPED_LIB
#define OSI_DELAY_100US		100U
#endif
#define OSI_DELAY_200US		200U
#define OSI_DELAY_500US		500U
#define OSI_DELAY_1000US	1000U
#define OSI_DELAY_10000US	10000U
#define OSI_DELAY_30000US	30000U
#define OSI_DELAY_1000000US	1000000U

/** @} */

/** \cond DO_NOT_DOCUMENT */
/**
 * @brief osi_readl_poll_timeout - Periodically poll an address until
 * a condition is met or a timeout occurs
 *
 * @param[in] addr: Memory mapped address.
 * @param[in] osi_core: OSI core private data structure.
 * @param[in] lmask: input mask to be masked against register value for poll condition.
 * @param[in] rmask: expected output value to be compared against masked register value
 * with lmask for poll condition.
 * @param[in] delay_us: Maximum time to sleep between reads in us.
 * @param[in] retry: Retry count.

 * @note Physical address has to be memmory mapped.
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
/* note: all users of osi_readl_poll_timeout are calling delay_us with 1us.
 * if delay_us > MIN_USLEEP_10US, then min_delay can be adjusted to input param instead.
 * currently adding this check to avoid logical dead code
 */
static inline s32 osi_readl_poll_timeout(void *addr, struct osi_core_priv_data *osi_core,
					     u32 lmask, u32 rmask, u32 delay_us,
					     u32 retry)
{
	u32 once = 0;
	u32 total_delay = (delay_us) * (retry);
	u16 min_delay = MIN_USLEEP_10US;
	u32 elapsed_delay = 0;
	s32 ret = -1;
	u32 val;

	while (elapsed_delay < total_delay) {
		val = osi_readl((u8 *)addr);
		if ((val & lmask) == rmask) {
			ret = 0;
			break;
		}
		if (once == 0U) {
			osi_core->osd_ops.udelay(OSI_DELAY_1US);
			once = 1U;
			elapsed_delay += 1U;
		} else {
			osi_core->osd_ops.usleep(min_delay);
			elapsed_delay &= (u32)INT_MAX;
			elapsed_delay += min_delay;
		}
	}

	return ret;
}
/** \endcond */

#endif /* INCLUDED_CORE_LOCAL_H */
