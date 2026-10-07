# ESPsoup firmware (ESP-SDR fork for the ESP32-C5)

This is **ESPsoup firmware**, a fork of [ESPARGOS ESP-SDR](https://github.com/ESPARGOS/esp-sdr)
with receive-only extensions for the **ESP32-C5**. It is the firmware behind the
ESPsoup SDR app, and it stays compatible with the upstream protocol and the
[ESP-SDR browser viewer](https://espargos.net/espsdr/app/): every upstream
command and reply is unchanged, new features are announced by new `CAPS`
tokens. All other chips build exactly as upstream.

| ESP32-C5 feature | Commands (CAPS) | Measured |
| --- | --- | --- |
| Register-level capture engine (no 1.7 ms vendor stall) | all captures, upstream `SPEC` snapshots | 80 MS/s capture 1886 → 206 µs |
| Long contiguous snapshots | `CAP*` up to 32,704 samples (`BIGCAP`), `CAPT16/20` timestamps | 8.2 ms at 4 MS/s |
| Anti-aliased decimation | `DDC8/DDC16` | 9–13 dB lower alias floor |
| On-device spectra / panorama | `SPEC <rate> <nfft> <navg>` (`SPECDB`), `SWEEP` | 100–6000 MHz in 0.69 s |
| Continuous capture with exact gap accounting | `STREAM8/16`, `CSPEC`, `CPKT`, `CGRID`, `CSTAT?` | 91–96 % of all samples at 4–10 MS/s |
| 5 GHz tuning via the Wi-Fi driver path | `TUNEM` | exact 4790–5990 MHz (stock path −20 dB above ~5.75 GHz) |
| FM-video demodulator (analog FPV) | `VFM` (`DEMODFM`) | 15,625 PAL lines/s, 100 % duty on PARLIO |
| GFSK / BLE packet catcher | `GFSK` (`GFSKPKT`) | 62–78 CRC-valid BLE adverts/s per channel |
| Gap-free 40 MS/s 4+4-bit path | `PCAP`, `PRUN`, `VFMSRC`, `STRSRC` (`PARLIO`) | 62 s, 2.48·10⁹ bytes, 0 overruns |
| Hardware DC calibration (port of the S31 `dc.c`) | `DCCAL`, `DCAUTO` | 5 GHz DC −27…−47 dB |
| 1 kHz tuning steps, temperature | `FREQK`, `TEMP?` | |

Supported boards: ESP32-C5 boards with native USB (tested: ESP32-C5-DevKitC-1
type boards and the Seeed Studio XIAO ESP32-C5), 2 MB flash or more.
Version: `FWOPT?` → `FWOPT 0.7.0 espsoup rx-only C5`.

* Protocol, measurements and limitations: [docs/espsoup-c5.md](docs/espsoup-c5.md)
* License: GPL-3.0 (ESP-SDR is GPL-3.0-or-later; see [NOTICE](NOTICE) for the
  ESPsoup additions and the note on C5VRX-inspired parts)
* Thanks to Florian Euchner, Zoltan Doczi and everyone at
  [ESPARGOS](https://espargos.net/) for ESP-SDR and for releasing it under the GPL.

### Build and flash the ESPsoup C5 firmware

Use the ESP-IDF commit pinned for `esp32c5` in [firmware-targets.json](firmware-targets.json),
then build like any other profile:

```sh
git submodule update --init
idf.py -B build-c5 -DIDF_TARGET=esp32c5 -DSDKCONFIG=sdkconfig.c5 \
  -DSDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32c5 build
idf.py -B build-c5 -p /dev/ttyACM0 flash
```

ESP32-C5 by default uses internal antenna, can be switched to external by adding `-DESP_SDR_C5_ANTENNA=EXTERNAL` build flag.

On Windows PowerShell quote the `-D` arguments (`"-DIDF_TARGET=esp32c5"`).
`python tools/espsoup_release.py --build-dir build-c5 --version 0.7.0 --output release`
packages a build (bootloader, partition table, application, `manifest.json`,
`flash_args`, `SHA256SUMS`). A release can be flashed with
`python -m esptool --chip esp32c5 -p PORT -b 921600 write-flash @flash_args`;
back up the old flash first (`read-flash 0 ALL backup.bin`). Distribute
binaries only together with the corresponding source (GPL).

The upstream ESP-SDR documentation follows.

---

# ESP-SDR firmware

<img src="docs/espargos-logo.png" width="40%" align="right" alt="ESPARGOS logo">

ESP-SDR turns the ESP32's built-in 2.4 GHz Wi-Fi radio into a
**software-defined radio (SDR)**. It lets you capture the radio signal itself
and process it in software, so you can view the spectrum and study signals
beyond ordinary Wi-Fi packets. No separate SDR hardware is needed. The
ESP32-C5 also supports reception in the 5 GHz band.

With the help of LLMs, we discovered an undocumented debug path that bypasses
the chip's fixed-function Wi-Fi modem. This gives software access to raw
radio samples, called **I/Q samples**, from the built-in receiver. ESP-SDR
captures short bursts of these samples and sends them to your
computer over USB or UART for analysis. It can also compute spectra on-device,
including continuous RF capture on selected chips.

<br clear="all">

![ESP32 radio architecture: an undocumented debug path connects the ADC/DAC to the CPU, bypassing the fixed-function Wi-Fi modem.](docs/sdr-bypass.png)

The diagram shows the hardware's receive and transmit paths; this firmware
currently implements reception only.

[Project overview](https://espargos.net/espsdr/) ·
[Browser SDR viewer](https://espargos.net/espsdr/app/) ·
[Browser firmware installer](https://espargos.net/espsdr/app/flash.html)

**Parts of the firmware code are AI-generated.**
While we have a very good understanding of how the IQ sampling functionality works on the ESP32-C61 chip (used in our ESPARGOS One array), making IQ sampling work on the whole range of ESP32 family chips would have been too much work without LLM support.

## Chip support

| Chip | Status | Native USB | UART0 TX / RX | Minimum flash | Special profiles |
| --- | --- | --- | --- | --- | --- |
| ESP32 | ✅ | — | GPIO1 / GPIO3 | 2 MB | — |
| ESP32-C2 | ✅ (26 MHz crystal) | — | GPIO20 / GPIO19 | 2 MB | — |
| ESP32-C3 | ✅ | Serial/JTAG | GPIO21 / GPIO20 | 2 MB | — |
| ESP32-C5 | ✅ | Serial/JTAG | GPIO11 / GPIO12 | 2 MB | — |
| ESP32-C6 | ✅ | Serial/JTAG | GPIO16 / GPIO17 | 2 MB | — |
| ESP32-C61 | ✅ | Serial/JTAG | GPIO11 / GPIO10 | 2 MB | — |
| ESP32-H2 | ✅ | Serial/JTAG | GPIO24 / GPIO23 | 2 MB | — |
| ESP32-H21 | 🚧 | — | — | — | — |
| ESP32-H4 | 🚧 | — | — | — | — |
| ESP32-P4 | ❌ | — | — | — | — |
| ESP32-S2 | ✅ | USB-OTG CDC | GPIO43 / GPIO44 | 4 MB | — |
| ESP32-S3 | ✅ | Serial/JTAG | GPIO43 / GPIO44 | 2 MB | — |
| ESP32-S31 | ✅ | Serial/JTAG | GPIO58 / GPIO59 | 2 MB | [High Speed USB / Ethernet streaming at up to 40 MSa/s (experimental)](#s31-streaming) |

✅ Supported · 🚧 Not yet supported · ❌ Unsupported (no integrated radio).

USB, UART, and flash requirements above refer to the standard profiles; see each
special profile for its board requirements.

## Special Chip- / Board-Specific Profiles

<a id="s31-streaming"></a>

### **ESP32-S31**: Ethernet / high-speed USB streaming

**Experimental:** The S31 streaming mode is under development. Signal quality,
including the remaining DC peak, still needs improvement.

The separate **`esp32s31-stream`** profile targets the ESP32-S31 Function-CoreBoard
with Gigabit Ethernet and native high-speed USB. It streams receive-only I/Q to
**SoapyESPSDR** and includes an on-device web page for receiver controls and
status, with rates up to 20 MSa/s over USB and 40 MSa/s over Ethernet using
8-bit I plus 8-bit Q. The ordinary `esp32s31` profile remains the serial burst/FFT firmware
for ESP-WebSDR. See [streaming build, architecture, and protocol](docs/s31-streaming.md).

![Gqrx displaying an LTE signal at 2.63 GHz, continuously sampled at 40 MSa/s over Ethernet with an ESP32-S31 and SoapyESPSDR.](docs/gqrx-esp-sdr.png)

## On-chip spectrum streaming

The firmware can compute FFTs on the device and send compact spectra instead
of raw I/Q. The viewer queries each device's supported rates, FFT sizes and
transport before offering this mode.

| Chip | Continuous RF capture with on-chip FFT | Snapshot FFT |
| --- | --- | --- |
| ESP32 | — | 256–2048 bins; 16/40/80 MS/s; UART |
| ESP32-C2 | — | 256–2048 bins; 80/40/16 MS/s; UART |
| ESP32-C3 | 256 bins; 80 MS/s; native USB | 512–2048 bins over USB; 256–2048 over UART |
| ESP32-C5 | — (see [C5 extensions](#esp32-c5-extensions-espsoup)) | 256–2048 bins; 4/8/10/20/40/80 MS/s |
| ESP32-C6 | 256 bins; 80 MS/s; native USB | 512–2048 bins over USB; 256–2048 over UART |
| ESP32-H2 | — | 256–2048 bins; 6.4/10.667/16/32 MS/s; USB or UART |
| ESP32-C61 | 256 bins; 4/8/10/20/40/80 MS/s; native USB | 512/1024 bins over USB; 256–1024 over UART |
| ESP32-S2 | — | 256–2048 bins; 16/40/80 MS/s; USB or UART |
| ESP32-S3 | 256–2048 bins; 16/40/80 MS/s; native USB | — |
| ESP32-S31 | 256–2048 bins; 4/8/10/20/40/80 MS/s; native USB | Same FFT sizes and rates over UART |

Continuous capture keeps the RF writer running, but the CPU analyzes only
selected FFT windows. It does **not** deliver every sample or guarantee that
every short RF event will be visible. Snapshot FFT stops reception between
captures; the viewer labels these gaps explicitly. Raw I/Q capture remains
available separately.

The original S3 Turbo Mode was developed by Zoltan Doczi from
[Z2Labs](https://www.z2labs.io/). The shared implementation extends it with
C6/C61 bank rotation, C3 live-bank reads, S31 dual-core SIMD processing with
continuous bank rotation, and portable snapshot FFTs.
See [spectrum protocol and hardware validation](docs/spectrum.md) for the
wire format, limitations and test results. The S3 ring diagnostic host tool
is [tools/s3_ring.py](tools/s3_ring.py).

<a id="esp32-c5-extensions-espsoup"></a>

## ESP32-C5 extensions (ESPsoup)

The C5 receiver has additional, receive-only features that are announced by
their own `CAPS` tokens; every upstream command and reply is unchanged:

- **Faster captures and BIGCAP:** a register-level capture engine removes the
  fixed ~1.7 ms stall of the vendor `adctrig()` per snapshot (80 MS/s capture
  1886 → 206 µs; also speeds up the snapshot FFTs) and allows contiguous
  snapshots of up to 32,704 samples (8.2 ms at 4 MS/s). `CAPT16/CAPT20` add
  boot-time timestamps.
- **On-device DSP:** anti-aliased decimated snapshots (`DDC8/DDC16`, 9–13 dB
  lower alias floor than the sample-dropping 4/8 MS/s rates), averaged power
  spectra and a device-side panorama (`SWEEP 100 6000 40 …` in 0.69 s),
  1 kHz tuning steps (`FREQK`), on-chip temperature (`TEMP?`).
- **Continuous capture:** back-to-back dumps with exact gap accounting process
  91–96 % of all samples at 4–10 MS/s: decimated I/Q streams (`STREAM8/16`),
  continuous spectrograms (`CSPEC`), a burst catcher (`CPKT`) and a time grid
  for two boards that cover each other's gaps (`CGRID`).
- **Tuning to 5990 MHz:** above 3 GHz the C5 tunes through the Wi-Fi driver
  channel path by default (`TUNEM`), which receives UNII-3 and ITS-G5
  (5.85–5.925 GHz) where the stock path loses ~20 dB above ~5.75 GHz.
- **On-device demodulators:** an FM-video line stream for analog FPV (`VFM`,
  full PAL line rate of 15,625 lines/s) and a GFSK packet catcher with BLE
  de-whitening and CRC (`GFSK`).
- **Gap-free 40 MS/s path:** the modem diagnostic bus routed through the GPIO
  matrix into PARLIO RX gives 4+4-bit samples without gaps (`PARLIO`), plus a
  C5 port of the S31 hardware DC calibration (`DCCAL`).

See [ESP32-C5 extensions: protocol, measurements and limitations](docs/espsoup-c5.md).

## Commands and transport

Connect over native USB or a 3.3 V USB-to-UART adapter with crossed TX/RX
and common ground, using the pins above. Both interfaces carry the same
request/response protocol: send newline-terminated ASCII commands and read
text replies. Capture replies also include a binary I/Q payload.

Query `INFO` and `CAPS` to identify the firmware and supported features.
`LIMITS?` reports receive-control limits, `RANGE?` reports the tuning range,
and `TRANSPORT?` identifies the active interface. Configure reception with
`FREQ <MHz>`, `BANDWIDTH <MHz>` and `GAIN` commands.

Request a snapshot with `CAP16 <samples> <rate-index>` for signed 8-bit I/Q
or `CAP20 <samples> <rate-index>` for packed signed 10-bit I/Q. The reply is
`DATA <samples> <crc32-hex> <capture-microseconds>`, followed by exactly
`ceil(samples × bits-per-component × 2 / 8)` binary bytes. Verify the
payload CRC32 before using the samples. Rate indices 0–6 select 80, 40, 20, 10,
8, 4 or 16 MS/s respectively; use only rates advertised by `LIMITS?`.

Finish reading each reply before sending another command. Failures return
`ERR <reason>`. `SYNC <nonce>` echoes the nonce to let clients resynchronize
after an incomplete transfer. One client controls the radio at a time;
`RELEASE` or five seconds of idle time releases it, while other clients
receive `ERR busy`.

## Build and flash

[firmware-targets.json](firmware-targets.json) lists the supported profiles and
pins their ESP-IDF commits, including the preview SDK for S31. Check out the
matching SDK, initialize its submodules, run `install.sh <target>`, and source
`export.sh`.

Use a separate build directory and configuration for each chip:

```sh
idf.py -B build-s3 -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=sdkconfig.s3 \
  -DSDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3 build
idf.py -B build-s3 -p /dev/ttyACM0 flash
```

Substitute the target and paths for your chip. S31 also requires `idf.py --preview`.

The C2 profile uses a **26 MHz crystal**, as on the tested ESP8684H board.
For a 40 MHz board, select `CONFIG_XTAL_FREQ_40=y` in menuconfig and rebuild;
the packaged browser image requires 26 MHz. C2 uses UART0 at 2 Mbaud by
default. With a CH340 bridge, use the viewer's **Switch to 1 Mbaud** warning
when transfers lose bytes; baud changes are session-only. C2 provides raw
IQ8/IQ10 captures, hardware/manual gain, and snapshot FFTs. Continuous capture
is not advertised; approximate analog bandwidth covers 12–20 MHz.
See [C2 backend and validation](docs/esp32c2.md).

The `esp32h2` profile uses the Bluetooth PHY capture engine and supports
both native USB Serial/JTAG and UART0. It supports 32, 16, approximately 10.667 and 6.4 MS/s hardware sampling,
plus approximately 4–11 MHz analog bandwidth control.
See [H2 backend and validation](docs/esp32h2.md).

## Source layout

- `main/targets/<target>/`: chip receiver or adapter, tuning helpers, and the
  linker guard for its capture SRAM. CMake selects only the requested target.
- `main/targets/esp32s31/burst/`: serial IQ capture and on-chip FFT firmware.
- `main/targets/esp32s31/streaming/`: continuous USB/Ethernet IQ application for
  SoapyESPSDR. Both S31 profiles share `main/targets/esp32s31/tuning.h`.
- `main/families/c5_c6_c61/`: receiver shared by C5, C6, and C61; its `chip.h`
  comes from the selected target directory.
- `main/common/`: burst serial transport, gain control, limits, and bandwidth
  helpers. The gain-table wrapper is linked only for C61 and S31.
- `main/families/c5_c6_c61/c5_*.h`, `cspec_dsp.h`, `demod_*.h`, `dc_cal.h`,
  `parlio_cap.h`: the C5 extensions (included by the shared receiver for C5 only);
  their host tests live in `tests/espsoup/` and `tests/test_espsoup_c5.py`.
- `main/diagnostics/`: optional register probes, excluded from release exports.
- `platform/esp32s2/`: pinned ROM USB CDC compatibility component.

The application component and UART configuration stay in `main/`. Target SDK
defaults stay at the repository root for the build tools and ESP-IDF defaults
lookup. The firmware uses the burst protocol over UART/native USB; the separate
`main/targets/esp32s31/streaming/` application implements the receive-only Ethernet and
vendor USB streaming profile.

Run `python3 -m unittest discover -s tests` for host checks. Build every profile
with `tools/build_firmware.py` and its pinned SDK before distributing a change;
the CI matrix does this automatically. Preserve the target SRAM guards and
gain-table linker wrappers when moving or refactoring receiver code.

## Receive controls

Hardware AGC is the default. `GAIN MANUAL <index>` sets manual gain;
`GAIN HARDWARE` restores AGC. `LIMITS?` reports available gain indices,
bandwidths, sample rates and bit depths. `BANDWIDTH <MHz>` sets approximate
analog bandwidth; zero selects the widest setting.

All supported burst targets accept tuning attempts from **100–6000 MHz in 1 MHz steps**.
The viewer shows an informational warning outside 2400–2483.5 MHz, with
5150–5895 MHz also treated as the supported 5 GHz Wi-Fi band on C5. The warning never blocks tuning.
These are software attempt limits; the expanded range has not been hardware
validated.

- **ESP32:** 80/40/16 MS/s.
- **C2:** 80/40/16 MS/s; up to 8,190 complex samples; approximately 12–20 MHz analog bandwidth.
- **H2:** 32/16/10.667/6.4 MS/s; approximately 4–11 MHz analog bandwidth; up to 16,380 complex samples.
- **C3:** 80 MS/s; 14–62 MHz analog bandwidth.
- **C5:** 11–48 MHz bandwidth; selects its 5 GHz RF path above 3000 MHz;
  above 3000 MHz the Wi-Fi driver channel path is used by default (`TUNEM`, see
  [C5 extensions](docs/espsoup-c5.md#tuning-tunem)). Measured exact tuning:
  about 2130–2730 and 4790–5990 MHz; below about 2.1 GHz the LO does not follow.
- **C61:** 80/40/20/10/8/4 MS/s; 13–54 MHz bandwidth.
- **C6:** 80 MS/s; 12–54 MHz bandwidth.
- **S2:** 80/40/16 MS/s; 15–60 MHz bandwidth; up to 12,284 complex samples.
- **S3:** 13–69 MHz bandwidth.
- **S31:** 80/40/20/10/8/4 MS/s; 13–54 MHz bandwidth.

Captures have gaps; nominal sample rates exceed sustained serial throughput.
Gain and power are uncalibrated. Extended tuning does not guarantee PLL lock
or reception; the viewer uses the ISM-band warning described above.

See [receive-control details](docs/rx-controls.md).

## Contributors

<table>
  <tr>
    <td align="center">
      <a href="https://github.com/Jeija">
        <img src="https://github.com/Jeija.png?size=160" width="80" height="80" alt="Florian Euchner"><br>
        <b>Florian Euchner</b>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/zodoczi">
        <img src="https://github.com/zodoczi.png?size=160" width="80" height="80" alt="Zoltan Doczi"><br>
        <b>Zoltan Doczi</b>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/pit711">
        <img src="https://github.com/pit711.png?size=160" width="80" height="80" alt="Peter Holzhauser"><br>
        <b>Peter Holzhauser</b>
      </a><br>
      <sub>ESPsoup: ESP32-C5 extensions</sub>
    </td>
  </tr>
</table>

## License

ESP-SDR is licensed under the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your option)
any later version (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the full terms.
It is provided without any warranty, including implied warranties of
merchantability or fitness for a particular purpose.

**We chose the GPL because we want improvements to ESP-SDR to make their way
back to the community**. When you distribute modified versions, the GPL requires
you to make the corresponding source available to recipients under the GPL,
so they can study, share, and build on those improvements. We encourage you to
contribute changes upstream, but the GPL does not require upstream submissions
or publication of private modifications. See the
[GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html#UnreleasedMods).

The discovery of the capture mechanism itself is not protected by copyright:
copyright covers the code and other copyrightable expression, not the
underlying facts, ideas, or methods. **You are free to independently implement
the mechanism in your own projects under a license of your choice**.

Third-party components retain their own licenses and copyright notices,
including the Apache-2.0 ESP-IDF compatibility code in
`platform/esp32s2/esp_usb_cdc_rom_console/`, the pinned
[ESP-DSP component](components/esp-dsp/LICENSE), and the derived FFT kernels
in `main/targets/esp32s3/s3_fft_rnd.S` and `main/targets/esp32s31/burst/s31_fft_rnd.S`.
