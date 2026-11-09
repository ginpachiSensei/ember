# Ember

A learning-focused, modern game engine in C++20 with Vulkan 1.3.
The long-term goal is to understand and implement the ideas behind UE5-class tech
(GPU-driven rendering, virtualized geometry, dynamic GI, virtual shadow maps).

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the engine is structured
- [docs/ROADMAP.md](docs/ROADMAP.md): phases and what's done

## Prerequisites (Windows)

1. **Visual Studio 2022** with the "Desktop development with C++" workload (includes CMake).
2. **vcpkg** (C++ package manager). One-time setup in PowerShell:
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git $HOME\vcpkg
   & $HOME\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   [Environment]::SetEnvironmentVariable("VCPKG_ROOT", "$HOME\vcpkg", "User")
   ```
   Then restart your terminal / VS Code so `VCPKG_ROOT` is picked up.
3. No Vulkan SDK needed: vcpkg provides the validation layer and the Slang shader compiler.
   (Install the SDK anyway if you want tools like `vkconfig`.)

## Build and run

**VS Code (recommended):** install the recommended extensions (CMake Tools, C/C++).
`.vscode/settings.json` points CMake Tools at the CMake that ships with Visual Studio.
Pick the `windows` configure preset and the `debug` build preset, then use the
Build / Debug / Run buttons in the status bar. The first configure builds the dependencies
with vcpkg, which takes a few minutes.

**Command line (any PowerShell):** the Visual Studio generator finds the compiler itself,
so only `cmake` has to be on PATH (e.g. `winget install Kitware.CMake`):

```powershell
cmake --preset windows
cmake --build --preset debug
.\build\apps\sandbox\Debug\ember_sandbox.exe
ctest --preset debug            # unit tests
```

Optional sample assets (the Damaged Helmet glTF and a CC0 sky HDR), not stored in git:

```powershell
.	oolsetch-sample-content.ps1
```

**Sandbox controls:** hold the right mouse button and use WASD/QE to fly (Shift = faster,
scroll = speed). F12 saves a screenshot to `screenshots/`. Esc quits.
Shaders in `shaders/` recompile when you save them while the sandbox runs.

Useful flags: `--hdr content/hdri/sky.hdr` (photo sky instead of the procedural one),
`--capture out.png` (render a few frames, save, exit), `--view 2` (debug views),
`--camera x,y,z,yaw,pitch`, `--sun azimuth,elevation`.
