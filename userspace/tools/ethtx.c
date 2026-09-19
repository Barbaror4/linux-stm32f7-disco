/*
 * ethtx - bare-metal STM32 Ethernet transmit test, independent of the Linux
 * driver. Builds one broadcast ARP request in DTCM (uncached, DMA-visible),
 * queues it through a single ring-mode descriptor the way U-Boot does, and
 * samples RMII TX_EN (PG11) while the DMA works on it.
 *
 * Run with eth0 down. NOMMU: all registers are directly addressable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REG(a)		(*(volatile unsigned int *)(a))

#define MACCR		0x40028000
#define MACFFR		0x40028004
#define MACA0HR		0x40028040
#define MACA0LR		0x40028044
#define MMCTGFCR	0x40028168
#define MMCRFCECR	0x40028194
#define MMCRGUFCR	0x400281C4
#define DMABMR		0x40029000
#define DMATPDR		0x40029004
#define DMASR		0x40029014
#define DMAOMR		0x40029018
#define DMATDLAR	0x40029010
#define DMACHTDR	0x40029048
#define GPIOG_IDR	0x40021810
#define RCC_AHB1RSTR	0x40023810
#define RCC_AHB1ENR	0x40023830
#define RCC_APB2ENR	0x40023844
#define SYSCFG_PMC	0x40013804
#define MACMIIAR	0x40028010

#define ETH_CLKS	(7u << 25)		/* MAC, TX, RX */

/*
 * Replay the boot loader's cold bring-up: clocks off, RMII selected, clocks
 * on, peripheral reset, DMA soft reset, MDIO clock, MAC config.
 */
static void cold_init(void)
{
	unsigned int i;

	REG(RCC_AHB1ENR) &= ~ETH_CLKS;
	REG(RCC_APB2ENR) |= 1u << 14;
	REG(SYSCFG_PMC) |= 1u << 23;
	REG(RCC_AHB1ENR) |= ETH_CLKS;
	REG(RCC_AHB1RSTR) |= 1u << 25;
	REG(RCC_AHB1RSTR) &= ~(1u << 25);
	REG(DMABMR) |= 1;
	for (i = 0; i < 1000 && (REG(DMABMR) & 1); i++)
		usleep(100);
	printf("cold init: DMA reset %s\n", (REG(DMABMR) & 1) ? "STUCK" : "done");
	REG(MACMIIAR) = 4u << 2;
	REG(MACCR) = 0x0000C800;
	REG(MACCR) = 0x0000C800;
	REG(DMABMR) = 0x02C16000;
	REG(MACA0HR) = 0x80008588;
	REG(MACA0LR) = 0x883CB1C0;
}

#define TDES0_OWN	(1u << 31)
#define TDES0_LS	(1u << 29)
#define TDES0_FS	(1u << 28)
#define TDES0_TER	(1u << 21)
#define TDES0_TCH	(1u << 20)

#define OMR_ST		(1u << 13)
#define OMR_SR		(1u << 1)
#define OMR_FTF		(1u << 20)
#define CR_TE		(1u << 3)
#define CR_RE		(1u << 2)

struct tdes {
	volatile unsigned int stat, ctrl, buf, next;
};

/* usage: ethtx [desc-addr] [buf-addr] [ring|chain] [len]  (defaults below) */
static struct tdes *DESC = (struct tdes *)0x2000F000;
static volatile unsigned char *BUF = (volatile unsigned char *)0x2000F100;

static unsigned char frame[60] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,		/* dst */
	0xc0, 0xb1, 0x3c, 0x88, 0x88, 0x85,		/* src */
	0x08, 0x06,					/* ARP */
	0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,	/* eth/ip/req */
	0xc0, 0xb1, 0x3c, 0x88, 0x88, 0x85, 172, 17, 4, 206,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 172, 17, 4, 1,
};

int main(int argc, char **argv)
{
	unsigned int i, high = 0, h13 = 0, h14 = 0, h1 = 0, n = 0, tgf0, sr0;
	unsigned int len = sizeof(frame), mode = TDES0_TER;

	if (argc > 1)
		DESC = (struct tdes *)strtoul(argv[1], NULL, 16);
	if (argc > 2)
		BUF = (volatile unsigned char *)strtoul(argv[2], NULL, 16);
	if (argc > 3 && !strcmp(argv[3], "chain"))
		mode = TDES0_TCH;
	if (argc > 4)
		len = strtoul(argv[4], NULL, 0);
	if (argc > 5 && !strcmp(argv[5], "self")) {
		/* unicast to our own address: counted by MMCRGUFCR if looped back */
		memcpy(frame, frame + 6, 6);
		REG(DMAOMR) |= OMR_SR;
	}
	if (argc > 6 && !strcmp(argv[6], "cold"))
		cold_init();
	printf("desc %p buf %p %s len %u\n", (void *)DESC, (void *)BUF,
	       mode == TDES0_TCH ? "chain" : "ring", len);

	for (i = 0; i < sizeof(frame); i++)
		BUF[i] = frame[i];

	/* Stop the DMA, flush the TX FIFO, install our one-entry ring */
	REG(DMAOMR) &= ~(OMR_ST | OMR_SR);
	REG(DMAOMR) |= OMR_FTF;
	for (i = 0; i < 1000 && (REG(DMAOMR) & OMR_FTF); i++)
		usleep(100);
	printf("FTF %s, DMASR=%08x MACCR=%08x DMABMR=%08x\n",
	       (REG(DMAOMR) & OMR_FTF) ? "STUCK" : "done", REG(DMASR),
	       REG(MACCR), REG(DMABMR));

	DESC->stat = mode;
	DESC->ctrl = len;
	DESC->buf = (unsigned int)BUF;
	DESC->next = (unsigned int)DESC;	/* chain to self */
	REG(DMATDLAR) = (unsigned int)DESC;
	REG(DMASR) = 0x0001ffff;		/* clear pending status */
	REG(MACCR) |= CR_TE | CR_RE;
	REG(DMAOMR) |= OMR_ST;

	tgf0 = REG(MMCTGFCR);
	DESC->stat = mode | TDES0_FS | TDES0_LS | TDES0_OWN;
	REG(DMATPDR) = 0;

	/* sample TX_EN while the DMA/MAC handle the frame */
	for (i = 0; i < 4000000; i++) {
		unsigned int g = REG(GPIOG_IDR);

		high += (g >> 11) & 1;
		h13 += (g >> 13) & 1;
		h14 += (g >> 14) & 1;
		h1 += (REG(0x40020010) >> 1) & 1;
		n++;
	}
	sr0 = REG(DMASR);

	printf("TDES0=%08x (OWN %s) DMASR=%08x CHTDR=%08x MMCTGFCR %u->%u\n",
	       DESC->stat, (DESC->stat & TDES0_OWN) ? "still set" : "cleared",
	       sr0, REG(DMACHTDR), tgf0, REG(MMCTGFCR));
	printf("PG11 (TX_EN) high in %u, TXD0 %u, TXD1 %u, REF_CLK %u of %u samples\n",
	       high, h13, h14, h1, n);
	usleep(20000);
	printf("MMC rx good unicast=%u rx crc err=%u\n", REG(MMCRGUFCR),
	       REG(MMCRFCECR));
	return 0;
}
