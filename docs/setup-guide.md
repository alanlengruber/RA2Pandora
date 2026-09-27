# Setup guide

This guide takes you from a Pandora Box DX running the stock Pandory menu to achievements unlocking in hardcore mode, step by step. Set aside an afternoon: most of the time goes into building the software and copying files to the drive.

When you are done, you will have:

- the new RetroArch and FinalBurn Neo on the board's USB drive, next to the stock system, which stays untouched;
- your games with achievements opening in the new RetroArch straight from the stock menu;
- a Raspberry Pi next to the cabinet, connected to the board by a USB-to-serial cable, carrying the achievement traffic to the internet.

```
Pandora Box DX                                             Raspberry Pi
RetroArch --HTTP--> pandora-relay --> CP2102 ==serial==> ra-relay-pi --HTTPS--> retroachievements.org
```

## 0. What you need

**Hardware**

| Item | Notes |
|---|---|
| Pandora Box DX with the [Pandory](https://github.com/TeamPandory/pandorytool) hack | Pandory lives on the board's USB drive (a stick or an SD card in a reader). |
| CP2102 USB-to-TTL adapter | 3.3 V logic, USB ID `10c4:ea60`. The board has no USB-serial drivers; RA2Pandora drives this chip directly, so other chips (CH340, FTDI, PL2303) will not work. |
| 3 female-to-female jumper wires | To connect the adapter to the Pi's pins. |
| Raspberry Pi with a network connection | Tested: Pi 3B on Wi-Fi. Other models whose GPIO UART is shared with Bluetooth (Zero W, 3, 4) should work the same way; the Pi 5 is untested. |
| microSD card (8 GB or more) and power supply for the Pi | |
| A PC | To build the software, prepare the Pi's card and copy files to the board's drive. |

**Accounts and files**

- A [RetroAchievements](https://retroachievements.org) account.
- Arcade ROMs that match the **current** FinalBurn Neo romsets. Most multi-game boards ship older sets; games whose set does not match keep running on the stock emulator. No ROMs are included in this project.

**Software on the PC**

- **Linux**, or **Windows with WSL 2** (Ubuntu). All commands below run in a Linux shell unless marked *(Windows)*.
- About 2 GB of free disk space.
- Build tools:

  ```sh
  sudo apt install git curl python3 make gcc binutils pkg-config bzip2 xz-utils
  ```

- [Raspberry Pi Imager](https://www.raspberrypi.com/software/), to prepare the Pi's card.

## 1. Back up the board's drive

Copy the whole USB drive to your PC before changing anything. RA2Pandora only adds new files and folders, with one exception: it replaces `pandory/pandory.xml`, the file that tells Pandory which emulator opens each game. Your backup is how you undo that.

If the drive is an SD card and your PC reports it as write-protected, check the lock slider on the side of the card or adapter.

## 2. Copy the board's system

The software is compiled against the board's own libraries (glibc 2.26, the Mali GPU driver, ALSA...), so it needs a copy of them.

1. Copy `deploy/roms_pandory/scripts/Diagnostico2.sh` from this repository to `roms_pandory/scripts/` on the drive.
2. Put the drive back in the board, turn it on and run **Diagnostico2** from the Scripts entries of the Pandory menu.
3. The screen stays still while it works. Wait until `diagnostico/concluido.txt` appears on the drive: that can take up to about 15 minutes, because the board writes to the drive slowly.
4. Turn the board off and take the drive to the PC.

Diagnostico2 only reads the board's system; it does not change it.

**After running any script, restart the board before playing.** Pandory sometimes keeps launching the last script instead of the game you pick.

## 3. Build

```sh
git clone https://github.com/alanlengruber/RA2Pandora.git
cd RA2Pandora
mkdir -p sysroot
cp /path/to/drive/diagnostico/rootfs.tar.gz sysroot/
```

On WSL, Windows drives appear under `/mnt/` (for example `/mnt/c/Users/you/Downloads`). Copying `rootfs.tar.gz` from the drive to your Windows Downloads folder first is the simplest route.

Then:

```sh
scripts/preparar-ambiente.sh   # downloads the toolchain, headers, RetroArch v1.22.2 and FBNeo
scripts/build-retroarch.sh     # out/retroarch, with the patches in patches/retroarch
scripts/build-fbneo.sh         # out/fbneo_libretro.so (the longest step)
scripts/build-tools.sh         # out/pandora-relay, out/pandora-coin, out/ra_trampolim_libretro.so
```

The toolchain is Bootlin's `armv7-eabihf--glibc--stable-2018.02-2`: GCC 6.4 and glibc 2.26, the same versions as the board. RetroArch and FBNeo are pinned to the versions this project was tested with.

## 4. Choose the games

Only games that have official achievements **and** a romset that matches the current FBNeo go to the new RetroArch; everything else stays on the stock emulator.

List the zips on the drive and match them against FBNeo and RetroAchievements:

```sh
# Linux, with the drive mounted:
scripts/cruzar-roms.py --roms /path/to/drive/roms
```

```powershell
# (Windows) list the zips first; this reads only the end of each zip, so it is fast:
powershell -ExecutionPolicy Bypass -File scripts\listar-zips.ps1 -Dir H:\roms -Out zips.tsv
```

```sh
# then, in WSL:
scripts/cruzar-roms.py --zips zips.tsv
```

This writes `data/cruzamento-roms.tsv`: for every zip FBNeo knows, whether the set is complete (checked by CRC, including parent sets and BIOS files), whether the game has achievements and whether it is vertical.

Then copy **your drive's** `pandory/pandory.xml` to `deploy/pandory/pandory.xml.original`. RA2Pandora never edits the original: it writes a copy with `core="ra_trampolim"` added to the chosen games.

## 5. Build the package and copy it to the drive

```sh
scripts/montar-pacote.sh
```

This creates `pacote/`, laid out like the root of the drive. Copy its **contents** onto the drive's root, merging with the folders already there. On Windows, WSL folders open in Explorer at `\\wsl.localhost\Ubuntu\home\<you>\RA2Pandora\pacote`.

| Path on the drive | What it is |
|---|---|
| `pandora-ra/` | the new RetroArch, FBNeo, relay, coin bridge and settings; everything the new RetroArch writes (saves, logs) stays here |
| `pandora-ra/raspberry-pi/` | the Raspberry Pi side, used in step 6 |
| `pandory/cores/ra_trampolim_libretro.so` | launcher core: the stock RetroArch loads it, and it hands the game over to the new one |
| `pandory/pandory.xml` | your `pandory.xml` with the game rules (replaces the original; keep your backup) |
| `roms_pandory/conquistas/` | one "(RA)" entry per game, at the end of the menu list |
| `roms_pandory/scripts/` | the cable test and the diagnostics |

## 6. Set up the Raspberry Pi

### 6.1 Prepare the card

In Raspberry Pi Imager:

1. Choose your Pi model and **Raspberry Pi OS Lite (64-bit)** (under *Raspberry Pi OS (other)*).
2. Choose the microSD card.
3. In the OS customisation settings:
   - **Hostname:** `pandora-relay` (any name works; the steps below use this one);
   - **Username and password:** your choice;
   - **Wireless LAN:** your network name, password and country (skip it if the Pi uses a network cable);
   - **Locale and time zone:** yours;
   - **Services:** enable **SSH**.
4. Write the card, put it in the Pi and turn it on. The first boot takes a couple of minutes.

### 6.2 Install the relay

From the PC, copy the Pi side of the package and log in:

```sh
scp -r pacote/pandora-ra/raspberry-pi <user>@pandora-relay.local:
ssh <user>@pandora-relay.local
```

If `pandora-relay.local` is not found (common inside WSL), use the Pi's IP address from your router's device list instead.

On the Pi:

```sh
cd raspberry-pi
sudo bash instalar-pi.sh
sudo reboot
```

The installer:

- frees the UART on pins 8 and 10 (on these models it belongs to Bluetooth: it adds `enable_uart=1` and `dtoverlay=disable-bt` to `config.txt`);
- removes the Linux serial console from that UART, so Linux does not write into the cable;
- installs the relay as the `ra-relay` service, which starts on every boot.

It keeps a `.antes-ra` copy of each system file it changes.

### 6.3 Check it

After the reboot, log in again and run:

```sh
systemctl status ra-relay
journalctl -u ra-relay -n 5
```

The service should be `active (running)`, and the log should end with a line like `escutando em /dev/ttyAMA0 a 921600 baud` ("listening on"; the relay's messages are in Portuguese).

### 6.4 Optional: protect the Pi's card from power cuts

If the Pi is switched off together with the cabinet, a power cut in the middle of a write can corrupt its card. The relay never needs to write to the card, so you can make the system read-only:

```sh
sudo raspi-config nonint do_overlayfs 0 && sudo reboot
```

With this on, every change you make on the Pi disappears at the next reboot. To update the relay later, turn it off first (`sudo raspi-config nonint do_overlayfs 1 && sudo reboot`), make the change, then turn it back on.

## 7. Wire the cable

Plug the CP2102 into the board's free USB port (the drive uses the other one) and connect it to the Pi's GPIO header:

| CP2102 | Raspberry Pi |
|---|---|
| TXD | pin 10 (GPIO15, RXD) |
| RXD | pin 8 (GPIO14, TXD) |
| GND | pin 6 (or any other GND pin) |
| 3V3 / +5V | not connected |

Pin 1 is at the end of the header away from the USB ports, and the even pins are on the row along the board's edge. Pins 2 and 4 carry 5 V: keep the wires away from them. If a case fan already uses pin 6, use another GND pin (9 or 14).

The adapter takes its power from the board's USB port and the Pi has its own supply, so only the two data wires and GND connect them.

## 8. Add your RetroAchievements account

On the drive, copy `pandora-ra/conta.cfg.exemplo` to `pandora-ra/conta.cfg` and fill in your username and password with a plain text editor (Notepad is fine). Achievements stay off while this file does not exist.

> **Use the username your account was created with.** If you renamed your RetroAchievements account, the login still expects the original name; the new one is only the display name.

The password is stored in plain text on the drive.

## 9. Test the cable

1. Turn on the Pi, then the board.
2. Run **TesteCabo** from the Scripts entries. The screen stays still for up to about 3 minutes while it downloads a large RetroAchievements response (~67 KB) three times over the cable, then three more times with all four CPU cores busy, as in a game.
3. Restart the board.
4. Open `pandora-ra/logs/cabo.txt` on the drive. The end of the file should have six `tentativa` ("attempt") lines, all with the same size and the same md5, and the relay's `pedido ... (gameslist): ok` lines. The exact size changes over time, as RetroAchievements adds games to the list.

If the attempts show no size, a `502 Bad Gateway` error or `SEM RESPOSTA` ("no reply"), check the wiring (TXD and RXD crossed, GND connected) and that the Pi's service is running. While the test runs, `journalctl -u ra-relay -f` on the Pi shows each request that arrives.

## 10. Play

1. Turn on the Pi before starting a game: achievements are loaded when the game starts.
2. Open a game from the stock menu or from the "(RA)" entries at the end of the list. `pandora-ra/jogos.txt` lists every game that goes to the new RetroArch.
3. A message at the top of the screen confirms the login, followed by "You have X of Y achievements unlocked".
4. When you unlock an achievement, its name appears at the top of the screen with a chime, and it shows up on your RetroAchievements profile.

Coins, both players and the stock button layout work as before. Hold Start for 2 seconds to leave a game. Hardcore mode is on, so save states, slow motion and cheats are disabled, as RetroAchievements requires.

If something goes wrong, see [Troubleshooting](../README.md#troubleshooting) in the README.

## Updating

1. On the PC: `git pull`, run the build scripts from step 3 again, then `scripts/montar-pacote.sh`.
2. Copy `pacote/` onto the drive again. Your `conta.cfg`, saves and logs are not in the package, so they stay as they are.
3. If `tools/ra-relay/ra-relay-pi.py` changed, copy `pacote/pandora-ra/raspberry-pi/` to the Pi and run `sudo bash instalar-pi.sh` again (turn off the read-only mode first if you enabled it).

## Undoing everything

1. Restore `pandory/pandory.xml` from your backup.
2. Delete `pandora-ra/`, `pandory/cores/ra_trampolim_libretro.so`, `roms_pandory/conquistas/` and the scripts added to `roms_pandory/scripts/`.

The board is then back to its stock state. On the Pi, the `.antes-ra` files next to `config.txt` and `cmdline.txt` hold the original settings.
