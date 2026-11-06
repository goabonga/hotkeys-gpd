/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/* Pure logic of gpd-hotkeysd: no hardware I/O, testable as a regular user. */

#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>

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

#endif /* LOGIC_H */
