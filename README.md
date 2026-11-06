# hotkeys-gpd

[![ci](https://github.com/goabonga/hotkeys-gpd/actions/workflows/ci.yml/badge.svg)](https://github.com/goabonga/hotkeys-gpd/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/goabonga/hotkeys-gpd)](https://github.com/goabonga/hotkeys-gpd/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

`gpd-hotkeysd`: hotkeys of the GPD Pocket without a desktop environment.

## Build

```bash
make
./build/gpd-hotkeysd --version
```

`make install` installs the binary under `$(PREFIX)/bin` (default
`/usr/local`), honouring `DESTDIR`.

## Brightness

`KEY_BRIGHTNESSUP` and `KEY_BRIGHTNESSDOWN` change the backlight by 5 %
of `max_brightness`, through `/sys/class/backlight/<name>/brightness`:

- below 10 % the step is 1 %, and a step crossing the 10 % threshold stops on
  it, in both directions;
- the brightness never goes below 1 (a black screen) nor above
  `max_brightness`;
- `backlight = auto` picks `intel_backlight`, else the first of the `raw`,
  `platform` and `firmware` types. Only a plain name whose link resolves under
  `/sys/devices/` is accepted.

A key press is simulated with `--test-event`; `-n` shows the action without
applying it:

```bash
gpd-hotkeysd -n -v --test-event KEY_BRIGHTNESSUP   # brightness 13 -> 18
```

## Configuration

`/etc/gpd-hotkeysd.conf` (`key = value`, `#` comments, all optional; select
another file with `-c`): `backlight`, `brightness_step` (1 to 50, in %),
`brightness_min` (raw floor). An unknown key, an invalid value or a line
longer than 511 bytes is reported and ignored: the default is kept.

## Tests

```bash
make test
```

The unit tests run with AddressSanitizer and UndefinedBehaviorSanitizer;
pass `SANITIZE=` to disable them on platforms that reject them. The daemon
tests use a fake backlight (`--backlight-dir`, an absolute path, refused as
root).

## Code quality

```bash
make format-check   # clang-format
make lint           # clang-tidy
make format         # apply clang-format
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
