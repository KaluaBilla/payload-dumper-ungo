#include "payload.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    std::string input_file;
    std::string output_dir;
    std::vector<std::string> partitions;
    std::string user_agent;
    int concurrency = 0;
    bool list_only = false;
    bool verify_hash = true;  // Enable verification by default
};

void printUsage(const char* program_name)
{
    std::cerr << "Usage: " << program_name << " [options] <input_file|url>\n\n"
              << "Options:\n"
              << "  -l, --list              List partitions in payload.bin\n"
              << "  -o, --output DIR        Output directory\n"
              << "  -p, --partitions LIST   Extract only specified partitions (comma-separated)\n"
              << "  -c, --concurrency N     Number of extraction threads\n"
              << "  --no-verify             Disable SHA-256 hash verification\n"
#ifdef HTTP_SUPPORT
              << "  -u, --user-agent STR    Custom User-Agent for HTTP requests\n"
#endif
              << "\n";
}

bool parseArguments(int argc, char* argv[], Options& opts)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        } else if (arg == "-l" || arg == "--list") {
            opts.list_only = true;
        } else if (arg == "--no-verify") {
            opts.verify_hash = false;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return false;
            }
            opts.output_dir = argv[++i];
        } else if (arg == "-p" || arg == "--partitions") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return false;
            }
            std::string partitions_str = argv[++i];
            std::stringstream ss(partitions_str);
            std::string partition;
            while (std::getline(ss, partition, ',')) {
                opts.partitions.push_back(partition);
            }
        } else if (arg == "-c" || arg == "--concurrency") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return false;
            }
            opts.concurrency = std::stoi(argv[++i]);
#ifdef HTTP_SUPPORT
        } else if (arg == "-u" || arg == "--user-agent") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return false;
            }
            opts.user_agent = argv[++i];
#endif
        } else if (arg[0] != '-') {
            opts.input_file = arg;
        } else {
            std::cerr << "Error: unknown option " << arg << "\n";
            return false;
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: input file or URL required\n";
        printUsage(argv[0]);
        return false;
    }

    return true;
}

std::string generateOutputDir()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "extracted_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

bool isUrl(const std::string& path)
{
    return (path.size() >= 7 && path.substr(0, 7) == "http://") ||
           (path.size() >= 8 && path.substr(0, 8) == "https://");
}

int main(int argc, char* argv[])
{
    Options opts;

    if (!parseArguments(argc, argv, opts)) {
        return 1;
    }

    if (!isUrl(opts.input_file) && !fs::exists(opts.input_file)) {
        std::cerr << "Error: file does not exist: " << opts.input_file << "\n";
        return 1;
    }

#ifndef HTTP_SUPPORT
    if (isUrl(opts.input_file)) {
        std::cerr << "Error: HTTP support not compiled. Use -DHTTP_SUPPORT\n";
        return 1;
    }
#endif

    if (isUrl(opts.input_file)) {
        std::cout << "Source: " << opts.input_file << " (remote)\n";
    } else {
        std::cout << "Source: " << opts.input_file << "\n";
    }

    payload_dumper::Payload payload(opts.input_file, opts.user_agent, opts.verify_hash);

    if (!payload.open()) {
        std::cerr << "Failed to open payload\n";
        return 1;
    }

    if (!payload.init()) {
        std::cerr << "Failed to initialize payload\n";
        return 1;
    }

    if (opts.list_only) {
        payload.listPartitions();
        return 0;
    }

    if (opts.concurrency == 0) {
        opts.concurrency = std::thread::hardware_concurrency();
        if (opts.concurrency == 0)
            opts.concurrency = 4;
    }

    if (opts.output_dir.empty()) {
        opts.output_dir = generateOutputDir();
    }

    if (!fs::exists(opts.output_dir)) {
        fs::create_directory(opts.output_dir);
    }

    std::cout << "Output directory: " << opts.output_dir << "\n";
    std::cout << "Concurrency: " << opts.concurrency << " thread(s)\n";

    auto start_time = std::chrono::steady_clock::now();

    bool success;
    if (opts.partitions.empty()) {
        success = payload.extractAll(opts.output_dir, opts.concurrency);
    } else {
        success = payload.extractSelected(opts.output_dir, opts.partitions, opts.concurrency);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

    if (!success) {
        std::cerr << "\n✗ Extraction failed\n";
        return 1;
    }

    std::cout << "\n✓ Extraction completed in " << duration.count() << " seconds\n";

#ifdef HTTP_SUPPORT
    if (isUrl(opts.input_file)) {
        uint64_t downloaded = payload.getBytesDownloaded();
        double mb = downloaded / (1024.0 * 1024.0);
        if (duration.count() > 0) {
            double speed = mb / duration.count();
            std::cout << "Average download speed: " << std::fixed << std::setprecision(2) << speed
                      << " MB/s\n";
        }
    }
#endif

    return 0;
}