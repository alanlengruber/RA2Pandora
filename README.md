# RA2Pandora

[RetroAchievements](https://retroachievements.org) on a **Pandora Box DX** multi-game arcade board, in hardcore mode, on the original cabinet and CRT.

The board's stock emulator is an old, closed RetroArch 1.7.4 with no networking, and the board itself has no network interface at all. RA2Pandora adds, next to the stock system and without replacing it:

- **RetroArch 1.22.2** and a current **FinalBurn Neo** core, cross-compiled for the board;
- a small launcher, so the games you choose open in the new RetroArch straight from the **stock Pandory menu**;
- a **serial relay**: the board talks to a **Raspberry Pi** over a USB-to-serial cable, and the Pi carries the RetroAchievements traffic to the internet.

```
Pandora Box DX                                             Raspberry Pi
RetroArch --HTTP--> pandora-relay --> CP2102 ==serial==> ra-relay-pi --HTTPS--> retroachievements.org
 (127.0.0.1:8080)                     (USB port)          (GPIO UART)
```

On the arcade side everything works as before: coins from the coin mechanism, both players, the stock button layout, **hold Start for 2 s to leave a game**. Achievement notifications are drawn at the top of the screen.

## Project status

Tested on a single Pandora Box DX (Rockchip RK3128, Mali-400, 384x224 CRT output) running the [Pandory](https://github.com/TeamPandory/pandorytool) hack, with a Raspberry Pi 3B as the relay.

| Part | Status |
|---|---|
| RetroArch 1.22.2 + FBNeo on the board: full speed, sound, coins, controls, vertical games | working |
| Games opened from the stock Pandory menu | working |
| On-screen notifications | working |
| Relay: board -> CP2102 -> Pi -> RetroAchievements (login, achievement sets, unlocks) | working |
| Unlock sound | added, awaiting hardware confirmation |

The code comments and some script names are in Portuguese.

## What you need

- A **Pandora Box DX** with the **Pandory** hack on its USB drive (the stick or SD card the board boots its games from).
- A **CP2102 USB-to-TTL adapter** (3.3 V logic, USB ID `10c4:ea60`) and 3 female-to-female jumper wires.
- A **Raspberry Pi** with a GPIO UART and internet access (tested: Pi 3B with Raspberry Pi OS Lite, Debian 13).
- A **RetroAchievements account**.
- A **Linux** machine (or WSL) to build: `git`, `curl`, `python3`, `make`, `gcc`, `binutils` (`ar`), `pkg-config`.
- **Arcade ROMs that match the current FBNeo romsets.** Most multi-game boards ship older sets (made for FBAlpha 2012 or MAME 2003); games whose sets do not match simply keep running on the stock emulator. No ROMs are included here.

## 1. Get a copy of the board's system

The build links against the board's own libraries (glibc 2.26, the Mali GPU driver, ALSA...), so it needs a copy of them.

1. Copy `deploy/roms_pandory/scripts/Diagnostico2.sh` to `roms_pandory/scripts/` on the board's USB drive.
2. On the board, run **Diagnostico2** from the Scripts entries of the menu. The screen stays still; wait **several minutes, up to ~15** (the board writes slowly to the drive).
3. When `diagnostico/concluido.txt` exists on the drive, copy `diagnostico/rootfs.tar.gz` to `sysroot/rootfs.tar.gz` in this repository.

`Diagnostico2` only reads the board's system; it does not change anything on it.

## 2. Build

```sh
scripts/preparar-ambiente.sh   # toolchain, sysroot, RetroArch v1.22.2 and FBNeo sources
scripts/build-retroarch.sh     # out/retroarch (applies patches/retroarch automatically)
scripts/build-fbneo.sh         # out/fbneo_libretro.so (takes a while)
scripts/build-tools.sh         # out/pandora-coin, out/pandora-relay, out/ra_trampolim_libretro.so
```

The toolchain is Bootlin's `armv7-eabihf--glibc--stable-2018.02-2`: GCC 6.4 and glibc 2.26, the same versions as the board.

## 3. Pick the games

Only games that have official achievements **and** a romset compatible with the current FBNeo are sent to the new RetroArch; everything else stays on the stock emulator.

```sh
# From Linux, with the drive mounted:
scripts/cruzar-roms.py --roms /path/to/drive/roms

# Or on Windows, list the zips first (reads only the end of each zip):
#   powershell -ExecutionPolicy Bypass -File scripts\listar-zips.ps1 -Dir H:\roms -Out zips.tsv
scripts/cruzar-roms.py --zips zips.tsv
```

This writes `data/cruzamento-roms.tsv`: for every zip FBNeo knows, whether the set is complete (checked by CRC, including parent sets and BIOS), whether it has achievements, and whether it is vertical. On the tested drive: 3224 zips, 314 games with achievements and a compatible set.

Then copy the **`pandory/pandory.xml` from your drive** to `deploy/pandory/pandory.xml.original`. RA2Pandora edits a copy of it: it adds `core="ra_trampolim"` to those 314 games and nothing else.

## 4. Package and copy to the drive

```sh
scripts/montar-pacote.sh
```

This builds `pacote/` with the same layout as the drive's root. **Back up the drive first**, then copy the contents of `pacote/` to the drive's root:

| Path on the drive | What it is |
|---|---|
| `pandora-ra/` | new RetroArch, FBNeo, relay, coin bridge, configuration; everything the new RetroArch writes (saves, logs) stays here |
| `pandory/cores/ra_trampolim_libretro.so` | launcher core: the stock RetroArch loads it, and it hands the game over to the new one |
| `pandory/pandory.xml` | your `pandory.xml` with the game rules (replaces the original: keep the backup) |
| `roms_pandory/conquistas/` | one "(RA)" entry per game at the end of the menu list |
| `roms_pandory/scripts/` | cable test and diagnostics |

To undo everything, restore your original `pandory.xml` and delete these files.

## 5. Wire the cable

Plug the CP2102 into the board's free USB port and connect it to the Pi's GPIO header. Physical pin numbers: pin 1 is at the end of the header away from the USB ports, and even pins are on the row along the board's edge.

| CP2102 | Raspberry Pi |
|---|---|
| TXD | pin 10 (GPIO15, RXD) |
| RXD | pin 8 (GPIO14, TXD) |
| GND | pin 6 (or any other GND pin) |
| 3V3 / +5V | not connected |

Pins 2 and 4 are 5 V: keep the wires away from them. If a case fan already uses pin 6, pick another GND pin (9 or 14).

## 6. Set up the Raspberry Pi

Copy `pandora-ra/raspberry-pi/` (or `tools/ra-relay/`) to the Pi and run:

```sh
sudo ./instalar-pi.sh
sudo reboot
```

The installer frees the GPIO UART (on the Pi 3 it belongs to Bluetooth: it adds `dtoverlay=disable-bt`), removes the Linux serial console from it, and installs the relay as a systemd service that starts on boot. It keeps a `.antes-ra` backup of each system file it changes. Check it with `systemctl status ra-relay` and `journalctl -u ra-relay -f`.

The relay needs no writes, so if the Pi shares the cabinet's power switch you can protect its SD card from power cuts with a read-only root:

```sh
sudo raspi-config nonint do_overlayfs 0 && sudo reboot   # 1 turns it off again
```

## 7. Log in to RetroAchievements

Copy `pandora-ra/conta.cfg.exemplo` to `pandora-ra/conta.cfg` on the drive and fill in your account. Achievements stay off while this file does not exist.

> **Use your original username.** If you renamed your RetroAchievements account, the login API still expects the name the account was created with; the new name is only the display name.

The password is stored in plain text on the drive.

## Playing

- Open a game from the stock menu (or from the "(RA)" entries at the end of the list). The first message at the top of the screen confirms the login.
- **Coins** come from the cabinet's coin mechanism as usual.
- **Hold Start for 2 seconds** to leave the game.
- A chime plays when an achievement unlocks. It is `pandora-ra/assets/sounds/unlock.wav`, generated by `scripts/gerar-som-conquista.py`; replace it with your own `unlock.wav` or `unlock.ogg` to change it.
- Hardcore mode is on: save states, slow motion and cheats are disabled, as RetroAchievements requires.
- Turn on the Pi before starting a game: achievements are loaded when the game starts. If a game was opened before the Pi was ready, leave it and open it again.

## Troubleshooting

- **Logs:** `pandora-ra/logs/retroarch.txt` (RetroArch), `relay.txt` (board side of the relay), `coin.txt`; on the Pi, `journalctl -u ra-relay`.
- **Cable test:** run **TesteCabo** from the menu; it downloads a large RetroAchievements response (~67 KB) three times over the cable, then three more times with all four CPU cores busy (as in a game), and writes the result to `pandora-ra/logs/cabo.txt`. On the Pi, `sudo cat /proc/tty/driver/ttyAMA` shows the bytes received (`rx`) and sent (`tx`).
- **Relay log:** each line of `relay.txt` is one request: the API call (`login2`, `patch`, `awardachievement`...), its size and time, and how many chunks had to be requested again. Passwords and tokens are never logged.
- **Restart the board after running any script.** After a script from the Scripts entries, Pandory may keep launching that script instead of the next game you pick.
- **ColetarLog** copies Pandory's own log (`/tmp/pandory.txt`) to `pandora-ra/logs-pandory/`.
- **"Hardcore paused. Setting not allowed"** means a core option RetroAchievements forbids is on. `run.sh` already turns off FBNeo's patched romsets, the only one that is on by default.

## How it works

- **Video:** RetroArch's plain `drm` driver, patched (`patches/retroarch`): it skips duplicated frames instead of crashing, fills the non-square-pixel 384x224 mode, flips pages without blocking, rotates vertical games in software and draws notifications with the bitmap font. The GL driver on this Mali-400 caps the game at ~30 FPS unless it runs threaded, and threaded GL drops about 8% of the frames.
- **Audio:** the codec only plays correctly in 16-bit; asking for 32-bit (what ALSA picks for RetroArch's float output) plays at half speed and garbled. `pandora-ra/.asoundrc` forces 16-bit and keeps the board's volume control.
- **Launcher:** Pandory always starts `/usr/bin/retroarch`, and passes only a placeholder where the ROM path should be (the real path is in `/tmp/retro_tmp`). `ra_trampolim` is a libretro core that reads the real path, checks it against `pandora-ra/jogos.txt` and `exec`s `pandora-ra/run.sh`.
- **Coins:** the coin mechanism is read by the stock `emulotar` daemon, which bumps a counter in `/tmp/pipe1`. `pandora-coin` watches it and presses Select through RetroArch's Remote RetroPad (UDP on the loopback, which boots down on this board and is brought up by `run.sh`).
- **Relay:** RetroArch's `cheevos_custom_host` points at `pandora-relay` on `127.0.0.1:8080`, which wraps each HTTP request in a CRC-checked frame and drives the CP2102 directly through usbfs (the board's kernel has no USB-serial drivers). Lost frames are resent; the Pi answers repeated requests from a cache, so an achievement is never submitted twice. See `tools/ra-relay/PROTOCOLO.md`.
- **Large responses:** the CP2102 sits on the board's DWC2 USB port, whose transfers depend on the CPU; with a game running, long bursts from the Pi lose bytes (a 21 KB achievement set never arrived whole). So the board pulls large responses in chunks of up to 1 KB, one at a time, and asks again only for a chunk that went missing; chunks shrink while the cable is losing data. The relay also runs with real-time priority and locked memory, so the game cannot starve it.

## Credits and licenses

This repository contains build scripts, patches and small tools; it does not include RetroArch, FBNeo or any ROM.

- [RetroArch](https://github.com/libretro/RetroArch) and the patches in `patches/retroarch` (GPLv3).
- [FinalBurn Neo](https://github.com/libretro/FBNeo) (non-commercial license).
- [rcheevos](https://github.com/RetroAchievements/rcheevos) (MIT), built into RetroArch.
- [Pandory](https://github.com/TeamPandory/pandorytool) by Team Pandory (GPL 2.0), the hack this project builds on.
