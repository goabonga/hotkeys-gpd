/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/* Pure logic of gpd-hotkeysd: no hardware I/O, testable as a regular user. */

#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>
#include <stddef.h>

/* Handled keys (internal index). */
enum hotkey {
	HK_BRIGHTNESS_UP,
	HK_BRIGHTNESS_DOWN,
	HK_VOLUME_UP,
	HK_VOLUME_DOWN,
	HK_MUTE,
	HK_COUNT
};

#define CONF_STR 256
#define MAX_MUTE 4

struct conf {
	/* "auto" or a plain name under /sys/class/backlight */
	char backlight[CONF_STR];
	/* step in % of max_brightness */
	int brightness_step;
	/* raw floor (>= 1: never a black screen) */
	int brightness_min;
	/* ALSA card name */
	char card[CONF_STR];
	/* simple mixer element of the master volume */
	char volume_control[CONF_STR];
	/* switches (interface MIXER) toggled by mute */
	char mute_controls[MAX_MUTE][CONF_STR];
	int n_mute;
	/* volume step in 1/100 dB */
	long volume_step;
	/* volume step in % of the dB range (0: use volume_step) */
	int volume_step_pct;
	/* volume ceiling in 1/100 dB */
	long volume_max;
	/* window in which a press repeated by another device is ignored */
	int dedup_ms;
	/* minimum interval between two steps while a key is held */
	int repeat_ms;
	/* state file ("" disables it) */
	char state_file[CONF_STR];
};

void conf_default(struct conf *c);

/*
 * Applies one "key = value" line ('#' starts a comment). Returns 0, or -1
 * with a message in err and c untouched.
 */
int conf_line(struct conf *c, const char *line, char *err, size_t errlen);

/* Volume step in 1/100 dB over [min, max]: volume_step or its percentage. */
long volume_step_for(const struct conf *c, long min, long max);

/*
 * New raw brightness after one step of pct % of max, clamped to [min, max].
 * Below 10 % of max the step is 1 % (at least 1) and a step crossing the
 * 10 % threshold stops on it, in both directions. An invalid max (< 1)
 * leaves cur unchanged.
 */
int brightness_next(int cur, int max, int pct, int min, bool up);

/* Rounded percentage of v within [min, max]; 100 when the range is empty. */
int percent(long v, long min, long max);

/* New volume (1/100 dB) after one step, clamped to [min, max]. */
long volume_next(long cur, long step, long min, long max, bool up);

/*
 * Duplicate filtering (the same key reported by several devices) and
 * autorepeat rate limiting.
 */
struct dedup {
	struct {
		int dev;
		long press_ms;
		long action_ms;
		bool seen;
	} t[HK_COUNT];
};

/*
 * value: 0 release, 1 press, 2 repeat. Returns true if an action is due: a
 * press not duplicating another device's press less than dedup_ms earlier,
 * or a repeat from the pressing device at least repeat_ms after the last
 * action (never for mute).
 */
bool dedup_accept(struct dedup *d, enum hotkey k, int dev, int value,
		  long now_ms, int dedup_ms, int repeat_ms);

/* KEY_* name (case-insensitive, "KEY_" optional) to key; -1 if unknown. */
int hotkey_by_name(const char *name);

extern const char *const hotkey_names[HK_COUNT];

#endif /* LOGIC_H */
