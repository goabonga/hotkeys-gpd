/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

#include "logic.h"

#include <stdio.h>
#include <string.h>

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

static void test_volume(void)
{
	/* 1/100 dB on a [-6525, 0] range */
	CHECK(volume_next(-900, 200, -6525, 0, true) == -700);
	CHECK(volume_next(-900, 200, -6525, 0, false) == -1100);
	CHECK(volume_next(-100, 200, -6525, 0, true) == 0);
	CHECK(volume_next(-6400, 200, -6525, 0, false) == -6525);
}

static void test_names(void)
{
	CHECK(hotkey_by_name("KEY_VOLUMEUP") == HK_VOLUME_UP);
	CHECK(hotkey_by_name("key_brightnessdown") == HK_BRIGHTNESS_DOWN);
	CHECK(hotkey_by_name("mute") == HK_MUTE);
	CHECK(hotkey_by_name("KEY_FOO") == -1);
	CHECK(strcmp(hotkey_names[HK_MUTE], "KEY_MUTE") == 0);
}

static void test_dedup(void)
{
	struct dedup d;

	/* device 1 is the Consumer Control, device 2 the Video Bus */
	memset(&d, 0, sizeof(d));
	CHECK(dedup_accept(&d, HK_BRIGHTNESS_UP, 1, 1, 1000, 100, 80));
	/* duplicate from another device, then releases */
	CHECK(!dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 1, 1020, 100, 80));
	CHECK(!dedup_accept(&d, HK_BRIGHTNESS_UP, 1, 0, 1030, 100, 80));
	CHECK(!dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 0, 1040, 100, 80));
	/* window elapsed, then same device */
	CHECK(dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 1, 1200, 100, 80));
	CHECK(dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 1, 1210, 100, 80));
	/* autorepeat: too fast, then at the repeat rate */
	CHECK(!dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 2, 1250, 100, 80));
	CHECK(dedup_accept(&d, HK_BRIGHTNESS_UP, 2, 2, 1300, 100, 80));
	/* repeat from a device that did not press */
	CHECK(!dedup_accept(&d, HK_BRIGHTNESS_UP, 1, 2, 1500, 100, 80));
	/* keys are independent */
	CHECK(dedup_accept(&d, HK_BRIGHTNESS_DOWN, 2, 1, 1305, 100, 80));
	/* mute never repeats */
	CHECK(dedup_accept(&d, HK_MUTE, 1, 1, 2000, 100, 80));
	CHECK(!dedup_accept(&d, HK_MUTE, 1, 2, 2500, 100, 80));
	CHECK(!dedup_accept(&d, HK_MUTE, 3, 1, 2050, 100, 80));
	/* repeat without a press */
	CHECK(!dedup_accept(&d, HK_VOLUME_UP, 1, 2, 3000, 100, 80));
	CHECK(!dedup_accept(&d, HK_COUNT, 1, 1, 3000, 100, 80));
}

int main(void)
{
	test_brightness();
	test_percent();
	test_volume();
	test_names();
	test_dedup();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	puts("all tests passed");
	return 0;
}
