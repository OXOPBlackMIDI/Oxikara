<h1 align="center">Oxikara</h1>
<p align="center">
  <img src="assets/icon.svg" alt="Oxikara Logo">
</p>
<p align="center">
  A high-performance C++ MIDI visualizer with real-time playback.
</p>

## Preview
<img width="1400" height="820" alt="image" src="https://github.com/user-attachments/assets/6b5b7fcb-9071-4ed1-bec5-22cd7e2808f5" />


## Features
- Threaded MIDI parsing (runs on a worker thread)
- Real-time audio output (KDMAPI / WinMM fallback)
- Non-overlapping note loading
- Adjustable MIDI event processing per frame

## Bootstrap (Windows)
Run:
```bat
build.cmd
```

## Run
```powershell
.\build\Release\oxikara.exe --midi path\to\song.mid
```

Optional:
- `--max-midi-events-per-frame N` (default `4096`) to limit per-frame MIDI dispatch spikes.

## Parsing info (console)
On load, Oxikara now prints:
- track count
- channel/meta/sysex event counts
- total notes loaded
- ignored overlapping note-ons
- tempo change count

Parsing itself runs on a worker thread.

## Audio backend order
1. KDMAPI (OmniMIDI): `InitializeKDMAPIStream` + `SendDirectData`
2. WinMM fallback (`midiOutShortMsg`)

## Renderer
- GLFW + OpenGL
- Vulkan swapchain renderer (no OpenGL backend).

