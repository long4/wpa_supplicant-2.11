# 网卡描述符结构、NAPI、多队列、多DMA调用关系分析

> 基于 Synopsys DWC 25GMAC Databook (Version 4.20a, January 2025) 与驱动源码交叉分析

## 一、描述符结构层次

整个描述符体系分为 **三层嵌套结构**，从外到内:

```
osi_dma_priv_data                     ← DMA引擎顶层 (每个netdev一个)
  ├─ tx_ring[0..N-1]                  ← 每个DMA channel有独立的TX ring
  │   ├─ tx_desc[0..ring_sz-1]        ← 硬件描述符环形队列 (4个u32字段)
  │   ├─ tx_swcx[0..ring_sz-1]        ← 软件上下文 (DMA映射地址/长度/flags)
  │   ├─ tx_pkt_cx                    ← 本次发送的包上下文 (VLAN/TSO/PTP标志)
  │   ├─ txdone_pkt_cx                ← 发送完成上下文 (回传给OSD层)
  │   ├─ cur_tx_idx                   ← 生产者指针 (发送时递增)
  │   └─ clean_idx                    ← 消费者指针 (完成中断时回收)
  │
  └─ rx_ring[0..N-1]                  ← 每个DMA channel有独立的RX ring
      ├─ rx_desc[0..ring_sz-1]        ← 硬件描述符环形队列
      ├─ rx_swcx[0..ring_sz-1]        ← 软件上下文 (含buf_phy_addr/buf_virt_addr)
      ├─ rx_pkt_cx                    ← 接收包上下文 (csum/VLAN/hash/PTP)
      ├─ cur_rx_idx                   ← 当前处理指针
      └─ refill_idx                   ← 补充buffer指针
```

### 关键数据结构

| 结构体 | 所在文件 | 作用 |
|--------|----------|------|
| `osi_tx_desc` / `osi_rx_desc` | `hardware/include/osi_dma.h:339-348,486-495` | 4×u32 硬件描述符，DMA-mapped，HW通过OWN bit仲裁 |
| `osi_tx_swcx` / `osi_rx_swcx` | `hardware/include/osi_dma.h:353-368,450-481` | 软件上下文：保存DMA映射的物理/虚拟地址、buffer长度 |
| `osi_tx_pkt_cx` | `hardware/include/osi_dma.h:501-536` | 单包发送上下文：VLAN tag、TSO MSS、PTP标志 (每ring复用) |
| `osi_tx_ring` / `osi_rx_ring` | `hardware/include/osi_dma.h:413-445,567-625` | 每个DMA channel独立的环形队列 |
| `osi_rx_pkt_cx` | `hardware/include/osi_dma.h:375-406` | 接收包上下文：csum/VLAN/hash/PTP时间戳 |
| `osi_txdone_pkt_cx` | `hardware/include/osi_dma.h:541-561` | 发送完成上下文：错误标志、PTP时间戳、pktid |
| `osi_dma_priv_data` | `hardware/include/osi_dma.h:699-846` | DMA引擎顶层私有数据：持有所有tx_ring/rx_ring指针数组 |

### Ring大小

| MAC类型 | TX Ring大小 | RX Ring大小 |
|---------|------------|------------|
| EQOS | 1024 | 1024 |
| MGBE | 4096 | 最大16384 (当前代码使用 `rx_ring_sz`) |

## 二、多DMA Channel架构

### 2.1 Channel数量来源

Channel数量由 **Device Tree** 配置，在probe阶段读取:

```
DT: nvidia,num_dma_chans = <N>   →  osi_dma->num_dma_chans
DT: nvidia,dma_chans = <0, 1, 2, 3>  →  osi_dma->dma_chans[]
```

MGBE最多支持 **10个DMA channel** (`OSI_MGBE_MAX_NUM_CHANS`)，EQOS最多4个。

### 2.2 资源分配

代码路径 (`ether_linux.c:2670-2697`):

```
ether_allocate_tx_dma_resources(osi_dma, dev):
    for each dma_chans[i] that is valid:
        allocate_tx_dma_resource(osi_dma, dev, chan):
            osi_dma->tx_ring[chan] = kzalloc(osi_tx_ring)
            tx_ring->tx_desc = dma_alloc_coherent(tx_desc_size, &phy_addr)  // 硬件描述符
            tx_ring->tx_swcx = kzalloc(tx_swcx_size)                        // 软件上下文
```

### 2.3 PDMA/VDMA 两级映射

#### 2.3.1 概念

在 Tegra T26x 平台上，DMA 架构存在 **VDMA (Virtual DMA) → PDMA (Physical DMA)** 两级层次，这是 T26X 硬件 DMA Controller 本身的特性，**与 VM 虚拟化无关**。

- **PDMA (Physical DMA)**：物理 DMA 引擎硬件，真正执行"取描述符→搬数据→回写描述符"。寄存器前缀 `PDMA_CHx_TxRxExtCfg`，配置 PBL、ORR、P2TCMP 映射等硬件级别参数。
- **VDMA (Virtual DMA)**：驱动软件操作的 DMA 通道，即 `dma_chans[]` 中的 channel 号。每个 VDMA 拥有独立的 `tx_ring` / `rx_ring` 描述符环。寄存器前缀 `VDMA_CHx_TX/RX_Desc_Ctrl`，配置 descriptor cache 大小、预取阈值等 per-channel 参数。
- **映射关系**：多个 VDMA channel 共享一个 PDMA 引擎。VDMA → PDMA 映射通过 TD2TCMP / RD2TCMP 寄存器配置，查询函数为 `vdma_to_pdma_map()`（`hardware/osi/dma/osi_dma.c:482`）。

```
PDMA (物理DMA引擎，硬件执行DMA)
├─ VDMA0 → tx_ring[0]/rx_ring[0] → descriptor cache + prefetch
├─ VDMA1 → tx_ring[1]/rx_ring[1]
└─ VDMA2 → tx_ring[2]/rx_ring[2]
   多个 VDMA 共享一个 PDMA，PDMA 硬件层面 round-robin 仲裁
```

**VDMA 的另一个用途**：PTP 时间戳追踪。在 `hw_transmit()` 中，`vdma_id`（即当前 channel 号）被写入 `tx_desc->tdes0[VDMA_ID]` 字段，发送完成时硬件通过 VDMA ID 将时间戳路由回正确的 channel（`osi_dma_txrx.c:1292-1293`）。

#### 2.3.2 数据结构

定义在 `hardware/include/osi_common.h:419-428`:

```c
/**
 * @brief OSI PDMA to VDMA mapping data
 * 描述每个 PDMA 引擎下挂载了哪些 VDMA channel
 */
struct osi_pdma_vdma_data {
    u32 pdma_chan;           // PDMA 物理通道号（硬件引擎编号）
    u32 num_vdma_chans;      // 该 PDMA 下有多少个 VDMA channel
    u32 vdma_chans[OSI_MGBE_MAX_NUM_CHANS]; // VDMA channel 列表
};
```

存储位置（两份副本）：
- `osi_core_priv_data->pdma_data[]`（`hardware/include/osi_core.h:1907`）
- `osi_dma_priv_data->pdma_data[]`（`hardware/include/osi_dma.h:845`）

初始化代码路径（`hardware/osi/core/mgbe_core.c:2754-2872`）：
```
for each PDMA:
    pdma_chan = pdma_data[i].pdma_chan
    写 PDMA_CH(#i)_TxRxExtCfg 寄存器（PBL, ORR, P2TCMP 映射）

    for each VDMA under this PDMA:
        vdma_chan = pdma_data[i].vdma_chans[j]
        写 VDMA_CH(vdma_chan)_TX_Desc_Ctrl 寄存器（DCSZ, DPS 预取阈值）
        写 VDMA_CH(vdma_chan)_RX_Desc_Ctrl 寄存器
```

#### 2.3.3 VDMA → PDMA 映射查询

`vdma_to_pdma_map()`（`hardware/osi/dma/osi_dma.c:482-518`）：
给定一个 VDMA channel 号，遍历 `pdma_data[]` 找到对应的 PDMA channel 号。这是 O(VDMA总数) 的线性查找，在需要操作 PDMA 级别寄存器时调用。

#### 2.3.4 IVC 虚拟化（独立于 VDMA 的另一个维度）

**IVC (Inter-VM Communication)** 是 Tegra Hypervisor 提供的 VM 间通信机制，与 VDMA **是两个独立的概念**：

- **VDMA**：硬件层面的 channel 抽象，始终存在（无论是否 VM 模式）
- **IVC 虚拟化**：Hypervisor 层面的 VM 隔离，通过 `use_virtualization` 标志控制

**非虚拟化**（`use_virtualization = OSI_DISABLE`）：
只有一个 OS 实例，直接操作所有 VDMA channel：
```
VDMA0 → PDMA0     (tx_ring[0] / rx_ring[0])
VDMA1 → PDMA0     (tx_ring[1] / rx_ring[1])
VDMA2 → PDMA1     (tx_ring[2] / rx_ring[2])
多个 VDMA 可以共享同一个 PDMA
```

**虚拟化**（`use_virtualization = OSI_ENABLE`）：
多个 VM 各自拥有一部分 VDMA channel，通过 IVC 与 Server VM 通信，由 Server VM 代理操作 PDMA 硬件。每个 VM 有独立的 VM IRQ，ISR 通过 `chan_mask` 只处理属于本 VM 的 channel：
```
VM0: 拥有 VDMA0, VDMA1  → 共享 PDMA0
VM1: 拥有 VDMA2, VDMA3  → 共享 PDMA1
Server VM: 接收各 VM 的 IVC 命令 → 操作 PDMA 寄存器
```

#### 2.3.5 虚拟化判定

代码路径 (`ether_linux.c:1950-1966`):

```c
// 通过 IVC (Inter-VM Communication) 检测是否在虚拟化环境
hv_np = of_parse_phandle(np, "ivc", 0);
if (!hv_np) {
    return -EINVAL;  // 没有 IVC → 非虚拟化
}
ictxt->ivck = tegra_hv_ivc_reserve(hv_np, id, NULL);

// probe 中的判定:
if (!ether_init_ivc(pdata)) {
    osi_core->use_virtualization = OSI_ENABLE;  // VM 模式
} else {
    osi_core->use_virtualization = OSI_DISABLE; // 非 VM 模式
}
```

核心逻辑：Device Tree 中有 `ivc` 节点 = Tegra Hypervisor 存在 = 虚拟化环境。

#### 2.3.6 PDMA/VDMA 映射解析（Device Tree）

`ether_get_vdma_mapping()` (`ether_linux.c:5198-5252`)：

从 DT 解析四个属性，填充 `osi_core->pdma_data[]`：
```
nvidia,pdma-num             → PDMA 数量
nvidia,pdma-chan            → PDMA 通道号
nvidia,num-vdma-channels    → 该 PDMA 下有多少 VDMA
nvidia,vdma-channels        → VDMA 通道号列表
```

每个 VM 通过 `ether_validate_vdma_chans()` (`ether_linux.c:5017-5028`) 校验其 VDMA channel 是否在某个 PDMA 的 `vdma_chans` 列表中有映射。

## 三、NAPI机制与队列绑定

### 3.1 NAPI实例化

**每个DMA channel 分配独立的 TX NAPI 和 RX NAPI 实例** (`ether_linux.c:4945-4994`):

```
for each DMA channel {
    pdata->tx_napi[chan]  = 新的 ether_tx_napi {
        .chan   = chan,
        .pdata  = pdata,
        .napi   = netif_napi_add(ndev, &napi, ether_napi_poll_tx),
        .tx_usecs_timer    // TX软件定时器 (合并中断用)
    };

    pdata->rx_napi[chan]  = 新的 ether_rx_napi {
        .chan   = chan,
        .pdata  = pdata,
        .napi   = netif_napi_add(ndev, &napi, ether_napi_poll_rx),
    };
}
```

数据结构定义 (`ether_linux.h:373-396`):

```c
struct ether_tx_napi {
    unsigned int chan;           // DMA channel号
    struct ether_priv_data *pdata;  // 回指私有数据
    struct napi_struct napi;     // 内核NAPI实例
    struct hrtimer tx_usecs_timer;  // TX SW定时器 (合并中断)
    atomic_t tx_usecs_timer_armed;  // 定时器是否已启动
};

struct ether_rx_napi {
    unsigned int chan;
    struct ether_priv_data *pdata;
    struct napi_struct napi;
};
```

### 3.2 NAPI Poll 函数详解

**RX Poll** (`ether_linux.c:4842-4865`):

```c
ether_napi_poll_rx(napi, budget):
    rx_napi = container_of(napi, ether_rx_napi)  // 从napi找回channel信息
    received = osi_process_rx_completions(osi_dma, chan, budget, &more)
    // osi_process_rx_completions 内部 (osi_dma_txrx.c:304):
    //   遍历 rx_ring[chan]->rx_desc[], 从 cur_rx_idx 开始
    //   检查 RDES3_OWN bit — 如果OWN=1说明HW还在处理, break
    //   解析 RDES3 字段:
    //     - pkt_len: 包长度
    //     - ES bits: 错误状态
    //     - FD/LD: first/last descriptor
    //   调用 process_rx_desc():
    //     - d_ops[mac].get_rx_csum()     → 硬件校验和结果
    //     - d_ops[mac].get_rx_vlan()     → VLAN tag
    //     - d_ops[mac].get_rx_hwstamp()  → PTP时间戳
    //     - osi_dma->osd_ops.receive_packet() → osd_receive_packet()
    //       → page_pool? → napi_gro_receive()
    //       → 否则: netif_receive_skb()

    if received < budget:
        napi_complete(napi)           // NAPI结束
        osi_handle_dma_intr(ENABLE)   // 重新使能硬件中断
```

**TX Poll** (`ether_linux.c:4880-4912`):

```c
ether_napi_poll_tx(napi, budget):
    tx_napi = container_of(napi, ether_tx_napi)  // 从napi找回channel信息
    processed = osi_process_tx_completions(osi_dma, chan, budget)
    // osi_process_tx_completions 内部 (osi_dma_txrx.c:710):
    //   从 tx_ring[chan]->clean_idx 开始遍历到 cur_tx_idx
    //   检查 TDES3_OWN bit — 如果OWN=1说明HW还没处理完, break
    //   检查 TDES3_LD (last descriptor):
    //     - 如果是最后一个描述符 → 触发 transmit_complete 回调
    //     - 检查 TDES3_ES_BITS → 发送错误统计
    //   处理PTP时间戳:
    //     - EQOS: 从 TDES0/TDES1 直接读取
    //     - MGBE: 标记 OSI_TXDONE_CX_TS_DELAYED (延迟获取)
    //   osi_dma->osd_ops.transmit_complete() = osd_transmit_complete()
    //     → dev_kfree_skb_any(skb)  // 释放SKB
    //   清零描述符, INCR_TX_DESC_INDEX(entry)
    //   tx_ring->clean_idx = entry  // 每步都更新clean_idx

    if !ring_empty && use_tx_usecs:
        hrtimer_start(tx_usecs_timer)  // 启动SW定时器继续轮询
    if processed < budget:
        napi_complete(napi)
        osi_handle_dma_intr(ENABLE)
```

### 3.3 中断处理与NAPI调度

#### 3.3.1 中断触发机制

**硬件中断触发条件完全相同**（无关VM/非VM）：
- TX: HW 完成发送 → `last_desc->tdes3 OWN=0` → `TDES2_IOC=1` → 硬件拉高中断信号
- RX: HW 写入数据 → `rx_desc->rdes3 OWN=0` → `RDES3_IOC=1` → 硬件拉高中断信号

**区分：中断路由到CPU的路径不同**：

```
非 VM 模式:
  DMA Ch0 TX 完成 ──→ IRQ line #tx0 ──→ GIC ──→ ether_tx_chan_isr(chan=0)
  DMA Ch0 RX 完成 ──→ IRQ line #rx0 ──→ GIC ──→ ether_rx_chan_isr(chan=0)
  DMA Ch1 TX 完成 ──→ IRQ line #tx1 ──→ GIC ──→ ether_tx_chan_isr(chan=1)
  每个 channel 的 TX/RX 各有一条独立 IRQ 线 → GIC 直接区分

VM 模式:
  DMA Ch0 TX 完成 ──┐
  DMA Ch0 RX 完成 ──┤
  DMA Ch1 TX 完成 ──┼──→ Global DMA Status 寄存器 ──→ 一条 VM IRQ → GIC → ether_vm_isr()
  DMA Ch2 RX 完成 ──┘
  所有 channel 汇入一个寄存器 → 单条 IRQ 线 → CPU 需要解析寄存器
```

#### 3.3.2 IOC (Interrupt on Completion) 位控制

在 `hw_transmit()` (`osi_dma_txrx.c:1352`) 中：

```c
// 默认：每个包的最后一个描述符设置 IOC
last_desc->tdes2 |= TDES2_IOC;

// 然后根据配置决定保留还是清除
set_clear_ioc_for_last_desc(...);
```

`set_clear_ioc_for_last_desc()` (`osi_dma_txrx.c:1160-1187`) 的决策逻辑：

```c
if (osi_dma->use_tx_usecs == OSI_ENABLE) {
    last_desc->tdes2 &= ~TDES2_IOC;      // ← 清除 IOC！不产生中断！

    if (osi_dma->use_tx_frames == OSI_ENABLE) {
        // 每 N 个帧才设一次 IOC (合并中断)
        if ((tx_ring->frame_cnt % osi_dma->tx_frames) == 0)
            last_desc->tdes2 |= TDES2_IOC;
    } else if (osi_dma->use_tx_descs == OSI_ENABLE) {
        // 每 N 个描述符才设一次 IOC
        if (tx_ring->desc_cnt >= osi_dma->intr_desc_count)
            last_desc->tdes2 |= TDES2_IOC;
    }
}
// use_tx_usecs == DISABLE: IOC 保持置位 = 每个包触发中断
```

#### 3.3.3 非VM模式 — 每channel独立的IRQ line

**IRQ注册** (`ether_linux.c:2079-2118`)：每个channel注册 2 个独立IRQ（TX + RX）

```
channel 0: IRQ_rx0 → ether_rx_chan_isr  (data = rx_napi[0])
           IRQ_tx0 → ether_tx_chan_isr  (data = tx_napi[0])
channel 1: IRQ_rx1 → ether_rx_chan_isr  (data = rx_napi[1])
           IRQ_tx1 → ether_tx_chan_isr  (data = tx_napi[1])
```

**ISR 处理** (`ether_linux.c:1741-1768`)：data 参数直接就是 `ether_tx_napi` 结构体，可直接拿到 channel 号。

```c
IRQ → ether_tx_chan_isr(irq, tx_napi_data)
    → raw_spin_lock(rlock)
    → osi_handle_dma_intr(chan, TX, DISABLE)  // 先关中断
    → raw_spin_unlock(rlock)
    → __napi_schedule_irqoff(&tx_napi->napi)  // 调度NAPI (软中断上下文)
```

```c
IRQ → ether_rx_chan_isr(irq, rx_napi_data)
    → raw_spin_lock(rlock)
    → osi_handle_dma_intr(chan, RX, DISABLE)
    → raw_spin_unlock(rlock)
    → __napi_schedule_irqoff(&rx_napi->napi)
```

#### 3.3.4 VM模式 — 共享VM IRQ，从Global DMA Status寄存器解析

**IRQ注册** (`ether_linux.c:2050-2077`)：每个VM注册一条VM IRQ

```c
VM0 的一条 IRQ → ether_vm_isr  (data = vm_irq_data[0])
                  └ chan_mask = {channel0, channel1} 的 TX+RX 位掩码
```

**ISR 处理** (`ether_linux.c:1665-1723`)：

```c
IRQ → ether_vm_isr(irq, vm_irq_data)
    → osi_get_global_dma_status(osi_dma, dma_status[3])   // 读Global DMA Status 寄存器
    → dma_status[i] &= vm_irq->chan_mask[i]                // 只关心本VM的channel
    → while dma_status[i] != 0:                             // 循环处理多个channel
        temp = ffs(dma_status[i]) - 1                       // 找到第一个置位bit
        chan = (temp >> 1) + (16 * i)                       // bit位→channel号
        txrx = temp & 1                                     // 0=TX, 1=RX
        if txrx:
            osi_handle_dma_intr(chan, RX, DISABLE)
            __napi_schedule_irqoff(&rx_napi[chan]->napi)
        else:
            osi_handle_dma_intr(chan, TX, DISABLE)
            __napi_schedule_irqoff(&tx_napi[chan]->napi)
        dma_status[i] &= ~BIT(temp)
```

Global DMA Status 寄存器编码：`dma_status[i]` 的 bit[2n] = channel n TX, bit[2n+1] = channel n RX。

**chan_mask 的生成** (`ether_linux.c:5052-5069`)：
```c
// DT: nvidia,vm-channels = <0 2>;  // VM0 拥有 channel 0 和 2
for (i = 0; i < num_vm_chan; i++) {
    chan = vm_chans[i];
    vm_irq_data->chan_mask[chan / 16] |= ETHER_VM_IRQ_TX_CHAN_MASK(chan % 16);
    vm_irq_data->chan_mask[chan / 16] |= ETHER_VM_IRQ_RX_CHAN_MASK(chan % 16);
}
```

这就是**多VM隔离的关键**：不同VM共享同一个Global DMA Status寄存器，但各自的ISR通过 `chan_mask` 只处理属于本VM的channel。

#### 3.3.5 非VM与VM NAPI调度对比总表

| 维度 | 非 VM 模式 | VM 模式 |
|------|-----------|--------|
| **IRQ 数量** | 每条 channel 2 个（TX+RX） | 每个 VM 1 个 |
| **IRQ 注册** | `devm_request_irq(ether_tx/rx_chan_isr)` | `devm_request_irq(ether_vm_isr)` |
| **ISR 入参 data** | `ether_tx_napi` / `ether_rx_napi` | `ether_vm_irq_data` |
| **channel 获知方式** | `((ether_tx_napi *)data)->chan` 直接读取 | 读 Global DMA Status 寄存器 + 位图解析 |
| **多 channel 处理** | 一次 ISR 只处理一个 channel | 一次 ISR 可能循环处理多个 channel |
| **channel 过滤** | 不需要（硬件中断线天然隔离） | `chan_mask` 必须过滤（多 VM 共享寄存器） |
| **NAPI 实例** | 完全相同：`pdata->tx_napi[chan]` / `rx_napi[chan]` | 完全相同 |
| **NAPI poll 函数** | `ether_napi_poll_tx` / `ether_napi_poll_rx` | 完全相同 |
| **中断触发机制** | IOC位 + OWN bit（完全相同） | IOC位 + OWN bit（完全相同） |
| **中断禁能/使能** | `osi_handle_dma_intr(chan, ...)` per-channel | 相同的 `osi_handle_dma_intr(chan, ...)` |

### 3.4 TX SW定时器 (合并中断优化)

当 `use_tx_usecs == ENABLE` 时，硬件发送完成不会触发中断（IOC被清除），而是通过SW hrtimer驱动轮询。这是驱动级别的中断合并优化。

#### 3.4.1 定时器启动：每次发包后

```c
// ether_start_xmit 中:
if (osi_dma->use_tx_usecs == OSI_ENABLE &&
    !tx_usecs_timer_armed) {
    atomic_set(&tx_napi->tx_usecs_timer_armed, OSI_ENABLE);
    hrtimer_start(&tx_napi->tx_usecs_timer, tx_usecs * NSEC_PER_USEC);
}
```

#### 3.4.2 定时器回调：在硬中断上下文中调度 NAPI

```c
ether_tx_usecs_hrtimer(timer):
    tx_napi = container_of(timer, ether_tx_napi, tx_usecs_timer)
    atomic_set(&tx_napi->tx_usecs_timer_armed, OSI_DISABLE)
    __napi_schedule_irqoff(&tx_napi->napi)  // 软中断中回收完成描述符
```

#### 3.4.3 NAPI Poll 中重新 ARM 定时器

```c
// ether_napi_poll_tx 中:
if (!osi_txring_empty(osi_dma, chan) &&
    osi_dma->use_tx_usecs == OSI_ENABLE &&
    !tx_usecs_timer_armed) {
    atomic_set(&tx_napi->tx_usecs_timer_armed, OSI_ENABLE);
    hrtimer_start(&tx_napi->tx_usecs_timer, ...);
}
```

#### 3.4.4 两种模式的完整对比

```
默认模式 (use_tx_usecs=0):
  skb → HW DMA → OWN=0, IOC=1 → IRQ → ISR(关中断) → NAPI poll → complete(开中断)
  特点: 低延迟，高吞吐时中断开销大

TX usecs 模式 (use_tx_usecs=1):
  skb → HW DMA → OWN=0, IOC=0 → (无硬件中断)
  ether_start_xmit() → hrtimer_start() → 超时 → NAPI poll → re-arm timer
  特点: 零硬件中断，高吞吐，延迟由 tx_usecs 微秒数控制

TX frames 模式 (折中):
  每 N 个帧的最后一个描述符才设 IOC = 1
  特点: 合并 N 个完成通知为一次中断
```

## 四、多队列(TX Queue)与DMA Channel映射

### 4.1 Linux netdev队列数量

```c
// probe中 (ether_linux.c:7522):
ndev = alloc_etherdev_mq(sizeof(ether_priv_data), num_dma_chans);
// num_dma_chans = osi_dma->num_dma_chans
// 这告诉内核: 此设备有 num_dma_chans 个TX队列
```

### 4.2 队列选择: `ndo_select_queue` — `ether_select_queue()`

(`ether_linux.c:3968-3995`)

```c
unsigned short ether_select_queue(struct net_device *dev, struct sk_buff *skb,
                                  struct net_device *sb_dev)
{
    unsigned int priority = skb->priority;           // ① 读取 skb 优先级

    if (skb_vlan_tag_present(skb)) {
        priority = skb_vlan_tag_get_prio(skb);       // ② VLAN 包用 PCP 优先级覆盖
    }

    for (i = 0; i < osi_core->num_dma_chans; i++) {
        mtlq = osi_core->dma_chans[i];
        if (pdata->txq_prio[mtlq] == priority) {     // ③ 按优先级匹配队列
            return i;                                 // ← 返回队列索引
        }
    }
    return 0;
}
```

### 4.3 `skb->queue_mapping` 的完整设置流程

`skb->queue_mapping` **不是**在 `ether_start_xmit` 中设置的，而是在内核协议栈调用 `ndo_start_xmit` 之前由 `__netdev_pick_tx()` 设置的：

```
应用层 send()
  → 内核协议栈 → dev_queue_xmit()
    → __dev_queue_xmit()
      → __netdev_pick_tx(dev, skb)
        │
        ├─ ① 如果有 XPS 配置 (sysfs: /sys/class/net/xxx/queues/tx-*/xps_cpus):
        │     根据 CPU → queue 映射选择队列
        │
        ├─ ② 否则调用 ndo_select_queue():
        │     queue_index = ether_select_queue(dev, skb)
        │     (通过 skb->priority 或 VLAN PCP 匹配 txq_prio[])
        │
        └─ ③ 内核写入 skb:
              skb_set_queue_mapping(skb, queue_index)
              // 等价于: skb->queue_mapping = queue_index

      → ndo_start_xmit = ether_start_xmit(skb, ndev)
          读取: qinx = skb_get_queue_mapping(skb)
```

### 4.4 `skb->priority` 的来源

| 来源 | 说明 |
|------|------|
| 应用层 `SO_PRIORITY` | `setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio)` |
| VLAN PCP | 当 skb 携带 VLAN tag 时，`ether_select_queue` 直接用 `skb_vlan_tag_get_prio()` 覆盖 |
| iptables CLASSIFY | `iptables -t mangle -A OUTPUT -j CLASSIFY --set-class 0:3` |
| TC 过滤器 | `tc filter ... action skbedit priority 3` |
| 应用层 cmsg | `sendmsg()` 通过 `SO_PRIORITY` 辅助数据 |

### 4.5 `txq_prio[]` 的配置来源

在 probe 阶段 (`ether_linux.c:6802`)，从 Device Tree 读取：

```
DT: nvidia,tx-queue-prio = <0x0 0x1 0x2 0x3>;
// MTL 队列 0 → 优先级 0, MTL 队列 1 → 优先级 1, ...
```

### 4.6 发送: 队列 → DMA channel

```c
ether_start_xmit(skb, ndev)                        [ether_linux.c:3997]:
    qinx  = skb_get_queue_mapping(skb)             // 内核选择的队列号
    chan  = osi_dma->dma_chans[qinx]               // 队列号 → DMA channel号
    tx_ring = osi_dma->tx_ring[chan]               // DMA channel → TX ring

    ether_tx_swcx_alloc(pdata, tx_ring, skb)       // 填充 tx_swcx[] 数组
    //   (映射 SKB 的 frags 到多个描述符)
    //   (每个描述符 ≤ 16KB = OSI_TX_MAX_BUFF_SIZE)
    osi_hw_transmit(osi_dma, chan)                  // 写描述符 → 更新 tail pointer

    if ether_avail_txdesc_cnt(tx_ring) <= THRESHOLD:
        netif_stop_subqueue(ndev, qinx)             // 停止该子队列
```

**关键**: `dma_chans[i]` 数组是队列索引到DMA channel号的映射表，由DT配置决定。

### 4.7 RSS多队列

```c
ether_init_rss(pdata, features)                    [ether_linux.c:3095]:
    num_q = osi_core->num_mtl_queues               // MTL队列数
    if T26x MGBE: num_q = num_dma_chans
    for each RSS table entry (128 entries):
        rss->table[i] = ethtool_rxfh_indir_default(i, num_q)
    // HW根据RSS hash将数据包分发到不同RX DMA channel
```

## 五、TX/RX 软硬件时序图

### TX 发送路径

![TX Sequence Diagram](tx_sequence.png)

### RX 接收路径

![RX Sequence Diagram](rx_sequence.png)

## 六、完整调用链

### TX Path (发送路径)

```
应用层 send()
  → 内核协议栈
    → ndo_start_xmit = ether_start_xmit()               [ether_linux.c:3997]
      → skb_get_queue_mapping(skb) → qinx
      → chan = osi_dma->dma_chans[qinx]                  // 队列→channel映射
      → tx_ring = osi_dma->tx_ring[chan]
      → ether_tx_swcx_alloc(pdata, tx_ring, skb)        // 填充tx_swcx[N]
          // TSO: 分配多个描述符 (每段≤16KB)
          // PTP: 标记 OSI_PKT_CX_PTP
          // VLAN: 标记 OSI_PKT_CX_VLAN
      → osi_hw_transmit(osi_dma, chan)                   [osi_dma_txrx.c:1086→1212]
        → hw_transmit(osi_dma, tx_ring, chan):
          → need_cntx_desc():                            // 检查是否需要VLAN/TSO/PTP上下文描述符
          → fill_first_desc():                           // 填充第一描述符
                                                          //   tdes0=addr_lo, tdes1=addr_hi
                                                          //   tdes2=len, tdes3=FD|CIC
          → for each remaining desc:                     // 填充中间描述符
              tx_desc->tdes0 = L32(buf_phy_addr)
              tx_desc->tdes3 |= TDES3_OWN                // 中间描述符先设OWN
          → last_desc->tdes3 |= TDES3_LD                 // 最后描述符
          → set_swcx_pkt_id_for_ptp()                    // PTP包ID
          → set_clear_ioc_for_last_desc()                // ★ 中断合并: IOC/frames/descs
          → first_desc->tdes3 |= TDES3_OWN               // ★最后设置first OWN (避免race)
          → set_context_desc_own_bit()                   // ★最后设置context OWN
          → dmb_oshst()                                  // ★内存屏障
          → tx_ring->cur_tx_idx = entry                  // 更新生产者指针
          → osi_dma_writel(tailptr, TDTP寄存器)          // ★写Tail Pointer → HW开始DMA

  // === HW异步DMA完成 (默认模式: use_tx_usecs=DISABLE) ===
  HW DMA → IOC=1 → IRQ
    → ether_tx_chan_isr(irq, tx_napi_data)               [ether_linux.c:1741]
      → raw_spin_lock(rlock)
      → osi_handle_dma_intr(chan, TX, DISABLE)           // 关TX中断
      → raw_spin_unlock(rlock)
      → __napi_schedule_irqoff(&tx_napi->napi)           // 调度NAPI

  // === TX usecs模式 ===
  ether_start_xmit()
    → hrtimer_start(tx_usecs_timer)                      // 启动SW定时器
  → 超时 → ether_tx_usecs_hrtimer()
    → __napi_schedule_irqoff(&tx_napi->napi)

  // === 软中断上下文 (NET_RX_SOFTIRQ) ===
  → ether_napi_poll_tx(napi, budget)                     [ether_linux.c:4880]
    → osi_process_tx_completions(osi_dma, chan, budget)  [osi_dma_txrx.c:710]
      → for entry = clean_idx .. cur_tx_idx:
          → tx_desc = tx_ring->tx_desc + entry
          → if TDES3_OWN: break                          // HW还在处理
          → process_last_desc():                         // 检查LD + 错误统计
          → if PTP two-step:
              txdone_pkt_cx->flags |= OSI_TXDONE_CX_TS_DELAYED
          → set_paged_buf_and_set_len()
          → osi_dma->osd_ops.transmit_complete()         // = osd_transmit_complete() [osd.c]
            → dev_kfree_skb_any(skb)                     // 释放SKB
          → 清零 tx_desc (tdes0~3 = 0)
          → 清零 tx_swcx (buf_virt_addr, buf_phy_addr, flags)
          → INCR_TX_DESC_INDEX(entry, tx_ring_sz)        // clean_idx前进
          → tx_ring->clean_idx = entry                   // ★立即更新clean_idx

      → if ring不为空 && use_tx_usecs:
          hrtimer_start(tx_usecs_timer)                  // SW定时器延迟回收
      → if processed < budget:
          napi_complete(napi)                            // NAPI结束
          raw_spin_lock(rlock)
          osi_handle_dma_intr(chan, TX, ENABLE)          // ★重新使能中断
          raw_spin_unlock(rlock)
      → if processed >= budget:
          // 不调用 napi_complete，中断保持关闭
          // 下一轮 NET_RX_SOFTIRQ 软中断继续处理
```

### RX Path (接收路径)

```
  // === HW写入RX描述符 ===
  HW DMA → 写入数据到 rx_desc[] → 设置 RDES3_OWN=0 (表示SW拥有)

  // === 硬件中断 ===
  HW → IRQ
    → ether_rx_chan_isr(irq, rx_napi_data)               [ether_linux.c:1788]
      → raw_spin_lock(rlock)
      → osi_handle_dma_intr(chan, RX, DISABLE)           // 关RX中断
      → raw_spin_unlock(rlock)
      → __napi_schedule_irqoff(&rx_napi->napi)           // 调度NAPI

  // === 软中断上下文 ===
  → ether_napi_poll_rx(napi, budget)                     [ether_linux.c:4842]
    → osi_process_rx_completions(osi_dma, chan, budget)  [osi_dma_txrx.c:304]
      → validate_rx_completions_arg()                    // 校验参数
      → if MGBE_T26X:
          rx_desc_compltd = compltd_rx_desc_cnt()        // 读HW写指针
          budget = min(budget, rx_desc_compltd)
      → while received < budget:
          → rx_desc = rx_ring->rx_desc + cur_rx_idx
          → if RDES3_OWN: break                          // HW拥有，没数据了
          → if EQOS: is_data_ready_to_process()          // 检查DMA是否空闲
          → INCR_RX_DESC_INDEX(cur_rx_idx)
          → if 保留buffer: realloc_buf() → continue
          → if 已处理 (OSI_RX_SWCX_PROCESSED): break
          → if FD+LD不都为1:  // 多描述符包，但非last
              标记invalid, 仍然上报receive_packet
              continue
          → pkt_len = rx_desc->rdes3 & RDES3_PKT_LEN    // 获取包长度
          → flags |= OSI_PKT_CX_VALID
          → process_rx_desc():                           // 解析csum/VLAN/hash/PTP
            → d_ops[mac].get_rx_csum(rx_desc, rx_pkt_cx)
            → d_ops[mac].get_rx_vlan(rx_desc, rx_pkt_cx)
            → d_ops[mac].get_rx_hash(rx_desc, rx_pkt_cx)
            → d_ops[mac].get_rx_hwstamp(...)
            → osi_dma->osd_ops.receive_packet()          // = osd_receive_packet() [osd.c]
              → if page_pool:
                  page_pool_recycle_direct()
                  napi_gro_receive()
              → else:
                  netif_receive_skb()
          → if received + received_resv >= budget:
              check_for_more_data_avail()                // 设置 more_data_avail 标志

      → if received < budget:
          napi_complete(napi)
          raw_spin_lock(rlock)
          osi_handle_dma_intr(chan, RX, ENABLE)
          raw_spin_unlock(rlock)
```

## 七、描述符字段详解

### TX描述符 (struct osi_tx_desc)

| 字段 | 位 | 含义 |
|------|-----|------|
| tdes0 | [31:0] | Buffer地址低32位 |
| tdes1 | [31:0] | Buffer地址高32位 (64-bit寻址时) |
| tdes2 | [31:0] | Buffer长度[13:0] + IOC(bit31) + TTSE(bit30) + VTIR(bit14) |
| tdes3 | [31:0] | OWN(bit31) + CTXT(bit30) + FD(bit29) + LD(bit28) + TSE(bit17) + CIC(bits23:22) |

TX描述符仅两种格式（读/写回），结构简单。

### RX描述符 (struct osi_rx_desc)

**重点**: RX描述符严格分为 **读格式 (Read Format)** 和 **写回格式 (Write-Back Format)**，同一个4×u32在不同阶段含义完全不同。

#### 读格式 (Read Format): SW准备 → HW读取

这是驱动初始化描述符时的格式：

| 字段 | 位 | 含义 |
|------|-----|------|
| rdes0 | [31:0] | **Header/Buffer 1 Address 低32位** — SW填写的 DMA 缓冲区物理地址 |
| rdes1 | [31:0] | **Header/Buffer 1 Address 高32位** (64-bit寻址时)，否则保留 |
| rdes2 | [31:0] | **Buffer 1 Length / Buffer 2 Address 低32位** — SW填写的第二个缓冲区地址 |
| rdes3 | [31:0] | **OWN(bit31) + IOC(bit30) + Buffer 2 Address [61:32]** — SW置OWN=1交给HW |

#### 写回格式 (Write-Back Format): HW完成 → HW写回 → SW读取

这是HW完成数据写入后回写的状态格式，仅在 **LD=1 (最后一个描述符)** 时字段有效：

| 字段 | 位 | 含义 |
|------|-----|------|
| rdes0 | [31:0] | **IVT[15:0] / OVT[15:0]** — Inner/Outer VLAN Tag (HW解析写入) |
| rdes1 | [31:0] | **RSS Hash [31:0]** — RSS哈希值 (HW计算写入，当 RSV=1) |
| rdes2 | [31:0] | **MAC Filter Status + Header Length** — L3L4 filter匹配号[31:29]、MAC地址匹配[26:19]、VLAN filter[15]、Header Length[9:2] 等 |
| rdes3 | [31:0] | **OWN(bit31) + CTXT(bit30) + FD(bit29) + LD(bit28)** + **Packet Length[13:0]** + ES(bit15) + Error Type/L2 Type[19:16] + L34T[23:20] + ISP[25] + RSV[26] |

#### RDES2 写回格式详细字段（Databook Table 13-18）

| 位 | 名称 | 含义 |
|----|------|------|
| 31:29 | L3L4FM | Layer 3/Layer 4 Filter Number Matched |
| 28 | L4FM | Layer 4 Filter Match |
| 27 | L3FM | Layer 3 Filter Match |
| 26:19 | MADRM | MAC Address Match / Hash Value / L3L4 Filter Match |
| 18:17 | HFDAF | Hash Filter and Destination Address Filter Status |
| 17 | DAF | DA Filter Fail |
| 16 | SAF | SA Filter Fail |
| 15 | VF | VLAN Filter Status |
| 14 | RPNG | Response Packet Not Generated |
| 13 | IOS | IVT-OVT Select (外部查找接口) |
| 12 | ELD | External Lookup Data valid |
| 11 | TNP | Tunnel Packet |
| 9:2 | HL | L3/L4 Header Length (头部分离功能) |
| 1 | AVTDP | AV Tagged Data Packet |
| 0 | AVTCP | AV Tagged Control Packet |

#### RDES3 PKT_LEN vs Buffer Size

- **RDES3[13:0] PKT_LEN (Packet Length)** = 接收到的整个以太网帧的**实际字节数**。仅在 LD=1 时有效
- **Buffer Size** = 由驱动在 RBSZ 寄存器中配置的**每个 DMA buffer 的容量**（从 MTU + 开销计算，存于 `osi_dma->rx_buf_len`），**不存储在任何描述符字段中**
- HW 不提供 buffer size in descriptor（Databook 明确指出："The DMA does not provide Buffer size in the Rx descriptors"）

多描述符包的计算：`最后一个buffer的有效数据 = PKT_LEN - sum(前置buffer大小)`

## 八、关键设计要点

1. **OWN bit 仲裁**: `TDES3_OWN` / `RDES3_OWN` 是HW与SW的同步点。OWN=1表示HW拥有该描述符，SW不能触碰。OWN=0表示SW拥有。

2. **描述符更新顺序 (TX)**: 最后设置 `first_desc->OWN` 和 `context_desc->OWN`，然后 `dmb_oshst()` 内存屏障，最后才更新 `cur_tx_idx` 和写 tail pointer——保证HW看到完整的描述符链。

3. **中断合并**: 支持多种策略:
   - **RX RIWT** (Receive Interrupt Watchdog Timer) — 硬件定时器延迟RX中断
   - **TX usecs** — SW hrtimer替代硬件中断：清除IOC → hrtimer驱动NAPI poll
   - **TX frames** — 每N个帧的最后一个描述符才设IOC=1
   - **TX descs** — 每N个描述符才设IOC=1
   - 决策函数: `set_clear_ioc_for_last_desc()` (`osi_dma_txrx.c:1160-1187`)

4. **描述符回环**: `INCR_TX_DESC_INDEX(idx, x)` / `INCR_RX_DESC_INDEX(idx, x)` 用 `& (x - 1)` 做环形回绕 (ring_sz 必须是2的幂)。

5. **PTP时间戳延迟 (MGBE)**: MGBE平台不从TX完成描述符直接读取时间戳，而是标记 `OSI_TXDONE_CX_TS_DELAYED`，由独立的 work queue (`tx_ts_work`) 周期性轮询OSI获取 (`ether_get_tx_ts`)。

6. **发送队列反压**: 当 `ether_avail_txdesc_cnt() <= ETHER_TX_DESC_THRESHOLD` 时，调用 `netif_stop_subqueue(ndev, qinx)` 停止该子队列。在TX完成回收时，`osd_transmit_complete()` 中调用 `netif_wake_subqueue()` 恢复。

7. **NAPI budget控制**: TX/RX poll函数都受 budget 限制 (默认64)。超过budget时 `napi_complete` 不会被调用：
   - **中断保持关闭**（防止重入）
   - NAPI子系统在**下一轮 NET_RX_SOFTIRQ 软中断**中继续调度 `ether_napi_poll_tx/rx`

8. **中断→NAPI→恢复循环**:
   ```
   默认模式: ISR(关中断) → __napi_schedule_irqoff → NAPI poll → if processed<budget:
               napi_complete() → osi_handle_dma_intr(ENABLE)   // ← 唯一恢复中断的路径
   ```
   中断在 ISR 中关闭，**仅在 NAPI poll 处理完所有可用描述符后才重新使能**。

9. **RDES2 不是 buffer 长度**：RDES2 在读格式中是 Buffer 2 Address Pointer，在写回格式中是 MAC filter status + Header Length 等状态字段。Buffer容量由 RBSZ 寄存器配置，不在描述符中体现。

## 九、虚拟化 (VM) vs 非虚拟化 (non-VM) 对比总结

| 维度 | 非 VM 模式 | VM 模式 |
|------|-----------|--------|
| **虚拟化检测** | DT 中无 `ivc` 节点 | DT 中有 `ivc` 节点 → `ether_init_ivc()` |
| **use_virtualization** | `OSI_DISABLE` | `OSI_ENABLE` |
| **PDMA/VDMA 关系** | VDMA:PDMA = 1:1 | VDMA:PDMA = N:1 (round-robin仲裁) |
| **IRQ 数量** | 每条 channel 2 个（TX+RX） | 每个 VM 1 个 |
| **中断源识别** | 硬件 IRQ 线天然隔离 | 软件读 Global DMA Status 寄存器 |
| **ISR 函数** | `ether_tx/rx_chan_isr` | `ether_vm_isr` |
| **channel 过滤** | 不需要 | `chan_mask` 必须过滤 |
| **一次 ISR 处理** | 1 个 channel | 可能多个 channel |
| **硬件中断触发机制** | IOC + OWN bit（完全相同） | IOC + OWN bit（完全相同） |
| **中断控制寄存器** | `osi_handle_dma_intr()` 操作 per-channel 寄存器 | 相同的 per-channel 寄存器 |
| **NAPI 实例/调度** | `pdata->tx/rx_napi[chan]->napi` | **完全相同** |

## 十、相关文件索引

| 文件 | 内容 |
|------|------|
| `hardware/include/osi_dma.h` | 所有DMA描述符结构体定义、DMA私有数据、OSD回调接口、PDMA数据 |
| `hardware/include/osi_dma_txrx.h` | Ring大小常量、描述符索引操作宏 |
| `hardware/include/osi_core.h` | `osi_pdma_vdma_data`、`osi_core_priv_data` (MTL队列/PDMA配置)、`osi_vm_irq_data` |
| `hardware/include/osi_common.h` | `osi_pdma_vdma_data` 结构体定义 |
| `hardware/osi/dma/osi_dma_txrx.c` | TX/RX completions处理、描述符填充、`set_clear_ioc_for_last_desc()`、硬件传输 |
| `hardware/osi/dma/osi_dma.c` | `osi_handle_dma_intr()`、`osi_get_global_dma_status()` |
| `hardware/osi/dma/hw_desc.h` | 描述符字段位定义 (RDES3_PKT_LEN, TDES2_IOC等) |
| `ether_linux.h` | `ether_tx_napi/ether_rx_napi`、`ether_vm_irq_data`、NAPI相关结构 |
| `ether_linux.c` | NAPI poll函数、IRQ handler (`ether_tx/rx_chan_isr`, `ether_vm_isr`)、资源分配、`ether_start_xmit`、`ether_select_queue`、`ether_alloc_napi`、`ether_set_vm_irq_chan_mask`、PDMA/VDMA解析 (`ether_get_vdma_mapping`, `ether_validate_vdma_chans`) |
| `osd.c` | `osd_transmit_complete()`、`osd_receive_packet()` — OSD回调实现 |

## 十一、CTXT 描述符与 Normal 描述符组织详解

所有描述符都使用同一个结构体 `struct osi_tx_desc`（4 个 u32），没有物理上的 "CTXT 描述符" 和 "普通描述符" 之分。区别仅在于 `tdes3` 中 `CTXT` bit（bit 30）是否置 1。

### VLAN/TSO/PTP 描述符链组织

规则：**只有一个 CTXT 描述符，始终放在链的最前面**。VLAN、TSO、PTP 无论单独还是组合，都共用一个 CTXT 描述符。

#### 1. VLAN 包

```
  ctx desc                  data desc
┌─────────────┐    ┌─────────────────────────┐
│ tdes0: 0    │    │ tdes0: buf_addr_lo      │
│ tdes1: 0    │    │ tdes1: buf_addr_hi      │
│ tdes2: 0    │    │ tdes2: len | IOC | VTIR │
│ tdes3:      │    │ tdes3: FD=1 LD=1 OWN=1  │
│  CTXT=1     │    │  CIC (如果需要csum)      │
│  VLTV=1     │    └─────────────────────────┘
│  vtag_id    │
└─────────────┘
  无实际buffer, len=-1
```

#### 2. TSO 包

```
  ctx desc (TSO meta)           data desc 0 (FD, header)    data desc 1..N-1         data desc N (LD)
┌──────────────┐  ┌──────────────────────────┐  ┌──────────────────┐  ┌──────────────────────┐
│ tdes0: 0     │  │ tdes0: buf_addr_lo       │  │ tdes0: addr_lo   │  │ tdes0: addr_lo       │
│ tdes1: 0     │  │ tdes1: buf_addr_hi       │  │ tdes1: addr_hi   │  │ tdes1: addr_hi       │
│ tdes2: MSS   │  │ tdes2: len               │  │ tdes2: len       │  │ tdes2: len | IOC     │
│ tdes3:       │  │ tdes3: FD=1 TSE=1        │  │ tdes3: OWN=1     │  │ tdes3: LD=1 OWN=1    │
│  CTXT=1      │  │  THL(TCP hdr/4) CIC      │  │                  │  │                      │
│  TCMSSV=1    │  │  TPL(payload len)        │  │                  │  │                      │
└──────────────┘  └──────────────────────────┘  └──────────────────┘  └──────────────────────┘
```

#### 3. PTP 包（MGBE twostep/onestep slave）

```
  ctx desc (PTP meta)              data desc (FD+LD)
┌──────────────────────┐  ┌──────────────────────────────┐
│ tdes0: pkt_id+vdma_id│  │ tdes0: buf_addr_lo           │
│ tdes1: 0             │  │ tdes1: buf_addr_hi           │
│ tdes2: 0             │  │ tdes2: len | IOC | TTSE=1   │
│ tdes3:               │  │ tdes3: FD=1 LD=1 OWN=1       │
│  CTXT=1 PIDV=1       │  │  CIC (如果需要csum)           │
│  OSTC (if onestep)   │  └──────────────────────────────┘
└──────────────────────┘
```

**PTP (EQOS twostep)：不需要 CTXT 描述符**。TTSE 在数据描述符 `tdes2` 设置，时间戳从 TDES0/1 回读。

#### 4. VLAN + TSO + PTP 全部组合

```
  ctx desc (三位合一)              data desc 0 (FD,header)  data desc 1..N(LD)
┌──────────────────────────┐  ┌──────────────────────┐  ┌──────────────────┐
│ tdes0: pkt_id+vdma_id    │  │ tdes0: buf_addr_lo   │  │ tdes0: addr_lo   │
│ tdes1: 0                 │  │ tdes1: buf_addr_hi   │  │ ...              │
│ tdes2: MSS               │  │ tdes2: len           │  │ tdes2: len|IOC   │
│ tdes3: CTXT=1           │  │ tdes3: FD=1 TSE=1    │  │ tdes3: LD=1 OWN  │
│  VLTV=1 vtag_id          │  │  THL TPL CIC          │  │  TTSE (可选)     │
│  TCMSSV=1 PIDV=1         │  └──────────────────────┘  └──────────────────┘
│  OSTC (if onestep)       │
└──────────────────────────┘
```

### 各特性 CTXT 需求总结

| 特性 | 需要 CTXT? | CTXT 内容 | 代码位置 |
|------|----------|---------|---------|
| VLAN | 是 | tag ID + VLTV | `need_cntx_desc:843-853` |
| TSO | 是 | MSS + TCMSSV | `need_cntx_desc:856-864` |
| PTP (MGBE) | 是 | pkt_id + PIDV + vdma_id | `need_cntx_desc:867-883` |
| PTP (EQOS onestep) | 是 | OSTC | `need_cntx_desc:877` |
| PTP (EQOS twostep) | 否 | — | 条件跳过 |
| TSN (slot) | 否 | slot号直接写入FD数据desc THL字段 | `fill_first_desc:1006-1013` |
| CSUM | 否 | CIC bits在FD数据desc | `fill_first_desc:956-964` |

**永远是 0 或 1 个 CTXT 描述符 + N 个数据描述符，不存在多个 CTXT 的情况。**

### 代码体现

`ether_tx_swcx_alloc` 中的判断 (`ether_linux.c:3798-3818`)：

```c
// VLAN / TSO / PTP → 先占用一个 tx_swcx，len=-1
if ((flags & VLAN) || (flags & TSO) || (flags & PTP)) {
    tx_swcx = tx_ring->tx_swcx + cur_tx_idx;
    tx_swcx->len = -1;     // ★ -1 标记: 这是 ctx, 无实际 buffer
    cnt++;
    INCR_TX_DESC_INDEX(cur_tx_idx);
}
// 然后继续正常分配数据描述符 ...
```

`hw_transmit` 中的处理 (`osi_dma_txrx.c:1275-1300`)：

```c
cntx_desc_consumed = need_cntx_desc(tx_pkt_cx, tx_swcx, tx_desc, ...);
if (cntx_desc_consumed == 1) {
    // ★ CTXT 描述符填完 → 跳到下一个位置
    INCR_TX_DESC_INDEX(entry);
    cx_desc = tx_desc;   // 记住，最后才设 OWN
    tx_desc = 下一个;     // 指向 FD 数据描述符
    desc_cnt--;
}
// 继续: FD → 中间 → LD 数据描述符 ...

// ★ 最后统一设 OWN:
first_desc->tdes3 |= TDES3_OWN;
set_context_desc_own_bit(cx_desc, cntx_desc_consumed);
```

## 十二、hw_transmit 如何处理一个 SKB 对应多个描述符

`hw_transmit` 本身不拆分 SKB。拆分在 `ether_tx_swcx_alloc` 完成，`hw_transmit` 只负责"翻译"。

### 两步分工

```
ether_start_xmit(skb)
  │
  ├─ ① ether_tx_swcx_alloc():
  │     把 SKB 拆成 N 个 tx_swcx[] (每段 DMA map + 记录地址)
  │     写入 tx_pkt_cx->desc_cnt = N
  │
  └─ ② osi_hw_transmit(chan) → hw_transmit():
        读取 desc_cnt, 循环把 tx_swcx[i] 翻译为 tx_desc[i]
        设 FD/LD/OWN, 写 tail pointer → HW 启动
```

### ether_tx_swcx_alloc 拆分流程

```
① 如需 VLAN/TSO/PTP → 分配 ctx desc (len=-1)
② 处理线性区: 每 16KB 切一段 → dma_map_single
③ TSO payload 剩余: 每 16KB 一段 → dma_map_single
④ 处理 frags[]: 每 16KB 一段 → dma_map_page, 标记 PAGED_BUF
⑤ 最后一个 swcx->buf_virt_addr = skb (LD 标识)
⑥ tx_pkt_cx->desc_cnt = cnt
```

### 具体示例 (64KB GSO SKB, 3 frags)

```
产生 6 个描述符:
  tx_swcx[0]: len=-1     ← ctx (TSO: MSS+TCMSSV)
  tx_swcx[1]: len=54B    ← header (TCP/IP)
  tx_swcx[2]: len=16KB   ← 线性区 payload
  tx_swcx[3]: len=16KB   ← frag[0] → dma_map_page
  tx_swcx[4]: len=16KB   ← frag[1] → dma_map_page
  tx_swcx[5]: len=16KB   ← frag[2] → dma_map_page, buf_virt_addr=skb ← LD

  desc_cnt = 6
```

## 十三、"最后设置 first OWN" 的 race condition 原理

OWN bit 是 HW 和 SW 之间的互斥锁：OWN=1 表示 HW 拥有，SW 不能碰；OWN=0 表示 SW 拥有。

### 为什么首描述符 OWN 要最后设

HW 的行为是：**一旦看到任何描述符 OWN=1，就从该描述符开始 DMA**，并沿链读至 LD。

```
错误顺序 (有 race):
  时刻 T1: 填 desc[0], 设 OWN=1
           → HW 立刻读取 desc[0], 启动 DMA
           → HW 继续读 desc[1]… 但 desc[1] 还没填完！
           → HW 读到脏数据 → DMA 到错误地址 → 数据损坏

正确顺序:
  时刻 T1: 先填 desc[1], desc[2]… 全部设 OWN=1
           (中间 desc 设 OWN 安全, 因为 HW 从链头开始读,
            desc[0] OWN 还是 0, HW 根本不会往下走)
  时刻 T2: 填 desc[0] 全部字段 (除 OWN)
  时刻 T3: desc[0]->tdes3 |= TDES3_OWN   ← ★ 最后设
           dmb_oshst()                     ← 内存屏障
           写 tail pointer                  ← 触发 HW
```

代码实现 (`osi_dma_txrx.c:1323-1388`)：

```c
// 中间描述符: 立刻设 OWN
for (i = 0; i < desc_cnt; i++) {
    tx_desc->tdes3 |= TDES3_OWN;  // 安全, HW 还没从链头开始读
    INCR_TX_DESC_INDEX(entry);
}

// ★ 关键步骤:
first_desc->tdes3 |= TDES3_OWN;    // (1) 首 desc 最后设 OWN
cx_desc->tdes3 |= TDES3_OWN;       // (2) ctx desc 最后设 OWN
dmb_oshst();                        // (3) 内存屏障, 确保 HW 可见
tx_ring->cur_tx_idx = entry;        // (4) SW 指针更新
osi_dma_writel(tailptr, ...);       // (5) 触发 HW — 比赛开始
```

## 十四、PTP 硬件时间戳存储路径

TX 时间戳不在描述符里，而是通过独立的异步路径获取。

### 完整存储链路

```
┌─ HW 层 ───────────────────────────────────────────┐
│ MGBE_MAC_TSS     ← Timestamp Status (TXTSC bit)   │
│ MGBE_MAC_TSNSSEC ← 纳秒                            │
│ MGBE_MAC_TSSEC   ← 秒                              │
│ MGBE_MAC_TSPKID  ← 包ID (与ctx描述符对应)           │
└───────────────────────────────────────────────────┘
              ↓ MAC 中断 (MGBE_ISR_TSIS)
┌─ SW 链表 ─────────────────────────────────────────┐
│ l_core->ts[] = 固定数组 (MAX_TX_TS_CNT个)          │
│ l_core->tx_ts_head = 双向链表头                     │
│   每节点: { pkt_id, vdma_id, sec, nsec }           │
└───────────────────────────────────────────────────┘
              ↓ workq 周期性轮询 (每 1ms)
┌─ 用户态 ──────────────────────────────────────────┐
│ skb_tstamp_tx(skb, &shhwtstamp)                    │
│   → sock_queue_err_skb() → recvmsg()               │
└───────────────────────────────────────────────────┘
```

### 流程

```
① 发送: ctx 描述符写入 pkt_id → HW 记住

② DMA 完成: HW 把 {nsec, sec, pkt_id} 写入 MAC 时间戳寄存器
   → 触发 MAC 中断 MGBE_ISR_TSIS
   → mgbe_handle_mac_intrs() 读寄存器
   → 存入 l_core->ts[] 链表

③ workq 轮询 (ether_get_tx_ts_work, 每 1ms):
   ether_get_tx_ts() → get_tx_ts()
     遍历 l_core->tx_ts_head 匹配 pkt_id
     → shhwtstamp.hwtstamp = ns_to_ktime(nsec)
     → skb_tstamp_tx(skb, &shhwtstamp)

④ 时间戳到达后释放 SKB
```

### 为什么不放描述符里

```
DMA 路径: 描述符写完 → DMA 发送 → 描述符快速回收 (微秒级)
PTP 路径: 帧发出 → PHY 打戳 → 异步上报 MAC 寄存器 (可能延迟)

如果放描述符: 描述符必须等时间戳回来才能回收 → 严重阻塞 TX ring
异步模式: 描述符立即回收 (标记 TX_DONE_CX_TS_DELAYED), 时间戳后取
```

## 十五、VDMA、PDMA、虚拟化 (VM) 三者的关系

### VDMA ≠ 虚拟化

**VDMA (Virtual DMA Channel) 是硬件内部概念**，名字中的 "Virtual" 相对 "Physical DMA"，指一个 PDMA 内部复用出的多条逻辑通道。与 Tegra HV 虚拟机隔离无关。

### PDMA ↔ VDMA 分层

```
PDMA (Physical DMA) = AXI Master, 真正做数据搬运的硬件引擎
  ├─ VDMA0 → tx_ring[0], rx_ring[0], IRQ0, MTL Queue 0
  ├─ VDMA1 → tx_ring[1], rx_ring[1], IRQ1, MTL Queue 1
  ├─ VDMA2 → tx_ring[2], rx_ring[2], IRQ2, MTL Queue 2
  └─ VDMA3 → tx_ring[3], rx_ring[3], IRQ3, MTL Queue 3
```

### 关键：MTL Queue ↔ VDMA (不是 PDMA)

```c
// ether_linux.c:4002 — start_xmit
qinx = skb_get_queue_mapping(skb);
chan = osi_dma->dma_chans[qinx];    // qinx → VDMA channel
tx_ring = osi_dma->tx_ring[chan];   // 每个 VDMA 有独立描述符环

// ether_linux.c:3986 — select_queue
mtlq = osi_core->dma_chans[i];     // dma_chans[i] 存的其实是 MTL 队列号
```

一个 MTL 队列 1:1 绑定一个 VDMA，不是一个 PDMA。TSN 调度链：

```
SKB → netdev TX queue → VDMA → tx_ring (描述符环)
                             ↓ 1:1
                        MTL 队列 → EST 门控/FPE 抢占/CBS 整形 → MAC TX
```

### VDMA vs PDMA vs VM

| | VDMA | PDMA | VM |
|------|------|------|----|
| 是什么 | DMA 逻辑通道 | 物理 DMA 引擎 | Hypervisor 虚拟机隔离 |
| 关系 | N:1 绑定 PDMA | 实际搬运工 | 分配 VDMA 资源给不同 VM |
| 谁做数据搬运 | 不做 | 通过 AXI 读写内存 | 不参与 |

**无论虚拟化开没开，VDMA 都是 MAC 硬件里同一个东西。** 虚拟化只是决定哪个 VM 可以操作哪些 VDMA 寄存器的子集。

## 十六、`osd_transmit_complete` 详解

回调函数**每个描述符调用一次**，但关键操作只在 LD 描述符执行。

### 对每个描述符都执行

```c
void osd_transmit_complete(priv, swcx, txdone_pkt_cx) {

    // ① 更新发送字节统计 (每描述符)
    ndev->stats.tx_bytes += swcx->len;

    // ② DMA unmap (每描述符)
    if (swcx->buf_phy_addr != 0) {
        if (PAGED_BUF)
            dma_unmap_page(...);    // frag buffer
        else
            dma_unmap_single(...);  // linear buffer
    }
```

### 仅 LD 描述符执行（skb != NULL）

```c
    // 只有最后一个描述符的 buf_virt_addr 指向 SKB
    if (skb) {
        // ③ 唤醒 TX 队列 (反压解除)
        txq = netdev_get_tx_queue(ndev, qinx);
        if (队列被stop && avail_desc > THRESHOLD)
            netif_tx_wake_queue(txq);

        ndev->stats.tx_packets++;

        // ④ PTP 时间戳 — 两条路径
        if (TX_DONE_CX_TS_DELAYED) {
            // MGBE: 时间戳还没回来 → 挂链表等
            add_skb_node(skb, pkt_id, vdma_id);
            ether_get_tx_ts();     // 立即尝试
            schedule_delayed_work(tx_ts_work, 1ms); // 周期性重试
        } else {
            // EQOS 或无 PTP: 直接释放
            dev_consume_skb_any(skb);
        }
    }
}
```

### PTP 双路径对比

```
EQOS (时间戳在描述符里):
  update_tx_done_ts() 从 TDES0/1 读 nsec
    → osd_transmit_complete() 中调用 skb_tstamp_tx()
    → 当场释放 SKB

MGBE (异步时间戳):
  标记 TX_DONE_CX_TS_DELAYED
    → osd_transmit_complete() 中不调用 skb_tstamp_tx
    → SKB 挂到 tx_ts_skb_head 链表
    → workq 每 1ms 轮询 l_core->tx_ts_head 链表
    → 匹配到 pkt_id → skb_tstamp_tx() → 释放 SKB
```

## 十七、TTSE 位与 TSN 的关系

**TTSE (bit30, TDES2)：Transmit Timestamp Enable**。

TTSE 是 TSN 时间同步基础设施，不仅服务于 PTP：

```
IEEE 802.1AS (gPTP)  ← TSN 的时间同步协议
IEEE 1588 (PTP)      ← gPTP 的协议基础

TTSE bit → HW 在帧离开 PHY 时抓取精确时间戳
  ├─ → PTP 协议做时钟同步 (802.1AS)
  ├─ → EST GCL 门控的 Base Time 基准
  ├─ → CBS credit 计算的时间基准
  └─ → FPE fragment 重组的时间窗口
```

### TSN 使用与 PTP 相同的描述符

TSN **不使用独立描述符**。描述符层面不区分 TSN/非 TSN。TSN 在更高层实现：

```
                         TSN 控制层 (MTL 寄存器)
┌──────────────────────────────────────────────────────┐
│ EST: GCL[256] + Base Time + Cycle Time 寄存器         │
│ FPE: 哪些队列可被抢占 + 残余队列寄存器                   │
│ CBS: 每队列 credit 整形参数                            │
│           ↓ 控制哪些队列何时可以发送                     │
└──────────────────────────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────┐
│                 DMA 描述符层 (完全不变)                 │
│  tx_ring[0] → [desc0][desc1]... (express/preempt)   │
│  tx_ring[N] → [desc0][desc1]... (RQ)                 │
│                                                       │
│  描述符格式: 完全相同                                   │
│  回收逻辑:   完全相同 (osi_process_tx_completions)     │
│  唯一区别:   slot_number 字段                          │
└──────────────────────────────────────────────────────┘
```

## 十八、ether_init_rss 详解

### 函数作用

初始化 RSS (Receive Side Scaling)，让 HW 根据五元组哈希将 RX 流量分发到多个 DMA 通道。

### 逐行分析

```c
static void ether_init_rss(struct ether_priv_data *pdata,
                           netdev_features_t features)
{
    struct osi_core_rss *rss = &ioctl_data.data.rss;
    unsigned int num_q = osi_core->num_mtl_queues;

    // ① 检查内核特性: 未启用 NETIF_F_RXHASH → 直接退出
    if (features & NETIF_F_RXHASH) rss->enable = 1;
    else { rss->enable = 0; return; }

    // ② 生成 40 字节随机哈希密钥 (Toeplitz 算法输入)
    netdev_rss_key_fill(rss->key, sizeof(rss->key));

    // ③ 确定重定向表队列数
    if (osi_core->mac == OSI_MAC_HW_MGBE_T26X)
        num_q = pdata->osi_dma->num_dma_chans;

    // ④ 初始化 128 项重定向表 → table[i] = i % num_q
    for (i = 0; i < OSI_RSS_MAX_TABLE_SIZE; i++)
        rss->table[i] = ethtool_rxfh_indir_default(i, num_q);

    // ⑤ 下发 OSI 层写硬件寄存器
    ioctl_data.cmd = OSI_CMD_CONFIG_RSS;
    osi_handle_ioctl(osi_core, &ioctl_data);
}
```

### 硬件寄存器写入 (mgbe_config_rss)

```
① 跳过单队列: num_mtl_queues == 1 → 直接退出
② 写 40B 哈希密钥 → MGBE_MAC_RSS_DATA (10次×4B)
③ 写 128 项重定向表 → MGBE_MAC_RSS_DATA
④ 启用 RSS: MGBE_MAC_RSS_CTRL |= RSSE | TCP4TE | UDP4TE | IP2TE
```

### RSS 数据流

```
配置:
  ether_init_rss()
    └─ 随机 key + table[128] → HW RSS 寄存器

接收:
  HW 收到帧 → 解析五元组 → Toeplitz(key, 五元组)
    → hash 低 7 位 → table[index] = queue#
    → DMA 到对应 VDMA rx_desc 环
    → RX desc 写回: rdes1 = hash, OSI_PKT_CX_RSS 标记
    → osd_receive_packet → skb_set_hash(skb, hash)
    → 内核协议栈 flow steering
```

### 为什么 Key 固定但流会分散到不同 CPU

**Key 不变是故意的，五元组每条流不同**：

```
流 A: {192.168.1.1, 10.0.0.1, 12345, 80, TCP}
      → Toeplitz(fixed_key, 五元组) → hash=0x3A → table[58]=0 → VDMA0

流 B: {192.168.1.2, 10.0.0.2, 54321, 443, TCP}
      → Toeplitz(fixed_key, 五元组) → hash=0x8F → table[15]=3 → VDMA3

流 A 下一个包 (同五元组):
      → Toeplitz(fixed_key, 五元组) → hash=0x3A → 同一队列 → 保序
```

```
固定的 Key → 同流同队列 (TCP 不乱序)
变化的五元组 → 异流不同队列 → 不同 IRQ → 不同 CPU → 并行处理
```
| `hardware/include/osi_core.h` | `osi_pdma_vdma_data`、`osi_core_priv_data` (MTL队列/PDMA配置) |
| `DWC_25gmac_databook.pdf` | Synopsys 25GMAC Databook v4.20a — DMA Controller(§2.4)、Descriptors(§13)、MTL(§2.5)、MAC(§2.6) |

---

## 六、一个 SKB 到多个描述符: `ether_start_xmit` 完整流程

### 6.1 调用路径

```
ether_start_xmit(skb, ndev)                     // Linux ndo_start_xmit 回调
  │
  ├─ ether_tx_swcx_alloc(pdata, tx_ring, skb)   // 步骤1: SKB → tx_swcx[] 拆分
  │
  └─ osi_hw_transmit(osi_dma, chan)              // 步骤2: 提交到硬件
       └─ hw_transmit(osi_dma, tx_ring, chan)    // 填充硬件描述符
```

单描述符最大数据量: `ETHER_TX_MAX_BUFF_SIZE = 0x3FFF = 16383` 字节。超过此限制（如 TSO/GSO 包可达 64KB）必须拆分到多个描述符。

### 6.2 步骤1: `ether_tx_swcx_alloc` — SKB 拆分

**文件**: `ether_linux.c:3750`

将**一个 SKB** 拆分成多个 `tx_swcx[]` 条目，总数记录在 `tx_pkt_cx->desc_cnt`。

#### 阶段A: Context 描述符 (可选)

```c
// ether_linux.c:3802-3818
// VLAN / TSO / PTP 包需要一个 context 描述符
if (VLAN || TSO || PTP) {
    tx_swcx->len = -1;   // 标记为 context 描述符
    cnt++;
}
```

#### 阶段B: 线性缓冲区 (skb_head)

```c
// ether_linux.c:3820-3860
len = skb_headlen(skb);  // SKB 线性区长度
while (valid_tx_len(len)) {
    size = min(len, ETHER_TX_MAX_BUFF_SIZE);  // 每 desc 最多 16383 字节
    tx_swcx->buf_phy_addr = dma_map_single(dev, skb->data + offset, size, DMA_TO_DEVICE);
    tx_swcx->len = size;
    len -= size;
    offset += size;
    cnt++;
}
```

#### 阶段C: Scatter/Gather 分片

```c
// ether_linux.c:3907-3949
num_frags = skb_shinfo(skb)->nr_frags;
for (i = 0; i < num_frags; i++) {
    len = skb_frag_size(&frags[i]);
    while (valid_tx_len(len)) {
        size = min(len, ETHER_TX_MAX_BUFF_SIZE);
        tx_swcx->buf_phy_addr = dma_map_page(dev, frag_page, page_offset, size, DMA_TO_DEVICE);
        tx_swcx->len = size;
        cnt++;
    }
}
```

#### 描述符数量公式

```
cnt = ctx (1 if VLAN/TSO/PTP, else 0)
    + N (线性区分片)
    + M (frag 分片)

每个包总描述符数 = cnt
```

#### 示例: 64KB TSO 包

```
SKB (64KB TSO)
├─ skb_head (线性区, 50KB)
│   ├─ desc[0]: context (TSO flags, MSS)
│   ├─ desc[1]: FD, 16383 bytes  ← dma_map_single
│   ├─ desc[2]: 16383 bytes
│   ├─ desc[3]: 16383 bytes
│   └─ desc[4]: 3851 bytes
├─ frag[0] (8KB)
│   └─ desc[5]: 8192 bytes       ← dma_map_page
└─ frag[1] (6KB)
    └─ desc[6]: LD, IOC, 6144 bytes  ← 最后一个描述符

desc_cnt = 7 (1 ctx + 6 data)
```

### 6.3 步骤2: `hw_transmit` — 硬件描述符编程

**文件**: `hardware/osi/dma/osi_dma_txrx.c:1212`

遍历所有已准备的 `tx_swcx[]`，逐条写入硬件描述符寄存器。

#### 关键硬件写入位置

| 步骤 | 代码行 | 操作 |
|------|--------|------|
| Context desc | 1277-1307 | `TDES3_CTXT`, VLAN tag, MSS, PTP flags |
| 第一个数据 desc | 1310 | `fill_first_desc()` → `TDES3_FD`, checksum offload |
| 剩余 desc 循环 | 1323-1341 | `tdes0/1 = buf_phy_addr`, `tdes2 = len`, `TDES3_OWN` |
| 最后 desc 标记 | 1347 | `last_desc->tdes3 \|= TDES3_LD` |
| IOC 设置 | 1352 | `last_desc->tdes2 \|= TDES2_IOC` (中断) |
| OWN bit (最后) | 1362-1363 | 先设 FD 的 OWN，再设 context 的 OWN (防竞态) |
| Tail pointer 写 | 1388 | `osi_dma_writel(tailptr, base + TDTP)` ← 触发 DMA |

#### 描述符字段映射

```
tx_swcx (软件上下文)              tx_desc (硬件描述符)
───────────────────────           ─────────────────────
buf_phy_addr[31:0]      ───────► tdes0 (Buffer Address Low)
buf_phy_addr[63:32]     ───────► tdes1 (Buffer Address High)
len                     ───────► tdes2 (Buffer Length)
PTP pktid               ───────► tdes0 (Packet ID bits)
tx_pkt_cx->flags        ───────► tdes3 (FD, LD, CIC, CTXT, OWN 等)
```

---

## 七、硬件描述符 vs 软件上下文

### 7.1 区别

| 维度 | `tx_desc` / `rx_desc` | `tx_swcx` / `rx_swcx` |
|------|----------------------|------------------------|
| **类型** | 硬件寄存器格式 (4×u32) | 软件管理结构 |
| **写入者** | CPU 写 (TX 提交) / HW 写 (RX 完成) | CPU 独有 |
| **硬件可见** | ✅ DMA 一致性内存 | ❌ 普通内核内存 |
| **内容** | DMA 地址、长度、OWN/FD/LD 标志 | 虚拟地址、DMA 映射信息、状态 flags |
| **生命周期** | 被 HW 持续循环使用 | 匹配 SKB 生命周期 (map → unmap → free) |
| **数组大小** | `OSI_EQOS_TX_DESC_CNT` (1024) / MGBE (4096) | 与 tx_desc 一一对应 |

### 7.2 内存布局

```
TX Ring:
  tx_ring->tx_desc[0..N-1]   ← DMA coherent memory, 硬件读写
  tx_ring->tx_swcx[0..N-1]   ← 普通内核内存, 仅 CPU 访问

  tx_desc[i] ←─── 一一对应 ───→ tx_swcx[i]
  (同一索引, 同一包, 不同用途)
```

### 7.3 关键数据结构

**`struct osi_tx_swcx`** (`hardware/include/osi_dma.h:450`):
```c
struct osi_tx_swcx {
    u64 buf_phy_addr;    // DMA 映射后的物理地址 → 复制到 tx_desc->tdes0/tdes1
    void *buf_virt_addr; // 内核虚拟地址 → CPU 访问/释放使用
    u32 len;             // 缓冲区长度 → 复制到 tx_desc->tdes2
    u32 flags;           // OSI_PKT_CX_PAGED_BUF → 区分 dma_unmap_single/page
    u32 pktid;           // PTP 包 ID
    u32 vdmaid;          // VDMA 通道 ID
};
```

**`struct osi_rx_swcx`** (`hardware/include/osi_dma.h:353`):
```c
struct osi_rx_swcx {
    u64 buf_phy_addr;    // 预分配的 DMA 缓冲区物理地址
    void *buf_virt_addr; // 内核虚拟地址 → 交给网络栈
    u32 len;             // 缓冲区长度
    u32 flags;           // REUSE / BUF_VALID / PROCESSED 状态
    u64 data_idx;        // nvsocket 数据索引
};
```

---

## 八、RX 描述符: 接收方向

### 8.1 RX Ring 初始化

**文件**: `hardware/osi/dma/osi_dma_txrx.c:1415` (`rx_dma_desc_initialization`)

```c
// 填充每个描述符
for (i = 0; i < osi_dma->rx_ring_sz; i++) {
    rx_desc->rdes0 = L32(rx_swcx->buf_phy_addr);  // 缓冲区地址低32位
    rx_desc->rdes1 = H32(rx_swcx->buf_phy_addr);  // 高32位
    rx_desc->rdes3 = RDES3_IOC | RDES3_B1V;       // 中断 + buffer1有效
    rx_desc->rdes3 |= RDES3_OWN;                  // 交给硬件
}

// 写硬件寄存器
osi_dma_writel(ring_len,  base + RDRL);  // Ring 长度
osi_dma_writel(desc_hi,   base + RDLH);  // 描述符基址高32位
osi_dma_writel(desc_lo,   base + RDLA);  // 描述符基址低32位
```

### 8.2 RX 数据流 (硬件 → CPU)

```
硬件写入数据到 buffer
  → RDES3_OWN = 0 (释放给 CPU)
  → RDES3_FD, RDES3_LD 标记包边界
  → RDES3_RS0V, RDES3_LT 用于 VLAN 提取
  → RDES3_CDA 如果后面跟着 PTP context 描述符

osi_process_rx_completions()                     // osi_dma_txrx.c:304
  │
  ├─ 检查: (rx_desc->rdes3 & RDES3_OWN) == 0  → 硬件已释放
  ├─ 检查: (rdes3 & FD) && (rdes3 & LD)       → 单描述符完整包
  ├─ process_rx_desc()                          // line 422
  │   ├─ get_rx_csum(rx_desc, rx_pkt_cx)       // checksum 从 normal desc
  │   ├─ get_rx_vlan(rx_desc, rx_pkt_cx)       // VLAN 从 normal desc 字段
  │   ├─ get_rx_hwstamp(rx_desc, context_desc) // PTP 从相邻 context desc
  │   └─ osd_ops.receive_packet()              // 交给网络栈
  │
  └─ cur_rx_idx 步进 (normal=1, PTP=2 [normal+context])
```

---

## 九、Context vs Normal 描述符组织

### 9.1 设计原则: TX 每个包最多一个 Context，RX 只有 PTP 需要 Context

**TX 方向** — 所有硬件 offload 特性共享**一个** context 描述符:

`need_cntx_desc()` (`osi_dma_txrx.c:833-884`) 将 VLAN + TSO + PTP 合并到**同一个** context desc:

```c
// 三个 if 是累加关系，不是互斥的——它们修改同一个 tx_desc:
if (VLAN) → tx_desc->tdes3 |= TDES3_CTXT | vtag_id | TDES3_VLTV; ret = 1;
if (TSO)  → tx_desc->tdes3 |= TDES3_CTXT; td2 |= mss; TDES3_TCMSSV; ret = 1;
if (PTP)  → tx_desc->tdes3 |= TDES3_CTXT; if (onestep) TDES3_OSTC; ret = 1;
```

### 9.2 每种包的 TX 描述符排列

| 包类型 | 描述符链 |
|--------|----------|
| 纯数据 | `[FD][LD]` (2 desc) |
| VLAN | `[CTXT(V)] [FD][LD]` (3 desc) |
| TSO (如 64KB) | `[CTXT(T)] [FD] [...] [LD]` (1+N desc) |
| PTP (EQOS 2-step) | `[FD][LD]` (2 desc, CTXT 合并入 FD) |
| PTP (其他) | `[CTXT(P)] [FD][LD]` (3 desc) |
| VLAN + TSO + PTP | `[CTXT(ALL)] [FD] [...] [LD]` (1+N desc, **一个合并的 CTXT**) |

> 唯一例外: EQOS MAC + PTP two-step 模式不需要 context 描述符，PTP 信息走 normal desc 的 TDES0 字段。

### 9.3 RX Context 描述符 — 仅 PTP 需要

RX 更简单 — **只有 PTP** 需要一个额外的 context 描述符:

| 包类型 | 描述符链 |
|--------|----------|
| 纯数据 | `[FD/LD]` |
| VLAN | `[FD/LD]` (VLAN 信息嵌入 normal desc) |
| 多 buffer (>MTU) | `[FD] [...] [LD]` (跨多个 normal desc) |
| PTP | `[FD/LD] [CTXT(timestamp)]` (CTXT 紧跟 normal 之后) |
| VLAN + PTP | `[FD/LD] [CTXT(timestamp)]` (VLAN 在 normal, PTP 在 context) |

**RX VLAN** 直接从 normal 描述符字段提取，**不需要**额外描述符:

```c
// EQOS: eqos_desc.c:45
if ((rx_desc->rdes3 & RDES3_RS0V) == RDES3_RS0V) {
    lt = rx_desc->rdes3 & RDES3_LT;            // 读 normal desc 的类型字段
    rx_pkt_cx->vlan_tag = rx_desc->rdes0 & RDES0_OVT;  // 读 normal desc 的 VLAN ID
}

// MGBE: mgbe_desc.c:48
ellt = rx_desc->rdes3 & RDES3_ELLT;
if (ellt & RDES3_ELLT_CVLAN) { vlan = rx_desc->rdes0 & RDES0_OVT; }
```

**RX PTP**: 硬件在 normal 描述符中设 `RDES3_CDA` (Context Descriptor Available) 标志，并在其后紧跟一个 timestamp context 描述符:

```c
// osi_dma_txrx.c:143
context_desc = rx_ring->rx_desc + rx_ring->cur_rx_idx;  // 相邻槽位

// context_desc->rdes3: CTXT | TSA (OWN=0)
// context_desc->rdes0/rdes1: 纳秒时间戳

// 消费后 cur_rx_idx 步进 2 (normal + context)
```

### 9.4 多包 Ring 布局

**TX Ring** (线性视角):

```
[CTXT₁][FD₁][LD₁][CTXT₂][FD₂][LD₂][CTXT₃][FD₃][LD₃] ...
 ←── pkt1 ──→ ←─── pkt2 ──→ ←─── pkt3 ──→

纯数据包 (无 VLAN/TSO/PTP 或 EQOS PTP 2-step):
[FD₁][LD₁][FD₂][LD₂][FD₃][LD₃] ...
```

**RX Ring** (线性视角):

```
普通/VLAN:
[FD/LD₁][FD/LD₂][FD/LD₃] ...
 ← pkt1 →← pkt2 →← pkt3 →

PTP:
[FD/LD₁][CTXT_t₁][FD/LD₂][CTXT_t₂][FD/LD₃] ...
 ← pkt1 →          ← pkt2 →          ← pkt3 →

处理后: cur_rx_idx PTP 步进 2, 普通步进 1
```

### 9.5 总结表

| 特性 | TX: Context 描述符? | TX: Normal 数量 | RX: Context 描述符? | RX: Normal 数量 |
|------|--------------------|-----------------|--------------------|-----------------|
| **普通** | 否 | 1 (FD/LD 合并) | 否 | 1 (FD/LD 合并) |
| **VLAN** | **是** (1) | 1+ | **否** (嵌入 normal) | 1 |
| **TSO** | **是** (1) | ceil(total_len/16383) | N/A (仅 TX) | N/A |
| **PTP** | **是** (1)* | 1+ | **是** (1 CTXT 在 normal 后) | 1 |
| **VLAN+TSO** | **是** (1 合并) | ceil(total_len/16383) | N/A | N/A |
| **VLAN+PTP** | **是** (1 合并) | 1+ | **是** (1 CTXT 在 normal 后) | 1 |

> \* 除 EQOS PTP 2-step 外

---

## 十、关键常量和源文件索引

### 常量

| 常量 | 值 | 位置 |
|------|-----|------|
| `ETHER_TX_MAX_BUFF_SIZE` | 0x3FFF (16383) | `ether_linux.h:233` |
| `ETHER_TX_MAX_FRAME_SIZE` | `GSO_MAX_SIZE` (65536) | `ether_linux.h:238` |
| `ETHER_TX_DESC_THRESHOLD` | 队列停止阈值 | `ether_linux.c:4039` |

### 涉及源文件

| 文件 | 作用 |
|------|------|
| `ether_linux.c:3750` | `ether_tx_swcx_alloc()` — SKB → swcx 拆分 |
| `ether_linux.c:3997` | `ether_start_xmit()` — 主 TX 入口 |
| `ether_linux.h:233` | `ETHER_TX_MAX_BUFF_SIZE` 定义 |
| `hardware/osi/dma/osi_dma_txrx.c:833` | `need_cntx_desc()` — context desc 判定 |
| `hardware/osi/dma/osi_dma_txrx.c:932` | `fill_first_desc()` — 第一个数据描述符 |
| `hardware/osi/dma/osi_dma_txrx.c:1212` | `hw_transmit()` — 硬件描述符编程 |
| `hardware/osi/dma/osi_dma_txrx.c:1415` | `rx_dma_desc_initialization()` — RX ring 初始化 |
| `hardware/osi/dma/osi_dma_txrx.c:304` | `osi_process_rx_completions()` — RX 完成处理 |
| `hardware/osi/dma/osi_dma_txrx.c:108` | `process_rx_desc()` — 单个 RX desc 处理 |
| `hardware/include/osi_dma.h:353` | `struct osi_rx_swcx` |
| `hardware/include/osi_dma.h:450` | `struct osi_tx_swcx` |
| `hardware/include/osi_dma.h:567` | `struct osi_tx_ring` |
| `hardware/osi/dma/eqos_desc.c:39` | `eqos_get_rx_vlan()` — VLAN 提取 |
| `hardware/osi/dma/eqos_desc.c:180` | `eqos_get_rx_hwstamp()` — PTP 时间戳提取 |
| `hardware/osi/dma/mgbe_desc.c:43` | `mgbe_get_rx_vlan()` |
| `hardware/osi/dma/mgbe_desc.c:226` | `mgbe_get_rx_hwstamp()` |