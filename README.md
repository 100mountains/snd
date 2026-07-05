# app-template

A minimal placeholder proving that [miniaudio](https://github.com/mackron/miniaudio) (audio I/O) and [Dear ImGui](https://github.com/ocornut/imgui) (UI) build and run together, cross-platform, via CMake + `FetchContent`.

There's no editor logic here, no CI, and no wrapper/framework layer yet -- it's deliberately just a "does the stack work" proof while that design is still being decided. Running it opens a window and starts a silent audio device.

See `AGENTS.md` for the multi-agent dev workflow (genericized from Murk's) that applies to work in this repo.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/app-template        # path varies by generator/platform
```
