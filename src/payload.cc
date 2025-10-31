#define NOMINMAX
#include "payload.hpp"
#include "progress.hpp"
#include "sha256.h"

#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_bswap64 _byteswap_uint64
#endif
#include <algorithm>
#include <atomic>
#include <bzlib.h>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <lzma.h>
#include <map>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>
#include <zstd.h>

namespace payload_dumper
{

static std::string formatBytes(uint64_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit_idx];
    return ss.str();
}

bool Payload::isUrl(const std::string& path)
{
    return (path.size() >= 7 && path.substr(0, 7) == "http://") ||
           (path.size() >= 8 && path.substr(0, 8) == "https://");
}

Payload::Payload(const std::string& filename, const std::string& user_agent, bool verify_hash)
    : filename_(filename), user_agent_(user_agent), verify_hash_(verify_hash), is_zip_(false), 
      is_http_(false)
#ifdef ENABLE_ZIP
      ,
      zip_io_(nullptr), zip_archive_(nullptr), zip_file_(nullptr)
#endif
      ,
      metadata_size_(0), data_offset_(0), initialized_(false)
{

    is_http_ = isUrl(filename);

#ifdef ENABLE_ZIP
    if (is_http_) {
        is_zip_ = true;
    } else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zip") {
        is_zip_ = true;
    }
#endif
}

Payload::~Payload()
{
#ifdef ENABLE_ZIP
    if (zip_file_) {
        ziprand_fclose(zip_file_);
    }
    if (zip_archive_) {
        ziprand_close(zip_archive_);
    }
#endif
    if (file_.is_open()) {
        file_.close();
    }
}

bool Payload::open()
{
#ifdef ENABLE_ZIP
    if (is_zip_) {
#ifdef HTTP_SUPPORT
        if (is_http_) {
            ziprand_http_config_t config = ziprand_http_config_default();
            if (!user_agent_.empty()) {
                config.user_agent = user_agent_.c_str();
            }
            config.verbose = 0;

            zip_io_ = ziprand_io_http_ex(filename_.c_str(), &config);
            if (!zip_io_) {
                std::cerr << "Failed to connect to URL\n";
                return false;
            }
        } else
#endif
        {
            zip_io_ = ziprand_io_file(filename_.c_str());
            if (!zip_io_) {
                std::cerr << "Failed to open ZIP file: " << filename_ << "\n";
                return false;
            }
        }

        zip_archive_ = ziprand_open(zip_io_);
        if (!zip_archive_) {
            std::cerr << "Failed to parse ZIP archive\n";
            ziprand_io_free(zip_io_);
            zip_io_ = nullptr;
            return false;
        }

        const ziprand_entry_t* entry = ziprand_find_entry(zip_archive_, "payload.bin");
        if (!entry) {
            std::cerr << "payload.bin not found in ZIP archive\n";
            ziprand_close(zip_archive_);
            ziprand_io_free(zip_io_);
            zip_archive_ = nullptr;
            zip_io_ = nullptr;
            return false;
        }

        if (entry->compression_method != 0) {
            std::cerr << "Error: payload.bin is compressed (method " << entry->compression_method
                      << ")\n";
            std::cerr << "Only uncompressed (stored) payload.bin is supported\n";
            ziprand_close(zip_archive_);
            ziprand_io_free(zip_io_);
            zip_archive_ = nullptr;
            zip_io_ = nullptr;
            return false;
        }

        zip_file_ = ziprand_fopen(zip_archive_, entry);
        if (!zip_file_) {
            std::cerr << "Failed to open payload.bin\n";
            ziprand_close(zip_archive_);
            ziprand_io_free(zip_io_);
            zip_archive_ = nullptr;
            zip_io_ = nullptr;
            return false;
        }

        return true;
    }
#endif

    file_.open(filename_, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "Failed to open file: " << filename_ << "\n";
        return false;
    }
    return true;
}

int64_t Payload::readBytes(void* buffer, int64_t offset, int64_t length)
{
#ifdef ENABLE_ZIP
    if (is_zip_) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        int64_t bytes_read = ziprand_fread_at(
            zip_file_, static_cast<uint64_t>(offset), buffer, static_cast<size_t>(length));
        if (bytes_read < 0) {
            std::cerr << "Read failed at offset " << offset << "\n";
            return -1;
        }
        return bytes_read;
    }
#endif

    std::lock_guard<std::mutex> lock(file_mutex_);
    file_.seekg(offset);
    if (!file_.good()) {
        std::cerr << "Seek failed to offset " << offset << "\n";
        return -1;
    }
    file_.read(reinterpret_cast<char*>(buffer), length);
    return file_.gcount();
}

bool Payload::readHeader()
{
    char magic[4];
    if (readBytes(magic, 0, 4) != 4 || std::string(magic, 4) != PAYLOAD_MAGIC) {
        std::cerr << "Invalid payload magic\n";
        return false;
    }

    uint64_t version;
    if (readBytes(&version, 4, 8) != 8)
        return false;
    header_.version = __builtin_bswap64(version);

    if (header_.version != BRILLO_MAJOR_VERSION) {
        std::cerr << "Unsupported payload version: " << header_.version << "\n";
        return false;
    }

    uint64_t manifest_len;
    if (readBytes(&manifest_len, 12, 8) != 8)
        return false;
    header_.manifest_len = __builtin_bswap64(manifest_len);

    uint32_t sig_len;
    if (readBytes(&sig_len, 20, 4) != 4)
        return false;
    header_.metadata_signature_len = __builtin_bswap32(sig_len);

    header_.size = 24;
    return true;
}

bool Payload::readManifest()
{
    std::vector<uint8_t> manifest_data(header_.manifest_len);
    if (readBytes(manifest_data.data(), header_.size, header_.manifest_len) !=
        static_cast<int64_t>(header_.manifest_len)) {
        return false;
    }

    return manifest_.ParseFromArray(manifest_data.data(), manifest_data.size());
}

bool Payload::readMetadataSignature()
{
    if (header_.metadata_signature_len > 0) {
        std::vector<uint8_t> sig_data(header_.metadata_signature_len);
        int64_t offset = header_.size + header_.manifest_len;
        if (readBytes(sig_data.data(), offset, header_.metadata_signature_len) !=
            header_.metadata_signature_len) {
            return false;
        }

        if (!signatures_.ParseFromArray(sig_data.data(), sig_data.size())) {
            return false;
        }
    }

    return true;
}

bool Payload::init()
{
    if (!readHeader())
        return false;
    if (!readManifest())
        return false;
    if (!readMetadataSignature())
        return false;

    metadata_size_ = header_.size + header_.manifest_len;
    data_offset_ = metadata_size_ + header_.metadata_signature_len;

    std::cout << "Payload version: " << header_.version << "\n";
    std::cout << "Number of partitions: " << manifest_.partitions_size() << "\n";
    std::cout << "Hash verification: " << (verify_hash_ ? "enabled" : "disabled") << "\n";

    initialized_ = true;
    return true;
}

void Payload::listPartitions() const
{
    std::cout << "\nPartitions in payload:\n";
    for (const auto& partition : manifest_.partitions()) {
        std::cout << "  " << std::left << std::setw(20) << partition.partition_name() << " "
                  << std::right << std::setw(10)
                  << formatBytes(partition.new_partition_info().size()) << "  ("
                  << partition.operations_size() << " ops)\n";
    }
}

#ifdef HTTP_SUPPORT
uint64_t Payload::getBytesDownloaded() const
{
    if (is_http_ && zip_io_) {
        return ziprand_http_get_bytes_downloaded(zip_io_);
    }
    return 0;
}
#endif

// SHA-256 streaming hasher class for TeeReader pattern
class SHA256Hasher {
public:
    SHA256Hasher() {
        sha256_init(&ctx_);
    }

    void update(const void* data, size_t len) {
        sha256_update(&ctx_, data, len);
    }

    void finalize(uint8_t hash[SHA256_DIGEST_SIZE]) {
        sha256_final(&ctx_, hash);
    }

    std::string finalizeHex() {
        uint8_t hash[SHA256_DIGEST_SIZE];
        sha256_final(&ctx_, hash);
        
        char hex[65];
        sha256_to_hex(hash, hex);
        return std::string(hex);
    }

private:
    SHA256_CTX ctx_;
};

// TeeReader: reads data and simultaneously feeds it to SHA-256 hasher
class TeeReader {
public:
    TeeReader(const uint8_t* data, size_t size, SHA256Hasher* hasher)
        : data_(data), size_(size), pos_(0), hasher_(hasher) {}

    size_t read(void* buffer, size_t count) {
        size_t to_read = std::min(count, size_ - pos_);
        if (to_read > 0) {
            memcpy(buffer, data_ + pos_, to_read);
            if (hasher_) {
                hasher_->update(data_ + pos_, to_read);
            }
            pos_ += to_read;
        }
        return to_read;
    }

    const uint8_t* currentPtr() const { return data_ + pos_; }
    size_t remaining() const { return size_ - pos_; }
    void skip(size_t count) { pos_ += std::min(count, size_ - pos_); }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    SHA256Hasher* hasher_;
};

bool Payload::extractPartition(const chromeos_update_engine::PartitionUpdate& partition,
                               const std::string& output_path,
                               ProgressTracker* progress_tracker)
{
    std::string name = partition.partition_name();

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        std::cerr << "\nFailed to create output file: " << output_path << "\n";
        return false;
    }

    int total_ops = partition.operations_size();
    int completed_ops = 0;

    if (progress_tracker) {
        progress_tracker->update(name, 0, total_ops);
    }

    for (const auto& operation : partition.operations()) {
        if (operation.dst_extents_size() == 0) {
            std::cerr << "\nInvalid operation for " << name << "\n";
            return false;
        }

        const auto& extent = operation.dst_extents(0);
        int64_t data_offset = data_offset_ + operation.data_offset();
        int64_t data_length = operation.data_length();
        output.seekp(extent.start_block() * BLOCK_SIZE);
        int64_t expected_size = extent.num_blocks() * BLOCK_SIZE;

        std::vector<uint8_t> compressed_data(data_length);
        if (readBytes(compressed_data.data(), data_offset, data_length) != data_length) {
            std::cerr << "\nFailed to read data for " << name << "\n";
            return false;
        }

        // Initialize SHA-256 hasher for verification
        SHA256Hasher hasher;
        TeeReader tee_reader(compressed_data.data(), compressed_data.size(), 
                             verify_hash_ ? &hasher : nullptr);

        std::vector<uint8_t> decompressed_data;

        switch (operation.type()) {
        case chromeos_update_engine::InstallOperation_Type_REPLACE: {
            decompressed_data = compressed_data;
            // Update hash with all data
            if (verify_hash_) {
                hasher.update(compressed_data.data(), compressed_data.size());
            }
            break;
        }

        case chromeos_update_engine::InstallOperation_Type_REPLACE_XZ: {
            decompressed_data.resize(expected_size);
            lzma_stream strm = LZMA_STREAM_INIT;
            lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);
            if (ret != LZMA_OK) {
                std::cerr << "\nXZ decoder init failed for " << name << "\n";
                return false;
            }

            strm.next_in = compressed_data.data();
            strm.avail_in = compressed_data.size();
            strm.next_out = decompressed_data.data();
            strm.avail_out = decompressed_data.size();

            // Hash the compressed data (input)
            if (verify_hash_) {
                hasher.update(compressed_data.data(), compressed_data.size());
            }

            ret = lzma_code(&strm, LZMA_FINISH);
            lzma_end(&strm);
            if (ret != LZMA_STREAM_END) {
                std::cerr << "\nXZ decompression failed for " << name << "\n";
                return false;
            }
            break;
        }

        case chromeos_update_engine::InstallOperation_Type_REPLACE_BZ: {
            decompressed_data.resize(expected_size);
            unsigned int dest_len = expected_size;
            
            // Hash the compressed data (input)
            if (verify_hash_) {
                hasher.update(compressed_data.data(), compressed_data.size());
            }

            int ret = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(decompressed_data.data()),
                                                 &dest_len,
                                                 reinterpret_cast<char*>(compressed_data.data()),
                                                 compressed_data.size(),
                                                 0,
                                                 0);
            if (ret != BZ_OK) {
                std::cerr << "\nBZ2 decompression failed for " << name << "\n";
                return false;
            }
            break;
        }

        case chromeos_update_engine::InstallOperation_Type_ZSTD: {
            size_t dest_size =
                ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
            decompressed_data.resize(dest_size);
            
            // Hash the compressed data (input)
            if (verify_hash_) {
                hasher.update(compressed_data.data(), compressed_data.size());
            }

            size_t ret = ZSTD_decompress(decompressed_data.data(),
                                         decompressed_data.size(),
                                         compressed_data.data(),
                                         compressed_data.size());
            if (ZSTD_isError(ret)) {
                std::cerr << "\nZSTD decompression failed for " << name << "\n";
                return false;
            }
            break;
        }

        case chromeos_update_engine::InstallOperation_Type_ZERO: {
            decompressed_data.resize(expected_size, 0);
            // No data to hash for ZERO operations
            break;
        }

        default:
            std::cerr << "\nUnhandled operation type for " << name << "\n";
            return false;
        }

        if (decompressed_data.size() != static_cast<size_t>(expected_size)) {
            std::cerr << "\nSize mismatch for " << name << "\n";
            return false;
        }

        // Verify SHA-256 hash if enabled and hash is present
        if (verify_hash_ && operation.has_data_sha256_hash() && 
            !operation.data_sha256_hash().empty()) {
            
            uint8_t calculated_hash[SHA256_DIGEST_SIZE];
            hasher.finalize(calculated_hash);

            const std::string& expected_hash_bytes = operation.data_sha256_hash();
            
            if (expected_hash_bytes.size() == SHA256_DIGEST_SIZE) {
                if (memcmp(calculated_hash, expected_hash_bytes.data(), SHA256_DIGEST_SIZE) != 0) {
                    // Convert hashes to hex for error message
                    char calculated_hex[65];
                    sha256_to_hex(calculated_hash, calculated_hex);
                    
                    char expected_hex[65];
                    sha256_to_hex(reinterpret_cast<const uint8_t*>(expected_hash_bytes.data()), 
                                  expected_hex);
                    
                    std::cerr << "\n✗ Hash verification failed for " << name << "\n";
                    std::cerr << "  Expected: " << expected_hex << "\n";
                    std::cerr << "  Got:      " << calculated_hex << "\n";
                    return false;
                }
            }
        }

        output.write(reinterpret_cast<char*>(decompressed_data.data()), decompressed_data.size());

        completed_ops++;

        if (progress_tracker &&
            (completed_ops == total_ops || (completed_ops % (total_ops / 20 + 1)) == 0)) {
            progress_tracker->update(name, completed_ops, total_ops);
        }
    }

    if (progress_tracker) {
        progress_tracker->update(name, total_ops, total_ops);
    }

    return true;
}

bool Payload::extractAll(const std::string& target_dir, int concurrency)
{
    return extractSelected(target_dir, {}, concurrency);
}

bool Payload::extractSelected(const std::string& target_dir,
                              const std::vector<std::string>& selected_partitions,
                              int concurrency)
{
    if (!initialized_)
        return false;

    std::vector<const chromeos_update_engine::PartitionUpdate*> to_extract;
    for (const auto& partition : manifest_.partitions()) {
        if (selected_partitions.empty() ||
            std::find(selected_partitions.begin(),
                      selected_partitions.end(),
                      partition.partition_name()) != selected_partitions.end()) {
            to_extract.push_back(&partition);
        }
    }

    if (to_extract.empty()) {
        std::cerr << "No partitions to extract\n";
        return false;
    }

    std::cout << "\nExtracting " << to_extract.size() << " partition(s)...\n";

    // Initialize progress tracker
    ProgressTracker progress_tracker;
    std::vector<std::string> partition_names;
    std::vector<int> operation_counts;
    
    for (const auto* p : to_extract) {
        partition_names.push_back(p->partition_name());
        operation_counts.push_back(p->operations_size());
    }
    
    progress_tracker.init(partition_names, operation_counts);

    std::queue<const chromeos_update_engine::PartitionUpdate*> work_queue;
    for (auto* p : to_extract) {
        work_queue.push(p);
    }

    std::mutex queue_mutex;
    std::atomic<bool> error_occurred{false};

    auto worker = [&]() {
        while (true) {
            const chromeos_update_engine::PartitionUpdate* partition = nullptr;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (work_queue.empty())
                    break;
                partition = work_queue.front();
                work_queue.pop();
            }

            std::string output_path = target_dir + "/" + partition->partition_name() + ".img";
            if (!extractPartition(*partition, output_path, &progress_tracker)) {
                error_occurred = true;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < concurrency; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    progress_tracker.finalize();

#ifdef HTTP_SUPPORT
    if (is_http_) {
        uint64_t downloaded = getBytesDownloaded();
        std::cout << "Total downloaded: " << formatBytes(downloaded) << "\n";
    }
#endif

    return !error_occurred;
}

} // namespace payload_dumper
