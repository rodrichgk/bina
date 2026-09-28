<div align="center">

<img src="src/resources/brand/bina-128.png" width="96" alt="Bina logo">

# Bina

**A music studio for writing, recording and arranging, from the [Butu](https://github.com/rodrichgk/Butu) family.**

*Bina* is Lingala for "dance!" (from *kobina*, to dance).

Windows desktop · Qt 6 · C++17 · Free & open-source

</div>

---

## What is Bina?

Bina is a desktop music production app (a DAW). Put audio clips and note clips on tracks, shape each track's sound with an instrument and effects, and play it all back on a timeline that thinks in bars and beats.

It is the sibling of **Butu**, a media player for Plex and Jellyfin. Butu is where you watch (night, cyan); Bina is where you make (dance, amber).

## Features

- **Tracks that hold anything.** Every track takes audio clips and note clips side by side. A track's name, colour, mute, solo, volume, pan, instrument and effects all live on the track, and every clip on it follows them.
- **A musical timeline.** Bars and beats on the ruler, Ctrl+wheel zoom that redraws crisply, and a grid that gets finer as you zoom in. Clips snap to it (hold Alt to place freely).
- **Piano roll.** Double-click an empty spot on any lane to create a note clip, and edit it in its own window: add, move, resize and delete notes, box-select, transpose with the arrow keys, audition keys, and choose your snap.
- **Five instruments.** Synth, Sampler (any audio file, pitched across the keyboard), Drum Kit (synthesized 808, 909 and acoustic-style kits in the General MIDI layout), FM Synth (electric pianos, bells, basses) and Plucked Strings (a physically modelled string).
- **Effects per track.** Low-pass, high-pass, delay, distortion and tremolo, processed on that track's own bus so they never touch other tracks.
- **Import.** Pick many files at once or drop them straight onto the timeline. WAV, MP3, FLAC, OGG, Opus, M4A, AAC and AIFF, decoded with FFmpeg on background threads.
- **Made to feel good.** A draggable tempo field, buttons that react when you press them, and dialogs that ease in. Motion follows Windows' "Animation effects" setting.

## Status

Bina is early. Things that work end to end: arranging audio and note clips, the instruments and effects, playback, the piano roll and the mixer settings per track.

Not there yet:
- saving and loading projects
- recording (the record button arms, but nothing is captured)
- per-note velocity editing, MIDI keyboards and plug-ins (VST)

It is developed and tested on Windows only.

## Build & run

**Requirements**
- Qt 6 with the Multimedia and Concurrent modules (developed with Qt 6.10.1, MinGW 64-bit)
- CMake and Ninja (both ship with Qt's installer)
- FFmpeg development files: headers plus `avformat`, `avcodec`, `avutil` and `swresample`

**Build**
```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64 -DFFMPEG_ROOT=C:/ffmpeg
cmake --build build
```
`FFMPEG_ROOT` is the folder that contains FFmpeg's `include/` and `lib/`. It can also come from an environment variable of the same name; without it, CMake looks in `C:/ffmpeg` and then asks pkg-config.

**Package a runnable copy** (Qt and FFmpeg DLLs next to the exe, in `dist\Bina\`):
```powershell
powershell -ExecutionPolicy Bypass -File deploy.ps1 -Launch
```
The script assumes Qt at `C:\Qt\6.10.1\mingw_64` and FFmpeg's DLLs in `C:\ffmpeg\bin` (or `FFMPEG_ROOT\bin`).

## How it's built

```
src/
├── core/          # ProjectModel: the single source of truth (tracks, clips, tempo)
├── audio/         # Engine (per-track buses), FFmpeg decoder, effects
│   └── instruments/   # One self-registering .cpp per instrument (see its README)
├── timeline/      # The arrangement view: ruler, grid, lanes, clips
├── pianoroll/     # Note editor window
├── widgets/       # Motion, tempo field, colour picker, brand lockup
├── resources/     # Bundled fonts and brand assets
└── theme.*        # Colours, type, icons and the stylesheet
tools/
├── instrument_render.cpp   # Offline test for instruments (renders WAVs, checks output)
└── make_icon.py            # Regenerates the app icon from the logo geometry
```

- **The model owns the project.** The timeline, piano roll and audio engine all read the `ProjectModel` and react to its signals; none of them keep their own copy of the project.
- **The audio thread never waits on the UI.** The engine renders from an immutable snapshot of the model, swapped in whenever the project changes. Each track mixes its clips and instrument into its own bus, runs its effects, then applies volume and pan.
- **Instruments are plug-ins.** Add one `.cpp` file to `src/audio/instruments/` and it is built, listed and given UI controls automatically. Test it without the app: `cmake --build build --target instrument_render`, then run `build/instrument_render.exe all`.

## Identity

| | |
|---|---|
| **Night** `#0C0E13` | The base, shared with Butu |
| **Moto** `#FFB454` | "Fire". The single accent |
| **Mwinda** `#EEF0F4` | "Light". Text |

The mark is Butu's lowercase b (a stem and a circle) with a waveform cut out of the bowl where Butu has its play button. The wordmark is set in Plus Jakarta Sans ExtraBold. The interface uses Inter and JetBrains Mono.

## Credits

Built by **Gabhy Rodrich**. Fonts: Inter, JetBrains Mono and Plus Jakarta Sans, all under the SIL Open Font License (licences in `src/resources/fonts/`). Audio decoding by FFmpeg.
