#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace payload_dumper
{

class ProgressTracker
{
  public:
    ProgressTracker();
    ~ProgressTracker();

    void init(const std::vector<std::string>& partition_names,
              const std::vector<int>& operation_counts);
    void update(const std::string& partition_name, int completed, int total);
    void finalize();

  private:
    struct PartitionProgress {
        int completed;
        int total;
    };

    std::mutex mutex_;
    std::map<std::string, PartitionProgress> progress_;
    std::vector<std::string> partition_order_;
    bool initialized_;
    bool finalized_;
    
#ifdef _WIN32
    void* console_handle_;
    bool vt_enabled_;
#endif

    void enableVirtualTerminal();
    void disableVirtualTerminal();
    void redrawAll();
    std::string createProgressBar(const std::string& name, int completed, int total);
};

} // namespace payload_dumper