# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/).

## [0.1.0] - 2026-11-06

### Added

- add the gpd-hotkeysd command with --version and --help (`c84c58f`)
- add brightness stepping with a fine step below 10 percent (`65004fe`)
- add volume stepping clamped to the decibel range (`89000f5`)
- add hotkey names and duplicate event filtering (`a150dec`)
- add configuration file parsing with validated defaults (`7e2d9eb`)
- change the brightness with --test-event, optionally in dry-run (`78a3b06`)
- handle the volume and mute keys through ALSA (`d9f4490`)
- publish brightness, volume and mute in a state file (`c071c2f`)
- listen to the input devices and follow their hotplug (`2d493b6`)
- install a hardened systemd unit, a udev rule and an example configuration (`bb5b241`)
