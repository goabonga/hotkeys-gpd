/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

#include "logic.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #cond);                              \
			failures++;                                            \
		}                                                              \
	} while (0)

static void test_brightness(void)
{
	CHECK(brightness_next(13, 100, 5, 1, true) == 18);
	/* stops at the 10 % threshold going down */
	CHECK(brightness_next(13, 100, 5, 1, false) == 10);
	CHECK(brightness_next(8, 100, 5, 1, false) == 7);
	CHECK(brightness_next(105, 1000, 5, 1, false) == 100);
	CHECK(brightness_next(10, 100, 5, 1, false) == 9);
	CHECK(brightness_next(9, 100, 5, 1, true) == 10);
	CHECK(brightness_next(10, 100, 5, 1, true) == 15);
	/* never 0 */
	CHECK(brightness_next(1, 100, 5, 1, false) == 1);
	CHECK(brightness_next(0, 100, 5, 1, false) == 1);
	/* ceiling */
	CHECK(brightness_next(98, 100, 5, 1, true) == 100);
	CHECK(brightness_next(100, 100, 5, 1, true) == 100);
	/* other maximums, fine step of 1 % */
	CHECK(brightness_next(500, 1000, 5, 1, true) == 550);
	CHECK(brightness_next(50, 1000, 5, 1, false) == 40);
	/* minimum step of 1 */
	CHECK(brightness_next(3, 7, 5, 1, true) == 4);
	/* configured floor */
	CHECK(brightness_next(5, 100, 5, 20, false) == 20);
	/* invalid max: unchanged */
	CHECK(brightness_next(5, 0, 5, 1, true) == 5);
}

static void test_percent(void)
{
	CHECK(percent(13, 0, 100) == 13);
	CHECK(percent(0, -6525, 0) == 100);
	CHECK(percent(-6525, -6525, 0) == 0);
	CHECK(percent(-3262, -6525, 0) == 50);
	CHECK(percent(5, 5, 5) == 100);
	CHECK(percent(-9000, -6525, 0) == 0);
	CHECK(percent(500, 0, 100) == 100);
}

int main(void)
{
	test_brightness();
	test_percent();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	puts("all tests passed");
	return 0;
}
