/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/*
 * gpd-hotkeysd: GPD Pocket hotkeys, without a desktop environment.
 * Writes /sys/class/backlight/<x>/brightness.
 */

#include "logic.h"
#include "version.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONF_DEFAULT "/etc/gpd-hotkeysd.conf"
#define DIR_BL "/sys/class/backlight"

static struct conf conf;
static const char *conf_path = CONF_DEFAULT;
/* --backlight-dir, for tests */
static const char *bl_test;
static bool conf_explicit;
static bool verbose;
static bool dry;
static bool journal;

/* ---------- logging (stderr, "<N>" prefix understood by journald) ----------
 */

__attribute__((format(printf, 2, 3))) static void msg(int level,
						      const char *fmt, ...)
{
	va_list ap;

	if (level >= 7 && !verbose)
		return;
	if (journal)
		fprintf(stderr, "<%d>", level);
	else if (level <= 4)
		fputs(level <= 3 ? "error: " : "warning: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}
#define ERR(...) msg(3, __VA_ARGS__)
#define WARN(...) msg(4, __VA_ARGS__)
#define INFO(...) msg(6, __VA_ARGS__)
#define DBG(...) msg(7, __VA_ARGS__)

/* ---------- configuration ---------- */

static int load_conf(struct conf *c)
{
	char l[512];
	char err[CONF_STR + 64];
	int n = 0;
	FILE *f;

	conf_default(c);
	f = fopen(conf_path, "re");
	if (!f) {
		if (errno == ENOENT && !conf_explicit)
			return 0; /* no file: defaults */
		ERR("%s: %s", conf_path, strerror(errno));
		return -1;
	}
	while (fgets(l, sizeof(l), f)) {
		size_t len = strlen(l);

		n++;
		/* truncated line: the rest must not become a key */
		if (len && l[len - 1] != '\n' && !feof(f)) {
			int ch;

			do
				ch = getc(f);
			while (ch != EOF && ch != '\n');
			WARN("%s:%d: line too long (ignored)", conf_path, n);
			if (ch == EOF)
				break;
			continue;
		}
		if (conf_line(c, l, err, sizeof(err)))
			WARN("%s:%d: %s (line ignored)", conf_path, n, err);
	}
	fclose(f);
	return 0;
}

/* ---------- backlight ---------- */

static int read_text(const char *dir, const char *file, char *buf, size_t len)
{
	char p[PATH_MAX];
	FILE *f;
	int r = -1;

	snprintf(p, sizeof(p), "%s/%s", dir, file);
	f = fopen(p, "re");
	if (!f)
		return -1;
	if (fgets(buf, (int)len, f)) {
		char *nl = strchr(buf, '\n');

		if (nl)
			*nl = '\0';
		r = 0;
	}
	fclose(f);
	return r;
}

static int read_int(const char *dir, const char *file, int *v)
{
	char b[32];
	char *end;
	long x;

	if (read_text(dir, file, b, sizeof(b)))
		return -1;
	errno = 0;
	x = strtol(b, &end, 10);
	if (errno || end == b || x < 0 || x > INT_MAX)
		return -1;
	*v = (int)x;
	return 0;
}

/* A real device: the link under /sys/class/backlight must resolve into
 * /sys/devices/. */
static int bl_valid(const char *dir)
{
	char r[PATH_MAX];

	return realpath(dir, r) && !strncmp(r, "/sys/devices/", 13) ? 0 : -1;
}

/*
 * Selection: --backlight-dir (tests), a name under /sys/class/backlight, or
 * "auto" (intel_backlight, else raw > platform > firmware).
 */
static int find_backlight(char *out, size_t len)
{
	static const char *const types[] = {"raw", "platform", "firmware"};
	char dir[PATH_MAX];
	char t[32];
	struct dirent *e;
	DIR *d;
	int best = 3;
	bool automatic = !strcmp(conf.backlight, "auto");

	if (bl_test) {
		snprintf(out, len, "%s", bl_test);
		return 0;
	}
	/* plain name only (already checked by conf_line) */
	if (strchr(conf.backlight, '/') || !strcmp(conf.backlight, ".") ||
	    !strcmp(conf.backlight, ".."))
		return -1;
	snprintf(out, len, DIR_BL "/%s",
		 automatic ? "intel_backlight" : conf.backlight);
	if (!bl_valid(out) || !automatic)
		return bl_valid(out);
	d = opendir(DIR_BL);
	if (!d)
		return -1;
	for (e = readdir(d); e; e = readdir(d)) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(dir, sizeof(dir), DIR_BL "/%s", e->d_name);
		if (bl_valid(dir) || read_text(dir, "type", t, sizeof(t)))
			continue;
		for (int i = 0; i < best; i++)
			if (!strcmp(t, types[i])) {
				best = i;
				snprintf(out, len, "%s", dir);
			}
	}
	closedir(d);
	return best < 3 ? 0 : -1;
}

static int brightness(bool up)
{
	char dir[PATH_MAX];
	char p[PATH_MAX + 16];
	int cur;
	int max;
	int v;
	int fd;
	int w;

	if (find_backlight(dir, sizeof(dir)) ||
	    read_int(dir, "max_brightness", &max) ||
	    read_int(dir, "brightness", &cur) || max < 1) {
		ERR("backlight \"%s\" not found or unreadable", conf.backlight);
		return -1;
	}
	v = brightness_next(cur, max, conf.brightness_step, conf.brightness_min,
			    up);
	snprintf(p, sizeof(p), "%s/brightness", dir);
	if (dry) {
		INFO("[dry-run] brightness %d -> %d (max %d): would write %d "
		     "to %s",
		     cur, v, max, v, p);
		return 0;
	}
	if (v != cur) {
		fd = open(p, O_WRONLY | O_CLOEXEC);
		if (fd < 0) {
			ERR("%s: %s", p, strerror(errno));
			return -1;
		}
		w = dprintf(fd, "%d\n", v);
		if (close(fd) || w < 0) {
			ERR("write %s: %s", p, strerror(errno));
			return -1;
		}
	}
	DBG("brightness %d -> %d / %d", cur, v, max);
	return 0;
}

static int action(enum hotkey k)
{
	if (k == HK_BRIGHTNESS_UP || k == HK_BRIGHTNESS_DOWN)
		return brightness(k == HK_BRIGHTNESS_UP);
	ERR("%s: not handled", hotkey_names[k]);
	return -1;
}

static void usage(FILE *f)
{
	fputs("usage: gpd-hotkeysd [-c file] [-v] [-n] [-b dir] "
	      "--test-event KEY_NAME\n"
	      "  -c, --config F       configuration file (default " CONF_DEFAULT
	      ")\n"
	      "  -v, --verbose        detailed log\n"
	      "  -n, --dry-run        show actions without applying them\n"
	      "  -t, --test-event K   simulate a key press and exit: "
	      "KEY_BRIGHTNESSUP,\n"
	      "                       KEY_BRIGHTNESSDOWN\n"
	      "  -b, --backlight-dir D\n"
	      "                       fake backlight (tests; absolute, refused "
	      "as root)\n"
	      "  -V, --version        version\n",
	      f);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
	    {"config", required_argument, NULL, 'c'},
	    {"verbose", no_argument, NULL, 'v'},
	    {"dry-run", no_argument, NULL, 'n'},
	    {"test-event", required_argument, NULL, 't'},
	    {"backlight-dir", required_argument, NULL, 'b'},
	    {"help", no_argument, NULL, 'h'},
	    {"version", no_argument, NULL, 'V'},
	    {NULL, 0, NULL, 0},
	};
	const char *test = NULL;
	int o;
	int k;

	while ((o = getopt_long(argc, argv, "c:vnt:b:hV", opts, NULL)) != -1) {
		switch (o) {
		case 'c':
			conf_path = optarg;
			conf_explicit = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'n':
			dry = true;
			break;
		case 't':
			test = optarg;
			break;
		case 'b':
			bl_test = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'V':
			puts("gpd-hotkeysd " HOTKEYS_VERSION);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	if (optind < argc || !test) {
		usage(stderr);
		return 2;
	}
	if (bl_test && (getuid() == 0 || geteuid() == 0 || *bl_test != '/')) {
		ERR("--backlight-dir: absolute path, for non-root tests only");
		return 2;
	}
	journal = getenv("JOURNAL_STREAM") != NULL;
	if (load_conf(&conf))
		return 1;
	k = hotkey_by_name(test);
	if (k < 0) {
		ERR("unknown key \"%s\"", test);
		usage(stderr);
		return 2;
	}
	return action((enum hotkey)k) ? 1 : 0;
}
