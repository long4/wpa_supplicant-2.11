const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType,
  ShadingType, PageNumber, PageBreak, TableOfContents, LevelFormat,
  ImageRun
} = require("docx");

// ── helpers ──────────────────────────────────────────────────────────
const border = { style: BorderStyle.SINGLE, size: 1, color: "999999" };
const borders = { top: border, bottom: border, left: border, right: border };
const thBorder = { style: BorderStyle.SINGLE, size: 1, color: "2E75B6" };
const thBorders = { top: thBorder, bottom: thBorder, left: thBorder, right: thBorder };
const cellMargins = { top: 60, bottom: 60, left: 100, right: 100 };
const thShading = { fill: "D5E8F0", type: ShadingType.CLEAR };
const tblWidth9360 = { size: 9360, type: WidthType.DXA };

function heading(level, text) {
  const h = [HeadingLevel.HEADING_1, HeadingLevel.HEADING_2, HeadingLevel.HEADING_3][level - 1];
  return new Paragraph({ heading: h, children: [new TextRun(text)] });
}

function para(text, opts = {}) {
  const runs = [];
  // simple inline code detection: `...`
  const parts = text.split(/(`[^`]+`)/g);
  for (const p of parts) {
    if (p.startsWith("`") && p.endsWith("`")) {
      runs.push(new TextRun({ text: p.slice(1, -1), font: "Courier New", size: 20, shading: { fill: "F2F2F2", type: ShadingType.CLEAR } }));
    } else {
      let t = p;
      if (opts.bold) runs.push(new TextRun({ text: t, bold: true, font: "Arial", size: 20 }));
      else runs.push(new TextRun({ text: t, font: "Arial", size: 20 }));
    }
  }
  return new Paragraph({ spacing: { after: 120 }, children: runs });
}

function boldPara(text) {
  return new Paragraph({ spacing: { after: 120 }, children: [new TextRun({ text, bold: true, font: "Arial", size: 20 })] });
}

function codeBlock(lines) {
  const children = [];
  const text = Array.isArray(lines) ? lines.join("\n") : lines;
  children.push(new TextRun({ text, font: "Courier New", size: 18 }));
  return new Paragraph({
    spacing: { before: 80, after: 80 },
    shading: { fill: "F5F5F5", type: ShadingType.CLEAR },
    indent: { left: 360 },
    children
  });
}

function tableCell(text, opts = {}) {
  const isHeader = opts.header || false;
  const font = opts.font || (opts.code ? "Courier New" : "Arial");
  const size = opts.size || (isHeader ? 20 : 20);
  const cBorders = isHeader ? thBorders : borders;
  const shading = isHeader ? thShading : undefined;
  const width = opts.width ? { size: opts.width, type: WidthType.DXA } : undefined;
  return new TableCell({
    borders: cBorders,
    width,
    shading,
    margins: cellMargins,
    verticalAlign: "center",
    children: [new Paragraph({
      spacing: { before: 40, after: 40 },
      children: [new TextRun({ text, font, size, bold: isHeader })]
    })]
  });
}

function makeTable(headers, rows, colWidths) {
  const totalW = colWidths.reduce((a, b) => a + b, 0);
  const headerRow = new TableRow({ children: headers.map((h, i) => tableCell(h, { header: true, width: colWidths[i] })) });
  const dataRows = rows.map(row =>
    new TableRow({ children: row.map((cell, i) => tableCell(String(cell), { code: i === 0, width: colWidths[i] })) })
  );
  return new Table({
    width: { size: totalW, type: WidthType.DXA },
    columnWidths: colWidths,
    rows: [headerRow, ...dataRows]
  });
}

// ── build document ───────────────────────────────────────────────────
const doc = new Document({
  styles: {
    default: { document: { run: { font: "Arial", size: 24 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 36, bold: true, font: "Arial", color: "1A5276" },
        paragraph: { spacing: { before: 360, after: 200 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 30, bold: true, font: "Arial", color: "2E4053" },
        paragraph: { spacing: { before: 280, after: 160 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 26, bold: true, font: "Arial", color: "34495E" },
        paragraph: { spacing: { before: 200, after: 120 }, outlineLevel: 2 } },
    ]
  },
  sections: [
    // ── Cover page ──
    {
      properties: {
        page: {
          size: { width: 12240, height: 15840 },
          margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
        }
      },
      children: [
        new Paragraph({ spacing: { before: 3600 } }),
        new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "OmniEth 网卡驱动架构分析", font: "Arial", size: 52, bold: true, color: "1A5276" })] }),
        new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 400 }, children: [new TextRun({ text: "描述符结构 · NAPI机制 · 多队列 · 多DMA Channel调用关系", font: "Arial", size: 26, color: "5D6D7E" })] }),
        new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 800 }, children: [new TextRun({ text: "Synopsys DWC Ethernet Controller Driver", font: "Arial", size: 22, color: "888888" })] }),
        new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200 }, children: [new TextRun({ text: "omnieth.ko (CONFIG_OMNI_APB_EMAC)", font: "Courier New", size: 22, color: "888888" })] }),
        new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 1600 }, children: [new TextRun({ text: "2026-05-20", font: "Arial", size: 22, color: "AAAAAA" })] }),
      ]
    },
    // ── TOC page ──
    {
      properties: {
        page: {
          size: { width: 12240, height: 15840 },
          margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
        }
      },
      headers: {
        default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT, children: [new TextRun({ text: "OmniEth 驱动架构分析", font: "Arial", size: 18, color: "999999" })], border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: "CCCCCC", space: 1 } } })] })
      },
      footers: {
        default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "Page ", font: "Arial", size: 18 }), new TextRun({ children: [PageNumber.CURRENT], font: "Arial", size: 18 })] })] })
      },
      children: [
        new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 400 }, children: [new TextRun({ text: "目  录", font: "Arial", size: 36, bold: true, color: "1A5276" })] }),
        new Paragraph({ spacing: { after: 400 } }),
        new TableOfContents("Table of Contents", { hyperlink: true, headingStyleRange: "1-3" }),
      ]
    },
    // ── Content ──
    {
      properties: {
        page: {
          size: { width: 12240, height: 15840 },
          margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
        }
      },
      headers: {
        default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT, children: [new TextRun({ text: "OmniEth 驱动架构分析", font: "Arial", size: 18, color: "999999" })], border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: "CCCCCC", space: 1 } } })] })
      },
      footers: {
        default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "Page ", font: "Arial", size: 18 }), new TextRun({ children: [PageNumber.CURRENT], font: "Arial", size: 18 })] })] })
      },
      children: [

        // ═══ Section 1 ═══
        heading(1, "一、描述符结构层次"),
        para("整个描述符体系分为三层嵌套结构，从外到内:"),
        codeBlock(
          "osi_dma_priv_data                     ← DMA引擎顶层 (每个netdev一个)\n" +
          "  ├─ tx_ring[0..N-1]                  ← 每个DMA channel有独立的TX ring\n" +
          "  │   ├─ tx_desc[0..ring_sz-1]        ← 硬件描述符环形队列 (4个u32字段)\n" +
          "  │   ├─ tx_swcx[0..ring_sz-1]        ← 软件上下文 (DMA映射地址/长度/flags)\n" +
          "  │   ├─ tx_pkt_cx                    ← 本次发送的包上下文 (VLAN/TSO/PTP标志)\n" +
          "  │   ├─ txdone_pkt_cx                ← 发送完成上下文 (回传给OSD层)\n" +
          "  │   ├─ cur_tx_idx                   ← 生产者指针 (发送时递增)\n" +
          "  │   └─ clean_idx                    ← 消费者指针 (完成中断时回收)\n" +
          "  │\n" +
          "  └─ rx_ring[0..N-1]                  ← 每个DMA channel有独立的RX ring\n" +
          "      ├─ rx_desc[0..ring_sz-1]        ← 硬件描述符环形队列\n" +
          "      ├─ rx_swcx[0..ring_sz-1]        ← 软件上下文 (含buf_phy_addr/buf_virt_addr)\n" +
          "      ├─ rx_pkt_cx                    ← 接收包上下文 (csum/VLAN/hash/PTP)\n" +
          "      ├─ cur_rx_idx                   ← 当前处理指针\n" +
          "      └─ refill_idx                   ← 补充buffer指针"
        ),

        heading(3, "关键数据结构"),
        makeTable(
          ["结构体", "所在文件", "作用"],
          [
            ["osi_tx_desc / osi_rx_desc", "hardware/include/osi_dma.h", "4×u32 硬件描述符，DMA-mapped，HW通过OWN bit仲裁"],
            ["osi_tx_swcx / osi_rx_swcx", "hardware/include/osi_dma.h", "软件上下文：保存DMA映射的物理/虚拟地址、buffer长度"],
            ["osi_tx_pkt_cx", "hardware/include/osi_dma.h", "单包发送上下文：VLAN tag、TSO MSS、PTP标志 (每ring复用)"],
            ["osi_tx_ring / osi_rx_ring", "hardware/include/osi_dma.h", "每个DMA channel独立的环形队列"],
            ["osi_rx_pkt_cx", "hardware/include/osi_dma.h", "接收包上下文：csum/VLAN/hash/PTP时间戳"],
            ["osi_txdone_pkt_cx", "hardware/include/osi_dma.h", "发送完成上下文：错误标志、PTP时间戳、pktid"],
            ["osi_dma_priv_data", "hardware/include/osi_dma.h", "DMA引擎顶层私有数据：持有所有tx_ring/rx_ring指针数组"],
          ],
          [2600, 3000, 3760]
        ),
        para(""),

        heading(3, "Ring大小"),
        makeTable(
          ["MAC类型", "TX Ring大小", "RX Ring大小"],
          [
            ["EQOS", "1024", "1024"],
            ["MGBE", "4096", "最大16384 (当前代码使用 rx_ring_sz)"],
          ],
          [3120, 3120, 3120]
        ),

        // ═══ Section 2 ═══
        heading(1, "二、多DMA Channel架构"),

        heading(2, "2.1 Channel数量来源"),
        para("Channel数量由 Device Tree 配置，在probe阶段读取:"),
        codeBlock(
          "DT: nvidia,num_dma_chans = <N>   →  osi_dma->num_dma_chans\n" +
          "DT: nvidia,dma_chans = <0, 1, 2, 3>  →  osi_dma->dma_chans[]"
        ),
        para("MGBE最多支持 10个DMA channel (OSI_MGBE_MAX_NUM_CHANS)，EQOS最多4个。"),

        heading(2, "2.2 资源分配"),
        para("代码路径 (ether_linux.c:2670-2697):"),
        codeBlock(
          "ether_allocate_tx_dma_resources(osi_dma, dev):\n" +
          "    for each dma_chans[i] that is valid:\n" +
          "        allocate_tx_dma_resource(osi_dma, dev, chan):\n" +
          "            osi_dma->tx_ring[chan] = kzalloc(osi_tx_ring)\n" +
          "            tx_ring->tx_desc = dma_alloc_coherent(tx_desc_size, &phy_addr)  // 硬件描述符\n" +
          "            tx_ring->tx_swcx = kzalloc(tx_swcx_size)                        // 软件上下文"
        ),

        heading(2, "2.3 PDMA/VDMA 两级映射"),
        para("在 Tegra T26x 平台上，存在 PDMA (Physical DMA) → VDMA (Virtual DMA) 两级映射 (osi_core.h):"),
        codeBlock(
          "struct osi_pdma_vdma_data {\n" +
          "    u32 pdma_chan;           // 物理DMA通道号\n" +
          "    u32 num_vdma_chans;      // 该PDMA下有多少VDMA\n" +
          "    u32 vdma_chans[...];     // VDMA通道列表\n" +
          "};"
        ),
        para("非虚拟化：每个DMA channel = 一个 PDMA"),
        para("虚拟化(VM)：一个PDMA可以分出多个VDMA，不同VM分到不同的VDMA子集"),

        // ═══ Section 3 ═══
        heading(1, "三、NAPI机制与队列绑定"),

        heading(2, "3.1 NAPI实例化"),
        para("每个DMA channel 分配独立的 TX NAPI 和 RX NAPI 实例 (ether_linux.c:4945-4994):"),
        codeBlock(
          "for each DMA channel {\n" +
          "    pdata->tx_napi[chan]  = 新的 ether_tx_napi {\n" +
          "        .chan   = chan,\n" +
          "        .pdata  = pdata,\n" +
          "        .napi   = netif_napi_add(ndev, &napi, ether_napi_poll_tx),\n" +
          "        .tx_usecs_timer    // TX软件定时器 (合并中断用)\n" +
          "    };\n" +
          "\n" +
          "    pdata->rx_napi[chan]  = 新的 ether_rx_napi {\n" +
          "        .chan   = chan,\n" +
          "        .pdata  = pdata,\n" +
          "        .napi   = netif_napi_add(ndev, &napi, ether_napi_poll_rx),\n" +
          "    };\n" +
          "}"
        ),

        para("数据结构定义 (ether_linux.h:373-396):"),
        codeBlock(
          "struct ether_tx_napi {\n" +
          "    unsigned int chan;                 // DMA channel号\n" +
          "    struct ether_priv_data *pdata;      // 回指私有数据\n" +
          "    struct napi_struct napi;            // 内核NAPI实例\n" +
          "    struct hrtimer tx_usecs_timer;      // TX SW定时器 (合并中断)\n" +
          "    atomic_t tx_usecs_timer_armed;      // 定时器是否已启动\n" +
          "};\n" +
          "\n" +
          "struct ether_rx_napi {\n" +
          "    unsigned int chan;\n" +
          "    struct ether_priv_data *pdata;\n" +
          "    struct napi_struct napi;\n" +
          "};"
        ),

        heading(2, "3.2 NAPI Poll 函数详解"),

        boldPara("RX Poll (ether_linux.c:4842-4865):"),
        codeBlock(
          "ether_napi_poll_rx(napi, budget):\n" +
          "    rx_napi = container_of(napi, ether_rx_napi)  // 从napi找回channel信息\n" +
          "    received = osi_process_rx_completions(osi_dma, chan, budget, &more)\n" +
          "    // osi_process_rx_completions 内部 (osi_dma_txrx.c:304):\n" +
          "    //   遍历 rx_ring[chan]->rx_desc[], 从 cur_rx_idx 开始\n" +
          "    //   检查 RDES3_OWN bit — 如果OWN=1说明HW还在处理, break\n" +
          "    //   解析 RDES3 字段:\n" +
          "    //     - pkt_len: 包长度\n" +
          "    //     - ES bits: 错误状态\n" +
          "    //     - FD/LD: first/last descriptor\n" +
          "    //   调用 process_rx_desc():\n" +
          "    //     - d_ops[mac].get_rx_csum()     → 硬件校验和结果\n" +
          "    //     - d_ops[mac].get_rx_vlan()     → VLAN tag\n" +
          "    //     - d_ops[mac].get_rx_hwstamp()  → PTP时间戳\n" +
          "    //     - osi_dma->osd_ops.receive_packet() → osd_receive_packet()\n" +
          "    //       → page_pool? → napi_gro_receive()\n" +
          "    //       → 否则: netif_receive_skb()\n" +
          "\n" +
          "    if received < budget:\n" +
          "        napi_complete(napi)           // NAPI结束\n" +
          "        osi_handle_dma_intr(ENABLE)   // 重新使能硬件中断"
        ),

        boldPara("TX Poll (ether_linux.c:4880-4912):"),
        codeBlock(
          "ether_napi_poll_tx(napi, budget):\n" +
          "    tx_napi = container_of(napi, ether_tx_napi)  // 从napi找回channel信息\n" +
          "    processed = osi_process_tx_completions(osi_dma, chan, budget)\n" +
          "    // osi_process_tx_completions 内部 (osi_dma_txrx.c:710):\n" +
          "    //   从 tx_ring[chan]->clean_idx 开始遍历到 cur_tx_idx\n" +
          "    //   检查 TDES3_OWN bit — 如果OWN=1说明HW还没处理完, break\n" +
          "    //   检查 TDES3_LD (last descriptor):\n" +
          "    //     - 如果是最后一个描述符 → 触发 transmit_complete 回调\n" +
          "    //     - 检查 TDES3_ES_BITS → 发送错误统计\n" +
          "    //   处理PTP时间戳:\n" +
          "    //     - EQOS: 从 TDES0/TDES1 直接读取\n" +
          "    //     - MGBE: 标记 OSI_TXDONE_CX_TS_DELAYED (延迟获取)\n" +
          "    //   osi_dma->osd_ops.transmit_complete() = osd_transmit_complete()\n" +
          "    //     → dev_kfree_skb_any(skb)  // 释放SKB\n" +
          "    //   清零描述符, INCR_TX_DESC_INDEX(entry)\n" +
          "    //   tx_ring->clean_idx = entry  // 每步都更新clean_idx\n" +
          "\n" +
          "    if !ring_empty && use_tx_usecs:\n" +
          "        hrtimer_start(tx_usecs_timer)  // 启动SW定时器继续轮询\n" +
          "    if processed < budget:\n" +
          "        napi_complete(napi)\n" +
          "        osi_handle_dma_intr(ENABLE)"
        ),

        heading(2, "3.3 中断处理与NAPI调度"),
        para("两种中断模式:"),

        boldPara("1. 非VM模式 — 每channel独立的IRQ line:"),
        codeBlock(
          "IRQ → ether_tx_chan_isr(irq, tx_napi_data)   [ether_linux.c:1741]\n" +
          "    → raw_spin_lock(rlock)\n" +
          "    → osi_handle_dma_intr(chan, TX, DISABLE)  // 先关中断\n" +
          "    → raw_spin_unlock(rlock)\n" +
          "    → __napi_schedule_irqoff(&tx_napi->napi)  // 调度NAPI (软中断上下文)\n" +
          "    // 注意: 中断已关闭，防止重入"
        ),
        codeBlock(
          "IRQ → ether_rx_chan_isr(irq, rx_napi_data)   [ether_linux.c:1788]\n" +
          "    → raw_spin_lock(rlock)\n" +
          "    → osi_handle_dma_intr(chan, RX, DISABLE)\n" +
          "    → raw_spin_unlock(rlock)\n" +
          "    → __napi_schedule_irqoff(&rx_napi->napi)"
        ),

        boldPara("2. VM模式 — 共享VM IRQ，从Global DMA Status寄存器解析:"),
        codeBlock(
          "IRQ → ether_vm_isr(irq, vm_irq_data)          [ether_linux.c:1665]\n" +
          "    → osi_get_global_dma_status(osi_dma, dma_status[3])\n" +
          "    → dma_status[i] &= vm_irq->chan_mask[i]   // 只关心本VM的channel\n" +
          "    → while dma_status[i] != 0:\n" +
          "        temp = ffs(dma_status[i]) - 1\n" +
          "        chan = (temp >> 1) + (16 * i)         // bit位→channel号\n" +
          "        txrx = temp & 1                        // 0=TX, 1=RX\n" +
          "        if txrx:\n" +
          "            osi_handle_dma_intr(chan, RX, DISABLE)\n" +
          "            __napi_schedule_irqoff(&rx_napi[chan]->napi)\n" +
          "        else:\n" +
          "            osi_handle_dma_intr(chan, TX, DISABLE)\n" +
          "            __napi_schedule_irqoff(&tx_napi[chan]->napi)\n" +
          "        dma_status[i] &= ~BIT(temp)"
        ),

        heading(2, "3.4 TX SW定时器 (合并中断优化)"),
        para("当 use_tx_usecs == ENABLE 时，硬件发送完成不会立即触发中断，而是通过SW定时器延迟回收:"),
        codeBlock(
          "// 在 ether_start_xmit 中，每次发包后:\n" +
          "if (osi_dma->use_tx_usecs == OSI_ENABLE &&\n" +
          "    !tx_usecs_timer_armed) {\n" +
          "    atomic_set(&tx_napi->tx_usecs_timer_armed, OSI_ENABLE);\n" +
          "    hrtimer_start(&tx_napi->tx_usecs_timer, tx_usecs * NSEC_PER_USEC);\n" +
          "}\n" +
          "\n" +
          "// 定时器回调:\n" +
          "ether_tx_usecs_hrtimer(timer):\n" +
          "    tx_napi = container_of(timer, ether_tx_napi, tx_usecs_timer)\n" +
          "    atomic_set(&tx_napi->tx_usecs_timer_armed, OSI_DISABLE)\n" +
          "    __napi_schedule_irqoff(&tx_napi->napi)  // 软中断中回收完成描述符"
        ),

        // ═══ Section 4 ═══
        heading(1, "四、多队列(TX Queue)与DMA Channel映射"),

        heading(2, "4.1 Linux netdev队列数量"),
        codeBlock(
          "// probe中 (ether_linux.c:7522):\n" +
          "ndev = alloc_etherdev_mq(sizeof(ether_priv_data), num_dma_chans);\n" +
          "// num_dma_chans = osi_dma->num_dma_chans\n" +
          "// 这告诉内核: 此设备有 num_dma_chans 个TX队列"
        ),

        heading(2, "4.2 队列选择: ndo_select_queue"),
        codeBlock(
          "ether_select_queue(dev, skb)    [ether_linux.c:3968]:\n" +
          "    priority = skb->priority\n" +
          "    for each dma_chans[i]:\n" +
          "        mtlq = osi_core->dma_chans[i]              // MTL队列号\n" +
          "        if pdata->txq_prio[mtlq] == priority:      // 匹配优先级\n" +
          "            return i                                 // ← 返回队列索引\n" +
          "    // 内核用返回值设置 skb->queue_mapping"
        ),

        heading(2, "4.3 发送: 队列 → DMA channel"),
        codeBlock(
          "ether_start_xmit(skb, ndev)                        [ether_linux.c:3997]:\n" +
          "    qinx  = skb_get_queue_mapping(skb)             // 内核选择的队列号\n" +
          "    chan  = osi_dma->dma_chans[qinx]               // 队列号 → DMA channel号\n" +
          "    tx_ring = osi_dma->tx_ring[chan]               // DMA channel → TX ring\n" +
          "\n" +
          "    ether_tx_swcx_alloc(pdata, tx_ring, skb)       // 填充 tx_swcx[] 数组\n" +
          "    //   (映射 SKB 的 frags 到多个描述符)\n" +
          "    //   (每个描述符 ≤ 16KB = OSI_TX_MAX_BUFF_SIZE)\n" +
          "    osi_hw_transmit(osi_dma, chan)                  // 写描述符 → 更新 tail pointer\n" +
          "\n" +
          "    if ether_avail_txdesc_cnt(tx_ring) <= THRESHOLD:\n" +
          "        netif_stop_subqueue(ndev, qinx)             // 停止该子队列"
        ),
        para("关键: dma_chans[i] 数组是队列索引到DMA channel号的映射表，由DT配置决定。"),

        heading(2, "4.4 RSS多队列"),
        codeBlock(
          "ether_init_rss(pdata, features)                    [ether_linux.c:3095]:\n" +
          "    num_q = osi_core->num_mtl_queues               // MTL队列数\n" +
          "    if T26x MGBE: num_q = num_dma_chans\n" +
          "    for each RSS table entry (128 entries):\n" +
          "        rss->table[i] = ethtool_rxfh_indir_default(i, num_q)\n" +
          "    // HW根据RSS hash将数据包分发到不同RX DMA channel"
        ),

        // ═══ Section 5 ═══
        heading(1, "五、TX/RX 软硬件时序图"),

        heading(2, "TX 发送路径"),
        new Paragraph({
          spacing: { before: 120, after: 120 },
          children: [new ImageRun({
            type: "png",
            data: fs.readFileSync("tx_sequence.png"),
            transformation: { width: 880, height: 720 },
            altText: { title: "TX Sequence", description: "TX发送路径时序图", name: "tx_seq" }
          })]
        }),

        new Paragraph({ children: [new PageBreak()] }),

        heading(2, "RX 接收路径"),
        new Paragraph({
          spacing: { before: 120, after: 120 },
          children: [new ImageRun({
            type: "png",
            data: fs.readFileSync("rx_sequence.png"),
            transformation: { width: 880, height: 680 },
            altText: { title: "RX Sequence", description: "RX接收路径时序图", name: "rx_seq" }
          })]
        }),

        // ═══ Section 6 ═══
        heading(1, "六、完整调用链"),

        heading(2, "TX Path (发送路径)"),
        codeBlock(
          "应用层 send()\n" +
          "  → 内核协议栈\n" +
          "    → ndo_start_xmit = ether_start_xmit()               [ether_linux.c:3997]\n" +
          "      → skb_get_queue_mapping(skb) → qinx\n" +
          "      → chan = osi_dma->dma_chans[qinx]                  // 队列→channel映射\n" +
          "      → tx_ring = osi_dma->tx_ring[chan]\n" +
          "      → ether_tx_swcx_alloc(pdata, tx_ring, skb)        // 填充tx_swcx[N]\n" +
          "          // TSO: 分配多个描述符 (每段≤16KB)\n" +
          "          // PTP: 标记 OSI_PKT_CX_PTP\n" +
          "          // VLAN: 标记 OSI_PKT_CX_VLAN\n" +
          "      → osi_hw_transmit(osi_dma, chan)                   [osi_dma_txrx.c:1086→1212]\n" +
          "        → hw_transmit(osi_dma, tx_ring, chan):\n" +
          "          → need_cntx_desc():                  // 是否需要VLAN/TSO/PTP上下文描述符\n" +
          "          → fill_first_desc():                 // 填充第一描述符\n" +
          "          → for each remaining desc:           // 填充中间描述符\n" +
          "              tx_desc->tdes3 |= TDES3_OWN       // 中间描述符先设OWN\n" +
          "          → last_desc->tdes3 |= TDES3_LD       // 最后描述符\n" +
          "          → set_swcx_pkt_id_for_ptp()          // PTP包ID\n" +
          "          → set_clear_ioc_for_last_desc()      // 中断合并 IOC/frames/descs\n" +
          "          → first_desc->tdes3 |= TDES3_OWN     // ★最后first OWN (避免race)\n" +
          "          → set_context_desc_own_bit()          // ★最后context OWN\n" +
          "          → dmb_oshst()                         // ★内存屏障\n" +
          "          → tx_ring->cur_tx_idx = entry         // 更新生产者指针\n" +
          "          → osi_dma_writel(tailptr, TDTP寄存器) // ★写Tail Pointer → HW开始DMA\n" +
          "\n" +
          "  // === HW异步DMA完成 — 硬件中断 ===\n" +
          "  HW DMA → IRQ\n" +
          "    → ether_tx_chan_isr(irq, tx_napi_data)     [ether_linux.c:1741]\n" +
          "      → osi_handle_dma_intr(chan, TX, DISABLE)  // 关TX中断\n" +
          "      → __napi_schedule_irqoff(&tx_napi->napi)  // 调度NAPI\n" +
          "\n" +
          "  // === 软中断上下文 (NET_RX_SOFTIRQ) ===\n" +
          "  → ether_napi_poll_tx(napi, budget)            [ether_linux.c:4880]\n" +
          "    → osi_process_tx_completions(osi_dma, chan, budget)  [osi_dma_txrx.c:710]\n" +
          "      → for entry = clean_idx .. cur_tx_idx:\n" +
          "          → if TDES3_OWN: break                  // HW还在处理\n" +
          "          → process_last_desc()                   // 检查LD + 错误统计\n" +
          "          → osi_dma->osd_ops.transmit_complete()  // = osd_transmit_complete()\n" +
          "            → dev_kfree_skb_any(skb)              // 释放SKB\n" +
          "          → INCR_TX_DESC_INDEX(entry)             // clean_idx前进\n" +
          "          → tx_ring->clean_idx = entry            // ★立即更新clean_idx\n" +
          "      → if ring不为空 && use_tx_usecs:\n" +
          "          hrtimer_start(tx_usecs_timer)           // SW定时器延迟回收\n" +
          "      → if processed < budget:\n" +
          "          napi_complete(napi)                     // NAPI结束\n" +
          "          osi_handle_dma_intr(chan, TX, ENABLE)   // 重新使能中断"
        ),

        heading(2, "RX Path (接收路径)"),
        codeBlock(
          "  // === HW写入RX描述符 ===\n" +
          "  HW DMA → 写入数据到 rx_desc[] → 设置 RDES3_OWN=0 (表示SW拥有)\n" +
          "\n" +
          "  // === 硬件中断 ===\n" +
          "  HW → IRQ\n" +
          "    → ether_rx_chan_isr(irq, rx_napi_data)      [ether_linux.c:1788]\n" +
          "      → osi_handle_dma_intr(chan, RX, DISABLE)   // 关RX中断\n" +
          "      → __napi_schedule_irqoff(&rx_napi->napi)   // 调度NAPI\n" +
          "\n" +
          "  // === 软中断上下文 ===\n" +
          "  → ether_napi_poll_rx(napi, budget)             [ether_linux.c:4842]\n" +
          "    → osi_process_rx_completions(osi_dma, chan, budget)  [osi_dma_txrx.c:304]\n" +
          "      → while received < budget:\n" +
          "          → if RDES3_OWN: break                   // HW拥有，没数据了\n" +
          "          → INCR_RX_DESC_INDEX(cur_rx_idx)\n" +
          "          → pkt_len = rx_desc->rdes3 & PKT_LEN    // 获取包长度\n" +
          "          → flags |= OSI_PKT_CX_VALID\n" +
          "          → process_rx_desc():                    // 解析csum/VLAN/hash/PTP\n" +
          "            → d_ops[mac].get_rx_csum()\n" +
          "            → d_ops[mac].get_rx_vlan()\n" +
          "            → d_ops[mac].get_rx_hwstamp()\n" +
          "            → osi_dma->osd_ops.receive_packet()   // = osd_receive_packet()\n" +
          "              → page_pool? → napi_gro_receive()\n" +
          "              → 否则: netif_receive_skb()\n" +
          "      → if received < budget:\n" +
          "          napi_complete(napi)\n" +
          "          osi_handle_dma_intr(chan, RX, ENABLE)"
        ),

        // ═══ Section 7 ═══
        heading(1, "七、描述符字段详解"),

        heading(2, "TX描述符 (struct osi_tx_desc)"),
        makeTable(
          ["字段", "位", "含义"],
          [
            ["tdes0", "[31:0]", "Buffer地址低32位"],
            ["tdes1", "[31:0]", "Buffer地址高32位"],
            ["tdes2", "[31:0]", "Buffer长度 + IOC(bit31) + TTSE(bit30) + VTIR(bit14)"],
            ["tdes3", "[31:0]", "OWN(bit31) + CTXT(bit30) + FD(bit29) + LD(bit28) + TSE(bit17) + CIC(bits23:22)"],
          ],
          [1200, 1800, 6360]
        ),
        para(""),

        heading(2, "RX描述符 (struct osi_rx_desc)"),
        makeTable(
          ["字段", "位", "含义"],
          [
            ["rdes0", "[31:0]", "Buffer地址低32位"],
            ["rdes1", "[31:0]", "Buffer地址高32位"],
            ["rdes2", "[31:0]", "Buffer长度 (由HW写回)"],
            ["rdes3", "[31:0]", "OWN(bit31) + FD(bit29) + LD(bit28) + ES bits + PKT_LEN(bits13:0)"],
          ],
          [1200, 1800, 6360]
        ),

        // ═══ Section 8 ═══
        heading(1, "八、关键设计要点"),

        para("1. OWN bit 仲裁: TDES3_OWN / RDES3_OWN 是HW与SW的同步点。OWN=1表示HW拥有该描述符，SW不能触碰。OWN=0表示SW拥有。"),
        para("2. 描述符更新顺序 (TX): 最后设置 first_desc->OWN 和 context_desc->OWN，然后 dmb_oshst() 内存屏障，最后才更新 cur_tx_idx 和写 tail pointer——保证HW看到完整的描述符链。"),

        para("3. 中断合并: 支持三种策略:"),
        para("   - RX RIWT (Receive Interrupt Watchdog Timer) — 硬件定时器", { bold: false }),
        para("   - TX usecs — SW hrtimer，延迟触发NAPI来合并中断", { bold: false }),
        para("   - TX frames — 每N个帧才触发一次IOC中断", { bold: false }),

        para("4. 描述符回环: INCR_TX_DESC_INDEX(idx, x) / INCR_RX_DESC_INDEX(idx, x) 用 & (x - 1) 做环形回绕 (ring_sz 必须是2的幂)。"),
        para("5. PTP时间戳延迟 (MGBE): MGBE平台不从TX完成描述符直接读取时间戳，而是标记 OSI_TXDONE_CX_TS_DELAYED，由独立的 work queue (tx_ts_work) 周期性轮询OSI获取 (ether_get_tx_ts)。"),
        para("6. 发送队列反压: 当 ether_avail_txdesc_cnt() <= ETHER_TX_DESC_THRESHOLD 时，调用 netif_stop_subqueue(ndev, qinx) 停止该子队列。在TX完成回收时，osd_transmit_complete() 中调用 netif_wake_subqueue() 恢复。"),
        para("7. NAPI budget控制: TX/RX poll函数都受 budget 限制 (默认64)。超过budget时 napi_complete 不会被调用，NAPI子系统会在下一轮软中断中继续调度。"),

        // ═══ Section 9 ═══
        heading(1, "九、相关文件索引"),
        makeTable(
          ["文件", "内容"],
          [
            ["hardware/include/osi_dma.h", "所有DMA描述符结构体定义、DMA私有数据、OSD回调接口"],
            ["hardware/include/osi_dma_txrx.h", "Ring大小常量、描述符索引操作宏"],
            ["hardware/osi/dma/osi_dma_txrx.c", "TX/RX completions处理、描述符填充、硬件传输"],
            ["ether_linux.h", "ether_tx_napi/ether_rx_napi、ether_vm_irq_data、NAPI相关结构"],
            ["ether_linux.c", "NAPI poll函数、IRQ handler、资源分配、ether_start_xmit、ether_select_queue"],
            ["osd.c", "osd_transmit_complete()、osd_receive_packet() — OSD回调实现"],
            ["hardware/include/osi_core.h", "osi_pdma_vdma_data、osi_core_priv_data (MTL队列/PDMA配置)"],
          ],
          [4800, 4560]
        ),
      ]
    }
  ]
});

// ── write ────────────────────────────────────────────────────────────
const outPath = "/home/nova_zhang/omni/omnieth/docs/descriptor_napi_multiqueue_analysis.docx";
Packer.toBuffer(doc).then(buf => {
  fs.writeFileSync(outPath, buf);
  console.log("Written: " + outPath + " (" + buf.length + " bytes)");
}).catch(err => {
  console.error(err);
  process.exit(1);
});
