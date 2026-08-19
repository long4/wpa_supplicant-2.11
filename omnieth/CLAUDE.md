# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

This is a Linux kernel module built as part of a larger kernel tree. The top-level Makefile produces `omnieth.ko` via Kbuild (CONFIG_OMNI_APB_EMAC). Build commands depend on the surrounding kernel build system; there is no standalone cross-compiler invocation in this directory.

Key build flags set in the Makefile:
- `-DLINUX_OS -DOBPKCS_MACSEC` — always set
- `-DMACSEC_SUPPORT` — enables MACsec (always on in current config)
- `-DBW_TEST` — enables bandwidth test infrastructure
- `-DCONFIG_OMNI_PTP` — conditional PTP (default y)
- `-Werror -Wframe-larger-than=4096 -mno-outline-atomics` — always set

Additional flags from `hardware/include/config.tmk`:
- `-DOSI_STRIPPED_LIB` — only when `IS_RELEASE=1`; strips ethtool WOL/EEE/self-test ops
- `-DOSI_DEBUG -DDEBUG_MACSEC` — only in non-release builds (`IS_RELEASE != 1`)
- `-DLOG_OSI -DPHY_PROG -DMACSEC_KEY_PROGRAM` — always set
- `-DOMNI_TEST` — only when `IS_OMNI_TEST=1`

Invocations follow the kernel convention: `make IS_RELEASE=1` for release, `make IS_OMNI_TEST=1` for test builds.

## Architecture

The driver follows a 2-layer split: **Linux OS-dependent (OSD) layer** and **hardware abstraction (OSI) layer**.

### Top-level files — Linux OSD layer

Each file maps to a kernel subsystem or `struct net_device` callback:

| File | Role |
|---|---|
| `ether_linux.c` | Module init/exit, `ether_probe/_remove/_shutdown`, `ether_open/_close`, `ether_start_xmit`, NAPI poll, IRQ handling (VM ISR), work queues, ndo callbacks |
| `osd.c` | OSD callback implementations invoked by the OSI layer: `osd_transmit_complete`, `osd_receive_packet`, `osd_ivc_send_cmd`, `osd_restart_lane_bringup`, TX timestamping list management |
| `ethtool.c` | Ethtool ops (get/set strings, stats, pause, WOL, EEE, coalesce, RSS, self-tests) |
| `ether_tc.c` | TC offload: TAPRIO (802.1Qbv EST / 802.1Qbu FPE) and CBS (802.1Qav credit-based shaper) |
| `sysfs.c` | Sysfs attributes: register dump, descriptor dump, MAC loopback, PTP config, error stats, pad calibration |
| `ioctl.c` | Private IOCTL dispatch via `SIOCDEVPRIVATE`: AVB config, VLAN filter, L3/L4 filter, ARP offload, PTP offload, EST/FPE/FRP config, L2 filter, pad calibration, TSC PTP |
| `ptp.c` | PTP clock registration, `get_time/set_time/adjust_freq/adjust_time`, `hwtstamp_ioctl` |
| `macsec.c` | MACsec generic netlink user-space interface: init/deinit, create/enable/disable TX/RX SAs, replay protection, cipher config, TZ key table ops |
| `selftests.c` | MAC loopback self-test, MMC counter verification |
| `ether_test_tx.c` | Test packet TX for bandwidth testing |
| `ether_callback_export.h` | Doxygen-documented function prototypes for all net_device and platform driver callbacks |
| `ether_export.h` | Private IOCTL command codes, struct definitions for ARP/PTP/L2 filter offload, MACsec netlink command enum |
| `ether_linux.h` | `struct ether_priv_data` (the per-device private data), NAPI structs, VM IRQ structs, IVC context, constants, inline helpers. Includes `ioctl.h` for private IOCTL structs and conditionally includes `macsec.h`. |

### hardware/ — OSI (hardware abstraction) layer

```
hardware/include/          # Shared headers: osi_core.h, osi_dma.h, osi_macsec.h, mmc.h, ivc_core.h, oeth_export.h
hardware/osi/core/         # MAC core: osi_core.c, osi_hal.c, core_common.c, common_macsec.c,
                           #   eqos_core.c, mgbe_core.c, ivc_core.c,
                           #   xpcs.c, frp.c, vlan_filter.c, eqos_mmc.c, mgbe_mmc.c, debug.c
hardware/osi/dma/          # DMA engine: osi_dma.c, osi_dma_txrx.c,
                           #   eqos_dma.c, mgbe_dma.c, eqos_desc.c, mgbe_desc.c, debug.c
hardware/osi/macsec/       # MACsec engine: dwc_macsec.c + dwc_macsec_reg.h (DWC HW impl),
                           #   macsec.c + macsec.h (OSI-layer helpers/register map),
                           #   macsec_debugfs.c, libnvmacsecrm.export
hardware/osi/phy/          # PCS/PHY: oxpcs.c
```

Two MAC IP variants are supported:
- **EQOS** (Enhanced QOS) — typically 1G/2.5G
- **MGBE** (Multi-Gigabit Ethernet) — 10G/25G, multiple instances (mgbe0–mgbe3)

The OSI layer exposes operations through `struct osi_core_priv_data` and `struct osi_dma_priv_data`. The pattern is:
1. Linux layer populates `struct osi_ioctl` with command + data
2. Calls `osi_handle_ioctl(core, cmd, &ioctl_data)`
3. OSI layer validates and programs hardware registers

OSI → OSD callbacks are registered via `ether_assign_osd_ops()` and include: TX/RX completion, lane restart, PAD calibration, IVC messaging.

### MACsec architecture (three layers)

MACsec involves files at three levels — be careful not to confuse the two `macsec.c`/`macsec.h` pairs:

1. **Linux OSD** — `macsec.c` + `macsec.h` (top-level):
   - Generic netlink family (`OB_MACSEC_GENL_VERSION`) for userspace MACsec control
   - `struct macsec_priv_data` holds per-device MACsec state (clocks, resets, IRQs, enabled ANs, cipher, supplicant tracking)
   - Netlink attribute policies for SA config (`ob_macsec_sa_attrs`), TZ key table (`ob_macsec_tz_attrs`), and SCI/MAC/AN/PN/key parameters
   - PKCS key wrapping (`OBPKCS_MACSEC`): wrapped SAK + KEK handle instead of raw keys
   - Standard MACsec offload (`CONFIG_MACSEC`): optional `macsec_ctx` for kernel MACsec ops
   - Key functions: `macsec_probe`, `macsec_open/_close`, `macsec_suspend/_resume`

2. **OSI core dispatch** — `hardware/osi/core/common_macsec.c`:
   - Initializes `osi_macsec_core_ops` by selecting virtualization ops or direct HW ops
   - The dispatch point between virtualized and direct MACsec register access

3. **OSI hardware implementation** — `hardware/osi/macsec/`:
   - `dwc_macsec.c` — Synopsys DWC_macsec 1.05a SAI/SAD register-level implementation. **This is the active, new implementation** replacing the previous LUT-based approach. Hardware specs: TX 16 SC × 2 SA = 32 SA, RX 32 SC × 2 SA = 64 SA, AES-GCM-128/256, 98 key contexts, MIB via indirect MIB_CMD, keys via separate APB (`macsec_id_aes_host_cfg`). Key operations: `osi_macsec_configure_sa`, `osi_macsec_enable_tx/rx`, `osi_macsec_set_pn`, `osi_macsec_mib_read`.
   - `dwc_macsec_reg.h` — Complete DWC MACsec register map (SAI, SAD, MIB_CMD, TRC_CMD, key config)
   - `macsec.c` + `macsec.h` (OSI-layer) — Register offset definitions, helper macros, AMAP helper functions
   - `macsec_debugfs.c` — MACsec debugfs interface for FPGA validation

The OSI MACsec interface is defined in `hardware/include/osi_macsec.h` (`struct osi_macsec_core_ops`), which is the header both the OSD macsec.c and OSI macsec implementations include.

### Key data flow

- **TX path**: `ether_start_xmit` → OSI DMA descriptor setup → HW → IRQ → VM ISR → NAPI poll → `osd_transmit_complete` → free SKB
- **RX path**: HW → IRQ → VM ISR → NAPI poll → `osi_dma_rx_process` → `osd_receive_packet` → napi_gro_receive
- **PTP TX timestamping**: `ether_start_xmit` stores SKB → work queue polls OSI for timestamps → updates SKB → delivers to socket error queue
- **MACsec control**: Userspace netlink → `macsec.c` handler → `common_macsec.c` dispatch → `dwc_macsec.c` SAI/SAD register programming

### VM / IVC support

The driver runs in virtualized environments using IVC (Inter-VM Communication) via `tegra_hv_ivc`. In VM mode, a `struct ether_vm_irq_data` + per-VM IRQ (`ether_vm_isr`) dispatches DMA channel interrupts based on channel mask. IVC messages carry IOCTL commands between server and client VMs.

### Safety and locking

- `raw_spinlock_t rlock` protects TX/RX interrupt enable registers
- `raw_spinlock_t ptp_lock` protects PTP register access
- `raw_spinlock_t txts_lock` protects TX timestamp SKB list
- `struct mutex osi_mdio_lock` serializes MDIO register access
- `atomic_t padcal_in_progress` guards PAD calibration state
- `struct mutex lock` in `macsec_priv_data` protects MACsec configuration
- Memory allocations use `devm_*` / `dmam_*` managed APIs where possible

## Key headers to include

When adding to the Linux layer, include `"ether_linux.h"` which pulls in all OSI headers, kernel headers, and feature flags. When modifying the OSI layer, headers are in `hardware/include/`. For MACsec work across layers, `hardware/include/osi_macsec.h` defines the shared `struct osi_macsec_core_ops` interface.

## Naming conventions

- OSD/Linux-layer functions: `ether_*` or `osd_*`
- OSI-core functions: `osi_core_*`, `osi_hal_*`, `eqos_*`, `mgbe_*`
- OSI-DMA functions: `osi_dma_*`, `eqos_dma_*`, `mgbe_dma_*`
- MACsec OSD functions: `macsec_*` (top-level `macsec.c`)
- MACsec OSI functions: `osi_macsec_*` (defined in `osi_macsec.h`, implemented in `dwc_macsec.c`)
- Doxygen `@brief` + `@note` block comments on every function
