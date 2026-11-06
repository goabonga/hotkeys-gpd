/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

#include "logic.h"

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
