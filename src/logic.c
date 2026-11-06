/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

#include "logic.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int brightness_next(int cur, int max, int pct, int min, bool up)
{
	int step;
	int threshold;
	int v;

	if (max < 1)
		return cur;
	if (min > max)
		min = max;
	step = (max * pct + 50) / 100;
	threshold = max / 10;
	if (step < 1)
		step = 1;
	/* below 10 % the eye is more sensitive: fine step of 1 % (at least 1)
	 */
	if (up ? cur < threshold : cur <= threshold)
		step = max / 100 > 1 ? max / 100 : 1;
	v = up ? cur + step : cur - step;
	if (up && cur < threshold && v > threshold)
		v = threshold;
	/* symmetric: also stop at the threshold when going down */
	if (!up && cur > threshold && v < threshold)
		v = threshold;
	if (v < min)
		return min;
	return v > max ? max : v;
}

int percent(long v, long min, long max)
{
	if (max <= min)
		return 100;
	if (v <= min)
		return 0;
	if (v >= max)
		return 100;
	return (int)(((v - min) * 100 + (max - min) / 2) / (max - min));
}

long volume_next(long cur, long step, long min, long max, bool up)
{
	long v = up ? cur + step : cur - step;

	if (v < min)
		return min;
	return v > max ? max : v;
}

const char *const hotkey_names[HK_COUNT] = {
    "KEY_BRIGHTNESSUP", "KEY_BRIGHTNESSDOWN", "KEY_VOLUMEUP",
    "KEY_VOLUMEDOWN",	"KEY_MUTE",
};

int hotkey_by_name(const char *name)
{
	for (int i = 0; i < HK_COUNT; i++)
		if (strcasecmp(name, hotkey_names[i]) == 0 ||
		    strcasecmp(name, hotkey_names[i] + 4) == 0)
			return i;
	return -1;
}

bool dedup_accept(struct dedup *d, enum hotkey k, int dev, int value,
		  long now_ms, int dedup_ms, int repeat_ms)
{
	if ((int)k < 0 || k >= HK_COUNT)
		return false;
	if (value == 1) {
		/* same key from another device right after: duplicate */
		if (d->t[k].seen && d->t[k].dev != dev &&
		    now_ms - d->t[k].press_ms < dedup_ms)
			return false;
		d->t[k].seen = true;
		d->t[k].dev = dev;
		d->t[k].press_ms = now_ms;
		d->t[k].action_ms = now_ms;
		return true;
	}
	if (value == 2 && k != HK_MUTE && d->t[k].seen && d->t[k].dev == dev &&
	    now_ms - d->t[k].action_ms >= repeat_ms) {
		d->t[k].action_ms = now_ms;
		return true;
	}
	/* release, repeat too fast, from another device, or mute */
	return false;
}

/* Copies s into a CONF_STR buffer. */
static void copy_str(char *dst, const char *s)
{
	snprintf(dst, CONF_STR, "%s", s);
}

void conf_default(struct conf *c)
{
	memset(c, 0, sizeof(*c));
	copy_str(c->backlight, "auto");
	c->brightness_step = 5;
	c->brightness_min = 1;
	copy_str(c->card, "chtrt5645");
	copy_str(c->volume_control, "DAC1");
	copy_str(c->mute_controls[0], "DAC1 MIXL DAC1 Switch");
	copy_str(c->mute_controls[1], "DAC1 MIXR DAC1 Switch");
	c->n_mute = 2;
	c->volume_step = 200;
	c->dedup_ms = 100;
	c->repeat_ms = 80;
	copy_str(c->state_file, "/run/gpd-hotkeysd/state");
}

/* Copies s[0..n) without leading and trailing whitespace. */
static void copy_trimmed(char *dst, size_t size, const char *s, size_t n)
{
	while (n && isspace((unsigned char)*s)) {
		s++;
		n--;
	}
	while (n && isspace((unsigned char)s[n - 1]))
		n--;
	if (n >= size)
		n = size - 1;
	memcpy(dst, s, n);
	dst[n] = '\0';
}

static int parse_int(const char *v, long lo, long hi, long *out)
{
	char *end;
	long x;

	errno = 0;
	x = strtol(v, &end, 10);
	if (errno || end == v || *end || x < lo || x > hi)
		return -1;
	*out = x;
	return 0;
}

/* Decibels (real number) to 1/100 dB. */
static int parse_db(const char *v, double lo, double hi, long *out)
{
	char *end;
	double x;

	errno = 0;
	x = strtod(v, &end);
	if (errno || end == v || *end || !(x >= lo && x <= hi))
		return -1;
	*out = lround(x * 100.0);
	return 0;
}

static int set_name(char *dst, const char *key, const char *val)
{
	if (!*val)
		return -1;
	/* backlight: a plain name under /sys/class/backlight */
	if (!strcmp(key, "backlight") &&
	    (strchr(val, '/') || !strcmp(val, ".") || !strcmp(val, "..")))
		return -1;
	copy_str(dst, val);
	return 0;
}

static void set_mute_controls(struct conf *c, const char *val)
{
	int k = 0;

	for (const char *p = val; *p && k < MAX_MUTE;) {
		const char *comma = strchr(p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen(p);

		copy_trimmed(c->mute_controls[k], CONF_STR, p, len);
		if (c->mute_controls[k][0])
			k++;
		p += len + (comma != NULL);
	}
	/* 0: fall back to the switch of the volume control, if any */
	c->n_mute = k;
}

static int set_volume_step(struct conf *c, const char *val)
{
	size_t n = strlen(val);
	char num[CONF_STR];
	long pct;

	if (n > 1 && val[n - 1] == '%') {
		snprintf(num, sizeof(num), "%.*s", (int)(n - 1), val);
		if (parse_int(num, 1, 50, &pct))
			return -1;
		c->volume_step_pct = (int)pct;
		return 0;
	}
	if (parse_db(val, 0.1, 20, &c->volume_step))
		return -1;
	c->volume_step_pct = 0;
	return 0;
}

/* Integer settings: key, bounds, offset of the field. */
static const struct {
	const char *key;
	long lo;
	long hi;
	size_t off;
} int_keys[] = {
    {"brightness_step", 1, 50, offsetof(struct conf, brightness_step)},
    {"brightness_min", 1, 1000000, offsetof(struct conf, brightness_min)},
    {"dedup_ms", 0, 2000, offsetof(struct conf, dedup_ms)},
    {"repeat_ms", 0, 2000, offsetof(struct conf, repeat_ms)},
};

static int apply(struct conf *c, const char *key, const char *val)
{
	long x;

	for (size_t i = 0; i < sizeof(int_keys) / sizeof(*int_keys); i++) {
		if (strcmp(key, int_keys[i].key) != 0)
			continue;
		if (parse_int(val, int_keys[i].lo, int_keys[i].hi, &x))
			return -1;
		*(int *)((char *)c + int_keys[i].off) = (int)x;
		return 0;
	}
	if (!strcmp(key, "backlight"))
		return set_name(c->backlight, key, val);
	if (!strcmp(key, "card"))
		return set_name(c->card, key, val);
	if (!strcmp(key, "volume_control"))
		return set_name(c->volume_control, key, val);
	if (!strcmp(key, "state_file")) {
		if (*val && *val != '/')
			return -1;
		copy_str(c->state_file, val);
		return 0;
	}
	if (!strcmp(key, "mute_controls")) {
		set_mute_controls(c, val);
		return 0;
	}
	if (!strcmp(key, "volume_step"))
		return set_volume_step(c, val);
	if (!strcmp(key, "volume_max_db"))
		return parse_db(val, -60, 12, &c->volume_max);
	return -2;
}

int conf_line(struct conf *c, const char *line, char *err, size_t errlen)
{
	char key[64];
	char val[CONF_STR];
	const char *hash = strchr(line, '#');
	size_t n = hash ? (size_t)(hash - line) : strlen(line);
	const char *eq;
	int r;

	copy_trimmed(val, sizeof(val), line, n);
	if (!*val)
		return 0; /* empty line or comment */
	eq = memchr(line, '=', n);
	if (!eq) {
		snprintf(err, errlen, "missing \"=\"");
		return -1;
	}
	copy_trimmed(key, sizeof(key), line, (size_t)(eq - line));
	copy_trimmed(val, sizeof(val), eq + 1, n - (size_t)(eq - line) - 1);
	if (strlen(val) >= CONF_STR - 1) {
		snprintf(err, errlen, "%s: value too long", key);
		return -1;
	}
	r = apply(c, key, val);
	if (r == -2)
		snprintf(err, errlen, "unknown key \"%s\"", key);
	else if (r)
		snprintf(err, errlen, "%s: invalid value \"%s\"", key, val);
	return r ? -1 : 0;
}

long volume_step_for(const struct conf *c, long min, long max)
{
	if (c->volume_step_pct)
		return (max - min) * c->volume_step_pct / 100;
	return c->volume_step;
}
