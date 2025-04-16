#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <future>
#include "common.h"
#include "log.h"

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop(false) {
        mylog(log_info, "Creating thread pool with %zu threads\n", num_threads);
        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back([this, i] {
                mylog(log_info, "Worker thread %zu started\n", i);
                ev_loop_ptr loop = ev_loop_new(EVFLAG_AUTO);
                loops.push_back(loop);
                
                struct ev_async watcher;
                ev_async_init(&watcher, [](struct ev_loop *loop, struct ev_async *w, int revents) {
                    // This callback will be triggered when ev_async_send is called
                });
                ev_async_start(loop, &watcher);
                
                // Store watcher for later use
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    watchers.push_back(&watcher);
                    loop_watchers.push_back(std::make_pair(loop, &watcher));
                }
                
                // Thread working loop
                while (!stop) {
                    std::function<void(struct ev_loop*)> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });
                        
                        if (stop && tasks.empty()) {
                            break;
                        }
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    
                    task(loop);
                }
                
                // Cleanup
                ev_async_stop(loop, &watcher);
                ev_loop_destroy(loop);
                mylog(log_info, "Worker thread %zu stopped\n", i);
            });
        }
    }
    
    template<class F>
    auto enqueue(F&& f) -> std::future<decltype(f(nullptr))> {
        using return_type = decltype(f(nullptr));
        
        auto task = std::make_shared<std::packaged_task<return_type(struct ev_loop*)>>(
            std::forward<F>(f)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex);
            
            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            
            tasks.emplace([task](struct ev_loop* loop) { (*task)(loop); });
        }
        condition.notify_one();
        return res;
    }
    
    void notify_all_loops() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& pair : loop_watchers) {
            ev_async_send(pair.first, pair.second);
        }
    }
    
    struct ev_loop* get_loop(size_t index) {
        if (index < loops.size()) {
            return loops[index];
        }
        return nullptr;
    }
    
    size_t size() const {
        return workers.size();
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex);
            stop = true;
        }
        
        condition.notify_all();
        
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
private:
    using ev_loop_ptr = struct ev_loop*;
    
    std::vector<std::thread> workers;
    std::vector<ev_loop_ptr> loops;
    std::vector<struct ev_async*> watchers;
    std::vector<std::pair<struct ev_loop*, struct ev_async*>> loop_watchers;
    std::queue<std::function<void(struct ev_loop*)>> tasks;
    
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

#endif // THREAD_POOL_H 