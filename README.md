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
---

# Performance Comparison
- Payload-dumper-ungo is extremely Fast

# Payload Dumper Performance Comparison

## benchmark-payload-dumper-go-raw

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload-dumper-go --concurrency 4 -o ../output_raw payload.bin` | 77.468 ± 0.120 | 77.361 | 77.598 | 1.00 |

## benchmark-payload-dumper-go-zip

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload-dumper-go --concurrency 4 -o ../output_zip ota.zip` | 81.037 ± 0.805 | 80.483 | 81.961 | 1.00 |

## benchmark-payload-dumper-rust-raw

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload_dumper --threads 4 --no-verify -o ../output_raw payload.bin` | 82.705 ± 0.135 | 82.608 | 82.860 | 1.00 |

## benchmark-payload-dumper-rust-zip

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload_dumper --threads 4 --no-verify -o ../output_zip ota.zip` | 83.472 ± 1.426 | 81.986 | 84.829 | 1.00 |

## benchmark-payload-dumper-ungo-raw

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload-dumper-ungo --concurrency 4 -o ../output_raw payload.bin` | 67.389 ± 0.170 | 67.273 | 67.585 | 1.00 |

## benchmark-payload-dumper-ungo-zip

| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| `payload-dumper-ungo --concurrency 4 -o ../output_zip ota.zip` | 67.066 ± 0.702 | 66.546 | 67.865 | 1.00 |

---

# How to Build ?

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
