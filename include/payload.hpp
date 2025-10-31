#pragma once
#include "update_metadata.pb.h"
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// HTTP support automatically enables ZIP
#ifdef HTTP_SUPPORT
#ifndef ENABLE_ZIP
#define ENABLE_ZIP
#endif
#endif

#ifdef ENABLE_ZIP
extern "C" {
#include "ziprand.h"
#ifdef HTTP_SUPPORT
#include "http.h"
#endif
}
#endif

namespace payload_dumper
{

// Forward declaration
class ProgressTracker;

constexpr const char* PAYLOAD_MAGIC = "CrAU";
constexpr uint64_t BRILLO_MAJOR_VERSION = 2;
constexpr uint64_t BLOCK_SIZE = 4096;

struct PayloadHeader {
    uint64_t version;
    uint64_t manifest_len;
    uint32_t metadata_signature_len;
    uint64_t size;
};

class Payload
{
  public:
    explicit Payload(const std::string& filename, 
                     const std::string& user_agent = "",
                     bool verify_hash = true);
    ~Payload();

    bool open();
    bool init();
    bool extractAll(const std::string& target_dir, int concurrency);
    bool extractSelected(const std::string& target_dir,
                         const std::vector<std::string>& partitions,
                         int concurrency);
    void listPartitions() const;

#ifdef HTTP_SUPPORT
    uint64_t getBytesDownloaded() const;
#endif

  private:
    std::string filename_;
    std::string user_agent_;
    bool verify_hash_;
    bool is_zip_;
    bool is_http_;

#ifdef ENABLE_ZIP
    ziprand_io_t* zip_io_;
    ziprand_archive_t* zip_archive_;
    ziprand_file_t* zip_file_;
#endif

    std::ifstream file_;
    PayloadHeader header_;
    chromeos_update_engine::DeltaArchiveManifest manifest_;
    chromeos_update_engine::Signatures signatures_;

    int64_t metadata_size_;
    int64_t data_offset_;
    bool initialized_;

    std::mutex file_mutex_;

    bool readHeader();
    bool readManifest();
    bool readMetadataSignature();
    bool extractPartition(const chromeos_update_engine::PartitionUpdate& partition,
                          const std::string& output_path,
                          ProgressTracker* progress_tracker);
    int64_t readBytes(void* buffer, int64_t offset, int64_t length);
    static bool isUrl(const std::string& path);
};

} // namespace payload_dumper