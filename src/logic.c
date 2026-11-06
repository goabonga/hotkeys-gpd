/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

#include "logic.h"

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
