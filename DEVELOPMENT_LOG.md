# Mini Contra Dos — Development Log

## 2026-11-16 — Initial implementation

### Repository and isolation
- Project repository: `darkstrikerclash92-dotcom/mini-contra1`.
- The earlier repository `darkstrikerclash92-dotcom/mini-contra` was inspected only for its build workflow and metadata reference; no files were changed there.
- New implementation created independently in this repository.

### SDK and build environment inspection
- Build image: `ghcr.io/altps3/ps3dev:latest`.
- Detected compiler: `ppu-gcc (GCC) 7.2.0`.
- Verified SDK paths and PSL1GHT rules under `/usr/local/ps3dev`.
- Inspected upstream PSL1GHT declarations and examples for `rsxInit`, `rsxMemalign`, `rsxAddressToOffset`, `gcmSetDisplayBuffer`, `gcmSetFlip`, `gcmSetWaitFlip`, video mode selection, and pad input.
- The container initially lacked the host-side `pkg` and `sfo` utilities. The workflow now builds those two utilities from the official `ps3dev/PSL1GHT/tools/sfo_pkg` sources and installs them into the container's PS3DEV bin directory.
- Confirmed `make_self`, `make_self_npdrm`, `sprxlinker`, `fself`, and `package_finalize` are available in the build image.

### Architecture
- 2D software rasterizer writes directly to 32-bit RSX framebuffer memory.
- Two framebuffers allocated with 64-byte alignment and converted to RSX offsets using the documented PSL1GHT API.
- Video mode selection attempts 720p first, then 480p and 576p.
- Presentation uses VSync flip mode and PSL1GHT flip/flush/wait calls.
- No shaders, textures, external graphics libraries, or RSX 3D draw pipeline are required for the first version.
- Controller state is retained between pad-change events because the pad API returns zero-filled data when there is no new input.
- Game systems: lateral movement, jump, firing, enemies, bullet/enemy collision, player/enemy collision, scrolling background, score, lives, and restart after game over.
- Audio is not implemented in this first stable build; it is intentionally deferred to keep the initial graphics and input path small.

### Errors found and corrections
1. **Missing host package tools:** the first CI attempt stopped because `pkg` was absent from the image. Added a build step for the official PSL1GHT `sfo_pkg` utilities.
2. **Wrong linker driver:** the first link attempt invoked the host `ld`, which rejected the PPU `-mhard-float` option. The workflow now explicitly invokes `make LD=ppu-gcc` and `make pkg LD=ppu-gcc`.
3. **Controller polling semantics:** input is now cached when a new pad report arrives instead of clearing the state every frame.
4. **Framebuffer presentation:** rendering now targets the framebuffer selected for the current flip, then swaps to the other buffer.

### Verified build
- GitHub Actions run: `43924295682` (build 9).
- Result: successful.
- PPU ELF, SELF, NPDRM EBOOT.BIN, PARAM.SFO, and PKG were generated.
- The generated PKG directory table was read back with the PSL1GHT `pkg --list` utility and verified to contain `ICON0.PNG`, `PARAM.SFO`, `USRDIR`, and `USRDIR/EBOOT.BIN`.
- Release assets were uploaded to GitHub Releases.

### Remaining hardware validation
- GitHub Actions validates compilation and package structure, not execution on a physical console.
- The PKG still requires a real-console installation and runtime test on the target PS3 Slim CECH-2511A with HEN 4.93.
- No claim is made that the black-screen issue is conclusively resolved until that hardware test succeeds.
