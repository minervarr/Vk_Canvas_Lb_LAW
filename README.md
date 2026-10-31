# vulkan_canvas_engine

Cross-platform Vulkan canvas engine (Android, Windows and Linux/Wayland). Uses [vulkan_font_engine](https://github.com/minervarr/vulkan_font_engine) as a submodule for GPU text rendering.

Direction, quality goals and the current phase: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Setup

```bash
git submodule update --init --recursive
```

## Build

```bash
# Android
cd platform/android && ./gradlew assembleDebug

# Windows (MSVC + Ninja + Vulkan SDK)
cd platform/windows && ./Build.bat
```
## License

Copyright (C) 2026 nava.

This library is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See the [`LICENSE`](LICENSE) file for the full text.

It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU Affero General Public License for more details.
