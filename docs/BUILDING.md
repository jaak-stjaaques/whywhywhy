# Build and test WHYWHYWHY

The FM‑1 package for version 0.1.0 is `build/whywhywhy_0.1.0.fwsc`. The device's
installer identifies WHYWHYWHY as `FM-1_947113`.

## Build on a computer

Use Python 3 with Pillow, the JieLi toolchain, and the AC79 SDK. The toolchain
runs on x86‑64 Linux. On macOS, the build uses a Linux Docker container.

```sh
python3 -m pip install Pillow
sh tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
sh build.sh
```

Use `JIELI_TOOLCHAIN` and `AC79_SDK` to point at copies you already have.
`sh tests/run_host.sh` runs the instrument on the computer and produces screen
images and a WAV in `build/host/`; `build/host/dinghy build/film/f tests/scripts/demo.txt 25` makes a film (one
image every 1/25 s; `ffmpeg` joins them). `cc -O2 -Ifirmware/src -o build/measure tools/measure_levels.c -lm` measures
the six algorithms' loudness. `sh tests/run_tests.sh` runs the offline test
suite after the FM‑1 build. See the [Dinghy upstream documentation](https://github.com/zvenson/dinghy)
for the platform setup, and [API](API.md) and [hardware rules](RULES.md) for the
inherited low-level interfaces.

## First hardware install and recovery

The offline suite exercises the synth, loops, updater, and package checks. An
actual USB install and power-cycle test on a physical FM‑1 remain to be
verified for WHYWHYWHY 0.1.0.

Before installing, make a full recovery backup of your FM‑1 and verify that
your recovery tool can open it. In **M‑Flasher**, choose **Install firmware
(.fwsc)** and select `whywhywhy_0.1.0.fwsc`. Leave the bootloader option unchecked
and review the detected model and planned changes before confirming.
[M‑Flasher installation guide](https://github.com/jvitkauskas/m-flasher/blob/main/docs/usage.md#the-actions)

You can also use the command-line USB installer. Install its MIDI dependencies
with `python3 -m pip install mido python-rtmidi` and connect the FM‑1 over USB
with a data cable. The installer asks before it writes.

```sh
python3 tools/fm1_install.py --info
python3 tools/fm1_install.py build/whywhywhy_0.1.0.fwsc
```

If the installed firmware fails to start, hold **OCT−** during power-on to
enter its USB rescue mode. The Firmware Hub documents recovery options and
returning to official firmware. [FM‑1 rescue guide](https://thefirmwarehub.com/tools/rescue/)
