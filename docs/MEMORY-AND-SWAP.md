# Memory, swap and NOMMU on the STM32F746G-DISCO

Measured on Linux 2.6.33-arm1 (Emcraft uClinux tree), 8 MB SDRAM, 2026-09-18.

## Can this system swap?

**No, and no kernel option can change that.** `init/Kconfig` has

```
config SWAP
	bool "Support for paging of anonymous memory (swap)"
	depends on MMU && BLOCK
```

Swap is demand paging: the kernel unmaps a page from a process, writes it
out, and relies on a page fault to bring it back when the process touches
it again. A Cortex-M7 has an MPU but no MMU, so there are no page tables,
no per-process address translation and no data-abort-driven page faults.
Without those there is nothing to "swap" against. The same applies to every
swap-shaped substitute:

| Idea | Why it does not apply here |
|------|----------------------------|
| swap file / partition on the SD card | `CONFIG_SWAP` requires MMU |
| zram / zswap (compressed RAM) | are swap devices / swap caches, need `CONFIG_SWAP` |
| overcommit / lazy allocation | NOMMU `mmap()` must hand out real, physically contiguous memory immediately |
| demand-loaded executables | bFLT binaries are copied whole into RAM at `exec()` (ARM bFLT has text relocations, so XIP from a file is not possible with this toolchain) |

`f7fetch` reports `Swap: none (NOMMU: no demand paging)` for this reason.

## What *does* act as backing store: the page cache

The one component that behaves like swap is the page cache for
**file-backed data**. Pages holding file contents from a block device are
reclaimable: under pressure the kernel drops them and re-reads them from the
SD card later. Before this work every file on the system lived in the
initramfs, which is a ramfs — its pages are pinned (`Unevictable`) and can
never be reclaimed. Moving the root filesystem to the SD card (goal 2)
turns the whole root into reclaimable storage.

Measurements (`free`, kB, idle system with LCD shell + uart_bridge running):

| Configuration | MemTotal | used (-buffers/cache) | free | cached | Unevictable |
|---|---|---|---|---|---|
| Original phase-3 image (initramfs, no block layer) | 6204 | 1680 | 3256 | 1268 | 1268 (all of it) |
| + block layer, MMC, ext2, FAT, 4×busybox copies removed | 5888 | 2228 | 3116 | 544 | 0 |
| Root on SD card (initramfs freed by `switch_root`) | 5888 | 2776 | 2768 | 268 | 0 |

MemTotal dropped by 316 kB because the kernel text grew (block layer, MMC
core, MMCI driver, ext2, FAT, NLS). The "used" column is higher on the SD
root only because the shell processes are larger (see next section) — the
`cached` column is now *reclaimable* file data instead of pinned ramfs.

## The real NOMMU memory problem: contiguous allocations

The failure that actually bites on this board is not lack of free memory
but **fragmentation**. Every process needs one physically contiguous block
for text + data + bss + stack (`binfmt_flat` allocates it with
`do_mmap()`, i.e. `alloc_pages(order)`). With the first 320 KB BusyBox
build a `cat /proc/slabinfo | head` pipeline died with:

```
head: page allocation failure. order:7, mode:0xd0
Normal: 0*4kB 0*8kB 5*16kB 5*32kB 3*64kB 4*128kB 2*256kB 0*512kB ... = 1456kB
Allocation of length 352256 from process 63 (head) failed
```

1.4 MB was free, but nothing larger than 256 kB was contiguous, and a
352 kB process needs an order-7 (512 kB) block. Two things were done about
it:

1. **Two BusyBox binaries.** `/bin/busybox` (core: init, hush, coreutils,
   mount, switch_root, ifconfig, ...) is kept at ~218 kB including its
   16 kB stack so every process fits an order-6 (256 kB) block. Everything
   else (vi, tar, gzip, fdisk, mkfs.ext2, mkfs.vfat, telnetd, wget, ...) is
   in `/bin/bbx` and only costs memory while such a command runs.
2. **No symlink farm on ramfs.** `busybox --install` had created ~145
   applet symlinks; on ramfs each symlink target is stored in a full 4 kB
   page, i.e. ~580 kB of pinned memory. The core shell now runs in
   standalone mode (`FEATURE_SH_STANDALONE` + `BUSYBOX_EXEC_PATH`) and
   needs no symlinks at all; the extended applets are exposed as shell
   functions from `/etc/profile`. On the ext2 SD root symlinks are inline
   in the inode and cost nothing, so `sdinstall` creates real ones there.

After these changes `/proc/buddyinfo` shows order-8/9 (1–2 MB) blocks free
on an idle system and pipelines work again.

## Other levers that remain (not implemented)

* **Internal SRAM as dedicated buffer memory.** The MCU has 320 kB of
  on-chip RAM: DTCM 64 kB (0x20000000, used by the Ethernet driver for its
  DMA ring from 0x20001000), SRAM1 240 kB (0x20010000) and SRAM2 16 kB.
  SRAM1 is unused by Linux. It cannot be added to the page allocator
  (FLATMEM needs one contiguous bank and the hole to SDRAM at 0xC0000000
  would need a 3 GB `mem_map`), but individual consumers can be moved there
  by hand: the 64 kB MMC bounce buffer, USB DWC2 buffers, or — with an
  8 bpp CLUT mode — the 130 kB framebuffer (261 kB at RGB565 does not fit).
  Each such move gives back the same amount of SDRAM to the page allocator
  and, more importantly, removes a large contiguous allocation from it.
* **Kernel size.** Kernel text is 1.1 MB of the 8 MB; `CONFIG_SLOB`,
  dropping `CONFIG_MODULES`, `LOG_BUF_SHIFT=12→10`, and `-Os` are already
  set or worth ~100 kB combined. XIP of the kernel from the 1 MB internal
  flash (`CONFIG_XIP_KERNEL` / `KERNEL_IN_ENVM`) does not fit.
* **Fewer resident BusyBox instances.** `init`, the LCD shell and every
  interactive shell each carry a full copy of the core binary (~218 kB).
  `FEATURE_SH_NOFORK` is enabled so trivial applets (`echo`, `test`, ...)
  run inside the shell without a second copy.
* **Compressed initramfs / bFLT (`BINFMT_ZFLAT`)** shrink the kernel image
  and the SD card, not RAM, and are therefore not pursued.

## Update 2026-09-19 — what was actually changed, and the swap verdict

**Swap: confirmed impossible, from the source.** `init/Kconfig`: `config SWAP …
depends on MMU && BLOCK`; our `.config` has `# CONFIG_MMU is not set`, so SWAP
is not even offered (0 occurrences in `.config`). No swap file, no swap
partition, no zram/zswap (all need `CONFIG_SWAP`). Reason restated: swap is
demand paging, and NOMMU has no page tables and no page faults, so anonymous
memory (heap/stack/bss) can never be evicted and brought back.

The NOMMU-appropriate equivalent already exists and is in use: with the root
filesystem on the SD card, **all file-backed pages (program text and file
data) are reclaimable** — the kernel drops them under pressure and re-reads
them from the card on demand. That is exactly "use the SD card as backing
store," just for the only memory that can be backed on a NOMMU system. The SD
card cannot back anonymous memory; nothing can, without an MMU.

**Allocation failures: measured, then reduced (not eliminable).**
- Empirically, even 24 concurrent busybox loads (eight 3-stage pipelines)
  produced a *single* transient failure, and the failing child just dies — the
  shell and system survive. The earlier cascades were pathological stress
  (concurrent multi-MB downloads + tight retry loops), not normal use.
- `drop_caches` frees order-0 pages but does NOT rebuild high-order blocks:
  there is no memory compaction on NOMMU (it needs page migration / an MMU).
  So post-hoc reclaim cannot fix contiguous-block exhaustion.
- Changes made:
  - `mm/nommu.c`: the private-mapping `alloc_pages()` now passes
    `__GFP_NOWARN`. A failed exec used to dump 40+ lines of `show_mem()` per
    failure; now it is the single line binfmt_flat already prints. This was the
    main user-visible pain.
  - `CONFIG_NOMMU_INITIAL_TRIM_EXCESS=0 -> 1`: each process over-allocates when
    its size rounds up to the next power-of-two block (e.g. 216 KB -> a 256 KB
    order-6 block); trimming returns the ~40 KB tail to the allocator. This is
    the upstream default.
- Deliberately NOT done: raising `vm.min_free_kbytes`. On 5.8 MB of RAM a large
  reserve is as likely to *increase* pressure (less usable memory) as to help
  the high-order watermark, and an A/B test could not be completed to justify
  it. Left at the computed default (360 KB).
- The only complete fix — shared flat binaries (`CONFIG_BINFMT_SHARED_FLAT`, so
  busybox text is one shared copy and each process allocates only ~58 KB of
  data+bss+stack) — is not usable here: the 2010 Sourcery `elf2flt` has no
  shared-library support (`-s`/`-p`/`-z` only).
