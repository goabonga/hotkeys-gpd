/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/*
 * gpd-hotkeysd: GPD Pocket hotkeys, without a desktop environment.
 * Writes /sys/class/backlight/<x>/brightness and drives the ALSA mixer of the
 * chtrt5645 card. Reads evdev devices WITHOUT grabbing them (no EVIOCGRAB): the
 * console and the compositor still receive every key.
 */

#include "logic.h"
#include "version.h"

#include <alsa/asoundlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CONF_DEFAULT "/etc/gpd-hotkeysd.conf"
#define DIR_BL "/sys/class/backlight"
#define DIR_INPUT "/dev/input"
#define MAX_DEV 32

static struct conf conf;
static const char *conf_path = CONF_DEFAULT;
/* --backlight-dir, for tests */
static const char *bl_test;
static bool conf_explicit;
static bool verbose;
static bool dry;
static bool journal;
/* an acpi_video backlight exists: the kernel handles "Video Bus" keys */
static bool ignore_video_bus;
/* current values in % (-1: unknown), exposed through the state file */
static struct {
	int brightness;
	int volume;
	int mute;
} state = {-1, -1, -1};

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

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

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

/* ---------- state file ---------- */

static void write_state(void)
{
	char tmp[PATH_MAX];
	char dir[PATH_MAX];
	char *slash;
	int fd;

	if (dry || !*conf.state_file)
		return;
	snprintf(dir, sizeof(dir), "%s", conf.state_file);
	slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	if (*dir)
		mkdir(dir, 0755); /* already there with RuntimeDirectory= */
	snprintf(tmp, sizeof(tmp), "%s.tmp", conf.state_file);
	/* O_EXCL|O_NOFOLLOW: a <state>.tmp symlink never redirects the write */
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
		  0644);
	if (fd < 0) {
		DBG("%s: %s", tmp, strerror(errno));
		return;
	}
	fchmod(fd, 0644); /* readable despite UMask=0077 */
	dprintf(fd, "brightness=%d\nvolume=%d\nmute=%d\n", state.brightness,
		state.volume, state.mute);
	if (close(fd) || rename(tmp, conf.state_file))
		DBG("%s: %s", conf.state_file, strerror(errno));
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

/* An acpi_video* is present: the kernel already changes the brightness. */
static bool acpi_video_present(void)
{
	struct dirent *e;
	bool found = false;
	DIR *d = opendir(DIR_BL);

	if (!d)
		return false;
	for (e = readdir(d); e; e = readdir(d))
		found = found || !strncmp(e->d_name, "acpi_video", 10);
	closedir(d);
	return found;
}

static int brightness(bool up, bool read_only)
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
	if (read_only) {
		state.brightness = percent(cur, 0, max);
		return 0;
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
	state.brightness = percent(v, 0, max);
	DBG("brightness %d -> %d / %d", cur, v, max);
	return 0;
}

/* ---------- sound (ALSA) ---------- */

struct sound {
	snd_mixer_t *m;
	snd_hctl_t *h;
	snd_mixer_elem_t *vol;
};

static void sound_close(struct sound *s)
{
	if (s->m)
		snd_mixer_close(s->m);
	memset(s, 0, sizeof(*s));
}

/* alsa-lib internal messages: verbose mode only */
__attribute__((format(printf, 5, 6))) static void
alsa_error(const char *file, int line, const char *fn, int err, const char *fmt,
	   ...)
{
	char b[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	DBG("alsa-lib %s:%d (%s): %s%s%s", file, line, fn, b, err ? ": " : "",
	    err ? snd_strerror(err) : "");
}

/* Opens the mixer on every action: the card may appear after the daemon. */
static int sound_open(struct sound *s, bool quiet)
{
	const char *const names[] = {conf.volume_control, "Master", "PCM"};
	char dev[CONF_STR + 8];
	int r;

	memset(s, 0, sizeof(*s));
	if (strchr(conf.card, ':'))
		snprintf(dev, sizeof(dev), "%s", conf.card);
	else
		snprintf(dev, sizeof(dev), "hw:%s", conf.card);
	r = snd_mixer_open(&s->m, 0);
	if (r >= 0)
		r = snd_mixer_attach(s->m, dev);
	if (r >= 0)
		r = snd_mixer_selem_register(s->m, NULL, NULL);
	if (r >= 0)
		r = snd_mixer_load(s->m);
	if (r < 0) {
		if (quiet)
			DBG("card %s: %s", dev, snd_strerror(r));
		else
			ERR("card %s: %s", dev, snd_strerror(r));
		sound_close(s);
		return -1;
	}
	if (snd_mixer_get_hctl(s->m, dev, &s->h) < 0)
		s->h = NULL;
	for (size_t i = 0; i < sizeof(names) / sizeof(*names) && !s->vol; i++) {
		snd_mixer_selem_id_t *id;
		snd_mixer_elem_t *e;

		snd_mixer_selem_id_alloca(&id);
		snd_mixer_selem_id_set_name(id, names[i]);
		e = snd_mixer_find_selem(s->m, id);
		if (e && snd_mixer_selem_has_playback_volume(e)) {
			s->vol = e;
			if (i)
				WARN("control \"%s\" missing: falling back "
				     "to \"%s\"",
				     conf.volume_control, names[i]);
		}
	}
	return 0;
}

static snd_hctl_elem_t *ctl_by_name(struct sound *s, const char *name)
{
	snd_ctl_elem_id_t *id;

	if (!s->h)
		return NULL;
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, name);
	return snd_hctl_find_elem(s->h, id);
}

/* 1 muted, 0 unmuted, -1 unknown: first mute switch, else the volume's. */
static int mute_read(struct sound *s)
{
	snd_hctl_elem_t *e = NULL;
	int on;

	if (conf.n_mute)
		e = ctl_by_name(s, conf.mute_controls[0]);
	if (e) {
		snd_ctl_elem_value_t *v;

		snd_ctl_elem_value_alloca(&v);
		if (snd_hctl_elem_read(e, v) < 0)
			return -1;
		return !snd_ctl_elem_value_get_boolean(v, 0);
	}
	if (s->vol && snd_mixer_selem_has_playback_switch(s->vol) &&
	    snd_mixer_selem_get_playback_switch(
		s->vol, SND_MIXER_SCHN_FRONT_LEFT, &on) >= 0)
		return !on;
	return -1;
}

static int mute_write(struct sound *s, bool muted)
{
	int ok = 0;
	int r;

	for (int i = 0; i < conf.n_mute; i++) {
		snd_hctl_elem_t *e = ctl_by_name(s, conf.mute_controls[i]);
		snd_ctl_elem_info_t *info;
		snd_ctl_elem_value_t *v;

		if (!e) {
			WARN("control \"%s\" missing", conf.mute_controls[i]);
			continue;
		}
		snd_ctl_elem_info_alloca(&info);
		snd_ctl_elem_value_alloca(&v);
		r = snd_hctl_elem_info(e, info);
		if (r >= 0)
			r = snd_hctl_elem_read(e, v);
		if (r < 0) {
			ERR("%s: %s", conf.mute_controls[i], snd_strerror(r));
			continue;
		}
		for (unsigned int c = 0; c < snd_ctl_elem_info_get_count(info);
		     c++)
			snd_ctl_elem_value_set_boolean(v, c, !muted);
		r = snd_hctl_elem_write(e, v);
		if (r < 0)
			ERR("%s: %s", conf.mute_controls[i], snd_strerror(r));
		else
			ok++;
	}
	if (!ok && s->vol && snd_mixer_selem_has_playback_switch(s->vol) &&
	    snd_mixer_selem_set_playback_switch_all(s->vol, !muted) >= 0)
		ok++;
	return ok ? 0 : -1;
}

static int toggle_mute(struct sound *s)
{
	int m = mute_read(s);

	if (m < 0) {
		ERR("mute: no usable switch");
		return -1;
	}
	if (dry) {
		INFO("[dry-run] mute: %s -> %s", m ? "muted" : "unmuted",
		     m ? "unmuted" : "muted");
		return 0;
	}
	if (mute_write(s, !m))
		return -1;
	state.mute = !m;
	DBG("mute: %s", m ? "unmuted" : "muted");
	return 0;
}

static int change_volume(struct sound *s, bool up)
{
	long cur;
	long min;
	long max;
	long ceiling;
	long v;
	int m = mute_read(s);
	int r;

	if (!s->vol) {
		ERR("volume: no usable control (\"%s\", Master, PCM)",
		    conf.volume_control);
		return -1;
	}
	r = snd_mixer_selem_get_playback_dB_range(s->vol, &min, &max);
	if (r >= 0)
		r = snd_mixer_selem_get_playback_dB(
		    s->vol, SND_MIXER_SCHN_FRONT_LEFT, &cur);
	if (r < 0) {
		ERR("volume: reading dB: %s", snd_strerror(r));
		return -1;
	}
	ceiling = max < conf.volume_max ? max : conf.volume_max;
	v = volume_next(cur, volume_step_for(&conf, min, ceiling), min, ceiling,
			up);
	if (dry) {
		INFO("[dry-run] volume %.2f -> %.2f dB%s", (double)cur / 100.0,
		     (double)v / 100.0, up && m == 1 ? " (+ unmute)" : "");
		return 0;
	}
	/* volume up unmutes first */
	if (up && m == 1 && !mute_write(s, false))
		state.mute = 0;
	r = snd_mixer_selem_set_playback_dB_all(s->vol, v,
						up && v < ceiling ? 1 : -1);
	if (r < 0) {
		ERR("volume: %s", snd_strerror(r));
		return -1;
	}
	state.volume = percent(v, min, ceiling);
	DBG("volume %.2f -> %.2f dB", (double)cur / 100.0, (double)v / 100.0);
	return 0;
}

/* Reads volume and mute without changing anything. */
static void sound_read_state(void)
{
	struct sound s;
	long min;
	long max;
	long cur;

	if (sound_open(&s, true))
		return;
	state.mute = mute_read(&s);
	if (s.vol &&
	    snd_mixer_selem_get_playback_dB_range(s.vol, &min, &max) >= 0 &&
	    snd_mixer_selem_get_playback_dB(s.vol, SND_MIXER_SCHN_FRONT_LEFT,
					    &cur) >= 0)
		state.volume = percent(
		    cur, min, max < conf.volume_max ? max : conf.volume_max);
	sound_close(&s);
}

static int sound(enum hotkey k)
{
	struct sound s;
	int r;

	if (sound_open(&s, dry)) {
		if (dry) {
			INFO("[dry-run] %s: card \"%s\" unavailable, no action",
			     hotkey_names[k], conf.card);
			return 0;
		}
		return -1;
	}
	r = k == HK_MUTE ? toggle_mute(&s)
			 : change_volume(&s, k == HK_VOLUME_UP);
	sound_close(&s);
	return r;
}

static int action(enum hotkey k)
{
	int r;

	if (k == HK_BRIGHTNESS_UP || k == HK_BRIGHTNESS_DOWN)
		r = brightness(k == HK_BRIGHTNESS_UP, false);
	else
		r = sound(k);
	write_state();
	return r;
}

/* ---------- evdev devices ---------- */

static const unsigned int codes[HK_COUNT] = {
    KEY_BRIGHTNESSUP, KEY_BRIGHTNESSDOWN, KEY_VOLUMEUP,
    KEY_VOLUMEDOWN,   KEY_MUTE,
};

static struct dev {
	int fd;
	int id;
	struct libevdev *ev;
	char name[32]; /* eventN */
} devs[MAX_DEV];
static int ndev;
static int next_id;
static struct dedup dedup;

/* Hardware-supplied name: no control characters in the log. */
static const char *safe_name(const struct libevdev *ev, char *b, size_t n)
{
	const char *s = libevdev_get_name(ev) ? libevdev_get_name(ev) : "?";
	size_t i;

	for (i = 0; s[i] && i + 1 < n; i++)
		b[i] = (unsigned char)s[i] < 0x20 || s[i] == 0x7f ? '?' : s[i];
	b[i] = '\0';
	return b;
}

static void dev_add(const char *name)
{
	char p[PATH_MAX];
	char n[64];
	struct libevdev *ev;
	bool useful = false;
	int fd;
	int r;

	if (strncmp(name, "event", 5) != 0 ||
	    strlen(name) >= sizeof(devs[0].name))
		return;
	for (int i = 0; i < ndev; i++)
		if (!strcmp(devs[i].name, name))
			return;
	if (ndev == MAX_DEV) {
		WARN("too many devices, %s ignored", name);
		return;
	}
	snprintf(p, sizeof(p), DIR_INPUT "/%s", name);
	/* read-only, never EVIOCGRAB */
	fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		/* EACCES: udev is not done yet, retried on IN_ATTRIB */
		DBG("%s: %s", p, strerror(errno));
		return;
	}
	r = libevdev_new_from_fd(fd, &ev);
	if (r < 0) {
		DBG("%s: %s", p, strerror(-r));
		close(fd);
		return;
	}
	for (int k = 0; k < HK_COUNT; k++)
		useful =
		    useful || libevdev_has_event_code(ev, EV_KEY, codes[k]);
	safe_name(ev, n, sizeof(n));
	if (useful && ignore_video_bus && !strcmp(n, "Video Bus")) {
		INFO("%s (%s) ignored: acpi_video already handles brightness",
		     n, name);
		useful = false;
	}
	if (!useful) {
		libevdev_free(ev);
		close(fd);
		return;
	}
	devs[ndev] = (struct dev){.fd = fd, .id = next_id++, .ev = ev};
	snprintf(devs[ndev++].name, sizeof(devs[0].name), "%s", name);
	INFO("added %s (%s)", n, name);
}

static void dev_remove(int i)
{
	char n[64];

	INFO("removed %s (%s)", safe_name(devs[i].ev, n, sizeof(n)),
	     devs[i].name);
	libevdev_free(devs[i].ev);
	close(devs[i].fd);
	devs[i] = devs[--ndev];
}

static void dev_scan(void)
{
	struct dirent *e;
	DIR *d = opendir(DIR_INPUT);

	if (!d) {
		ERR(DIR_INPUT ": %s", strerror(errno));
		return;
	}
	for (e = readdir(d); e; e = readdir(d))
		dev_add(e->d_name);
	closedir(d);
}

static void dev_key(int i, const struct input_event *e)
{
	for (int k = 0; k < HK_COUNT; k++) {
		if (e->code != codes[k])
			continue;
		DBG("%s: %s value %d", devs[i].name, hotkey_names[k], e->value);
		if (dedup_accept(&dedup, (enum hotkey)k, devs[i].id, e->value,
				 now_ms(), conf.dedup_ms, conf.repeat_ms))
			action((enum hotkey)k);
	}
}

/* Reads all pending events; the device is removed if it went away. */
static void dev_read(int i)
{
	struct input_event e;
	unsigned int flag = LIBEVDEV_READ_FLAG_NORMAL;
	int r;

	for (;;) {
		r = libevdev_next_event(devs[i].ev, flag, &e);
		if (flag == LIBEVDEV_READ_FLAG_SYNC) {
			if (r == LIBEVDEV_READ_STATUS_SYNC)
				continue; /* resync: take no action */
			if (r == -EAGAIN) {
				flag = LIBEVDEV_READ_FLAG_NORMAL;
				continue;
			}
		} else if (r == LIBEVDEV_READ_STATUS_SYNC) {
			DBG("%s: SYN_DROPPED, resyncing", devs[i].name);
			flag = LIBEVDEV_READ_FLAG_SYNC;
			continue;
		}
		if (r == -EAGAIN)
			return;
		if (r < 0) {
			if (r != -ENODEV)
				ERR("%s: %s", devs[i].name, strerror(-r));
			dev_remove(i);
			return;
		}
		if (e.type == EV_KEY)
			dev_key(i, &e);
	}
}

static void inotify_read(int ino)
{
	char buf[4096]
	    __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;

	for (n = read(ino, buf, sizeof(buf)); n > 0;
	     n = read(ino, buf, sizeof(buf))) {
		for (char *p = buf; p < buf + n;) {
			const struct inotify_event *ev =
			    (const struct inotify_event *)p;

			p += sizeof(*ev) + ev->len;
			if (ev->mask & IN_Q_OVERFLOW) {
				dev_scan();
				continue;
			}
			if (!ev->len)
				continue;
			if (!(ev->mask & IN_DELETE)) {
				/* IN_CREATE, or IN_ATTRIB: udev set the mode */
				dev_add(ev->name);
				continue;
			}
			for (int i = 0; i < ndev; i++)
				if (!strcmp(devs[i].name, ev->name)) {
					dev_remove(i);
					break;
				}
		}
	}
}

/* ---------- main loop ---------- */

/* Returns true when the daemon must stop. */
static bool signals_read(int sfd)
{
	struct signalfd_siginfo si;
	bool done = false;

	while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
		struct conf c;

		if (si.ssi_signo != SIGHUP) {
			done = true;
		} else if (!load_conf(&c)) {
			conf = c;
			INFO("configuration reloaded");
		}
	}
	return done;
}

static int run(void)
{
	struct pollfd pfd[MAX_DEV + 2];
	sigset_t set;
	int sfd;
	int ino;
	bool done = false;

	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGHUP);
	sfd = sigprocmask(SIG_BLOCK, &set, NULL)
		  ? -1
		  : signalfd(-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);
	if (sfd < 0) {
		ERR("signalfd: %s", strerror(errno));
		return 1;
	}
	ino = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (ino < 0 ||
	    inotify_add_watch(ino, DIR_INPUT,
			      IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
		ERR("inotify " DIR_INPUT ": %s", strerror(errno));
		close(sfd);
		return 1;
	}
	ignore_video_bus = acpi_video_present();
	INFO("gpd-hotkeysd " HOTKEYS_VERSION "%s", dry ? " (dry-run)" : "");
	dev_scan();
	if (!ndev)
		WARN(
		    "no device with the handled keys (permissions on " DIR_INPUT
		    "?); waiting");
	brightness(false, true);
	sound_read_state();
	write_state();

	while (!done) {
		int n = 2;

		pfd[0] = (struct pollfd){.fd = sfd, .events = POLLIN};
		pfd[1] = (struct pollfd){.fd = ino, .events = POLLIN};
		for (int i = 0; i < ndev; i++)
			pfd[n++] =
			    (struct pollfd){.fd = devs[i].fd, .events = POLLIN};
		if (poll(pfd, (nfds_t)n, -1) < 0) {
			if (errno == EINTR)
				continue;
			ERR("poll: %s", strerror(errno));
			break;
		}
		if (pfd[0].revents & POLLIN)
			done = signals_read(sfd);
		/*
		 * Devices BEFORE inotify: this loop only closes descriptors,
		 * so no number is reused by a new device before its revents
		 * have been read.
		 */
		for (int j = 2; j < n; j++) {
			if (!pfd[j].revents)
				continue;
			for (int i = 0; i < ndev; i++) {
				if (devs[i].fd != pfd[j].fd)
					continue;
				if (pfd[j].revents & POLLIN)
					dev_read(i);
				else
					dev_remove(i); /* HUP, ERR, NVAL */
				break;
			}
		}
		if (pfd[1].revents & POLLIN)
			inotify_read(ino); /* new devices join on next turn */
	}
	INFO("stopping");
	while (ndev)
		dev_remove(ndev - 1);
	close(ino);
	close(sfd);
	return done ? 0 : 1;
}

static void usage(FILE *f)
{
	fputs("usage: gpd-hotkeysd [-c file] [-v] [-n] [-b dir] "
	      "[--test-event KEY_NAME]\n"
	      "  -c, --config F       configuration file (default " CONF_DEFAULT
	      ")\n"
	      "  -v, --verbose        detailed log\n"
	      "  -n, --dry-run        show actions without applying them\n"
	      "  -t, --test-event K   simulate a key press and exit "
	      "(without it, listen to the keyboards): "
	      "KEY_BRIGHTNESSUP,\n"
	      "                       KEY_BRIGHTNESSDOWN, KEY_VOLUMEUP, "
	      "KEY_VOLUMEDOWN, KEY_MUTE\n"
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
	if (optind < argc) {
		usage(stderr);
		return 2;
	}
	if (bl_test && (getuid() == 0 || geteuid() == 0 || *bl_test != '/')) {
		ERR("--backlight-dir: absolute path, for non-root tests only");
		return 2;
	}
	journal = getenv("JOURNAL_STREAM") != NULL;
	snd_lib_error_set_handler(alsa_error);
	if (load_conf(&conf))
		return 1;
	if (!test)
		return run();
	k = hotkey_by_name(test);
	if (k < 0) {
		ERR("unknown key \"%s\"", test);
		usage(stderr);
		return 2;
	}
	/* full state first: the state file never gets unknown values */
	if (!dry) {
		brightness(false, true);
		sound_read_state();
	}
	return action((enum hotkey)k) ? 1 : 0;
}
