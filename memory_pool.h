#ifndef UDP2RAW_MEMORY_POOL_H
#define UDP2RAW_MEMORY_POOL_H

#include <mutex>
#include <vector>
#include <memory>
#include <cstddef>
#include <functional>
#include <cassert>

// Memory chunk class that holds preallocated memory
class MemoryChunk {
public:
    MemoryChunk(size_t size) : size_(size) {
        data_ = new char[size];
    }
    
    ~MemoryChunk() {
        delete[] data_;
    }
    
    char* data() { return data_; }
    size_t size() const { return size_; }
    
private:
    char* data_;
    size_t size_;
};

// Memory buffer class with reference counting for zero-copy operations
class MemoryBuffer {
public:
    MemoryBuffer(char* data, size_t size, std::function<void()> releaseCallback)
        : data_(data), size_(size), releaseCallback_(releaseCallback) {}
    
    ~MemoryBuffer() {
        if (releaseCallback_) {
            releaseCallback_();
        }
    }
    
    char* data() { return data_; }
    size_t size() const { return size_; }
    
private:
    char* data_;
    size_t size_;
    std::function<void()> releaseCallback_;
};

// Memory pool class for efficient memory allocation
class MemoryPool : public std::enable_shared_from_this<MemoryPool> {
public:
    MemoryPool(size_t chunkSize = 4096, size_t initialChunks = 32) 
        : chunkSize_(chunkSize) {
        // Pre-allocate initial chunks
        for (size_t i = 0; i < initialChunks; ++i) {
            availableChunks_.push_back(std::make_shared<MemoryChunk>(chunkSize_));
        }
    }
    
    // Get a buffer from the pool
    std::shared_ptr<MemoryBuffer> getBuffer(size_t size) {
        if (size > chunkSize_) {
            // For oversized buffers, allocate directly
            char* data = new char[size];
            return std::make_shared<MemoryBuffer>(data, size, [data]() { delete[] data; });
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (availableChunks_.empty()) {
            // Allocate a new chunk if none available
            availableChunks_.push_back(std::make_shared<MemoryChunk>(chunkSize_));
        }
        
        // Get a chunk from the pool
        std::shared_ptr<MemoryChunk> chunk = availableChunks_.back();
        availableChunks_.pop_back();
        
        // Create a buffer with a release callback that returns the chunk to the pool
        std::weak_ptr<MemoryPool> weakThis = shared_from_this();
        auto releaseCallback = [weakThis, chunk]() {
            if (auto pool = weakThis.lock()) {
                std::lock_guard<std::mutex> lock(pool->mutex_);
                pool->availableChunks_.push_back(chunk);
            }
        };
        
        return std::make_shared<MemoryBuffer>(chunk->data(), chunk->size(), releaseCallback);
    }
    
    // Get the number of available chunks (for diagnostics)
    size_t availableChunks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return availableChunks_.size();
    }
    
private:
    size_t chunkSize_;
    std::vector<std::shared_ptr<MemoryChunk>> availableChunks_;
    mutable std::mutex mutex_;
};

// Global memory pool instance
extern std::shared_ptr<MemoryPool> g_memory_pool;

// Initialize the global memory pool
inline void init_memory_pool(size_t chunkSize = 4096, size_t initialChunks = 32) {
    g_memory_pool = std::make_shared<MemoryPool>(chunkSize, initialChunks);
}

#endif // UDP2RAW_MEMORY_POOL_H 