# Make it yours

Dinghy is meant to be copied. This is how a copy becomes your firmware, gets released, and gets into the
[FM-1 Firmware Hub](https://thefirmwarehub.com) next to the others.

## 1. Copy

On GitHub: **Use this template** (or fork). Then build once before changing anything, so you know your setup
works: `./build.sh` and `tests/run_host.sh` (see the [README](../README.md)).

## 2. Name it

- `dinghy.c` is yours: rename it if you like (and the one line in `tools/build.py` that compiles it).
- At its top, before `#include "platform.c"`:

  ```c
  #define APP_NAME "MYSYNTH"        /* the rescue and the crash screen say this */
  ```

- The package file names (`dinghy.fwsc`) are in `tools/build.py` and `.github/workflows/release.yml`: search for
  `dinghy`.
- The update loader's screen (FM-1) shows `SCREEN_NAME` in `SCREEN_COLOUR` (RGB565), both in `tools/build.py`.
- Your data: a `STORE_MAGIC` of your own in `firmware/src/store.h`, so your firmware never reads another Dinghy fork's
  settings as its own; for the SMK-37 also `DATA_FAMILY` in `tools/build.py` (the store's owner tag, 1..7 of A-Z 0-9).
- Make your own logo; `assets/logo.svg` is Dinghy's.

## 3. Your package identity

The FM-1 reports the package identity of the firmware it runs. Installers and
the Hub use it to tell firmwares apart. WHYWHYWHY uses `FM-1_947113`.
For a fork, choose your own number with the
[Hub's identity checker](https://thefirmwarehub.com/submit/#check), then update
`FM1_PRODUCT` in `tools/build.py` and the FM-1 fallback in `firmware/src/platform.c`.

## 4. Release

```sh
./build.sh                      # -> build/whywhywhy_<VERSION>.fwsc
```

On GitHub, publish a release with a tag matching `VERSION` and attach the FM-1
`.fwsc`. If you push the tag first, the release workflow builds all targets,
runs the offline suite, and publishes the FM-1 package once the checks pass.

## 5. Into the hub

The [FM-1 Firmware Hub](https://thefirmwarehub.com) installs open-source FM-1 firmwares with one click,
and checks every night for new releases. To be listed:

- the source is public and GPL-3.0 (Dinghy already is: keep it so);
- the releases are on GitHub, each with its `.fwsc`;
- a screenshot of your firmware's screen: `tests/run_host.sh` makes one (`build/host/*.png`), or a photo;
- you ask: the hub's *"Made an open-source firmware?"* card says how.

After that every release you make shows up there by itself.

## 6. Keep the credits

Your firmware stands on Felucca (Leo Kuroshita, Hügelton Instruments) and SLOOP (isod89), on the SMK-37 also on
genkigenki's board layer. The GPL asks you to
keep their copyright lines and to give your users your source; it also is just fair. Keep `LICENSING.md` and
the headers, and add yourself.

## Ideas to start from

- A second oscillator, detuned: two phases, one `inc` a little bigger.
- An envelope per key with the four knobs (`ENC_KNOB1..4`) as attack, decay, sustain, release.
- A one-pole low-pass: `y += (x - y) * k >> 15` per frame; `k` from a knob.
- A step sequencer on the white keys, `BTN_PLAY` to start, the tempo on `ENC_SELECT`.
- Look at how zp12 or sloopDX do it: same platform, much bigger apps.
