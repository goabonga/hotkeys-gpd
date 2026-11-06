# hotkeys-gpd

[![ci](https://github.com/goabonga/hotkeys-gpd/actions/workflows/ci.yml/badge.svg)](https://github.com/goabonga/hotkeys-gpd/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/goabonga/hotkeys-gpd)](https://github.com/goabonga/hotkeys-gpd/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

`gpd-hotkeysd`: hotkeys of the GPD Pocket without a desktop environment.
It needs `libevdev`, `alsa-lib` and `pkg-config` to build.

## Build

```bash
make
./build/gpd-hotkeysd --version
```

`make install` honours `DESTDIR` and installs, under `$(PREFIX)` (default
`/usr/local`):

- `bin/gpd-hotkeysd`;
- `lib/systemd/system/gpd-hotkeysd.service`;
- `lib/udev/rules.d/90-gpd-hotkeys.rules`;

and `$(SYSCONFDIR)/gpd-hotkeysd.conf` (default `/etc`), a commented example
with the default values.

## Service

The unit runs the daemon without root: `DynamicUser=yes` with the groups
`input` (read `/dev/input`), `audio` (`/dev/snd`) and `video`, and a
restrictive systemd sandbox. The udev rule makes
`/sys/class/backlight/*/brightness` writable by the `video` group; apply it
without rebooting:

```bash
sudo udevadm control --reload
sudo udevadm trigger -s backlight -c add
sudo systemctl enable --now gpd-hotkeysd
```

Reload the configuration with `sudo systemctl reload gpd-hotkeysd`.
Installing does not enable anything.

## Listening to the keys

Without `--test-event`, `gpd-hotkeysd` stays in the foreground and reads
every `/dev/input/event*` advertising at least one of the handled keys,
detected by capabilities rather than by number:

- devices are opened read-only and never grabbed (no `EVIOCGRAB`, no
  `uinput`): the console and the compositor still receive every key;
- devices appearing or disappearing are followed through inotify on
  `/dev/input`;
- the same key reported by another device less than `dedup_ms` (100 ms)
  later is ignored (Consumer Control, Video Bus and Intel HID report one
  press); a held key repeats every `repeat_ms` (80 ms), never for mute;
- when an `acpi_video*` backlight exists, the `Video Bus` device is ignored
  because the kernel already handles it;
- `SIGHUP` reloads the configuration, `SIGTERM` and `SIGINT` stop the daemon.

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

## Volume and mute

`KEY_VOLUMEUP` and `KEY_VOLUMEDOWN` change the playback volume of the
`DAC1` control of the `chtrt5645` card by 2 dB, between its minimum and 0 dB;
volume up also unmutes. `KEY_MUTE` toggles the switches `DAC1 MIXL DAC1
Switch` and `DAC1 MIXR DAC1 Switch`. The speaker and headphone volumes are
never touched.

The card is opened by name (`hw:chtrt5645`) on every key press, so it may
appear after the daemon starts. When the control is missing, `Master` then
`PCM` are tried.

## State file

After each action the daemon writes the current values to
`/run/gpd-hotkeysd/state` (`state_file`, empty to disable, an absolute
path):

```
brightness=18
volume=100
mute=0
```

Values are percentages (`mute` is 0 or 1), `-1` meaning unknown. The file is
replaced atomically, is world-readable, and a `<state_file>.tmp` symbolic link
is never followed. `--test-event` reads the full state first so no value is
unknown. Nothing is written with `-n`.

A key press is simulated with `--test-event`; `-n` shows the action without
applying it:

```bash
gpd-hotkeysd -n -v --test-event KEY_BRIGHTNESSUP   # brightness 13 -> 18
gpd-hotkeysd -n -v --test-event KEY_VOLUMEUP
```

## Configuration

`/etc/gpd-hotkeysd.conf` (`key = value`, `#` comments, all optional; select
another file with `-c`): `backlight`, `brightness_step` (1 to 50, in %),
`brightness_min` (raw floor), `card`, `volume_control`, `mute_controls`
(comma-separated, empty to use the switch of the volume control),
`volume_step` (dB, or a percentage of the range such as `1%`),
`volume_max_db`, `dedup_ms`, `repeat_ms`, `state_file`. An unknown key, an invalid value or a line
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
