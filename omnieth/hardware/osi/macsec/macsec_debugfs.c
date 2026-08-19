// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024-2026, Omni. All rights reserved
//
// DWC_macsec 1.05a debugfs interface for FPGA validation
//
// Provides raw register read/write, internal table dump, and
// hardware status overview via:
//
//   /sys/kernel/debug/omni_macsec/<ethX>/
//   ├── status       (R)   Hardware status overview
//   ├── regdump      (R)   Full MACsec register dump
//   ├── reg_read     (R/W) Raw register read
//   ├── reg_write    (W)   Raw register write
//   └── table_dump   (R/W) Internal table dump

#ifdef MACSEC_SUPPORT

#include "../../../ether_linux.h"
#include "../../../macsec.h"
#include <linux/debugfs.h>

/* ============================================================
 * Register read/write definitions from dwc_macsec_reg.h
 * (re-declared locally so OSD doesn't depend on OSI internals)
 * ============================================================ */

/* Address space boundaries */
#define MACSEC_REG_MAX_OFFSET	0x600U
#define AES_REG_MAX_OFFSET	0x2C0U
#define CSR_REG_MAX_OFFSET	0x24U

/* Key register offsets for status display */
#define MACSEC_VER_NUM		0x00U
#define MACSEC_VER_TYPE		0x04U
#define MACSEC_IP_CONTROL	0x08U
#define MACSEC_TX_ACTIVE_AN	0x60U
#define MACSEC_IRQ_GLBL_EN	0x178U
#define MACSEC_IRQ_GLBL_STAT	0x17CU

/* AES register offsets */
#define AES_CONFIG		0x10U
#define AES_CTRL		0x14U
#define AES_STAT		0x18U
#define AES_IRQ_EN		0x08U
#define AES_IRQ_STAT		0x0CU
#define AES_FIPS_STAT		0x58U

/* CSR register offsets */
#define CSR_CTRL_5		0x14U
#define CSR_CTRL_7		0x1CU

/* Table-access register offsets */
#define TX_SAI_PG		0x48U
#define TX_SAI_PRG		0x4CU
#define TX_SAI_DATA_FETCH	0x54U
#define TX_SAI_OTHER_FIELDS	0x50U
#define TX_SRC_FILT_MSB		0x38U
#define TX_SRC_FILT_LSB		0x3CU
#define TX_DST_FILT_MSB		0x40U
#define TX_DST_FILT_LSB		0x44U

#define RX_SAI_PG		0x28U
#define RX_SAI_PRG		0x2CU
#define RX_SAI_DATA_FETCH	0x34U
#define RX_SAI_OTHER_FIELDS	0x30U
#define RX_LT_SCI_HIGH		0x20U
#define RX_LT_SCI_LOW		0x1CU

#define TX_SAD_PG		0xC8U
#define TX_SAD_PRG		0xCCU
#define TX_SAD_DATA_FETCH	0xD0U
#define TX_SAD_0_LOW		0xD4U
#define TX_SAD_0_HIGH		0xD8U
#define TX_SAD_1_LOW		0xDCU
#define TX_SAD_1_HIGH		0xE0U

#define RX_SAD_PG		0xF4U
#define RX_SAD_PRG		0xF8U
#define RX_SAD_DATA_FETCH	0xFCU
#define RX_SAD_0_LOW		0x100U
#define RX_SAD_0_HIGH		0x104U
#define RX_SAD_1_LOW		0x108U
#define RX_SAD_1_HIGH		0x10CU

#define RX_ARW_PG		0xB8U
#define RX_ARW_PRG		0xBCU
#define RX_ARW_DATA_FETCH	0xC0U
#define RX_ARW_TABLE_0		0xC4U

#define RX_SC_CORR_PG		0x78U
#define RX_SC_CORR_PRG		0x7CU
#define RX_SC_CORR_DATA_FETCH	0x80U
#define RX_SC_CORR_SC_VALUE	0x84U

/* PRG bit fields */
#define PRG_TYPE_READ		0x0U
#define PRG_TYPE_WRITE		0x1U
#define PRG_ENTRY_SHIFT		1

/* PG / DATA_FETCH bit fields */
#define PG_BUSY			BIT(0)
#define DATA_FETCH_READY	BIT(0)

/* Polling limits */
#define POLL_RETRY		1000U
#define POLL_DELAY_US		1U

/* Debugfs directory root */
static struct dentry *omni_macsec_debugfs_root;

/* Per-device debugfs context */
struct macsec_debugfs_ctx {
	struct ether_priv_data *pdata;
	/* reg_read result storage */
	u32 last_read_val;
	bool read_valid;
	/* table_dump config */
	char table_cmd[64];
};

/* ============================================================
 * Polling helpers (OSD-level, direct ioremap access)
 * ============================================================ */

static int dbgfs_poll_busy(void __iomem *base, u32 pg_off)
{
	u32 i;

	for (i = 0; i < POLL_RETRY; i++) {
		if ((readl(base + pg_off) & PG_BUSY) == 0)
			return 0;
		udelay(POLL_DELAY_US);
	}
	return -ETIMEDOUT;
}

static int dbgfs_poll_ready(void __iomem *base, u32 df_off)
{
	u32 i;

	for (i = 0; i < POLL_RETRY; i++) {
		if ((readl(base + df_off) & DATA_FETCH_READY) != 0)
			return 0;
		udelay(POLL_DELAY_US);
	}
	return -ETIMEDOUT;
}

/* ============================================================
 * Address space resolution
 * ============================================================ */

enum macsec_addr_space {
	ADDR_MACSEC = 0,
	ADDR_AES,
	ADDR_CSR,
};

static void __iomem *resolve_base(struct osi_core_priv_data *osi_core,
				  enum macsec_addr_space space,
				  u32 offset, u32 *max_off)
{
	switch (space) {
	case ADDR_MACSEC:
		*max_off = MACSEC_REG_MAX_OFFSET;
		return osi_core->macsec_base;
	case ADDR_AES:
		*max_off = AES_REG_MAX_OFFSET;
		return osi_core->tz_base;
	case ADDR_CSR:
		*max_off = CSR_REG_MAX_OFFSET;
		return osi_core->csr_base;
	default:
		return NULL;
	}
}

static int parse_space(const char *name, enum macsec_addr_space *space)
{
	if (strncmp(name, "macsec", 6) == 0)
		*space = ADDR_MACSEC;
	else if (strncmp(name, "aes", 3) == 0)
		*space = ADDR_AES;
	else if (strncmp(name, "csr", 3) == 0)
		*space = ADDR_CSR;
	else
		return -EINVAL;
	return 0;
}

/* ============================================================
 * debugfs: status (read-only)
 * ============================================================ */

static int status_show(struct seq_file *s, void *unused)
{
	struct macsec_debugfs_ctx *ctx = s->private;
	struct osi_core_priv_data *osi_core = ctx->pdata->osi_core;
	void __iomem *mbase = osi_core->macsec_base;
	void __iomem *abase = osi_core->tz_base;
	void __iomem *cbase = osi_core->csr_base;
	u32 val;

	if (!mbase) {
		seq_puts(s, "MACsec not mapped\n");
		return 0;
	}

	seq_puts(s, "=== DWC MACsec 1.05a Status ===\n");

	val = readl(mbase + MACSEC_VER_NUM);
	seq_printf(s, "VER_NUM:      0x%08x\n", val);
	val = readl(mbase + MACSEC_VER_TYPE);
	seq_printf(s, "VER_TYPE:     0x%08x\n", val);

	if (cbase) {
		val = readl(cbase + CSR_CTRL_5);
		seq_printf(s, "CSR ctrl_5:   0x%08x [EN=%u RST_N=%u SRAM_ZERO=%u]\n",
			   val, val & 1, (val >> 1) & 1, (val >> 2) & 1);
		val = readl(cbase + CSR_CTRL_7);
		seq_printf(s, "CSR ctrl_7:   0x%08x\n", val);
	}

	val = readl(mbase + MACSEC_IP_CONTROL);
	seq_printf(s, "IP_CONTROL:   0x%08x [TX_BYP=%u RX_BYP=%u XPNSEL=%u]\n",
		   val, (val >> 3) & 1, (val >> 4) & 1, val & 1);

	val = readl(mbase + MACSEC_TX_ACTIVE_AN);
	seq_printf(s, "TX_ACTIVE_AN: 0x%08x\n", val);

	val = readl(mbase + MACSEC_IRQ_GLBL_EN);
	seq_printf(s, "IRQ_GLBL_EN:  0x%08x\n", val);
	val = readl(mbase + MACSEC_IRQ_GLBL_STAT);
	seq_printf(s, "IRQ_GLBL_STAT:0x%08x\n", val);

	if (abase) {
		val = readl(abase + AES_CONFIG);
		seq_printf(s, "AES CONFIG:   0x%08x\n", val);
		val = readl(abase + AES_STAT);
		seq_printf(s, "AES STAT:     0x%08x [BUSY=%u]\n",
			   val, val & 1);
		val = readl(abase + AES_IRQ_EN);
		seq_printf(s, "AES IRQ_EN:   0x%08x\n", val);
		val = readl(abase + AES_IRQ_STAT);
		seq_printf(s, "AES IRQ_STAT: 0x%08x\n", val);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(status);

/* ============================================================
 * debugfs: regdump (read-only)
 * ============================================================ */

static int regdump_show(struct seq_file *s, void *unused)
{
	struct macsec_debugfs_ctx *ctx = s->private;
	struct osi_core_priv_data *osi_core = ctx->pdata->osi_core;
	void __iomem *base;
	u32 off;

	/* MACsec CFG registers */
	base = osi_core->macsec_base;
	if (base) {
		seq_puts(s, "=== MACsec CFG Registers (macsec_base) ===\n");
		for (off = 0; off < MACSEC_REG_MAX_OFFSET; off += 4)
			seq_printf(s, "  [0x%03x] = 0x%08x\n",
				   off, readl(base + off));
	}

	/* AES registers */
	base = osi_core->tz_base;
	if (base) {
		seq_puts(s, "\n=== AES Registers (tz_base) ===\n");
		for (off = 0; off < AES_REG_MAX_OFFSET; off += 4)
			seq_printf(s, "  [0x%03x] = 0x%08x\n",
				   off, readl(base + off));
	}

	/* CSR registers */
	base = osi_core->csr_base;
	if (base) {
		seq_puts(s, "\n=== ETH CSR Registers (csr_base) ===\n");
		for (off = 0; off < CSR_REG_MAX_OFFSET; off += 4)
			seq_printf(s, "  [0x%03x] = 0x%08x\n",
				   off, readl(base + off));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(regdump);

/* ============================================================
 * debugfs: reg_read (write command, then read result)
 *
 * Usage:
 *   echo "macsec 0x08" > reg_read    # read IP_CONTROL
 *   cat reg_read                     # shows result
 * ============================================================ */

static ssize_t reg_read_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct macsec_debugfs_ctx *ctx = s->private;
	struct osi_core_priv_data *osi_core = ctx->pdata->osi_core;
	char buf[64];
	char space_name[16];
	u32 offset, max_off;
	enum macsec_addr_space space;
	void __iomem *base;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	/* Parse: "<space> <offset>" */
	ret = sscanf(buf, "%15s 0x%x", space_name, &offset);
	if (ret != 2) {
		ret = sscanf(buf, "%15s %u", space_name, &offset);
		if (ret != 2)
			return -EINVAL;
	}

	if (parse_space(space_name, &space) < 0)
		return -EINVAL;

	base = resolve_base(osi_core, space, offset, &max_off);
	if (!base)
		return -ENODEV;
	if ((offset & 3) != 0 || offset >= max_off)
		return -EINVAL;

	ctx->last_read_val = readl(base + offset);
	ctx->read_valid = true;

	return count;
}

static int reg_read_show(struct seq_file *s, void *unused)
{
	struct macsec_debugfs_ctx *ctx = s->private;

	if (ctx->read_valid)
		seq_printf(s, "0x%08x\n", ctx->last_read_val);
	else
		seq_puts(s, "No read pending. echo \"<space> <offset>\" > reg_read\n");

	return 0;
}

static int reg_read_open(struct inode *inode, struct file *file)
{
	return single_open(file, reg_read_show, inode->i_private);
}

static const struct file_operations reg_read_fops = {
	.owner   = THIS_MODULE,
	.open    = reg_read_open,
	.read    = seq_read,
	.write   = reg_read_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ============================================================
 * debugfs: reg_write (write-only)
 *
 * Usage:
 *   echo "macsec 0x08 0x00000018" > reg_write
 * ============================================================ */

static ssize_t reg_write_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct macsec_debugfs_ctx *ctx = file->private_data;
	struct osi_core_priv_data *osi_core = ctx->pdata->osi_core;
	char buf[64];
	char space_name[16];
	u32 offset, value, max_off;
	enum macsec_addr_space space;
	void __iomem *base;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	/* Parse: "<space> <offset> <value>" */
	ret = sscanf(buf, "%15s 0x%x 0x%x", space_name, &offset, &value);
	if (ret != 3) {
		ret = sscanf(buf, "%15s %u %u", space_name, &offset, &value);
		if (ret != 3)
			return -EINVAL;
	}

	if (parse_space(space_name, &space) < 0)
		return -EINVAL;

	base = resolve_base(osi_core, space, offset, &max_off);
	if (!base)
		return -ENODEV;
	if ((offset & 3) != 0 || offset >= max_off)
		return -EINVAL;

	writel(value, base + offset);

	return count;
}

static int reg_write_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations reg_write_fops = {
	.owner   = THIS_MODULE,
	.open    = reg_write_open,
	.write   = reg_write_write,
	.llseek  = noop_llseek,
};

/* ============================================================
 * debugfs: table_dump (write command, then read result)
 *
 * Usage:
 *   echo "tx_sai 0 15" > table_dump   # dump TX SAI entries 0-15
 *   echo "rx_sad 0 63" > table_dump   # dump RX SAD entries 0-63
 *   echo "rx_arw 0 63" > table_dump   # dump RX ARW entries 0-63
 *   echo "rx_sc_corr 0 63" > table_dump
 *   cat table_dump
 * ============================================================ */

/* Table types */
enum macsec_table_type {
	TBL_TX_SAI = 0,
	TBL_RX_SAI,
	TBL_TX_SAD,
	TBL_RX_SAD,
	TBL_RX_ARW,
	TBL_RX_SC_CORR,
};

struct table_desc {
	const char *name;
	enum macsec_table_type type;
	u32 max_entries;
	u32 pg_off;
	u32 prg_off;
	u32 df_off;
};

static const struct table_desc table_list[] = {
	{ "tx_sai",     TBL_TX_SAI,     32,  TX_SAI_PG,      TX_SAI_PRG,      TX_SAI_DATA_FETCH },
	{ "rx_sai",     TBL_RX_SAI,     64,  RX_SAI_PG,      RX_SAI_PRG,      RX_SAI_DATA_FETCH },
	{ "tx_sad",     TBL_TX_SAD,     32,  TX_SAD_PG,      TX_SAD_PRG,      TX_SAD_DATA_FETCH },
	{ "rx_sad",     TBL_RX_SAD,     64,  RX_SAD_PG,      RX_SAD_PRG,      RX_SAD_DATA_FETCH },
	{ "rx_arw",     TBL_RX_ARW,     64,  RX_ARW_PG,      RX_ARW_PRG,      RX_ARW_DATA_FETCH },
	{ "rx_sc_corr", TBL_RX_SC_CORR, 64,  RX_SC_CORR_PG,  RX_SC_CORR_PRG,  RX_SC_CORR_DATA_FETCH },
};

static const struct table_desc *find_table(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(table_list); i++) {
		if (strcmp(table_list[i].name, name) == 0)
			return &table_list[i];
	}
	return NULL;
}

static void dump_tx_sai_entry(struct seq_file *s, void __iomem *base, u32 idx)
{
	u32 src_msb, src_lsb, dst_msb, dst_lsb, other;

	src_msb = readl(base + TX_SRC_FILT_MSB);
	src_lsb = readl(base + TX_SRC_FILT_LSB);
	dst_msb = readl(base + TX_DST_FILT_MSB);
	dst_lsb = readl(base + TX_DST_FILT_LSB);
	other   = readl(base + TX_SAI_OTHER_FIELDS);

	seq_printf(s, "  [%2u] SA=%02x:%02x:%02x:%02x:%02x:%02x "
		   "DA=%02x:%02x:%02x:%02x:%02x:%02x "
		   "outcome=%u sc_idx=%u\n",
		   idx,
		   (src_msb >> 8) & 0xFF, src_msb & 0xFF,
		   (src_lsb >> 24) & 0xFF, (src_lsb >> 16) & 0xFF,
		   (src_lsb >> 8) & 0xFF, src_lsb & 0xFF,
		   (dst_msb >> 8) & 0xFF, dst_msb & 0xFF,
		   (dst_lsb >> 24) & 0xFF, (dst_lsb >> 16) & 0xFF,
		   (dst_lsb >> 8) & 0xFF, dst_lsb & 0xFF,
		   other & 0x3, (other >> 2) & 0x1F);
}

static void dump_rx_sai_entry(struct seq_file *s, void __iomem *base, u32 idx)
{
	u32 sci_hi, sci_lo, other;

	sci_hi = readl(base + RX_LT_SCI_HIGH);
	sci_lo = readl(base + RX_LT_SCI_LOW);
	other  = readl(base + RX_SAI_OTHER_FIELDS);

	seq_printf(s, "  [%2u] SCI=%08x_%08x AN=%u\n",
		   idx, sci_hi, sci_lo, other & 0x3);
}

static void dump_tx_sad_entry(struct seq_file *s, void __iomem *base, u32 idx)
{
	u32 sad0l, sad0h, sad1l, sad1h;

	sad0l = readl(base + TX_SAD_0_LOW);
	sad0h = readl(base + TX_SAD_0_HIGH);
	sad1l = readl(base + TX_SAD_1_LOW);
	sad1h = readl(base + TX_SAD_1_HIGH);

	seq_printf(s, "  [%2u] PN=0x%08x ACTIVE=%u TCI=0x%02x "
		   "SOFT_TTL=0x%04x MTU=%u\n",
		   idx, sad0l,
		   (sad0h >> 31) & 1,
		   (sad0h >> 2) & 0x1F,
		   (sad1l >> 16) & 0xFFFF,
		   sad1h & 0x3FFF);
}

static void dump_rx_sad_entry(struct seq_file *s, void __iomem *base, u32 idx)
{
	u32 sad0l, sad0h;

	sad0l = readl(base + RX_SAD_0_LOW);
	sad0h = readl(base + RX_SAD_0_HIGH);

	seq_printf(s, "  [%2u] LowestPN=0x%08x ACTIVE=%u REPLAY=%u "
		   "VALIDATE=%u\n",
		   idx, sad0l,
		   (sad0h >> 31) & 1,
		   (sad0h >> 30) & 1,
		   (sad0h >> 27) & 0x3);
}

static void dump_rx_arw_entry(struct seq_file *s, void __iomem *base, u32 idx)
{
	u32 arw = readl(base + RX_ARW_TABLE_0);

	seq_printf(s, "  [%2u] ARW_exp=%u (window=2^%u=%u)\n",
		   idx, arw, arw, arw > 0 ? (1U << arw) : 0);
}

static void dump_rx_sc_corr_entry(struct seq_file *s, void __iomem *base,
				  u32 idx)
{
	u32 sc_val = readl(base + RX_SC_CORR_SC_VALUE);

	seq_printf(s, "  [%2u] SC_index=%u\n", idx, sc_val);
}

static int table_dump_one(struct seq_file *s, void __iomem *base,
			  const struct table_desc *td, u32 idx)
{
	u32 prg_val;
	int ret;

	/* Wait not busy */
	ret = dbgfs_poll_busy(base, td->pg_off);
	if (ret < 0) {
		seq_printf(s, "  [%2u] TIMEOUT (busy)\n", idx);
		return ret;
	}

	/* Trigger read */
	prg_val = (idx << PRG_ENTRY_SHIFT) | PRG_TYPE_READ;
	writel(prg_val, base + td->prg_off);

	/* Wait data ready */
	ret = dbgfs_poll_ready(base, td->df_off);
	if (ret < 0) {
		seq_printf(s, "  [%2u] TIMEOUT (fetch)\n", idx);
		return ret;
	}

	/* Decode and print based on table type */
	switch (td->type) {
	case TBL_TX_SAI:
		dump_tx_sai_entry(s, base, idx);
		break;
	case TBL_RX_SAI:
		dump_rx_sai_entry(s, base, idx);
		break;
	case TBL_TX_SAD:
		dump_tx_sad_entry(s, base, idx);
		break;
	case TBL_RX_SAD:
		dump_rx_sad_entry(s, base, idx);
		break;
	case TBL_RX_ARW:
		dump_rx_arw_entry(s, base, idx);
		break;
	case TBL_RX_SC_CORR:
		dump_rx_sc_corr_entry(s, base, idx);
		break;
	}
	return 0;
}

static ssize_t table_dump_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct macsec_debugfs_ctx *ctx = s->private;

	if (count >= sizeof(ctx->table_cmd))
		return -EINVAL;
	if (copy_from_user(ctx->table_cmd, ubuf, count))
		return -EFAULT;
	ctx->table_cmd[count] = '\0';
	/* Strip trailing newline */
	if (count > 0 && ctx->table_cmd[count - 1] == '\n')
		ctx->table_cmd[count - 1] = '\0';

	return count;
}

static int table_dump_show(struct seq_file *s, void *unused)
{
	struct macsec_debugfs_ctx *ctx = s->private;
	struct osi_core_priv_data *osi_core = ctx->pdata->osi_core;
	void __iomem *base = osi_core->macsec_base;
	char name[32];
	u32 start = 0, end = 0;
	const struct table_desc *td;
	u32 idx;
	int ret;

	if (!base) {
		seq_puts(s, "MACsec not mapped\n");
		return 0;
	}

	if (ctx->table_cmd[0] == '\0') {
		seq_puts(s, "Usage: echo \"<table> <start> <end>\" > table_dump\n");
		seq_puts(s, "Tables: tx_sai rx_sai tx_sad rx_sad rx_arw rx_sc_corr\n");
		return 0;
	}

	ret = sscanf(ctx->table_cmd, "%31s %u %u", name, &start, &end);
	if (ret < 1)
		return -EINVAL;

	td = find_table(name);
	if (!td) {
		seq_printf(s, "Unknown table: %s\n", name);
		return 0;
	}

	/* If only table name given, dump all */
	if (ret == 1) {
		start = 0;
		end = td->max_entries - 1;
	} else if (ret == 2) {
		end = start;
	}

	if (end >= td->max_entries)
		end = td->max_entries - 1;
	if (start > end) {
		seq_puts(s, "Invalid range\n");
		return 0;
	}

	seq_printf(s, "=== %s [%u..%u] ===\n", td->name, start, end);
	for (idx = start; idx <= end; idx++)
		table_dump_one(s, base, td, idx);

	return 0;
}

static int table_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, table_dump_show, inode->i_private);
}

static const struct file_operations table_dump_fops = {
	.owner   = THIS_MODULE,
	.open    = table_dump_open,
	.read    = seq_read,
	.write   = table_dump_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ============================================================
 * Init / Cleanup
 * ============================================================ */

/**
 * @brief Create debugfs entries for a MACsec device
 *
 * Called from macsec_probe() after all resources are mapped.
 * Creates the root directory on first call.
 *
 * @param[in] pdata: Ethernet private data
 * @return 0 on success, negative on failure (non-fatal)
 */
int macsec_debugfs_init(struct ether_priv_data *pdata)
{
	struct device *dev = pdata->dev;
	struct dentry *dir;
	struct macsec_debugfs_ctx *ctx;
	const char *ifname;

	/* Lazy-create root directory on first device */
	if (!omni_macsec_debugfs_root) {
		omni_macsec_debugfs_root =
			debugfs_create_dir("omni_macsec", NULL);
		if (IS_ERR(omni_macsec_debugfs_root)) {
			omni_macsec_debugfs_root = NULL;
			return -ENODEV;
		}
	}

	ifname = dev_name(dev);
	if (pdata->ndev)
		ifname = netdev_name(pdata->ndev);

	dir = debugfs_create_dir(ifname, omni_macsec_debugfs_root);
	if (IS_ERR(dir)) {
		dev_warn(dev, "debugfs: failed to create dir %s\n", ifname);
		return PTR_ERR(dir);
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		debugfs_remove_recursive(dir);
		return -ENOMEM;
	}
	ctx->pdata = pdata;

	debugfs_create_file("status",     0444, dir, ctx, &status_fops);
	debugfs_create_file("regdump",    0444, dir, ctx, &regdump_fops);
	debugfs_create_file("reg_read",   0644, dir, ctx, &reg_read_fops);
	debugfs_create_file("reg_write",  0200, dir, ctx, &reg_write_fops);
	debugfs_create_file("table_dump", 0644, dir, ctx, &table_dump_fops);

	/* Store dir for cleanup */
	pdata->macsec_debugfs_dir = dir;

	dev_info(dev, "MACsec debugfs created at /sys/kernel/debug/omni_macsec/%s\n",
		 ifname);
	return 0;
}

/**
 * @brief Remove debugfs entries for a MACsec device
 *
 * Called from macsec_remove().
 */
void macsec_debugfs_remove(struct ether_priv_data *pdata)
{
	if (pdata->macsec_debugfs_dir) {
		debugfs_remove_recursive(pdata->macsec_debugfs_dir);
		pdata->macsec_debugfs_dir = NULL;
	}
}

/**
 * @brief Module-level debugfs root init
 *
 * Called once at module load.
 */
void macsec_debugfs_root_init(void)
{
	omni_macsec_debugfs_root = debugfs_create_dir("omni_macsec", NULL);
	if (IS_ERR(omni_macsec_debugfs_root))
		omni_macsec_debugfs_root = NULL;
}

/**
 * @brief Module-level debugfs root cleanup
 *
 * Called once at module unload.
 */
void macsec_debugfs_root_exit(void)
{
	debugfs_remove_recursive(omni_macsec_debugfs_root);
	omni_macsec_debugfs_root = NULL;
}

#endif /* MACSEC_SUPPORT */
