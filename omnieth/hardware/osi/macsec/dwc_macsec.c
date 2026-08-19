/*
 * Copyright (c) 2026-2030, Omni. All rights reserved.
 *
 * DWC_macsec 1.05a OSI Hardware Abstraction Implementation
 *
 * Implements the osi_macsec_core_ops interface using Synopsys DWC_macsec
 * 1.05a SAI/SAD register architecture, replacing Omni's LUT-based approach.
 *
 * Hardware: DWC_macsec Single Port AC Security Module v1.05a
 *   - TX: 16 SC x 2 SA = 32 SA total
 *   - RX: 128 SC x 4 SA = 512 SA total
 *   - AES-GCM-128/256, 546 key contexts
 *   - MIB counters via indirect MIB_CMD interface
 *   - Keys via separate APB (macsec_id_aes_host_cfg)
 */

#ifdef MACSEC_SUPPORT
#include <osi_macsec.h>
#include "dwc_macsec_reg.h"
#include "../osi/core/common.h"
#include "../osi/core/core_local.h"

/* DWC TCI field encoding: {ES, SC, SCB, E, C} (5 bits)
 * Standard MACsec (encrypt+auth, SCI present): SC=1, E=1, C=1 = 0x0B
 * Auth only (SCI present): SC=1 = 0x08
 */
#define DWC_TCI_ENCRYPT_AUTH		0x0BU
#define DWC_TCI_AUTH_ONLY		0x08U

/**
 * @brief Map Omni OSD TCI (2 bits: {ES, SC}) to DWC TCI (5 bits: {ES,SC,SCB,E,C})
 *
 * Omni OSD TCI from omni_macsec_get_tx_tci():
 *   bit 0 = SC (send SCI), bit 1 = ES (end station)
 * DWC TCI layout: bit4=ES, bit3=SC, bit2=SCB, bit1=E, bit0=C
 *
 * Assumes encrypt+auth mode (E=1, C=1).
 */
static inline u8 dwc_map_tci(u8 osd_tci, u8 encrypt)
{
	u8 dwc_tci = (encrypt != 0) ? 0x03U : 0x01U; /* E=1, C=1 or E=0, C=1 */

	if ((osd_tci & 0x1U) != OSI_NONE) {
		dwc_tci |= 0x08U; /* SC bit */
	}
	if ((osd_tci & 0x2U) != OSI_NONE) {
		dwc_tci |= 0x10U; /* ES bit */
	}
	return dwc_tci;
}

/**
 * @brief Reverse-map DWC 5-bit TCI back to OSD TCI format
 *
 * DWC TCI: bit4=ES, bit3=SC, bit2=SCB, bit1=E, bit0=C
 * OSD TCI: bit0=SC, bit1=ES, bit2=SCB (V always implicit)
 */
static inline u8 dwc_unmap_tci(u8 dwc_tci)
{
	u8 osd_tci = 0U;

	if ((dwc_tci & 0x08U) != OSI_NONE) {
		osd_tci |= 0x1U; /* SC bit */
	}
	if ((dwc_tci & 0x10U) != OSI_NONE) {
		osd_tci |= 0x2U; /* ES bit */
	}
	if ((dwc_tci & 0x04U) != OSI_NONE) {
		osd_tci |= 0x4U; /* SCB bit */
	}
	return osd_tci;
}

/* Default PN threshold (SOFT_TTL) - trigger at 75% of 32-bit PN space */
#define DWC_DEFAULT_PN_THRESHOLD	0xC0000000U

#if 0
#include <linux/printk.h>
#define MACSEC_LOG(...) pr_debug(__VA_ARGS__)
#else
#define MACSEC_LOG(...)
#endif

/* ============================================================
 * Polling helpers
 * ============================================================ */

static s32 dwc_poll_busy(struct osi_core_priv_data *const osi_core,
			     u32 reg_offset)
{
	u32 retry = DWC_MACSEC_SAI_RETRY_COUNT;
	u32 val;
	u32 count = 0;

	while (count <= retry) {
		val = osi_readla(osi_core,
				 (u8 *)osi_core->macsec_base + reg_offset);
		if ((val & DWC_MACSEC_PG_BUSY) == OSI_NONE) {
			return 0;
		}
		count++;
		osi_core->osd_ops.udelay(DWC_MACSEC_SAI_RETRY_DELAY);
	}
	OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
		     "DWC MACsec busy timeout\n", (u64)reg_offset);
	return -1;
}

static s32 dwc_poll_ready(struct osi_core_priv_data *const osi_core,
			      u32 reg_offset)
{
	u32 retry = DWC_MACSEC_SAI_RETRY_COUNT;
	u32 val;
	u32 count = 0;

	while (count <= retry) {
		val = osi_readla(osi_core,
				 (u8 *)osi_core->macsec_base + reg_offset);
		if ((val & DWC_MACSEC_DATA_FETCH_READY) != OSI_NONE) {
			return 0;
		}
		count++;
		osi_core->osd_ops.udelay(DWC_MACSEC_SAI_RETRY_DELAY);
	}
	OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
		     "DWC MACsec data fetch timeout\n", (u64)reg_offset);
	return -1;
}

static s32 dwc_poll_mib_busy(struct osi_core_priv_data *const osi_core)
{
	u32 retry = DWC_MACSEC_SAI_RETRY_COUNT;
	u32 val;
	u32 count = 0;

	while (count <= retry) {
		val = osi_readla(osi_core,
				 (u8 *)osi_core->macsec_base +
				 DWC_MACSEC_MIB_CMD_STAT);
		if ((val & DWC_MACSEC_MIB_CMD_STAT_BUSY) == OSI_NONE) {
			return 0;
		}
		count++;
		osi_core->osd_ops.udelay(DWC_MACSEC_SAI_RETRY_DELAY);
	}
	OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
		     "MIB command timeout\n", 0ULL);
	return -1;
}

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
static s32 dwc_poll_aes_busy(struct osi_core_priv_data *const osi_core)
{
	u32 retry = DWC_MACSEC_SAI_RETRY_COUNT;
	u32 val;
	u32 count = 0;

	while (count <= retry) {
		val = osi_readla(osi_core,
				 (u8 *)osi_core->tz_base + DWC_AES_STAT);
		if ((val & DWC_AES_STAT_BUSY) == OSI_NONE) {
			return 0;
		}
		count++;
		osi_core->osd_ops.udelay(DWC_MACSEC_SAI_RETRY_DELAY);
	}
	OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
		     "AES busy timeout\n", 0ULL);
	return -1;
}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

/* ============================================================
 * TX/RX SAI (Lookup Table) programming
 * ============================================================ */

/**
 * @brief Write a TX SAI lookup entry
 *
 * Programs a TX SAI entry which maps a frame (by MAC/VLAN/ethtype match)
 * to a specific SC index with a given outcome (bypass/macsec/drop).
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] index: SAI entry index
 * @param[in] sc_index: SC index for this entry
 * @param[in] outcome: 0=bypass, 1=macsec, 2=drop
 * @param[in] sa_mac: Source MAC for filter (6 bytes), or NULL
 * @param[in] da_mac: Destination MAC for filter (6 bytes), or NULL
 */
static s32 dwc_tx_sai_write(struct osi_core_priv_data *const osi_core,
				u16 index, u32 sc_index,
				u32 outcome,
				const u8 *sa_mac,
				const u8 *da_mac)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	/* Wait for TX SAI not busy */
	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	/* Program source MAC filter and Port ID if provided (sa_mac is 8-byte SCI) */
	if (sa_mac != OSI_NULL) {
		u32 port_id;

		val = ((u32)sa_mac[0] << 8) | (u32)sa_mac[1];
		osi_writela(osi_core, val, base + DWC_MACSEC_TX_SRC_FILT_MSB);
		val = ((u32)sa_mac[2] << 24) | ((u32)sa_mac[3] << 16) |
		      ((u32)sa_mac[4] << 8) | (u32)sa_mac[5];
		osi_writela(osi_core, val, base + DWC_MACSEC_TX_SRC_FILT_LSB);

		port_id = ((u32)sa_mac[6] << 8) | (u32)sa_mac[7];
		osi_writela(osi_core, port_id, base + DWC_MACSEC_TX_SAI_PORT_ID);
	}

	/* Program destination MAC filter if provided */
	if (da_mac != OSI_NULL) {
		val = ((u32)da_mac[0] << 8) | (u32)da_mac[1];
		osi_writela(osi_core, val, base + DWC_MACSEC_TX_DST_FILT_MSB);
		val = ((u32)da_mac[2] << 24) | ((u32)da_mac[3] << 16) |
		      ((u32)da_mac[4] << 8) | (u32)da_mac[5];
		osi_writela(osi_core, val, base + DWC_MACSEC_TX_DST_FILT_LSB);
	}

	/* TX_SAI_OTHER_FIELDS: outcome + sc_index */
	val = (outcome & DWC_MACSEC_TX_SAI_OUTCOME_MASK) |
	      ((sc_index & 0x1FU) << DWC_MACSEC_TX_SAI_SC_INDEX_SHIFT);
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAI_OTHER_FIELDS);

	/* TX_SAI_PRG: trigger write, ENTRY = index, TYPE = write */
	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAI_PRG);

	/* Wait for completion */
	return dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAI_PG);
}

/**
 * @brief Read a TX SAI lookup entry
 *
 * Reads back the TX SAI entry fields: source/dest MAC filters,
 * outcome, and SC index.
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  index: SAI entry index
 * @param[out] sc_index: SC index read from entry
 * @param[out] outcome: outcome read from entry (bypass/macsec/drop)
 * @param[out] sa_mac: 6-byte source MAC buffer (may be NULL to skip)
 * @param[out] da_mac: 6-byte dest MAC buffer (may be NULL to skip)
 */
static s32 dwc_tx_sai_read(struct osi_core_priv_data *const osi_core,
				u16 index, u32 *sc_index,
				u32 *outcome,
				u8 *sa_mac, u8 *da_mac)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	/* TX_SAI_PRG: trigger read */
	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAI_PRG);

	ret = dwc_poll_ready(osi_core, DWC_MACSEC_TX_SAI_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	/* Read source MAC filter */
	if (sa_mac != OSI_NULL) {
		val = osi_readla(osi_core, base + DWC_MACSEC_TX_SRC_FILT_MSB);
		sa_mac[0] = (u8)((val >> 8) & 0xFFU);
		sa_mac[1] = (u8)(val & 0xFFU);
		val = osi_readla(osi_core, base + DWC_MACSEC_TX_SRC_FILT_LSB);
		sa_mac[2] = (u8)((val >> 24) & 0xFFU);
		sa_mac[3] = (u8)((val >> 16) & 0xFFU);
		sa_mac[4] = (u8)((val >> 8) & 0xFFU);
		sa_mac[5] = (u8)(val & 0xFFU);
	}

	/* Read destination MAC filter */
	if (da_mac != OSI_NULL) {
		val = osi_readla(osi_core, base + DWC_MACSEC_TX_DST_FILT_MSB);
		da_mac[0] = (u8)((val >> 8) & 0xFFU);
		da_mac[1] = (u8)(val & 0xFFU);
		val = osi_readla(osi_core, base + DWC_MACSEC_TX_DST_FILT_LSB);
		da_mac[2] = (u8)((val >> 24) & 0xFFU);
		da_mac[3] = (u8)((val >> 16) & 0xFFU);
		da_mac[4] = (u8)((val >> 8) & 0xFFU);
		da_mac[5] = (u8)(val & 0xFFU);
	}

	/* Read OTHER_FIELDS: outcome + sc_index */
	val = osi_readla(osi_core, base + DWC_MACSEC_TX_SAI_OTHER_FIELDS);
	if (outcome != OSI_NULL) {
		*outcome = val & DWC_MACSEC_TX_SAI_OUTCOME_MASK;
	}
	if (sc_index != OSI_NULL) {
		*sc_index = (val >> DWC_MACSEC_TX_SAI_SC_INDEX_SHIFT) & 0x1FU;
	}

	return 0;
}

/**
 * @brief Write an RX SAI lookup entry
 *
 * Programs an RX SAI entry which maps an incoming MACsec frame (by SCI match)
 * to a specific SA index.
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] index: SAI entry index
 * @param[in] sci: 8-byte SCI
 * @param[in] an: Association Number (0-3, but only 0-1 used for 2 SA/SC)
 */
static s32 dwc_rx_sai_write(struct osi_core_priv_data *const osi_core,
				u16 index, const u8 *sci, u8 an)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	/* Wait for RX SAI not busy */
	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	/* Program SCI (8 bytes split across two 32-bit registers) */
	val = ((u32)sci[0] << 24) | ((u32)sci[1] << 16) |
	      ((u32)sci[2] << 8) | (u32)sci[3];
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_LT_SCI_HIGH);

	val = ((u32)sci[4] << 24) | ((u32)sci[5] << 16) |
	      ((u32)sci[6] << 8) | (u32)sci[7];
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_LT_SCI_LOW);

	/* RX_SAI_OTHER_FIELDS: AN */
	val = (u32)(an & DWC_MACSEC_RX_SAI_AN_MASK);
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAI_OTHER_FIELDS);

	/* RX_SAI_PRG: trigger write */
	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAI_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAI_PG);
}

/**
 * @brief Read an RX SAI lookup entry
 *
 * Reads back the RX SAI entry: SCI and AN.
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  index: SAI entry index
 * @param[out] sci: 8-byte SCI buffer
 * @param[out] an: Association Number read from entry
 */
static s32 dwc_rx_sai_read(struct osi_core_priv_data *const osi_core,
				u16 index, u8 *sci, u8 *an)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	/* RX_SAI_PRG: trigger read */
	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAI_PRG);

	ret = dwc_poll_ready(osi_core, DWC_MACSEC_RX_SAI_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	/* Read SCI (8 bytes from two 32-bit registers) */
	if (sci != OSI_NULL) {
		val = osi_readla(osi_core, base + DWC_MACSEC_RX_LT_SCI_HIGH);
		sci[0] = (u8)((val >> 24) & 0xFFU);
		sci[1] = (u8)((val >> 16) & 0xFFU);
		sci[2] = (u8)((val >> 8) & 0xFFU);
		sci[3] = (u8)(val & 0xFFU);

		val = osi_readla(osi_core, base + DWC_MACSEC_RX_LT_SCI_LOW);
		sci[4] = (u8)((val >> 24) & 0xFFU);
		sci[5] = (u8)((val >> 16) & 0xFFU);
		sci[6] = (u8)((val >> 8) & 0xFFU);
		sci[7] = (u8)(val & 0xFFU);
	}

	/* Read AN from OTHER_FIELDS */
	if (an != OSI_NULL) {
		val = osi_readla(osi_core, base + DWC_MACSEC_RX_SAI_OTHER_FIELDS);
		*an = (u8)(val & DWC_MACSEC_RX_SAI_AN_MASK);
	}

	return 0;
}

/**
 * @brief Clear a TX SAI entry (invalidate)
 */
static s32 dwc_tx_sai_clear(struct osi_core_priv_data *const osi_core,
				u16 index)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	/* Zero all fields */
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_SRC_FILT_MSB);
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_SRC_FILT_LSB);
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_DST_FILT_MSB);
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_DST_FILT_LSB);
	/* outcome = bypass (0) */
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_SAI_OTHER_FIELDS);

	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAI_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAI_PG);
}

/**
 * @brief Clear an RX SAI entry (invalidate)
 */
static s32 dwc_rx_sai_clear(struct osi_core_priv_data *const osi_core,
				u16 index)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAI_PG);
	if (ret < 0) {
		return ret;
	}

	osi_writela(osi_core, 0U, base + DWC_MACSEC_RX_LT_SCI_LOW);
	osi_writela(osi_core, 0U, base + DWC_MACSEC_RX_LT_SCI_HIGH);
	osi_writela(osi_core, 0U, base + DWC_MACSEC_RX_SAI_OTHER_FIELDS);

	val = ((u32)index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAI_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAI_PG);
}

/* ============================================================
 * TX/RX SAD (Security Association Data) programming
 * ============================================================ */

/**
 * @brief Write a TX SAD entry
 *
 * Programs packet number, TCI, SOFT_TTL, MTU, active status for a TX SA.
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] sa_index: SA index (0 to DWC_MACSEC_TX_MAX_SA-1)
 * @param[in] pn: Initial packet number (must be >=1)
 * @param[in] tci: TCI field (5 bits: ES,SC,SCB,E,C)
 * @param[in] active: 1 to activate SA, 0 to deactivate
 * @param[in] mtu: MTU value for frame length check (0 to use default)
 */
static s32 dwc_tx_sad_write(struct osi_core_priv_data *const osi_core,
				u16 sa_index, u32 pn_or_ssci,
				u8 tci, u32 active,
				u32 mtu, u32 port_id,
				u32 sys_id_lo, u32 sys_id_hi,
				u32 xpn_lo, u32 xpn_hi)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	/* Wait for TX SAD not busy */
	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	/* TX_SAD_0_LOW: PN (or SSCI in XPN mode) */
	osi_writela(osi_core, pn_or_ssci, base + DWC_MACSEC_TX_SAD_0_LOW);

	/* TX_SAD_0_HIGH: ACTIVE | TCI (bits 6:2) */
	val = ((u32)(tci & 0x1FU) << DWC_MACSEC_TX_SAD0H_TCI_SHIFT);
	if (active != OSI_NONE) {
		val |= DWC_MACSEC_TX_SAD0H_ACTIVE;
	}
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAD_0_HIGH);

	/* TX_SAD_1_LOW: [31:16]SOFT_TTL [15:0]HARD_TTL (upper 16 bits each) */
	{
		u32 soft_ttl_hi = (DWC_DEFAULT_PN_THRESHOLD >> 16) & 0xFFFFU;
		u32 hard_ttl_hi = 0xFFFFU; /* max PN threshold */
		u32 sad1_low = (soft_ttl_hi << DWC_MACSEC_TX_SAD1L_SOFT_TTL_SHIFT) |
				    (hard_ttl_hi << DWC_MACSEC_TX_SAD1L_HARD_TTL_SHIFT);
		osi_writela(osi_core, sad1_low, base + DWC_MACSEC_TX_SAD_1_LOW);
	}

	/* TX_SAD_1_HIGH: [31:16]PORT_ID, [13:0]MTU */
	{
		u32 sad1_high = ((port_id & 0xFFFFU) << DWC_MACSEC_TX_SAD1H_PORT_SHIFT);
		if (mtu != OSI_NONE) {
			sad1_high |= (mtu & DWC_MACSEC_TX_SAD1H_MTU_MASK);
		}
		osi_writela(osi_core, sad1_high, base + DWC_MACSEC_TX_SAD_1_HIGH);
	}

	/* TX_SAD_2: SYSTEM_IDENTIFIER 64-bit (SCI) */
	osi_writela(osi_core, sys_id_lo, base + DWC_MACSEC_TX_SAD_2_LOW);
	osi_writela(osi_core, sys_id_hi, base + DWC_MACSEC_TX_SAD_2_HIGH);

	/* TX_SAD_3: Extended PN 64-bit (XPN) */
	osi_writela(osi_core, xpn_lo, base + DWC_MACSEC_TX_SAD_3_LOW);
	osi_writela(osi_core, xpn_hi, base + DWC_MACSEC_TX_SAD_3_HIGH);

	/* TX_SAD_PRG: trigger write */
	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAD_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAD_PG);
}

/**
 * @brief Read TX SAD entry to get current PN
 */
static s32 dwc_tx_sad_read_pn(struct osi_core_priv_data *const osi_core,
				  u16 sa_index, u32 *pn)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	/* TX_SAD_PRG: trigger read */
	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAD_PRG);

	/* Wait for data fetch ready */
	ret = dwc_poll_ready(osi_core, DWC_MACSEC_TX_SAD_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	*pn = osi_readla(osi_core, base + DWC_MACSEC_TX_SAD_0_LOW);
	return 0;
}

/**
 * @brief Read TX SAD entry fields: PN, TCI, ACTIVE
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  sa_index: SA index
 * @param[out] pn: Current PN
 * @param[out] tci: TCI field (5 bits, may be NULL to skip)
 * @param[out] active: 1=active, 0=inactive (may be NULL to skip)
 */
static s32 dwc_tx_sad_read(struct osi_core_priv_data *const osi_core,
				u16 sa_index, u32 *pn,
				u8 *tci, u32 *active)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SAD_PRG);

	ret = dwc_poll_ready(osi_core, DWC_MACSEC_TX_SAD_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	if (pn != OSI_NULL) {
		*pn = osi_readla(osi_core, base + DWC_MACSEC_TX_SAD_0_LOW);
	}

	val = osi_readla(osi_core, base + DWC_MACSEC_TX_SAD_0_HIGH);
	if (tci != OSI_NULL) {
		*tci = (u8)((val & DWC_MACSEC_TX_SAD0H_TCI_MASK) >>
				  DWC_MACSEC_TX_SAD0H_TCI_SHIFT);
	}
	if (active != OSI_NULL) {
		*active = ((val & DWC_MACSEC_TX_SAD0H_ACTIVE) != OSI_NONE) ?
			  1U : 0U;
	}

	return 0;
}

/**
 * @brief Write an RX SAD entry
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] sa_index: SA index (0 to DWC_MACSEC_RX_MAX_SA-1)
 * @param[in] lowest_pn: Lowest acceptable PN (anti-replay)
 * @param[in] active: 1=active, 0=inactive
 * @param[in] replay_protect: 1=enable replay protection
 */
static s32 dwc_rx_sad_write(struct osi_core_priv_data *const osi_core,
				u16 sa_index, u32 pn_or_ssci,
				u32 active, u32 replay_protect, u32 validate,
				u32 port_id,
				u32 sys_id_lo, u32 sys_id_hi,
				u32 xpn_lo, u32 xpn_hi)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	/* RX_SAD_0_LOW: PN (or SSCI in XPN mode) */
	osi_writela(osi_core, pn_or_ssci, base + DWC_MACSEC_RX_SAD_0_LOW);

	/* RX_SAD_0_HIGH: ACTIVE, REPLAY_EN, VALIDATE, PORT_ID */
	val = (port_id & DWC_MACSEC_RX_SAD0H_PORT_ID_MASK);
	if (active != OSI_NONE) {
		val |= DWC_MACSEC_RX_SAD0H_ACTIVE;
		/* Set VALIDATE mode from caller (bits 28:27) */
		val |= ((validate & 0x3U) << DWC_MACSEC_RX_SAD0H_VALIDATE_SHIFT);
	}
	if (replay_protect != OSI_NONE) {
		val |= DWC_MACSEC_RX_SAD0H_REPLAY_EN;
	}
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAD_0_HIGH);

	/* RX_SAD_1: SYSTEM_IDENTIFIER 64-bit (SCI) */
	osi_writela(osi_core, sys_id_lo, base + DWC_MACSEC_RX_SAD_1_LOW);
	osi_writela(osi_core, sys_id_hi, base + DWC_MACSEC_RX_SAD_1_HIGH);

	/* RX_SAD_2: Extended PN 64-bit (XPN) */
	osi_writela(osi_core, xpn_lo, base + DWC_MACSEC_RX_SAD_2_LOW);
	osi_writela(osi_core, xpn_hi, base + DWC_MACSEC_RX_SAD_2_HIGH);

	/* RX_SAD_PRG: trigger write */
	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAD_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAD_PG);
}

/**
 * @brief Read RX SAD entry to get current lowest PN
 */
static s32 dwc_rx_sad_read_pn(struct osi_core_priv_data *const osi_core,
				  u16 sa_index, u32 *lowest_pn)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	/* RX_SAD_PRG: trigger read */
	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAD_PRG);

	/* Wait for data fetch ready */
	ret = dwc_poll_ready(osi_core, DWC_MACSEC_RX_SAD_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	*lowest_pn = osi_readla(osi_core, base + DWC_MACSEC_RX_SAD_0_LOW);
	return 0;
}

/**
 * @brief Read RX SAD entry fields: lowest_pn, active, replay_protect
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  sa_index: SA index
 * @param[out] lowest_pn: Current lowest PN (may be NULL to skip)
 * @param[out] active: 1=active, 0=inactive (may be NULL to skip)
 * @param[out] replay_protect: 1=replay enabled (may be NULL to skip)
 */
static s32 dwc_rx_sad_read(struct osi_core_priv_data *const osi_core,
				    u16 sa_index, u32 *lowest_pn, u32 *active,
				    u32 *replay_protect, u32 *validate)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SAD_PG);
	if (ret < 0) {
		return ret;
	}

	val = ((u32)sa_index << DWC_MACSEC_SAD_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAD_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SAD_PRG);

	ret = dwc_poll_ready(osi_core, DWC_MACSEC_RX_SAD_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	if (lowest_pn != OSI_NULL) {
		*lowest_pn = osi_readla(osi_core, base + DWC_MACSEC_RX_SAD_0_LOW);
	}

	val = osi_readla(osi_core, base + DWC_MACSEC_RX_SAD_0_HIGH);
	if (active != OSI_NULL) {
		*active = ((val & DWC_MACSEC_RX_SAD0H_ACTIVE) != OSI_NONE) ?
			  1U : 0U;
	}
	if (replay_protect != OSI_NULL) {
		*replay_protect = ((val & DWC_MACSEC_RX_SAD0H_REPLAY_EN) != OSI_NONE) ?
				  1U : 0U;
	}
	if (validate != OSI_NULL) {
		*validate = (val & DWC_MACSEC_RX_SAD0H_VALIDATE_MASK) >>
			    DWC_MACSEC_RX_SAD0H_VALIDATE_SHIFT;
	}

	return 0;
}

/**
 * @brief Clear a TX SAD entry
 */
static s32 dwc_tx_sad_clear(struct osi_core_priv_data *const osi_core,
				u16 sa_index)
{
	return dwc_tx_sad_write(osi_core, sa_index, 0U, 0U, OSI_NONE, 0U, 0U,
				0U, 0U, 0U, 0U);
}

/**
 * @brief Clear an RX SAD entry
 */
static s32 dwc_rx_sad_clear(struct osi_core_priv_data *const osi_core,
				u16 sa_index)
{
	return dwc_rx_sad_write(osi_core, sa_index, 0U, OSI_NONE, OSI_NONE, 0U, 0U,
				0U, 0U, 0U, 0U);
}

/* ============================================================
 * RX SC correlation table programming
 * Maps RX SA index -> SC index for MIB counter grouping
 * ============================================================ */

static s32 dwc_rx_sc_corr_write(struct osi_core_priv_data *const osi_core,
				    u16 sa_index, u32 sc_index)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SC_CORR_PG);
	if (ret < 0) {
		return ret;
	}

	/* Set SC value */
	osi_writela(osi_core, sc_index, base + DWC_MACSEC_RX_SC_CORR_SC_VALUE);

	/* PRG: ENTRY = sa_index, TYPE = write */
	val = ((u32)sa_index << 1U) | 1U;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SC_CORR_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_SC_CORR_PG);
}

/* ============================================================
 * RX Anti-Replay Window (ARW) table programming
 * DWC_macsec 1.05a uses logarithmic ARW: the table stores an
 * exponent n, representing a window of size 2^n.
 * One entry per RX SA (depth = DWC_MACSEC_RX_MAX_SA = 64).
 * ============================================================ */

/**
 * @brief Convert linear pn_window to logarithmic ARW exponent
 *
 * In logarithmic mode the hardware interprets the ARW value as
 * a power-of-2 exponent: effective_window = 2^arw_val.
 * We compute ceil(log2(pn_window)) so the programmed window is
 * always >= the requested value.
 *
 * @param[in] pn_window: Linear replay window size from OSD
 * @return Logarithmic ARW exponent (0..31 for 32-bit PN)
 */
static u32 dwc_pn_window_to_arw(u32 pn_window)
{
	u32 arw = 0U;
	u32 v;

	if (pn_window <= 1U) {
		return 0U;
	}

	/* ceil(log2(pn_window)): count bit shifts of (pn_window - 1) */
	v = pn_window - 1U;
	while (v > 0U) {
		arw++;
		v >>= 1U;
	}

	/* Hardware ARW SRAM width is 5-bit (Logarithmic mode), max exponent is 31 (2^31).
	 * Clamp to 31 to prevent 32 overflow to 0 (which would turn a 4B window into 1). */
	if (arw > 31U) {
		arw = 31U;
	}

	return arw;
}

/**
 * @brief Write an ARW table entry for a given RX SA
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] sa_index: RX SA index (0 to DWC_MACSEC_RX_MAX_SA-1)
 * @param[in] pn_window: Linear replay window size from OSD layer
 * @return 0 on success, negative on error
 */
static s32 dwc_arw_write(struct osi_core_priv_data *const osi_core,
			     u16 sa_index, u32 pn_window)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 arw_val;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_ARW_PG);
	if (ret < 0) {
		return ret;
	}

	/* Convert linear window to logarithmic exponent */
	arw_val = dwc_pn_window_to_arw(pn_window);

	/* Write ARW value to TABLE_0 */
	osi_writela(osi_core, arw_val, base + DWC_MACSEC_RX_ARW_TABLE_0);

	/* PRG: ENTRY = sa_index, TYPE = write */
	val = ((u32)sa_index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_ARW_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_ARW_PG);
}

/**
 * @brief Clear an ARW table entry (set window to 0)
 */
static s32 dwc_arw_clear(struct osi_core_priv_data *const osi_core,
			     u16 sa_index)
{
	return dwc_arw_write(osi_core, sa_index, 0U);
}

/**
 * @brief Read an ARW table entry (logarithmic exponent)
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  sa_index: RX SA index
 * @param[out] arw_val: Logarithmic ARW exponent read from table
 */
static s32 dwc_arw_read(struct osi_core_priv_data *const osi_core,
			    u16 sa_index, u32 *arw_val)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_ARW_PG);
	if (ret < 0) {
		return ret;
	}

	/* ARW_PRG: trigger read */
	val = ((u32)sa_index << DWC_MACSEC_SAI_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SAI_PRG_TYPE_READ;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_ARW_PRG);

	ret = dwc_poll_ready(osi_core, DWC_MACSEC_RX_ARW_DATA_FETCH);
	if (ret < 0) {
		return ret;
	}

	*arw_val = osi_readla(osi_core, base + DWC_MACSEC_RX_ARW_TABLE_0);
	return 0;
}

/* ============================================================
 * TX/RX SALT table programming (XPN)
 *
 * DWC_macsec 1.05a with XPN enabled stores a 96-bit SSCI
 * (Short SCI) per SA in the SALT table. The SAD entries
 * reference SALT table entries by index.
 *
 * SSCI derivation (IEEE 802.1AE):
 *   SSCI[31:0]  = SCI[4:7] (Port ID)
 *   SSCI[63:32] = SCI[0:3] (System Identifier)
 *   SSCI[95:64] = SCI[4:7] (duplicated for 96-bit table entry)
 *
 * Both CONFIG_MACSEC and MACSEC_KEY_PROGRAM paths need SALT
 * programming since the DWC IP has XPN always enabled.
 *
 * TX SALT depth = DWC_MACSEC_TX_SALT_MAX (32 entries)
 * RX SALT depth = DWC_MACSEC_RX_SALT_MAX (512 entries)
 * ============================================================ */

/**
 * @brief Derive 96-bit SSCI from 8-byte SCI for SALT table programming.
 *
 * IEEE 802.1AE SSCI = Port ID (4B) || System Identifier (4B).
 * The 96-bit SALT entry duplicates SCI[4:7] for the upper 32 bits.
 *
 * @param[in]  sci:      8-byte Secure Channel Identifier
 * @param[out] salt:     12-byte (96-bit) SSCI buffer
 * @param[out] ssci_lo:  Lower 32 bits of 64-bit SSCI for SAD SSCI[31:0]
 * @param[out] ssci_hi:  Upper 32 bits of 64-bit SSCI for SAD SSCI[63:32]
 */
static void dwc_derive_ssci(const u8 *sci, u8 *salt,
			    u32 *ssci_lo, u32 *ssci_hi)
{
	/* SSCI[31:0] from SCI[4:7] (Port ID) */
	*ssci_lo = ((u32)sci[4] << 24) | ((u32)sci[5] << 16) |
		   ((u32)sci[6] << 8)  | (u32)sci[7];
	/* SSCI[63:32] from SCI[0:3] (System Identifier) */
	*ssci_hi = ((u32)sci[0] << 24) | ((u32)sci[1] << 16) |
		   ((u32)sci[2] << 8)  | (u32)sci[3];

	/* 96-bit SALT entry:
	 * salt[0:3] = SCI[0:3] (System ID — SSCI[63:32])
	 * salt[4:7] = SCI[4:7] (Port ID  — SSCI[31:0])
	 * salt[8:11]= SCI[4:7] (duplicated for upper 32)
	 */
	osi_memcpy(&salt[0], &sci[0], 4U);
	osi_memcpy(&salt[4], &sci[4], 4U);
	osi_memcpy(&salt[8], &sci[4], 4U);
}

/**
 * @brief Prepare XPN SSCI/SALT fields for an SA, resolving both
 *        CONFIG_MACSEC (kernel-provided) and MACSEC_KEY_PROGRAM
 *        (derived from SCI) paths.
 *
 * If sc_info->xpn == OSI_ENABLE and salt is non-zero, use the
 * pre-filled values (CONFIG_MACSEC path). Otherwise derive SSCI
 * from SCI (MACSEC_KEY_PROGRAM path). Returns the salt_idx for
 * SAD programming (typically == sa_idx).
 *
 * @param[in]  sc_info:  SC/SA information
 * @param[in]  sa_idx:   SA hardware index (used as SALT index)
 * @param[out] ssci_lo:  Lower 32 bits of SSCI for TX_SAD_2_LOW
 * @param[out] ssci_hi:  Upper 32 bits of SSCI for TX_SAD_2_HIGH
 * @param[out] salt_idx: SALT table index for SAD SALT_IDX field
 */
static void dwc_prepare_xpn_sad_fields(
		const struct osi_macsec_sc_info *const sc_info,
		u16 sa_idx,
		u32 *ssci_lo, u32 *ssci_hi,
		u16 *salt_idx)
{
	u8 derived_salt[12];
	u32 slo, shi;
	static const u8 zero12[12];

	if (sc_info->xpn != OSI_ENABLE) {
		/* Non-XPN mode: zero SSCI/SALT */
		*ssci_lo = 0U;
		*ssci_hi = 0U;
		*salt_idx = 0U;
		return;
	}

	/* XPN mode: check if caller already filled salt (CONFIG_MACSEC path).
	 * If salt is non-zero, use pre-filled ssci/salt directly.
	 * Otherwise derive SSCI from SCI (MACSEC_KEY_PROGRAM path).
	 */
	if (osi_memcmp(sc_info->salt, zero12, sizeof(zero12)) !=
	    OSI_NONE_SIGNED) {
		/* Caller-provided SSCI (kernel MACsec offload) */
		*ssci_lo = sc_info->ssci;
		*ssci_hi = sc_info->next_pn_hi;
		*salt_idx = sa_idx;
		return;
	}

	/* MACSEC_KEY_PROGRAM path: derive SSCI from SCI */
	dwc_derive_ssci(sc_info->sci, derived_salt, &slo, &shi);
	*ssci_lo = slo;
	*ssci_hi = shi;
	*salt_idx = sa_idx;
}

/**
 * @brief Write a TX SALT table entry
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] sa_index: SA index (0 to DWC_MACSEC_TX_SALT_MAX-1)
 * @param[in] salt: 12-byte SSCI data (96 bits)
 */
static s32 dwc_tx_salt_write(struct osi_core_priv_data *const osi_core,
			     u16 sa_index, const u8 *salt)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_TX_SALT_PG);
	if (ret < 0) {
		return ret;
	}

	/* TABLE_0/1/2: 96-bit SSCI (12 bytes -> 3 x 32-bit) */
	val = ((u32)salt[0] << 24) | ((u32)salt[1] << 16) |
	      ((u32)salt[2] << 8) | (u32)salt[3];
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SALT_TABLE_0);

	val = ((u32)salt[4] << 24) | ((u32)salt[5] << 16) |
	      ((u32)salt[6] << 8) | (u32)salt[7];
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SALT_TABLE_1);

	val = ((u32)salt[8] << 24) | ((u32)salt[9] << 16) |
	      ((u32)salt[10] << 8) | (u32)salt[11];
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SALT_TABLE_2);

	/* PRG: trigger write */
	val = ((u32)sa_index << DWC_MACSEC_SALT_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SALT_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_SALT_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_TX_SALT_PG);
}

/**
 * @brief Write an RX SALT table entry
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] sa_index: SA index (0 to DWC_MACSEC_RX_SALT_MAX-1)
 * @param[in] salt: 12-byte SSCI data (96 bits)
 */
static s32 dwc_rx_salt_write(struct osi_core_priv_data *const osi_core,
			     u16 sa_index, const u8 *salt)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;
	s32 ret;

	ret = dwc_poll_busy(osi_core, DWC_MACSEC_RX_SALT_PG);
	if (ret < 0) {
		return ret;
	}

	/* TABLE_0/1/2: 96-bit SSCI */
	val = ((u32)salt[0] << 24) | ((u32)salt[1] << 16) |
	      ((u32)salt[2] << 8) | (u32)salt[3];
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SALT_TABLE_0);

	val = ((u32)salt[4] << 24) | ((u32)salt[5] << 16) |
	      ((u32)salt[6] << 8) | (u32)salt[7];
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SALT_TABLE_1);

	val = ((u32)salt[8] << 24) | ((u32)salt[9] << 16) |
	      ((u32)salt[10] << 8) | (u32)salt[11];
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SALT_TABLE_2);

	/* PRG: trigger write */
	val = ((u32)sa_index << DWC_MACSEC_SALT_PRG_ENTRY_SHIFT) |
	      DWC_MACSEC_SALT_PRG_TYPE_WRITE;
	osi_writela(osi_core, val, base + DWC_MACSEC_RX_SALT_PRG);

	return dwc_poll_busy(osi_core, DWC_MACSEC_RX_SALT_PG);
}

/**
 * @brief Clear a TX SALT table entry (write all zeros)
 */
static s32 dwc_tx_salt_clear(struct osi_core_priv_data *const osi_core,
			     u16 sa_index)
{
	u8 zero_salt[12] = {0};

	return dwc_tx_salt_write(osi_core, sa_index, zero_salt);
}

/**
 * @brief Clear an RX SALT table entry (write all zeros)
 */
static s32 dwc_rx_salt_clear(struct osi_core_priv_data *const osi_core,
			     u16 sa_index)
{
	u8 zero_salt[12] = {0};

	return dwc_rx_salt_write(osi_core, sa_index, zero_salt);
}

/**
 * @brief Clear all TX and RX SALT table entries
 */
static s32 dwc_clear_all_salt(struct osi_core_priv_data *const osi_core)
{
	u16 i;
	s32 ret;

	for (i = 0; i < DWC_MACSEC_TX_SALT_MAX; i++) {
		ret = dwc_tx_salt_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	for (i = 0; i < DWC_MACSEC_RX_SALT_MAX; i++) {
		ret = dwc_rx_salt_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

/* ============================================================
 * TX Active AN management
 * TX_ACTIVE_AN (0x60): bitmap, 1 bit per SC
 * Bit = 0 -> SA0 active, Bit = 1 -> SA1 active
 * ============================================================ */

static void dwc_tx_set_active_an(struct osi_core_priv_data *const osi_core,
				 u32 sc_index, u8 an)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;

	osi_lock_irq_enabled(&osi_core->macsec_fpe_lock);

	val = osi_readla(osi_core, base + DWC_MACSEC_TX_ACTIVE_AN);
	if ((an & 1U) != OSI_NONE) {
		val |= OSI_BIT(sc_index);
	} else {
		val &= ~OSI_BIT(sc_index);
	}
	osi_writela(osi_core, val, base + DWC_MACSEC_TX_ACTIVE_AN);

	osi_unlock_irq_enabled(&osi_core->macsec_fpe_lock);
}

/**
 * @brief Get active AN for a given TX SC
 *
 * @param[in]  osi_core: OSI core private data
 * @param[in]  sc_index: SC index (0..15)
 * @return Active AN value (0 or 1)
 */
static u8 dwc_tx_get_active_an(struct osi_core_priv_data *const osi_core,
				    u32 sc_index)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;

	val = osi_readla(osi_core, base + DWC_MACSEC_TX_ACTIVE_AN);
	return ((val & OSI_BIT(sc_index)) != OSI_NONE) ? 1U : 0U;
}

/* ============================================================
 * AES Key programming (via separate APB interface: tz_base)
 * ============================================================ */

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
/**
 * @brief Program an AES key into a specific key context
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] ctx_idx: Key context index (0 to DWC_MACSEC_AES_KEY_CTX-1)
 * @param[in] key: Key data (16 or 32 bytes)
 * @param[in] key_len_256: 1 for AES-256, 0 for AES-128
 * @param[in] encrypt: 1 for TX (encrypt), 0 for RX (decrypt)
 */
static s32 dwc_aes_key_program(struct osi_core_priv_data *const osi_core,
				   u32 ctx_idx,
				   const u8 *key,
				   u32 key_len_256,
				   u32 encrypt)
{
	u8 *base = (u8 *)osi_core->tz_base;
	u32 val;
	u32 i;
	u32 key_regs;
	s32 ret;

	/* Wait for AES engine not busy */
	ret = dwc_poll_aes_busy(osi_core);
	if (ret < 0) {
		return ret;
	}

	/* Write key data to KEY_0..KEY_7 in Big-Endian order per DWC driver manual section 3.2 step 2.a.i */
	key_regs = (key_len_256 != OSI_NONE) ? 8U : 4U;
	for (i = 0; i < key_regs; i++) {
		val = ((u32)key[i * 4U] << 24) |
		      ((u32)key[(i * 4U) + 1U] << 16) |
		      ((u32)key[(i * 4U) + 2U] << 8) |
		      (u32)key[(i * 4U) + 3U];
		osi_writela(osi_core, val, base + DWC_AES_KEY(i));
	}

	/* CTRL: set direction, mode, key size, context index */
	val = (ctx_idx & DWC_AES_CTRL_CTX_IDX_MASK);
	val |= DWC_AES_CTRL_MODE_GCM;
	if (key_len_256 != OSI_NONE) {
		val |= DWC_AES_CTRL_KEY_SZ_256;
	}
	if (encrypt != OSI_NONE) {
		val |= DWC_AES_CTRL_ENCRYPT;
	}
	osi_writela(osi_core, val, base + DWC_AES_CTRL);

	/* Wait for completion */
	ret = dwc_poll_aes_busy(osi_core);
	if (ret < 0) {
		return ret;
	}

	/* Security best practice per DWC driver manual section 3.2 step 2.a.iv:
	 * Clear KEY_0..KEY_7 to 0x0 to avoid key readback.
	 */
	for (i = 0; i < key_regs; i++) {
		osi_writela(osi_core, 0U, base + DWC_AES_KEY(i));
	}

	return 0;
}

/* ============================================================
 * FIPS 140-3 on-demand BIST (Built-In Self Test)
 *
 * Databook section 2.14.1:
 *   - Power-up BIST runs automatically after core reset
 *   - On-demand BIST can be re-triggered by software
 *   - Uses hard-coded test vectors, context index 96
 *
 * On-demand BIST flow (databook 2.14.1):
 *   1. Inhibit AES output via MISC_CONFIG.INHIBIT_OUTPUT
 *   2. Enable test mode via FIPS_SELF_TEST_CTL.ENA
 *   3. Setup BIST_VECT_MODE (batch mode = 0)
 *   4. Clear FIPS_SELF_TEST_STAT.DONE and .FAIL
 *   5. Trigger via BIST_VECT_CTL.BIST_TEST_GO
 *   6. Poll FIPS_SELF_TEST_STAT.SELF_TEST_DONE
 *   7. Check FIPS_SELF_TEST_STAT.SELF_TEST_FAIL
 *   8. On failure, read FAIL_CAUSE; optionally inhibit output
 *   9. Clear test mode and re-enable output
 * ============================================================ */

/**
 * @brief Run FIPS 140-3 on-demand BIST
 *
 * Must be called during init before any crypto operations.
 * Per FIPS 140-3, if BIST fails, the module MUST NOT
 * perform any cryptographic operations.
 *
 * @param[in] osi_core: OSI core private data
 * @return 0 on self-test PASS, negative on FAIL or timeout
 */
static s32 dwc_aes_fips_selftest(struct osi_core_priv_data *const osi_core)
{
	u8 *base = (u8 *)osi_core->tz_base;
	u32 val;
	u32 count = 0;

	/* Check if CTR mode is supported (CONFIG.CTR_EN reflects
	 * DWC_MACSEC_AES_FIPS_EN at synthesis time). If CTR_EN=0,
	 * FIPS is not synthesized �� skip self-test.
	 */
	val = osi_readla(osi_core, base + DWC_AES_CONFIG);
	if ((val & DWC_AES_CONFIG_CTR_EN) == OSI_NONE) {
		MACSEC_LOG("AES FIPS not supported in HW config\n");
		return 0;
	}

	MACSEC_LOG("Starting AES FIPS 140-3 on-demand BIST\n");

	/* Step 1: Inhibit AES-GCM output */
	osi_writela(osi_core, DWC_AES_MISC_INHIBIT_OUTPUT,
		    base + DWC_AES_MISC_CONFIG);

	/* Step 2: Enable FIPS self-test mode */
	osi_writela(osi_core, DWC_AES_FIPS_CTL_ENA,
		    base + DWC_AES_FIPS_SELF_TEST_CTL);

	/* Step 3: Setup BIST vector mode: Batch (all tests) */
	osi_writela(osi_core, DWC_AES_BIST_FUNCT_BATCH,
		    base + DWC_AES_BIST_VECT_MODE);

	/* Step 4: Clear previous DONE and FAIL status (W1C) */
	osi_writela(osi_core,
		    DWC_AES_FIPS_STAT_SELF_TEST_DONE |
		    DWC_AES_FIPS_STAT_SELF_TEST_FAIL,
		    base + DWC_AES_FIPS_SELF_TEST_STAT);

	/* Step 5: Trigger BIST (self-clearing bit) */
	osi_writela(osi_core, DWC_AES_BIST_TEST_GO,
		    base + DWC_AES_BIST_VECT_CTL);

	/* Step 6: Poll FIPS_SELF_TEST_STAT.SELF_TEST_DONE */
	while (count < DWC_AES_FIPS_POLL_COUNT) {
		val = osi_readla(osi_core,
				 base + DWC_AES_FIPS_SELF_TEST_STAT);
		if ((val & DWC_AES_FIPS_STAT_SELF_TEST_DONE) != OSI_NONE) {
			break;
		}
		count++;
		osi_core->osd_ops.udelay(1U);
	}

	if (count >= DWC_AES_FIPS_POLL_COUNT) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "AES FIPS BIST timeout\n", 0ULL);
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.aes_fips_fail);
		/* Inhibit output permanently on failure */
		goto cleanup;
	}

	/* Step 7: Check fail status */
	if ((val & DWC_AES_FIPS_STAT_SELF_TEST_FAIL) != OSI_NONE) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "AES FIPS BIST FAILED\n",
			     (u64)(val & DWC_AES_FIPS_STAT_FAIL_CAUSE_MASK));
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.aes_fips_fail);
		/* Leave output inhibited on failure */
		goto cleanup;
	}

	/* Step 8: Success �� disable test mode, re-enable output */
	osi_writela(osi_core, 0U, base + DWC_AES_FIPS_SELF_TEST_CTL);
	osi_writela(osi_core, 0U, base + DWC_AES_MISC_CONFIG);

	MACSEC_LOG("AES FIPS 140-3 BIST PASSED\n");
	return 0;

cleanup:
	/* Disable test mode but keep output inhibited */
	osi_writela(osi_core, 0U, base + DWC_AES_FIPS_SELF_TEST_CTL);
	osi_writela(osi_core, DWC_AES_MISC_INHIBIT_OUTPUT,
		    base + DWC_AES_MISC_CONFIG);
	return -1;
}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

/* ============================================================
 * MIB counter access
 * ============================================================ */

/**
 * @brief Read a single MIB counter value
 *
 * @param[in] osi_core: OSI core private data
 * @param[in] ctr_id: Counter ID (from DWC_MIB_* defines)
 * @param[in] obj_id: SA or SC index (0 for global counters)
 * @param[in] dir_tx: 1 for TX, 0 for RX
 * @param[in] clr: 1 to clear counter after read
 *
 * @return 64-bit counter value
 */
static u64 dwc_mib_read_counter(struct osi_core_priv_data *const osi_core,
				      u32 ctr_id, u32 obj_id,
				      u32 dir_tx, u32 clr)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 cmd_val;
	u32 lo, hi;
	u64 result;

	if (dwc_poll_mib_busy(osi_core) < 0) {
		return 0ULL;
	}

	/* Build MIB_CMD */
	cmd_val = DWC_MACSEC_MIB_CMD_RD_SGL;
	if (dir_tx != OSI_NONE) {
		cmd_val |= DWC_MACSEC_MIB_CMD_DIR_TX;
	}
	if (clr != OSI_NONE) {
		cmd_val |= DWC_MACSEC_MIB_CMD_CLR;
	}
	cmd_val |= ((ctr_id & 0x7FU) << DWC_MACSEC_MIB_CMD_CTR_ID_SHIFT);
	cmd_val |= (obj_id << DWC_MACSEC_MIB_CMD_OBJ_ID_SHIFT);

	osi_writela(osi_core, cmd_val, base + DWC_MACSEC_MIB_CMD);

	/* Wait for completion */
	if (dwc_poll_mib_busy(osi_core) < 0) {
		return 0ULL;
	}

	/* Read result from D_BUF[0] (first buffer pair) */
	lo = osi_readla(osi_core, base + DWC_MACSEC_MIB_D_BUF_LOW(0));
	hi = osi_readla(osi_core, base + DWC_MACSEC_MIB_D_BUF_HIGH(0));

	result = ((u64)hi << 32) | (u64)lo;
	return result;
}

/**
 * @brief Initialize all MIB counters for a given direction
 */
static s32 dwc_mib_init(struct osi_core_priv_data *const osi_core,
			    u32 dir_tx)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 cmd_val;

	if (dwc_poll_mib_busy(osi_core) < 0) {
		return -1;
	}

	/* INIT command: clears all counters for the given direction */
	cmd_val = DWC_MACSEC_MIB_CMD_INIT;
	if (dir_tx != OSI_NONE) {
		cmd_val |= DWC_MACSEC_MIB_CMD_DIR_TX;
	}
	osi_writela(osi_core, cmd_val, base + DWC_MACSEC_MIB_CMD);

	return dwc_poll_mib_busy(osi_core);
}

/* ============================================================
 * SA index calculation helpers
 * TX: sa_index = (sc_idx * DWC_MACSEC_TX_SA_PER_SC) + (an & 0x1)
 *     TX has 2 SA per SC, AN is 0 or 1
 * RX: sa_index = (sc_idx * DWC_MACSEC_RX_SA_PER_SC) + (an & 0x3)
 *     RX has 4 SA per SC, AN is 0-3
 * ============================================================ */

static inline u16 dwc_tx_sa_index(u32 sc_idx, u8 an)
{
	return (u16)((sc_idx * DWC_MACSEC_TX_SA_PER_SC) + (an & 0x1U));
}

static inline u16 dwc_rx_sa_index(u32 sc_idx, u8 an)
{
	return (u16)((sc_idx * DWC_MACSEC_RX_SA_PER_SC) + (an & 0x3U));
}

/* ============================================================
 * Main enable/disable
 * DWC_macsec uses bypass bits (set=bypass, clear=active)
 * ============================================================ */

static s32 dwc_macsec_enable(struct osi_core_priv_data *const osi_core,
				 u32 enable)
{
	u32 val;
	u8 *base = (u8 *)osi_core->macsec_base;
	s32 ret = 0;

	osi_lock_irq_enabled(&osi_core->macsec_fpe_lock);

	val = osi_readla(osi_core, base + DWC_MACSEC_IP_CONTROL);

	if (enable == OSI_ENABLE) {
		MACSEC_LOG("Enabling macsec TX and RX\n");
		/* Clear bypass bits to enable MACsec processing */
		val &= ~DWC_MACSEC_CTRL_TX_BYPASS;
		val &= ~DWC_MACSEC_CTRL_RX_BYPASS;
		osi_core->is_macsec_enabled = OSI_ENABLE;
	} else {
		MACSEC_LOG("Disabling macsec TX and RX\n");
		/* Set bypass bits to disable MACsec processing */
		val |= DWC_MACSEC_CTRL_TX_BYPASS;
		val |= DWC_MACSEC_CTRL_RX_BYPASS;
		osi_core->is_macsec_enabled = OSI_DISABLE;
	}

	osi_writela(osi_core, val, base + DWC_MACSEC_IP_CONTROL);

	osi_unlock_irq_enabled(&osi_core->macsec_fpe_lock);
	return ret;
}

/* ============================================================
 * LUT config (compatibility with existing osi_macsec_core_ops)
 *
 * Maps the Omni LUT-based interface to DWC SAI/SAD operations.
 * The lut_sel field determines which DWC table to program:
 *   0 (BYP LUT)    -> TX/RX SAI with bypass outcome
 *   1 (SCI LUT)    -> TX/RX SAI with macsec outcome
 *   2 (SC_PARAM)   -> TX/RX SAD for SC parameters (key index mapping)
 *   3 (SC_STATE)   -> TX_ACTIVE_AN for TX, no-op for RX
 *   4 (SA_STATE)   -> TX/RX SAD for PN/lowest_pn
 * ============================================================ */

static s32 dwc_lut_config(struct osi_core_priv_data *const osi_core,
			      struct osi_macsec_lut_config *const lut_config)
{
	u16 ctlr = lut_config->table_config.ctlr_sel;
	u16 index = lut_config->table_config.index;
	u16 rw = lut_config->table_config.rw;
	s32 ret = 0;

	switch (lut_config->lut_sel) {
	case OSI_LUT_SEL_BYPASS:
		if (rw == OSI_LUT_WRITE) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				if ((lut_config->flags & OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) {
					ret = dwc_tx_sai_write(osi_core, index, 0U,
							       DWC_MACSEC_TX_SAI_OUTCOME_BYPASS,
							       lut_config->lut_in.sa,
							       lut_config->lut_in.da);
				} else {
					ret = dwc_tx_sai_clear(osi_core, index);
				}
			} else {
				if ((lut_config->flags & OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) {
					ret = dwc_rx_sai_write(osi_core, index,
							       lut_config->sci_lut_out.sci,
							       0U);
				} else {
					ret = dwc_rx_sai_clear(osi_core, index);
				}
			}
		} else {
			/* Read: get SAI entry fields */
			if (ctlr == OSI_CTLR_SEL_TX) {
				u32 outcome = 0U;
				ret = dwc_tx_sai_read(osi_core, index, OSI_NULL,
						      &outcome,
						      lut_config->lut_in.sa,
						      lut_config->lut_in.da);
				if (ret == 0 && outcome == DWC_MACSEC_TX_SAI_OUTCOME_BYPASS) {
					lut_config->flags |= OSI_LUT_FLAGS_ENTRY_VALID;
				}
			} else {
				u8 an = 0U;
				ret = dwc_rx_sai_read(osi_core, index,
						      lut_config->sci_lut_out.sci,
						      &an);
			}
		}
		break;

	case OSI_LUT_SEL_SCI:
		if (rw == OSI_LUT_WRITE) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				if ((lut_config->flags & OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) {
					ret = dwc_tx_sai_write(osi_core, index,
							       lut_config->sci_lut_out.sc_index,
							       DWC_MACSEC_TX_SAI_OUTCOME_MACSEC,
							       lut_config->lut_in.sa,
							       OSI_NULL);
				} else {
					ret = dwc_tx_sai_clear(osi_core, index);
				}
			} else {
				if ((lut_config->flags & OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) {
					/* For RX, program SAI for each valid AN */
					u32 an_valid = lut_config->sci_lut_out.an_valid;
					u32 an;
					u32 sc_idx = lut_config->sci_lut_out.sc_index;

					for (an = 0; an < DWC_MACSEC_RX_SA_PER_SC; an++) {
						if ((an_valid & OSI_BIT(an)) != OSI_NONE) {
							u16 sa_idx = dwc_rx_sa_index(sc_idx, (u8)an);
							ret = dwc_rx_sai_write(osi_core, sa_idx,
									       lut_config->sci_lut_out.sci,
									       (u8)an);
							if (ret < 0) {
								break;
							}
						}
					}
				} else {
					ret = dwc_rx_sai_clear(osi_core, index);
				}
			}
		} else {
			/* Read: get SAI entry and populate sci_lut_out */
			if (ctlr == OSI_CTLR_SEL_TX) {
				u32 sc_idx = 0U;
				u32 outcome = 0U;
				ret = dwc_tx_sai_read(osi_core, index, &sc_idx,
						      &outcome,
						      lut_config->lut_in.sa, OSI_NULL);
				if (ret == 0) {
					lut_config->sci_lut_out.sc_index = sc_idx;
					if (outcome == DWC_MACSEC_TX_SAI_OUTCOME_MACSEC) {
						lut_config->flags |= OSI_LUT_FLAGS_ENTRY_VALID;
					}
				}
			} else {
				/* RX: read SAI for the given index to get SCI+AN */
				u8 an = 0U;
				ret = dwc_rx_sai_read(osi_core, index,
						      lut_config->sci_lut_out.sci,
						      &an);
				if (ret == 0) {
					lut_config->sci_lut_out.an_valid = OSI_BIT(an);
				}
			}
		}
		break;

	case OSI_LUT_SEL_SC_PARAM:
		/* DWC_macsec has no separate SC_PARAM table.
		 * Map SC_PARAM updates to SAD re-writes:
		 * - TX: re-program SAD with updated TCI
		 * - RX: re-program SAD with updated pn_window (replay)
		 */
		if (rw == OSI_LUT_WRITE) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				/* Re-read current PN, then rewrite SAD with new TCI */
				u32 cur_pn = 0U;
				u8 new_tci;

				/* Map Omni OSD TCI to DWC 5-bit TCI */
				new_tci = dwc_map_tci(lut_config->sc_param_out.tci,
						      lut_config->sc_param_out.encrypt);
				ret = dwc_tx_sad_read_pn(osi_core, index, &cur_pn);
				if (ret < 0) {
					break;
				}
				ret = dwc_tx_sad_write(osi_core, index, cur_pn,
						       new_tci, 1U, osi_core->mtu, 0U,
						       0U, 0U, 0U, 0U);
			} else {
				/* RX: re-program SAD with replay settings.
				 * Read current lowest_pn first to preserve it.
				 */
				u32 cur_lowest_pn = 0U;
				ret = dwc_rx_sad_read_pn(osi_core, index,
							 &cur_lowest_pn);
				if (ret < 0) {
					break;
				}
				ret = dwc_rx_sad_write(osi_core, index,
						       cur_lowest_pn, 1U,
						       (lut_config->sc_param_out.pn_window > 0U) ?
						       1U : 0U,
						       DWC_MACSEC_RX_SAD0H_VALIDATE_STRICT, 0U,
						       0U, 0U, 0U, 0U);
				if (ret < 0) {
					break;
				}
				/* Update ARW table with new replay window */
				ret = dwc_arw_write(osi_core, index,
						    lut_config->sc_param_out.pn_window);
			}
		} else {
			/* Read: reconstruct SC_PARAM from SAD + ARW */
			if (ctlr == OSI_CTLR_SEL_TX) {
				u32 pn = 0U;
				u8 tci = 0U;
				u32 active = 0U;

				ret = dwc_tx_sad_read(osi_core, index,
						      &pn, &tci, &active);
				if (ret == 0) {
					/* Convert DWC TCI back to OSD format */
					lut_config->sc_param_out.tci = dwc_unmap_tci(tci);
					if (active != OSI_NONE) {
						lut_config->flags |= OSI_LUT_FLAGS_ENTRY_VALID;
					}
				}
			} else {
				/* RX: read lowest_pn and ARW pn_window */
				u32 lowest_pn = 0U;
				u32 active = 0U;
				u32 replay = 0U;
				u32 arw_val = 0U;

				ret = dwc_rx_sad_read(osi_core, index,
						      &lowest_pn, &active, &replay,
						      OSI_NULL);
				if (ret < 0) {
					break;
				}
				ret = dwc_arw_read(osi_core, index, &arw_val);
				if (ret == 0) {
					/* Convert log ARW back to linear window */
					lut_config->sc_param_out.pn_window =
						(arw_val > 0U) ?
						(1U << arw_val) : 0U;
					if (active != OSI_NONE) {
						lut_config->flags |= OSI_LUT_FLAGS_ENTRY_VALID;
					}
				}
			}
		}
		break;

	case OSI_LUT_SEL_SC_STATE:
		/* SC state (current AN selection) */
		if (rw == OSI_LUT_WRITE) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				dwc_tx_set_active_an(osi_core, index,
						     (u8)lut_config->sc_state_out.curr_an);
			}
			/* RX doesn't have explicit SC state; AN is in SAI entry */
		} else {
			/* Read: get current active AN for TX SC */
			if (ctlr == OSI_CTLR_SEL_TX) {
				lut_config->sc_state_out.curr_an =
					(u32)dwc_tx_get_active_an(osi_core, index);
			}
			/* RX: AN comes from SAI entry, not a separate state */
		}
		break;

	case OSI_LUT_SEL_SA_STATE:
		if (rw == OSI_LUT_WRITE) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				u8 tx_tci = (lut_config->sa_state_out.encrypt != 0) ?
					    DWC_TCI_ENCRYPT_AUTH : DWC_TCI_AUTH_ONLY;

				ret = dwc_tx_sad_write(osi_core, index,
						       lut_config->sa_state_out.next_pn,
						       tx_tci,
						       ((lut_config->flags &
							 OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) ?
						       1U : 0U,
						       osi_core->mtu, 0U,
						       0U, 0U, 0U, 0U);
			} else {
				ret = dwc_rx_sad_write(osi_core, index,
						       lut_config->sa_state_out.lowest_pn,
						       ((lut_config->flags &
							 OSI_LUT_FLAGS_ENTRY_VALID) != OSI_NONE) ?
						       1U : 0U,
						       1U, /* replay protect enabled */
						       DWC_MACSEC_RX_SAD0H_VALIDATE_STRICT, 0U,
						       0U, 0U, 0U, 0U);
			}
		} else {
			/* Read operation: get current PN from SAD */
			if (ctlr == OSI_CTLR_SEL_TX) {
				ret = dwc_tx_sad_read_pn(osi_core, index,
							 &lut_config->sa_state_out.next_pn);
			} else {
				ret = dwc_rx_sad_read_pn(osi_core, index,
							 &lut_config->sa_state_out.lowest_pn);
			}
		}
		break;

	default:
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "Invalid lut_sel\n", (u64)lut_config->lut_sel);
		ret = -1;
		break;
	}

	return ret;
}

/* ============================================================
 * Key table config (compatibility wrapper)
 * ============================================================ */

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
/**
 * @brief Read cached SAK from software lut_status for a key index.
 *
 * DWC_macsec KEY_x registers are write-only; hardware key readback is
 * not supported.  Return the SAK cached in macsec_lut_status during
 * SA creation, together with the H-key if MACSEC_KEY_PROGRAM is active.
 */
static s32 dwc_kt_read_cached(struct osi_core_priv_data *const osi_core,
			      u16 index, u16 ctlr,
			      struct osi_macsec_kt_config *const kt_config)
{
	struct osi_macsec_lut_status *lut_status;
	u32 sa_per_sc;
	u32 sc_idx;
	u32 an;
	u32 i;

	sa_per_sc = (ctlr == OSI_CTLR_SEL_TX) ?
		    DWC_MACSEC_TX_SA_PER_SC : DWC_MACSEC_RX_SA_PER_SC;
	sc_idx = (u32)index / sa_per_sc;
	an = (u32)index % sa_per_sc;

	lut_status = &osi_core->macsec_lut_status[ctlr];
	for (i = 0; i < OSI_MAX_NUM_SC; i++) {
		if (lut_status->sc_info[i].an_valid == OSI_NONE)
			continue;
		if (lut_status->sc_info[i].sc_idx_start == sc_idx &&
		    (lut_status->sc_info[i].an_valid & OSI_BIT(an))) {
			osi_memcpy(kt_config->entry.sak,
				   lut_status->sc_info[i].sak,
				   OSI_KEY_LEN_256);
#if defined(MACSEC_KEY_PROGRAM)
			osi_memcpy(kt_config->entry.h,
				   lut_status->sc_info[i].hkey,
				   OSI_KEY_LEN_128);
#endif
			kt_config->flags |= OSI_LUT_FLAGS_ENTRY_VALID;
			return 0;
		}
	}

	return -1;
}

static s32 dwc_kt_config(struct osi_core_priv_data *const osi_core,
			     struct osi_macsec_kt_config *const kt_config)
{
	u16 ctlr = kt_config->table_config.ctlr_sel;
	u16 rw = kt_config->table_config.rw;
	u16 index = kt_config->table_config.index;
	u32 encrypt = (ctlr == OSI_CTLR_SEL_TX) ? 1U : 0U;
	u32 key_len_256 = (osi_core->macsec_cipher ==
				OSI_MACSEC_CIPHER_AES256) ? 1U : 0U;

	if (rw != OSI_LUT_WRITE) {
		/* KEY_x registers are write-only; return cached SAK */
		return dwc_kt_read_cached(osi_core, index, ctlr,
					  kt_config);
	}

	if ((kt_config->flags & OSI_LUT_FLAGS_ENTRY_VALID) == OSI_NONE) {
		/* Invalidate: write all zeros */
		u8 zero_key[OSI_KEY_LEN_256] = {0};

		return dwc_aes_key_program(osi_core, index, zero_key,
					   0U, encrypt);
	}

	return dwc_aes_key_program(osi_core, index, kt_config->entry.sak,
				   key_len_256, encrypt);
}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */
/* ============================================================
 * Cipher configuration
 * DWC_macsec selects cipher via AES_CTRL.KEY_SZ per key context,
 * not a global register. This function just stores the setting.
 * ============================================================ */

static s32 dwc_cipher_config(struct osi_core_priv_data *const osi_core,
				 u32 cipher)
{
	/* DWC_macsec sets key size per-context in AES_CTRL.KEY_SZ.
	 * Store the cipher selection for use during key programming.
	 */
	MACSEC_LOG("Cipher config: %u\n", cipher);
	osi_core->macsec_cipher = cipher;
	return 0;
}

/* ============================================================
 * MACsec MMC counter reading
 * DWC_macsec uses indirect MIB_CMD to read counters
 * ============================================================ */

static void dwc_read_mmc(struct osi_core_priv_data *const osi_core)
{
	struct osi_macsec_mmc_counters *mmc = &osi_core->macsec_mmc;
	u16 i;

	/* TX Global counters */
	mmc->tx_pkts_untaged = dwc_mib_read_counter(osi_core,
		DWC_MIB_TX_UNTAGGED_PKTS_GBL, 0U, 1U, 0U);
	mmc->tx_pkts_too_long = dwc_mib_read_counter(osi_core,
		DWC_MIB_TX_TOO_LONG_PKTS_GBL, 0U, 1U, 0U);
	mmc->tx_octets_protected = dwc_mib_read_counter(osi_core,
		DWC_MIB_TX_OCTETS_PROTECTED_GBL, 0U, 1U, 0U);

	/* RX Global counters */
	mmc->rx_pkts_no_tag = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_NO_TAG_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_pkts_untagged = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_UNTAGGED_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_pkts_bad_tag = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_BAD_TAG_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_pkts_no_sa_err = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_NO_SA_ERROR_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_pkts_no_sa = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_NO_SA_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_pkts_overrun = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_OVERRUN_PKTS_GBL, 0U, 0U, 0U);
	mmc->rx_octets_validated = dwc_mib_read_counter(osi_core,
		DWC_MIB_RX_OCTETS_VALIDATED_GBL, 0U, 0U, 0U);

	/* Per-SC counters */
	for (i = 0; i < OSI_MACSEC_SC_INDEX_MAX && i < DWC_MACSEC_TX_MAX_SC; i++) {
		mmc->tx_pkts_protected[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_TX_PROTECTED_PKTS_SC, i, 1U, 0U);
	}

	for (i = 0; i < OSI_MACSEC_SC_INDEX_MAX && i < DWC_MACSEC_RX_MAX_SC; i++) {
		mmc->rx_pkts_late[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_LATE_PKTS_SC, i, 0U, 0U);
		mmc->rx_pkts_delayed[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_DELAYED_PKTS_SC, i, 0U, 0U);
		mmc->rx_pkts_not_valid[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_NOT_VALID_PKTS_SC, i, 0U, 0U);
		mmc->in_pkts_invalid[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_INVALID_PKTS_SC, i, 0U, 0U);
		mmc->rx_pkts_unchecked[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_UNCHECKED_SC, i, 0U, 0U);
		mmc->rx_pkts_ok[i] = dwc_mib_read_counter(osi_core,
			DWC_MIB_RX_OK_PKTS_SC, i, 0U, 0U);
	}
}

/* ============================================================
 * IRQ handler
 * DWC_macsec uses IRQ_GLBL_STAT (W1C) + per-SA IRQ registers
 * ============================================================ */

static void dwc_handle_tx_sa_irq(struct osi_core_priv_data *const osi_core,
				 u32 sa_idx)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 stat;

	stat = osi_readla(osi_core, base + DWC_MACSEC_IRQ_TX_SA_STAT(sa_idx));
	if (stat == OSI_NONE) {
		return;
	}

	if ((stat & DWC_MACSEC_IRQ_TX_SA_PN_THR) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.tx_pn_threshold);
		MACSEC_LOG("TX SA %u: PN threshold reached\n", sa_idx);
		if (osi_core->osd_ops.macsec_pn_event != OSI_NULL) {
			u32 sc_idx = sa_idx / DWC_MACSEC_TX_SA_PER_SC;

			osi_core->osd_ops.macsec_pn_event(
				osi_core->osd,
				sa_idx, sc_idx,
				OSI_MACSEC_PN_EVENT_THRESHOLD,
				OSI_CTLR_SEL_TX);
		}
	}

	if ((stat & DWC_MACSEC_IRQ_TX_SA_PN_EXHAUST) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.tx_pn_exhausted);
		MACSEC_LOG("TX SA %u: PN exhausted\n", sa_idx);
		if (osi_core->osd_ops.macsec_pn_event != OSI_NULL) {
			u32 sc_idx = sa_idx / DWC_MACSEC_TX_SA_PER_SC;

			osi_core->osd_ops.macsec_pn_event(
				osi_core->osd,
				sa_idx, sc_idx,
				OSI_MACSEC_PN_EVENT_EXHAUSTED,
				OSI_CTLR_SEL_TX);
		}
	}

	if ((stat & DWC_MACSEC_IRQ_TX_SA_TOO_LONG) != OSI_NONE) {
		MACSEC_LOG("TX SA %u: Frame too long\n", sa_idx);
	}

	if ((stat & DWC_MACSEC_IRQ_TX_SA_INACTIVE) != OSI_NONE) {
		MACSEC_LOG("TX SA %u: Inactive SA hit\n", sa_idx);
	}

	/* W1C: clear handled interrupts */
	osi_writela(osi_core, stat, base + DWC_MACSEC_IRQ_TX_SA_STAT(sa_idx));
}

static void dwc_handle_rx_sa_irq(struct osi_core_priv_data *const osi_core,
				 u32 sa_idx)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 stat;

	stat = osi_readla(osi_core, base + DWC_MACSEC_IRQ_RX_SA_STAT(sa_idx));
	if (stat == OSI_NONE) {
		return;
	}

	if ((stat & DWC_MACSEC_IRQ_RX_SA_CHECK_FAIL) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.rx_icv_err_threshold);
		MACSEC_LOG("RX SA %u: ICV check failed\n", sa_idx);
	}

	if ((stat & DWC_MACSEC_IRQ_RX_SA_PRE_REPLAY) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.rx_replay_error);
		MACSEC_LOG("RX SA %u: Pre-replay check failed\n", sa_idx);
	}

	if ((stat & DWC_MACSEC_IRQ_RX_SA_POST_REPLAY) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.rx_replay_error);
		MACSEC_LOG("RX SA %u: Post-replay check failed\n", sa_idx);
	}

	

	if ((stat & DWC_MACSEC_IRQ_RX_SA_BAD_SECTAG) != OSI_NONE) {
		MACSEC_LOG("RX SA %u: Bad SecTAG\n", sa_idx);
	}

	if ((stat & DWC_MACSEC_IRQ_RX_SA_UNKNOWN_CIPH) != OSI_NONE) {
		MACSEC_LOG("RX SA %u: Unknown cipher suite\n", sa_idx);
	}

	/* W1C: clear handled interrupts */
	osi_writela(osi_core, stat, base + DWC_MACSEC_IRQ_RX_SA_STAT(sa_idx));
}

/* ============================================================
 * AES APB IRQ handler (tz_base domain)
 * Databook 6.2.1 / 6.2.2: IRQ_EN / IRQ_STAT bit fields
 *   bit 31 = GLBL (master enable, EN only)
 *   bit 17 = FSM_PAR_ERR
 *   bit 16 = REG_PAR_ERR
 *   bit  4 = CTX_IDX_ERR
 *   bit  0 = KEY_DONE
 * ============================================================ */

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
static void dwc_handle_aes_irq(struct osi_core_priv_data *const osi_core)
{
	u8 *base = (u8 *)osi_core->tz_base;
	u32 stat;

	stat = osi_readla(osi_core, base + DWC_AES_IRQ_STAT);
	if (stat == OSI_NONE) {
		return;
	}

	MACSEC_LOG("AES IRQ: STAT=0x%x\n", stat);

	if ((stat & DWC_AES_IRQ_FSM_PAR_ERR) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.aes_fsm_par_err);
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "AES FSM parity error\n", 0ULL);
	}

	if ((stat & DWC_AES_IRQ_REG_PAR_ERR) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.aes_reg_par_err);
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "AES register parity error\n", 0ULL);
	}

	if ((stat & DWC_AES_IRQ_CTX_IDX_ERR) != OSI_NONE) {
		CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.aes_ctx_idx_err);
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "AES context index error\n", 0ULL);
	}

	if ((stat & DWC_AES_IRQ_KEY_DONE) != OSI_NONE) {
		MACSEC_LOG("AES key programming completed\n");
	}

	/* W1C: clear all handled AES interrupts */
	osi_writela(osi_core, stat, base + DWC_AES_IRQ_STAT);
}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

static void dwc_handle_irq(struct osi_core_priv_data *const osi_core)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 glbl_stat;
	u32 i;

	/* Handle global error interrupts */
	glbl_stat = osi_readla(osi_core, base + DWC_MACSEC_IRQ_GLBL_STAT);
	if (glbl_stat != OSI_NONE) {
		MACSEC_LOG("DWC MACsec IRQ: GLBL_STAT=0x%x\n", glbl_stat);

		if ((glbl_stat & DWC_MACSEC_IRQ_GLBL_RX_UNKNOWN_SC) != OSI_NONE) {
			CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.rx_lkup_miss);
		}
		if ((glbl_stat & DWC_MACSEC_IRQ_GLBL_TX_CRC_ERR) != OSI_NONE) {
			CERT_C__POST_INC__U64(osi_core->macsec_irq_stats.tx_sc_an_not_valid);
		}
		if ((glbl_stat & DWC_MACSEC_IRQ_GLBL_AES_KEY_ERR) != OSI_NONE) {
			MACSEC_LOG("AES key error\n");
		}

		/* W1C: clear global interrupt status */
		osi_writela(osi_core, glbl_stat, base + DWC_MACSEC_IRQ_GLBL_STAT);
	}

	/* Poll per-SA TX interrupts independently */
	for (i = 0; i < DWC_MACSEC_TX_MAX_SA; i++) {
		dwc_handle_tx_sa_irq(osi_core, i);
	}

	/* Poll per-SA RX interrupts independently */
	for (i = 0; i < DWC_MACSEC_RX_MAX_SA; i++) {
		dwc_handle_rx_sa_irq(osi_core, i);
	}

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
	/* Handle AES APB domain interrupts (tz_base) */
	dwc_handle_aes_irq(osi_core);
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */
}

/* ============================================================
 * Clear all SAI and SAD tables
 * ============================================================ */

static s32 dwc_clear_all_sai(struct osi_core_priv_data *const osi_core)
{
	u16 i;
	s32 ret;

	/* Clear all TX SAI entries (per-SC, 16 entries) */
	for (i = 0; i < DWC_MACSEC_TX_MAX_SC; i++) {
		ret = dwc_tx_sai_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	/* Clear all RX SAI entries (per-SA, 64 entries) */
	for (i = 0; i < DWC_MACSEC_RX_MAX_SA; i++) {
		ret = dwc_rx_sai_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static s32 dwc_clear_all_sad(struct osi_core_priv_data *const osi_core)
{
	u16 i;
	s32 ret;

	/* Clear all TX SAD entries (per-SA, 32 entries) */
	for (i = 0; i < DWC_MACSEC_TX_MAX_SA; i++) {
		ret = dwc_tx_sad_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	/* Clear all RX SAD entries (per-SA, 64 entries) */
	for (i = 0; i < DWC_MACSEC_RX_MAX_SA; i++) {
		ret = dwc_rx_sad_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static s32 dwc_clear_all_arw(struct osi_core_priv_data *const osi_core)
{
	u16 i;
	s32 ret;

	/* Clear all RX ARW entries (per-SA, 64 entries) */
	for (i = 0; i < DWC_MACSEC_RX_MAX_SA; i++) {
		ret = dwc_arw_clear(osi_core, i);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

/* ============================================================
 * Set bypass SAI entries for MKPDU and broadcast packets
 * These are equivalent to Omni's BYP LUT entries
 * ============================================================ */

static s32 dwc_set_bypass_entries(struct osi_core_priv_data *const osi_core)
{
	u8 *base = (u8 *)osi_core->macsec_base;
	u32 val;

	/* Ethertype bypass filters: frames matching these ethertypes
	 * pass through without MACsec processing. Required per
	 * IEEE 802.1AE-2018 section 9.3.1 (PAUSE/Slow Protocol/EAPoL
	 * shall not be MACsec-protected).
	 *
	 * ETHERTYPE_FILT_0: 0x888E = EAPoL (MKA key agreement)
	 * ETHERTYPE_FILT_1: 0x8809 = Slow Protocols
	 *   (PAUSE 0x0001, LACP 0x0002, OAM 0x0003, MRP 0x000E)
	 * ETHERTYPE_FILT_2: 0x88CC = LLDP
	 */
	osi_writela(osi_core, 0x888EU,
		    base + DWC_MACSEC_ETHERTYPE_FILT(0));
	osi_writela(osi_core, 0x8809U,
		    base + DWC_MACSEC_ETHERTYPE_FILT(1));
	osi_writela(osi_core, 0x88CCU,
		    base + DWC_MACSEC_ETHERTYPE_FILT(2));

	/* Enable ethertype filter in IP_CONTROL */
	val = osi_readla(osi_core, base + DWC_MACSEC_IP_CONTROL);
	val |= DWC_MACSEC_CTRL_ETHERTYPE_FILT_EN;
	osi_writela(osi_core, val, base + DWC_MACSEC_IP_CONTROL);

	return 0;
}

/* ============================================================
 * MTU update
 * DWC_macsec doesn't have separate MTU registers like Omni.
 * MTU is handled per-SA via SAD_1 registers, or the MAC layer.
 * ============================================================ */

static s32 dwc_update_mtu(struct osi_core_priv_data *const osi_core,
			      u32 mtu)
{
	/* Store MTU; will be applied to TX SAD entries when SAs are configured.
	 * DWC_macsec 1.05a checks MTU per-SA via TX_SAD_1_HIGH[13:0].
	 */
	osi_core->mtu = mtu;
	MACSEC_LOG("MTU update: %u\n", mtu);
	return 0;
}

/* ============================================================
 * SC/SA configuration (osi_macsec_core_ops.config)
 *
 * This is the main entry point for creating/enabling/disabling SAs.
 * Maps Omni's SC-based configuration to DWC_macsec's SAI/SAD/AES model.
 * ============================================================ */

static s32 dwc_find_existing_sc(struct osi_core_priv_data *const osi_core,
				    const struct osi_macsec_sc_info *const sc,
				    u16 ctlr)
{
	struct osi_macsec_lut_status *lut_status;
	u32 i;

	lut_status = &osi_core->macsec_lut_status[ctlr];
	for (i = 0; i < OSI_MAX_NUM_SC; i++) {
		if (osi_memcmp(lut_status->sc_info[i].sci, sc->sci,
			       (s32)OSI_SCI_LEN) == OSI_NONE_SIGNED) {
			if (lut_status->sc_info[i].an_valid != OSI_NONE) {
				return (s32)i;
			}
		}
	}
	return -1;
}

static s32 dwc_get_avail_sc_idx(struct osi_core_priv_data *const osi_core,
				    u16 ctlr)
{
	struct osi_macsec_lut_status *lut_status;
	u32 max_sc;
	u32 i;

	lut_status = &osi_core->macsec_lut_status[ctlr];
	max_sc = (ctlr == OSI_CTLR_SEL_TX) ?
		 DWC_MACSEC_TX_MAX_SC : DWC_MACSEC_RX_MAX_SC;
	if (max_sc > OSI_MAX_NUM_SC) {
		max_sc = OSI_MAX_NUM_SC;
	}

	for (i = 0; i < max_sc; i++) {
		if (lut_status->sc_info[i].an_valid == OSI_NONE) {
			return (s32)i;
		}
	}
	return -1;
}



static s32 dwc_add_update_sa(struct osi_core_priv_data *const osi_core,
				 struct osi_macsec_sc_info *const sc_info,
				 u16 ctlr, u16 *kt_idx)
{
	u16 sa_idx;
	s32 ret;
	u32 key_len_256 = (osi_core->macsec_cipher ==
				OSI_MACSEC_CIPHER_AES256) ? 1U : 0U;

	if (ctlr == OSI_CTLR_SEL_TX) {
		sa_idx = dwc_tx_sa_index(sc_info->sc_idx_start, sc_info->curr_an);
		*kt_idx = sa_idx;

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
		/* Program AES key */
		if (sc_info->flags == OSI_CREATE_SA) {
			ret = dwc_aes_key_program(osi_core, sa_idx,
						  sc_info->sak, key_len_256, 1U);
			if (ret < 0) {
				OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
					     "TX key program failed\n", 0ULL);
				return ret;
			}
		}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */
		/* Program TX SAD — always include XPN SSCI/SALT fields since
	 * DWC IP_CONTROL.XPNSEL is always set (hardware XPN always on).
	 * For both CONFIG_MACSEC and MACSEC_KEY_PROGRAM, derive SSCI
	 * from SCI per IEEE 802.1AE.
	 */
		{
			u8 salt_buf[12];
			u32 sys_id_lo = 0U, sys_id_hi = 0U;
			u32 pn_or_ssci = sc_info->next_pn;
			u32 xpn_lo = 0U, xpn_hi = 0U;
			u16 salt_idx = sa_idx;

			/* System Identifier from SCI[0:5] */
			sys_id_lo = ((u32)sc_info->sci[2] << 24) |
				    ((u32)sc_info->sci[3] << 16) |
				    ((u32)sc_info->sci[4] << 8)  |
				    (u32)sc_info->sci[5];
			sys_id_hi = ((u32)sc_info->sci[0] << 8) |
				    (u32)sc_info->sci[1];

			if (sc_info->xpn == OSI_ENABLE) {
				u32 ssci_lo = 0U, ssci_hi = 0U;
				dwc_prepare_xpn_sad_fields(sc_info, sa_idx,
							   &ssci_lo, &ssci_hi,
							   &salt_idx);
				pn_or_ssci = ssci_lo;
				xpn_lo = sc_info->next_pn;
				xpn_hi = sc_info->next_pn_hi;
			}

			u32 port_id = ((u32)sc_info->sci[6] << 8) | (u32)sc_info->sci[7];
			ret = dwc_tx_sad_write(osi_core, sa_idx,
					       pn_or_ssci,
					       DWC_TCI_ENCRYPT_AUTH, 1U,
					       osi_core->mtu, port_id,
					       sys_id_lo, sys_id_hi,
					       xpn_lo, xpn_hi);
			if (ret < 0) {
				return ret;
			}

			if (sc_info->xpn == OSI_ENABLE) {
				dwc_derive_ssci(sc_info->sci, salt_buf,
						&sys_id_lo, &sys_id_hi);
				ret = dwc_tx_salt_write(osi_core, salt_idx, salt_buf);
				if (ret < 0) {
					return ret;
				}
			}
		}

		/* Program TX SAI */
		ret = dwc_tx_sai_write(osi_core, sc_info->sc_idx_start,
				       sc_info->sc_idx_start,
				       DWC_MACSEC_TX_SAI_OUTCOME_MACSEC,
				       sc_info->sci, OSI_NULL);
		if (ret < 0) {
			return ret;
		}

		/* Set active AN */
		if (sc_info->flags == OSI_ENABLE_SA) {
			dwc_tx_set_active_an(osi_core, sc_info->sc_idx_start,
					     sc_info->curr_an);
		}
	} else {
		/* RX */
		sa_idx = dwc_rx_sa_index(sc_info->sc_idx_start, sc_info->curr_an);
		*kt_idx = sa_idx;

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
		if (sc_info->flags == OSI_CREATE_SA) {
			u16 rx_key_idx = sa_idx + DWC_MACSEC_TX_MAX_SA;
			ret = dwc_aes_key_program(osi_core, rx_key_idx,
						  sc_info->sak, key_len_256, 0U);
			if (ret < 0) {
				OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
					     "RX key program failed\n", 0ULL);
				return ret;
			}
		}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */
		/* DWC manual 3.2: RX order = Key → SAD → SALT → ARW -> SAI → MIB SC */
		{
			u8 salt_buf[12];
			u32 validate_frames = DWC_MACSEC_RX_SAD0H_VALIDATE_STRICT;
			u32 sys_id_lo = 0U, sys_id_hi = 0U;
			u32 pn_or_ssci = sc_info->lowest_pn;
			u32 xpn_lo = 0U, xpn_hi = 0U;
			u16 salt_idx = sa_idx;

#ifdef CONFIG_MACSEC
			validate_frames = sc_info->validate_frames;
#endif /* CONFIG_MACSEC */

			/* System Identifier from SCI[0:5] */
			sys_id_lo = ((u32)sc_info->sci[2] << 24) |
				    ((u32)sc_info->sci[3] << 16) |
				    ((u32)sc_info->sci[4] << 8)  |
				    (u32)sc_info->sci[5];
			sys_id_hi = ((u32)sc_info->sci[0] << 8) |
				    (u32)sc_info->sci[1];

			if (sc_info->xpn == OSI_ENABLE) {
				u32 ssci_lo = 0U, ssci_hi = 0U;
				dwc_prepare_xpn_sad_fields(sc_info, sa_idx,
							   &ssci_lo, &ssci_hi,
							   &salt_idx);
				pn_or_ssci = ssci_lo;
				xpn_lo = sc_info->lowest_pn;
				xpn_hi = sc_info->lowest_pn_hi;
			}

			/* Step 6: RX SAD first (DWC manual 3.2: SAD→SALT→ARW→SAI) */
			u32 port_id = ((u32)sc_info->sci[6] << 8) | (u32)sc_info->sci[7];
			ret = dwc_rx_sad_write(osi_core, sa_idx,
					       pn_or_ssci, 1U, 1U,
					       validate_frames, port_id,
					       sys_id_lo, sys_id_hi,
					       xpn_lo, xpn_hi);
			if (ret < 0) {
				return ret;
			}

			if (sc_info->xpn == OSI_ENABLE) {
				dwc_derive_ssci(sc_info->sci, salt_buf,
						&sys_id_lo, &sys_id_hi);
				ret = dwc_rx_salt_write(osi_core, salt_idx, salt_buf);
				if (ret < 0) {
					return ret;
				}
			}

			/* Program RX ARW → Step 9: RX SAI — ARW must be ready before SAI lookup */
			ret = dwc_arw_write(osi_core, sa_idx, sc_info->pn_window);
			if (ret < 0) {
				return ret;
			}

			/* RX SAI */
			ret = dwc_rx_sai_write(osi_core, sa_idx,
					       sc_info->sci, sc_info->curr_an);
			if (ret < 0) {
				return ret;
			}

			/* Program RX SC correlation for MIB counters */
			ret = dwc_rx_sc_corr_write(osi_core, sa_idx,
						   sc_info->sc_idx_start);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return 0;
}

static s32 dwc_delete_sa(struct osi_core_priv_data *const osi_core,
			     const struct osi_macsec_sc_info *const sc_info,
			     u8 an, u16 ctlr)
{
	u16 sa_idx;
	s32 ret;

	if (ctlr == OSI_CTLR_SEL_TX) {
		sa_idx = dwc_tx_sa_index(sc_info->sc_idx_start, an);
		ret = dwc_tx_sad_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
		/* Clear TX SALT entry (XPN) */
		ret = dwc_tx_salt_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
		/* If no more valid ANs, clear the SAI entry too */
	} else {
		sa_idx = dwc_rx_sa_index(sc_info->sc_idx_start, an);
		ret = dwc_rx_sad_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
		ret = dwc_rx_sai_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
		/* Clear RX Anti-Replay Window entry */
		ret = dwc_arw_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
		/* Clear RX SALT entry (XPN) */
		ret = dwc_rx_salt_clear(osi_core, sa_idx);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static s32 dwc_configure(struct osi_core_priv_data *const osi_core,
			     struct osi_macsec_sc_info *const sc,
			     u32 enable, u16 ctlr,
			     u16 *kt_idx)
{
	struct osi_macsec_lut_status *lut_status;
	struct osi_macsec_sc_info *existing_sc;
	s32 sc_sw_idx;
	s32 ret = 0;

	if (kt_idx == OSI_NULL) {
		return -1;
	}
	if ((ctlr != OSI_CTLR_SEL_TX) && (ctlr != OSI_CTLR_SEL_RX)) {
		return -1;
	}

	lut_status = &osi_core->macsec_lut_status[ctlr];

	/* Find existing SC */
	sc_sw_idx = dwc_find_existing_sc(osi_core, sc, ctlr);

	if (sc_sw_idx < 0) {
		/* SC not found */
		if (enable == OSI_DISABLE) {
			OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
				     "Cannot delete non-existing SC\n", 0ULL);
			return -1;
		}

		/* Allocate new SC */
		sc_sw_idx = dwc_get_avail_sc_idx(osi_core, ctlr);
		if (sc_sw_idx < 0) {
			OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
				     "No available SC index\n", 0ULL);
			return -1;
		}

		existing_sc = &lut_status->sc_info[sc_sw_idx];
		osi_memcpy(existing_sc->sci, sc->sci, OSI_SCI_LEN);
		osi_memcpy(existing_sc->sak, sc->sak, OSI_KEY_LEN_256);
#ifdef MACSEC_KEY_PROGRAM
		osi_memcpy(existing_sc->hkey, sc->hkey, OSI_KEY_LEN_128);
#endif
		existing_sc->sc_idx_start = (u32)sc_sw_idx;
		existing_sc->curr_an = sc->curr_an;
		existing_sc->next_pn = sc->next_pn;
		existing_sc->lowest_pn = sc->lowest_pn;
		existing_sc->pn_window = sc->pn_window;
		existing_sc->flags = sc->flags;
		existing_sc->an_valid = OSI_BIT(sc->curr_an & 0x1FU);

		ret = dwc_add_update_sa(osi_core, existing_sc, ctlr, kt_idx);
		if (ret < 0) {
			osi_memset(existing_sc, OSI_NONE, sizeof(*existing_sc));
			return ret;
		}
		lut_status->num_of_sc_used++;
	} else {
		/* SC exists */
		existing_sc = &lut_status->sc_info[sc_sw_idx];

		if (enable == OSI_DISABLE) {
			/* Delete SA */
			ret = dwc_delete_sa(osi_core, existing_sc,
					    sc->curr_an, ctlr);
			if (ret < 0) {
				return ret;
			}

			existing_sc->an_valid &= ~OSI_BIT(sc->curr_an & 0x1FU);
			*kt_idx = (ctlr == OSI_CTLR_SEL_TX) ?
				  dwc_tx_sa_index(existing_sc->sc_idx_start, sc->curr_an) :
				  dwc_rx_sa_index(existing_sc->sc_idx_start, sc->curr_an);

			if (existing_sc->an_valid == OSI_NONE) {
				/* All ANs removed, clear SC */
				if (ctlr == OSI_CTLR_SEL_TX) {
					dwc_tx_sai_clear(osi_core,
							 (u16)existing_sc->sc_idx_start);
				} else {
					/* Clear RX SC correlation entries */
					u8 a;
					for (a = 0; a < DWC_MACSEC_RX_SA_PER_SC; a++) {
						u16 rx_sa = dwc_rx_sa_index(
							existing_sc->sc_idx_start, a);
						dwc_rx_sc_corr_write(osi_core, rx_sa, 0U);
					}
				}
				if (lut_status->num_of_sc_used != OSI_NONE) {
					lut_status->num_of_sc_used--;
				}
				osi_memset(existing_sc, OSI_NONE,
					   sizeof(*existing_sc));
			}
		} else {
			/* Add/update SA to existing SC */
			if (sc->flags != OSI_ENABLE_SA) {
				/* Only copy key for CREATE_SA. During ENABLE_SA
				 * the caller may not have a valid key (kernel
				 * doesn't provide key during SA updates).
				 */
				osi_memcpy(existing_sc->sak, sc->sak,
					   OSI_KEY_LEN_256);
#ifdef MACSEC_KEY_PROGRAM
				osi_memcpy(existing_sc->hkey, sc->hkey,
					   OSI_KEY_LEN_128);
#endif
			}
			existing_sc->curr_an = sc->curr_an;
			existing_sc->next_pn = sc->next_pn;
			existing_sc->lowest_pn = sc->lowest_pn;
			existing_sc->pn_window = sc->pn_window;
			existing_sc->flags = sc->flags;
			existing_sc->an_valid |= OSI_BIT(sc->curr_an & 0x1FU);

			ret = dwc_add_update_sa(osi_core, existing_sc, ctlr, kt_idx);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return ret;
}

/* ============================================================
 * Get key index for a given SCI
 * ============================================================ */

static s32 dwc_get_sc_lut_key_index(
	struct osi_core_priv_data *const osi_core,
	u8 *sci, u32 *key_index, u16 ctlr)
{
	struct osi_macsec_lut_status *lut_status;
	u32 i;

	if ((sci == OSI_NULL) || (key_index == OSI_NULL)) {
		return -1;
	}

	lut_status = &osi_core->macsec_lut_status[ctlr];
	for (i = 0; i < OSI_MAX_NUM_SC; i++) {
		if (osi_memcmp(lut_status->sc_info[i].sci, sci,
			       (s32)OSI_SCI_LEN) == OSI_NONE_SIGNED) {
			if (ctlr == OSI_CTLR_SEL_TX) {
				*key_index = lut_status->sc_info[i].sc_idx_start *
					     DWC_MACSEC_TX_SA_PER_SC;
			} else {
				*key_index = lut_status->sc_info[i].sc_idx_start *
					     DWC_MACSEC_RX_SA_PER_SC;
			}
			return 0;
		}
	}

	return -1;
}

/* ============================================================
 * ETH Subsystem CSR: MACsec clock-gate, reset, SRAM zero
 * These registers are outside the MACsec IP, in the ETH CSR block.
 * ============================================================ */

/**
 * @brief Enable MACsec clock-gate and release reset via ETH CSR
 *
 * Sequence: set macsec_enable �� set macsec_rst_n �� SRAM zeroization
 *
 * @param[in] osi_core: OSI core private data (csr_base must be set)
 * @return 0 on success, -1 on failure
 */
static s32 dwc_eth_csr_macsec_init(
		struct osi_core_priv_data *const osi_core)
{
	u8 *csr = (u8 *)osi_core->csr_base;
	u32 val;

	if (csr == OSI_NULL) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "csr_base is NULL\n", 0ULL);
		return -1;
	}

	/* Step 1: Enable MACsec clock gate */
	val = osi_readla(osi_core, csr + ETH_CSR_CTRL_5);
	val |= ETH_CSR_MACSEC_ENABLE;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);

	/* Step 2: De-assert MACsec software reset */
	val |= ETH_CSR_MACSEC_RST_N;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);

	/* Small delay for reset propagation */
	osi_core->osd_ops.udelay(10U);

#ifdef DWC_MACSEC_USE_SRAM
	/* Step 3: SRAM zeroization (databook 2.15)
	 * Assert I_sram_zero_init �� fixed delay �� deassert.
	 * Note: O_sram_zero_done is not exposed in CSR, use fixed delay.
	 */
	val |= ETH_CSR_MACSEC_SRAM_ZERO_INIT;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);

	/* Wait for SRAM zeroization to complete.
	 * Worst case: all SRAM entries (~32+64 SAD + SALT + MIB + ARW)
	 * at ~1 entry/clk cycle @ 250MHz �� a few us. Use conservative delay.
	 */
	osi_core->osd_ops.udelay(100U);

	/* Deassert sram_zero_init */
	val &= ~ETH_CSR_MACSEC_SRAM_ZERO_INIT;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);

	/* Wait for zeroization done */
	osi_core->osd_ops.udelay(100U);

	MACSEC_LOG("ETH CSR: SRAM zeroization complete\n");
#endif /* DWC_MACSEC_USE_SRAM */

#ifdef DWC_MACSEC_SIDEBAND_LOCK
	/* Lock MACsec and AES APB configuration after init.
	 * This prevents accidental reconfiguration from other bus masters. */
	osi_writela(osi_core, ETH_CSR_MACSEC_ALL_LOCKS,
		    csr + ETH_CSR_CTRL_7);
	MACSEC_LOG("ETH CSR: sideband locks engaged\n");
#endif /* DWC_MACSEC_SIDEBAND_LOCK */

	MACSEC_LOG("ETH CSR: MACsec enabled and reset released\n");
	return 0;
}

/**
 * @brief Disable MACsec via ETH CSR: assert reset, gate clock
 *
 * @param[in] osi_core: OSI core private data
 */
static void dwc_eth_csr_macsec_deinit(
		struct osi_core_priv_data *const osi_core)
{
	u8 *csr = (u8 *)osi_core->csr_base;
	u32 val;

	if (csr == OSI_NULL) {
		return;
	}

#ifdef DWC_MACSEC_SIDEBAND_LOCK
	/* Release locks before shutdown */
	osi_writela(osi_core, 0U, csr + ETH_CSR_CTRL_7);
#endif /* DWC_MACSEC_SIDEBAND_LOCK */

	/* Assert MACsec reset, then disable clock gate */
	val = osi_readla(osi_core, csr + ETH_CSR_CTRL_5);
	val &= ~ETH_CSR_MACSEC_RST_N;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);

	val &= ~ETH_CSR_MACSEC_ENABLE;
	osi_writela(osi_core, val, csr + ETH_CSR_CTRL_5);
}

/* ============================================================
 * Initialization
 * ============================================================ */

static s32 dwc_macsec_initialize(struct osi_core_priv_data *const osi_core,
				     u32 mtu,
				     u8 *const macsec_vf_mac)
{
	u32 val;
	u8 *base = (u8 *)osi_core->macsec_base;
	s32 ret;

	if (osi_core->macsec_initialized == OSI_ENABLE) {
		return 0;
	}

	/* Enable MACsec in ETH subsystem CSR (clock-gate + reset + SRAM zero) */
	ret = dwc_eth_csr_macsec_init(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "ETH CSR MACsec init failed\n", 0ULL);
		return ret;
	}

	/* Configure MACsec_IP_Control:
	 * - Clear TX/RX bypass (enable MACsec processing)
	 * - Set data_mode for 25G XLGMII (128-bit)
	 * - XPN enabled (xpnsel = 1)
	 * - VLANMODE not-on-clear: VLAN tags encrypted with payload
	 * - Dual-IF: data_mode is fixed to 25G XLGMII;
	 *   if runtime GMII fallback is needed, reconfigure via
	 *   osi_core->mac_ver check and DWC_MACSEC_DATA_MODE_GMII_1G.
	 */
	val = osi_readla(osi_core, base + DWC_MACSEC_IP_CONTROL);
	MACSEC_LOG("Read IP_CONTROL: 0x%x\n", val);

	/* Hardware default: bypass=0 (MACsec active). With empty SAI/SAD
	 * tables and MIB_VALIDATE=NOT_STRICT, all frames lookup-miss
	 * -> bypassed. Encryption starts when first SA is added. */
	/* Set data_mode for 25G XLGMII [9:5] */
	val &= ~DWC_MACSEC_CTRL_DATA_MODE_MASK;
	val |= (DWC_MACSEC_DATA_MODE_XLGMII_25G <<
		DWC_MACSEC_CTRL_DATA_MODE_SHIFT);
	/* Enable XPN */
	val |= DWC_MACSEC_CTRL_XPNSEL | DWC_MACSEC_CTRL_USGMII;
	/* VLANMODE = 000 (not on the clear):
	 * VLAN tags are encrypted and authenticated together
	 * with the payload. No VLAN info in the clear.
	 */
	val &= ~DWC_MACSEC_CTRL_VLANMODE_MASK;
	val |= (DWC_MACSEC_VLANMODE_NOT_ON_CLEAR <<
		DWC_MACSEC_CTRL_VLANMODE_SHIFT);

	osi_writela(osi_core, val, base + DWC_MACSEC_IP_CONTROL);

	/* MIB_VALIDATE.PHY_MAP = 0 (disabled/check) during init.
	 * Per manual 1.8.1: do not set PHY_MAP during initial setup,
	 * otherwise all non-MACsec frames will be poisoned.
	 * PHY_MAP will be set to strict when a MACsec session is active.
	 */
	osi_writela(osi_core, DWC_MACSEC_MIB_VALIDATE_NOT_STRICT,
		    base + DWC_MACSEC_MIB_VALIDATE);

	/* Initialize MIB counters for both directions */
	ret = dwc_mib_init(osi_core, 1U); /* TX */
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "TX MIB init failed\n", 0ULL);
		return ret;
	}
	ret = dwc_mib_init(osi_core, 0U); /* RX */
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "RX MIB init failed\n", 0ULL);
		return ret;
	}

	/* Clear all SAI, SAD, SALT and ARW tables */
	ret = dwc_clear_all_sai(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "Clear SAI tables failed\n", 0ULL);
		return ret;
	}

	ret = dwc_clear_all_sad(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "Clear SAD tables failed\n", 0ULL);
		return ret;
	}

	/* Clear all XPN SALT table entries */
	ret = dwc_clear_all_salt(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "Clear SALT tables failed\n", 0ULL);
		return ret;
	}

	/* Clear all RX Anti-Replay Window entries */
	ret = dwc_clear_all_arw(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "Clear ARW table failed\n", 0ULL);
		return ret;
	}

	/* Clear TX_ACTIVE_AN */
	osi_writela(osi_core, 0U, base + DWC_MACSEC_TX_ACTIVE_AN);

	/* Enable global interrupts for TX SA PN threshold/exhaustion */
	val = 0U;
	/* Enable interrupts for all TX SAs */
	{
		u32 i;
		u32 tx_irq_en = DWC_MACSEC_IRQ_TX_SA_PN_THR |
				     DWC_MACSEC_IRQ_TX_SA_PN_EXHAUST |
				     DWC_MACSEC_IRQ_TX_SA_TOO_LONG |
				     DWC_MACSEC_IRQ_TX_SA_INACTIVE;
		u32 rx_irq_en = DWC_MACSEC_IRQ_RX_SA_CHECK_FAIL |
				     DWC_MACSEC_IRQ_RX_SA_PRE_REPLAY |
				     DWC_MACSEC_IRQ_RX_SA_POST_REPLAY |
				     DWC_MACSEC_IRQ_RX_SA_BAD_SECTAG;

		for (i = 0; i < DWC_MACSEC_TX_MAX_SA; i++) {
			osi_writela(osi_core, tx_irq_en | OSI_BIT(31),
				    base + DWC_MACSEC_IRQ_TX_SA_EN(i));
		}
		/* Enable interrupts for all RX SAs */
		for (i = 0; i < DWC_MACSEC_RX_MAX_SA; i++) {
			osi_writela(osi_core, rx_irq_en | OSI_BIT(31),
				    base + DWC_MACSEC_IRQ_RX_SA_EN(i));
		}
		/* Enable global IRQ with master enable (bit 31) and all error bits */
		val = DWC_MACSEC_IRQ_GLBL_MASTER_EN | DWC_MACSEC_IRQ_GLBL_ALL_ERRORS;
	}
	osi_writela(osi_core, val, base + DWC_MACSEC_IRQ_GLBL_EN);

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
	/* Enable AES APB domain interrupts (tz_base):
	 * Databook 6.2.1: bit 31=GLBL, 17=FSM_PAR_ERR,
	 * 16=REG_PAR_ERR, 4=CTX_IDX_ERR, 0=KEY_DONE
	 */
	{
		u8 *aes_base = (u8 *)osi_core->tz_base;

		osi_writela(osi_core,
			    DWC_AES_IRQ_GLBL | DWC_AES_IRQ_ALL,
			    aes_base + DWC_AES_IRQ_EN);
	}

	/* Run FIPS 140-3 self-test (on-demand BIST).
	 * Per FIPS 140-3, power-up self-test must pass before
	 * any cryptographic operations are allowed.
	 */
	ret = dwc_aes_fips_selftest(osi_core);
	if (ret < 0) {
		OSI_CORE_ERR(osi_core->osd, OSI_LOG_ARG_HW_FAIL,
			     "FIPS self-test failed, MACsec init aborted\n",
			     0ULL);
		return ret;
	}
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

	/* Set up bypass entries (MKPDU, broadcast) */
	ret = dwc_set_bypass_entries(osi_core);
	if (ret < 0) {
		return ret;
	}

	/* Do NOT enable MACsec yet; keep bypass active until SAs are configured.
	 * The OSD layer will call dwc_macsec_enable() when ready.
	 */

	/* Clear software SC tracking */
	osi_memset(&osi_core->macsec_lut_status[0], OSI_NONE,
		   sizeof(struct osi_macsec_lut_status));
	osi_memset(&osi_core->macsec_lut_status[1], OSI_NONE,
		   sizeof(struct osi_macsec_lut_status));

	osi_core->macsec_initialized = OSI_ENABLE;

	MACSEC_LOG("DWC MACsec 1.05a initialized\n");
	return 0;
}

/* ============================================================
 * De-initialization
 * ============================================================ */

static s32 dwc_macsec_deinit(struct osi_core_priv_data *const osi_core)
{
	s32 ret;

	/* Disable MACsec processing (set bypass) */
	ret = dwc_macsec_enable(osi_core, OSI_DISABLE);
	if (ret < 0) {
		return ret;
	}

	/* Clear all tables */
	dwc_clear_all_sai(osi_core);
	dwc_clear_all_sad(osi_core);
	dwc_clear_all_arw(osi_core);
	dwc_clear_all_salt(osi_core);

	/* Clear software state */
	osi_memset(&osi_core->macsec_lut_status[0], OSI_NONE,
		   sizeof(struct osi_macsec_lut_status));
	osi_memset(&osi_core->macsec_lut_status[1], OSI_NONE,
		   sizeof(struct osi_macsec_lut_status));

	/* Disable interrupts */
	osi_writela(osi_core, 0U,
		    (u8 *)osi_core->macsec_base + DWC_MACSEC_IRQ_GLBL_EN);

#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
	/* Disable AES APB domain interrupts */
	osi_writela(osi_core, 0U,
		    (u8 *)osi_core->tz_base + DWC_AES_IRQ_EN);
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */

	osi_core->macsec_initialized = OSI_DISABLE;

	/* Shutdown MACsec in ETH subsystem CSR (assert reset, gate clock) */
	dwc_eth_csr_macsec_deinit(osi_core);

	MACSEC_LOG("DWC MACsec de-initialized\n");
	return 0;
}

#ifdef DEBUG_MACSEC
/* ============================================================
 * Debug operations (stubs)
 * DWC_macsec 1.05a doesn't have the same debug buffer
 * architecture as Omni. Provide no-op stubs.
 * ============================================================ */

static s32 dwc_loopback_config(struct osi_core_priv_data *const osi_core,
				   u32 enable)
{
	/* DWC_macsec doesn't have a dedicated loopback mode register */
	MACSEC_LOG("Loopback config: %u (not supported on DWC)\n", enable);
	return 0;
}

static s32 dwc_dbg_buf_config(struct osi_core_priv_data *const osi_core,
				  struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	/* Not available on DWC_macsec 1.05a */
	return 0;
}

static s32 dwc_dbg_events_config(struct osi_core_priv_data *const osi_core,
				     struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	/* Not available on DWC_macsec 1.05a */
	return 0;
}

static void dwc_intr_config(struct osi_core_priv_data *const osi_core,
			    u32 enable)
{
	u8 *base = (u8 *)osi_core->macsec_base;

	if (enable == OSI_ENABLE) {
		osi_writela(osi_core, 0xFFFFFFFFU,
			    base + DWC_MACSEC_IRQ_GLBL_EN);
	} else {
		osi_writela(osi_core, 0U, base + DWC_MACSEC_IRQ_GLBL_EN);
	}
}
#endif /* DEBUG_MACSEC */

/* ============================================================
 * COE (Camera Over Ethernet) operations
 *
 * DWC_macsec 1.05a is a standard IEEE 802.1AE implementation and does NOT
 * have Omni's proprietary COE registers (COE_CONFIG at 0xA000,
 * COE_LINE_CNTR at 0xA004+ch*4).
 *
 * For MACsec-protected COE traffic, DWC_macsec handles frames transparently
 * via standard SAI LUT matching (DA/SA/EtherType) without needing knowledge
 * of the inner COE/AVTP header structure.
 *
 * The MGBE MAC-level COE DMA configuration (VDMA/PDMA buffers, split-header
 * offsets) is managed separately through osi_core_ops::config_coe_buf /
 * OSI_CMD_GMSL_COE_CONFIG, which is orthogonal to the MACsec path.
 * ============================================================ */

/**
 * @brief dwc_coe_config - Configure COE mode for DWC_macsec
 *
 * @param[in] osi_core:       OSI core private data structure
 * @param[in] coe_enable:     1 = COE enabled, 0 = COE disabled
 * @param[in] coe_hdr_offset: Byte offset of the COE header from SOF
 *                            (informational only; DWC IP has no COE_CONFIG reg)
 *
 * DWC_macsec 1.05a performs standard IEEE 802.1AE Rx processing regardless of
 * the inner AVTP/COE payload layout.  Returning success allows the upper layers
 * (sysfs, OSD) to track COE state while MGBE MAC registers handle the actual
 * DMA-level COE header split.
 *
 * @retval 0  Always succeeds (no DWC register to program).
 */
static s32 dwc_coe_config(struct osi_core_priv_data *const osi_core,
			      u32 coe_enable, u32 coe_hdr_offset)
{
	MACSEC_LOG("DWC COE config: enable=%u hdr_offset=%u"
		   " (no DWC hardware register; handled by MGBE MAC layer)\n",
		   coe_enable, coe_hdr_offset);

	/*
	 * Persist coe_enable in osi_core so that MAC-level paths that query
	 * osi_core->coe_enable stay consistent with the MACsec COE state.
	 */
	osi_core->coe_enable = coe_enable;

	return 0;
}

/**
 * @brief dwc_coe_lc - Configure COE line-counter thresholds for DWC_macsec
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] ch:       DMA channel index
 * @param[in] lc1:      Line-counter threshold 1 (14-bit value)
 * @param[in] lc2:      Line-counter threshold 2 (14-bit value)
 *
 * Omni's private MACsec had per-channel line-counter registers
 * (COE_LINE_CNTR at 0xA004+ch*4) tied to its proprietary VDMA integration.
 * DWC_macsec 1.05a has no equivalent hardware; line-counter-driven interrupts
 * for video DMA are managed by MGBE WRAP/PDMA registers outside the MACsec IP.
 *
 * @retval 0  Always succeeds (no-op stub).
 */
static s32 dwc_coe_lc(struct osi_core_priv_data *const osi_core,
			   u32 ch, u32 lc1, u32 lc2)
{
	MACSEC_LOG("DWC COE lc: ch=%u lc1=%u lc2=%u"
		   " (no DWC line-counter register; no-op)\n",
		   ch, lc1, lc2);
	return 0;
}

/* ============================================================
 * OSI operations struct and init function
 * ============================================================ */

void macsec_init_ops(void *macsecops)
{
	struct osi_macsec_core_ops *macsec_ops = (struct osi_macsec_core_ops *) macsecops;
	
	macsec_ops->init = dwc_macsec_initialize;
	macsec_ops->deinit = dwc_macsec_deinit;
	macsec_ops->handle_irq = dwc_handle_irq;
	macsec_ops->lut_config = dwc_lut_config;
#if defined(MACSEC_KEY_PROGRAM) || defined(CONFIG_MACSEC)
	macsec_ops->kt_config = dwc_kt_config;
#endif /* MACSEC_KEY_PROGRAM || CONFIG_MACSEC */
	macsec_ops->cipher_config = dwc_cipher_config;
	macsec_ops->config = dwc_configure;
	macsec_ops->read_mmc = dwc_read_mmc;
	macsec_ops->get_sc_lut_key_index = dwc_get_sc_lut_key_index;
	macsec_ops->update_mtu = dwc_update_mtu;
	macsec_ops->coe_config = dwc_coe_config;
	macsec_ops->coe_lc = dwc_coe_lc;
#ifdef DEBUG_MACSEC
	macsec_ops->loopback_config = dwc_loopback_config;
	macsec_ops->dbg_buf_config = dwc_dbg_buf_config;
	macsec_ops->dbg_events_config = dwc_dbg_events_config;
	macsec_ops->intr_config = dwc_intr_config;
#endif
#if 0
	s32 ret = 0;	
	static struct osi_macsec_core_ops virt_macsec_ops;
	if (osi_core->use_virtualization == OSI_ENABLE) {
		osi_core->macsec_ops = &virt_macsec_ops;
		ivc_init_macsec_ops(osi_core->macsec_ops);
	} else {
		if (osi_core->macsec_base == OSI_NULL) {
			ret = -1;
			goto exit;
		}
		osi_core->macsec_ops = &macsec_ops;
	}
exit:
	return ret;
#endif
}

/* ============================================================
 * Public API wrappers (same interface as Omni's)
 * These dispatch through the ops table.
 * ============================================================ */
#if 0
s32 osi_macsec_init(struct osi_core_priv_data *const osi_core,
			u32 mtu, u8 *const macsec_vf_mac)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->init != OSI_NULL)) {
		ret = osi_core->macsec_ops->init(osi_core, mtu, macsec_vf_mac);
	}
	return ret;
}
s32 osi_macsec_deinit(struct osi_core_priv_data *const osi_core)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->deinit != OSI_NULL)) {
		ret = osi_core->macsec_ops->deinit(osi_core);
	}
	return ret;
}

void osi_macsec_isr(struct osi_core_priv_data *const osi_core)
{
	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->handle_irq != OSI_NULL)) {
		osi_core->macsec_ops->handle_irq(osi_core);
	}
}

s32 osi_macsec_config_lut(struct osi_core_priv_data *const osi_core,
			      struct osi_macsec_lut_config *const lut_config)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->lut_config != OSI_NULL) &&
	    (lut_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->lut_config(osi_core, lut_config);
	}
	return ret;
}

#ifdef MACSEC_KEY_PROGRAM
s32 osi_macsec_config_kt(struct osi_core_priv_data *const osi_core,
			     struct osi_macsec_kt_config *const kt_config)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->kt_config != OSI_NULL) &&
	    (kt_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->kt_config(osi_core, kt_config);
	}
	return ret;
}
#endif /* MACSEC_KEY_PROGRAM */

s32 osi_macsec_cipher_config(struct osi_core_priv_data *const osi_core,
				 u32 cipher)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->cipher_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->cipher_config(osi_core, cipher);
	}
	return ret;
}

s32 osi_macsec_config(struct osi_core_priv_data *const osi_core,
			  struct osi_macsec_sc_info *const sc,
			  u32 enable, u16 ctlr,
			  u16 *kt_idx)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->config != OSI_NULL)) {
		ret = osi_core->macsec_ops->config(osi_core, sc, enable,
						   ctlr, kt_idx);
	}
	return ret;
}

s32 osi_macsec_read_mmc(struct osi_core_priv_data *const osi_core)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->read_mmc != OSI_NULL)) {
		osi_core->macsec_ops->read_mmc(osi_core);
		ret = 0;
	}
	return ret;
}

s32 osi_macsec_get_sc_lut_key_index(
	struct osi_core_priv_data *const osi_core,
	u8 *sci, u32 *key_index, u16 ctlr)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->get_sc_lut_key_index != OSI_NULL)) {
		ret = osi_core->macsec_ops->get_sc_lut_key_index(osi_core,
								 sci, key_index,
								 ctlr);
	}
	return ret;
}

s32 osi_macsec_update_mtu(struct osi_core_priv_data *const osi_core,
			      u32 mtu)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->update_mtu != OSI_NULL)) {
		ret = osi_core->macsec_ops->update_mtu(osi_core, mtu);
	}
	return ret;
}

#ifdef DEBUG_MACSEC
s32 osi_macsec_loopback(struct osi_core_priv_data *const osi_core,
			    u32 enable)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->loopback_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->loopback_config(osi_core, enable);
	}
	return ret;
}

s32 osi_macsec_config_dbg_buf(
	struct osi_core_priv_data *const osi_core,
	struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->dbg_buf_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->dbg_buf_config(osi_core,
							   dbg_buf_config);
	}
	return ret;
}

s32 osi_macsec_dbg_events_config(
	struct osi_core_priv_data *const osi_core,
	struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	s32 ret = -1;

	if ((osi_core != OSI_NULL) && (osi_core->macsec_ops != OSI_NULL) &&
	    (osi_core->macsec_ops->dbg_events_config != OSI_NULL)) {
		ret = osi_core->macsec_ops->dbg_events_config(osi_core,
							      dbg_buf_config);
	}
	return ret;
}
#endif /* DEBUG_MACSEC */
#endif

#endif /* MACSEC_SUPPORT */
