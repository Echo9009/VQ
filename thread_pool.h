#ifndef UDP2RAW_THREAD_POOL_H
#define UDP2RAW_THREAD_POOL_H

#include <vector>
#include <thread>
#include <future>
#include <functional>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <array>

// Lock-free queue implementation for per-thread work stealing
template<typename T>
class LockFreeQueue {
private:
    struct Node {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
        
        Node() : next(nullptr) {}
    };
    
    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic<size_t> size_counter;
    
public:
    LockFreeQueue() : size_counter(0) {
        // Initialize with a dummy node
        Node* dummy = new Node();
        head.store(dummy);
        tail.store(dummy);
    }
    
    // Delete copy and move constructors/assignments
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;
    
    ~LockFreeQueue() {
        // Clean up remaining nodes
        while (pop_front()) {}
        
        // Delete the dummy node
        Node* dummy = head.load();
        delete dummy;
    }
    
    void push_back(T item) {
        // Create a new node with the item
        std::shared_ptr<T> new_data = std::make_shared<T>(std::move(item));
        Node* new_node = new Node();
        new_node->data = new_data;
        
        // Add the node to the end of the queue
        Node* old_tail = tail.load();
        old_tail->next.store(new_node);
        tail.store(new_node);
        
        // Increment size counter
        size_counter.fetch_add(1, std::memory_order_relaxed);
    }
    
    bool pop_front(T& item) {
        Node* old_head = head.load();
        Node* next_node = old_head->next.load();
        
        // Check if queue is empty
        if (!next_node) {
            return false;
        }
        
        // Move the data from the node
        item = std::move(*next_node->data);
        
        // Update head to skip the popped node
        head.store(next_node);
        
        // Decrement size counter
        size_counter.fetch_sub(1, std::memory_order_relaxed);
        
        // Delete the old dummy node
        delete old_head;
        
        return true;
    }
    
    bool pop_front() {
        Node* old_head = head.load();
        Node* next_node = old_head->next.load();
        
        if (!next_node) {
            return false;
        }
        
        head.store(next_node);
        size_counter.fetch_sub(1, std::memory_order_relaxed);
        delete old_head;
        
        return true;
    }
    
    size_t size() const {
        return size_counter.load(std::memory_order_relaxed);
    }
    
    bool empty() const {
        return size() == 0;
    }
};

class ThreadPool {
public:
    ThreadPool(size_t num_threads)
        : stop(false)
    {
        // Create per-thread queues and idle flags
        for (size_t i = 0; i < num_threads; ++i) {
            queues.push_back(std::make_unique<LockFreeQueue<std::function<void()>>>());
            idle_flags.push_back(std::make_unique<std::atomic<bool>>(true));
        }
        
        // Create worker threads
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this, i] {
                size_t thread_id = i;
                size_t steal_attempt_count = 0;
                
                while (true) {
                    std::function<void()> task;
                    bool found_task = false;
                    
                    // First try to get a task from our own queue
                    if (!queues[thread_id]->empty()) {
                        found_task = queues[thread_id]->pop_front(task);
                    }
                    
                    // If no task in our queue, try work stealing
                    if (!found_task) {
                        idle_flags[thread_id]->store(true, std::memory_order_relaxed);
                        
                        for (size_t attempt = 0; attempt < queues.size() * 2 && !found_task; ++attempt) {
                            size_t victim = (thread_id + steal_attempt_count) % queues.size();
                            steal_attempt_count++;
                            
                            if (thread_id != victim && !queues[victim]->empty()) {
                                found_task = queues[victim]->pop_front(task);
                            }
                        }
                        
                        idle_flags[thread_id]->store(false, std::memory_order_relaxed);
                    }
                    
                    // If found a task, execute it
                    if (found_task) {
                        task();
                        continue;
                    }
                    
                    // Check if we should stop
                    if (stop.load(std::memory_order_relaxed)) {
                        return;
                    }
                    
                    // No task found, yield to give other threads a chance
                    std::this_thread::yield();
                    
                    // Sleep for a short time if no tasks are available
                    if (all_threads_idle()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        // Set stop flag to true
        stop.store(true, std::memory_order_relaxed);
        
        // Join all worker threads
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        
        // Create a packaged task that will execute the function with the arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        // Get the future from the task before the task is moved to the queue
        std::future<return_type> result = task->get_future();
        
        // Create a wrapper function that invokes the task
        std::function<void()> wrapper_task = [task]() {
            (*task)();
        };
        
        // Find the queue with the fewest tasks
        size_t min_queue_idx = 0;
        size_t min_queue_size = std::numeric_limits<size_t>::max();
        
        for (size_t i = 0; i < queues.size(); ++i) {
            size_t queue_size = queues[i]->size();
            if (queue_size < min_queue_size) {
                min_queue_size = queue_size;
                min_queue_idx = i;
            }
            
            // If we find an empty queue, use it immediately
            if (queue_size == 0) {
                break;
            }
        }
        
        // Add the task to the queue with the fewest tasks
        queues[min_queue_idx]->push_back(std::move(wrapper_task));
        
        return result;
    }
    
private:
    // Worker threads
    std::vector<std::thread> workers;
    
    // Per-thread task queues (lock-free)
    std::vector<std::unique_ptr<LockFreeQueue<std::function<void()>>>> queues;
    
    // Flags indicating if threads are idle
    std::vector<std::unique_ptr<std::atomic<bool>>> idle_flags;
    
    // Stop flag
    std::atomic<bool> stop;
    
    // Check if all threads are idle
    bool all_threads_idle() {
        for (const auto& flag : idle_flags) {
            if (!flag->load(std::memory_order_relaxed)) {
                return false;
            }
        }
        return true;
    }
};

#endif // UDP2RAW_THREAD_POOL_H 