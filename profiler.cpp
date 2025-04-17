#include "profiler.h"
#include "thread_pool.h"
#include "log.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// For system metrics
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#endif

using json = nlohmann::json;

// Callback for CURL
size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append(contents, size * nmemb);
    return size * nmemb;
}

// Singleton implementation
Profiler& Profiler::getInstance() {
    static Profiler instance;
    return instance;
}

Profiler::Profiler() 
    : reporting_interval_ms_(5000),
      running_(false),
      total_packets_processed_(0),
      total_packets_dropped_(0),
      total_bytes_sent_(0),
      total_bytes_received_(0),
      thread_pool_(nullptr) {
    // Initialize CURL globally
    curl_global_init(CURL_GLOBAL_ALL);
}

Profiler::~Profiler() {
    stop();
    curl_global_cleanup();
}

void Profiler::initialize(const std::string& webhook_url, int reporting_interval_ms) {
    webhook_url_ = webhook_url;
    reporting_interval_ms_ = reporting_interval_ms;
    
    mylog(log_info, "Profiler initialized with Discord webhook URL: %s (interval: %d ms)\n", 
          webhook_url_.c_str(), reporting_interval_ms_);
}

void Profiler::start() {
    if (running_) return;
    
    running_ = true;
    start_time_ = std::chrono::system_clock::now();
    
    // Reset counters
    total_packets_processed_ = 0;
    total_packets_dropped_ = 0;
    total_bytes_sent_ = 0;
    total_bytes_received_ = 0;
    
    // Clear metrics
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        recent_metrics_.clear();
        historical_metrics_.clear();
    }
    
    // Start reporting thread
    reporting_thread_ = std::thread(&Profiler::reportingThreadFunc, this);
    
    mylog(log_info, "Profiler started\n");
}

void Profiler::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Wait for reporting thread to finish
    if (reporting_thread_.joinable()) {
        reporting_thread_.join();
    }
    
    // Send one final report
    reportToDiscord();
    
    mylog(log_info, "Profiler stopped\n");
}

void Profiler::recordFunctionTiming(const std::string& function_name, double execution_time_ms) {
    if (!running_) return;
    
    ProfileMetric metric;
    metric.name = function_name;
    metric.category = "function_timing";
    metric.value = execution_time_ms;
    metric.units = "ms";
    metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(metric);
}

void Profiler::recordThreadUtilization(int thread_id, double utilization_percent) {
    if (!running_) return;
    
    ProfileMetric metric;
    metric.name = "thread_" + std::to_string(thread_id);
    metric.category = "thread_utilization";
    metric.value = utilization_percent;
    metric.units = "%";
    metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(metric);
}

void Profiler::recordMemoryUsage(size_t bytes_used) {
    if (!running_) return;
    
    ProfileMetric metric;
    metric.name = "memory_usage";
    metric.category = "system";
    metric.value = static_cast<double>(bytes_used);
    metric.units = "bytes";
    metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(metric);
}

void Profiler::recordNetworkActivity(size_t bytes_sent, size_t bytes_received) {
    if (!running_) return;
    
    total_bytes_sent_ += bytes_sent;
    total_bytes_received_ += bytes_received;
    
    ProfileMetric sent_metric;
    sent_metric.name = "network_tx";
    sent_metric.category = "network";
    sent_metric.value = static_cast<double>(bytes_sent);
    sent_metric.units = "bytes";
    sent_metric.timestamp = std::chrono::system_clock::now();
    
    ProfileMetric recv_metric;
    recv_metric.name = "network_rx";
    recv_metric.category = "network";
    recv_metric.value = static_cast<double>(bytes_received);
    recv_metric.units = "bytes";
    recv_metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(sent_metric);
    recent_metrics_.push_back(recv_metric);
}

void Profiler::recordPacketStats(size_t processed, size_t dropped) {
    if (!running_) return;
    
    total_packets_processed_ += processed;
    total_packets_dropped_ += dropped;
    
    ProfileMetric proc_metric;
    proc_metric.name = "packets_processed";
    proc_metric.category = "packets";
    proc_metric.value = static_cast<double>(processed);
    proc_metric.units = "count";
    proc_metric.timestamp = std::chrono::system_clock::now();
    
    ProfileMetric drop_metric;
    drop_metric.name = "packets_dropped";
    drop_metric.category = "packets";
    drop_metric.value = static_cast<double>(dropped);
    drop_metric.units = "count";
    drop_metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(proc_metric);
    recent_metrics_.push_back(drop_metric);
}

void Profiler::recordCustomMetric(const std::string& name, const std::string& category, 
                                 double value, const std::string& units) {
    if (!running_) return;
    
    ProfileMetric metric;
    metric.name = name;
    metric.category = category;
    metric.value = value;
    metric.units = units;
    metric.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recent_metrics_.push_back(metric);
}

void Profiler::registerThreadPool(ThreadPool* pool) {
    thread_pool_ = pool;
}

SystemMetrics Profiler::getCurrentMetrics() const {
    return current_system_metrics_;
}

void Profiler::reportingThreadFunc() {
    while (running_) {
        // Sleep for the reporting interval
        std::this_thread::sleep_for(std::chrono::milliseconds(reporting_interval_ms_));
        
        if (!running_) break;
        
        // Collect system metrics
        collectSystemMetrics();
        
        // Update historical metrics
        updateHistoricalMetrics();
        
        // Report to Discord
        reportToDiscord();
    }
}

void Profiler::collectSystemMetrics() {
    // Get CPU and memory usage
    SystemMetrics metrics = {};
    
#ifdef _WIN32
    // Windows implementation
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        static ULARGE_INTEGER lastIdleTime = {0};
        static ULARGE_INTEGER lastKernelTime = {0};
        static ULARGE_INTEGER lastUserTime = {0};
        
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime;
        idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;
        
        if (lastIdleTime.QuadPart != 0) {
            ULARGE_INTEGER idleDiff, kernelDiff, userDiff;
            idleDiff.QuadPart = idle.QuadPart - lastIdleTime.QuadPart;
            kernelDiff.QuadPart = kernel.QuadPart - lastKernelTime.QuadPart;
            userDiff.QuadPart = user.QuadPart - lastUserTime.QuadPart;
            
            ULARGE_INTEGER totalDiff;
            totalDiff.QuadPart = kernelDiff.QuadPart + userDiff.QuadPart;
            
            if (totalDiff.QuadPart > 0) {
                double idlePercent = 100.0 * idleDiff.QuadPart / totalDiff.QuadPart;
                metrics.cpu_usage_percent = 100.0 - idlePercent;
            }
        }
        
        lastIdleTime = idle;
        lastKernelTime = kernel;
        lastUserTime = user;
    }
    
    // Memory info
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        metrics.memory_usage_bytes = pmc.WorkingSetSize;
    }
#else
    // Linux implementation
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        static struct timespec lastTime = {0};
        static double lastUsage = 0;
        
        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        
        double currentUsage = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0 +
                            usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
        
        if (lastTime.tv_sec != 0) {
            double timeElapsed = (currentTime.tv_sec - lastTime.tv_sec) + 
                               (currentTime.tv_nsec - lastTime.tv_nsec) / 1000000000.0;
            double usageElapsed = currentUsage - lastUsage;
            
            if (timeElapsed > 0) {
                metrics.cpu_usage_percent = 100.0 * usageElapsed / timeElapsed;
            }
        }
        
        lastTime = currentTime;
        lastUsage = currentUsage;
    }
    
    // Memory info
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        metrics.memory_usage_bytes = (info.totalram - info.freeram) * info.mem_unit;
    }
    
    // Process memory info
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        long resident;
        statm >> resident; // First value is total program size
        statm >> resident; // Second value is resident set size
        metrics.memory_usage_bytes = resident * sysconf(_SC_PAGESIZE);
        statm.close();
    }
#endif

    // Set packet and network info from our counters
    metrics.packets_processed = total_packets_processed_;
    metrics.packets_dropped = total_packets_dropped_;
    metrics.network_tx_bytes = total_bytes_sent_;
    metrics.network_rx_bytes = total_bytes_received_;
    
    // Thread pool info if available
    if (thread_pool_ != nullptr) {
        // Record thread pool metrics
        int busy_threads = thread_pool_->getBusyThreadCount();
        int total_threads = thread_pool_->getThreadCount();
        
        recordCustomMetric("thread_pool_utilization", "threads", 
                         100.0 * busy_threads / std::max(1, total_threads), "%");
        recordCustomMetric("busy_threads", "threads", busy_threads, "count");
        recordCustomMetric("total_threads", "threads", total_threads, "count");
    }
    
    // Update current metrics
    current_system_metrics_ = metrics;
}

void Profiler::updateHistoricalMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Group recent metrics by name
    std::map<std::string, std::vector<double>> grouped_metrics;
    for (const auto& metric : recent_metrics_) {
        grouped_metrics[metric.name].push_back(metric.value);
    }
    
    // Calculate averages and add to historical data
    for (const auto& [name, values] : grouped_metrics) {
        if (values.empty()) continue;
        
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double avg = sum / values.size();
        
        historical_metrics_[name].push_back(avg);
        
        // Keep only last 100 values for each metric
        if (historical_metrics_[name].size() > 100) {
            historical_metrics_[name].erase(historical_metrics_[name].begin());
        }
    }
    
    // Clear recent metrics after processing
    recent_metrics_.clear();
}

std::string Profiler::generatePerformanceChart() {
    // Simple ASCII chart for key metrics
    std::stringstream chart;
    
    // Find metrics to display
    std::vector<std::string> key_metrics = {
        "cpu_usage_percent", 
        "memory_usage", 
        "thread_pool_utilization",
        "packets_processed",
        "network_tx"
    };
    
    // Generate a simple chart
    chart << "```\n";
    chart << "Performance Trends (last " << reporting_interval_ms_ / 1000 << " seconds):\n";
    
    for (const auto& metric_name : key_metrics) {
        if (historical_metrics_.find(metric_name) != historical_metrics_.end() && 
            !historical_metrics_[metric_name].empty()) {
            
            const auto& values = historical_metrics_[metric_name];
            
            // Only show chart if we have enough data points
            if (values.size() < 2) continue;
            
            double min_val = *std::min_element(values.begin(), values.end());
            double max_val = *std::max_element(values.begin(), values.end());
            double range = max_val - min_val;
            
            // Set width of chart
            const int chart_width = 20;
            
            chart << std::left << std::setw(25) << metric_name << " ";
            
            // Show last 10 values or less
            int start_idx = std::max(0, static_cast<int>(values.size()) - 10);
            
            for (int i = start_idx; i < values.size(); i++) {
                double normalized = range > 0 ? (values[i] - min_val) / range : 0.5;
                int bar_height = static_cast<int>(normalized * chart_width);
                
                chart << static_cast<char>(0x2581 + std::min(bar_height, 7));
            }
            
            // Add current value
            chart << " " << std::fixed << std::setprecision(2) << values.back() << "\n";
        }
    }
    
    chart << "```";
    return chart.str();
}

std::string Profiler::formatDiscordMessage() {
    auto now = std::chrono::system_clock::now();
    auto runtime = now - start_time_;
    auto runtime_seconds = std::chrono::duration_cast<std::chrono::seconds>(runtime).count();
    
    // Get current time as string
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream time_ss;
    time_ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    
    // Format Discord embed
    json embed;
    
    // Title and description
    embed["title"] = "UDP2RAW Performance Report";
    embed["description"] = "Performance metrics collected at " + time_ss.str();
    
    // Color (Discord uses decimal color values)
    embed["color"] = 3447003;  // Blue color
    
    // Fields for various metrics
    json fields = json::array();
    
    // Runtime field
    fields.push_back({
        {"name", "Runtime"},
        {"value", std::to_string(runtime_seconds) + " seconds"},
        {"inline", true}
    });
    
    // CPU Usage
    std::stringstream cpu_ss;
    cpu_ss << std::fixed << std::setprecision(2) << current_system_metrics_.cpu_usage_percent << "%";
    fields.push_back({
        {"name", "CPU Usage"},
        {"value", cpu_ss.str()},
        {"inline", true}
    });
    
    // Memory Usage
    std::stringstream mem_ss;
    double mem_mb = current_system_metrics_.memory_usage_bytes / (1024.0 * 1024.0);
    mem_ss << std::fixed << std::setprecision(2) << mem_mb << " MB";
    fields.push_back({
        {"name", "Memory Usage"},
        {"value", mem_ss.str()},
        {"inline", true}
    });
    
    // Packet statistics
    fields.push_back({
        {"name", "Packets Processed"},
        {"value", std::to_string(current_system_metrics_.packets_processed)},
        {"inline", true}
    });
    
    if (current_system_metrics_.packets_dropped > 0) {
        double drop_rate = 0;
        if (current_system_metrics_.packets_processed > 0) {
            drop_rate = 100.0 * current_system_metrics_.packets_dropped / 
                       (current_system_metrics_.packets_processed + current_system_metrics_.packets_dropped);
        }
        
        std::stringstream drop_ss;
        drop_ss << std::to_string(current_system_metrics_.packets_dropped)
                << " (" << std::fixed << std::setprecision(2) << drop_rate << "%)";
        
        fields.push_back({
            {"name", "Packets Dropped"},
            {"value", drop_ss.str()},
            {"inline", true}
        });
    }
    
    // Network throughput
    double tx_mb = current_system_metrics_.network_tx_bytes / (1024.0 * 1024.0);
    double rx_mb = current_system_metrics_.network_rx_bytes / (1024.0 * 1024.0);
    
    std::stringstream net_ss;
    net_ss << std::fixed << std::setprecision(2) << tx_mb << " MB out / " 
           << std::fixed << std::setprecision(2) << rx_mb << " MB in";
    
    fields.push_back({
        {"name", "Network Traffic"},
        {"value", net_ss.str()},
        {"inline", false}
    });
    
    // Thread pool status if available
    if (thread_pool_ != nullptr) {
        int busy = thread_pool_->getBusyThreadCount();
        int total = thread_pool_->getThreadCount();
        double utilization = 100.0 * busy / std::max(1, total);
        
        std::stringstream thread_ss;
        thread_ss << busy << "/" << total << " threads active ("
                 << std::fixed << std::setprecision(2) << utilization << "% utilization)";
        
        fields.push_back({
            {"name", "Thread Pool"},
            {"value", thread_ss.str()},
            {"inline", false}
        });
    }
    
    // Performance chart
    std::string chart = generatePerformanceChart();
    if (!chart.empty()) {
        fields.push_back({
            {"name", "Performance Trends"},
            {"value", chart},
            {"inline", false}
        });
    }
    
    // Add fields to embed
    embed["fields"] = fields;
    
    // Footer with timestamp
    embed["footer"] = {
        {"text", "UDP2RAW Performance Monitor"}
    };
    embed["timestamp"] = time_ss.str();
    
    // Create final message
    json message;
    message["embeds"] = json::array({embed});
    
    return message.dump();
}

bool Profiler::sendWebhookMessage(const std::string& message) {
    if (webhook_url_.empty()) {
        mylog(log_warn, "Discord webhook URL is not set\n");
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        mylog(log_error, "Failed to initialize CURL\n");
        return false;
    }
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, webhook_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "UDP2RAW Profiler/1.0");
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        mylog(log_error, "Discord webhook request failed: %s\n", curl_easy_strerror(res));
        return false;
    }
    
    mylog(log_debug, "Discord webhook response: %s\n", response.c_str());
    return true;
}

void Profiler::reportToDiscord() {
    std::string message = formatDiscordMessage();
    if (!sendWebhookMessage(message)) {
        mylog(log_warn, "Failed to send profiling data to Discord\n");
    }
} 