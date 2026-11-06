# hotkeys-gpd

`gpd-hotkeysd`: hotkeys of the GPD Pocket without a desktop environment.

## Build

```bash
make
./build/gpd-hotkeysd --version
```

`make install` installs the binary under `$(PREFIX)/bin` (default
`/usr/local`), honouring `DESTDIR`.

## Tests

```bash
make test
```

The unit tests run with AddressSanitizer and UndefinedBehaviorSanitizer.
