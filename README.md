# WhatSie

Feature rich WhatsApp web client based on Qt WebEngine for Linux Desktop

---

> **PERSONAL FORK** — built on top of the original Whatsie to fix daily-use pain points and add features not available anywhere else.

---

## Fork Improvements

This fork adds several features and fixes on top of the original Whatsie project.

### Local Audio Transcription (no API key required)

The killer feature: transcribe WhatsApp voice messages and audio messages **entirely on your machine**, offline, with no external API calls.

- Powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — runs locally using your GPU (CUDA) for fast inference
- A **📝 button** appears next to every voice message and PTT audio player
- Click it after pressing play — the audio is captured, decoded, and transcribed in seconds
- Transcription appears inline below the audio player
- Works with any language (auto-detection or manual language setting)
- **No API key. No internet. No cloud.** Fully private.

**How it works (technical):**
1. JS injection intercepts `decodeAudioData` in WhatsApp Web to capture the raw audio buffer
2. Buffer is sent to C++ via `QWebChannel` (no file needed)
3. `libavformat` + `libswresample` decode any audio format → mono f32 PCM at 16 kHz
4. `whisper_full()` runs on GPU via CUDA with Flash Attention enabled
5. Transcript is sent back to JS and displayed inline — the whole round-trip is non-blocking

**Setup:**
1. Download a GGML Whisper model (e.g. `ggml-large-v3-turbo.bin`) from [huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp)
2. Place it at `~/.local/share/whatsie/models/` (auto-detected) or set the path in **Settings → Transcription**
3. Optionally set a language code (`pt`, `en`, `es`, …) or keep `auto` for automatic detection

**Build requirements for this feature:**
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) built with `GGML_CUDA=ON` and installed to `/usr/local`
- FFmpeg dev libraries: `libavformat`, `libavcodec`, `libswresample`, `libavutil`

---

### Input Focus Fix (Focus Keeper v4)

Fixes the extremely annoying WhatsApp Web bug where the message input field constantly loses focus, making it impossible to type properly.

- **The problem:** WhatsApp Web randomly steals focus away from the input field — after clicking in the chat area, closing popups, switching windows, or interacting with any UI element.
- **The fix:** Aggressive JavaScript injection (`injectInputFocusKeeper` in `src/webenginepage.cpp`) uses a smart polling loop to detect focus loss and restore it immediately, without interfering with intentional clicks on other elements.
- Debug from DevTools console: `window._whatsieFocusControl.status()`

---

### Qt6 Migration
- Full migration from Qt5 to Qt6 WebEngine API
- Replaced deprecated Qt5 APIs with modern Qt6 equivalents:
  - `QDesktopWidget` → `QScreen` APIs
  - `toTime_t()` → `toSecsSinceEpoch()`
  - `QTextCodec` → `QString::fromUtf8()`
  - `DataLocation` → `AppLocalDataLocation`
  - Fixed geolocation timer using `QTimer` instead of deprecated signal

### System Theme Support
- Added new "System" theme option that automatically follows OS theme
- Uses `gsettings` for GNOME/GTK theme detection with palette fallback
- Default theme changed from "Light" to "System" for better desktop integration
- Theme toggle now cycles through: System → Dark → Light

### Cleanup & Modernization
- Removed RateApp and MoreApps widgets (cleaner experience)
- Updated Chrome user agent from v125 to v134
- Fixed `StartupWMClass` in desktop file for proper window grouping
- Improved notification handling and crash prevention
- Removed `--single-process` chromium flag (caused issues)

### Build Requirements Update
- Now requires Qt6 (Qt 6.2+ recommended) instead of Qt5

## Whatsie Key features

- Light and Dark Themes with automatic switching
- Customized Notifications & Native Notifications
- Keyboard Shortcuts
- BuiltIn download manager
- Mute Audio, Disable Notifications
- App Lock feature
- Hardware access permission manager
- Built in Spell Checker (with support for 31 Major languages)
- Other settings that let you control every aspect of WebApp like:
	+ Do not disturb mode
	+ Full view mode, lets you expand the main view to the full width of the window
	+ Ability to switch between Native & Custom notification
	+ Configurable notification popup timeout
	+ Mute all audio from Whatapp
	+ Disabling auto playback of media
	+ Minimize to tray on application start
	+ Toggle to enable single click hide to the system tray
	+ Switching download location
	+ Enable disable app lock on application start
	+ Auto-locking after a certain interval of time
	+ App lock password management
	+ Widget styling
	+ Configurable auto Theme switching based on day night time
	+ Configurable close button action
	+ Global App shortcuts
	+ Permission manager let you toggle camera mic and other hardware level permissions
	+ Configurable page zoom factor, switching based on window state maximized on normal 
	+ Configurable App User Agent
	+ Application Storage management, lets you clean residual cache and persistent data

## Command line options:
Comes with general CLI support, with a bunch of options that let you interact with already running instances of Whatsie.

Run: `whatsie -h` to see all supported options.

```
Usage: whatsie [options]
Feature rich WhatsApp web client based on Qt WebEngine

Options:
  -h, --help           Displays help on commandline options
  -v, --version        Displays version information.
  -b, --build-info     Shows detailed current build infomation
  -w, --show-window    Show main window of running instance of WhatSie
  -s, --open-settings  Opens Settings dialog in a running instance of WhatSie
  -l, --lock-app       Locks a running instance of WhatSie
  -i, --open-about     Opens About dialog in a running instance of WhatSie
  -t, --toggle-theme   Toggle between dark & light theme in a running instance
                       of WhatSie
  -r, --reload-app     Reload the app in a running instance of WhatSie
  -n, --new-chat       Open new chat prompt in a running instance of WhatSie
```

## Build instructions (Linux)
The source code can be built using the regular Qt application development procedure. Whatsie Project makes use of Qt's QMake build system, which simplifies the build process. To build Whatsie locally on your system, follow the steps below.

### Build requirements
 - git (to clone repo)
 - libx11-dev libx11-xcb-dev (required for x11 XKB module support at build time)
 - Qt 6.2+ (6.5+ recommended) with the following modules installed with development headers
	+ webengine (qt6-webengine-dev)
	+ webenginewidgets
	+ positioning
	+ webchannel
	+ concurrent
 - **For local audio transcription** (optional but recommended):
	+ [whisper.cpp](https://github.com/ggerganov/whisper.cpp) built with `GGML_CUDA=ON` and installed (`make install`) — provides `libwhisper.so` and `whisper.h`
	+ FFmpeg dev libraries: `libavformat-dev libavcodec-dev libswresample-dev libavutil-dev`
	+ A GGML model file (e.g. `ggml-large-v3-turbo.bin`) placed in `~/.local/share/whatsie/models/`
	
### Build steps

> **IMPORTANT**: Always use `./build.sh` to compile. Never run `make` or `qmake` directly, as the build number won't update correctly.

 1. **Clone** source code

 	`git clone https://github.com/keshavbhatt/whatsie.git`

 2. Enter into source directory

	`cd whatsie`

 3. **Build** using the build script (auto-increments build number)

	```bash
	./build.sh
	```

 4. **Run** the built executable

	```bash
	./build.sh run
	```

 5. **(Optional)** Other build commands:

	```bash
	./build.sh rebuild  # Clean and rebuild from scratch
	./build.sh version  # Show current version info
	./build.sh clean    # Clean build artifacts
	```

 6. **(Optional)** Install system-wide

	```bash
	cd build && sudo make install
	```

### Troubleshooting

- **Build number not updating in window title?** Run `./build.sh rebuild` to force full recompilation.
- **Permission denied on build.sh?** Run `chmod +x build.sh` first.

## Packaging and Releases

### One-command (Linux + Windows + GitHub Release)

```bash
./setup.sh --tag v4.16.3
```

By default it builds Linux packages, cross-compiles Windows via MXE, and uploads `.deb`, `.AppImage`, and `.exe` to the GitHub Release.

### Linux (.deb + AppImage)

```bash
./build.sh
./package_linux.sh --all
```

Artifacts are generated in `build/package/out/`.

### Windows (.exe installer via MXE on Linux)

Requires MXE + NSIS installed locally.

```bash
./build_windows_mxe.sh --static   # or --shared
./package_windows_mxe.sh --static
```

### GitHub Releases (automated)

Push a tag like `v4.16.3` and GitHub Actions will build and publish:

```bash
git tag v4.16.3
git push origin v4.16.3
```

Artifacts are attached to the GitHub Release as `.deb`, `.AppImage`, and `.exe`.



## Install Whatsie on Linux Desktop

### On any snapd supported Linux distributions

 `snap install whatsie`

### On any Arch based Linux distribution
Using Arch User Repository (AUR), [AUR package for Whatsie](https://aur.archlinux.org/packages/whatsie-git) is maintained by [M0Rf30](https://github.com/M0Rf30)

 `yay -S whatsie-git`

## Screenshots (could be old)

![WhatSie for Linux Desktop Light Theme](https://github.com/keshavbhatt/whatsie/blob/main/screenshots/1.jpg?raw=true)
![WhatSie for Linux Desktop Dark Theme](https://github.com/keshavbhatt/whatsie/blob/main/screenshots/2.jpg?raw=true)
![WhatSie for Linux Desktop Setting module](https://github.com/keshavbhatt/whatsie/blob/main/screenshots/4.jpg?raw=true)
![WhatSie for Linux Desktop App Lock screen](https://github.com/keshavbhatt/whatsie/blob/main/screenshots/3.jpg?raw=true)
![WhatSie for Linux Desktop Shortcuts & Permissions](https://github.com/keshavbhatt/whatsie/blob/main/screenshots/5.jpg?raw=true)
