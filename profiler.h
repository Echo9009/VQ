#ifndef UDP2RAW_PROFILER_H
#define UDP2RAW_PROFILER_H

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>

// Forward declarations
class ThreadPool;

// Profiling metrics
struct ProfileMetric {
    std::string name;
    std::string category;
    double value;
    std::string units;
    std::chrono::system_clock::time_point timestamp;
};

struct SystemMetrics {
    double cpu_usage_percent;
    size_t memory_usage_bytes;
    size_t network_tx_bytes;
    size_t network_rx_bytes;
    size_t packets_processed;
    size_t packets_dropped;
};

class Profiler {
public:
    static Profiler& getInstance();
    
    // Initialize with Discord webhook URL
    void initialize(const std::string& webhook_url, int reporting_interval_ms = 5000);
    
    // Start/stop the profiler
    void start();
    void stop();
    
    // Record various performance metrics
    void recordFunctionTiming(const std::string& function_name, double execution_time_ms);
    void recordThreadUtilization(int thread_id, double utilization_percent);
    void recordMemoryUsage(size_t bytes_used);
    void recordNetworkActivity(size_t bytes_sent, size_t bytes_received);
    void recordPacketStats(size_t processed, size_t dropped);
    void recordCustomMetric(const std::string& name, const std::string& category, 
                          double value, const std::string& units);
    
    // Register thread pool for monitoring
    void registerThreadPool(std::shared_ptr<ThreadPool> pool);
    
    // Get current system metrics
    SystemMetrics getCurrentMetrics() const;

private:
    Profiler();  // Private constructor for singleton
    ~Profiler();
    
    // Discord reporting
    void reportToDiscord();
    std::string formatDiscordMessage();
    bool sendWebhookMessage(const std::string& message);
    
    // Internal data collection
    void collectSystemMetrics();
    void updateHistoricalMetrics();
    std::string generatePerformanceChart();
    
    // Reporting thread function
    void reportingThreadFunc();
    
    // Data members
    std::string webhook_url_;
    int reporting_interval_ms_;
    std::atomic<bool> running_;
    std::thread reporting_thread_;
    
    // Performance metrics storage
    std::mutex metrics_mutex_;
    std::vector<ProfileMetric> recent_metrics_;
    std::map<std::string, std::vector<double>> historical_metrics_;
    SystemMetrics current_system_metrics_;
    
    // Collected statistics
    std::chrono::system_clock::time_point start_time_;
    std::atomic<size_t> total_packets_processed_;
    std::atomic<size_t> total_packets_dropped_;
    std::atomic<size_t> total_bytes_sent_;
    std::atomic<size_t> total_bytes_received_;
    
    // Thread pool monitoring
    std::weak_ptr<ThreadPool> thread_pool_;
};

// Convenience macro for timing function calls
#define PROFILE_FUNCTION() \
    auto profile_start_time = std::chrono::high_resolution_clock::now(); \
    auto profile_function_name = __func__; \
    struct ProfilerFunctionGuard { \
        std::chrono::time_point<std::chrono::high_resolution_clock> start; \
        const char* func_name; \
        ProfilerFunctionGuard(std::chrono::time_point<std::chrono::high_resolution_clock> s, const char* n) \
            : start(s), func_name(n) {} \
        ~ProfilerFunctionGuard() { \
            auto end = std::chrono::high_resolution_clock::now(); \
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0; \
            Profiler::getInstance().recordFunctionTiming(func_name, duration); \
        } \
    } profile_guard(profile_start_time, profile_function_name);

#endif // UDP2RAW_PROFILER_H 