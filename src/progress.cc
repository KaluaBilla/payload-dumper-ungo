#include "progress.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace payload_dumper
{

ProgressTracker::ProgressTracker()
    : initialized_(false), finalized_(false)
#ifdef _WIN32
      ,
      console_handle_(nullptr), vt_enabled_(false)
#endif
{
}

ProgressTracker::~ProgressTracker()
{
    if (initialized_ && !finalized_) {
        finalize();
    }
#ifdef _WIN32
    disableVirtualTerminal();
#endif
}

void ProgressTracker::enableVirtualTerminal()
{
#ifdef _WIN32
    console_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console_handle_ == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(console_handle_, &mode)) {
        return;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(console_handle_, mode)) {
        vt_enabled_ = true;
    }
#endif
}

void ProgressTracker::disableVirtualTerminal()
{
#ifdef _WIN32
    if (vt_enabled_ && console_handle_ != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(console_handle_, &mode)) {
            mode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(console_handle_, mode);
        }
        vt_enabled_ = false;
    }
#endif
}

void ProgressTracker::init(const std::vector<std::string>& partition_names,
                           const std::vector<int>& operation_counts)
{
    std::lock_guard<std::mutex> lock(mutex_);

    enableVirtualTerminal();

    progress_.clear();
    partition_order_.clear();

    for (size_t i = 0; i < partition_names.size(); ++i) {
        const std::string& name = partition_names[i];
        int ops = (i < operation_counts.size()) ? operation_counts[i] : 0;
        progress_[name] = {0, ops};
        partition_order_.push_back(name);
    }

    // reserve space for progress bars
    for (size_t i = 0; i < partition_order_.size(); ++i) {
        std::cout << "\n";
    }
    std::cout << std::flush;

    initialized_ = true;
}

std::string ProgressTracker::createProgressBar(const std::string& name, int completed, int total)
{
    int percentage = (total > 0) ? (completed * 100) / total : 0;

    const int bar_width = 30;
    int filled = (bar_width * percentage) / 100;

    std::ostringstream oss;
    oss << "[" << std::setw(15) << std::left << name << "] ";

    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            oss << "=";
        } else if (i == filled && percentage < 100) {
            oss << ">";
        } else {
            oss << " ";
        }
    }

    oss << " " << std::setw(3) << std::right << percentage << "% (" << completed << "/" << total
        << ")";

    return oss.str();
}

void ProgressTracker::redrawAll()
{
    if (!initialized_ || finalized_) {
        return;
    }

    // move cursor up to start of progress section
    if (!partition_order_.empty()) {
        std::cout << "\033[" << partition_order_.size() << "A";
    }

    // draw all progress bars
    for (const auto& name : partition_order_) {
        auto it = progress_.find(name);
        if (it != progress_.end()) {
            std::string line = createProgressBar(name, it->second.completed, it->second.total);
            std::cout << "\r" << line << "\033[K\n";
        }
    }

    std::cout << std::flush;
}

void ProgressTracker::update(const std::string& partition_name, int completed, int total)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || finalized_) {
        return;
    }

    auto it = progress_.find(partition_name);
    if (it != progress_.end()) {
        it->second.completed = completed;
        it->second.total = total;
    } else {
        progress_[partition_name] = {completed, total};
    }

    redrawAll();
}

void ProgressTracker::finalize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || finalized_) {
        return;
    }

    std::cout << "\n" << std::flush;
    finalized_ = true;

#ifdef _WIN32
    disableVirtualTerminal();
#endif
}

} // namespace payload_dumper