#ifndef UDP2RAW_THREAD_POOL_H
#define UDP2RAW_THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
    
    // New methods for profiling
    size_t getThreadCount() const { return workers.size(); }
    size_t getBusyThreadCount() const { return busy_threads; }
    size_t getQueueSize() const;
    double getUtilization() const;

private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::function<void()>> tasks;
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    
    // For profiling
    std::atomic<size_t> busy_threads{0};
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads) : stop(false), busy_threads(0) {
    for(size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace([task, this]() {
            busy_threads++;
            (*task)();
            busy_threads--;
        });
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        if(worker.joinable())
            worker.join();
}

inline size_t ThreadPool::getQueueSize() const {
    // Use const_cast to allow locking a const mutex - this is safe because we're only reading
    std::unique_lock<std::mutex> lock(*const_cast<std::mutex*>(&queue_mutex));
    return tasks.size();
}

inline double ThreadPool::getUtilization() const {
    if (workers.empty()) return 0.0;
    return (double)busy_threads / workers.size() * 100.0;
}

#endif 