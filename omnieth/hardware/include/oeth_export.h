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

#ifndef INCLUDED_OETHRM_EXPORT_H
#define INCLUDED_OETHRM_EXPORT_H

#include <oeth_type.h>

/**
 * @addtogroup Helper MACROS
 *
 * @brief EQOS generic helper MACROS.
 * @{
 */
/**
 * @brief size of GCL-256
 */
#define OSI_GCL_SIZE_256		256U
/**
 * @brief Maximum Traffic Classes supported
 */
#define OSI_MAX_TC_NUM			8U
/**
 * @brief Ethernet Address length
 */
#define OSI_ETH_ALEN			6U
/** @} */

/**
 * @brief FRP data matching length
 */
#define OSI_FRP_MATCH_DATA_MAX		12U
#define OSI_FRP_MATCH_DATA_MIN		1U
/**
 * @brief Maximum FRP indexes
 */
#define OSI_FRP_ID_MAX			0xFF

/**
 * @addtogroup MTL queue operation mode
 *
 * @brief MTL queue operation mode options
 * @{
 */
/** @brief MTL queue operation mode is AVB */
#define OSI_MTL_QUEUE_AVB	0x1U
/** @brief MTL queue operation mode is enable and
 * credit control is disabled */
#define OSI_MTL_QUEUE_ENABLE	0x2U
#define OSI_MTL_QUEUE_MODEMAX	0x3U
#ifndef OSI_STRIPPED_LIB
#define OSI_MAX_NUM_CHANS		48U
#endif
/** @} */

/**
 * @addtogroup EQOS_MTL MTL queue AVB algorithm mode
 *
 * @brief MTL AVB queue algorithm type
 * @{
 */
/** @brief AVB algorithm mode is CBS */
#define OSI_MTL_TXQ_AVALG_CBS	1U
/** @brief AVB algorithm mode is Strict Priority */
#define OSI_MTL_TXQ_AVALG_SP	0U
/** @} */

#ifndef OSI_STRIPPED_LIB
/**
 * @addtogroup Helper MACROS
 *
 * @brief EQOS generic helper MACROS.
 * @{
 */
/* L2 DA filter mode(enable/disable) */
#define OSI_OPER_EN_L2_DA_INV		OSI_BIT(4)
#define OSI_OPER_DIS_L2_DA_INV		OSI_BIT(5)
/** @} */
#endif /* !OSI_STRIPPED_LIB */

#pragma pack(push, 1)
/**
 * @brief FRP command structure for OSD to OSI
 */
struct osi_core_frp_cmd {
	/** FRP Command type
	 * Valid values are OETHRM_PIF$OSI_FRP_CMD_ADD or
	 * OETHRM_PIF$OSI_FRP_CMD_UPDATE or
	 * OETHRM_PIF$OSI_FRP_CMD_DEL */
	u32 cmd;
	/** OSD FRP ID, valid values are from 0 to OETHRM_PIF$OSI_FRP_ID_MAX */
	s32 frp_id;
	/** OSD match data type
	 * valid values are from OETHRM_PIF$OSI_FRP_MATCH_NORMAL
	 * to OETHRM_PIF$OSI_FRP_MATCH_VLAN*/
	u8 match_type;
	/** OSD match data */
	u8 match[OSI_FRP_MATCH_DATA_MAX];
	/** OSD match data length
	 * valid value is from OETHRM_PIF$OSI_FRP_MATCH_DATA_MIN to
	 * OETHRM_PIF$OSI_FRP_MATCH_DATA_MAX
	 */
	u8 match_length;
	/** OSD Offset
	 * Valid values are from 0 to (OETHRM_PIF$OSI_FRP_OFFSET_MAX-1)
	 */
	u8 offset;
	/** OSD FRP filter mode flag
	 * Valid values are from OETHRM_PIF$OSI_FRP_MODE_ROUTE
	 * to OETHRM_PIF$OSI_FRP_MODE_IM_LINK*/
	u8 filter_mode;
	/** OSD FRP Link ID
	 * valid values are from 0 to OETHRM_PIF$OSI_FRP_ID_MAX
	 */
	s32 next_frp_id;
	/** OSD DMA Channel Selection
	 * Bit selection of DMA channels to route the frame
	 * Bit[0] - DMA channel 0
	 * ..
	 * Bit [N] - DMA channel N] */
	u64 dma_sel;
	/** OSD DCHT */
	u8 dcht;
};

/**
 * @brief OSI Core avb data structure per queue.
 */
struct  osi_core_avb_algorithm {
	/** TX Queue/TC index
	 * valid range  0 to OETHRM_PIF$OSI_MGBE_MAX_NUM_QUEUES for MGBE
	 * valid range 0 to OETHRM_PIF$OSI_EQOS_MAX_NUM_CHANS for EQOS */
	u32 qindex;
	/** CBS Algorithm is either OETHRM_PIF$OSI_MTL_TXQ_AVALG_CBS or
	 * OETHRM_PIF$OSI_MTL_TXQ_AVALG_SP */
	u32 algo;
	/** When this bit is set, the accumulated credit parameter in the
	 * credit-based shaper algorithm logic is not reset to zero when
	 * there is positive credit and no packet to transmit in the channel.
	 *
	 * Expected values are enable(1) or disable(0) */
	u32 credit_control;
	/** idleSlopeCredit value required for CBS
	 * Max value for EQOS - 0x000FFFFFU
	 * Max value for MGBE - 0x001FFFFFU */
	u32 idle_slope;
	/** sendSlopeCredit value required for CBS
	 * Max value for EQOS - 0x0000FFFFU
	 * Max value for MGBE - 0x00003FFFU */
	u32 send_slope;
	/** hiCredit value required for CBS
	 * Max value - 0x1FFFFFFFU */
	u32 hi_credit;
	/** lowCredit value required for CBS
	 * Max value - 0x1FFFFFFFU */
	u32 low_credit;
	/** Transmit queue operating mode
	 * either disable(0) or OETHRM_PIF$OSI_MTL_QUEUE_AVB or
	 * OETHRM_PIF$OSI_MTL_QUEUE_AVB*/
	u32 oper_mode;
	/** Traffic Classes from 0 to OETHRM_PIF$OSI_MAX_TC_NUM-1 */
	u32 tcindex;
};

/**
 * @brief OSI Core EST structure
 */
struct osi_est_config {
	/** Valid values ate 0 and 1
	 * o to disable EST and 1 to enable EST */
	u32 en_dis;
	/** 64 bit base time register
	 * if both values are 0, take ptp time to avoid BTRE
	 * index 0 for nsec, index 1 for sec
	 * Valid values are from 0 to UNIT32_MAX for each index
	 */
	u32 btr[2];
	/** 64 bit base time offset index 0 for nsec, index 1 for sec
	 * 32 bits for Seconds, 32 bits for nanoseconds (max 10^9) */
	u32 btr_offset[2];
	/** 40 bits cycle time register, index 0 for nsec, index 1 for sec
	 * 8 bits for Seconds, 32 bits for nanoseconds (max 10^9) */
	u32 ctr[2];
	/** Configured Time Interval width(24 bits) + 7 bits
	 * extension register
	 * Valid values are from 1 to 0x7FFFFFFFU*/
	u32 ter;
	/** size of the gate control list Max 256 entries
	 * valid value range (1-255)*/
	u32 llr;
	/** data array 8 bit gate op + 24 execution time
	 * MGBE HW support GCL depth 256 */
	u32 gcl[OSI_GCL_SIZE_256];
};

/**
 * @brief OSI Core FPE structure
 */
struct osi_fpe_config {
	/** Queue Mask 1 - preemption 0 - express
	 * bit representation for each queue
	 * valud values are from 1 to 0xFF*/
	u32 tx_queue_preemption_enable;
	/** residual queues for all preemptable packets  which are not filtered
	 * based on user priority or SA-DA
	 * Value range for EQOS 1 to OETHRM_PIF$OSI_EQOS_MAX_NUM_QUEUES-1
	 * Value range for MGBE 1 to OETHRM_PIF$OSI_MGBE_MAX_NUM_QUEUES-1 */
	u32 rq;
};

/**
 * @brief OSI Core error stats structure
 */
struct osi_stats {
	/** Constant Gate Control Error
	 * Valid values are from 0 to UINT64_MAX */
	u64 const_gate_ctr_err;
	/** Head-Of-Line Blocking due to Scheduling
	 * Valid values are from 0 to UINT64_MAX */
	u64 head_of_line_blk_sch;
	/** Per TC Schedule Error
	 * Valid values are from 0 to UINT64_MAX */
	u64 hlbs_q[OSI_MAX_TC_NUM];
	/** Head-Of-Line Blocking due to Frame Size
	 * Valid values are from 0 to UINT64_MAX */
	u64 head_of_line_blk_frm;
	/** Per TC Frame Size Error/
	 * Valid values are from 0 to UINT64_MAX */
	u64 hlbf_q[OSI_MAX_TC_NUM];
	/** BTR Error
	 * Valid values are from 0 to UINT64_MAX */
	u64 base_time_reg_err;
	/** Switch to Software Owned List Complete
	 * Valid values are from 0 to UINT64_MAX */
	u64 sw_own_list_complete;
#ifndef OSI_STRIPPED_LIB
	/** IP Header Error */
	u64 mgbe_ip_header_err;
	/** Jabber time out Error */
	u64 mgbe_jabber_timeout_err;
	/** Payload Checksum Error */
	u64 mgbe_payload_cs_err;
	/** Under Flow Error */
	u64 mgbe_tx_underflow_err;
	/** RX buffer unavailable irq count */
	u64 rx_buf_unavail_irq_n[OSI_MAX_NUM_CHANS];
	/** Transmit Process Stopped irq count */
	u64 tx_proc_stopped_irq_n[OSI_MAX_NUM_CHANS];
	/** Transmit Buffer Unavailable irq count */
	u64 tx_buf_unavail_irq_n[OSI_MAX_NUM_CHANS];
	/** Receive Process Stopped irq count */
	u64 rx_proc_stopped_irq_n[OSI_MAX_NUM_CHANS];
	/** Receive Watchdog Timeout irq count */
	u64 rx_watchdog_irq_n;
	/** Fatal Bus Error irq count */
	u64 fatal_bus_error_irq_n;
	/** lock fail count node addition */
	u64 ts_lock_add_fail;
	/** lock fail count node removal */
	u64 ts_lock_del_fail;
#endif
};

/**
 * @brief osi_mmc_counters - The structure to hold RMON counter values
 */
struct osi_mmc_counters {
	/** This counter provides the number of bytes transmitted, exclusive of
	 * preamble and retried bytes, in good and bad packets */
	u64 mmc_tx_octetcount_gb;
	/** This counter provides upper 32 bits of transmitted octet count */
	u64 mmc_tx_octetcount_gb_h;
	/** This counter provides the number of good and
	 * bad packets transmitted, exclusive of retried packets */
	u64 mmc_tx_framecount_gb;
	/** This counter provides upper 32 bits of transmitted good and bad
	 * packets count */
	u64 mmc_tx_framecount_gb_h;
	/** This counter provides number of good broadcast
	 * packets transmitted */
	u64 mmc_tx_broadcastframe_g;
	/** This counter provides upper 32 bits of transmitted good broadcast
	 * packets count */
	u64 mmc_tx_broadcastframe_g_h;
	/** This counter provides number of good multicast
	 * packets transmitted */
	u64 mmc_tx_multicastframe_g;
	/** This counter provides upper 32 bits of transmitted good multicast
	 * packet count */
	u64 mmc_tx_multicastframe_g_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 64 bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_64_octets_gb;
	/** This counter provides upper 32 bits of transmitted 64 octet size
	 * good and bad packets count */
	u64 mmc_tx_64_octets_gb_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 65-127 bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_65_to_127_octets_gb;
	/** Provides upper 32 bits of transmitted 65-to-127 octet size good and
	 * bad packets count */
	u64 mmc_tx_65_to_127_octets_gb_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 128-255 bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_128_to_255_octets_gb;
	/** This counter provides upper 32 bits of transmitted 128-to-255
	 * octet size good and bad packets count */
	u64 mmc_tx_128_to_255_octets_gb_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 256-511 bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_256_to_511_octets_gb;
	/** This counter provides upper 32 bits of transmitted 256-to-511
	 * octet size good and bad packets count. */
	u64 mmc_tx_256_to_511_octets_gb_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 512-1023 bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_512_to_1023_octets_gb;
	/** This counter provides upper 32 bits of transmitted 512-to-1023
	 * octet size good and bad packets count.*/
	u64 mmc_tx_512_to_1023_octets_gb_h;
	/** This counter provides the number of good and bad packets
	 * transmitted with length 1024-max bytes, exclusive of preamble and
	 * retried packets */
	u64 mmc_tx_1024_to_max_octets_gb;
	/** This counter provides upper 32 bits of transmitted 1024-tomaxsize
	 * octet size good and bad packets count. */
	u64 mmc_tx_1024_to_max_octets_gb_h;
	/** This counter provides the number of good and bad unicast packets */
	u64 mmc_tx_unicast_gb;
	/** This counter provides upper 32 bits of transmitted good bad
	 * unicast packets count */
	u64 mmc_tx_unicast_gb_h;
	/** This counter provides the number of good and bad
	 * multicast packets */
	u64 mmc_tx_multicast_gb;
	/** This counter provides upper 32 bits of transmitted good bad
	 * multicast packets count */
	u64 mmc_tx_multicast_gb_h;
	/** This counter provides the number of good and bad
	 * broadcast packets */
	u64 mmc_tx_broadcast_gb;
	/** This counter provides upper 32 bits of transmitted good bad
	 * broadcast packets count */
	u64 mmc_tx_broadcast_gb_h;
	/** This counter provides the number of abort packets due to
	 * underflow error */
	u64 mmc_tx_underflow_error;
	/** This counter provides upper 32 bits of abort packets due to
	 * underflow error */
	u64 mmc_tx_underflow_error_h;
	/** This counter provides the number of successfully transmitted
	 * packets after a single collision in the half-duplex mode */
	u64 mmc_tx_singlecol_g;
	/** This counter provides the number of successfully transmitted
	 * packets after a multi collision in the half-duplex mode */
	u64 mmc_tx_multicol_g;
	/** This counter provides the number of successfully transmitted
	 * after a deferral in the half-duplex mode */
	u64 mmc_tx_deferred;
	/** This counter provides the number of packets aborted because of
	 * late collision error */
	u64 mmc_tx_latecol;
	/** This counter provides the number of packets aborted because of
	 * excessive (16) collision errors */
	u64 mmc_tx_exesscol;
	/** This counter provides the number of packets aborted because of
	 * carrier sense error (no carrier or loss of carrier) */
	u64 mmc_tx_carrier_error;
	/** This counter provides the number of bytes transmitted,
	 * exclusive of preamble, only in good packets */
	u64 mmc_tx_octetcount_g;
	/** This counter provides upper 32 bytes of bytes transmitted,
	 * exclusive of preamble, only in good packets */
	u64 mmc_tx_octetcount_g_h;
	/** This counter provides the number of good packets transmitted */
	u64 mmc_tx_framecount_g;
	/** This counter provides upper 32 bytes of good packets transmitted */
	u64 mmc_tx_framecount_g_h;
	/** This counter provides the number of packets aborted because of
	 * excessive deferral error
	 * (deferred for more than two max-sized packet times) */
	u64 mmc_tx_excessdef;
	/** This counter provides the number of good Pause
	 * packets transmitted */
	u64 mmc_tx_pause_frame;
	/** This counter provides upper 32 bytes of good Pause
	 * packets transmitted */
	u64 mmc_tx_pause_frame_h;
	/** This counter provides the number of good VLAN packets transmitted */
	u64 mmc_tx_vlan_frame_g;
	/** This counter provides upper 32 bytes of good VLAN packets
	 * transmitted */
	u64 mmc_tx_vlan_frame_g_h;
	/** This counter provides the number of packets transmitted without
	 * errors and with length greater than the maxsize (1,518 or 1,522 bytes
	 * for VLAN tagged packets; 2000 bytes */
	u64 mmc_tx_osize_frame_g;
	/** This counter provides the number of good and bad packets received */
	u64 mmc_rx_framecount_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received */
	u64 mmc_rx_framecount_gb_h;
	/** This counter provides the number of bytes received by DWC_ther_qos,
	 * exclusive of preamble, in good and bad packets */
	u64 mmc_rx_octetcount_gb;
	/** This counter provides upper 32 bytes of bytes received by
	 * DWC_ether_qos, exclusive of preamble, in good and bad packets */
	u64 mmc_rx_octetcount_gb_h;
	/** This counter provides the number of bytes received by DWC_ether_qos,
	 * exclusive of preamble, in good and bad packets */
	u64 mmc_rx_octetcount_g;
	/** This counter provides upper 32 bytes of bytes received by
	 * DWC_ether_qos, exclusive of preamble, in good and bad packets */
	u64 mmc_rx_octetcount_g_h;
	/** This counter provides the number of good
	 * broadcast packets received */
	u64 mmc_rx_broadcastframe_g;
	/** This counter provides upper 32 bytes of good
	 * broadcast packets received */
	u64 mmc_rx_broadcastframe_g_h;
	/** This counter provides the number of good
	 * multicast packets received */
	u64 mmc_rx_multicastframe_g;
	/** This counter provides upper 32 bytes of good
	 * multicast packets received */
	u64 mmc_rx_multicastframe_g_h;
	/** This counter provides the number of packets
	 * received with CRC error */
	u64 mmc_rx_crc_error;
	/** This counter provides upper 32 bytes of packets
	 * received with CRC error */
	u64 mmc_rx_crc_error_h;
	/** This counter provides the number of packets received with
	 * alignment (dribble) error. It is valid only in 10/100 mode */
	u64 mmc_rx_align_error;
	/** This counter provides the number of packets received  with
	 * runt (length less than 64 bytes and CRC error) error */
	u64 mmc_rx_runt_error;
	/** This counter provides the number of giant packets received  with
	 * length (including CRC) greater than 1,518 bytes (1,522 bytes for
	 * VLAN tagged) and with CRC error */
	u64 mmc_rx_jabber_error;
	/** This counter provides the number of packets received  with length
	 * less than 64 bytes, without any errors */
	u64 mmc_rx_undersize_g;
	/** This counter provides the number of packets received  without error,
	 * with length greater than the maxsize */
	u64 mmc_rx_oversize_g;
	/** This counter provides the number of good and bad packets received
	 * with length 64 bytes, exclusive of the preamble */
	u64 mmc_rx_64_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 64 bytes, exclusive of the preamble */
	u64 mmc_rx_64_octets_gb_h;
	/** This counter provides the number of good and bad packets received
	 * with length 65-127 bytes, exclusive of the preamble */
	u64 mmc_rx_65_to_127_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 65-127 bytes, exclusive of the preamble */
	u64 mmc_rx_65_to_127_octets_gb_h;
	/** This counter provides the number of good and bad packets received
	 * with length 128-255 bytes, exclusive of the preamble */
	u64 mmc_rx_128_to_255_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 128-255 bytes, exclusive of the preamble */
	u64 mmc_rx_128_to_255_octets_gb_h;
	/** This counter provides the number of good and bad packets received
	 * with length 256-511 bytes, exclusive of the preamble */
	u64 mmc_rx_256_to_511_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 256-511 bytes, exclusive of the preamble */
	u64 mmc_rx_256_to_511_octets_gb_h;
	/** This counter provides the number of good and bad packets received
	 * with length 512-1023 bytes, exclusive of the preamble */
	u64 mmc_rx_512_to_1023_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 512-1023 bytes, exclusive of the preamble */
	u64 mmc_rx_512_to_1023_octets_gb_h;
	/** This counter provides the number of good and bad packets received
	 * with length 1024-maxbytes, exclusive of the preamble */
	u64 mmc_rx_1024_to_max_octets_gb;
	/** This counter provides upper 32 bytes of good and bad packets
	 * received with length 1024-maxbytes, exclusive of the preamble */
	u64 mmc_rx_1024_to_max_octets_gb_h;
	/** This counter provides the number of good unicast packets received */
	u64 mmc_rx_unicast_g;
	/** This counter provides upper 32 bytes of good unicast packets
	 * received */
	u64 mmc_rx_unicast_g_h;
	/** This counter provides the number of packets received  with length
	 * error (Length Type field not equal to packet size), for all packets
	 * with valid length field */
	u64 mmc_rx_length_error;
	/** This counter provides upper 32 bytes of packets received  with
	 * length error (Length Type field not equal to packet size), for all
	 * packets with valid length field */
	u64 mmc_rx_length_error_h;
	/** This counter provides the number of packets received  with length
	 * field not equal to the valid packet size (greater than 1,500 but
	 * less than 1,536) */
	u64 mmc_rx_outofrangetype;
	/** This counter provides upper 32 bytes of packets received  with
	 * length field not equal to the valid packet size (greater than 1,500
	 * but less than 1,536) */
	u64 mmc_rx_outofrangetype_h;
	/** This counter provides the number of good and valid Pause packets
	 * received */
	u64 mmc_rx_pause_frames;
	/** This counter provides upper 32 bytes of good and valid Pause packets
	 * received */
	u64 mmc_rx_pause_frames_h;
	/** This counter provides the number of missed received packets
	 * because of FIFO overflow in DWC_ether_qos */
	u64 mmc_rx_fifo_overflow;
	/** This counter provides upper 32 bytes of missed received packets
	 * because of FIFO overflow in DWC_ether_qos */
	u64 mmc_rx_fifo_overflow_h;
	/** This counter provides the number of good and bad VLAN packets
	 * received */
	u64 mmc_rx_vlan_frames_gb;
	/** This counter provides upper 32 bytes of good and bad VLAN packets
	 * received */
	u64 mmc_rx_vlan_frames_gb_h;
	/** This counter provides the number of packets received with error
	 * because of watchdog timeout error */
	u64 mmc_rx_watchdog_error;
	/** This counter provides the number of packets received with Receive
	 * error or Packet Extension error on the GMII or MII interface */
	u64 mmc_rx_receive_error;
	/** This counter provides the number of packets received with Receive
	 * error or Packet Extension error on the GMII or MII interface */
	u64 mmc_rx_ctrl_frames_g;
	/** This counter provides the number of microseconds Tx LPI is asserted
	 * in the MAC controller */
	u64 mmc_tx_lpi_usec_cntr;
	/** This counter provides the number of times MAC controller has
	 * entered Tx LPI. */
	u64 mmc_tx_lpi_tran_cntr;
	/** This counter provides the number of microseconds Rx LPI is asserted
	 * in the MAC controller */
	u64 mmc_rx_lpi_usec_cntr;
	/** This counter provides the number of times MAC controller has
	 * entered Rx LPI.*/
	u64 mmc_rx_lpi_tran_cntr;
	/** This counter provides the number of good IPv4 datagrams received
	 * with the TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv4_gd;
	/** This counter provides upper 32 bytes of good IPv4 datagrams received
	 * with the TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv4_gd_h;
	/** RxIPv4 Header Error Packets */
	u64 mmc_rx_ipv4_hderr;
	/** RxIPv4 of upper 32 bytes of Header Error Packets */
	u64 mmc_rx_ipv4_hderr_h;
	/** This counter provides the number of IPv4 datagram packets received
	 * that did not have a TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv4_nopay;
	/** This counter provides upper 32 bytes of IPv4 datagram packets
	 * received that did not have a TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv4_nopay_h;
	/** This counter provides the number of good IPv4 datagrams received
	 * with fragmentation */
	u64 mmc_rx_ipv4_frag;
	/** This counter provides upper 32 bytes of good IPv4 datagrams received
	 * with fragmentation */
	u64 mmc_rx_ipv4_frag_h;
	/** This counter provides the number of good IPv4 datagrams received
	 * that had a UDP payload with checksum disabled */
	u64 mmc_rx_ipv4_udsbl;
	/** This counter provides upper 32 bytes of good IPv4 datagrams received
	 * that had a UDP payload with checksum disabled */
	u64 mmc_rx_ipv4_udsbl_h;
	/** This counter provides the number of good IPv6 datagrams received
	 * with the TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv6_gd_octets;
	/** This counter provides upper 32 bytes of good IPv6 datagrams received
	 * with the TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv6_gd_octets_h;
	/** This counter provides the number of IPv6 datagrams received
	 * with header (length or version mismatch) errors */
	u64 mmc_rx_ipv6_hderr_octets;
	/** This counter provides the number of IPv6 datagrams received
	 * with header (length or version mismatch) errors */
	u64 mmc_rx_ipv6_hderr_octets_h;
	/** This counter provides the number of IPv6 datagram packets received
	 * that did not have a TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv6_nopay_octets;
	/** This counter provides upper 32 bytes of IPv6 datagram packets
	 * received that did not have a TCP, UDP, or ICMP payload */
	u64 mmc_rx_ipv6_nopay_octets_h;
	/* Protocols */
	/** This counter provides the number of good IP datagrams received by
	 * DWC_ether_qos with a good UDP payload */
	u64 mmc_rx_udp_gd;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * by DWC_ether_qos with a good UDP payload */
	u64 mmc_rx_udp_gd_h;
	/** This counter provides the number of good IP datagrams received by
	 * DWC_ether_qos with a good UDP payload. This counter is not updated
	 * when the RxIPv4_UDP_Checksum_Disabled_Packets counter is
	 * incremented */
	u64 mmc_rx_udp_err;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * by DWC_ether_qos with a good UDP payload. This counter is not updated
	 * when the RxIPv4_UDP_Checksum_Disabled_Packets counter is
	 * incremented */
	u64 mmc_rx_udp_err_h;
	/** This counter provides the number of good IP datagrams received
	 * with a good TCP payload */
	u64 mmc_rx_tcp_gd;
	/** This counter provides the number of good IP datagrams received
	 * with a good TCP payload */
	u64 mmc_rx_tcp_gd_h;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * with a good TCP payload */
	u64 mmc_rx_tcp_err;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * with a good TCP payload */
	u64 mmc_rx_tcp_err_h;
	/** This counter provides the number of good IP datagrams received
	 * with a good ICMP payload */
	u64 mmc_rx_icmp_gd;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * with a good ICMP payload */
	u64 mmc_rx_icmp_gd_h;
	/** This counter provides the number of good IP datagrams received
	 * whose ICMP payload has a checksum error */
	u64 mmc_rx_icmp_err;
	/** This counter provides upper 32 bytes of good IP datagrams received
	 * whose ICMP payload has a checksum error */
	u64 mmc_rx_icmp_err_h;
	/** This counter provides the number of bytes received by DWC_ether_qos
	 * in good IPv4 datagrams encapsulating TCP, UDP, or ICMP data.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_gd_octets;
	/** This counter provides upper 32 bytes received by DWC_ether_qos
	 * in good IPv4 datagrams encapsulating TCP, UDP, or ICMP data.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_gd_octets_h;
	/** This counter provides the number of bytes received in IPv4 datagram
	 * with header errors (checksum, length, version mismatch). The value
	 * in the Length field of IPv4 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_hderr_octets;
	/** This counter provides upper 32 bytes received in IPv4 datagram
	 * with header errors (checksum, length, version mismatch). The value
	 * in the Length field of IPv4 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_hderr_octets_h;
	/** This counter provides the number of bytes received in IPv4 datagram
	 * that did not have a TCP, UDP, or ICMP payload. The value in the
	 * Length field of IPv4 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_nopay_octets;
	/** This counter provides upper 32 bytes received in IPv4 datagram
	 * that did not have a TCP, UDP, or ICMP payload. The value in the
	 * Length field of IPv4 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv4_nopay_octets_h;
	/** This counter provides the number of bytes received in fragmented
	 * IPv4 datagrams. The value in the Length field of IPv4 header is
	 * used to update this counter. (Ethernet header, FCS, pad, or IP pad
	 * bytes are not included in this counter */
	u64 mmc_rx_ipv4_frag_octets;
	/** This counter provides upper 32 bytes received in fragmented
	 * IPv4 datagrams. The value in the Length field of IPv4 header is
	 * used to update this counter. (Ethernet header, FCS, pad, or IP pad
	 * bytes are not included in this counter */
	u64 mmc_rx_ipv4_frag_octets_h;
	/** This counter provides the number of bytes received in a UDP segment
	 * that had the UDP checksum disabled. This counter does not count IP
	 * Header bytes. (Ethernet header, FCS, pad, or IP pad bytes are not
	 * included in this counter */
	u64 mmc_rx_ipv4_udsbl_octets;
	/** This counter provides upper 32 bytes received in a UDP segment
	 * that had the UDP checksum disabled. This counter does not count IP
	 * Header bytes. (Ethernet header, FCS, pad, or IP pad bytes are not
	 * included in this counter */
	u64 mmc_rx_ipv4_udsbl_octets_h;
	/** This counter provides the number of bytes received in good IPv6
	 * datagrams encapsulating TCP, UDP, or ICMP data. (Ethernet header,
	 * FCS, pad, or IP pad bytes are not included in this counter */
	u64 mmc_rx_ipv6_gd;
	/** This counter provides upper 32 bytes received in good IPv6
	 * datagrams encapsulating TCP, UDP, or ICMP data. (Ethernet header,
	 * FCS, pad, or IP pad bytes are not included in this counter */
	u64 mmc_rx_ipv6_gd_h;
	/** This counter provides the number of bytes received in IPv6 datagrams
	 * with header errors (length, version mismatch). The value in the
	 * Length field of IPv6 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included in
	 * this counter */
	u64 mmc_rx_ipv6_hderr;
	/** This counter provides upper 32 bytes received in IPv6 datagrams
	 * with header errors (length, version mismatch). The value in the
	 * Length field of IPv6 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included in
	 * this counter */
	u64 mmc_rx_ipv6_hderr_h;
	/** This counter provides the number of bytes received in IPv6
	 * datagrams that did not have a TCP, UDP, or ICMP payload. The value
	 * in the Length field of IPv6 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv6_nopay;
	/** This counter provides upper 32 bytes received in IPv6
	 * datagrams that did not have a TCP, UDP, or ICMP payload. The value
	 * in the Length field of IPv6 header is used to update this counter.
	 * (Ethernet header, FCS, pad, or IP pad bytes are not included
	 * in this counter */
	u64 mmc_rx_ipv6_nopay_h;
	/* Protocols */
	/** This counter provides the number of bytes received in a good UDP
	 * segment. This counter does not count IP header bytes */
	u64 mmc_rx_udp_gd_octets;
	/** This counter provides upper 32 bytes received in a good UDP
	 * segment. This counter does not count IP header bytes */
	u64 mmc_rx_udp_gd_octets_h;
	/** This counter provides the number of bytes received in a UDP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_udp_err_octets;
	/** This counter provides upper 32 bytes received in a UDP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_udp_err_octets_h;
	/** This counter provides the number of bytes received in a good
	 * TCP segment. This counter does not count IP header bytes */
	u64 mmc_rx_tcp_gd_octets;
	/** This counter provides upper 32 bytes received in a good
	 * TCP segment. This counter does not count IP header bytes */
	u64 mmc_rx_tcp_gd_octets_h;
	/** This counter provides the number of bytes received in a TCP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_tcp_err_octets;
	/** This counter provides upper 32 bytes received in a TCP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_tcp_err_octets_h;
	/** This counter provides the number of bytes received in a good
	 * ICMP segment. This counter does not count IP header bytes */
	u64 mmc_rx_icmp_gd_octets;
	/** This counter provides upper 32 bytes received in a good
	 * ICMP segment. This counter does not count IP header bytes */
	u64 mmc_rx_icmp_gd_octets_h;
	/** This counter provides the number of bytes received in a ICMP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_icmp_err_octets;
	/** This counter provides upper 32 bytes received in a ICMP
	 * segment that had checksum errors. This counter does not count
	 * IP header bytes */
	u64 mmc_rx_icmp_err_octets_h;
	/** This counter provides the number of additional mPackets
	 * transmitted due to preemption */
	u64 mmc_tx_fpe_frag_cnt;
	/** This counter provides the count of number of times a hold
	 *  request is given to MAC */
	u64 mmc_tx_fpe_hold_req_cnt;
	/** This counter provides the number of MAC frames with reassembly
	 *  errors on the Receiver, due to mismatch in the fragment
	 *  count value */
	u64 mmc_rx_packet_reass_err_cnt;
	/** This counter the number of received MAC frames rejected
	 *  due to unknown SMD value and MAC frame fragments rejected due
	 *  to arriving with an SMD-C when there was no preceding preempted
	 *  frame */
	u64 mmc_rx_packet_smd_err_cnt;
	/** This counter provides the number of MAC frames that were
	 * successfully reassembled and delivered to MAC */
	u64 mmc_rx_packet_asm_ok_cnt;
	/** This counter provides the number of additional mPackets received
	 *   due to preemption */
	u64 mmc_rx_fpe_fragment_cnt;
};

#pragma pack(pop)
#endif /* INCLUDED_OETHRM_EXPORT_H */
