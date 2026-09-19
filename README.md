# Linux on the STM32F746G-DISCO (uClinux, NOMMU, Cortex-M7)

A working **Linux 2.6.33 (uClinux / NOMMU)** port for the low-cost
**STMicroelectronics STM32F746G-DISCO** board — a 216 MHz Cortex-M7 with only
**8 MB of external SDRAM and no MMU**. This tree brings the board up to a
usable single-board Linux system: network boot, a serial *and* on-screen
terminal, a USB keyboard, wired Ethernet, and a persistent root filesystem on
a microSD card.

> Based on [`angmouzakitis/linux-stm32f7`](https://github.com/angmouzakitis/linux-stm32f7)
> (itself derived from the Emcraft Systems uClinux BSP for the STM32/Cortex-M).
> This repository adds SD-card storage, a persistent root filesystem, USB-HID
> host input, a working Ethernet transmit path, a small system-info tool, and a
> pile of NOMMU/Windows-host build fixes — all documented below. See
> [Credits](#credits).

---

## Table of contents

- [What works](#what-works)
- [Highlights / notable fixes](#highlights--notable-fixes)
- [Hardware](#hardware)
- [Repository layout](#repository-layout)
- [Building](#building)
- [Booting the board](#booting-the-board)
- [Using it](#using-it)
- [Limitations & catches](#limitations--catches)
- [What could be done next](#what-could-be-done-next)
- [Credits](#credits)
- [License](#license)

---

## What works

| Subsystem | Status | Notes |
|-----------|--------|-------|
| Boot | ✅ | U-Boot 2010.03 netboots the kernel over TFTP (`bootm`) |
| Serial console | ✅ | ST-LINK VCP → `ttyS5` @ 115200 (USART6) |
| LCD framebuffer | ✅ | 480×272 RGB565 LTDC, Rocktech RK043FN48H, `/dev/fb0` |
| Framebuffer console | ✅ | `fbcon` 58×16 with an 8-px bezel margin (`/dev/tty1`) |
| Shared LCD terminal | ✅ | one shell on the LCD, driven by USB keyboard **and** UART (`uart_bridge`, `TIOCSTI`) |
| USB OTG-FS host | ✅ | DWC2 host on CN13, VBUS via STMPS2151 power switch |
| USB HID keyboard | ✅ | enumerates & delivers keystrokes to the LCD console |
| Ethernet | ✅ | RMII, LAN8742A PHY, `eth0`, ping/TFTP/HTTP verified both directions |
| SD card (SDMMC) | ✅ | `mmci-pl18x` / SDIO, `/dev/mmcblk0`, DMA |
| Persistent root | ✅ | ext2 root on the SD card via `switch_root`; FAT32 data partition |
| BusyBox userspace | ✅ | 1.24.2, NOMMU/bFLT, two-binary split (see below) |
| `f7fetch` | ✅ | neofetch-style system summary tool |

## Highlights / notable fixes

The interesting engineering is in making a **MMU-less** SoC with **8 MB RAM** do
these things. Each of these is written up in `docs/` and in the commit history:

- **Ethernet transmit was completely dead** (RX fine) because of a one-bit-field
  bug in the GPIO iomux: `STM32F7_GPIO_OTYPE_PP` was `0x02` instead of `0x00`,
  and `OTYPER |= otype << pin` therefore set the *neighbouring* pin's bit.
  Configuring PG12 (LCD_B4) for the LTDC forced **PG13 = RMII_TXD0 to
  open-drain**, so the MAC could never drive TXD0 high. Found by diffing the
  live GPIO registers under U-Boot vs Linux. (`arch/arm/mach-stm32/iomux.c`)
- **USB full-speed host + HID keyboard**: correct active-low VBUS switch on PD5,
  a real 48 MHz USB clock synthesised from PLLSAI, `GCCFG.VBDEN`/`PWRDWN` set
  the STM32F7 way (not the F4 way), and the low-speed 6 MHz PHY clock select so
  low-speed keyboards enumerate. (`drivers/usb/dwc2/*`, `arch/arm/mach-stm32/`)
- **SDMMC + persistent SD root**: enabled the block layer, MMC/SDIO, ext2, FAT;
  card-detect on PC13; `/init` `switch_root`s into an ext2 root on the card and
  falls back to the initramfs as a recovery/installer environment; `sdinstall`
  partitions and populates a card in place.
- **NOMMU memory reality**: a two-binary BusyBox split so each process fits an
  order-6 (256 KB) contiguous block, quiet allocation-failure handling, and a
  full analysis of why **swap is impossible without an MMU**. See
  [`docs/MEMORY-AND-SWAP.md`](docs/MEMORY-AND-SWAP.md).
- **Windows-host cross build**: fixes for the Sourcery 2010 toolchain emitting
  CRLF dependency files (`scripts/basic/fixdep.c`, and the same bug in BusyBox),
  a Perl 5.22 breakage (`kernel/timeconst.pl`), and initramfs permission
  handling.

## Hardware

- **Board:** STM32F746G-DISCO (silicon rev Z).
- **MCU:** STM32F746NGH6 — Cortex-M7 @ 216 MHz, MPU but **no MMU**.
- **RAM:** 8 MB external SDRAM (`0xC0000000`), 320 KB internal SRAM.
- **Flash:** 1 MB internal + external QSPI.
- **Display:** 4.3" 480×272 RGB565 (Rocktech RK043FN48H) on LTDC.
- **Ethernet:** LAN8742A PHY, RMII.
- **USB:** OTG-FS on CN13 (Micro-AB) used in host mode.
- **microSD:** SDMMC1 socket.
- **Console:** ST-LINK Virtual COM Port, 115200 8N1.

## Repository layout

```
linux-stm32f7-master/   the kernel source tree (2.6.33 uClinux) + .config + initdir/
docs/                   design notes (memory/swap, etc.)
tools/                  host-side helpers: mkimage.py, tftp_server.py, board.py, serd.py …
userspace/              BusyBox config fragments, patches, build scripts, C sources for
                        f7fetch / uart_bridge / fbtest / diagnostic tools
build_all.sh            one-shot: build userspace tools + kernel Image + package uImage
```

The kernel build artifacts (`vmlinux`, `*.o`, `System.map`, …), the upstream
BusyBox source tree and the compiled userspace binaries are **not** committed;
they are regenerated by the build scripts (see below).

## Building

Built on **Windows** with MSYS2 for `make`/coreutils and the
**Sourcery CodeBench Lite 2010.09** `arm-uclinuxeabi` toolchain (gcc 4.5.1,
uClibc, `elf2flt`). It builds equally well on Linux with the same toolchain;
override paths via environment variables.

Prerequisites:
- `arm-uclinuxeabi-` toolchain (Sourcery 2010.09) — provides `elf2flt` for bFLT.
- `make`, `perl`, `bash`, `tar`, `curl` (MSYS2 on Windows).
- Python 3 (for `mkimage.py` and the serial helpers).

```bash
# 1) BusyBox: fetch 1.24.2 and apply this project's patches, then build both variants
cd userspace
./setup-busybox.sh
./build_busybox.sh all        # produces userspace/busybox (core) and userspace/bbx (extended)

# 2) Kernel + userspace tools + uImage (paths override-able via env: TC=, PYTHON=, UIMAGE=)
cd ..
./build_all.sh                # -> $UIMAGE (default C:/tftp/networking.uImage)
```

`.config` is committed, so `oldconfig` reproduces the exact configuration.

## Booting the board

There is **no SDMMC support in this U-Boot**, so the kernel is always network
booted; the *root filesystem* then lives on the SD card.

1. Serve the uImage over TFTP from `172.17.4.1` (a minimal server is included):
   ```bash
   python tools/tftp_server.py       # serves C:\tftp on 172.17.4.1:69
   ```
2. At the U-Boot prompt (`STM32F746-DISCO>`): `run netboot`
   (the stock env does `tftp ${image}; bootm`).
3. `/init` mounts `/dev/mmcblk0p1` (ext2) and `switch_root`s into it. With no
   card, or a blank one, it stays on the initramfs — run `sdinstall` there to
   partition + populate a card, then reboot.

Kernel command-line knobs: `sdroot=off` forces the initramfs;
`sdroot=/dev/mmcblk0pN` selects a different root partition.

`tools/board.py` automates reset → netboot → console for development.

## Using it

- Type on the **USB keyboard** (CN13) or over the **UART** — both drive the same
  shell shown on the **LCD**. `Ctrl-X` on the UART drops to a private recovery
  shell.
- `f7fetch` prints a system summary (CPU, memory, disk, display, network).
- Standard BusyBox: `ls`, `vi`, `tar`, `wget`, `telnetd`, `mount`, `fdisk`,
  `mkfs.ext2`/`mkfs.vfat`, etc. (common applets in `busybox`, the rest in `bbx`).

## Limitations & catches

- **No MMU.** Every process needs one *physically contiguous* block of RAM
  (text+data+bss+stack). On 8 MB this works, but under heavy memory
  fragmentation large `exec()`s can fail with `errno 12`; the failing child
  dies and the shell keeps going. There is **no memory compaction** on NOMMU,
  so `drop_caches` cannot rebuild large blocks — only a reboot does.
- **No swap. At all.** `CONFIG_SWAP` requires an MMU. zram/zswap likewise. The
  SD-backed page cache is the only paging-like mechanism and it only covers
  *file-backed* pages. Full analysis: [`docs/MEMORY-AND-SWAP.md`](docs/MEMORY-AND-SWAP.md).
- **U-Boot can't read the SD card**, so cold boot is always via TFTP/Ethernet.
- **One USB port, host-only**, no hub power budget to speak of; tested with a
  single HID keyboard.
- **Old toolchain.** The bFLT `elf2flt` in Sourcery 2010.09 has no shared-library
  support, which is why BusyBox can't be a single shared-text binary (the clean
  fix for the per-process RAM cost).
- **Windows line endings.** The tree is built on a Windows host; `.gitattributes`
  disables EOL conversion so byte-sensitive files (e.g. `fixdep.c`) stay intact.
- Kernel is **2.6.33** (the Emcraft Cortex-M base) — ancient by mainline
  standards; this is a board-bringup/hobby port, not a maintained modern kernel.

## What could be done next

- **Shared-flat BusyBox** (`CONFIG_BINFMT_SHARED_FLAT` + a newer `elf2flt`) so
  the ~155 KB of text is shared and each process only needs ~60 KB — this would
  largely dissolve the fragmentation failures.
- **Move large buffers to internal SRAM1** (unused 240 KB): the MMC bounce
  buffer, DWC2 buffers, or an 8-bpp framebuffer, freeing SDRAM and removing big
  contiguous allocations from the page allocator.
- **RTC, SPI, I²C, audio (SAI), touchscreen** — hardware present, drivers not
  wired up here.
- **U-Boot SDMMC** so the board can boot standalone without a TFTP server.

## Credits

- **Base tree:** [`angmouzakitis/linux-stm32f7`](https://github.com/angmouzakitis/linux-stm32f7)
  — the starting point this work builds on.
- **Upstream BSP:** [Emcraft Systems](https://www.emcraft.com/) uClinux for the
  STM32 / Cortex-M (the `arch/arm/mach-stm32` platform, DWC2/MMCI/LTDC
  integration, and the NOMMU `dmamem`/bFLT plumbing originate there).
- **Linux kernel:** © its many authors, 2.6.33.
- **BusyBox:** © Erik Andersen, Denys Vlasenko et al. (GPLv2).
- Board bring-up, SD/USB/Ethernet fixes, tooling and documentation in this
  repository: **@Barbaror4**, with assistance from Claude (Anthropic).

## License

GPLv2, following the Linux kernel — see [`linux-stm32f7-master/COPYING`](linux-stm32f7-master/COPYING).
BusyBox and other bundled components retain their own GPLv2 licenses.
