![logo-sm](https://raw.githubusercontent.com/djshaji/opiqo-windows/refs/heads/main/logo-sm.jpg)

# Opiqo LV2 plugin host for Windows
This project is a Windows port of the Opiqo LV2 plugin host, which allows users to load and manage LV2 audio plugins on the Windows platform. The application provides a user-friendly interface for controlling plugin parameters, managing presets, and recording audio output.

## Features
- Load and manage multiple LV2 plugins simultaneously.
- Control plugin parameters in real-time.
- Save and load plugin presets.
- Record audio output to various file formats.
- Support for a wide range of LV2 plugins.

---

## Building

The project is cross-compiled for Windows from a Linux host using **MinGW-w64** and **CMake 3.16+**.

### Prerequisites

Install the MinGW-w64 cross-compiler and the required audio libraries:

```bash
# Debian / Ubuntu
sudo apt install mingw-w64 cmake make

# MinGW sysroot audio dependencies (x86_64-w64-mingw32 sysroot)
sudo apt install libopus-dev:amd64   # provided via MinGW sysroot
```

The following libraries must be present in `/usr/x86_64-w64-mingw32/sys-root/mingw/lib/`:

| Library | Package (Fedora/openSUSE) |
|---------|--------------------------|
| opus    | `mingw64-opus` |
| vorbis / vorbisenc / vorbisfile | `mingw64-libvorbis` |
| ogg     | `mingw64-libogg` |
| FLAC    | `mingw64-flac` |
| fftw3   | `mingw64-fftw` |

The following libraries are bundled in the `libs/` directory and do not require separate installation:
`lilv`, `serd`, `sord`, `sratom`, `zix`, `mp3lame`, `opusenc`, `sndfile`.

### Compile

```bash
# Configure (output goes to build-windows/)
cmake --preset windows-default

# Build
cmake --build build-windows
```

Or equivalently without presets:

```bash
mkdir -p build-windows && cd build-windows
cmake .. \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The compiled binary `opiqo.exe` is placed in `build-windows/`.

---

## Usage

Run `opiqo.exe` on Windows. On first launch the application will scan for LV2 plugins in the standard Windows LV2 path (`%APPDATA%\LV2` and `%COMMONPROGRAMFILES%\LV2`).

### Loading plugins

1. Click the **+** button in any of the four plugin slots to open the plugin browser.
2. Select a plugin from the list and click **Add**.

### Audio routing

- Select your **input** and **output** device from the dropdown menus.
- Choose **Shared** or **Exclusive** WASAPI mode in **Settings**.
- Default sample rate is **48 000 Hz**, default block size is **4096 samples** — both are configurable in Settings.
- Press the **Power** toggle to start/stop the audio engine.

### Recording

1. Select the output **format** and **quality** from the dropdowns in the control bar.
2. Press **Record** to start capturing the processed audio.
3. Press **Record** again to stop and save the file.

### Presets

Presets can be exported and imported via **Settings → Export / Import Presets**.

