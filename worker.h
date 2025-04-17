#ifndef WORKER_H
#define WORKER_H

#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <memory>
#include "lock_free_queue.h"
#include "thread_pool.h"
#include "common.h"
#include "log.h"

// Forward declarations
struct conn_info_t;
int client_on_raw_recv(conn_info_t &conn_info);
int server_on_raw_recv(conn_info_t &conn_info);

// External globals we need access to
extern char g_packet_buf[huge_buf_len];
extern int g_packet_buf_len;
extern int g_packet_buf_cnt;

// Structure to hold packet processing context
struct PacketContext {
    char buffer[huge_buf_len];
    int len;
    conn_info_t* conn_info;
    bool is_client_mode;
    
    PacketContext(const char* data, int data_len, conn_info_t* conn, bool client_mode) 
        : len(data_len), conn_info(conn), is_client_mode(client_mode) {
        memcpy(buffer, data, data_len);
    }
};

// Multi-threaded packet processor
class PacketWorker {
private:
    // Singleton pattern
    PacketWorker() {
        // Initialize the thread pool for packet processing
        mylog(log_info, "Initializing packet worker with %zu threads\n", 
              ThreadPool::instance().size());
        
        // Set flag indicating we're running
        running.store(true, std::memory_order_release);
    }
    
    // Flag to signal workers to stop
    std::atomic<bool> running{false};
    
public:
    // Get singleton instance
    static PacketWorker& instance() {
        static PacketWorker instance;
        return instance;
    }
    
    // Delete copy and move constructors and assignment operators
    PacketWorker(const PacketWorker&) = delete;
    PacketWorker& operator=(const PacketWorker&) = delete;
    PacketWorker(PacketWorker&&) = delete;
    PacketWorker& operator=(PacketWorker&&) = delete;
    
    // Start processing packets
    void start(bool is_client_mode) {
        ThreadPool::instance();  // Ensure thread pool is initialized
        
        mylog(log_info, "Starting packet workers in %s mode\n", 
              is_client_mode ? "client" : "server");
    }
    
    // Process a packet asynchronously
    void process_packet(const char* packet, int len, conn_info_t* conn_info, bool is_client_mode) {
        if (!running.load(std::memory_order_acquire)) {
            return;
        }
        
        // Create a shared pointer to the packet context with C++14's make_shared
        auto context = std::make_shared<PacketContext>(packet, len, conn_info, is_client_mode);
        
        // Create a job for the thread pool using a lambda that captures the shared context
        ThreadPool::instance().enqueue(
            [this, context]() {
                // Process in the worker thread
                if (context->is_client_mode) {
                    // Set up globals needed for processing
                    g_packet_buf_len = context->len;
                    memcpy(g_packet_buf, context->buffer, context->len);
                    g_packet_buf_cnt = 1;
                    
                    // Process the packet
                    client_on_raw_recv(*context->conn_info);
                } else {
                    // Set up globals needed for processing
                    g_packet_buf_len = context->len;
                    memcpy(g_packet_buf, context->buffer, context->len);
                    g_packet_buf_cnt = 1;
                    
                    // Process the packet
                    server_on_raw_recv(*context->conn_info);
                }
                
                // Reset global counters
                g_packet_buf_cnt = 0;
            }
        );
    }
    
    // Stop all workers
    void stop() {
        running.store(false, std::memory_order_release);
        
        // Wait for all tasks to complete
        ThreadPool::instance().wait_all();
        
        mylog(log_info, "All packet workers stopped\n");
    }
    
    // Destructor ensures thread pool is properly shutdown
    ~PacketWorker() {
        if (running.load(std::memory_order_acquire)) {
            stop();
        }
    }
};

#endif // WORKER_H 