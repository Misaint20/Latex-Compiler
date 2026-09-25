# Contributing

Thanks for considering a contribution. This guide covers the minimum to get a
change in: build it, test it, commit it with a clean history.

## Getting the code

```bash
git clone <URL-de-tu-fork> "Latex Compiler"
cd "Latex Compiler"
```

Prerequisites: CMake ≥ 3.30, Ninja, pnpm, and a C++20 toolchain. First configure
downloads nlohmann_json, doctest and webview via FetchContent.

## Building (per OS)

**macOS** (Xcode/clang — produces `build/dev/Latex Compiler.app`):

```bash
cmake --preset dev
cmake --build --preset dev
open "build/dev/Latex Compiler.app"
```

**Windows** (VS 2022 Build Tools — run from the *Developer PowerShell for VS
2022*, or `cmake` will not find `cl`):

```powershell
cmake --preset dev
cmake --build --preset dev
```

**Linux** (gcc/clang + webkit dependencies):

```bash
sudo apt-get install -y libwebkit2gtk-4.1-dev libgtk-3-dev
cmake --preset dev
cmake --build --preset dev
```

The frontend builds automatically (`frontend-assets` target). Outside macOS it
compiles as a single-file bundle (`INLINE_BUNDLE=1`) because `file://` pages
cannot load ES-module chunks. Full setup walkthrough for a clean Windows
machine: `docs/windows-packaging-guide.md`.

## Tests

```bash
cd build/dev && ./core-tests        # direct run, human-readable output
cd build/dev && ctest               # registered test, CI-equivalent
```

The suite is doctest-based (110 cases / 590 assertions at the time of writing)
covering the core services, the engine chain and the request dispatcher. Run it
before opening a PR, and after any CMake change.

Conventions:

- New tests go in `tests/test_<area>.cpp` and must be added to the
  `core-tests` sources in `CMakeLists.txt`.
- The Linux/POSIX adapters compile inside `core-tests` on **every** platform, so
  Linux-only code is syntax-checked from any host — keep them in that list.
- Test business rules through the core services, not the adapters: no platform
  code in `src/core/`.

## Where changes belong

The architecture is hexagonal; one behavior lands in one obvious place:

- Business rules and policies → `src/core/` (no platform code).
- OS differences → the per-OS adapter files (`adapters/*_{mac,win,linux}.*`)
  behind a port in `src/core/contracts/`.
- A new adapter wiring → `src/app/composition_root.cpp` only.
- UI behavior → `frontend/src/` (talks to the core over the JSON protocol in
  `src/ipc/`).
- Packaging rules → `CMakeLists.txt` and `scripts/`.

## Commit message style

- Subject in the imperative, ≤ 72 chars, no trailing period:
  `Add macOS DMG packaging script`.
- Body explains *why*, not a file-by-file *what*, wrapped at ~72 chars.
- One logical change per commit; mixed refactors are harder to review.
- No machine-generated trailers or signatures.

Reference the existing history (`git log --oneline`) for tone.

## Pull requests

- Tests green from a fresh configure (`ctest` in a new build directory).
- If behavior changed, update `README.md` / `docs/` in the same PR.
- Version bumps (`CMakeLists.txt` + `frontend/package.json`) belong to release
  commits, not feature PRs.
