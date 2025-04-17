#ifndef UDP2RAW_THREAD_POOL_H
#define UDP2RAW_THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <atomic>
#include <future>
#include <functional>
#include <stdexcept>
#include <array>

// Lock-free single-producer, single-consumer queue (per thread)
template<typename T, size_t Size = 1024>
class LockFreeQueue {
    alignas(64) std::array<T, Size> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    bool push(T&& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) % Size;
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer_[current_tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        
        item = std::move(buffer_[current_head]);
        head_.store((current_head + 1) % Size, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
};

// Scheduler for work-stealing
class WorkStealingScheduler {
    using Task = std::function<void()>;
    
    alignas(64) std::vector<std::unique_ptr<LockFreeQueue<Task>>> local_queues_;
    alignas(64) std::atomic<size_t> next_queue_{0};
    
public:
    explicit WorkStealingScheduler(size_t num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            local_queues_.emplace_back(std::make_unique<LockFreeQueue<Task>>());
        }
    }

    bool push_task(Task&& task) {
        // Round-robin task distribution
        const size_t start_idx = next_queue_.fetch_add(1, std::memory_order_relaxed) % local_queues_.size();
        
        // Try to push to the selected queue
        if (local_queues_[start_idx]->push(std::move(task))) {
            return true;
        }
        
        // If failed, try other queues
        for (size_t i = 0; i < local_queues_.size(); ++i) {
            const size_t idx = (start_idx + i + 1) % local_queues_.size();
            if (local_queues_[idx]->push(std::move(task))) {
                return true;
            }
        }
        
        return false; // All queues are full
    }

    bool pop_task_from_local(size_t thread_id, Task& task) {
        return local_queues_[thread_id]->pop(task);
    }

    bool steal_task(size_t thread_id, Task& task) {
        for (size_t i = 0; i < local_queues_.size(); ++i) {
            size_t victim = (thread_id + i + 1) % local_queues_.size();
            if (victim != thread_id && local_queues_[victim]->pop(task)) {
                return true;
            }
        }
        return false;
    }
};

class ThreadPool {
private:
    std::vector<std::thread> workers_;
    WorkStealingScheduler scheduler_;
    std::atomic<bool> stop_{false};
    
    // Condition variable alternative for sleeping/waking threads
    struct alignas(64) ThreadNotifier {
        std::atomic<bool> flag_{false};
        
        void notify() {
            flag_.store(true, std::memory_order_release);
            // Could add a notification system like futex for more efficient wakeups
        }
        
        bool check_and_clear() {
            bool expected = true;
            return flag_.compare_exchange_strong(expected, false, 
                std::memory_order_acquire, std::memory_order_relaxed);
        }
    };
    
    std::vector<std::unique_ptr<ThreadNotifier>> notifiers_;

public:
    ThreadPool(size_t threads) : scheduler_(threads), stop_(false) {
        for (size_t i = 0; i < threads; ++i) {
            notifiers_.emplace_back(std::make_unique<ThreadNotifier>());
        }
        
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this, i] {
                this->worker_thread(i);
            });
        }
    }
    
    void worker_thread(size_t thread_id) {
        while (!stop_.load(std::memory_order_relaxed)) {
            std::function<void()> task;
            
            // Try to get task from own queue
            if (scheduler_.pop_task_from_local(thread_id, task)) {
                task();
                continue;
            }
            
            // Try to steal task from other queues
            if (scheduler_.steal_task(thread_id, task)) {
                task();
                continue;
            }
            
            // No task available, check for notifications
            if (notifiers_[thread_id]->check_and_clear()) {
                continue; // Woke up, try again
            }
            
            // No work and no notification, do a short exponential backoff
            for (int i = 0; i < 32; ++i) {
                if (stop_.load(std::memory_order_relaxed)) return;
                
                // Pause instruction to reduce power consumption and improve SMT performance
                #if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
                    _mm_pause();
                #elif defined(__i386__) || defined(__x86_64__)
                    __builtin_ia32_pause();
                #elif defined(__arm__) || defined(__aarch64__)
                    __asm__ volatile("yield" ::: "memory");
                #else
                    // Fallback: just a brief delay
                    for (volatile int j = 0; j < 50; ++j) {}
                #endif
                
                // Check for work again
                if (scheduler_.pop_task_from_local(thread_id, task) || 
                    scheduler_.steal_task(thread_id, task)) {
                    task();
                    break;
                }
            }
        }
    }
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        auto wrapper = [task]() { (*task)(); };
        
        // Try to push task to scheduler
        bool success = scheduler_.push_task(std::move(wrapper));
        
        if (!success) {
            throw std::runtime_error("Failed to enqueue task - all queues full");
        }
        
        // Notify a random worker thread
        size_t notify_thread = rand() % notifiers_.size();
        notifiers_[notify_thread]->notify();
        
        return res;
    }
    
    ~ThreadPool() {
        stop_.store(true, std::memory_order_release);
        
        // Notify all threads to check stop condition
        for (auto& notifier : notifiers_) {
            notifier->notify();
        }
        
        for (std::thread &worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

#endif 