# Latex Compiler

Desktop app to compile LaTeX projects with automatic Mermaid diagram building, a multi-engine
compiler (pdfLaTeX/XeLaTeX/LuaLaTeX + Tectonic) and OS-level notifications. C++20 with an embedded
webview UI (React + Vite).

| macOS | Windows | Linux |
|---|---|---|
| ✅ primary dev machine | ✅ supported (MSI/ZIP) | ✅ supported (portable TGZ) |

## Architecture (hexagonal)

```
src/
  core/            business rules. No platform code: CompileService, DiagramBuildService,
    contracts/     ports (ICompiler, IProcessRunner, INotifier, IFileStore, ...).
  adapters/        replaceable implementations behind the ports:
    classtex/      pdfLaTeX/XeLaTeX/LuaLaTeX chain (fontspec detection via project-local includes)
    tectonic/      Tectonic fallback engine
    notify/        OS notifications: UNUserNotificationCenter (mac) / WinRT toasts (win) / notify-send (linux)
    filesystem|dialog|process|history/  per-OS openers, pickers, process runner, JSON storage
  ipc/             RequestDispatcher: the JSON protocol between UI and core
  app/             composition_root.cpp: the only place that knows concrete adapters
  platform/        webview window, native menus, app:// scheme handler (macOS)
frontend/          React + Vite UI, served from app:// (macOS) or single-file file:// (win/linux)
tests/             110 doctest cases (590 assertions), run on every build
```

A change to one behavior lands in one obvious place: engine selection in the adapter, packaging
rules in CMake, per-OS differences only in `adapters/*_{mac,win,linux}.*` and CMake conditionals.

## Build (all platforms)

Prerequisites: CMake ≥ 3.30, Ninja, pnpm, a C++20 toolchain.

```bash
cmake --preset dev
cmake --build --preset dev
cd build/dev && ./core-tests        # or ctest
```

- **macOS**: Xcode/clang, output `build/dev/Latex Compiler.app`
- **Windows**: VS 2022 Build Tools (run from the Developer shell)
- **Linux**: gcc/clang + `libwebkit2gtk-4.1-dev` (webview dependency)

The frontend builds automatically (`frontend-assets` target). Outside macOS it is compiled as a
single-file bundle (`INLINE_BUNDLE=1`) because `file://` pages cannot load ES-module chunks.

## Release process

### 1. Bump the version (two files)

```
CMakeLists.txt      project(LatexCompiler VERSION x.y.z ...)
frontend/package.json   "version": "x.y.z"
```

Everything else (Info.plist, GetInfoString, CPack names) propagates from these.

### 2. macOS — signed, notarized-less DMG

```bash
scripts/package_dmg.sh              # build release + verify signature + DMG
scripts/package_dmg.sh --skip-build # repackage only
```

Produces `dist/Latex Compiler-<version>-<arch>.dmg` (drag-to-Applications layout, checksum
verified). The build itself ad-hoc signs the whole bundle (Info.plist + resources sealed; see the
`sign-bundle` CMake target) — `codesign --verify --strict` passing is a hard gate of the packaging
script, which also checks the signed identifier matches `com.Misaint20.latexcompiler`.

**Distributing beyond your own machines**: ad-hoc signatures trigger Gatekeeper's first-open
confirmation on other Macs (right-click → Open). Removing that step requires a paid Apple Developer
account: sign with Developer ID (`codesign --options runtime --sign "Developer ID Application: …"`)
and notarize (`xcrun notarytool submit`), then staple the ticket (`xcrun stapler staple`).

Note: CI does not notarize; DMG signing there is the build's ad-hoc signature (see §5 for what
CI *does* sign when secrets are configured).

### 3. Windows — MSI + portable ZIP (run ON Windows)

```powershell
pwsh scripts/package_windows.ps1              # build + MSI + ZIP
pwsh scripts/package_windows.ps1 -SkipBuild
```

Requires on that machine: VS 2022 Build Tools (Developer PowerShell), CMake+Ninja, pnpm, and WiX
Toolset v3.14 for the MSI (ZIP builds without it). Outputs to `dist/`. Notes: WebView2 Runtime is
preinstalled on updated Win10/11; unsigned builds trigger SmartScreen once (More info → Run anyway);
toast notifications attribute via AUMID `Misaint20.latexcompiler` (verify the Start Menu shortcut
carries it if toasts do not appear). For signed distribution add
`-CertificateThumbprint <id>` (or `-TestSelfSigned` to validate the pipeline without a real
certificate) — the script signs the exe before packaging and the MSI afterwards. Full walkthrough:
`docs/windows-packaging-guide.md` §6; on CI the same flow is driven by the
`WINDOWS_PFX_BASE64`/`WINDOWS_PFX_PASSWORD` secrets (§5).

### 4. Linux — AppImage (recommended) or portable TGZ

**AppImage** (single file, bundles webkit libs, desktop-integration ready):

```bash
scripts/package_appimage.sh              # build + AppDir + linuxdeploy
scripts/package_appimage.sh --skip-build # repackage only
```

Produces `dist/Latex Compiler-<version>-<arch>.AppImage`. Requires `curl` and network access the
first time (downloads linuxdeploy + the AppRun runtime into `build/appimage-tools`). The bundled
AppImage runs on distros with glibc >= the build machine's — build on an older distro (or a
manylinux container) for wide coverage.

**GPG signature**: set `GPG_KEY=<fingerprint|email>` to also produce `<AppImage>.sig` and the
exported public key (`dist/latexcompiler-release-key.asc`) — the script verifies the pair before
shipping it. Users verify with:

```bash
curl -O https://<your-host>/latexcompiler-release-key.asc   # or fetch from a keyserver
gpg --import latexcompiler-release-key.asc
gpg --verify 'Latex Compiler-0.1.0-x86_64.AppImage.sig' 'Latex Compiler-0.1.0-x86_64.AppImage'
```

Publish the key fingerprint (e.g. on the GitHub Release page) so downloads can be checked against
it, and for CI use a passphrase-less dedicated signing key stored as a secret.

**Portable TGZ** (no external tools):

```bash
cmake --preset release && cmake --build --preset release
cd build/release && cpack -C Release
```

Both ship `share/applications/latexcompiler.desktop` and the hicolor icon. In the TGZ the menu entry
only resolves once the extracted `bin` folder is on PATH (or edit `Exec=` to the absolute path);
the AppImage wires it automatically. `notify-send` (libnotify) provides toast notifications.

### 5. CI (automated)

Tagging `v*` (e.g. `v0.1.0`) and pushing the tag runs `.github/workflows/release.yml`: MSI+ZIP on
`windows-latest` (MSVC env + WiX), DMG on `macos-latest`, and AppImage on `ubuntu-latest`
(webkit/fuse deps installed by the workflow). Tests run on all three; artifacts attach to the
GitHub Release. `workflow_dispatch` allows manual dry-runs from the Actions tab.

Both signing paths are **opt-in via repository secrets** — with no secrets configured, CI ships
unsigned artifacts and every job still succeeds.

#### CI secrets for signed releases

Add them in *Settings → Secrets and variables → Actions → New repository secret*. Secrets are
masked in logs and only exposed to workflows on this repository.

**Linux — `GPG_PRIVATE_KEY`** (AppImage signature):

1. Create a dedicated passphrase-less signing key (do not reuse your personal one):

   ```bash
   gpg --full-generate-key                # RSA, name/emails of the project, empty passphrase
   gpg --list-secret-keys --keyid-format long   # note the KEYID
   gpg --armor --export-secret-keys <KEYID> > gpg-private-key.asc
   ```

2. Paste the file's whole content (including the `-----BEGIN PGP PRIVATE KEY BLOCK-----` armor)
   as the value of a secret named `GPG_PRIVATE_KEY`.
3. That is all: the workflow's `Import GPG release key` step detects the secret, imports it, and
   exports the key id into the job environment — which switches on signing in
   `scripts/package_appimage.sh` (`.sig` + exported public key land next to the AppImage, and the
   pair is verified before shipping). Publish the public key's fingerprint on the Release page (see
   §4) so downloads can be checked against it.

If you prefer to keep a passphrase, the workflow import uses `gpg --batch --import`, which
requires either a passphrase-less key or `%no-ask-passphrase` loopback settings — the
passphrase-less dedicated key is the pragmatic CI choice.

**Windows — `WINDOWS_PFX_BASE64` + `WINDOWS_PFX_PASSWORD`** (Authenticode, exe + MSI):

1. Get a code-signing certificate (OV is the reasonable entry point; see
   `docs/windows-packaging-guide.md` §6.1 for the OV/EV trade-offs — an EV token's non-exportable
   key cannot be uploaded as a secret and needs a different signing flow).
2. Export the issued certificate as `.pfx`, then Base64-encode it:

   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes('.\misaint20-codesign.pfx')) |
       Set-Content codesign-base64.txt
   ```

3. Create two secrets: `WINDOWS_PFX_BASE64` with that single-line text, and `WINDOWS_PFX_PASSWORD`
   with the PFX's password.
4. The workflow's `Import code-signing certificate` step decodes and imports it into the user
   store, passes the discovered thumbprint to `scripts/package_windows.ps1`, and the script signs
   the exe before `cpack` plus the MSI afterwards (same flow as §3, timestamped RFC3161).
5. Verify on the artifact before publishing: `Get-AuthenticodeSignature` → `Status: Valid` with a
   `TimeStamper`.

Security hygiene for both: keep secrets at repository scope only if the repo is private, otherwise
restrict them to an [environment](https://docs.github.com/en/actions/deployment-security-hardening)
and require the tag job to target it; never `echo` secret values (GitHub masks them, but the
Base64/armor blobs around them are not masked); a CI secret is a copy of the private key — treat
its leak as full compromise and revoke/reissue. Delete the local `gpg-private-key.asc` /
`codesign-base64.txt` files after uploading (they must never be committed; `.gitignore` does not
cover arbitrary names).

### 6. Verify before publishing

### 6. Verify before publishing

- macOS: `codesign --verify --strict "build/release/Latex Compiler.app"`
- Windows: `Get-AuthenticodeSignature 'dist\Latex Compiler-<v>-windows-x64.msi' | Format-List`
  → `Status: Valid` (only when signing secrets were configured; unsigned CI builds skip this)
- Linux: `gpg --verify '<AppImage>.sig' '<AppImage>'` against the published key (§4)
- All: open the artifact on a clean location/machine and compile a real project once.
- `ctest` green from a fresh configure.

## Testing

```bash
cd build/dev && ./core-tests          # 110 cases / 590 assertions
```

The Linux adapters compile inside `core-tests` on every platform, so Linux-only code is
syntax-checked from any host.
