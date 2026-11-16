# Mini Contra Dos (PS3)

Homebrew 2D run-and-gun prototype for PlayStation 3 using PSL1GHT.

## Build

The GitHub Actions workflow builds the PPU ELF, SELF and installable PKG inside the PS3DEV Docker toolchain. No local computer is required.

## Controls

- D-pad left/right: move
- Cross: jump
- Square: fire
- Start: restart after game over
- PS button: system menu / exit handling

## Graphics strategy

The first version uses a deliberately small, CPU-rendered 32-bit framebuffer allocated with PSL1GHT RSX memory APIs. It avoids shader, texture, and complex RSX draw-state dependencies while validating video mode selection, 64-byte framebuffer alignment, RSX offsets, double buffering, and VSync presentation.

## Build validation

The workflow checks the toolchain commands, builds the ELF and SELF, packages the title, and verifies the PKG plus the staged EBOOT.BIN and PARAM.SFO before uploading artifacts.

## Hardware test still required

GitHub Actions cannot prove runtime behavior on the target console. The PKG must still be installed and tested on the user's PS3 Slim CECH-2511A with HEN 4.93.
