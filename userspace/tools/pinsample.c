/*
 * pinsample - sample a GPIO port's input data register in a tight loop for a
 * number of seconds and report how often each requested pin was seen high.
 * NOMMU: the peripheral registers are directly addressable from user space.
 *
 * usage: pinsample <seconds> <port-letter> <pin> [<pin>...]
 *        e.g. pinsample 3 G 11 13 14
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define GPIO_BASE(port)	(0x40020000UL + 0x400UL * (port))
#define GPIO_IDR	0x10

int main(int argc, char **argv)
{
	volatile unsigned int *idr;
	unsigned int counts[16] = { 0 };
	unsigned int pins[16], npins = 0, i;
	unsigned long samples = 0;
	time_t end;
	int port;

	if (argc < 4) {
		fprintf(stderr, "usage: pinsample <seconds> <port A-K> <pin>...\n");
		return 1;
	}
	end = time(NULL) + strtoul(argv[1], NULL, 0);
	port = argv[2][0] - 'A';
	if (port < 0 || port > 10)
		return 1;
	for (i = 3; i < (unsigned)argc && npins < 16; i++)
		pins[npins++] = strtoul(argv[i], NULL, 0);

	idr = (volatile unsigned int *)(GPIO_BASE(port) + GPIO_IDR);

	do {
		unsigned int k;

		/* Check the clock only every 64k reads to keep the loop tight */
		for (k = 0; k < 65536; k++) {
			unsigned int v = *idr;

			for (i = 0; i < npins; i++)
				counts[i] += (v >> pins[i]) & 1;
		}
		samples += 65536;
	} while (time(NULL) < end);

	for (i = 0; i < npins; i++)
		printf("P%c%u high in %u of %lu samples\n", 'A' + port,
		       pins[i], counts[i], samples);
	return 0;
}
