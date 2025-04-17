#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <assert.h>
#include "common.h"
#include "log.h"

// Cache line size on most modern processors
constexpr size_t CACHE_LINE_SIZE = 64;

// Round up to next power of 2
constexpr size_t next_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

// Make sure queue_len is a power of 2
constexpr size_t QUEUE_LEN_POWER_OF_2 = next_power_of_2(queue_len);

// Padding to avoid false sharing - specialized for non-atomic types
template <typename T, typename = void>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T data;
    
    CacheAligned() = default;
    
    CacheAligned(const T& val) : data(val) {}
    
    CacheAligned& operator=(const T& val) {
        data = val;
        return *this;
    }
    
    operator T&() { return data; }
    operator const T&() const { return data; }
};

// Specialization for atomic types which cannot be copy-constructed
template <typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<T>> {
    std::atomic<T> data;
    
    CacheAligned() = default;
    
    CacheAligned(T val) {
        data.store(val, std::memory_order_relaxed);
    }
    
    CacheAligned& operator=(T val) {
        data.store(val, std::memory_order_relaxed);
        return *this;
    }
    
    operator std::atomic<T>&() { return data; }
    operator const std::atomic<T>&() const { return data; }
};

// Single-producer, single-consumer lock-free queue for per-thread use
template <size_t MaxSize = QUEUE_LEN_POWER_OF_2>
class SPSCLockFreeQueue {
private:
    // Buffer and position tracking with proper cache alignment
    char buffer[MaxSize][huge_buf_len];
    int sizes[MaxSize];
    
    // Use specialized CacheAligned for atomics
    CacheAligned<std::atomic<size_t>> head;
    CacheAligned<std::atomic<size_t>> tail;

public:
    SPSCLockFreeQueue() {
        head.data.store(0, std::memory_order_relaxed);
        tail.data.store(0, std::memory_order_relaxed);
    }
    
    bool empty() const {
        return head.data.load(std::memory_order_acquire) == 
               tail.data.load(std::memory_order_acquire);
    }
    
    bool full() const {
        size_t next_tail = (tail.data.load(std::memory_order_acquire) + 1) % MaxSize;
        return next_tail == head.data.load(std::memory_order_acquire);
    }
    
    // Push data to the queue (for producer)
    bool push(const char* data, int len) {
        const size_t current_tail = tail.data.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) % MaxSize;
        
        if (next_tail == head.data.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        memcpy(buffer[current_tail], data, len);
        sizes[current_tail] = len;
        
        // Memory barrier to ensure data is written before tail is updated
        tail.data.store(next_tail, std::memory_order_release);
        return true;
    }
    
    // Pop data from the queue (for consumer)
    bool pop(char*& data, int& len) {
        const size_t current_head = head.data.load(std::memory_order_relaxed);
        
        if (current_head == tail.data.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        
        data = buffer[current_head];
        len = sizes[current_head];
        
        // Memory barrier to ensure data is read before head is updated
        head.data.store((current_head + 1) % MaxSize, std::memory_order_release);
        return true;
    }
    
    // Peek at the front data without removing it
    bool peek(char*& data, int& len) {
        const size_t current_head = head.data.load(std::memory_order_relaxed);
        
        if (current_head == tail.data.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        
        data = buffer[current_head];
        len = sizes[current_head];
        return true;
    }
    
    void clear() {
        head.data.store(0, std::memory_order_relaxed);
        tail.data.store(0, std::memory_order_relaxed);
    }
};

// Multi-producer, multi-consumer lock-free queue
template <size_t MaxSize = QUEUE_LEN_POWER_OF_2>
class MPMCLockFreeQueue {
private:
    struct Cell {
        std::atomic<size_t> sequence;
        char data[huge_buf_len];
        int size;
        
        Cell() {
            sequence.store(0, std::memory_order_relaxed);
        }
    };
    
    alignas(CACHE_LINE_SIZE) Cell* buffer;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> enqueue_pos{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> dequeue_pos{0};
    
    const size_t buffer_mask;
    
public:
    MPMCLockFreeQueue() : buffer_mask(MaxSize - 1) {
        // Size must be power of 2
        mylog(log_debug, "Creating MPMC queue with size %zu (requested size was %zu)\n", 
              MaxSize, queue_len);
        
        buffer = new Cell[MaxSize];
        for (size_t i = 0; i < MaxSize; ++i) {
            buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    ~MPMCLockFreeQueue() {
        delete[] buffer;
    }
    
    bool push(const char* item, int size) {
        Cell* cell;
        size_t pos = enqueue_pos.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer[pos & buffer_mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            
            if (diff == 0) {
                // Cell is available, try to claim it
                if (enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                // Another thread is in the middle of enqueuing, move to next position
                pos = enqueue_pos.load(std::memory_order_relaxed);
            }
        }
        
        // Write the data
        memcpy(cell->data, item, size);
        cell->size = size;
        
        // Make the cell available for dequeuing
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }
    
    bool pop(char*& item, int& size) {
        Cell* cell;
        size_t pos = dequeue_pos.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer[pos & buffer_mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
            
            if (diff == 0) {
                // Cell is available for dequeuing
                if (dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // Queue is empty
                return false;
            } else {
                // Another thread is in the middle of dequeuing, move to next position
                pos = dequeue_pos.load(std::memory_order_relaxed);
            }
        }
        
        // Read the data
        item = cell->data;
        size = cell->size;
        
        // Mark the cell as available for enqueuing
        cell->sequence.store(pos + buffer_mask + 1, std::memory_order_release);
        return true;
    }
    
    bool empty() const {
        size_t pos = dequeue_pos.load(std::memory_order_relaxed);
        Cell* cell = &buffer[pos & buffer_mask];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        return seq != pos + 1;
    }
    
    void clear() {
        while (!empty()) {
            char* unused;
            int unused_size;
            pop(unused, unused_size);
        }
    }
};

// Per-thread queue manager to handle multi-core scaling
class ThreadQueueManager {
private:
    static constexpr size_t MAX_THREADS = 64;
    
    // Thread-local storage for thread ID - use extern in header to avoid multiple definitions
    static thread_local int thread_id;
    
    // Queue per thread (SPSC queue for each thread)
    std::vector<std::unique_ptr<SPSCLockFreeQueue<>>> per_thread_queues;
    
    // Global queue for load balancing (MPMC queue)
    std::unique_ptr<MPMCLockFreeQueue<>> global_queue;
    
    // Track active threads
    std::atomic<int> next_thread_id{0};
    
    // Singleton pattern
    ThreadQueueManager() {
        // Initialize global queue with C++14 make_unique
        global_queue = std::make_unique<MPMCLockFreeQueue<>>();
        
        // Initialize per-thread queues
        per_thread_queues.resize(MAX_THREADS);
        for (size_t i = 0; i < MAX_THREADS; ++i) {
            per_thread_queues[i] = std::make_unique<SPSCLockFreeQueue<>>();
        }
    }
    
public:
    // Get singleton instance
    static ThreadQueueManager& instance() {
        static ThreadQueueManager instance;
        return instance;
    }
    
    // Delete copy and move constructors and assignment operators
    ThreadQueueManager(const ThreadQueueManager&) = delete;
    ThreadQueueManager& operator=(const ThreadQueueManager&) = delete;
    ThreadQueueManager(ThreadQueueManager&&) = delete;
    ThreadQueueManager& operator=(ThreadQueueManager&&) = delete;
    
    // Register current thread and get thread ID
    int register_thread() {
        if (thread_id == -1) {
            thread_id = next_thread_id.fetch_add(1, std::memory_order_relaxed);
            if (thread_id >= static_cast<int>(MAX_THREADS)) {
                mylog(log_fatal, "Maximum number of threads exceeded\n");
                myexit(-1);
            }
        }
        return thread_id;
    }
    
    // Get the queue for the current thread
    SPSCLockFreeQueue<>* get_thread_queue() {
        if (thread_id == -1) {
            register_thread();
        }
        return per_thread_queues[thread_id].get();
    }
    
    // Get the global queue (shared between all threads)
    MPMCLockFreeQueue<>* get_global_queue() {
        return global_queue.get();
    }
    
    // Push packet to thread-specific queue
    bool push_to_thread(int target_thread_id, const char* data, int len) {
        if (target_thread_id >= 0 && target_thread_id < static_cast<int>(MAX_THREADS)) {
            return per_thread_queues[target_thread_id]->push(data, len);
        }
        return false;
    }
    
    // Push packet to global queue (when thread queue is full or for load balancing)
    bool push_to_global(const char* data, int len) {
        return global_queue->push(data, len);
    }
    
    // Pop packet from current thread's queue, or from global if local is empty
    bool pop_packet(char*& data, int& len) {
        // First try the thread's own queue
        auto* thread_queue = get_thread_queue();
        if (thread_queue->pop(data, len)) {
            return true;
        }
        
        // If empty, try the global queue
        return global_queue->pop(data, len);
    }
};

// Initialize thread_local member in a separate .cpp file
// thread_local int ThreadQueueManager::thread_id = -1;

#endif // LOCK_FREE_QUEUE_H 