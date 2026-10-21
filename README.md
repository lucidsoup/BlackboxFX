# BlackboxFX

An "everything" effects processor in the spirit of granular/looper pedals and
four-module colour boxes, written to eventually run as custom firmware on the
**M-VAVE ANN BlackBox**. Today it runs on your computer; see
[docs/HARDWARE.md](docs/HARDWARE.md) for why it isn't on the device yet.

```
in ─► LOOPER(pre) ─► CHARACTER ─► MOVEMENT ─► CLOUD ─► TAPE ─► SPACE ─► LOOPER(post) ─► MIX ─► out
       optional       drive        tremolo     granular  wow &    FDN       optional
                      fold         chorus      pitch     flutter  reverb
                      crush        vibrato     reverse   delay    shimmer
                      tone         autofilter  freeze
```

| Module | What it does |
| --- | --- |
| **CHARACTER** | Saturating drive, triangle wavefolder, or bit + sample-rate crusher, then a tone low-pass |
| **MOVEMENT** | Tremolo, stereo chorus, vibrato or resonant auto-filter |
| **CLOUD** | Granular engine: up to 16 overlapping grains from the last few seconds, pitched (unison, ±octave, fifth, random harmonies, detune), optional reverse, and freeze |
| **TAPE** | Delay with wow, flutter and drift, saturating feedback that can self-oscillate safely, darkening repeats, and pitch-bending time changes |
| **SPACE** | 8-line feedback delay network reverb with modulation, damping and a shimmer that stays stable at any setting |
| **LOOPER** | Record, overdub, play, stop, reverse and half-speed, before or after the effects |

## Try it

```bash
cmake -S . -B build && cmake --build build
./build/bfx --list                                  # presets, parameters, memory
./build/bfx --demo --render-all demo/               # every preset on a built-in guitar phrase
./build/bfx -p "Shimmer Halo" my_guitar.wav out.wav
./build/bfx -p 2 --set cloud_reverse=0.8 --set tape_wow=0.6 --tail 8 in.wav out.wav
./build/test_engine
```

Presets: Clean Bloom, Haze Cloud, Octave Mosaic, Sub Tunnel, Shimmer Halo,
Warped Tape, Crushed Pulse, Glitch Reverse, Frozen Choir, Wobble Chorus,
Filter Drift.

Output is 24-bit stereo WAV at the input's sample rate. The BlackBox itself
runs at 44.1 kHz.

## Using the engine

```c
bfx_config cfg = bfx_device_config();          /* or bfx_default_config() */
static uint8_t arena[1200 * 1024];
static uint64_t engine_mem[...];               /* bfx_engine_size() bytes */
bfx_engine *fx = bfx_init(engine_mem, arena, sizeof arena, &cfg);
bfx_load_preset(fx, 4);
bfx_set_param(fx, BFX_SPACE_SHIMMER, 0.8f);    /* every parameter is 0..1 */
bfx_loop(fx, BFX_LOOP_RECORD);
bfx_process(fx, in_l, in_r, out_l, out_r, frames);
```

No malloc, no I/O. One arena holds every buffer.

## With the BlackBox plugged in

Nothing in this repo writes to the device. To help plan the port, run the
read-only probe and share its output:

```bash
pip install pyusb
python3 tools/probe/blackbox_probe.py --json blackbox.json
```

## Layout

| Path | |
| --- | --- |
| `include/bfx.h` | Public API |
| `src/` | Engine (portable C99) |
| `host/` | Desktop command-line renderer and WAV I/O |
| `tests/` | Engine tests: transparency, silence, stability, tail decay, looper, pitch, delay timing, determinism |
| `tools/probe/` | Read-only USB probe for the BlackBox |
| `docs/HARDWARE.md` | What's known about the device and the path to flashing |

## Credits

Device facts come from the community reverse-engineering project
[jakino0/mvave-blackbox-reverse-engineering](https://github.com/jakino0/mvave-blackbox-reverse-engineering).
This project isn't affiliated with M-VAVE or JieLi.
