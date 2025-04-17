#ifndef UDP2RAW_BATCH_PROCESSOR_H
#define UDP2RAW_BATCH_PROCESSOR_H

#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include "common.h"
#include "log.h"
#include "memory_pool.h"

// Forward declaration
struct raw_info_t;

// Structure to hold packet data and metadata
struct PacketBatch {
    using PacketPtr = std::shared_ptr<MemoryBuffer>;
    std::vector<PacketPtr> packets;
    std::vector<int> packet_lengths;
    std::vector<raw_info_t> raw_infos;
    
    void clear() {
        packets.clear();
        packet_lengths.clear();
        raw_infos.clear();
    }
    
    size_t size() const {
        return packets.size();
    }
    
    bool empty() const {
        return packets.size() == 0;
    }
};

// Batch processor class for efficient packet processing
class BatchProcessor {
public:
    using ProcessingFunction = std::function<void(const PacketBatch&)>;
    
    BatchProcessor(
        size_t maxBatchSize = 64, 
        std::chrono::milliseconds maxDelay = std::chrono::milliseconds(10),
        size_t numWorkerThreads = 2
    ) : 
        maxBatchSize_(maxBatchSize),
        maxDelay_(maxDelay),
        running_(false),
        currentBatch_(std::make_shared<PacketBatch>()),
        lastProcessTime_(std::chrono::steady_clock::now())
    {
        workers_.resize(numWorkerThreads);
    }
    
    ~BatchProcessor() {
        stop();
    }
    
    // Start the batch processor with the given processing function
    void start(ProcessingFunction processingFunction) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return;
        
        running_ = true;
        processingFunction_ = processingFunction;
        
        // Start the worker threads
        for (size_t i = 0; i < workers_.size(); ++i) {
            workers_[i] = std::thread(&BatchProcessor::workerThread, this);
        }
        
        // Start the timer thread
        timerThread_ = std::thread(&BatchProcessor::timerThread, this);
    }
    
    // Stop the batch processor
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) return;
            running_ = false;
        }
        
        // Wake up all waiting threads
        condition_.notify_all();
        
        // Join all worker threads
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        // Join the timer thread
        if (timerThread_.joinable()) {
            timerThread_.join();
        }
    }
    
    // Add a packet to the batch
    void addPacket(const char* data, int length, const raw_info_t& raw_info) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get a buffer from the memory pool
        auto buffer = g_memory_pool->getBuffer(length);
        memcpy(buffer->data(), data, length);
        
        // Add the packet to the current batch
        currentBatch_->packets.push_back(buffer);
        currentBatch_->packet_lengths.push_back(length);
        currentBatch_->raw_infos.push_back(raw_info);
        
        // Process the batch if it's full
        if (currentBatch_->size() >= maxBatchSize_) {
            processBatch();
        }
    }
    
    // Force processing of the current batch
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!currentBatch_->empty()) {
            processBatch();
        }
    }
    
private:
    // Process the current batch and create a new one
    void processBatch() {
        auto batchToProcess = currentBatch_;
        currentBatch_ = std::make_shared<PacketBatch>();
        lastProcessTime_ = std::chrono::steady_clock::now();
        
        // Add the batch to the queue
        batchQueue_.push_back(batchToProcess);
        
        // Notify a worker
        condition_.notify_one();
    }
    
    // Worker thread function
    void workerThread() {
        while (running_) {
            std::shared_ptr<PacketBatch> batch;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return !running_ || !batchQueue_.empty();
                });
                
                if (!running_ && batchQueue_.empty()) {
                    break;
                }
                
                if (!batchQueue_.empty()) {
                    batch = batchQueue_.front();
                    batchQueue_.erase(batchQueue_.begin());
                }
            }
            
            if (batch) {
                processingFunction_(*batch);
            }
        }
    }
    
    // Timer thread function to ensure batches are processed even if not full
    void timerThread() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProcessTime_);
            
            if (!currentBatch_->empty() && elapsed >= maxDelay_) {
                processBatch();
            }
        }
    }
    
    size_t maxBatchSize_;
    std::chrono::milliseconds maxDelay_;
    std::atomic<bool> running_;
    
    std::mutex mutex_;
    std::condition_variable condition_;
    
    std::shared_ptr<PacketBatch> currentBatch_;
    std::vector<std::shared_ptr<PacketBatch>> batchQueue_;
    
    std::chrono::steady_clock::time_point lastProcessTime_;
    
    ProcessingFunction processingFunction_;
    std::vector<std::thread> workers_;
    std::thread timerThread_;
};

// Global batch processor instance
extern std::unique_ptr<BatchProcessor> g_packet_batch_processor;

// Initialize the global batch processor
void init_batch_processor(
    size_t maxBatchSize = 64,
    std::chrono::milliseconds maxDelay = std::chrono::milliseconds(10),
    size_t numWorkerThreads = 2
);

#endif // UDP2RAW_BATCH_PROCESSOR_H 