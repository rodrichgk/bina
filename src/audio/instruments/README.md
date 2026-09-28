# Instruments

Each instrument is **one self-contained `.cpp` file** in this folder. The build picks
every `.cpp` here up automatically (CMake glob), and the file registers itself with
`REGISTER_INSTRUMENT(...)`, so adding an instrument touches no other file.

- `instrumentapi.h`: the contract (`Instrument`, `InstrumentDefinition`, `InstrumentParam`). Read it first.
- `dsp.h`: shared helpers (`Dsp::Adsr`, `Dsp::StereoLowpass`, `Dsp::Noise`, `Dsp::polyBlep`, `Dsp::noteFrequency`).
- `synth.cpp`, `sampler.cpp`: reference implementations.

## Writing one

1. Create `src/audio/instruments/<name>.cpp`. Keep everything in an anonymous namespace.
2. Subclass `Instrument`: implement `render()` (ADD into the bus) and `tailFrames()`.
3. Build an `InstrumentDefinition` (unique lowercase `id`, `name`, one-sentence
   `description`, `params`, `create`) and register it: `REGISTER_INSTRUMENT(makeDefinition())`.
4. Parameters become UI controls automatically: `InstrumentParam::continuous(...)` (slider),
   `::choice(...)` (dropdown, value = index), `::toggle(...)` (checkbox). Read values in your
   constructor via `ctx.param("id")`, `ctx.choice("id")`, `ctx.toggle("id")`.

Rules that keep playback glitch-free (the harness checks most of them):
- Output is interleaved stereo float at 16-bit scale. Aim for about -12 dBFS per voice
  (peak around 8000) so a 5-note chord at full velocity stays under full scale.
- Blocks can jump (seeking). Either compute voices from "time since note on", or key
  per-voice state by `(startFrame, pitch)` and reset it when `blockStart` isn't where the
  previous block ended.
- Everything a note produces must be silent once `endFrame + tailFrames()` has passed.
- No allocation, locks or I/O per sample. Output must stay finite for any parameter value.

## Testing without the app

`tools/instrument_render.cpp` renders a fixed phrase (arpeggio across the range, a chord,
a 5 ms note, a very quiet note, overlapping repeats) in 512-frame blocks, then seeks back
and renders again. It fails on NaN/inf, silence, clipping, a bad tail or bad parameter
ranges, and writes `<id>.wav` so you can listen.

Build it with **only** the API and the instruments you want to test (from the repo root,
Git Bash, MinGW from the Qt install):

```sh
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.10.1/mingw_64/bin:$PATH"
g++ -std=c++17 -O2 -IC:/Qt/6.10.1/mingw_64/include -IC:/Qt/6.10.1/mingw_64/include/QtCore \
    tools/instrument_render.cpp src/audio/instruments/instrumentapi.cpp \
    src/audio/instruments/<name>.cpp \
    C:/Qt/6.10.1/mingw_64/lib/libQt6Core.a -o <outdir>/render_<name>.exe
<outdir>/render_<name>.exe <id> <outdir>/wav            # defaults
<outdir>/render_<name>.exe <id> <outdir>/wav attack=2   # override parameters
```

Exit code 0 means every check passed.
