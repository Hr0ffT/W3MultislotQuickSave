# W3MultislotQuickSave

Rotating quick saves and a quick load key for **Warcraft III 1.26a, 1.27b and 1.28.5**.

[Русская версия](README.ru.md)

The game's quick save (F6) always overwrites the same file, and there is no quick load at all. With this mod:

- **F6** saves to `QuickSave_1`, `QuickSave_2`, ... `QuickSave_10` in turn, then starts over at 1, so the last ten quick saves are kept.
- **F8** loads the newest of them.
- The keys and the number of slots are set in the ini.

Both go through the game's own code: the quick save is the game's own (with its own checks, so it works where F6 works: single player), and the quick load is a quick load routine that exists in the game but is never called. No game files are modified.

## Requirements

- Warcraft III 1.26a (Game.dll 1.26.0.6401), 1.27b (Game.dll 1.27.1.7085) or 1.28.5 (Game.dll 1.28.5.7680). The mod checks the version and does nothing on any other patch.

## Installation

1. Copy `W3MultislotQuickSave.mix` and `W3MultislotQuickSave.ini` into the Warcraft III folder, next to `war3.exe`.
2. Start the game. The `.mix` is loaded automatically, no launcher needed.

Works alongside other `.mix` mods, for example [W3TrueWidescreen](https://github.com/Hr0ffT/W3TrueWidescreen).

To uninstall, delete `W3MultislotQuickSave.mix` and `W3MultislotQuickSave.ini`. The quick saves stay in your `Save` folder as ordinary saves and can also be loaded from the game's Load menu.

## Settings

All in `W3MultislotQuickSave.ini`:

| Setting | Default | What it does |
|---|---|---|
| `Slots` | 10 | Number of quick save files (1..99) |
| `QuickSaveKey` | F6 | Quick save key. Another key works as F6 does; F6 keeps working too |
| `QuickLoadKey` | F8 | Quick load key: loads the newest quick save. The key does nothing else in the game then |
| `Debug` | 0 | 1 = write `W3MultislotQuickSave.log` next to the game (for bug reports) |

Keys: `F1`..`F12`, a letter or a digit, or a virtual-key code like `0x75`.

## Notes

- The newest save is the one written last (by file time). If there is no `QuickSave_N` yet, F8 loads the game's own `QuickSave`.
- Quick load does nothing outside a game and in multiplayer, like the game's own routine.

## Changelog

- **1.2** — Works on 1.28.5 too.
- **1.1** — Works on 1.27b too. Fixed a crash when the sound provider is changed in the game's options (e.g. EAX switched on).
- **1.0** — First release.

## Building from source

```
i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s -o W3MultislotQuickSave.mix src/W3MultislotQuickSave.c -lversion
```

## License

MIT, see [LICENSE](LICENSE).
