/*
 * Copyright (c) 2026-2030, Omni. All rights reserved.
 *
 * DWC_macsec 1.05a Register Definitions
 * Based on Synopsys DWC_macsec Single Port AC Security Module Databook
 *
 * This file replaces Omni's LUT-based register abstraction with
 * Omni's DWC_macsec 1.05a SAI/SAD direct table access registers.
 */

#ifndef INCLUDED_DWC_MACSEC_REG_H
#define INCLUDED_DWC_MACSEC_REG_H

/* ============================================================
 * DWC_macsec Hardware Configuration (from ComponentConfiguration)
 * ============================================================
 * TX SC: 16, TX SA per SC: 2, Total TX SA: 32
 * RX SC: 128, RX SA per SC: 4, Total RX SA: 512
 * Max Key: AES-256, XPN: enabled
 * Anti-Replay Window: Logarithmic
 * AES Key Contexts: 546 (TX 32 + RX 512 + FIPS 1 + SPARE 1)
 * Perf Rate: up to 25Gbps, Dual-IF: enabled
 * Data Interface: XLGMII (128-bit)
 * ============================================================ */

#define DWC_MACSEC_TX_MAX_SC		16U
#define DWC_MACSEC_TX_SA_PER_SC		2U
#define DWC_MACSEC_TX_MAX_SA		32U
#define DWC_MACSEC_RX_MAX_SC		128U
#define DWC_MACSEC_RX_SA_PER_SC		4U
#define DWC_MACSEC_RX_MAX_SA		512U
#define DWC_MACSEC_AES_KEY_CTX		546U

/* TX SALT entries (XPN): one per TX SA, depth = DWC_MACSEC_TX_MAX_SA */
#define DWC_MACSEC_TX_SALT_MAX		32U
/* RX SALT entries (XPN): one per RX SA, depth = DWC_MACSEC_RX_MAX_SA */
#define DWC_MACSEC_RX_SALT_MAX		512U
/* RX ARW entries: one per RX SA */
#define DWC_MACSEC_RX_ARW_MAX		512U

/* ============================================================
 * TCAM vs Internal FFs mode selection
 * When DWC_MACSEC_USE_TCAM is defined, SAI lookup uses external
 * TCAM memory. TCAM mode has higher datapath latency so polling
 * retries are increased. The SAI_PRG register interface is
 * identical; the IP internally bridges APB writes to the TCAM
 * O_sai_tcam_* hardware port signals.
 * ============================================================ */

#ifdef DWC_MACSEC_USE_TCAM
/* TCAM mode: higher latency (~2x), increase retry budget */
#define DWC_MACSEC_SAI_RETRY_COUNT	2000U
#define DWC_MACSEC_SAI_RETRY_DELAY	2U
#else
/* Internal FFs mode: default timing */
#define DWC_MACSEC_SAI_RETRY_COUNT	1000U
#define DWC_MACSEC_SAI_RETRY_DELAY	1U
#endif /* DWC_MACSEC_USE_TCAM */

/* ============================================================
 * macsec_apb_cfg register offsets
 * ============================================================ */

/** @addtogroup DWC_MACSEC core registers
 * @{ */
#define DWC_MACSEC_CORE_VER_NUM		0x00
#define DWC_MACSEC_CORE_VER_TYPE	0x04
#define DWC_MACSEC_IP_CONTROL		0x08
#define DWC_MACSEC_CFG_MIN_IPG		0x0C
#define DWC_MACSEC_IP_VLAN		0x10
#define DWC_MACSEC_ETHERTYPE_REPLACE	0x18
/** @} */

/** @addtogroup DWC_MACSEC RX SAI (Security Association Identifier) registers
 * @{ */
#define DWC_MACSEC_RX_LT_SCI_LOW	0x1C
#define DWC_MACSEC_RX_LT_SCI_HIGH	0x20
#define DWC_MACSEC_RX_LT_VLAN_INFO	0x24
#define DWC_MACSEC_RX_SAI_PG		0x28
#define DWC_MACSEC_RX_SAI_PRG		0x2C
#define DWC_MACSEC_RX_SAI_OTHER_FIELDS	0x30
#define DWC_MACSEC_RX_SAI_DATA_FETCH	0x34
/** @} */

/** @addtogroup DWC_MACSEC TX filter and SAI registers
 * @{ */
#define DWC_MACSEC_TX_SRC_FILT_MSB	0x38
#define DWC_MACSEC_TX_SRC_FILT_LSB	0x3C
#define DWC_MACSEC_TX_DST_FILT_MSB	0x40
#define DWC_MACSEC_TX_DST_FILT_LSB	0x44
#define DWC_MACSEC_TX_SAI_PG		0x48
#define DWC_MACSEC_TX_SAI_PRG		0x4C
#define DWC_MACSEC_TX_SAI_OTHER_FIELDS	0x50
#define DWC_MACSEC_TX_SAI_DATA_FETCH	0x54
#define DWC_MACSEC_TX_LT_VLAN_INFO	0x58
#define DWC_MACSEC_TX_SAI_PORT_ID	0x5C
/** @} */

/** @addtogroup DWC_MACSEC TX Active AN and MIB command
 * @{ */
#define DWC_MACSEC_TX_ACTIVE_AN		0x60
#define DWC_MACSEC_MIB_CMD		0x64
#define DWC_MACSEC_MIB_CMD_STAT		0x68
#define DWC_MACSEC_MIB_VALIDATE		0x6C
#define DWC_MACSEC_MIB_WR_DATA_HIGH	0x70
#define DWC_MACSEC_MIB_WR_DATA_LOW	0x74
/** @} */

/** @addtogroup DWC_MACSEC RX SC correlation table
 * @{ */
#define DWC_MACSEC_RX_SC_CORR_PG	0x78
#define DWC_MACSEC_RX_SC_CORR_PRG	0x7C
#define DWC_MACSEC_RX_SC_CORR_DATA_FETCH 0x80
#define DWC_MACSEC_RX_SC_CORR_SC_VALUE	0x84
/** @} */

/** @addtogroup DWC_MACSEC TX SALT table registers (XPN-related)
 * SALT table depth = DWC_MACSEC_TX_SALT_MAX (32 entries).
 * Each entry holds a 96-bit SSCI (across TABLE_0/1/2).
 * @{ */
#define DWC_MACSEC_TX_SALT_PG		0xA0
#define DWC_MACSEC_TX_SALT_PRG		0xA4
#define DWC_MACSEC_TX_SALT_DATA_FETCH	0xA8
#define DWC_MACSEC_TX_SALT_TABLE_0	0xAC
#define DWC_MACSEC_TX_SALT_TABLE_1	0xB0
#define DWC_MACSEC_TX_SALT_TABLE_2	0xB4
/** @} */

/** @addtogroup DWC_MACSEC RX SALT table registers (XPN-related)
 * SALT table depth = DWC_MACSEC_RX_SALT_MAX (512 entries).
 * Each entry holds a 96-bit SSCI (across TABLE_0/1/2).
 * @{ */
#define DWC_MACSEC_RX_SALT_PG		0x88
#define DWC_MACSEC_RX_SALT_PRG		0x8C
#define DWC_MACSEC_RX_SALT_DATA_FETCH	0x90
#define DWC_MACSEC_RX_SALT_TABLE_0	0x94
#define DWC_MACSEC_RX_SALT_TABLE_1	0x98
#define DWC_MACSEC_RX_SALT_TABLE_2	0x9C
/** @} */

/** @addtogroup DWC_MACSEC SALT PRG bit fields (shared TX/RX)
 * @{ */
#define DWC_MACSEC_SALT_PRG_TYPE_WRITE		OSI_BIT(0)
#define DWC_MACSEC_SALT_PRG_TYPE_READ		0U
#define DWC_MACSEC_SALT_PRG_ENTRY_SHIFT		1
/** @} */

/** @addtogroup DWC_MACSEC RX Anti-Replay Window (ARW) registers
 * @{ */
#define DWC_MACSEC_RX_ARW_PG		0xB8
#define DWC_MACSEC_RX_ARW_PRG		0xBC
#define DWC_MACSEC_RX_ARW_DATA_FETCH	0xC0
#define DWC_MACSEC_RX_ARW_TABLE_0	0xC4
/** @} */

/** @addtogroup DWC_MACSEC TX SAD (Security Association Data) registers
 * @{ */
#define DWC_MACSEC_TX_SAD_PG		0xC8
#define DWC_MACSEC_TX_SAD_PRG		0xCC
#define DWC_MACSEC_TX_SAD_DATA_FETCH	0xD0
#define DWC_MACSEC_TX_SAD_0_LOW		0xD4
#define DWC_MACSEC_TX_SAD_0_HIGH	0xD8
#define DWC_MACSEC_TX_SAD_1_LOW		0xDC
#define DWC_MACSEC_TX_SAD_1_HIGH	0xE0
#define DWC_MACSEC_TX_SAD_2_LOW		0xE4
#define DWC_MACSEC_TX_SAD_2_HIGH	0xE8
#define DWC_MACSEC_TX_SAD_3_LOW		0xEC
#define DWC_MACSEC_TX_SAD_3_HIGH	0xF0
/** @} */

/** @addtogroup DWC_MACSEC RX SAD (Security Association Data) registers
 * @{ */
#define DWC_MACSEC_RX_SAD_PG		0xF4
#define DWC_MACSEC_RX_SAD_PRG		0xF8
#define DWC_MACSEC_RX_SAD_DATA_FETCH	0xFC
#define DWC_MACSEC_RX_SAD_0_LOW		0x100
#define DWC_MACSEC_RX_SAD_0_HIGH	0x104
#define DWC_MACSEC_RX_SAD_1_LOW		0x108
#define DWC_MACSEC_RX_SAD_1_HIGH	0x10C
#define DWC_MACSEC_RX_SAD_2_LOW		0x110
#define DWC_MACSEC_RX_SAD_2_HIGH	0x114
/** @} */

/** @addtogroup DWC_MACSEC MIB data buffer registers
 * @{ */
#define DWC_MACSEC_MIB_D_BUF_HIGH(x)	(0x118U + ((x) * 8U))
#define DWC_MACSEC_MIB_D_BUF_LOW(x)	(0x11CU + ((x) * 8U))
/** @} */

/** @addtogroup DWC_MACSEC IRQ registers
 * @{ */
#define DWC_MACSEC_IRQ_GLBL_EN		0x178
#define DWC_MACSEC_IRQ_GLBL_STAT	0x17C
/** @} */

/** @addtogroup DWC_MACSEC Ethertype bypass filters
 * @{ */
#define DWC_MACSEC_ETHERTYPE_FILT(x)	(0x180U + ((x) * 4U))
/** @} */

/** @addtogroup DWC_MACSEC Per-SA TX IRQ registers
 * @{ */
#define DWC_MACSEC_IRQ_TX_SA_EN(x)	(0x190U + ((x) * 8U))
#define DWC_MACSEC_IRQ_TX_SA_STAT(x)	(0x194U + ((x) * 8U))
/** @} */

/* ============================================================
 * MACsec_IP_Control register (0x08) bit fields
 * ============================================================ */

/** @addtogroup DWC_MACSEC_IP_CONTROL bit fields
 * @{ */
#define DWC_MACSEC_CTRL_VLANMODE_SHIFT		29
#define DWC_MACSEC_CTRL_VLANMODE_MASK		(0x7U << 29)
#define DWC_MACSEC_CTRL_PNSE_CHECK_EN		OSI_BIT(28)
#define DWC_MACSEC_CTRL_KAY_DROP_EN		OSI_BIT(11)
#define DWC_MACSEC_CTRL_ETHERTYPE_FILT_EN	OSI_BIT(10)
#define DWC_MACSEC_CTRL_DATA_MODE_SHIFT		5
#define DWC_MACSEC_CTRL_DATA_MODE_MASK		(0x1FU << 5)
#define DWC_MACSEC_CTRL_RX_BYPASS		OSI_BIT(4)
#define DWC_MACSEC_CTRL_TX_BYPASS		OSI_BIT(3)
#define DWC_MACSEC_CTRL_USGMII			OSI_BIT(2)
#define DWC_MACSEC_CTRL_XPNSEL			OSI_BIT(0)
/** @brief VLANMODE values for MACsec_IP_Control [31:29] */
#define DWC_MACSEC_VLANMODE_NOT_ON_CLEAR	0x0U
#define DWC_MACSEC_VLANMODE_ON_CLEAR		0x1U
#define DWC_MACSEC_VLANMODE_DUAL_COPY		0x2U
#define DWC_MACSEC_VLANMODE_CTOS_REPLACE	0x3U
#define DWC_MACSEC_VLANMODE_STAG_COPY		0x4U
#define DWC_MACSEC_VLANMODE_STAG_CLEAR		0x5U
#define DWC_MACSEC_VLANMODE_CTAG_COPY		0x6U

/** @brief DATA_MODE values for MACsec_IP_Control [9:5]
 *
 * Encodes the data interface type and speed. Used with Dual-IF
 * to select GMII vs XLGMII at boot time.
 */
#define DWC_MACSEC_DATA_MODE_GMII_1G		0x02U  /* 8-bit GMII, 1Gbps */
#define DWC_MACSEC_DATA_MODE_GMII_2P5G		0x03U  /* 32-bit XGMII, 2.5Gbps */
#define DWC_MACSEC_DATA_MODE_XGMII_10G		0x06U  /* 32-bit XGMII, 10Gbps */
#define DWC_MACSEC_DATA_MODE_XLGMII_25G		0x07U  /* 32-bit XGMII, 25Gbps */
#define DWC_MACSEC_DATA_MODE_XLGMII_50G		0x0bU  /* 64-bit XLGMII, 50Gbps */
#define DWC_MACSEC_DATA_MODE_XLGMII_100G	0x10U  /* 128-bit XLGMII, 100Gbps */

/** @brief IP_CONTROL default value for 25G XLGMII with XPN, no bypass
 *
 * Composes: VLANMODE not-on-clear, ethertype filter, DATA_MODE 25G,
 *           TX/RX bypass cleared (MACsec active), XPN enabled.
 */
#define DWC_MACSEC_IP_CONTROL_DEFAULT				\
	((DWC_MACSEC_VLANMODE_NOT_ON_CLEAR <<			\
	  DWC_MACSEC_CTRL_VLANMODE_SHIFT) |			\
	 DWC_MACSEC_CTRL_ETHERTYPE_FILT_EN |			\
	 (DWC_MACSEC_DATA_MODE_XLGMII_25G <<			\
	  DWC_MACSEC_CTRL_DATA_MODE_SHIFT) |			\
	 DWC_MACSEC_CTRL_XPNSEL)
/** @} */

/* ============================================================
 * SAI PRG register bit fields (shared for TX/RX)
 * ============================================================ */

/** @addtogroup SAI_PRG bit fields
 * @{ */
#define DWC_MACSEC_SAI_PRG_TYPE_WRITE		OSI_BIT(0)
#define DWC_MACSEC_SAI_PRG_TYPE_READ		0U
#define DWC_MACSEC_SAI_PRG_ENTRY_SHIFT		1
/** @} */

/** @addtogroup RX_SAI_OTHER_FIELDS register (0x30) bit fields
 * @{ */
#define DWC_MACSEC_RX_SAI_AN_MASK		0x3U
#define DWC_MACSEC_RX_SAI_VLAN_WILD_SHIFT	2
/** @} */

/** @addtogroup TX_SAI_OTHER_FIELDS register (0x50) bit fields
 * @{ */
#define DWC_MACSEC_TX_SAI_OUTCOME_SHIFT		0
#define DWC_MACSEC_TX_SAI_OUTCOME_MASK		0x3U
#define DWC_MACSEC_TX_SAI_OUTCOME_BYPASS	0x0U
#define DWC_MACSEC_TX_SAI_OUTCOME_MACSEC	0x1U
#define DWC_MACSEC_TX_SAI_OUTCOME_DROP		0x2U
#define DWC_MACSEC_TX_SAI_SC_INDEX_SHIFT	2
#define DWC_MACSEC_TX_SAI_ETH_TYPE_SHIFT	7
#define DWC_MACSEC_TX_SAI_VLAN_WILD_SHIFT	23
/** @} */

/* ============================================================
 * SAD PRG register bit fields (shared for TX/RX)
 * ============================================================ */

/** @addtogroup SAD_PRG bit fields
 * @{ */
#define DWC_MACSEC_SAD_PRG_TYPE_WRITE		OSI_BIT(0)
#define DWC_MACSEC_SAD_PRG_TYPE_READ		0U
#define DWC_MACSEC_SAD_PRG_ENTRY_SHIFT		1
/** @} */

/* ============================================================
 * TX_SAD_0_LOW (0xD4): PN_OR_SSCI (31:0)
 * TX_SAD_0_HIGH (0xD8) bit fields
 * ============================================================ */

/** @addtogroup TX_SAD_0_HIGH bit fields (0xD8)
 * Bit layout: [31]ACTIVE [27:21]OFFSET [6:2]TCI{ES,SC,SCB,E,C} [1]AN_HI
 * @{ */
#define DWC_MACSEC_TX_SAD0H_ACTIVE		OSI_BIT(31)
#define DWC_MACSEC_TX_SAD0H_OFFSET_SHIFT	21
#define DWC_MACSEC_TX_SAD0H_OFFSET_MASK		(0x7FU << 21)
#define DWC_MACSEC_TX_SAD0H_TCI_SHIFT		2
#define DWC_MACSEC_TX_SAD0H_TCI_MASK		(0x1FU << 2)
#define DWC_MACSEC_TX_SAD0H_AN_HI_SHIFT		1
#define DWC_MACSEC_TX_SAD0H_AN_HI_MASK		0x2U
/** @} */

/* TX_SAD_1_LOW (0xDC): [31:16]SOFT_TTL [15:0]HARD_TTL */
#define DWC_MACSEC_TX_SAD1L_SOFT_TTL_SHIFT	16
#define DWC_MACSEC_TX_SAD1L_SOFT_TTL_MASK	(0xFFFFU << 16)
#define DWC_MACSEC_TX_SAD1L_HARD_TTL_SHIFT	0
#define DWC_MACSEC_TX_SAD1L_HARD_TTL_MASK	0xFFFFU

/* TX_SAD_1_HIGH (0xE0): [31:16]PORT [13:0]MTU */
#define DWC_MACSEC_TX_SAD1H_PORT_SHIFT		16
#define DWC_MACSEC_TX_SAD1H_PORT_MASK		(0xFFFFU << 16)
#define DWC_MACSEC_TX_SAD1H_MTU_SHIFT		0
#define DWC_MACSEC_TX_SAD1H_MTU_MASK		0x3FFFU

/* ============================================================
 * TX SAD XPN fields (0xE4-0xF0)
 * Used when DWC_MACSEC_CTRL_XPNSEL is set in IP_CONTROL.
 * ============================================================ */

/** @addtogroup TX SAD XPN fields
 * TX_SAD_2_LOW  (0xE4): SSCI[31:0]
 * TX_SAD_2_HIGH (0xE8): SSCI[63:32]
 * TX_SAD_3_LOW  (0xEC): [31:16]reserved [15:0]SALT_IDX
 * TX_SAD_3_HIGH (0xF0): [31:1]reserved [0]XPN_CIPHER_SUITE
 * @{ */
#define DWC_MACSEC_TX_SAD3L_SALT_IDX_SHIFT	0
#define DWC_MACSEC_TX_SAD3L_SALT_IDX_MASK	0xFFFFU
#define DWC_MACSEC_TX_SAD3H_XPN_CIPHER		OSI_BIT(0)
/** @} */

/* ============================================================
 * RX_SAD_0_HIGH (0x104) bit fields
 * ============================================================ */

/** @addtogroup RX_SAD_0_HIGH bit fields (0x104)
 * Bit layout: [31]ACTIVE [30]REPLAY [29]USED [28:27]VALIDATE [26:20]OFFSET [15:0]PORT_ID
 * @{ */
#define DWC_MACSEC_RX_SAD0H_ACTIVE		OSI_BIT(31)
#define DWC_MACSEC_RX_SAD0H_REPLAY_EN		OSI_BIT(30)
#define DWC_MACSEC_RX_SAD0H_SA_USED		OSI_BIT(29)
#define DWC_MACSEC_RX_SAD0H_VALIDATE_SHIFT	27
#define DWC_MACSEC_RX_SAD0H_VALIDATE_MASK	(0x3U << 27)
#define DWC_MACSEC_RX_SAD0H_VALIDATE_DISABLED	0x0U
#define DWC_MACSEC_RX_SAD0H_VALIDATE_CHECK	0x1U
#define DWC_MACSEC_RX_SAD0H_VALIDATE_STRICT	0x2U
#define DWC_MACSEC_RX_SAD0H_OFFSET_SHIFT	20
#define DWC_MACSEC_RX_SAD0H_OFFSET_MASK		(0x7FU << 20)
#define DWC_MACSEC_RX_SAD0H_PORT_ID_SHIFT	0
#define DWC_MACSEC_RX_SAD0H_PORT_ID_MASK	0xFFFFU
/** @} */

/* ============================================================
 * RX SAD XPN fields (0x108-0x114)
 * Used when DWC_MACSEC_CTRL_XPNSEL is set in IP_CONTROL.
 *
 * RX_SAD_1_LOW  (0x108): LOWEST_PN_HI[31:0] (upper 32 bits of lowest PN)
 * RX_SAD_1_HIGH (0x10C): SSCI[31:0]
 * RX_SAD_2_LOW  (0x110): SSCI[63:32]
 * RX_SAD_2_HIGH (0x114): [31:17]reserved [16]XPN_CIPHER [15:0]SALT_IDX
 * ============================================================ */

/** @addtogroup RX SAD XPN fields
 * @{ */
#define DWC_MACSEC_RX_SAD2H_SALT_IDX_SHIFT	0
#define DWC_MACSEC_RX_SAD2H_SALT_IDX_MASK	0xFFFFU
#define DWC_MACSEC_RX_SAD2H_XPN_CIPHER		OSI_BIT(16)
/** @} */

/* ============================================================
 * PG (Page Guard/Busy) register bit field
 * ============================================================ */

#define DWC_MACSEC_PG_BUSY			OSI_BIT(0)

/* ============================================================
 * DATA_FETCH register bit field
 * ============================================================ */

#define DWC_MACSEC_DATA_FETCH_READY		OSI_BIT(0)

/* ============================================================
 * TX_ACTIVE_AN (0x60) register
 * One bit per SC: 0 = SA0 active, 1 = SA1 active
 * ============================================================ */

/* TX_ACTIVE_AN bitmap: bit i = active SA for SC i (0 or 1) */

/* ============================================================
 * MIB_CMD register (0x64) bit fields
 * ============================================================ */

/** @addtogroup MIB_CMD bit fields
 * @{ */
#define DWC_MACSEC_MIB_CMD_OBJ_ID_SHIFT	16
#define DWC_MACSEC_MIB_CMD_CTR_ID_SHIFT		8
#define DWC_MACSEC_MIB_CMD_CTR_ID_MASK		(0x7FU << 8)
#define DWC_MACSEC_MIB_CMD_CLR			OSI_BIT(5)
#define DWC_MACSEC_MIB_CMD_DIR_TX		OSI_BIT(4)
#define DWC_MACSEC_MIB_CMD_DIR_RX		0U
#define DWC_MACSEC_MIB_CMD_CMD_MASK		0x7U
#define DWC_MACSEC_MIB_CMD_INIT			0x0U
#define DWC_MACSEC_MIB_CMD_RD_SGL		0x1U
#define DWC_MACSEC_MIB_CMD_WR_SGL		0x5U
/** @} */

/** MIB_CMD_STAT busy bit */
#define DWC_MACSEC_MIB_CMD_STAT_BUSY		OSI_BIT(0)

/* ============================================================
 * MIB Counter IDs (CTR_ID values for MIB_CMD)
 * ============================================================ */

/** @addtogroup TX MIB Counter IDs
 * @{ */
#define DWC_MIB_TX_UNTAGGED_PKTS_GBL		0x00U
#define DWC_MIB_TX_LOOKUP_DIS_PKTS_GBL		0x01U
#define DWC_MIB_TX_TOO_LONG_PKTS_GBL		0x02U
#define DWC_MIB_TX_OCTETS_PROTECTED_GBL		0x05U
#define DWC_MIB_TX_OCTETS_ENCRYPTED_GBL		0x06U
#define DWC_MIB_TX_PROTECTED_PKTS_SC		0x20U
#define DWC_MIB_TX_ENCRYPTED_PKTS_SC		0x21U
#define DWC_MIB_TX_OCTETS_PROTECTED_SC		0x22U
#define DWC_MIB_TX_OCTETS_ENCRYPTED_SC		0x23U
#define DWC_MIB_TX_PROTECTED_PKTS_SA		0x40U
#define DWC_MIB_TX_ENCRYPTED_PKTS_SA		0x41U
/** @} */

/** @addtogroup RX MIB Counter IDs
 * @{ */
#define DWC_MIB_RX_UNTAGGED_PKTS_GBL		0x00U
#define DWC_MIB_RX_BAD_TAG_PKTS_GBL		0x01U
#define DWC_MIB_RX_NO_TAG_PKTS_GBL		0x04U
#define DWC_MIB_RX_OCTETS_VALIDATED_GBL		0x05U
#define DWC_MIB_RX_OCTETS_DECRYPTED_GBL		0x06U
#define DWC_MIB_RX_OVERRUN_PKTS_GBL		0x07U
#define DWC_MIB_RX_NO_SA_ERROR_PKTS_GBL		0x08U
#define DWC_MIB_RX_NO_SA_PKTS_GBL		0x09U
#define DWC_MIB_RX_OK_PKTS_SC			0x20U
#define DWC_MIB_RX_INVALID_PKTS_SC		0x21U
#define DWC_MIB_RX_NOT_VALID_PKTS_SC		0x22U
#define DWC_MIB_RX_DELAYED_PKTS_SC		0x25U
#define DWC_MIB_RX_LATE_PKTS_SC			0x26U
#define DWC_MIB_RX_UNCHECKED_SC			0x27U
#define DWC_MIB_RX_OCTETS_VALIDATED_SC		0x28U
#define DWC_MIB_RX_OCTETS_DECRYPTED_SC		0x29U
#define DWC_MIB_RX_OK_PKTS_SA			0x40U
#define DWC_MIB_RX_INVALID_PKTS_SA		0x41U
#define DWC_MIB_RX_NOT_VALID_PKTS_SA		0x42U
#define DWC_MIB_RX_NOT_USING_SA_PKTS_SA		0x43U
#define DWC_MIB_RX_UNUSED_SA_PKTS_SA		0x44U
/** @} */

/* ============================================================
 * MIB_VALIDATE register (0x6C) bit fields
 * PHY_MAP[0]: default VALIDATE_FRAMES for SA lookup miss
 *   0 = disabled/check (non-MACsec frames pass through)
 *   1 = strict (non-MACsec frames are poisoned)
 * Per manual 1.8.1: do NOT set during initial setup.
 * ============================================================ */

#define DWC_MACSEC_MIB_VALIDATE_PHY_MAP_SHIFT	0
#define DWC_MACSEC_MIB_VALIDATE_PHY_MAP_MASK	0x1U
#define DWC_MACSEC_MIB_VALIDATE_NOT_STRICT	0x0U
#define DWC_MACSEC_MIB_VALIDATE_STRICT		0x1U

/* ============================================================
 * IRQ_GLBL_EN (0x178) and IRQ_GLBL_STAT (0x17C) bit fields
 * 16 interrupt enable/status bits
 * ============================================================ */

/** @addtogroup DWC_MACSEC IRQ_GLBL_EN (0x178) / IRQ_GLBL_STAT (0x17C)
 * 16 global error interrupt bits (NOT per-SA). W1C for STAT.
 * @{ */
#define DWC_MACSEC_IRQ_GLBL_MASTER_EN		OSI_BIT(31)
#define DWC_MACSEC_IRQ_GLBL_AES_APB_PTCOL	OSI_BIT(15)
#define DWC_MACSEC_IRQ_GLBL_APB_PTCOL		OSI_BIT(14)
#define DWC_MACSEC_IRQ_GLBL_AES_APB_PARITY	OSI_BIT(13)
#define DWC_MACSEC_IRQ_GLBL_APB_PARITY		OSI_BIT(12)
#define DWC_MACSEC_IRQ_GLBL_AES_KEY_ERR		OSI_BIT(11)
#define DWC_MACSEC_IRQ_GLBL_RX_PREAMBLE		OSI_BIT(10)
#define DWC_MACSEC_IRQ_GLBL_TX_PREAMBLE		OSI_BIT(9)
#define DWC_MACSEC_IRQ_GLBL_RX_IF_ERR		OSI_BIT(8)
#define DWC_MACSEC_IRQ_GLBL_TX_IF_ERR		OSI_BIT(7)
#define DWC_MACSEC_IRQ_GLBL_RX_ILLEGAL_IPG	OSI_BIT(6)
#define DWC_MACSEC_IRQ_GLBL_TX_ILLEGAL_IPG	OSI_BIT(5)
#define DWC_MACSEC_IRQ_GLBL_RX_TOO_SHORT	OSI_BIT(4)
#define DWC_MACSEC_IRQ_GLBL_TX_TOO_SHORT	OSI_BIT(3)
#define DWC_MACSEC_IRQ_GLBL_RX_CRC_ERR		OSI_BIT(2)
#define DWC_MACSEC_IRQ_GLBL_TX_CRC_ERR		OSI_BIT(1)
#define DWC_MACSEC_IRQ_GLBL_RX_UNKNOWN_SC	OSI_BIT(0)
#define DWC_MACSEC_IRQ_GLBL_ALL_ERRORS		0xFFFFU
/** @} */

/** @addtogroup Per-SA TX IRQ type bits (IRQ_TYPE_STAT_TX_SA)
 * Bit layout: [4]INACTIVE [3]SOFT_TTL [2]HARD_TTL [1]TOO_LONG [0]reserved
 * @{ */
#define DWC_MACSEC_IRQ_TX_SA_INACTIVE		OSI_BIT(4)
#define DWC_MACSEC_IRQ_TX_SA_PN_THR		OSI_BIT(3)
#define DWC_MACSEC_IRQ_TX_SA_PN_EXHAUST		OSI_BIT(2)
#define DWC_MACSEC_IRQ_TX_SA_TOO_LONG		OSI_BIT(1)
#define DWC_MACSEC_IRQ_TX_SA_ALL		0x1EU
/** @} */

/** @addtogroup Per-SA RX IRQ registers and bits
 * @{ */
#define DWC_MACSEC_IRQ_RX_SA_EN(x)	(0x390U + ((x) * 8U))
#define DWC_MACSEC_IRQ_RX_SA_STAT(x)	(0x394U + ((x) * 8U))
#define DWC_MACSEC_IRQ_RX_SA_INACTIVE		OSI_BIT(6)
#define DWC_MACSEC_IRQ_RX_SA_UNKNOWN_CIPH	OSI_BIT(5)
#define DWC_MACSEC_IRQ_RX_SA_PRE_REPLAY		OSI_BIT(4)
#define DWC_MACSEC_IRQ_RX_SA_POST_REPLAY	OSI_BIT(3)
#define DWC_MACSEC_IRQ_RX_SA_BAD_SECTAG		OSI_BIT(2)
#define DWC_MACSEC_IRQ_RX_SA_CHECK_FAIL		OSI_BIT(1)
#define DWC_MACSEC_IRQ_RX_SA_NEW_SA		OSI_BIT(0)
#define DWC_MACSEC_IRQ_RX_SA_ALL		0x7FU
/** @} */

/* ============================================================
 * AES Key Table APB registers (DWC_macsec_uaes_gcmp_apb)
 * Accessed via osi_core->tz_base (separate APB interface)
 * Databook section 6.2
 * ============================================================ */

/** @addtogroup AES APB register offsets (via tz_base)
 * @{ */
#define DWC_AES_IRQ_EN			0x08
#define DWC_AES_IRQ_STAT		0x0C
#define DWC_AES_CONFIG			0x10
#define DWC_AES_CTRL			0x14
#define DWC_AES_STAT			0x18
#define DWC_AES_KEY(x)			(0x20U + ((x) * 4U))
#define DWC_AES_MISC_CONFIG		0x40
#define DWC_AES_FIPS_SELF_TEST_CTL	0x50
#define DWC_AES_FIPS_SELF_TEST_ATT	0x54
#define DWC_AES_FIPS_SELF_TEST_STAT	0x58
#define DWC_AES_BIST_VECT_MODE		0x2A4
#define DWC_AES_BIST_VECT_ERR_INJ	0x2A8
#define DWC_AES_BIST_VECT_CTL		0x2AC
/** @} */

/* ============================================================
 * AES IRQ_EN (0x08) and IRQ_STAT (0x0C) bit fields
 * Databook 6.2.1 / 6.2.2
 * IRQ_STAT is W1C (write-1-to-clear).
 * ============================================================ */

/** @addtogroup DWC_AES_IRQ bit fields
 * @{ */
#define DWC_AES_IRQ_GLBL			OSI_BIT(31)
#define DWC_AES_IRQ_FSM_PAR_ERR			OSI_BIT(17)
#define DWC_AES_IRQ_REG_PAR_ERR			OSI_BIT(16)
#define DWC_AES_IRQ_CTX_IDX_ERR			OSI_BIT(4)
#define DWC_AES_IRQ_KEY_DONE			OSI_BIT(0)
#define DWC_AES_IRQ_ALL_ERRORS			(DWC_AES_IRQ_FSM_PAR_ERR | \
						 DWC_AES_IRQ_REG_PAR_ERR | \
						 DWC_AES_IRQ_CTX_IDX_ERR)
#define DWC_AES_IRQ_ALL				(DWC_AES_IRQ_FSM_PAR_ERR | \
						 DWC_AES_IRQ_REG_PAR_ERR | \
						 DWC_AES_IRQ_CTX_IDX_ERR | \
						 DWC_AES_IRQ_KEY_DONE)
/** @} */

/** @addtogroup DWC_AES_CONFIG bit fields (0x10)
 * Databook 6.2.3 �� all fields are read-only HW configuration
 * @{ */
#define DWC_AES_CONFIG_NUM_CTX_SHIFT	15
#define DWC_AES_CONFIG_NUM_CTX_MASK	(0x1FFFFU << 15)
#define DWC_AES_CONFIG_AES_EN		OSI_BIT(14)
#define DWC_AES_CONFIG_SM4_EN		OSI_BIT(13)
#define DWC_AES_CONFIG_CTR_STREAM_EN	OSI_BIT(12)
#define DWC_AES_CONFIG_ILEAVE_EN	OSI_BIT(11)
#define DWC_AES_CONFIG_DEC_EN		OSI_BIT(10)
#define DWC_AES_CONFIG_ENC_EN		OSI_BIT(9)
#define DWC_AES_CONFIG_KEY256_EN	OSI_BIT(8)
#define DWC_AES_CONFIG_KEY128_EN	OSI_BIT(7)
#define DWC_AES_CONFIG_CTR_EN		OSI_BIT(6)  /* reset = DWC_MACSEC_AES_FIPS_EN */
#define DWC_AES_CONFIG_GCM_EN		OSI_BIT(5)
#define DWC_AES_CONFIG_SK_EN		OSI_BIT(4)
#define DWC_AES_CONFIG_DP_WIDTH_MASK	0x0FU
/** @} */

/** @addtogroup DWC_AES_CTRL bit fields (0x14)
 * Databook 6.2.4
 * @{ */
#define DWC_AES_CTRL_ENCRYPT		OSI_BIT(18)
#define DWC_AES_CTRL_MODE_GCM		0U
#define DWC_AES_CTRL_MODE_CTR		OSI_BIT(17)
#define DWC_AES_CTRL_KEY_SZ_256		OSI_BIT(16)
#define DWC_AES_CTRL_CTX_IDX_MASK	0x3FFU  /* 10 bits for 546 contexts */
/** @} */

/** @addtogroup DWC_AES_STAT bit fields (0x18)
 * Databook 6.2.5 �� only BUSY bit
 * @{ */
#define DWC_AES_STAT_BUSY		OSI_BIT(0)
/** @} */

/** @addtogroup DWC_AES_MISC_CONFIG bit fields (0x40)
 * Databook 6.2.14
 * @{ */
#define DWC_AES_MISC_CLR_SSP		OSI_BIT(2)
#define DWC_AES_MISC_INHIBIT_OUTPUT	OSI_BIT(1)
/** @} */

/** @addtogroup DWC_AES_FIPS_SELF_TEST_CTL bit fields (0x50)
 * Databook 6.2.15 �� Exists when DWC_MACSEC_AES_FIPS_EN==1
 * @{ */
#define DWC_AES_FIPS_CTL_CHECK		OSI_BIT(1)
#define DWC_AES_FIPS_CTL_ENA		OSI_BIT(0)
/** @} */

/** @addtogroup DWC_AES_FIPS_SELF_TEST_STAT bit fields (0x58)
 * Databook 6.2.17 �� all W1C
 * @{ */
#define DWC_AES_FIPS_STAT_FAIL_CAUSE_MASK	(0x7FU << 4)
#define DWC_AES_FIPS_STAT_MAC_MATCH_RES		OSI_BIT(2)
#define DWC_AES_FIPS_STAT_SELF_TEST_FAIL	OSI_BIT(1)
#define DWC_AES_FIPS_STAT_SELF_TEST_DONE	OSI_BIT(0)
/** @} */

/** @addtogroup DWC_AES_BIST_VECT_MODE bit fields (0x2A4)
 * Databook 6.2.63
 * @{ */
#define DWC_AES_BIST_MODE_ENCRYPT	OSI_BIT(8)
#define DWC_AES_BIST_MODE_KEY256	OSI_BIT(3)
#define DWC_AES_BIST_MODE_FUNCT_MASK	0x3U
#define DWC_AES_BIST_FUNCT_BATCH	0U
#define DWC_AES_BIST_FUNCT_CTR		1U
#define DWC_AES_BIST_FUNCT_GCM		2U
#define DWC_AES_BIST_FUNCT_ECB		3U
/** @} */

/** @addtogroup DWC_AES_BIST_VECT_CTL bit fields (0x2AC)
 * Databook 6.2.65
 * @{ */
#define DWC_AES_BIST_TEST_GO		OSI_BIT(0)  /* self-clearing */
/** @} */

/** FIPS context index (DWC_MACSEC_AES_DEFAULT_FIPS_CTX_IDX = 544)
 * Last valid context index = DWC_MACSEC_AES_KEY_CTX - 2 = 544 */
#define DWC_AES_FIPS_CTX_IDX		544U

/** FIPS self-test polling timeout (iterations) */
#define DWC_AES_FIPS_POLL_COUNT		5000U

/** Number of 32-bit key data registers (KEY_0 through KEY_7) */
#define DWC_AES_KEY_REG_CNT		8U

/* ============================================================
 * Compatibility macros
 * Keep existing Omni macro names that are referenced widely
 * in the codebase, mapped to DWC_macsec equivalents
 * ============================================================ */

/* ============================================================
 * ETH Subsystem CSR registers (CSR_BASE + offset)
 *
 * These registers are in the ETH subsystem CSR module, NOT inside
 * the MACsec IP itself.  They control MACsec clock-gate, reset,
 * SRAM zeroization, and sideband lock signals.
 * ============================================================ */

/** @addtogroup ETH_CSR MACsec-related control registers
 * @{ */

/** eth_ctrl_5 (CSR_BASE + 0x14): MACsec enable & input control */
#define ETH_CSR_CTRL_5			0x14U
#define ETH_CSR_MACSEC_ENABLE		OSI_BIT(0)  /* clock-gate enable */
#define ETH_CSR_MACSEC_RST_N		OSI_BIT(1)  /* software reset (active-low) */
#ifdef DWC_MACSEC_USE_SRAM
#define ETH_CSR_MACSEC_SRAM_ZERO_INIT	OSI_BIT(2)  /* SRAM zeroization trigger */
#endif /* DWC_MACSEC_USE_SRAM */

#ifdef DWC_MACSEC_SIDEBAND_LOCK
/** eth_ctrl_7 (CSR_BASE + 0x1C): MACsec sideband lock signals */
#define ETH_CSR_CTRL_7			0x1CU
#define ETH_CSR_MACSEC_AES_HCFG_LOCK	OSI_BIT(0)  /* lock AES APB config */
#define ETH_CSR_MACSEC_INT_STATUS_LOCK	OSI_BIT(1)  /* lock interrupt status regs */
#define ETH_CSR_MACSEC_HCFG_LOCK	OSI_BIT(2)  /* lock MACsec APB config */
#define ETH_CSR_MACSEC_ALL_LOCKS	(ETH_CSR_MACSEC_AES_HCFG_LOCK | \
					 ETH_CSR_MACSEC_INT_STATUS_LOCK | \
					 ETH_CSR_MACSEC_HCFG_LOCK)
#endif /* DWC_MACSEC_SIDEBAND_LOCK */

/** SRAM zero init pulse width: wait iterations after assert / deassert */
#ifdef DWC_MACSEC_USE_SRAM
#define ETH_CSR_SRAM_ZERO_POLL_COUNT	10000U
#endif /* DWC_MACSEC_USE_SRAM */

/** @} */

/* MACSEC_IP_Control maps to old MACSEC_CONTROL0 for enable/disable */
#define MACSEC_CONTROL0			DWC_MACSEC_IP_CONTROL
#define MACSEC_TX_EN			DWC_MACSEC_CTRL_TX_BYPASS
#define MACSEC_RX_EN			DWC_MACSEC_CTRL_RX_BYPASS

/* Shared utility macros from old macsec.h preserved here */
#define MAX_U64_VAL			0xFFFFFFFFFFFFFFFFU
#define CERT_C__POST_INC__U64(a)\
	{\
		if ((a) < MAX_U64_VAL) {\
			(a)++;\
		} else {\
			(a) = 0;\
		} \
	}

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
#define INTEGER_LEN		4U
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

#ifdef DEBUG_MACSEC
#define HKEY2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7], (a)[8], (a)[9], (a)[10], (a)[11], (a)[12], (a)[13], (a)[14], (a)[15]
#define HKEYSTR "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"
#define KEY2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7], (a)[8], (a)[9], (a)[10], (a)[11], (a)[12], (a)[13], (a)[14], (a)[15]
//#define KEYSTR "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"
#endif /* DEBUG_MACSEC */

#ifdef HSI_SUPPORT
/* DWC_macsec uses IRQ_GLBL_STAT with W1C, no separate ISR_SET registers */
#define MACSEC_COMMON_ISR_SET		DWC_MACSEC_IRQ_GLBL_STAT
#endif

#endif /* INCLUDED_DWC_MACSEC_REG_H */
