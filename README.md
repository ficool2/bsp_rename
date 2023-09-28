# BSP rename
Tool to rename BSPs, while correctly modifying packed content. Supports compressed (repacked) BSPs.

# Usage
1) Download [latest release](https://github.com/ficool2/bsp_name/releases/). 
2) Run the exe or drag and drop a .bsp file.

# Build
This project requires the following library.
- [minizip-ng](https://github.com/zlib-ng/minizip-ng).
- LZMA SDK. This is built as part of `minizip-ng`.

After fetching the libraries, copy `minizip-ng` includes to `include/minizip`.

Copy `liblzma` and `libminizip`.lib/.pdbs to `lib/debug` and `lib/release`.

Open `.sln` in Visual Studio 2022 and build.