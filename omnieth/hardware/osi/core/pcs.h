/**
 * @file omni_pcs.h
 * @brief PCS Driver Header File
 * 
 * This header file defines the public API for the PCS driver.
 */

#ifndef __OMNI_PCS_H__
#define __OMNI_PCS_H__

#include <linux/platform_device.h>
#include <oeth_type.h>
#include "core_local.h"
#include <osi_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCS speed enumeration
 */
typedef enum {
    PCS_SPEED_UNKNOWN = 0,
    PCS_SPEED_10M = 10,
    PCS_SPEED_100M = 100,
    PCS_SPEED_1G = 1000,
    PCS_SPEED_2DOT5G = 2500,
    PCS_SPEED_5G = 5000,
    PCS_SPEED_10G = 10000,
    PCS_SPEED_25G = 25000
} pcs_speed_t;

/**
 * @brief XPCS duplex mode enumeration
 */
typedef enum {
    PCS_HALF_DUPLEX = 0,
    PCS_FULL_DUPLEX = 1
} pcs_duplex_t;

/**
 * @brief XPCS PCS type enumeration
 */
typedef enum {
    PCS_TYPE_25GBASE_R = 0,
    PCS_TYPE_10GBASE_KR = 1,
    PCS_TYPE_10GBASE_R = 2,
    PCS_TYPE_5GBASE_R = 3,
    PCS_TYPE_2DOT5GBASE_X = 4,
    PCS_TYPE_1GBASE_X = 5,
    PCS_TYPE_1G_SGMII = 6
} pcs_type_t;

/**
 * @brief XPCS USXGMII mode enumeration
 * 000: 10 Mbps 链路 
   001: 100 Mbps 链路 
   010: 1000 Mbps 链路 
   011: 10 Gbps 链路 
   100: 2.5 Gbps 链路 
   101: 5 Gbps 链路 
 */
typedef enum {
    XPCS_USXG_10M = 0,
    XPCS_USXG_100M = 1,
    XPCS_SGMII_1G = 2,
    XPCS_USXG_10G = 3,
    XPCS_USXG_2DOT5G = 4,
    XPCS_USXG_5G = 5
} xpcs_usxg_mode_t;

/**
 * @brief XPCS device structure
 */
struct pcs_device {
    struct device *dev;
    void * base_addr;                /* Base address of XPCS registers */
    pcs_speed_t current_speed;        /* Current link speed */
    pcs_duplex_t current_duplex;      /* Current duplex mode */
    u32 link_up;                      /* Link status */
    u32 an_enabled;                   /* Auto-negotiation enabled */
    u32 eee_enabled;                  /* EEE enabled */
    u32 fec_enabled;
    u32 kr_training_enabled;          /* KR training enabled (for Clause 72) */
    u32 flags;                    /* Device flags */
    pcs_speed_t max_speed;            /* Maximum supported speed */
    pcs_type_t pcs_mode;              /* PCS mode */
    u32 last_err;                /* Last error code */
    u32 eee_txtimer;
    u32 eee_rxtimer;
    u32 eee_fast_wake;
    struct osi_core_priv_data * osi_core;
};

/**
 * @brief Read PHY register using indirect access
 * 
 * The indirect access uses two-phase approach:
 * 1. Address phase: Write base address to offset 0x3FC
 * 2. Data phase: Read/write data using offset address
 * 
 * @param base_addr Base address of XPCS
 * @param reg_addr Register address (23-bit)
 * @return Value read from register
 */
static inline u32 pcs_read_reg(struct pcs_device * pcs, u32 reg_addr)
{
    osi_writela(pcs, ((reg_addr >> 10)&0x1FFFU), ((u8 *)(pcs->base_addr) + 0x3FC));
    return osi_readla(pcs, (u8 *)(pcs->base_addr) + (reg_addr & 0x3FF));
}

/**
 * @brief Write to PHY register using indirect access
 * 
 * @param base_addr Base address of XPCS
 * @param reg_addr Register address (23-bit)
 * @param data Data to write
 */
static inline void pcs_write_reg(struct pcs_device *pcs, u32 reg_addr, u32 data)
{
    osi_writela(pcs, ((reg_addr >> 10) & 0x1FFFU), ((u8 *)(pcs->base_addr) + 0x3FC));
    osi_writela(pcs, data, (u8 *)(pcs->base_addr) + (reg_addr & 0x3FF));
}

/**
 * @brief Safely write to PHY register with retry mechanism
 * 
 * @param base_addr Base address of XPCS
 * @param reg_addr Register address (23-bit)
 * @param data Data to write
 * @return 0 on success, -1 on failure
 */
static inline s32 pcs_write_reg_safe(struct pcs_device *pcs, u32 reg_addr, u32 data)
{
    u32 read_val;
    u32 retry = 10;
    s32 ret = -1;

    while (--retry > 0) {
        pcs_write_reg(pcs, reg_addr, data);
        read_val = pcs_read_reg(pcs, reg_addr);
        if (data == read_val) {
            ret = 0;
            break;
        }
        pcs->osi_core->osd_ops.udelay(1);
    }

    return ret;
}

/**
 * @brief Wait for register bit to clear
 * 
 * @param base_addr Base address of XPCS
 * @param reg_addr Register address
 * @param bit_mask Bit mask to check
 * @param timeout_us Timeout in microseconds
 * @return 0 on success, -1 on timeout
 */
static inline s32 wait_reg_clear(struct pcs_device *pcs, u32 reg_addr, 
                          u32 bit_mask, u32 timeout_us)
{
    u32 timeout = timeout_us;
    
    while (timeout-- > 0) {
        u32 val = pcs_read_reg(pcs, reg_addr);
        if (!(val & bit_mask))
            return 0;
        pcs->osi_core->osd_ops.udelay(1);
    }
    
    return -1;
}

/**
 * @brief Wait for register bit to set
 * 
 * @param base_addr Base address of XPCS
 * @param reg_addr Register address
 * @param bit_mask Bit mask to check
 * @param timeout_us Timeout in microseconds
 * @return 0 on success, -1 on timeout
 */
static inline s32 wait_reg_set(struct pcs_device *pcs, u32 reg_addr, 
                        u32 bit_mask, u32 timeout_us)
{
    u32 timeout = timeout_us;
    
    while (timeout-- > 0) {
        u32 val = pcs_read_reg(pcs, reg_addr);
        if (val & bit_mask)
            return 0;
        pcs->osi_core->osd_ops.udelay(1);
    }
    
    return -1;
}

/**
 * @brief Initialize XPCS device
 * 
 * @param xpcs Pointer to XPCS device structure
 * @param base_addr Base address of XPCS registers
 * @param config Configuration parameters (can be NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
s32 xpcs_init(struct osi_core_priv_data *osi_core);

/**
 * @brief xpcs_start - Start XPCS
 *
 * Algorithm: This routine enables AN and set speed based on AN status
 *
 * @param[in] xpcs: Pointer to XPCS device structure
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
s32 xpcs_start(struct pcs_device *xpcs);

/**
 * @brief xpcs_eee - XPCS enable/disable EEE
 *
 * Algorithm: This routine update register related to EEE
 * for XPCS.
 *
 * @param[in] xpcs: Pointer to XPCS device structure
 * @param[in] en_dis: enable - 1 or disable - 0
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
s32 xpcs_eee(struct pcs_device *xpcs, u32 en_dis);

/**
 * @brief Reset XPCS device
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return 0 on success, negative error code on failure
 */
int xpcs_reset(struct pcs_device *xpcs);

/**
 * @brief Get current speed
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return Current speed enumeration
 */
pcs_speed_t xpcs_get_speed(struct pcs_device *xpcs);

/**
 * @brief Get current duplex mode
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return Current duplex mode
 */
pcs_duplex_t xpcs_get_duplex(struct pcs_device *xpcs);


void xpcs_ue_intr_handle(struct pcs_device *xpcs);
void xpcs_ce_intr_handle(struct pcs_device *xpcs);
void xpcs_sbd_intr_handle(struct pcs_device *xpcs);

 s32 xpcs_set_duplex(struct pcs_device *xpcs, pcs_duplex_t duplex);
 u32 xpcs_get_link_status(struct pcs_device *xpcs);
 s32 xpcs_an_clear_intr(struct pcs_device *xpcs);
 s32 xpcs_cr_port_read(struct pcs_device *xpcs, u32 phy_addr, u32 *data, u32 is_xs);
 s32 xpcs_cr_port_write(struct pcs_device *xpcs, u32 phy_addr, u32 data, u32 is_xs);

/**
 * @brief Initialize XLGPCS device
 * 
 * @param xlgpcs Pointer to XLGPCS device structure
 * @param base_addr Base address of XLGPCS registers
 * @param config Configuration parameters (can be NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
s32 xlgpcs_init(struct osi_core_priv_data *osi_core);

/**
 * @brief xlgpcs_start - Start XLGPCS
 *
 * Algorithm: This routine enables AN and set speed based on AN status
 *
 * @param[in] xlgpcs: Pointer to XLGPCS device structure
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
s32 xlgpcs_start(struct pcs_device *xlgpcs);

/**
 * @brief xpcs_eee - XPCS enable/disable EEE
 *
 * Algorithm: This routine update register related to EEE
 * for XPCS.
 *
 * @param[in] xpcs: Pointer to XPCS device structure
 * @param[in] en_dis: enable - 1 or disable - 0
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
s32 xlgpcs_eee(struct pcs_device *xpcs, u32 en_dis);

/**
 * @brief Reset XPCS device
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return 0 on success, negative error code on failure
 */
int xlgpcs_reset(struct pcs_device *xpcs);

/**
 * @brief Get current speed
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return Current speed enumeration
 */
pcs_speed_t xlgpcs_get_speed(struct pcs_device *xpcs);

/**
 * @brief Get current duplex mode
 * 
 * @param xpcs Pointer to XPCS device structure
 * @return Current duplex mode
 */
pcs_duplex_t xlgpcs_get_duplex(struct pcs_device *xpcs);


void xlgpcs_ue_intr_handle(struct pcs_device *xlgpcs);
void xlgpcs_ce_intr_handle(struct pcs_device *xlgpcs);
void xlgpcs_sbd_intr_handle(struct pcs_device *xlgpcs);

int xlgpcs_cr_write(struct pcs_device *dev, u16 cr_addr, u16 cr_data);
int xlgpcs_cr_read(struct pcs_device *dev, u16 cr_addr, u16 *cr_data);
int xlgpcs_switch_to_25g(struct pcs_device *dev);
int xlgpcs_switch_to_10g(struct pcs_device *dev);
int xlgpcs_enable_clause72(struct pcs_device *dev);
int xlgpcs_adjust_coefficients(struct pcs_device *dev, u8 coeff_idx, u8 direction);
int xlgpcs_configure_clause73(struct pcs_device *dev);
int xlgpcs_start_clause73(struct pcs_device *dev);
int xlgpcs_set_advertisement(struct pcs_device *dev, u32 adv1, u32 adv2, u32 adv3);
int xlgpcs_set_next_page(struct pcs_device *dev, u32 np1, u32 np2, u32 np3);
int xlgpcs_enable_base_r_fec(struct pcs_device *dev);
int xlgpcs_disable_base_r_fec(struct pcs_device *dev);
int xlgpcs_enable_rs_fec(struct pcs_device *dev);
int xlgpcs_disable_rs_fec(struct pcs_device *dev);
int xlgpcs_get_link_status(struct pcs_device *dev, int *link_up);
int xlgpcs_get_pseq_state(struct pcs_device *dev, u8 *state);
int xlgpcs_an_reset(struct pcs_device *dev);
int xlgpcs_an_enable(struct pcs_device *dev, int enable);
int xlgpcs_enable_loopback(struct pcs_device *dev);
int xlgpcs_disable_loopback(struct pcs_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* __OMNI_PCS_H__ */
