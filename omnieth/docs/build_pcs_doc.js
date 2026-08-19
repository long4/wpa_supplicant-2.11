// XPCS/XLGPCS PCS Driver Analysis Document Generator
const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, PageNumber, HeadingLevel,
  BorderStyle, WidthType, ShadingType, PageBreak, LevelFormat,
  TableOfContents
} = require("docx");

const FONT = "Arial";
const border = { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" };
const borders = { top: border, bottom: border, left: border, right: border };
const cellMargins = { top: 60, bottom: 60, left: 100, right: 100 };
const headerBg = { fill: "1F4E79", type: ShadingType.CLEAR };
const subHeaderBg = { fill: "D6E4F0", type: ShadingType.CLEAR };

// Helper: a table cell with text
function tCell(text, opts = {}) {
  const { bold, shading, width: w, alignment } = opts;
  return new TableCell({
    borders,
    width: w ? { size: w, type: WidthType.DXA } : undefined,
    margins: cellMargins,
    shading,
    children: [new Paragraph({
      alignment: alignment || AlignmentType.LEFT,
      children: [new TextRun({ text, bold: !!bold, font: FONT, size: 20 })]
    })]
  });
}

function tCellRuns(runs, opts = {}) {
  const { shading, width: w } = opts;
  return new TableCell({
    borders,
    width: w ? { size: w, type: WidthType.DXA } : undefined,
    margins: cellMargins,
    shading,
    children: [new Paragraph({ children: runs })]
  });
}

function headerCell(text, w) {
  return tCell(text, { bold: true, shading: headerBg, width: w, alignment: AlignmentType.CENTER });
}

function subHeaderCell(text, w) {
  return tCell(text, { bold: true, shading: subHeaderBg, width: w });
}

function bodyRow(cells) {
  return new TableRow({ children: cells });
}

// Heading helpers
function h1(text) {
  return new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun({ text, font: FONT })] });
}
function h2(text) {
  return new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun({ text, font: FONT })] });
}
function h3(text) {
  return new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun({ text, font: FONT })] });
}
function para(text, opts = {}) {
  return new Paragraph({
    spacing: { after: 120 },
    children: [new TextRun({ text, font: FONT, size: 20, ...opts })]
  });
}
function bold(text) { return new TextRun({ text, font: FONT, size: 20, bold: true }); }
function txt(text) { return new TextRun({ text, font: FONT, size: 20 }); }
function code(text) { return new TextRun({ text, font: "Courier New", size: 18 }); }

// Bullet paragraph
const bulletConfig = {
  reference: "bullets",
  levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 720, hanging: 360 } } } }]
};

function bullet(text) {
  return new Paragraph({
    numbering: { reference: "bullets", level: 0 },
    children: [new TextRun({ text, font: FONT, size: 20 })]
  });
}

// ============ DOCUMENT CONTENT ============

const children = [];

// === COVER / TITLE ===
children.push(new Paragraph({ spacing: { before: 3000 }, children: [] }));
children.push(new Paragraph({
  alignment: AlignmentType.CENTER,
  spacing: { after: 200 },
  children: [new TextRun({ text: "XPCS/XLGPCS 驱动代码分析", font: FONT, size: 56, bold: true, color: "1F4E79" })]
}));
children.push(new Paragraph({
  alignment: AlignmentType.CENTER, spacing: { after: 200 },
  children: [new TextRun({ text: "PCS Driver Implementation Analysis", font: FONT, size: 28, color: "666666" })]
}));
children.push(new Paragraph({ spacing: { before: 1200 }, children: [] }));
children.push(new Paragraph({
  alignment: AlignmentType.CENTER,
  children: [new TextRun({ text: "Author: Nova Zhang    Review: David Ma", font: FONT, size: 22, color: "333333" })]
}));
children.push(new Paragraph({
  alignment: AlignmentType.CENTER, spacing: { after: 200 },
  children: [new TextRun({ text: "基于 Synopsys DWC_xpcs v3.50a + DWC_xlgpcs v2.20a Databook 与实际驱动代码", font: FONT, size: 20, color: "888888" })]
}));
children.push(new Paragraph({
  alignment: AlignmentType.CENTER,
  children: [new TextRun({ text: "2026年6月", font: FONT, size: 20, color: "888888" })]
}));

children.push(new PageBreak());

// Table of Contents
children.push(new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun({ text: "目录", font: FONT })] }));
children.push(new TableOfContents("Table of Contents", { hyperlink: true, headingStyleRange: "1-2" }));
children.push(new PageBreak());

// ============================================================
// CHAPTER 1: PCS Basic Concepts & IP Manual Cross-Reference
// ============================================================
children.push(h1("1. PCS 基础概念与 IP 手册对照"));

// 1.1
children.push(h2("1.1 PCS 在以太网协议栈中的位置"));
children.push(para("以太网物理层分层模型：MAC → PCS (Physical Coding Sublayer) → PMA (Physical Medium Attachment) → PMD (Physical Medium Dependent)"));
children.push(para("PCS 子层的核心职责："));
children.push(bullet("线路编码/解码：8B/10B (1G/10G-X), 64B/66B (10G-R/25G+), 256B/257B (RS-FEC)"));
children.push(bullet("数据同步：Block Lock, Deskew (多通道), Lane Alignment"));
children.push(bullet("自协商 (Auto-Negotiation)：CL37 (SGMII), CL73 (KR/25G+)"));
children.push(bullet("前向纠错 (FEC)：CL74 Fire Code, RS(528,514), RS(544,514)"));
children.push(bullet("EEE (Energy Efficient Ethernet)：低功耗管理"));

// 1.2
children.push(h2("1.2 Synopsys PCS IP 家族对比"));

children.push(para("Synopsys 提供两款 PCS IP 覆盖不同速率范围，以下基于最新版本 Databook 和 ComponentConfiguration 进行对比："));

const ipCompareTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 3580, 3580],
  rows: [
    bodyRow([
      headerCell("维度", 2200),
      headerCell("DWC_xpcs (v3.50a, Apr 2022)", 3580),
      headerCell("DWC_xlgpcs (v2.20a, Oct 2023)", 3580),
    ]),
    bodyRow([subHeaderCell("产品代码", 2200), tCell("3977-0 (Ether PCS)"), tCell("5528-0 (Enterprise Ether PCS)")]),
    bodyRow([subHeaderCell("速率范围", 2200), tCell("1G / 2.5G / 5G / 10G"), tCell("5G / 10G / 25G / 40G / 50G / 100G")]),
    bodyRow([subHeaderCell("PCS 模式", 2200), tCell("PCS-X (多通道XAUI)\nPCS-R (串行Base-R)"), tCell("PCS-R (高速串行)")]),
    bodyRow([subHeaderCell("MAC 接口", 2200), tCell("XGMII SDR/DDR/DDW, GMII"), tCell("XLGMII (32/64/128-bit)")]),
    bodyRow([subHeaderCell("自协商协议", 2200), tCell("CL37 (1G/SGMII)\nCL73 (KR), CL72 (KR训练)"), tCell("CL73 (25G+)\nCL72/CL92/CL93 (链路训练)")]),
    bodyRow([subHeaderCell("FEC 类型", 2200), tCell("CL74 Fire Code FEC"), tCell("CL74 Fire Code\nRS(528,514), RS(544,514)")]),
    bodyRow([subHeaderCell("PAM4 支持", 2200), tCell("不支持"), tCell("支持 (50G/100G PAM4)")]),
    bodyRow([subHeaderCell("管理接口", 2200), tCell("MDIO / MCI / APB3\n(legacy/16-bit/32-bit)"), tCell("MDIO / MCI / APB3\n(legacy/16-bit/32-bit)")]),
    bodyRow([subHeaderCell("汽车安全", 2200), tCell("ECC + 数据通路奇偶校验\n+ FSM 保护"), tCell("ECC + 数据通路奇偶校验\n+ FSM 保护")]),
  ]
});
children.push(ipCompareTable);

// 1.3 Current config
children.push(h2("1.3 当前项目配置对照"));
children.push(para("以下配置来自 xpcs_ComponentConfiguration.html 和 xlgpcs_ComponentConfiguration.html："));

children.push(h3("1.3.1 DWC_xpcs 当前配置"));

const xpcsCfgTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3500, 5860],
  rows: [
    bodyRow([headerCell("参数 (Parameter Name)", 3500), headerCell("当前值 (Value)", 5860)]),
    bodyRow([tCell("MAIN_MODE"), tCell("2 (Backplane Ethernet PCS)")]),
    bodyRow([tCell("BACKPLANE_ETH_CONFIG"), tCell("5 (KR_KX)")]),
    bodyRow([tCell("ADD_2PT5G / ADD_5G"), tCell("1 / 1")]),
    bodyRow([tCell("USXG_SPORT"), tCell("1 (Single-port USXGMII)")]),
    bodyRow([tCell("CSR_INTERFACE"), tCell("4 (APB3 32-bit), INDIRECT_ACCESS=1")]),
    bodyRow([tCell("FEC_EN / FEC_ERROR_FWD"), tCell("1 / 1")]),
    bodyRow([tCell("CL72_EN / CL37_AN"), tCell("1 / 1")]),
    bodyRow([tCell("SGM_OR_QSGM_SEL"), tCell("1 (SGMII Only)")]),
    bodyRow([tCell("EEE_EN"), tCell("1")]),
    bodyRow([tCell("DWC_XPCS_ASP"), tCell("1 (Automotive Safety)")]),
    bodyRow([tCell("SNPS_PHY_TYPE"), tCell("4 (Multi-protocol 32G)")]),
    bodyRow([tCell("REF_CLK_FREQ"), tCell("0 (156.25 MHz)")]),
    bodyRow([tCell("TX_CNTX_SEL / RX_CNTX_SEL / CM_CNTX_SEL"), tCell("0x9 / 0x9 / 0x9")]),
  ]
});
children.push(xpcsCfgTable);

children.push(h3("1.3.2 DWC_xlgpcs 当前配置"));

const xlgpcsCfgTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3500, 5860],
  rows: [
    bodyRow([headerCell("参数 (Parameter Name)", 3500), headerCell("当前值 (Value)", 5860)]),
    bodyRow([tCell("DWCXLP_MAIN_MODE"), tCell("0 (25G)")]),
    bodyRow([tCell("DWCXLP_SINGLE_LANE"), tCell("1")]),
    bodyRow([tCell("DWCXLP_CL73_AN"), tCell("1")]),
    bodyRow([tCell("DWCXLP_TS_EN"), tCell("1 (Timestamping Accuracy)")]),
    bodyRow([tCell("DWCXLP_CSR_INTERFACE"), tCell("4 (APB3 32-bit)")]),
    bodyRow([tCell("DWCXLP_FEC_EN / DWCXLP_RSFEC_EN"), tCell("1 (CL74) / 1 (RS528,514)")]),
    bodyRow([tCell("DWCXLP_LNK_TRAIN_EN"), tCell("1")]),
    bodyRow([tCell("DWCXLP_CLK_CMP_EN / SCC_ALGN_EN"), tCell("1 / 1")]),
    bodyRow([tCell("DWCXLP_EEE_EN"), tCell("1")]),
    bodyRow([tCell("DWCXLP_ASP"), tCell("1 (Automotive Safety)")]),
    bodyRow([tCell("DWCXLP_PHY_TYPE"), tCell("3 (Multi-protocol 32G)")]),
    bodyRow([tCell("DWCXLP_XLGMII_INTF_WIDTH"), tCell("2 (32-bit)")]),
    bodyRow([tCell("DWCXLP_R_SRDS_INTF"), tCell("1 (32-bit width)")]),
  ]
});
children.push(xlgpcsCfgTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 2: Register Architecture & API Layer
// ============================================================
children.push(h1("2. 寄存器间接寻址架构与 API 分层"));

// 2.1
children.push(h2("2.1 寻址机制"));
children.push(para("Synopsys DesignWare PCS IP 统一使用两级间接寻址："));
children.push(bullet("32位寄存器地址 = 13位页号 (bits 22:10) + 10位偏移 (bits 9:0)"));
children.push(bullet("写入页号到 XPCS_ADDRESS (0x03FC)，然后读写偏移地址完成操作"));
children.push(bullet("每页包含 1024 个寄存器偏移，最大 8192 页，理论地址空间 8M registers"));

children.push(h2("2.2 三层 API 设计"));

const apiTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1500, 3500, 1500, 2860],
  rows: [
    bodyRow([
      headerCell("API 层", 1500), headerCell("函数", 3500),
      headerCell("实现位置", 1500), headerCell("使用场景", 2860)
    ]),
    bodyRow([
      tCell("通用OSI层"),
      tCell("xpcs_read / xpcs_write"),
      tCell("xpcs.h (inline)"),
      tCell("MGBE XPCS/XLGPCS 通用寄存器操作")
    ]),
    bodyRow([
      tCell("安全写入层"),
      tCell("xpcs_write_safety"),
      tCell("xpcs.h (inline)"),
      tCell("关键寄存器写后读回验证，重试机制")
    ]),
    bodyRow([
      tCell("NV 直访层"),
      tCell("nv_xpcs_read / nv_xpcs_write / nv_xpcs_write_safety"),
      tCell("oxpcs.h (inline)"),
      tCell("EQOS/oxpcs 模块，使用 volatile 指针直接访问")
    ]),
  ]
});
children.push(apiTable);

children.push(para("关键差异："));
children.push(bullet("xpcs_* 使用 osi_readl/osi_writel —— OSI 框架封装，含 pre_sil 等逻辑"));
children.push(bullet("nv_xpcs_* 使用 nv_osi_readl/nv_osi_writel —— volatile uint32_t* 直接访问，无框架开销"));
children.push(bullet("xpcs_write_safety 重试策略：首次 udelay(1us) busy wait，后续 usleep(10us) 让出 CPU"));

// 2.3
children.push(h2("2.3 寄存器地址宏（按 IP 分类）"));

children.push(h3("2.3.1 XPCS (DWC_xpcs) 核心寄存器"));

const xpcsRegTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3600, 1200, 4560],
  rows: [
    bodyRow([headerCell("寄存器宏", 3600), headerCell("地址", 1200), headerCell("功能与关键位域", 4560)]),
    bodyRow([tCell("XPCS_SR_XS_PCS_STS1"), tCell("0xC0004", undefined), tCell("PCS 状态1 — RLU(bit2), FLT(bit7)")]),
    bodyRow([tCell("XPCS_SR_XS_PCS_CTRL2"), tCell("0xC001C", undefined), tCell("PCS 类型选择 — PCS_TYPE_SEL_BASE_R(0x0)")]),
    bodyRow([tCell("XPCS_VR_XS_PCS_DIG_CTRL1"), tCell("0xE0000", undefined), tCell("数字控制 — USXG_EN(bit9), VR_RST(bit15), USRA_RST(bit10), CL37_BP(bit12)")]),
    bodyRow([tCell("XPCS_VR_XS_PCS_KR_CTRL"), tCell("0xE001C", undefined), tCell("KR/USXG 模式选择 — USXG_MODE_MASK(bits12:10), USXG_MODE_5G(bit10)")]),
    bodyRow([tCell("XPCS_SR_AN_CTRL"), tCell("0x1C0000", undefined), tCell("AN 控制 — AN_EN(bit12)")]),
    bodyRow([tCell("XPCS_SR_MII_CTRL"), tCell("0x7C0000", undefined), tCell("MII 接口速率+AN — AN_ENABLE(bit12), RESTART_AN(bit9), SS5/SS6/SS13")]),
    bodyRow([tCell("XPCS_VR_MII_AN_INTR_STS"), tCell("0x7E0008", undefined), tCell("AN 完成中断 — CL37_ANCMPLT_INTR(bit0)")]),
    bodyRow([tCell("XPCS_SR_PMA_KR_FEC_CTRL"), tCell("0x402AC", undefined), tCell("BASE-R FEC — FEC_EN(bit0), EN_ERR_IND(bit1)")]),
    bodyRow([tCell("XPCS_VR_XS_PCS_EEE_MCTRL0"), tCell("0xE0018", undefined), tCell("EEE 主控 — LTX_EN(bit0), LRX_EN(bit1)")]),
    bodyRow([tCell("XPCS_VR_XS_PCS_EEE_MCTRL1"), tCell("0xE002C", undefined), tCell("EEE 主控1 — TRN_LPI(bit0)")]),
  ]
});
children.push(xpcsRegTable);

children.push(h3("2.3.2 XLGPCS (DWC_xlgpcs) 核心寄存器"));

const xlgpcsRegTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3600, 1200, 4560],
  rows: [
    bodyRow([headerCell("寄存器宏", 3600), headerCell("地址", 1200), headerCell("功能与关键位域", 4560)]),
    bodyRow([tCell("XLGPCS_SR_PCS_CTRL1"), tCell("0xC0000", undefined), tCell("PCS 控制1 — RST(bit15), SS5_2(bits4:2)")]),
    bodyRow([tCell("XLGPCS_SR_PCS_STS1"), tCell("0xC0004", undefined), tCell("PCS 状态1 — RLU(bit2)")]),
    bodyRow([tCell("XLGPCS_SR_PCS_CTRL2"), tCell("0xC001C", undefined), tCell("PCS 类型选择 — PCS_TYPE_SEL(bits3:0)")]),
    bodyRow([tCell("XLGPCS_VR_PCS_DIG_CTRL1"), tCell("0xE0000", undefined), tCell("数字控制1 — VR_RST(bit15)")]),
    bodyRow([tCell("XLGPCS_VR_PCS_DIG_CTRL3"), tCell("0xE000C", undefined), tCell("数字控制3 — CNS_EN(bit0)")]),
    bodyRow([tCell("XLGPCS_SR_AN_CTRL"), tCell("0x1C0000", undefined), tCell("AN 控制 — AN_EN(bit12)")]),
    bodyRow([tCell("XLGPCS_SR_PMA_CTRL2"), tCell("0x4001C", undefined), tCell("PMA 类型选择 — PMA_TYPE_MASK(0x7F)")]),
  ]
});
children.push(xlgpcsRegTable);

// 2.4 UPHY/Wrapper
children.push(h2("2.4 UPHY/Wrapper 寄存器（按 MAC 类型变化）"));

const uphyTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2400, 1100, 1100, 1100, 3560],
  rows: [
    bodyRow([
      headerCell("寄存器名", 2400), headerCell("MGBE", 1100),
      headerCell("EQOS", 1100), headerCell("T26X", 1100), headerCell("功能", 3560)
    ]),
    bodyRow([tCell("UPHY_HW_INIT_CTRL"), tCell("0x8020", undefined), tCell("0x8038", undefined), tCell("0x8038", undefined), tCell("Lane Init (TX_EN bit0, RX_EN bit2)")]),
    bodyRow([tCell("UPHY_STATUS"), tCell("0x8044", undefined), tCell("0x8064", undefined), tCell("0x8080", undefined), tCell("Lane Power Up (TX_P_UP bit0, RX_P_UP bit2)")]),
    bodyRow([tCell("INTERRUPT_STATUS"), tCell("0x8050", undefined), tCell("0x8070", undefined), tCell("0x808C", undefined), tCell("IRQ Status (PCS_LINK_STS bit6)")]),
    bodyRow([tCell("UPHY_RX_CONTROL"), tCell("0x801C", undefined), tCell("—", undefined), tCell("0x8034", undefined), tCell("RX Lane Control (SW Override)")]),
    bodyRow([tCell("INTERRUPT_CONTROL"), tCell("0x8048", undefined), tCell("—", undefined), tCell("0x8084", undefined), tCell("IRQ Enable (HSI)")]),
    bodyRow([tCell("T26X_CONFIG_0"), tCell("—", undefined), tCell("—", undefined), tCell("0x8094", undefined), tCell("PCS Type Select: bit0=0 XPCS, bit0=1 XLGPCS")]),
  ]
});
children.push(uphyTable);

children.push(para("代码中用 uphy_status_reg[] / uphy_init_ctrl_reg[] / uphy_rx_ctrl_reg[] / uphy_irq_sts_reg[] 数组按 MAC 类型索引，避免硬编码地址。"));

children.push(h3("2.4.1 RX_CONTROL 位域完整定义"));

const rxCtrlTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1400, 2500, 5460],
  rows: [
    bodyRow([headerCell("位", 1400), headerCell("名称", 2500), headerCell("描述", 5460)]),
    bodyRow([tCell("bit31"), tCell("RX_SW_OVRD"), tCell("软件覆盖使能")]),
    bodyRow([tCell("bit12"), tCell("RX_EQ_RESET"), tCell("RX 均衡器复位 (T26X 专用)")]),
    bodyRow([tCell("bit11"), tCell("RX_EQ_TRAIN_EN"), tCell("RX 均衡器训练使能 (T26X 专用)")]),
    bodyRow([tCell("bit10"), tCell("RX_PCS_PHY_RDY"), tCell("PHY 就绪")]),
    bodyRow([tCell("bit9"), tCell("RX_CDR_RESET"), tCell("CDR 复位")]),
    bodyRow([tCell("bit8"), tCell("RX_CAL_EN"), tCell("校准使能")]),
    bodyRow([tCell("bit7:6"), tCell("RX_SLEEP"), tCell("睡眠模式 (两位组合)")]),
    bodyRow([tCell("bit5"), tCell("AUX_RX_IDDQ"), tCell("辅助 IDDQ")]),
    bodyRow([tCell("bit4"), tCell("RX_IDDQ"), tCell("IDDQ 模式")]),
    bodyRow([tCell("bit0"), tCell("RX_DATA_EN"), tCell("数据接收使能")]),
  ]
});
children.push(rxCtrlTable);

// 2.5 HSI
children.push(h2("2.5 HSI 安全寄存器"));

const hsiTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 1100, 1100, 1100, 1100, 2760],
  rows: [
    bodyRow([
      headerCell("寄存器", 2200), headerCell("MGBE", 1100), headerCell("EQOS", 1100),
      headerCell("T26X", 1100), headerCell("XLGPCS", 1100), headerCell("功能", 2760)
    ]),
    bodyRow([tCell("SFTY_UE_INTR0"), tCell("0xE03C0", undefined), tCell("—", undefined), tCell("—", undefined), tCell("—", undefined), tCell("不可纠错误中断")]),
    bodyRow([tCell("SFTY_CE_INTR"), tCell("0xE03C8", undefined), tCell("—", undefined), tCell("—", undefined), tCell("—", undefined), tCell("可纠错误中断")]),
    bodyRow([tCell("SFTY_DISABLE_0"), tCell("0xE03D0", undefined), tCell("0x7E03D0", undefined), tCell("—", undefined), tCell("0xE03C4", undefined), tCell("安全功能禁用控制")]),
    bodyRow([tCell("SFTY_TMR_CTRL"), tCell("0xE03D4", undefined), tCell("0x7E03D4", undefined), tCell("—", undefined), tCell("0xE03E4", undefined), tCell("FSM 超时控制")]),
  ]
});
children.push(hsiTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 3: Driver Implementation
// ============================================================
children.push(h1("3. 驱动实现分析"));

// 3.1 Module Function Overview
children.push(h2("3.1 模块函数总览"));

const funcTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3600, 1400, 4360],
  rows: [
    bodyRow([headerCell("函数", 3600), headerCell("文件", 1400), headerCell("用途", 4360)]),
    bodyRow([tCell("xpcs_init()"), tCell("xpcs.c", undefined), tCell("MGBE XPCS USXGMII 初始化 (≤10G)")]),
    bodyRow([tCell("xpcs_start()"), tCell("xpcs.c", undefined), tCell("MGBE XPCS 启动 (AN+速率+链路)")]),
    bodyRow([tCell("xlgpcs_init()"), tCell("xpcs.c", undefined), tCell("XLGPCS 25G 初始化")]),
    bodyRow([tCell("xlgpcs_start()"), tCell("xpcs.c", undefined), tCell("XLGPCS 25G 启动")]),
    bodyRow([tCell("eqos_xpcs_init()"), tCell("oxpcs.c", undefined), tCell("EQOS XPCS SGMII 初始化")]),
    bodyRow([tCell("xpcs_lane_bring_up()"), tCell("xpcs.c", undefined), tCell("UPHY Tx/Rx Lane 启动统一入口")]),
    bodyRow([tCell("xpcs_uphy_lane_bring_up()"), tCell("xpcs.c", undefined), tCell("HW FSM Lane 初始化 (TX 或 RX)")]),
    bodyRow([tCell("sw_ovveride_method_for_uphy_rx_lane()"), tCell("xpcs.c", undefined), tCell("SW Override RX (16步流程)")]),
    bodyRow([tCell("perform_xpcs_rx_eq_reset_and_train()"), tCell("xpcs.c", undefined), tCell("T26X RX EQ 训练")]),
    bodyRow([tCell("xpcs_poll_flt_rx_link()"), tCell("xpcs.c", undefined), tCell("RLU + FLT 双重链路检测")]),
    bodyRow([tCell("xpcs_check_pcs_lock_status()"), tCell("xpcs.c", undefined), tCell("PCS 锁定状态检测")]),
    bodyRow([tCell("vendor_specifc_sw_rst_usxgmii_an_en()"), tCell("xpcs.c", undefined), tCell("Vendor 复位+USXGMII AN 配置")]),
    bodyRow([tCell("xpcs_base_r_fec()"), tCell("xpcs.c", undefined), tCell("BASE-R FEC 使能/禁用 (XPCS+XLGPCS 共用)")]),
    bodyRow([tCell("xpcs_poll_for_an_complete()"), tCell("xpcs.c", undefined), tCell("USXGMII AN 完成轮询")]),
    bodyRow([tCell("xpcs_set_speed()"), tCell("xpcs.c", undefined), tCell("USXGMII 速率设置 (2.5G/5G/10G)")]),
    bodyRow([tCell("xlgpcs_eee()"), tCell("xpcs.c", undefined), tCell("XLGPCS EEE 控制")]),
    bodyRow([tCell("xpcs_eee()"), tCell("oxpcs.c", undefined), tCell("XPCS EEE 控制 (使用 nv_xpcs API)")]),
  ]
});
children.push(funcTable);

// 3.2 Dispatch
children.push(h2("3.2 初始化分发逻辑 (hw_set_speed)"));
children.push(para("core_common.c 的 hw_set_speed() 中按速度决定调用策略："));
children.push(bullet("speed == 25G → xlgpcs_init() → xlgpcs_start() (XLGPCS IP)"));
children.push(bullet("speed ≤ 10G (MGBE/T26X) → xpcs_init() → xpcs_start() (XPCS IP USXGMII)"));
children.push(bullet("speed ≤ 2.5G (EQOS) → eqos_xpcs_init() (XPCS IP SGMII)"));

// 3.3 XPCS init
children.push(h2("3.3 MGBE XPCS 初始化详解 (xpcs_init)"));
children.push(para("对应 IP 手册：DWC_xpcs Databook §7.6 (Switching to USXGMII Mode)"));
children.push(bold("流程："));
children.push(bullet("Step 0: pre_sil 检查 → 跳过 lane bring up (预硅片模式)"));
children.push(bullet("Step 0b: xpcs_base_r_fec() → 配置 BASE-R FEC (若使能)"));
children.push(bullet("Step 0c: [T26X + 10G] → 编程 T26X 时序延迟 + CONFIG_0 bit0=0 (选择 XPCS)"));
children.push(bullet("Step 0d: xpcs_lane_bring_up() → TX Lane + RX Lane + PCS Lock"));
children.push(bullet("Step 1: 设置 PCS_TYPE_SEL_BASE_R → XPCS_SR_XS_PCS_CTRL2 (safety write)"));
children.push(bullet("Step 2-3: USXGMII Mode 配置 → XPCS_VR_XS_PCS_KR_CTRL (safety write), 5G时 USXG_MODE_5G"));
children.push(bullet("Step 4: PHY 速度已在 PHY INIT 完成，跳过"));
children.push(bullet("Step 5: vendor_specifc_sw_rst_usxgmii_an_en()"));
children.push(bullet("  5.1: USXG_EN=1 (safety)"));
children.push(bullet("  5.2: VR_RST=1 (self-clearing, 无需 safety)"));
children.push(bullet("  5.3: Poll VR_RST self-clear (1000 × 1ms)"));
children.push(bullet("  5.4: Backplane 模式: clear SR_AN_CTRL.AN_EN + set CL37_BP (safety)"));

// 3.4 XLGPCS init
children.push(h2("3.4 XLGPCS 初始化详解 (xlgpcs_init)"));
children.push(para("对应 IP 手册：DWC_xlgpcs Databook §8.5 (Switching to 25G Speed Mode)"));
children.push(bold("流程："));
children.push(bullet("Step 0: NULL 检查 + pre_sil 检查"));
children.push(bullet("Step 1: [T26X + 25G] → 编程 20+ 个 T26X 时序寄存器 + CONFIG_0 bit0=1 (选择 XLGPCS)"));
children.push(bullet("  CDR Reset 宽度: 0x21、IDDQ→SLEEP: 0x29 (TX+RX)、RX Power Down SLEEP→IDDQ: 0xA1"));
children.push(bullet("  TX Power Down 五段: 0x29/0x81/0xA1/0x285/0x29"));
children.push(bullet("  CAL_EN→DATAREADY 两段: 0x19/0x79、DATAREADY→DATAEN: 0x82"));
children.push(bullet("  RX EQ: enable bit31, RESET 0x29, TRAIN delay 0x29, TRAIN hi-lo 0x19"));
children.push(bullet("  SLEEP→CAL: 0xFBD, CAL→DATA: 0x78, T0_CTRL2 EQ_DONE_TOV: 0xFFFF"));
children.push(bullet("Step 2: xpcs_base_r_fec() → BASE-R FEC 配置"));
children.push(bullet("Step 3: xpcs_lane_bring_up() → TX + RX + PCS Lock"));
children.push(bullet("[STEP 4 已注释掉]: PCS_TYPE_SEL/CNS_EN 等 IAS 步骤 (当前由 HW 脚本完成)"));

// 3.5 Lane bring up
children.push(h2("3.5 UPHY Lane 启动 (xpcs_lane_bring_up)"));
children.push(bold("统一入口函数，分三个阶段："));
children.push(h3("3.5.1 阶段 1 — TX Lane (xpcs_uphy_lane_bring_up)"));
children.push(bullet("TX_P_UP_STATUS 预检查 (幂等性)"));
children.push(bullet("写 TX_EN 到 HW_INIT_CTRL"));
children.push(bullet("延时策略按 MAC: MGBE=1×1us, T26X=1000×100us (EQ使能), EQOS=1000×10us"));
children.push(h3("3.5.2 阶段 2 — RX Lane"));
children.push(bullet("EQOS: xpcs_uphy_lane_bring_up(RX_EN) — HW FSM 自动序列"));
children.push(bullet("MGBE/T26X: sw_ovveride_method_for_uphy_rx_lane() — 16步软件序列"));
children.push(h3("3.5.3 阶段 3 — PCS Lock"));
children.push(bullet("xpcs_check_pcs_lock_status() — poll INTERRUPT_STATUS bit6 (PCS_LINK_STS)"));
children.push(bullet("结果存入 l_core->lane_status"));

// 3.6 SW Override 16 steps
children.push(h2("3.6 SW Override RX Lane 16步流程"));

const swStepsTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1300, 2600, 5460],
  rows: [
    bodyRow([headerCell("步骤", 1300), headerCell("操作", 2600), headerCell("详情", 5460)]),
    bodyRow([tCell("1"), tCell("Set RX_SW_OVRD (bit31)"), tCell("使能软件覆盖")]),
    bodyRow([tCell("2"), tCell("Clear RX_IDDQ | AUX_RX_IDDQ"), tCell("退出 IDDQ (一步完成)")]),
    bodyRow([tCell("3"), tCell("udelay(1us)"), tCell("HW 要求最小 50ns")]),
    bodyRow([tCell("4"), tCell("Clear RX_SLEEP (bits7:6)"), tCell("退出睡眠")]),
    bodyRow([tCell("5"), tCell("udelay(1us)"), tCell("HW 要求最小 500ns")]),
    bodyRow([tCell("6"), tCell("Set RX_CAL_EN (bit8)"), tCell("启用校准")]),
    bodyRow([tCell("7"), tCell("Poll RX_CAL_EN clear"), tCell("7次×200us (HW上限 100us)")]),
    bodyRow([tCell("8"), tCell("udelay(1us)"), tCell("—")]),
    bodyRow([tCell("9"), tCell("Set RX_DATA_EN (bit0)"), tCell("启用接收; lane_powered_up=OSI_ENABLE")]),
    bodyRow([tCell("10"), tCell("Clear RX_PCS_PHY_RDY (bit10)"), tCell("PHY 复位")]),
    bodyRow([tCell("11"), tCell("udelay(1us)"), tCell("—")]),
    bodyRow([tCell("12"), tCell("Set RX_CDR_RESET (bit9)"), tCell("CDR 复位; [T26X] 同时 set RX_EQ_RESET(bit12)")]),
    bodyRow([tCell("13"), tCell("udelay(1us)"), tCell("—")]),
    bodyRow([tCell("14"), tCell("Set RX_PCS_PHY_RDY (bit10)"), tCell("PHY 就绪; usleep(30000us)")]),
    bodyRow([tCell("15"), tCell("Clear RX_CDR_RESET"), tCell(
      "CDR 复位结束; [T26X] → EQ reset(2ms) + EQ train(70ms); [MGBE] → usleep(30000us)"
    )]),
  ]
});
children.push(swStepsTable);

// 3.7 EQ training
children.push(h2("3.7 T26X EQ 训练 (perform_xpcs_rx_eq_reset_and_train)"));
children.push(bullet("Step 1: Poll RX_EQ_RESET(bit12) clear — 3次×1ms = 2ms 超时"));
children.push(bullet("Step 2: Set RX_EQ_TRAIN_EN(bit11)"));
children.push(bullet("Step 3: Poll RX_EQ_TRAIN_EN clear — 70次×1ms = 70ms 超时"));

// 3.8 XPCS start
children.push(h2("3.8 XPCS 启动 (xpcs_start)"));
children.push(para("对应 IP 手册：DWC_xpcs Databook §7.15 (CL73 AN) + §7.13 (CL37 AN)"));
children.push(para("仅在 USXGMII 10G/5G 模式下生效："));
children.push(bullet("Step 1: update_an_status() → 从 DT 获取默认速率 (10G→0xC00, 5G→0x1400)"));
children.push(bullet("Step 2: [skip_usxgmii_an == DISABLE]: SR_MII_CTRL |= AN_ENABLE (safety)"));
children.push(bullet("Step 3: xpcs_poll_for_an_complete() → 1000×1ms (pre_sil: 10000×1ms)"));
children.push(bullet("Step 4: xpcs_set_speed(an_status) → 配置 SS5/SS6/SS13 (safety)"));
children.push(bullet("Step 5: USRA_RST → poll self-clear (1000×1ms)"));
children.push(bullet("Step 6: xpcs_poll_flt_rx_link()："));
children.push(bullet("  Poll SR_XS_PCS_STS1 RLU(bit2) → 1次"));
children.push(bullet("  Poll SR_XS_PCS_STS1 FLT(bit7)=0 → 1000×1ms (T26X 跳过)"));
children.push(bullet("  Delay 10ms 等状态传播到 MAC"));

// 3.9 XLGPCS start
children.push(h2("3.9 XLGPCS 启动 (xlgpcs_start)"));
children.push(para("对应 IP 手册：DWC_xlgpcs Databook §8.11 (CL73 AN)"));
children.push(bullet("[FEC not enabled]: XLGPCS_SR_PCS_CTRL1 |= RST(bit15) → poll self-clear (1000×10us)"));
children.push(bullet("Clear XLGPCS_SR_AN_CTRL.AN_EN (safety)"));
children.push(bullet("Poll XLGPCS_SR_PCS_STS1 RLU(bit2) → 1000×10us"));

// 3.10 AN complete
children.push(h2("3.10 AN 完成检测 (xpcs_poll_for_an_complete)"));
children.push(bullet("retry = 1000 (pre_sil = 10000)"));
children.push(bullet("poll VR_MII_AN_INTR_STS bit0 (CL37_ANCMPLT_INTR) → 清除中断位 (safety)"));
children.push(bullet("关键校验: status & USXG_AN_STS_SPEED_MASK != 0 (防止 zero-speed AN)"));

// 3.11 Speed table
children.push(h2("3.11 速率设置表 (xpcs_set_speed)"));

const speedTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1500, 1600, 1600, 1600, 3060],
  rows: [
    bodyRow([
      headerCell("速率", 1500), headerCell("AN 状态值", 1600),
      headerCell("SS5 (bit5)", 1600), headerCell("SS6 (bit6)", 1600), headerCell("SS13 (bit13)", 3060)
    ]),
    bodyRow([tCell("2.5Gbps"), tCell("0x1000", undefined), tCell("1", undefined), tCell("0", undefined), tCell("0", undefined)]),
    bodyRow([tCell("5Gbps"), tCell("0x1400", undefined), tCell("1", undefined), tCell("0", undefined), tCell("1", undefined)]),
    bodyRow([tCell("10Gbps (default)"), tCell("0x0C00", undefined), tCell("0", undefined), tCell("1", undefined), tCell("1", undefined)]),
  ]
});
children.push(speedTable);

// 3.12 EEE
children.push(h2("3.12 EEE 功能控制"));

children.push(h3("3.12.1 xpcs_eee() — oxpcs.c (nv_xpcs API)"));
children.push(bullet("适用: EQOS + MGBE XPCS"));
children.push(bullet("Disable: 清除 LTX_EN(bit0) & LRX_EN(bit1)"));
children.push(bullet("Enable: check EEE能力 → 定时器(默认匹配) → 使能 LTX_EN|LRX_EN → 使能 TRN_LPI(bit0)"));
children.push(bullet("时钟: clk_eee_i = 102MHz, MULT_FACT_100NS = 9"));

children.push(h3("3.12.2 xlgpcs_eee() — xpcs.c (xpcs API)"));
children.push(bullet("适用: T26X XLGPCS 25G"));
children.push(bullet("Disable: 清除 LTX_EN & LRX_EN (safety) → poll TX_ACTIVE 状态 (1000×100us)"));
children.push(bullet("Enable: check EEE → 定时器 → FEC/RS-FEC → 使能 LTX_EN|LRX_EN (safety) → PMA service (NA)"));

// 3.13 HSI Error
children.push(h2("3.13 HSI 安全错误处理"));

const errTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 2000, 1700, 1600, 1860],
  rows: [
    bodyRow([
      headerCell("错误类型", 2200), headerCell("错误码", 2000),
      headerCell("索引", 1700), headerCell("触发位置", 1600),
      headerCell("处理方式", 1860)
    ]),
    bodyRow([tCell("PCS AN 超时"), tCell("0x1004", undefined), tCell("AUTONEG_ERR_IDX(5)", undefined), tCell("xpcs_poll_for_an_complete", undefined), tCell("err_code + report_err + report_count_err")]),
    bodyRow([tCell("XPCS 写失败"), tCell("0x1009", undefined), tCell("XPCS_WRITE_FAIL_IDX(6)", undefined), tCell("xpcs_write_safety", undefined), tCell("err_code + report_err")]),
    bodyRow([tCell("PCS 链路超时"), tCell("0x13", undefined), tCell("PCS_LNK_ERR_IDX(9)", undefined), tCell("eqos_xpcs_init (LINK_STS)", undefined), tCell("err_code + report_err + report_count_err")]),
    bodyRow([tCell("PCS 可纠错误"), tCell("—", undefined), tCell("—", undefined), tCell("mgbe_core ISR", undefined), tCell("计数 + WARN")]),
    bodyRow([tCell("PCS 不可纠错误"), tCell("—", undefined), tCell("—", undefined), tCell("mgbe_core ISR", undefined), tCell("计数 + 禁用中断 + ERR")]),
    bodyRow([tCell("PCS 寄存器奇偶错"), tCell("—", undefined), tCell("—", undefined), tCell("mgbe_core ISR", undefined), tCell("计数 + 禁用中断 + ERR")]),
  ]
});
children.push(errTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 4: Source File Index
// ============================================================
children.push(h1("4. 源代码文件索引"));

const srcTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3800, 5560],
  rows: [
    bodyRow([headerCell("文件路径", 3800), headerCell("内容说明", 5560)]),
    bodyRow([tCell("hardware/osi/core/xpcs.h"), tCell("XPCS/XLGPCS 寄存器地址宏 + 位域定义 + xpcs_read/write/safety API")]),
    bodyRow([tCell("hardware/osi/core/xpcs.c"), tCell("xpcs_init/start, xlgpcs_init/start, lane_bring_up, FEC, EEE 全流程 (1338行)")]),
    bodyRow([tCell("hardware/osi/phy/oxpcs.h"), tCell("nv_xpcs_read/write API — volatile 直访层 (nvxpcsrm 模块)")]),
    bodyRow([tCell("hardware/osi/phy/oxpcs.c"), tCell("eqos_xpcs_init (EQOS SGMII), xpcs_eee (使用 nv API), mixed_bank_reg_prog")]),
    bodyRow([tCell("hardware/osi/core/core_common.c"), tCell("hw_set_speed() 分发逻辑 — 按速度调用 xpcs_init/xlgpcs_init/eqos_xpcs_init")]),
    bodyRow([tCell("hardware/include/osi_core.h"), tCell("osi_core_priv_data 结构体: xpcs_base, uphy_gbe_mode, skip_usxgmii_an, pcs_base_r_fec_en")]),
  ]
});
children.push(srcTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 5: IP Manual Cross-Reference
// ============================================================
children.push(h1("5. 与 IP 手册的对应关系"));

const refTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3600, 5760],
  rows: [
    bodyRow([headerCell("代码实现", 3600), headerCell("IP 手册章节对应", 5760)]),
    bodyRow([tCell("xpcs_init()"), tCell("DWC_xpcs Databook §7.6 (Switching to USXGMII Mode)")]),
    bodyRow([tCell("xlgpcs_init()"), tCell("DWC_xlgpcs Databook §8.5 (Switching to 25G Speed Mode)")]),
    bodyRow([tCell("xpcs_start()"), tCell("DWC_xpcs Databook §7.15 (CL73 AN) + §7.13 (CL37 AN)")]),
    bodyRow([tCell("xlgpcs_start()"), tCell("DWC_xlgpcs Databook §8.11 (CL73 AN)")]),
    bodyRow([tCell("xpcs_lane_bring_up()"), tCell("DWC_xpcs Databook §7.2 (10G XAUI/KX4 mode) + §7.4 (1G/KX mode)")]),
    bodyRow([tCell("sw_ovveride_method_for_uphy_rx_lane()"), tCell("UPHY RX SW Override 流程 — HW 团队提供的序列")]),
    bodyRow([tCell("xpcs_base_r_fec()"), tCell("DWC_xpcs §2.4 (PCS-R/FEC) + DWC_xlgpcs §2.2.1 (TX FEC)")]),
    bodyRow([tCell("xlgpcs_eee()"), tCell("DWC_xlgpcs Databook §8.12 (EEE)")]),
    bodyRow([tCell("xpcs_eee()"), tCell("DWC_xpcs Databook §7.16 (EEE)")]),
    bodyRow([tCell("HSI 安全错误处理"), tCell("DWC_xpcs Ch.8 / DWC_xlgpcs Ch.4 (Automotive Safety)")]),
    bodyRow([tCell("T26X 时序寄存器"), tCell("HW 脚本生成，对应 PHY Interface 参数章节")]),
    bodyRow([tCell("xpcs_poll_for_an_complete()"), tCell("DWC_xpcs §7.13 (CL37 AN) — AN complete 状态轮询")]),
    bodyRow([tCell("BPMP/ATF SerDes 初始化"), tCell("DWC_32g_PHY Databook §4.2 (Init Sequence) + §4.16 (SRAM) + §4.17 (Bootloading) + §4.18 (Firmware) + §4.24 (Context Restore)")]),
    bodyRow([tCell("image_gen.pl 脚本 & cntx_sel"), tCell("DWC_32g_PHY Databook §4.24.4 (Generating SRAM and ROM Images) + Configuration Reference Manual (.xlsx)")]),
    bodyRow([tCell("CR_PARA 接口"), tCell("DWC_32g_PHY Reference Manual §1.6 (Parallel Control Register Port Signals)")]),
  ]
});
children.push(refTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 6: SerDes PHY Initialization & Software Layering
// ============================================================
children.push(h1("6. SerDes PHY 初始化流程与软件分层"));

// 6.1 Overview
children.push(h2("6.1 概述"));
children.push(para("本章基于 DWC_32g_PHY tsmc12ffc x2ns Databook v3.01a & Reference Manual v3.01a (Product Code: H976-0)，分析 Synopsys Multi-Protocol 32G PHY 的完整初始化流程，以及驱动代码中 UPHY Wrapper 操作与底层 SerDes 固件初始化之间的边界。"));

children.push(h2("6.2 PHY 初始化完整时序"));
children.push(para("Databook §4.2 Figure 4-1 定义了 PHY 从电源上电到 Ready 的完整流程。时序图如下："));

const phyInitSeqTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2000, 2000, 5360],
  rows: [
    bodyRow([headerCell("阶段", 2000), headerCell("关键事件", 2000), headerCell("说明 (Databook 引用)", 5360)]),
    bodyRow([tCell("1. 电源上电"), tCell("VP/VPH/VPTx 上电"), tCell("任意顺序，单调 ramp ≥10µs (§4.1)")]),
    bodyRow([tCell("2. 复位断言"), tCell("phy_reset=1"), tCell("POR ≥15µs, Warm Reset ≥10ns (§4.2)")]),
    bodyRow([tCell("3. Primary Inputs"), tCell("配置 MPLL/Lane 输入"), tCell("txX_rate/width, rxX_rate/width, cntx_sel, MPLL 参数 (§4.2 Step 2)")]),
    bodyRow([tCell("4. SRAM Bootloading"), tCell("ROM→SRAM 加载"), tCell("PHY 内部 bootloader 从 ROM 拷贝 firmware 到 SRAM。sram_bootload_bypass=1 可跳过 (§4.16)")]),
    bodyRow([tCell("5. SRAM External Load"), tCell("SoC 侧加载(可选)"), tCell("通过 CR_PARA/JTAG 更新 SRAM。完成后设 sram_ext_ld_done (§4.16)")]),
    bodyRow([tCell("6. Firmware 启动"), tCell("校准 + 初始化"), tCell("包括 Resistor Tuning, MPLL Calibration, Initial Calibration (§4.2.1, §4.9.2)")]),
    bodyRow([tCell("7. Lane 握手"), tCell("txX_ack/rxX_ack=0"), tCell("PHY Ready，进入 Pn state (P0/P0s/P1/P2) (§4.2 Step 4)")]),
  ]
});
children.push(phyInitSeqTable);

// 6.3 SRAM/Firmware bootloading options
children.push(h2("6.3 SRAM/Firmware 加载模式"));
children.push(para("Databook §4.17 Table 4-23 定义了 4 种 firmware 加载模式："));

const bootTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1100, 1000, 2400, 1800, 3060],
  rows: [
    bodyRow([headerCell("sram_bootload_bypass", 1100), headerCell("sram_bypass", 1000), headerCell("启动方式", 2400), headerCell("Firmware 存储", 1800), headerCell("Firmware 文件", 3060)]),
    bodyRow([tCell("0"), tCell("0"), tCell("PHY 自动加载 ROM→SRAM"), tCell("SRAM"), tCell("Internal: pcs_raw_mem_rst.v / External: pcs_raw_ext_rom.bin")]),
    bodyRow([tCell("0"), tCell("1"), tCell("从 ROM 直接执行(无SRAM)"), tCell("ROM"), tCell("pcs_raw_mem_rst.v 或 pcs_raw_ext_rom.bin")]),
    bodyRow([tCell("1"), tCell("0"), tCell("SoC 侧加载 SRAM"), tCell("SRAM"), tCell("CR_PARA: pcs_raw_mem_sram_cr_para.fw / Side-load: pcs_raw_ext_rom.bin")]),
    bodyRow([tCell("1"), tCell("1"), tCell("ROM 直接执行(无SRAM)"), tCell("ROM"), tCell("pcs_raw_mem_rst.v 或 pcs_raw_ext_rom.bin")]),
  ]
});
children.push(bootTable);

children.push(para("手册在 §4.16 和 §4.18 两次强调：\"It is mandatory to update the PHY firmware post-tapeout in silicon, therefore, the SoC must be capable of handling this update.\"（芯片流片后必须能更新 PHY firmware）"));

// 6.4 Context Restore
children.push(h2("6.4 Context Restore 与 Context Select"));
children.push(para("Context Restore (§4.24) 是 PHY 内部的自动化机制：从 SRAM/ROM 中读取预存配置 (CREG)，自动恢复 Tx/Rx/CM 设置，不再需要软件逐位配置 Primary Inputs。"));

children.push(h3("6.4.1 Firmware Image 生成流程"));
children.push(para("Synopsys 提供 image_gen.pl Perl 脚本，根据所选协议、速率、参考时钟生成组合 firmware image："));

const imageGenTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2800, 6560],
  rows: [
    bodyRow([headerCell("输入/输出", 2800), headerCell("文件", 6560)]),
    bodyRow([tCell("输入"), tCell("protocol_input.txt — 选择协议 (P2=PCIe, P9=Ethernet)、速率、参考时钟频率")]),
    bodyRow([tCell("输入"), tCell("all_protocols.txt — 所有协议/速率的二进制配置数据")]),
    bodyRow([tCell("输入"), tCell("protocols_metadata.txt — 配置的二进制元数据")]),
    bodyRow([tCell("输出"), tCell("ROM/SRAM Image (.bin) — firmware code + context restore 配置")]),
    bodyRow([tCell("输出"), tCell("context_sel_ID.txt — txX_cntx_sel, rxX_cntx_sel, tx_cmnX_cntx_sel 有效范围")]),
  ]
});
children.push(imageGenTable);

children.push(para("Figure 4-42 示例展示了 PCIe (Protocol ID=2, cntx_ID=0..4) 和 Ethernet (Protocol ID=9, cntx_ID=5..6) 共存于同一 image。项目中的 TX_CNTX_SEL=0x9 对应 Ethernet AUI@25.78125G 配置。一份 firmware image 可包含多种协议 — 每个 lane 通过自己的 txX_cntx_sel 独立选择。"));

// 6.5 CR_PARA interface
children.push(h2("6.5 CR Parallel Port 接口"));
children.push(para("CR_PARA interface (Reference Manual §1.6) 是 SoC 访问 PHY 内部 CREG 和 SRAM 的硬件接口。信号如下："));

const crParaTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2500, 1500, 5360],
  rows: [
    bodyRow([headerCell("信号", 2500), headerCell("I/O", 1500), headerCell("说明", 5360)]),
    bodyRow([tCell("cr_para_clk"), tCell("I"), tCell("CR 时钟 (≤125MHz)")]),
    bodyRow([tCell("cr_para_sel"), tCell("I"), tCell("接口选择: 0=JTAG, 1=CR Parallel (仅 cr_para_clk 和 jtag_tck 关闭时可切换)")]),
    bodyRow([tCell("cr_para_addr[15:0]"), tCell("I"), tCell("寄存器/SRAM 地址")]),
    bodyRow([tCell("cr_para_wr_data[15:0]"), tCell("I"), tCell("写数据")]),
    bodyRow([tCell("cr_para_wr_en"), tCell("I"), tCell("写使能 (高有效)")]),
    bodyRow([tCell("cr_para_rd_data[15:0]"), tCell("O"), tCell("读数据")]),
    bodyRow([tCell("cr_para_rd_en"), tCell("I"), tCell("读使能 (高有效)")]),
    bodyRow([tCell("cr_para_ack"), tCell("O"), tCell("读写完成确认")]),
  ]
});
children.push(crParaTable);

children.push(para("SRAM 地址空间为 16K×16-bit，通过 RAWCMN_DIG_LANE_FSM_OP_XTND.MEM_ADDR_EXT_EN 选择上/下半区。SRAM 访问流程 (§4.16)：sram_init_done 断言后 → CR_PARA 访问 → sram_ext_ld_done 断言 → 固件启动。"));

// 6.6 Initialization layering
children.push(h2("6.6 初始化分层：BPMP vs 驱动"));

children.push(para("综合 Databook 证据和驱动代码分析，32G PHY 初始化分为两个阶段、两层软件："));

const layerTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1600, 3600, 4160],
  rows: [
    bodyRow([headerCell("层级", 1600), headerCell("负责者", 3600), headerCell("职责", 4160)]),
    bodyRow([tCell("阶段 1"), tCell("BPMP (Boot and Power Management Processor) 或 ATF (ARM Trusted Firmware)"), tCell("电源排序 → 时钟使能 → Primary Inputs 设置 → FW Image 加载到 SRAM → phy_reset 解除 → 等待校准完成 (txX_ack/rxX_ack=0) → FW 版本验证")]),
    bodyRow([tCell("阶段 2"), tCell("Linux 驱动 (xpcs.c)"), tCell("UPHY Wrapper Lane Bring-up (IDDQ→SLEEP→CAL→CDR→DATA_EN) → PCS 协议配置 (BASE-R/USXGMII/AN/FEC/EEE) → 链路检测")]),
  ]
});
children.push(layerTable);

children.push(para("驱动代码中的证据："));

children.push(bullet("xpcs_init() 注释确认：\"Program PHY to operate at 10Gbps/5Gbps/2Gbps — this step not required since PHY speed programming already done as part of phy INIT\" — 此 phy INIT 即 BPMP/ATF 的 SerDes 初始化"));
children.push(bullet("xlgpcs_init() #if 0 块 (PCS_TYPE_SEL/CNS_EN/PMA_TYPE 等) 已被 BPMP 侧完成，驱动跳过"));
children.push(bullet("驱动只操作 UPHY Wrapper 寄存器 (0x8000-0x80C0) 和 PCS 协议层寄存器 (0x03FC 间接寻址)，从不直接访问 PHY CREG 或 SRAM"));

// 6.7 CR_PARA vs UPHY Wrapper
children.push(h2("6.7 控制接口对比"));

const ifaceTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2000, 3700, 3660],
  rows: [
    bodyRow([headerCell("接口", 2000), headerCell("CR Parallel Port", 3700), headerCell("UPHY Wrapper + PCS", 3660)]),
    bodyRow([tCell("操作对象"), tCell("PHY 内部 CREG, SRAM, ROM"), tCell("UPHY Lane 控制, PCS 协议层寄存器")]),
    bodyRow([tCell("访问方式"), tCell("16-bit 地址/数据, 同步 cr_para_clk"), tCell("32-bit APB3 间接寻址 (页号+偏移)")]),
    bodyRow([tCell("使用软件"), tCell("BPMP / ATF / Bootloader"), tCell("Linux 驱动 (xpcs.c)")]),
    bodyRow([tCell("生命周期"), tCell("系统启动阶段"), tCell("运行时 (ifconfig up/down, 速率切换, ethtool)")]),
    bodyRow([tCell("安全域"), tCell("可能挂在 Secure APB (TZ 保护)"), tCell("Non-secure APB")]),
    bodyRow([tCell("典型操作"), tCell("Firmware 加载, MPLL 配置, Context 选择"), tCell("Lane Bring-up, AN 启动, FEC/EEE 配置, 速率设置")]),
  ]
});
children.push(ifaceTable);

// 6.8 Protocol differentiation
children.push(h2("6.8 多协议 SerDes：通用引擎 + 协议参数区分"));
children.push(para("32G PHY 支持 PCIe, Ethernet, SATA, JESD204C, CPRI 等多种协议。BPMP 初始化代码采用表驱动模式，不是按协议分支。"));

children.push(h3("6.8.1 通用部分（不区分协议）"));
children.push(bullet("电源排序 (VP/VPH/VPTx) — 所有协议相同"));
children.push(bullet("phy_reset 时序 (POR≥15µs, Warm≥10ns) — 所有协议相同"));
children.push(bullet("Resistor Tuning (默认 50Ω) — 所有协议相同"));
children.push(bullet("Firmware Image 加载 — 同一 image 包含所有协议"));
children.push(bullet("CR_PARA 访问时序 — 所有协议相同"));
children.push(bullet("Firmware Version 验证 (RAWCMN_DIG_AON_FW_VERSION_0/1) — 所有协议相同"));

children.push(h3("6.8.2 协议区分参数（Per-Lane Primary Inputs）"));
children.push(para("每个 Lane 独立设置下列参数。参数值来自 context_sel_ID.txt (image_gen.pl 输出) 和 Configuration Reference Manual (Excel 文件)。"));

const protoDiffTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2600, 2100, 2100, 2560],
  rows: [
    bodyRow([headerCell("参数", 2600), headerCell("Ethernet 25G AUI", 2100), headerCell("PCIe Gen4", 2100), headerCell("是否协议相关", 2560)]),
    bodyRow([tCell("txX_cntx_sel"), tCell("5 (来自 Image 生成)"), tCell("3 (来自 Image 生成)"), tCell("✅ Context 选择")]),
    bodyRow([tCell("rxX_cntx_sel"), tCell("7"), tCell("3"), tCell("✅ Context 选择")]),
    bodyRow([tCell("tx_cmnX_cntx_sel"), tCell("1"), tCell("0"), tCell("✅ 公共配置选择")]),
    bodyRow([tCell("txX_rate"), tCell("25.78125Gbps"), tCell("16.0Gbps"), tCell("✅ 速率")]),
    bodyRow([tCell("txX_width"), tCell("32/40-bit"), tCell("32-bit"), tCell("✅ 数据位宽")]),
    bodyRow([tCell("rxX_rate"), tCell("25.78125Gbps"), tCell("16.0Gbps"), tCell("✅ 速率")]),
    bodyRow([tCell("rxX_width"), tCell("32/40/64-bit"), tCell("32-bit"), tCell("✅ 数据位宽")]),
    bodyRow([tCell("rxX_div16p5_clk_en"), tCell("1 (Ethernet PCS 专用)"), tCell("0"), tCell("✅ Ethernet 特有")]),
    bodyRow([tCell("rxX_cdr_ssc_en"), tCell("0"), tCell("1 (PCIe SSC)"), tCell("✅ PCIe 特有")]),
    bodyRow([tCell("mpllX_multiplier"), tCell("取决于 RefClk×速率"), tCell("取决于 RefClk×速率"), tCell("✅ 时钟倍频")]),
    bodyRow([tCell("sram_bypass"), tCell("0"), tCell("0"), tCell("❌ 通用")]),
    bodyRow([tCell("phy_reset"), tCell("≥15µs(冷)/≥10ns(热)"), tCell("≥15µs(冷)/≥10ns(热)"), tCell("❌ 通用")]),
  ]
});
children.push(protoDiffTable);

// 6.9 Table-driven BPMP code pattern
children.push(h2("6.9 表驱动 BPMP SerDes 初始化模式"));
children.push(para("推荐实现模式 — 配置表 + 通用引擎，而非 if-else 分支："));

children.push(para("struct serdes_lane_config { protocol, tx_cntx_sel, rx_cntx_sel, tx_rate, tx_width, rx_rate, rx_width, mpll_multiplier, cdr_ssc_en, div16p5_clk_en, ... };", { italics: true }));
children.push(para("lane_configs[LANE_ETH0] = { .protocol=SERDES_PROTO_ETHERNET, .tx_cntx_sel=5, .rx_cntx_sel=7, ... };", { italics: true }));
children.push(para("lane_configs[LANE_PCIE0] = { .protocol=SERDES_PROTO_PCIE, .tx_cntx_sel=3, .cdr_ssc_en=1, ... };", { italics: true }));
children.push(para("通用引擎：bpmp_serdes_init_lane(lane_id) → set_primary_inputs(lane_id, &cfg) → assert_tx/rx_req → wait_ack;", { italics: true }));

// 6.10 Key registers for firmware
children.push(h2("6.10 Firmware 关键寄存器"));
children.push(para("以下寄存器用于 firmware 版本管理和状态验证 (§4.18, Reference Manual Registers)："));

const fwRegTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3600, 5760],
  rows: [
    bodyRow([headerCell("寄存器", 3600), headerCell("说明", 5760)]),
    bodyRow([tCell("RAWCMN_DIG_AON_FW_VERSION_0"), tCell("Firmware 版本: 16-bit a.b.c 格式 (bits[15:12]=a, [11:4]=b, [3:0]=c). 例: 0x2040 = v2.4.0")]),
    bodyRow([tCell("RAWCMN_DIG_AON_FW_VERSION_1"), tCell("编译日期: 12-bit Day/Month/Year (bits[11:7]=Day, [6:3]=Month, [2:0]=Year). 例: 0x0829 = May 16, 2019")]),
    bodyRow([tCell("RAWCMN_DIG_CONFIG_MASTER_VERSION"), tCell("Configuration Master Version — 用于调试")]),
    bodyRow([tCell("RAWCMN_DIG_AON_SRAM_EOF_ADDR"), tCell("SRAM End-of-File 地址")]),
    bodyRow([tCell("RAWCMN_DIG_AON_SRAM_BOC_ADDR"), tCell("SRAM Beginning-of-Code 地址")]),
    bodyRow([tCell("RAWCMN_DIG_MEM_POWER_SAVING.ROM_POWER_SAVING_EN"), tCell("ROM 省电使能 (需清零才能通过 CR_PARA/JTAG 读 ROM)")]),
    bodyRow([tCell("RAWCMN_DIG_LANE_FSM_OP_XTND.MEM_ADDR_EXT_EN"), tCell("SRAM 地址扩展使能 (选择 16K 空间的上下半区)")]),
  ]
});
children.push(fwRegTable);

// 6.11 Summary
children.push(h2("6.11 总结"));

const summaryTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [3200, 6160],
  rows: [
    bodyRow([headerCell("问题", 3200), headerCell("答案 (基于 Databook v3.01a 证据)", 6160)]),
    bodyRow([tCell("驱动代码包含 SerDes PHY 初始化吗？"), tCell("不包含。驱动只操作 UPHY Wrapper 和 PCS 协议层。SerDes 内部 CREG/SRAM 由 BPMP 通过 CR_PARA 管理")]),
    bodyRow([tCell("\"HW 脚本\" 是什么？"), tCell("Synopsys 提供的 IAS 寄存器编程序列 + image_gen.pl 生成的 firmware image")]),
    bodyRow([tCell("PHY Firmware 何时加载？"), tCell("在 BPMP/ATF 阶段，phy_reset 解除之前写入 SRAM。sram_bootload_bypass 决定是 PHY 自动加载还是 SoC 侧加载")]),
    bodyRow([tCell("Bootloader = U-Boot 吗？"), tCell("通常不是。T26X 中最可能是 BPMP (独立电源/时钟管理核) 或 ATF (ARM Trusted Firmware)")]),
    bodyRow([tCell("BPMP 初始化要区分协议吗？"), tCell("初始化流程(电源/时钟/复位/FW加载)通用。Per-Lane 参数(txX_cntx_sel, rate, width, MPLL)必须按协议区分。推荐表驱动")]),
    bodyRow([tCell("一份 Firmware 够吗？"), tCell("够。image_gen.pl 一次运行生成包含所有已选协议的组合 image。同一 32KB SRAM 供所有 Lane 共享")]),
    bodyRow([tCell("不同协议共用 PHY Lane 吗？"), tCell("可以。每个 Lane 通过独立的 txX_cntx_sel 选择 SRAM 中的配置。Lane 0 做 Ethernet, Lane 1 做 PCIe")]),
    bodyRow([tCell("Context Select=0x9 来源？"), tCell("image_gen.pl 根据所选协议+速率+参考时钟自动计算，输出到 context_sel_ID.txt")]),
  ]
});
children.push(summaryTable);

children.push(new PageBreak());

// ============================================================
// CHAPTER 7: Terminology Glossary
// ============================================================
children.push(h1("7. 术语表 (Glossary)"));

// 7.1 IEEE 802.3 Clause
children.push(h2("7.1 IEEE 802.3 标准条文 (Clauses)"));

const clauseTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [1200, 2400, 5760],
  rows: [
    bodyRow([headerCell("条文", 1200), headerCell("名称", 2400), headerCell("说明", 5760)]),
    bodyRow([tCell("CL22"), tCell("MII Management"), tCell("MDIO 管理接口基础规范，定义 MII 寄存器的 16-bit 地址空间与读写时序")]),
    bodyRow([tCell("CL37"), tCell("1000BASE-X AN"), tCell("1000BASE-X 自协商协议。通过发送 /C/ 有序集携带 16-bit 配置寄存器交换能力信息，用于 SGMII/QSGMII 协商速率、双工模式和链路状态")]),
    bodyRow([tCell("CL72"), tCell("10GBASE-KR Training"), tCell("10GBASE-KR PMD 训练协议。在链路建立前通过发送训练帧 (Training Frame) 自适应调整发送/接收均衡器系数 (TX FIR tap weights)，补偿背板信道损耗")]),
    bodyRow([tCell("CL73"), tCell("Backplane AN"), tCell("背板以太网自协商协议。使用差分曼彻斯特编码 (DME) 通过基页 (Base Page) 和附加页 (Next Page) 交换 FEC 能力、速度能力和训练参数")]),
    bodyRow([tCell("CL74"), tCell("BASE-R FEC"), tCell("BASE-R Forward Error Correction。使用 Fire Code (2112, 2080) 编码，可检测并纠正单个突发错误最长 11 bits，用于 KR 和 CR 模式对抗背板/铜缆噪声")]),
    bodyRow([tCell("CL91"), tCell("RS-FEC (528,514)"), tCell("Reed-Solomon FEC。使用 RS(528,514) 编码，t=7 symbol 纠错能力 (@10-bits per symbol)，用于 25GBASE-R 的强纠错场景")]),
    bodyRow([tCell("CL92"), tCell("100G Link Training"), tCell("100GBASE-KR4 / 100GBASE-CR4 链路训练。扩展 CL72 协议用于 100G 多通道场景，协调各 lane 间的训练帧和系数交换")]),
    bodyRow([tCell("CL93"), tCell("50G PAM4 Link Training"), tCell("50GBASE-KR/CR PAM4 链路训练。针对 PAM4 四电平调制扩展的训练协议，处理 PAM4 特有的 SNR 和线性度要求")]),
    bodyRow([tCell("CL108"), tCell("RS-FEC for 50G/100G"), tCell("用于 50G/100G PAM4 模式的 Reed-Solomon FEC 定义，包含 RS(544,514) 编码及其交错变体")]),
  ]
});
children.push(clauseTable);

// 7.2 PHY Architecture
children.push(h2("7.2 物理层架构与接口"));

const phyTermTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("PCS"), tCell("Physical Coding Sublayer (物理编码子层)。负责 8B/10B 或 64B/66B 线路编码、块同步 (Block Lock)、通道去偏斜 (Deskew)、自协商 (AN)、FEC 编解码。位于 MAC 和 PMA 之间")]),
    bodyRow([tCell("PMA"), tCell("Physical Medium Attachment (物理介质连接子层)。负责串行化/解串化 (SerDes)、时钟数据恢复 (CDR)、信号均衡 (EQ)。位于 PCS 和 PMD 之间")]),
    bodyRow([tCell("PMD"), tCell("Physical Medium Dependent (物理介质相关子层)。定义具体物理介质的电气/光学特性，如电压、波长、阻抗匹配。位于 PMA 和物理介质之间")]),
    bodyRow([tCell("XLGMII"), tCell("40/100 Gigabit Media Independent Interface。高带宽 MAC↔PCS 接口，支持 32-bit/64-bit/128-bit 宽度，分别对应 1.25GHz/644.53MHz/322.27MHz 时钟 (for 40G)")]),
    bodyRow([tCell("XGMII"), tCell("10 Gigabit Media Independent Interface。MAC↔PCS 接口，32-bit 数据 + 4-bit 控制，支持 SDR (156.25MHz)、DDR (78.125MHz)、DDW (312.5MHz) 三种模式")]),
    bodyRow([tCell("GMII"), tCell("Gigabit Media Independent Interface。1G MAC↔PHY 接口，8-bit 数据 @ 125MHz。xlgpcs IP 可作为独立 GMII 端口复用")]),
    bodyRow([tCell("XAUI"), tCell("10 Gigabit Attachment Unit Interface。10G 以太网 PCS↔PMA 扩展接口，4 通道 × 3.125Gbps，每通道 8B/10B 编码后的 2.5Gbps 有效载荷")]),
    bodyRow([tCell("RXAUI"), tCell("Reduced XAUI。2 通道 × 6.25Gbps，减少引脚数但每通道速率加倍，用于紧凑型背板或 SFP+ 直连")]),
    bodyRow([tCell("SGMII"), tCell("Serial Gigabit Media Independent Interface。1G 以太网串行接口，单通道 1.25Gbps (8B/10B 编码)，兼容 10/100/1000 Mbps 三速。使用 CL37 自协商传递链路状态")]),
    bodyRow([tCell("QSGMII"), tCell("Quad SGMII。单通道承载 4 端口 SGMII，通道速率为 4 × 1.25Gbps = 5Gbps。每端口独立 AN、速率和双工")]),
    bodyRow([tCell("USXGMII"), tCell("Universal Serial XGMII。单通道高速串行接口，支持 10G/5G/2.5G 三速，单端口 (SXGMII)、双端口 (DXGMII)、四端口 (QXGMII) 三种配置")]),
    bodyRow([tCell("KR / KX / KX4"), tCell("背板以太网 PHY 类型。KR=1×10.3125Gbps (10GBASE-R), KX=1×1.25Gbps (1000BASE-X), KX4=4×3.125Gbps (10GBASE-X)。均使用 64B/66B 编码，KR 需训练/FEC")]),
    bodyRow([tCell("BASE-R"), tCell("Base Rate PHY 类型。纯 64B/66B 编码的串行模式，无多通道对齐。包括 10GBASE-R (10.3125Gbps)、25GBASE-R (25.78125Gbps)、50GBASE-R 等")]),
    bodyRow([tCell("BASE-X"), tCell("Base-X PHY 类型。8B/10B 编码的通道化模式，需要 lane-to-lane deskew。包括 1000BASE-X、10GBASE-X (4 lanes)、2.5GBASE-X 等")]),
  ]
});
children.push(phyTermTable);

// 7.3 Encoding & Modulation
children.push(h2("7.3 编码与调制技术"));

const encTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("8B/10B"), tCell("8-bit 到 10-bit 块编码。每 8-bit 数据编码为 10-bit 符号，DC 平衡、确保足够跳变密度用于 CDR。开销 25%。用于 1G/10G-X/SGMII")]),
    bodyRow([tCell("64B/66B"), tCell("64-bit 到 66-bit 块编码。64-bit 数据 + 2-bit 同步头 (01=数据, 10=控制)。配合加扰器 (scrambler) 实现 DC 平衡。开销仅 3.125%。用于 10G-R/25G/40G/100G")]),
    bodyRow([tCell("256B/257B"), tCell("256-bit 到 257-bit 转码。RS-FEC 编码前的转码步骤，重新排列 64B/66B 块以适配 RS 符号边界。1 bit 开销标志块类型")]),
    bodyRow([tCell("NRZ"), tCell("Non-Return to Zero (不归零编码)。二电平调制：0=低电平，1=高电平。每符号 1 bit。用于 ≤25Gbps 的串行链路。信噪比要求较低")]),
    bodyRow([tCell("PAM4"), tCell("Pulse Amplitude Modulation 4-level (四电平脉冲幅度调制)。使用 4 个电平 (00/01/10/11)，每符号 2 bits。在相同 Baud Rate 下带宽效率是 NRZ 的 2 倍。用于 50G/100G。SNR 要求更高")]),
    bodyRow([tCell("DME"), tCell("Differential Manchester Encoding (差分曼彻斯特编码)。CL73 AN 页的编码方式。每个 bit 单元中必有跳变 (中间跳变)，1→0 表示 '1'，0→1 表示 '0'。自带时钟信号，无需独立时钟线")]),
  ]
});
children.push(encTable);

// 6.4 FEC
children.push(h2("7.4 前向纠错 (FEC)"));

const fecTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("FEC"), tCell("Forward Error Correction (前向纠错)。在发送端添加冗余校验信息，接收端利用冗余数据检测并纠正传输错误。避免重传带来的延迟，对有噪信道 (背板/铜缆) 至关重要")]),
    bodyRow([tCell("Fire Code"), tCell("CL74 定义的 FEC 编码。码字长度 n=2112 bits，数据 k=2080 bits，生成多项式 g(x)=1+x¹³+x³³。可纠正 ≤11 bits 的突发错误。编码增益约 6dB")]),
    bodyRow([tCell("RS(528,514)"), tCell("Reed-Solomon 编码，CL91 定义。码字长度 528 符号，数据 514 符号，GF(2¹⁰) 伽罗瓦域。t=7 symbol 纠错能力。以 10-bit 为一个符号，可纠正 ≤7 个错误符号。用于 25GBASE-R")]),
    bodyRow([tCell("RS(544,514)"), tCell("Reed-Solomon 编码，CL108 定义。码字长度 544 符号，数据 514 符号，GF(2¹⁰)。t=15 symbol 纠错能力。用于 50G/100G PAM4，提供更强的纠错来补偿 PAM4 低 SNR")]),
    bodyRow([tCell("Error Forwarding"), tCell("FEC 错误转发。FEC 解码器检测到不可纠错误时，将错误标记同步传递到下游 (Block Sync → MAC)，使 MAC 层丢弃受损帧而非传递错误数据。增加少量延迟和硬件开销")]),
  ]
});
children.push(fecTable);

// 6.5 UPHY & Equalization
children.push(h2("7.5 UPHY 与信号完整性"));

const uphyGlossTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("UPHY"), tCell("Unified PHY (统一 PHY 封装层)。Synopsys 提供的 PHY wrapper 抽象层，封装了 Tx/Rx Lane 控制、中断管理、Power Down 序列的寄存器接口。驱动通过 UPHY 寄存器间接控制底层 PHY")]),
    bodyRow([tCell("CDR"), tCell("Clock and Data Recovery (时钟数据恢复)。接收端从串行数据流中恢复时钟信号。PLL 锁相到输入数据的边沿跳变，用恢复的时钟采样数据。需定期 reset 以重新锁定")]),
    bodyRow([tCell("RX EQ"), tCell("Receiver Equalizer (接收均衡器)。补偿传输通道的频率相关损耗 (趋肤效应、介质损耗)。包括 CTLE (连续时间线性均衡) 和 DFE (判决反馈均衡)。需要训练找到最优系数")]),
    bodyRow([tCell("IDDQ"), tCell("Quiescent Current Drain (静态电流测试)。芯片测试中切断动态电流，仅测量静态漏电流。在 UPHY 中，IDDQ 模式 = 模拟电路完全断电，用于省电和故障隔离")]),
    bodyRow([tCell("Lane"), tCell("物理通道。一条 SerDes Tx/Rx 差分对构成一个 lane。XAUI=4 lane, RXAUI=2 lane, SGMII/USXGMII/25G=1 lane。多 lane 模式下需要 lane-to-lane deskew 对齐")]),
    bodyRow([tCell("SW Override"), tCell("软件覆盖模式。绕过硬件的 FSM (有限状态机) 自动序列，由软件按精确时序手动控制 Lane bring-up 每一步 (IDDQ→SLEEP→CAL→CDR→PHY_RDY)。在 HW FSM 不可用时使用")]),
    bodyRow([tCell("CAL_EN"), tCell("Calibration Enable (校准使能)。启动模拟电路的自动校准过程，包括终端电阻校准、偏置电压校准、VCO 频率校准。校准完成标志由硬件自动清除")]),
    bodyRow([tCell("PCS Block Lock"), tCell("PCS 块锁定。接收端在比特流中识别 66-bit 块的边界 (通过同步头 01/10 图案)。锁定前数据无效，锁定后开始正常的解扰和解码。由 RLU (Receive Link Up) 位指示")]),
  ]
});
children.push(uphyGlossTable);

// 6.6 HSI Safety
children.push(h2("7.6 HSI 安全与可靠性"));

const safetyTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("HSI"), tCell("Hardware Safety Integrity (硬件安全完整性)。驱动层的安全错误报告框架，实现 ISO 26262 要求的故障检测与报告。ISR 收集 PCS 层的奇偶校验错误、FSM 超时、接口超时等 safety events")]),
    bodyRow([tCell("ECC"), tCell("Error Correction Code (纠错码)。在内部 SRAM 中存储额外的 ECC 校验位，可检测 2-bit 错误并纠正 1-bit 错误 (SECDED)。DWC_xpcs/xlgpcs 的汽车安全包支持 ECC 保护内部数据缓冲")]),
    bodyRow([tCell("FSM"), tCell("Finite State Machine (有限状态机)。硬件中的时序控制逻辑。FSM 保护包括：①奇偶校验 (每状态一位 parity)、②超时监控 (状态机卡死的 watchdog timer)。检测到错误则触发中断")]),
    bodyRow([tCell("CE / UE"), tCell("Correctable Error / Uncorrectable Error (可纠错/不可纠错)。CE：ECC 检测到并自动纠正的 1-bit 错误 → 日志 WARN + 计数。UE：ECC 检测到但无法纠正的 2-bit 错误 → 禁用中断源 + 日志 ERR")]),
    bodyRow([tCell("CSR Protection"), tCell("Control & Status Register Protection (寄存器保护)。寄存器写入锁机制，防止关键控制寄存器被意外改写。写入前需先解锁对应的写保护位")]),
    bodyRow([tCell("Interface Timeout"), tCell("接口超时保护。监控 APB3/MCI/CR 接口的 transaction 完成时间。若 Master 未在设定时间内完成读写，触发超时中断，防止总线挂死")]),
    bodyRow([tCell("Safety Subordinate"), tCell("Safety Subordinate (安全从属模块)。DWC_xpcs 可选的安全监控模块，通过 SSM (Safety Subordinate Master) 总线独立于主控逻辑运行。提供独立的诊断寄存器访问和错误报告通道")]),
  ]
});
children.push(safetyTable);

// 6.7 Misc acronyms
children.push(h2("7.7 其他缩写与工程术语"));

const miscTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("CNS_EN"), tCell("Consortium Enable (联盟模式使能)。XLGPCS 寄存器位，使能 25G Consortium 规范的编码模式，区别于 IEEE 802.3by 标准。驱动程序 hw_set_speed 前配置")]),
    bodyRow([tCell("EEE"), tCell("Energy Efficient Ethernet (高效节能以太网)。IEEE 802.3az 标准。在低数据活动期间进入 LPI (Low Power Idle) 状态，关闭发送器但周期性发送 Refresh 信号保持链路对齐。节能 50%-90%")]),
    bodyRow([tCell("LPI"), tCell("Low Power Idle (低功耗空闲)。EEE 的省电状态。TX 路径：发送 Quiet 信号 → 定期 Refresh 训练帧 → 恢复 Active。RX 路径：检测 Quiet/Refresh → 静音 PHY → 唤醒")]),
    bodyRow([tCell("AN"), tCell("Auto-Negotiation (自协商)。两台设备在建立链路前自动协商最佳工作模式。交换的内容包括：速率能力、双工模式、FEC 能力、EEE 能力、Training 参数等")]),
    bodyRow([tCell("T26X"), tCell("Thor 2.6X 平台代号。NVIDIA 下一代 Ethernet 控制器平台，集成 MGBE MAC + Synopsys 32G Multi-protocol PHY。新增 RX EQ 训练功能，UPHY 时序寄存器与 MGBE 不同")]),
    bodyRow([tCell("MGBE"), tCell("Multi-Gigabit Ethernet (多千兆以太网 MAC)。Synopsys 的 10G/25G MAC IP (对应 driver 中的 OSI_MAC_HW_MGBE)。通过 TMCR 寄存器配置速率")]),
    bodyRow([tCell("EQOS"), tCell("Enhanced Quality-of-Service (增强 QoS MAC)。Synopsys 的 1G/2.5G MAC IP (对应 OSI_MAC_HW_EQOS)。通过 MAC_MCR 寄存器配置速率，支持 SGMII 接口")]),
    bodyRow([tCell("pre_sil"), tCell("Pre-silicon (硅前验证)。在 FPGA/uFPGA 原型平台上运行驱动，而非最终的 ASIC 芯片。pre_sil=1 时跳过 Lane bring-up (FPGA 的 PHY 配置方式不同)，并放大超时倍数 (10x)")]),
    bodyRow([tCell("DT"), tCell("Device Tree (设备树)。Linux 内核描述硬件拓扑的数据结构。驱动从 DT 读取 PHY 接口模式 (phy_iface_mode)、USXGMII 模式选择、skip_usxgmii_an 等配置")]),
    bodyRow([tCell("SLT"), tCell("System Level Test (系统级测试)。在完整系统环境中测试驱动功能，区别于单元测试/仿真。注释中的 'Not used for SLT EQOS bringup' 指生产系统中已禁用该路径")]),
    bodyRow([tCell("safety write"), tCell("安全写入 (写后读回验证)。调用 xpcs_write_safety() 而非 xpcs_write()，写入后立即读取同一寄存器并比较值是否一致。失败则重试 1 次 (udelay → usleep)")]),
    bodyRow([tCell("self-clearing"), tCell("自清除位。硬件在操作完成后自动清除该位，软件只需 poll 该位直到变为 0。比读取状态寄存器的延迟更短。典型例子：VR_RST, RX_CAL_EN, RX_EQ_RESET")]),
  ]
});
children.push(miscTable);

// 7.8 SerDes & Bootflow terms
children.push(h2("7.8 SerDes 初始化与启动流术语"));

const serdesTermTable = new Table({
  width: { size: 9360, type: WidthType.DXA },
  columnWidths: [2200, 7160],
  rows: [
    bodyRow([headerCell("术语", 2200), headerCell("说明", 7160)]),
    bodyRow([tCell("BPMP"), tCell("Boot and Power Management Processor (启动与电源管理处理器)。NVIDIA SoC 中的专用管理核心，独立于主 CPU 运行。负责系统级时钟树建立、电源排序、PHY 初始化等底层启动任务。T26X 中 SerDes firmware 加载最可能在 BPMP 中完成")]),
    bodyRow([tCell("ATF / BL31"), tCell("ARM Trusted Firmware (BL31 阶段)。实现 EL3 (Secure Monitor) 运行时，管理系统安全状态切换 (TrustZone)。如果 CR_PARA 挂在安全总线，只有 ATF 有权访问 PHY 寄存器")]),
    bodyRow([tCell("CR_PARA"), tCell("Control Register Parallel Interface (控制寄存器并行接口)。SoC 访问 PHY 内部 CREG 和 SRAM 的硬件接口，16-bit 地址+数据。使用 cr_para_clk (≤125MHz)，通过 cr_para_sel 与 JTAG 切换。是 BPMP 加载 firmware 和访问 PHY 配置寄存器的主要途径")]),
    bodyRow([tCell("CREG"), tCell("Control Register (PHY 内部控制寄存器)。区别于驱动侧的 PCS 寄存器，CREG 是 PHY Macro 内部的配置/状态寄存器，包括 MPLL 配置、校准数据、TX EQ 系数、RX Adaptation 参数等。通过 CR_PARA 或 JTAG 访问")]),
    bodyRow([tCell("Primary Inputs"), tCell("PHY 顶层硬件配置引脚。在 phy_reset 解除前设置为目标值，由 PHY 内部在初始化序列中采样。包括 txX_rate/width、rxX_rate/width、cntx_sel、MPLL 参数等。可使用 Context Restore 替代")]),
    bodyRow([tCell("Context Restore"), tCell("PHY 内部自动化配置恢复机制。Raw PCS 中的状态机从 SRAM/ROM 读取预存 CREG 数据，自动写入所有 Lane 的配置寄存器。Context Select 值 (如 0x9) 索引不同的配置集。可移除大量 Primary Inputs 引脚")]),
    bodyRow([tCell("SRAM Bootloading"), tCell("PHY 内部 bootloader 在上电后将 firmware code 从 ROM 拷贝到外部 SRAM 的过程。完成后断言 sram_init_done。可通过 sram_bootload_bypass=1 让 SoC 跳过此步骤，自行侧加载")]),
    bodyRow([tCell("image_gen.pl"), tCell("Synopsys 提供的 Perl 脚本，根据协议选择 (protocol_input.txt)、速率、参考时钟频率，生成包含 firmware code + Context Restore 配置的组合 ROM/SRAM image。输出 context_sel_ID.txt 列出有效的 cntx_sel 值")]),
    bodyRow([tCell("Firmware Clock"), tCell("PHY 执行任务 (SRAM bootload, POR 校准, 电源门控退出, 速率切换, adaptation) 时请求的专用时钟 (100-175MHz)。fw_clk_req/ack/fw_clk 三信号握手机制。内部 GLCM 复用器在内部参考时钟和 fw_clk 之间选择")]),
    bodyRow([tCell("GLCM"), tCell("Glitch-Less Free Multiplexer (无毛刺时钟复用器)。PHY 内部使用的时钟源切换电路，确保 cr_int_clk 在 ref_clk 与 fw_clk 之间切换时不产生毛刺脉冲")]),
    bodyRow([tCell("IAS"), tCell("Implementation Application Script (实现应用脚本)。Synopsys 随 IP 交付的寄存器编程序列文档，描述从 POR 到链路建立的完整步骤。HW 验证团队在 FPGA 上跑通后，驱动团队翻译为 C 代码")]),
  ]
});
children.push(serdesTermTable);

children.push(new PageBreak());

// ============================================================
// BUILD
// ============================================================

const doc = new Document({
  styles: {
    default: { document: { run: { font: FONT, size: 20 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 34, bold: true, font: FONT, color: "1F4E79" },
        paragraph: { spacing: { before: 360, after: 200 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: FONT, color: "2E75B6" },
        paragraph: { spacing: { before: 240, after: 160 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, font: FONT, color: "404040" },
        paragraph: { spacing: { before: 180, after: 120 }, outlineLevel: 2 } },
    ]
  },
  numbering: { config: [bulletConfig] },
  sections: [{
    properties: {
      page: {
        size: { width: 12240, height: 15840 }, // US Letter
        margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
      }
    },
    headers: {
      default: new Header({
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: "1F4E79", space: 1 } },
          children: [new TextRun({ text: "XPCS/XLGPCS 驱动代码分析", font: FONT, size: 16, color: "888888", italics: true })]
        })]
      })
    },
    footers: {
      default: new Footer({
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          border: { top: { style: BorderStyle.SINGLE, size: 4, color: "CCCCCC", space: 1 } },
          children: [
            new TextRun({ text: "Page ", font: FONT, size: 16, color: "888888" }),
            new TextRun({ children: [PageNumber.CURRENT], font: FONT, size: 16, color: "888888" })
          ]
        })]
      })
    },
    children,
  }]
});

Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync("/home/nova_zhang/omni/omnieth/docs/PCS驱动代码分析.docx", buffer);
  console.log("Document written successfully: " + buffer.length + " bytes");
});
