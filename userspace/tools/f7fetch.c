/*
 * f7fetch - neofetch-style system summary for the STM32F7-DISCO uClinux port.
 *
 * Everything comes from /proc and a couple of device nodes, so this works on
 * a NOMMU kernel with no procps and no dynamic linker. The output is ANSI
 * coloured and fits the 58-column LCD console as well as a serial terminal.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <linux/fb.h>

#define C_RESET  "\033[0m"
#define C_TITLE  "\033[1;36m"
#define C_KEY    "\033[1;33m"
#define C_VAL    "\033[0;37m"
#define C_LOGO   "\033[1;34m"

#define LABEL_WIDTH 8

/* One logo line is printed to the left of each info line. */
static const char *logo[] = {
	" ______  ",
	"|  ____| ",
	"| |__    ",
	"|  __|   ",
	"| |      ",
	"|_|7     ",
	"         ",
	" DISCO   ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
	"         ",
};
#define LOGO_LINES ((int)(sizeof(logo) / sizeof(logo[0])))
#define LOGO_BLANK "         "

static int line_no;

static const char *next_art(void)
{
	return line_no < LOGO_LINES ? logo[line_no] : LOGO_BLANK;
}

/*
 * Print one "key: value" row, prefixed by the next line of the logo. A NULL
 * key produces a continuation line aligned under the previous value.
 */
static void row(const char *key, const char *fmt, ...)
{
	va_list ap;

	printf(C_LOGO "%s" C_RESET, next_art());
	if (key)
		printf(C_KEY "%-*s" C_RESET C_VAL, LABEL_WIDTH, key);
	else
		printf("%-*s" C_VAL, LABEL_WIDTH, "");

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	printf(C_RESET "\n");
	line_no++;
}

/*
 * Read a whole (small) file into buf, NUL terminated. Returns 0 on success.
 */
static int slurp(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	return 0;
}

/*
 * Copy the value of a "key: value" line out of /proc-style text. Returns 0
 * on success.
 */
static int proc_field(const char *text, const char *key, char *out, size_t len)
{
	const char *p = text;
	size_t klen = strlen(key);

	while (p && *p) {
		if (!strncmp(p, key, klen)) {
			const char *v = strchr(p, ':');
			const char *e;
			size_t n;

			if (!v)
				return -1;
			for (v++; *v == ' ' || *v == '\t'; v++)
				;
			e = strchr(v, '\n');
			if (!e)
				e = v + strlen(v);
			while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
				e--;
			n = e - v;
			if (n >= len)
				n = len - 1;
			memcpy(out, v, n);
			out[n] = 0;
			return 0;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return -1;
}

/*
 * Pull a "<name>: <n> kB" figure out of /proc/meminfo. Returns -1 if absent.
 */
static long meminfo_kb(const char *text, const char *key)
{
	char val[64];

	if (proc_field(text, key, val, sizeof(val)) < 0)
		return -1;
	return strtol(val, NULL, 10);
}

/*
 * Render a fixed width usage gauge, e.g. [#####-----].
 */
static void gauge(char *out, size_t len, long used, long total)
{
	const int width = 10;
	int fill, i;
	size_t n = 0;

	if (total <= 0 || len < (size_t)width + 3) {
		if (len)
			*out = 0;
		return;
	}

	fill = (int)((used * width + total / 2) / total);
	if (fill < 0)
		fill = 0;
	if (fill > width)
		fill = width;

	out[n++] = '[';
	for (i = 0; i < width; i++)
		out[n++] = i < fill ? '#' : '-';
	out[n++] = ']';
	out[n] = 0;
}

static void show_host(void)
{
	struct utsname u;
	const char *user = getenv("USER");

	if (uname(&u) < 0)
		return;

	printf(C_LOGO "%s" C_RESET C_TITLE "%s@%s" C_RESET "\n",
	       next_art(), user ? user : "root", u.nodename);
	line_no++;

	row(NULL, "--------------------------");
	row("OS", "uClinux NOMMU (%s)", u.machine);
	row("Kernel", "%s", u.release);
	row("Board", "STM32F746G-DISCO, 8M SDRAM");
}

static void show_cpu(void)
{
	char buf[2048];
	char val[128];

	if (slurp("/proc/cpuinfo", buf, sizeof(buf)) < 0)
		return;

	if (proc_field(buf, "Processor", val, sizeof(val)) == 0 ||
	    proc_field(buf, "model name", val, sizeof(val)) == 0)
		row("CPU", "%s", val);
	if (proc_field(buf, "BogoMIPS", val, sizeof(val)) == 0)
		row("BogoMIPS", "%s", val);
}

static void show_uptime(void)
{
	char buf[128];
	long up;

	if (slurp("/proc/uptime", buf, sizeof(buf)) < 0)
		return;
	up = strtol(buf, NULL, 10);

	if (up >= 3600)
		row("Uptime", "%ldh %ldm %lds", up / 3600,
		    (up % 3600) / 60, up % 60);
	else if (up >= 60)
		row("Uptime", "%ldm %lds", up / 60, up % 60);
	else
		row("Uptime", "%lds", up);
}

static void show_memory(void)
{
	char buf[2048];
	char bar[32];
	long total, freek, buffers, cached, used;

	if (slurp("/proc/meminfo", buf, sizeof(buf)) < 0)
		return;

	total = meminfo_kb(buf, "MemTotal");
	freek = meminfo_kb(buf, "MemFree");
	buffers = meminfo_kb(buf, "Buffers");
	cached = meminfo_kb(buf, "Cached");
	if (total < 0 || freek < 0)
		return;
	if (buffers < 0)
		buffers = 0;
	if (cached < 0)
		cached = 0;

	used = total - freek - buffers - cached;
	gauge(bar, sizeof(bar), used, total);

	row("Memory", "%ldK/%ldK %s", used, total, bar);
	row(NULL, "free %ldK, cache %ldK", freek, buffers + cached);
}

static void show_swap(void)
{
	char buf[2048];
	long total, freek;

	if (slurp("/proc/meminfo", buf, sizeof(buf)) < 0)
		return;

	total = meminfo_kb(buf, "SwapTotal");
	if (total < 0) {
		/*
		 * No SwapTotal line at all: the kernel was built without
		 * CONFIG_SWAP, which is what NOMMU forces.
		 */
		row("Swap", "none (NOMMU: no demand paging)");
		return;
	}
	if (total == 0) {
		row("Swap", "none active");
		return;
	}

	freek = meminfo_kb(buf, "SwapFree");
	if (freek < 0)
		freek = 0;
	row("Swap", "%ldK/%ldK used", total - freek, total);
}

/*
 * Report every mounted filesystem backed by a real device, which on this
 * board means the SD card.
 */
static void show_storage(void)
{
	FILE *f;
	char dev[128], dir[128], type[64], opts[160];
	int shown = 0;

	f = fopen("/proc/mounts", "r");
	if (!f)
		return;

	while (fscanf(f, "%127s %127s %63s %159s %*d %*d",
		      dev, dir, type, opts) == 4) {
		struct statfs st;
		char bar[32];
		unsigned long total_mb, used_mb;
		long long bytes;

		if (strncmp(dev, "/dev/", 5))
			continue;
		if (statfs(dir, &st) < 0 || st.f_blocks == 0)
			continue;

		bytes = (long long)st.f_blocks * st.f_bsize;
		total_mb = (unsigned long)(bytes >> 20);
		bytes = (long long)(st.f_blocks - st.f_bfree) * st.f_bsize;
		used_mb = (unsigned long)(bytes >> 20);
		gauge(bar, sizeof(bar), used_mb, total_mb);

		row(shown ? NULL : "Disk", "%s %luM/%luM %s %s",
		    dir, used_mb, total_mb, bar, type);
		shown = 1;
	}
	fclose(f);

	if (!shown)
		row("Disk", "no block filesystem mounted");
}

static void show_display(void)
{
	struct fb_var_screeninfo var;
	int fd;

	fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0)
		return;

	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0)
		row("Display", "%ux%u %ubpp LTDC",
		    var.xres, var.yres, var.bits_per_pixel);
	close(fd);
}

static void show_net(void)
{
	char buf[2048];
	char *p;

	if (slurp("/proc/net/dev", buf, sizeof(buf)) < 0)
		return;

	/* Skip the two header lines. */
	p = strchr(buf, '\n');
	if (p)
		p = strchr(p + 1, '\n');
	if (!p)
		return;
	p++;

	while (*p) {
		char *nl = strchr(p, '\n');
		char *colon = strchr(p, ':');
		char name[32];
		size_t n;

		if (!colon || (nl && colon > nl))
			break;
		while (*p == ' ')
			p++;
		n = colon - p;
		if (n >= sizeof(name))
			n = sizeof(name) - 1;
		memcpy(name, p, n);
		name[n] = 0;

		if (strcmp(name, "lo"))
			row("Network", "%s", name);

		if (!nl)
			break;
		p = nl + 1;
	}
}

static void show_palette(void)
{
	int i;

	printf(C_LOGO "%s" C_RESET, next_art());
	for (i = 0; i < 8; i++)
		printf("\033[4%dm  ", i);
	printf(C_RESET "\n");
	line_no++;
}

int main(void)
{
	show_host();
	show_cpu();
	show_uptime();
	show_memory();
	show_swap();
	show_storage();
	show_display();
	show_net();
	row(NULL, "");
	show_palette();

	return 0;
}
