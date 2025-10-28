project('payload-dumper-ungo', ['c', 'cpp'],
  version: '1.0.0',
  default_options: ['warning_level=2', 'cpp_std=c++17', 'c_std=c17']
)

add_project_arguments('-DNDEBUG', language: ['c', 'cpp'])

# --- Compression Libraries ---
lzma_dep = dependency('liblzma', required: false)
if not lzma_dep.found()
  lzma_dep = dependency('liblzma', fallback: ['liblzma', 'lzma_dep'], default_options: ['default_library=static'])
endif

zstd_dep = dependency('libzstd', required: false)
if not zstd_dep.found()
  zstd_dep = dependency('libzstd', fallback: ['zstd', 'libzstd_dep'], default_options: ['default_library=static'])
endif

bz2_dep = dependency('bzip2', required: false)
if not bz2_dep.found()
  bz2_dep = dependency('bzip2', fallback: ['bzip2', 'bzip2_dep'], default_options: ['default_library=static'])
endif

protobuf_dep = dependency('libprotobuf', required: false)
if not protobuf_dep.found()
  protobuf_dep = dependency('protobuf', fallback: ['protobuf', 'protobuf_dep'], default_options: ['default_library=static'])
endif

# --- Optional HTTP ---
enable_http = get_option('enable_http')
curl_dep = disabler()
if enable_http
  curl_dep = dependency('libcurl', required: false)
  if curl_dep.found()
    add_project_arguments('-DHTTP_SUPPORT', language: ['c', 'cpp'])
  endif
endif

# --- ZIP + ziprand integration ---
enable_zip = get_option('enable_zip')
if curl_dep.found() and enable_http
  enable_zip = true
endif

ziprand_sources = []
ziprand_dep = disabler()

if enable_zip
  add_project_arguments('-DENABLE_ZIP', language: ['c', 'cpp'])
  ziprand_dep = dependency('libziprand', required: false)
  
  if not ziprand_dep.found()
    # Only fallback subproject forced static
    ziprand_dep = dependency('libziprand',
      fallback: ['libziprand', 'ziprand_dep'],
      required: true,
      default_options: ['default_library=static']
    )
  endif
endif

# --- Protocol Buffers ---
protoc = find_program('protoc', required: false)
proto_src = []
if protoc.found()
  proto_gen = generator(protoc,
    output: ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
    arguments: [
      '--proto_path=@CURRENT_SOURCE_DIR@/proto',
      '--cpp_out=@BUILD_DIR@',
      '@INPUT@'
    ]
  )
  proto_src = proto_gen.process('proto/update_metadata.proto')
else
  proto_src = files('proto/update_metadata.pb.cc')
endif

# --- Sources ---
sources = [
  'src/main.cc',
  'src/payload.cc',
  proto_src
]

if curl_dep.found()
  sources += files('src/http.c')
endif

# --- Includes ---
inc_dirs = include_directories('include', 'proto')

# --- Dependencies ---
deps = [zstd_dep, lzma_dep, bz2_dep, protobuf_dep]
if curl_dep.found()
  deps += curl_dep
endif
if enable_zip and ziprand_dep.found()
  deps += ziprand_dep
endif

# --- Executable ---
executable('payload-dumper-ungo',
  sources,
  include_directories: inc_dirs,
  dependencies: deps,
  install: true
)