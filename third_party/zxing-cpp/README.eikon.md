# ZXing-C++ (vendored subset)

Eikon looks for a QR code before running OCR: if the area holds one, its payload is copied instead
of recognised text. This folder holds the part of [ZXing-C++](https://github.com/zxing-cpp/zxing-cpp)
needed for that, built by `ZXing.vcxproj` as a static library that `src/Eikon/Eikon.vcxproj`
references.

- Version: **v3.1.1**, commit `287c85df6f961c8efbfb5ffd736cd9457b8b890e`.
- Licence: Apache-2.0 (`LICENSE`, unchanged). `libzueci` is BSD-3-Clause (header of `zueci.c`).
- Contents: `core/src` files that upstream's CMake compiles for `ZXING_READERS=ON`,
  `ZXING_WRITERS=OFF`, `ZXING_ENABLE_QRCODE=ON` and every other `ZXING_ENABLE_*` off, plus
  `MultiFormatWriter.h` (included unconditionally by `CreateBarcode.cpp`). `src/Version.h` is
  `core/Version.h.in` configured by hand for that build. No source file is modified.

## Why vendored source and not vcpkg

- **QR only at compile time.** Eikon detects QR codes and nothing else; it should not start
  reading EAN or DataMatrix. Building only the QR reader enforces that in the binary, not just in
  `ReaderOptions`, and keeps the library small (about 1.9 MB of source, mostly libzueci's
  character-set tables for Shift_JIS/GB/Big5 QR payloads).
- **Hermetic builds.** Eikon builds from the git tree with MSBuild alone, locally and on a clean CI
  runner. vcpkg manifest mode would need vcpkg bootstrapped on every build machine,
  network access during every clean build, and a custom triplet to match Eikon's flags
  (`/guard:cf`, the dynamic CRT, ARM64). A pinned copy in git builds the same everywhere.
- **Cost:** updates are manual (below), so a ZXing security fix has to be pulled in by hand.

## Updating

1. `git clone --depth 1 --branch <tag> https://github.com/zxing-cpp/zxing-cpp.git`
2. Recreate `src/` from `core/src` with the file lists in `core/CMakeLists.txt` (`COMMON_FILES`
   with the `ZXING_READERS` additions, `QRCODE_FILES` for readers, `libzueci`, `librscpp`), plus
   `MultiFormatWriter.h`.
3. Update `src/Version.h` from `core/Version.h.in` (version numbers, any new `#cmakedefine`).
4. Update the version and commit above and build; the core tests `QrCode_*` check that QR codes are still read and nothing else is.
