/*
 * omni_xlgpcs.c - XLGPCS Driver Implementation
 * Based on xlgpcs_driver_HLD.md specification
 */

#include "pcs.h"
#include <osi_common.h>

/* Bit manipulation macros */
//#define BIT(x)                            (1UL << (x))

/* Base addresses */
#define XLGPCS0_BASE_ADDR                 0x18000
#define XLGPCS1_BASE_ADDR                 0x19000

/* Register offsets */
#define SR_PMA_CTRL1                      0x10000
#define SR_PMA_CTRL2                      0x10007
#define SR_PMA_KR_PMD_CTRL                0x010096
#define SR_PMA_KR_LP_CEU                  0x010098
#define SR_PMA_KR_LP_CESTS                0x010099
#define SR_PMA_KR_LD_CEU                  0x01009A
#define SR_PMA_KR_LD_CESTS                0x01009B
#define SR_PMA_KR_FEC_CTRL                0x0100AB
#define SR_PMA_RS_FEC_CTRL                0x0100C8
#define VR_PMA_DIG_CTRL1                  0x018000
#define VR_PMA_MP_25G_16G_12G_RX_GENCTRL0  0x018050
#define VR_PMA_MP_25G_16G_SRAM             0x01809A
#define VR_PMA_MP_25G_16G_CR_CTRL          0x0180A0
#define VR_PMA_MP_25G_16G_CR_ADDR          0x0180A1
#define VR_PMA_MP_25G_16G_CR_DATA          0x0180A2
#define VR_PMA_CWM00                       0x0180A3
#define VR_PMA_CWM01                       0x0180A4
#define VR_PMA_CWM02                       0x0180A5
#define VR_PMA_CWM03                       0x0180A6
#define VR_PMA_KRTR_RX_EQ_CTRL0            0x0180BC
#define VR_PMA_MP_32G_CNTX_LINK_NUM_CTRL   0x0180F0
#define VR_PMA_MP_32G_TX_CNTX_SEL0         0x018101
#define VR_PMA_MP_32G_CM_CNTX_SEL0         0x018104
#define VR_PMA_MP_32G_RX_CNTX_SEL0         0x018110
#define SR_VSMMD_CTRL                      0x01E009
#define SR_PCS_CTRL1                       0x030000
#define SR_PCS_STS1                        0x030001
#define SR_PCS_CTRL2                       0x030007
#define SR_PCS_EEE_ABL                     0x030014
#define VR_PCS_DIG_CTRL1                   0x038000
#define VR_PCS_DIG_CTRL3                   0x038003
#define VR_PCS_DEBUG_CTRL                  0x038005
#define VR_PCS_EEE_MCTRL                   0x038006
#define VR_PCS_EEE_TXTIMER                 0x038008
#define VR_PCS_EEE_RXTIMER                 0x038009
#define VR_PCS_DIG_STS                     0x038010
#define VR_PCS_AM_CNT                      0x038018
#define SR_AN_CTRL                         0x70000
#define SR_AN_ADV1                         0x70010
#define SR_AN_ADV2                         0x70011
#define SR_AN_ADV3                         0x70012
#define SR_AN_XNP_TX1                      0x70016
#define SR_AN_XNP_TX2                      0x70017
#define SR_AN_XNP_TX3                      0x70018
#define VR_AN_DIG_CTRL1                    0x078000
#define VR_AN_INTR                         0x078002

/* Bit field definitions */
/* SR_PMA_CTRL1 */
#define RST                               BIT(15)
#define LPM                               BIT(11)
#define LB                                BIT(0)

/* SR_PMA_CTRL2 */
#define PMA_TYPE_MASK                     0x7F
#define PMA_TYPE_25G_R                    0x39

/* SR_PMA_KR_PMD_CTRL */
#define TR_EN                             BIT(1)
#define RS_TR                             BIT(0)

/* SR_PMA_KR_FEC_CTRL */
#define FEC_EN                            BIT(0)

/* SR_PMA_RS_FEC_CTRL */
#define RSFEC_EN                          BIT(2)

/* VR_PMA_DIG_CTRL1 */
#define GAUI_MODE                         BIT(8)

/* VR_PMA_MP_25G_16G_12G_RX_GENCTRL0 */
#define RX_DT_EN_0                        BIT(8)
#define RX_DT_EN_3_1                      (BIT(11) | BIT(10) | BIT(9))

/* VR_PMA_MP_25G_16G_SRAM */
#define SRAM_INIT_DN                      BIT(0)
#define SRAM_EXT_LD_DN                    BIT(1)

/* VR_PMA_KRTR_RX_EQ_CTRL0 */
#define RX_EQ_MM                          BIT(15)

/* VR_PMA_MP_32G_CNTX_LINK_NUM_CTRL */
#define L0_LN                             (BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* SR_VSMMD_CTRL */
#define FASTSIM                           BIT(4)

/* SR_PCS_CTRL1 */
#define SS13                              BIT(13)
#define CS_EN                             BIT(10)
#define SS6                               BIT(6)
#define SS_5_2                            (BIT(5) | BIT(4) | BIT(3) | BIT(2))

/* SR_PCS_STS1 */
#define RLU                               BIT(2)

/* VR_PCS_DIG_CTRL1 */
#define EN_VSMMD1                         BIT(13)

/* VR_PCS_DIG_CTRL3 */
#define EN_50G                            BIT(1)
#define CNS_EN                            BIT(0)
#define CL72_EN                           BIT(2)

/* VR_PCS_DEBUG_CTRL */
#define SUPRESS_LOS_DET                   BIT(4)
#define RX_DT_ENCTL                       BIT(6)

/* VR_PCS_EEE_MCTRL */
#define LTX_EN                            BIT(0)
#define LRX_EN                            BIT(1)
#define EEE_SLR_BYP                       BIT(5)

/* SR_PCS_CTRL2 */
#define XLGPCS_TYPE_SEL                      (BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define XLGPCS_TYPE_25GBASE_R                0x7
#define XLGPCS_TYPE_10GBASE_KR               0x0

/* SR_PCS_EEE_ABL */
#define CGDSL                             BIT(13)
#define CGFWK                             BIT(12)
#define XXVGDSL                           BIT(11)
#define XXVGFWK                           BIT(10)
#define XLGDSL                            BIT(9)
#define XLGFWK                            BIT(8)
#define XLG_EEE                           BIT(7)
#define XG_EEE                            BIT(6)
#define KX4EEE                            BIT(5)
#define KXEEE                             BIT(4)
#define TEEE                              BIT(3)
#define EN_1GTEEE                         BIT(2)
#define EN_100TEE                         BIT(1)
#define FWK_EN                            BIT(0)

/* VR_PCS_DIG_STS */
#define PSEQ_STATE                        (BIT(2) | BIT(3) | BIT(4))
#define WAIT_ACK_HIGH_0                   0x0
#define WAIT_ACK_LOW_0                    0x4
#define WAIT_ACK_HIGH_1                   0x8
#define WAIT_ACK_LOW_1                    0xC
#define TX_RX_STABLE_0                    0x10
#define POWER_SAVE_STATE                  0x14
#define POWER_DOWN_STATE                  0x18
#define OTHER_INTERMEDIATE_STATES         0x1C

/* VR_PCS_AM_CNT */
#define AM_CNT                            0x400

/* SR_AN_CTRL */
#define AN_RST                            BIT(15)
#define AN_EN                             BIT(12)
#define RSTRT_AN                          BIT(9)

/* VR_AN_INTR */
#define AN_CMPLT_INTR                     BIT(0)

/* SR_PMA_KR_LP_CESTS */
#define LP_CFF_STSM0_MASK                 0x3
#define LP_CFF_NOT_UPDATED                0x0
#define LP_CFF_UPDATED                    0x1
#define LP_CFF_MIN                        0x2
#define LP_CFF_MAX                        0x3

/* SR_PMA_KR_LP_CEU */
#define LP_CFF_UPDTM1_MASK                0x3
#define LP_CFF_INCREMENT                  0x1
#define LP_CFF_DECREMENT                  0x2

/* Context selection values (to be loaded from context_sel_ID.txt) */
#define CTX_SEL_25G_CM                    0x0  /* To be configured based on actual values */
#define CTX_SEL_25G_TX                    0x0  /* To be configured based on actual values */
#define CTX_SEL_25G_RX                    0x0  /* To be configured based on actual values */
#define CTX_SEL_10G_CM                    0x0  /* To be configured based on actual values */
#define CTX_SEL_10G_TX                    0x0  /* To be configured based on actual values */
#define CTX_SEL_10G_RX                    0x0  /* To be configured based on actual values */

struct pcs_device xlgpcs0 = {
    .base_addr = (void *)XLGPCS0_BASE_ADDR,
    .current_speed = PCS_SPEED_10G,
    .current_duplex = PCS_FULL_DUPLEX,
    .link_up = 0,
    .an_enabled = 0,
    .eee_enabled = 0,
    .kr_training_enabled = 0,
    .max_speed = PCS_SPEED_25G,
    .pcs_mode = PCS_TYPE_10GBASE_KR,
};

struct pcs_device xlgpcs1 = {
    .base_addr = (void *)XLGPCS1_BASE_ADDR,
    .current_speed = PCS_SPEED_10G,
    .current_duplex = PCS_FULL_DUPLEX,
    .link_up = 0,
    .an_enabled = 0,
    .eee_enabled = 0,
    .kr_training_enabled = 0,
    .max_speed = PCS_SPEED_25G,
    .pcs_mode = PCS_TYPE_10GBASE_KR,
};

/*
 * CR Port write with protection
 */
int xlgpcs_cr_write(struct pcs_device *dev, u16 cr_addr, u16 cr_data)
{
    u32 cr_ctrl;
    u32 timeout = 10000; /* 10ms timeout */

    /* Step 1: Wait for CR_CTRL bit 0 to be 0 */
    do {
        cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
        if (!(cr_ctrl & BIT(0))) {
            break;
        }
	dev->osi_core->osd_ops.usleep(1);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout */
    }

    /* Step 2: Write CR address */
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_ADDR, cr_addr);

    /* Step 3: Write CR data */
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_DATA, cr_data);

    /* Step 4: Set CR_CTRL bit 1 */
    cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL, cr_ctrl | BIT(1));

    /* Step 5: Set CR_CTRL bit 0 */
    cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL, cr_ctrl | BIT(0));

    /* Step 6: Wait for CR_CTRL bit 0 to be 0 */
    timeout = 10000;
    do {
        cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
        if (!(cr_ctrl & BIT(0))) {
            break;
        }
	dev->osi_core->osd_ops.usleep(1);
        timeout--;
    } while (timeout > 0);

    return (timeout == 0) ? -1 : 0;
}

/*
 * CR Port read with protection
 */
int xlgpcs_cr_read(struct pcs_device *dev, u16 cr_addr, u16 *cr_data)
{
    u32 cr_ctrl;
    u32 timeout = 10000; /* 10ms timeout */

    /* Step 1: Wait for CR_CTRL bit 0 to be 0 */
    do {
        cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
        if (!(cr_ctrl & BIT(0))) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(1);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout */
    }

    /* Step 2: Write CR address */
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_ADDR, cr_addr);

    /* Step 3: Clear CR_CTRL bit 1 (read operation) */
    cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL, cr_ctrl & ~BIT(1));

    /* Step 4: Set CR_CTRL bit 0 */
    cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL, cr_ctrl | BIT(0));

    /* Step 5: Wait for CR_CTRL bit 0 to be 0 */
    timeout = 10000;
    do {
        cr_ctrl = pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_CTRL);
        if (!(cr_ctrl & BIT(0))) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(1);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout */
    }

    /* Step 6: Read CR data */
    *cr_data = (u16)pcs_read_reg(dev, VR_PMA_MP_25G_16G_CR_DATA);

    return 0;
}

/*
 * Initialize DWC_xlgpcs
 */
int xlgpcs_init(struct osi_core_priv_data *osi_core)
{
    u32 reg_val;
    u32 timeout = 1000000; /* 1 second timeout */
    struct pcs_device *dev = NULL;

    if(osi_core->instance_id == 0){
	    dev = &xlgpcs0;
    }
    else if(osi_core->instance_id == 1){
	    dev = &xlgpcs1;
    }
    else{
	    dev_err(osi_core->dev, "%s can not support xlgpcs\n", osi_core->if_name);
	    return -1;
    }

    osi_core->pcs_dev = dev;
    dev->dev = osi_core->dev;
    dev->base_addr = osi_core->xlgpcs_base;       /* Base address of XPCS registers */
    dev->current_speed = osi_core->speed;       /* Current link speed */
    dev->current_duplex = PCS_FULL_DUPLEX;      /* Current duplex mode */
    dev->link_up = 1;                      	/* Link status */
    dev->an_enabled = 1;                   	/* Auto-negotiation enabled */
    dev->eee_enabled = 1;                  	/* EEE enabled */
    dev->fec_enabled = 1;
    dev->kr_training_enabled = 1;          	/* KR training enabled (for Clause 72) */
    dev->flags = 0;                    		/* Device flags */
    dev->max_speed = PCS_SPEED_25G;            	/* Maximum supported speed */
    dev->pcs_mode = osi_core->phy_iface_mode;	/* PCS mode */
    dev->osi_core = osi_core;

    //TODO: will be deleted
    return 0;

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Step 1-3: Power on and wait for reset (handled by hardware) */

    /* Step 4.1: Poll for SRAM initialization done */
    do {
        reg_val = pcs_read_reg(dev, VR_PMA_MP_25G_16G_SRAM);
        if (reg_val & SRAM_INIT_DN) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for SRAM init */
    }

    /* Step 4.2: Skip CR writes for now (platform-specific firmware loading) */

    /* Step 4.3: Set EXT_LD_DN bit */
    reg_val = pcs_read_reg(dev, VR_PMA_MP_25G_16G_SRAM);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_SRAM, reg_val | SRAM_EXT_LD_DN);

    /* Step 5: Release PCS reset */
    timeout = 1000000;
    do {
        reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
        if (!(reg_val & RST)) {
            break;
        }
        pcs_write_reg(dev, SR_PCS_CTRL1, reg_val & ~RST);
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for reset release */
    }

    /* Step 6: Disable auto-negotiation */
    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    pcs_write_reg(dev, SR_AN_CTRL, reg_val & ~AN_EN);

    /* Step 7: Skip (Enterprise 56G PHY only) */

    /* Step 8: Enable debug controls */
    pcs_write_reg(dev, VR_PCS_DEBUG_CTRL,
                    SUPRESS_LOS_DET | RX_DT_ENCTL);

    /* Step 9: Skip auto-negotiation for now */

    /* Step 10: Wait for RLU bit */
    timeout = 1000000;
    do {
        reg_val = pcs_read_reg(dev, SR_PCS_STS1);
        if (reg_val & RLU) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for RLU */
    }

    return 0;
}

/*
 * Switch to 25G mode
 */
int xlgpcs_switch_to_25g(struct pcs_device *dev)
{
    u32 reg_val;
    u32 timeout = 1000000; /* 1 second timeout */

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Step 1: Set SS_5_2 to 4'b0101 */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    reg_val &= ~SS_5_2;
    reg_val |= (0x5 << 2);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val);

    /* Step 2: Set PCS type to 25GBASE-R */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL2);
    reg_val &= ~XLGPCS_TYPE_SEL;
    reg_val |= XLGPCS_TYPE_25GBASE_R;
    pcs_write_reg(dev, SR_PCS_CTRL2, reg_val);

    /* Step 3: Set PMA type to 25GBASE-KR */
    reg_val = pcs_read_reg(dev, SR_PMA_CTRL2);
    reg_val &= ~PMA_TYPE_MASK;
    reg_val |= PMA_TYPE_25G_R;
    pcs_write_reg(dev, SR_PMA_CTRL2, reg_val);

    /* Step 4: Disable 50G mode */
    reg_val = pcs_read_reg(dev, VR_PCS_DIG_CTRL3);
    reg_val &= ~EN_50G;
    pcs_write_reg(dev, VR_PCS_DIG_CTRL3, reg_val);

    /* Step 5: Set CNS_EN (optional) */
    reg_val = pcs_read_reg(dev, VR_PCS_DIG_CTRL3);
    reg_val |= CNS_EN;
    pcs_write_reg(dev, VR_PCS_DIG_CTRL3, reg_val);

    /* Step 6: Configure RS FEC (optional - skip for now) */

    /* Step 7: Configure BASE-R FEC (optional - skip for now) */

    /* Step 8: Configure PHY context for 25G */
    pcs_write_reg(dev, VR_PMA_MP_32G_CM_CNTX_SEL0, CTX_SEL_25G_CM);
    pcs_write_reg(dev, VR_PMA_MP_32G_TX_CNTX_SEL0, CTX_SEL_25G_TX);
    pcs_write_reg(dev, VR_PMA_MP_32G_RX_CNTX_SEL0, CTX_SEL_25G_RX);

    /* Step 9: Set LPM bit */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val | LPM);

    /* Step 10: Wait for power down state */
    do {
        reg_val = pcs_read_reg(dev, VR_PCS_DIG_STS);
        if ((reg_val & PSEQ_STATE) == POWER_DOWN_STATE) {
            break;
        }
	dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for power down */
    }

    /* Step 11: Release LPM and RST */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    reg_val &= ~LPM;
    reg_val &= ~RST;
    reg_val &= ~CS_EN;
    reg_val &= ~SS_5_2;
    reg_val |= (0x5 << 2);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val);

    /* Step 12: Wait for power good state */
    timeout = 1000000;
    do {
        reg_val = pcs_read_reg(dev, VR_PCS_DIG_STS);
        if ((reg_val & PSEQ_STATE) == TX_RX_STABLE_0) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for power good */
    }

    return 0;
}

/*
 * Switch to 10G mode
 */
int xlgpcs_switch_to_10g(struct pcs_device *dev)
{
    u32 reg_val;
    u32 timeout = 1000000; /* 1 second timeout */

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Step 1: Clear EN_50G and CNS_EN */
    reg_val = pcs_read_reg(dev, VR_PCS_DIG_CTRL3);
    reg_val &= ~(EN_50G | CNS_EN);
    pcs_write_reg(dev, VR_PCS_DIG_CTRL3, reg_val);

    /* Step 2: Set SS_5_2 to 4'b0000 */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    reg_val &= ~SS_5_2;
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val);

    /* Step 3: Set PCS type to 10GBASE-KR */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL2);
    reg_val &= ~XLGPCS_TYPE_SEL;
    reg_val |= XLGPCS_TYPE_10GBASE_KR;
    pcs_write_reg(dev, SR_PCS_CTRL2, reg_val);

    /* Step 4: Configure BASE-R FEC (optional - skip for now) */

    /* Step 5: Configure PHY context for 10G */
    pcs_write_reg(dev, VR_PMA_MP_32G_CM_CNTX_SEL0, CTX_SEL_10G_CM);
    pcs_write_reg(dev, VR_PMA_MP_32G_TX_CNTX_SEL0, CTX_SEL_10G_TX);
    pcs_write_reg(dev, VR_PMA_MP_32G_RX_CNTX_SEL0, CTX_SEL_10G_RX);

    /* Step 5 (continued): Set link number for bifurcation mode */
    pcs_write_reg(dev, VR_PMA_MP_32G_CNTX_LINK_NUM_CTRL, L0_LN);

    /* Step 5 (continued): Set LPM bit */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val | LPM);

    /* Step 6: Wait for power down state */
    do {
        reg_val = pcs_read_reg(dev, VR_PCS_DIG_STS);
        if ((reg_val & PSEQ_STATE) == POWER_DOWN_STATE) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for power down */
    }

    /* Step 7: Release LPM */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val & ~LPM);

    /* Step 8: Wait for power good state */
    timeout = 1000000;
    do {
        reg_val = pcs_read_reg(dev, VR_PCS_DIG_STS);
        if ((reg_val & PSEQ_STATE) == TX_RX_STABLE_0) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout waiting for power good */
    }

    return 0;
}

/*
 * Enable Clause 72 link training
 */
int xlgpcs_enable_clause72(struct pcs_device *dev)
{
    u32 reg_val;
    u32 timeout = 500000; /* 500ms timeout */

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Step 4: Enable Clause 72 startup protocol */
    reg_val = pcs_read_reg(dev, SR_PMA_KR_PMD_CTRL);
    reg_val |= TR_EN;
    pcs_write_reg(dev, SR_PMA_KR_PMD_CTRL, reg_val);

    /* Step 5: Restart Clause 72 startup protocol */
    reg_val = pcs_read_reg(dev, SR_PMA_KR_PMD_CTRL);
    reg_val |= RS_TR;
    pcs_write_reg(dev, SR_PMA_KR_PMD_CTRL, reg_val);

    /* Wait for training completion (check AN completion interrupt) */
    do {
        reg_val = pcs_read_reg(dev, VR_AN_INTR);
        if (reg_val & AN_CMPLT_INTR) {
            /* Training successful */
            break;
        }
	    dev->osi_core->osd_ops.usleep(100);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        /* Training failed - link fault detected */
        return -1;
    }

    /* Step 7: Clear AN completion interrupt */
    reg_val = pcs_read_reg(dev, VR_AN_DIG_CTRL1);
    reg_val |= AN_CMPLT_INTR;
    pcs_write_reg(dev, VR_AN_DIG_CTRL1, reg_val);

    return 0;
}

/*
 * Adjust local transmitter coefficients (manual mode)
 */
int xlgpcs_adjust_coefficients(struct pcs_device *dev, u8 coeff_idx, u8 direction)
{
    u32 reg_val;
    u32 timeout = 10000; /* 10ms timeout */
    u8 status;

    if (dev == OSI_NULL || coeff_idx > 2 || direction > 2) {
        return -1;
    }

    /* Step 1: Wait for NOT_UPDATED status */
    do {
        reg_val = pcs_read_reg(dev, SR_PMA_KR_LP_CESTS);
        status = reg_val & LP_CFF_STSM0_MASK;
        if (status == LP_CFF_NOT_UPDATED) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        return -1; /* Timeout */
    }

    /* Step 2: Write coefficient update direction */
    reg_val = pcs_read_reg(dev, SR_PMA_KR_LP_CEU);
    reg_val &= ~LP_CFF_UPDTM1_MASK;
    reg_val |= (direction & LP_CFF_UPDTM1_MASK);
    pcs_write_reg(dev, SR_PMA_KR_LP_CEU, reg_val);

    /* Step 3: Wait for update to complete */
    timeout = 10000;
    do {
        reg_val = pcs_read_reg(dev, SR_PMA_KR_LP_CESTS);
        status = reg_val & LP_CFF_STSM0_MASK;
        if (status == LP_CFF_UPDATED ||
            status == LP_CFF_MIN ||
            status == LP_CFF_MAX) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    return (timeout == 0) ? -1 : 0;
}

/*
 * Configure Clause 73 auto-negotiation
 */
int xlgpcs_configure_clause73(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Step 1: Disable auto-negotiation */
    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    pcs_write_reg(dev, SR_AN_CTRL, reg_val & ~AN_EN);

    /* Step 2: Disable RX data training */
    reg_val = pcs_read_reg(dev, VR_PMA_MP_25G_16G_12G_RX_GENCTRL0);
    reg_val &= ~(RX_DT_EN_0 | RX_DT_EN_3_1);
    pcs_write_reg(dev, VR_PMA_MP_25G_16G_12G_RX_GENCTRL0, reg_val);

    /* Step 3: Enable RX EQ MM */
    reg_val = pcs_read_reg(dev, VR_PMA_KRTR_RX_EQ_CTRL0);
    reg_val |= RX_EQ_MM;
    pcs_write_reg(dev, VR_PMA_KRTR_RX_EQ_CTRL0, reg_val);

    /* Step 4: Restart KR training */
    reg_val = pcs_read_reg(dev, SR_PMA_KR_PMD_CTRL);
    reg_val |= (TR_EN | RS_TR);
    pcs_write_reg(dev, SR_PMA_KR_PMD_CTRL, reg_val);

    /* Step 5: Enable auto-negotiation */
    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    reg_val |= AN_EN;
    pcs_write_reg(dev, SR_AN_CTRL, reg_val);

    return 0;
}

/*
 * Start Clause 73 auto-negotiation
 */
int xlgpcs_start_clause73(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Restart auto-negotiation */
    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    reg_val |= RSTRT_AN;
    pcs_write_reg(dev, SR_AN_CTRL, reg_val);

    return 0;
}

/*
 * Configure advertisement registers
 */
int xlgpcs_set_advertisement(struct pcs_device *dev, u32 adv1, u32 adv2, u32 adv3)
{
    if (dev == OSI_NULL) {
        return -1;
    }

    /* Write in reverse order as per specification */
    pcs_write_reg(dev, SR_AN_ADV3, adv3);
    pcs_write_reg(dev, SR_AN_ADV2, adv2);
    pcs_write_reg(dev, SR_AN_ADV1, adv1);

    return 0;
}

/*
 * Configure next page registers
 */
int xlgpcs_set_next_page(struct pcs_device *dev, u32 np1, u32 np2, u32 np3)
{
    if (dev == OSI_NULL) {
        return -1;
    }

    /* Write in reverse order as per specification */
    pcs_write_reg(dev, SR_AN_XNP_TX3, np3);
    pcs_write_reg(dev, SR_AN_XNP_TX2, np2);
    pcs_write_reg(dev, SR_AN_XNP_TX1, np1);

    return 0;
}

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
s32 xlgpcs_start(struct pcs_device *dev)
{
    //u32 an_status;
    //pcs_speed_t speed;
    //pcs_duplex_t duplex_mode;
    //u32 link_status = 0;
    s32 ret = 0;

    /* Enable auto-negotiation if needed */
    xlgpcs_an_enable(dev, 1);

    //wait for an complete
    /*
    xlgpcs_an_get_status(dev, &an_status);
    speed = ((an_status & USXG_AN_STS_SPEED) >> 10);
    duplex_mode = ((an_status & USXG_AN_STS_DUPLEX) >> 13);
    link_status = ((an_status & USXG_AN_STS_LINK) >> 14);

    ret = xlgpcs_set_speed(dev, speed);
    */
    return ret;
}


static u32 xlgpcs_check_eee_capability(struct pcs_device *dev)
{
    u32 ret = OSI_DISABLE;
    u32 reg_val = pcs_read_reg(dev, SR_PCS_EEE_ABL);
    switch(dev->pcs_mode)
    {
        case PCS_TYPE_25GBASE_R:
            if((reg_val & XXVGDSL) != 0 && (reg_val & XXVGFWK) != 0){
                ret = OSI_ENABLE;
            }
            else{
                ret = OSI_DISABLE;
            }
            break;
        case PCS_TYPE_10GBASE_KR:
            if(reg_val & XG_EEE)
            {
                ret = OSI_ENABLE;
            }
            else{
                ret = OSI_DISABLE;
            }
            break;
        default:
            ret = OSI_DISABLE;
    }
    return ret;
}

/*
 * Enable EEE (Energy Efficient Ethernet)
 */
static int xlgpcs_enable_eee(struct pcs_device *dev)
{
    u32 reg_val;

    /* Step 2: Configure EEE timers */
    pcs_write_reg(dev, VR_PCS_EEE_TXTIMER, dev->eee_txtimer);
    pcs_write_reg(dev, VR_PCS_EEE_RXTIMER, dev->eee_rxtimer);

    /* Step 3: Enable FEC fast sync if FEC is enabled */
    reg_val = pcs_read_reg(dev, VR_PCS_EEE_MCTRL);
    if (dev->fec_enabled) {
        reg_val |= EEE_SLR_BYP;
    } else {
        reg_val &= ~EEE_SLR_BYP;
    }
    pcs_write_reg(dev, VR_PCS_EEE_MCTRL, reg_val);

    /* Step 4: Enable VSMMD1 register */
    reg_val = pcs_read_reg(dev, VR_PCS_DIG_CTRL1);
    reg_val |= EN_VSMMD1;
    pcs_write_reg(dev, VR_PCS_DIG_CTRL1, reg_val);

    /* Step 5: Enable fast_sim */
    reg_val = pcs_read_reg(dev, SR_VSMMD_CTRL);
    reg_val |= FASTSIM;
    pcs_write_reg(dev, SR_VSMMD_CTRL, reg_val);

    /* Step 6: Configure AM interval for RS FEC (if enabled) */
    if (dev->fec_enabled) {
        pcs_write_reg(dev, VR_PCS_AM_CNT, AM_CNT);
        reg_val = pcs_read_reg(dev, SR_PMA_RS_FEC_CTRL);
        reg_val |= RSFEC_EN;
        pcs_write_reg(dev, SR_PMA_RS_FEC_CTRL, reg_val);
    }

    /* Step 8: Configure FAST_WAKE */
    reg_val = pcs_read_reg(dev, SR_PCS_EEE_ABL);
    if (dev->eee_fast_wake) {
        reg_val |= FWK_EN;
    } else {
        reg_val &= ~FWK_EN;
    }
    pcs_write_reg(dev, SR_PCS_EEE_ABL, reg_val);

    /* Step 9: Enable EEE for TX and RX paths */
    reg_val = pcs_read_reg(dev, VR_PCS_EEE_MCTRL);
    reg_val |= (LTX_EN | LRX_EN);
    pcs_write_reg(dev, VR_PCS_EEE_MCTRL, reg_val);

    return 0;
}

/*
 * Disable EEE
 */
static int xlgpcs_disable_eee(struct pcs_device *dev)
{
    u32 reg_val;

    /* Disable EEE for TX and RX paths */
    reg_val = pcs_read_reg(dev, VR_PCS_EEE_MCTRL);
    reg_val &= ~(LTX_EN | LRX_EN);
    pcs_write_reg(dev, VR_PCS_EEE_MCTRL, reg_val);

    return 0;
}

/**
 * @brief Enable/Disable EEE
 * 
 * Follows the sequence from section 3.10:
 * 1. Check EEE capability
 * 2. Configure timers based on clk_eee_i frequency
 * 3. Enable FEC fast sync if needed
 * 4. Enable TX EEE
 * 5. Enable RX EEE
 * 
 * @param xlgpcs Pointer to XLGPCS device structure
 * @param tx_enable Enable TX EEE
 * @param rx_enable Enable RX EEE
 * @param fec_enable Enable FEC fast sync
 * @return 0 on success, negative error code on failure
 */
int xlgpcs_eee(struct pcs_device *xlgpcs, u32 en_dis)
{
    u32 eee_abl;
    if (xlgpcs == OSI_NULL) {
        return -1;
    }
    
    /* Step 1: Check EEE capability */
    eee_abl = xlgpcs_check_eee_capability(xlgpcs);
    
    if (!eee_abl) {
        if(en_dis)
        {
            dev_err(xlgpcs->dev,"EEE not supported");
            return -1;
        }
        else{
            dev_err(xlgpcs->dev,"Disable EEE but not supported");
            return 0;
        }
    }
    
    if(en_dis)
    {
        xlgpcs_enable_eee(xlgpcs);
    }
    else{
        xlgpcs_disable_eee(xlgpcs);
    }    
    xlgpcs->eee_enabled = en_dis;
    
    return 0;
}

/*
 * Enable BASE-R FEC
 */
int xlgpcs_enable_base_r_fec(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PMA_KR_FEC_CTRL);
    reg_val |= FEC_EN;
    pcs_write_reg(dev, SR_PMA_KR_FEC_CTRL, reg_val);

    return 0;
}

/*
 * Disable BASE-R FEC
 */
int xlgpcs_disable_base_r_fec(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PMA_KR_FEC_CTRL);
    reg_val &= ~FEC_EN;
    pcs_write_reg(dev, SR_PMA_KR_FEC_CTRL, reg_val);

    return 0;
}

/*
 * Enable RS FEC
 */
int xlgpcs_enable_rs_fec(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Configure AM interval */
    pcs_write_reg(dev, VR_PCS_AM_CNT, AM_CNT);

    /* Configure CWM registers */
    pcs_write_reg(dev, VR_PMA_CWM00, 0x68C1);
    pcs_write_reg(dev, VR_PMA_CWM01, 0x3321);
    pcs_write_reg(dev, VR_PMA_CWM02, 0x973E);
    pcs_write_reg(dev, VR_PMA_CWM03, 0xCCDE);

    /* Enable RS FEC */
    reg_val = pcs_read_reg(dev, SR_PMA_RS_FEC_CTRL);
    reg_val |= RSFEC_EN;
    pcs_write_reg(dev, SR_PMA_RS_FEC_CTRL, reg_val);

    return 0;
}

/*
 * Disable RS FEC
 */
int xlgpcs_disable_rs_fec(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PMA_RS_FEC_CTRL);
    reg_val &= ~RSFEC_EN;
    pcs_write_reg(dev, SR_PMA_RS_FEC_CTRL, reg_val);

    return 0;
}

/*
 * Get link status
 */
int xlgpcs_get_link_status(struct pcs_device *dev, int *link_up)
{
    u32 reg_val;

    if (dev == OSI_NULL || link_up == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PCS_STS1);
    *link_up = (reg_val & RLU) ? 1 : 0;

    return 0;
}

/*
 * Get power sequence state
 */
int xlgpcs_get_pseq_state(struct pcs_device *dev, u8 *state)
{
    u32 reg_val;

    if (dev == OSI_NULL || state == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, VR_PCS_DIG_STS);
    *state = (reg_val & PSEQ_STATE) >> 2;

    return 0;
}

/*
 * Soft reset
 */
int xlgpcs_reset(struct pcs_device *dev)
{
    u32 reg_val;
    u32 timeout = 1000000; /* 1 second timeout */

    if (dev == OSI_NULL) {
        return -1;
    }

    /* Assert reset */
    reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
    pcs_write_reg(dev, SR_PCS_CTRL1, reg_val | RST);

    /* Wait for reset to complete */
    do {
        reg_val = pcs_read_reg(dev, SR_PCS_CTRL1);
        if (!(reg_val & RST)) {
            break;
        }
	    dev->osi_core->osd_ops.usleep(10);
        timeout--;
    } while (timeout > 0);

    return (timeout == 0) ? -1 : 0;
}

/*
 * Auto-negotiation reset
 */
int xlgpcs_an_reset(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    reg_val |= AN_RST;
    pcs_write_reg(dev, SR_AN_CTRL, reg_val);

    return 0;
}

/*
 * Auto-negotiation enable/disable
 */
int xlgpcs_an_enable(struct pcs_device *dev, int enable)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_AN_CTRL);
    if (enable) {
        reg_val |= AN_EN;
    } else {
        reg_val &= ~AN_EN;
    }
    pcs_write_reg(dev, SR_AN_CTRL, reg_val);

    return 0;
}

/*
 * Enable loopback mode
 */
int xlgpcs_enable_loopback(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PMA_CTRL1);
    reg_val |= LB;
    pcs_write_reg(dev, SR_PMA_CTRL1, reg_val);

    return 0;
}

/*
 * Disable loopback mode
 */
int xlgpcs_disable_loopback(struct pcs_device *dev)
{
    u32 reg_val;

    if (dev == OSI_NULL) {
        return -1;
    }

    reg_val = pcs_read_reg(dev, SR_PMA_CTRL1);
    reg_val &= ~LB;
    pcs_write_reg(dev, SR_PMA_CTRL1, reg_val);

    return 0;
}


void xlgpcs_ce_intr_handle(struct pcs_device * xlgpcs)
{
    dev_err(xlgpcs->dev, "%s handle xlgpcs sfty ce interrupt\n", xlgpcs->osi_core->if_name);
}

void xlgpcs_ue_intr_handle(struct pcs_device *xlgpcs)
{
    dev_err(xlgpcs->dev, "%s handle xlgpcs sfty ue interrupt\n", xlgpcs->osi_core->if_name);
}

void xlgpcs_sbd_intr_handle(struct pcs_device *xlgpcs)
{
    dev_err(xlgpcs->dev, "%s handle xlgpcs sbd interrupt\n", xlgpcs->osi_core->if_name);
}
