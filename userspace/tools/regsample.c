/*
 * regsample - sample a 32-bit register in a tight loop for N seconds, report
 * how often (value & mask) was non-zero and which distinct masked values were
 * seen. NOMMU: peripheral registers are directly addressable.
 *
 * usage: regsample <seconds> <hex-addr> <hex-mask>
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
	volatile unsigned int *reg;
	unsigned int mask, seen[16], nseen = 0, nonzero = 0, i;
	unsigned long samples = 0;
	time_t end;

	if (argc < 4) {
		fprintf(stderr, "usage: regsample <seconds> <addr> <mask>\n");
		return 1;
	}
	end = time(NULL) + strtoul(argv[1], NULL, 0);
	reg = (volatile unsigned int *)strtoul(argv[2], NULL, 16);
	mask = strtoul(argv[3], NULL, 16);

	do {
		unsigned int k;

		for (k = 0; k < 65536; k++) {
			unsigned int v = *reg & mask;

			if (v) {
				nonzero++;
				for (i = 0; i < nseen; i++)
					if (seen[i] == v)
						break;
				if (i == nseen && nseen < 16)
					seen[nseen++] = v;
			}
		}
		samples += 65536;
	} while (time(NULL) < end);

	printf("%p & %08x: non-zero in %u of %lu samples\n", (void *)reg, mask,
	       nonzero, samples);
	for (i = 0; i < nseen; i++)
		printf("  seen %08x\n", seen[i]);
	return 0;
}
