/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/* gpd-hotkeysd: GPD Pocket hotkeys, without a desktop environment. */

#include "version.h"

#include <getopt.h>
#include <stdio.h>

static void usage(FILE *f)
{
	fputs("usage: gpd-hotkeysd [-h] [-V]\n"
	      "  -h, --help           this help\n"
	      "  -V, --version        version\n",
	      f);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'V' },
		{ NULL, 0, NULL, 0 },
	};
	int o;

	while ((o = getopt_long(argc, argv, "hV", opts, NULL)) != -1) {
		switch (o) {
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
	usage(stdout);
	return 0;
}
