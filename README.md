# payload-dumper-ungo

An Android OTA payload dumper **not** written in Go.

## Features

-  Fast extraction of Android OTA payload.bin files
-  Direct URL dumping - Extract payloads directly from remote URLs
-  Smart ZIP handling - Random access extraction from ZIP files without extracting payload.bin first
-  Compatible interface with the original payload-dumper-go

## Difference from original

- Written in C and C++
- **Direct URL support**: Extract OTA payloads directly from URLs using libcurl
- **Random access ZIP extraction**: Process ZIP files efficiently without intermediate extraction
- **Default number of concurrent extraction** = Number of cpu cores


## Build Dependencies

**Required:**
- `lzma` - LZMA decompression support
- `bzip2` - Bzip2 decompression support
- `zstd` - Zstandard decompression support
- `protobuf` - Protocol buffers

**Optional:**
- `libziprand` - Required for ZIP support
- `libcurl` - Required for HTTP/network support

## Building

```bash
# Setup build directory
meson setup build

# Compile
ninja -C build
```

### Build Options

```bash
# Enable ZIP support (requires libziprand)
meson setup build -Denable_zip=true

# Disable HTTP support
meson setup build -Denable_http=false

# Both options
meson setup build -Denable_zip=true -Denable_http=false
```

## Usage

```bash
# Extract from payload.bin file
payload-dumper-ungo payload.bin

# Extract from ZIP file (requires -Denable_zip=true)
payload-dumper-ungo ota-package.zip

# Extract directly from URL (requires -Denable_http=true)
payload-dumper-ungo https://example.com/ota-package.zip

# Extract specific partitions
payload-dumper-ungo -p system,vendor,boot payload.bin

# Specify output directory
payload-dumper-ungo -o output_dir payload.bin
```

## Credits

Original [payload-dumper-go](https://github.com/ssut/payload-dumper-go) by ssut

## License

GPL-3.0
