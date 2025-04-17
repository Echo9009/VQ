#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <future>
#include "lock_free_queue.h"
#include "log.h"

// Thread pool for distributing work across multiple cores
class ThreadPool {
private:
    // Need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    
    // Task queue
    std::queue<std::function<void()>> tasks;
    
    // Synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};
    std::atomic<size_t> active_tasks{0};
    
    // Worker thread count
    size_t num_threads;
    
    // Singleton pattern
    ThreadPool(size_t threads = 0) : num_threads(threads) {
        // Default to using hardware concurrency
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            // Ensure at least one thread
            if (num_threads == 0) {
                num_threads = 1;
            }
        }
        
        mylog(log_info, "Starting thread pool with %zu threads\n", num_threads);
        
        // Create worker threads
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this, i] {
                // Register this thread with the queue manager
                ThreadQueueManager::instance().register_thread();
                
                // Process tasks from the queue
                for (;;) {
                    std::function<void()> task;
                    
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    
                    ++active_tasks;
                    task();
                    --active_tasks;
                }
            });
        }
    }
    
public:
    // Get the singleton instance
    static ThreadPool& instance(size_t threads = 0) {
        static ThreadPool instance(threads);
        return instance;
    }
    
    // Delete the copy and move constructors and assignment operators
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    
    // Add a task to the thread pool
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> 
    {
        using return_type = typename std::result_of<F(Args...)>::type;
        
        // Create a packaged task with the function and arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // Don't allow enqueueing after stopping the pool
            if (stop) {
                throw std::runtime_error("Enqueue on stopped ThreadPool");
            }
            
            tasks.emplace([task]() { (*task)(); });
        }
        
        condition.notify_one();
        return result;
    }
    
    // Get the number of threads in the pool
    size_t size() const {
        return num_threads;
    }
    
    // Get the number of active tasks
    size_t active() const {
        return active_tasks;
    }
    
    // Wait for all tasks to complete
    void wait_all() {
        while (active_tasks > 0 || !tasks.empty()) {
            std::this_thread::yield();
        }
    }
    
    // Stop the thread pool
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        
        condition.notify_all();
        
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

#endif // THREAD_POOL_H 