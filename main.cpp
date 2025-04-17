#include "common.h"
#include "network.h"
#include "connection.h"
#include "misc.h"
#include "log.h"
#include "lib/md5.h"
#include "encrypt.h"
#include "fd_manager.h"
#include "thread_pool.h"
#include <thread>
#include <memory>   // for std::unique_ptr

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>  // for sysconf
#endif

// Global variables
std::unique_ptr<ThreadPool> g_thread_pool;
volatile bool program_terminated = false;  // Flag to signal program termination

// Function to determine optimal thread count based on system
unsigned int determine_optimal_thread_count() {
    unsigned int processor_count = std::thread::hardware_concurrency();
    
    // If we couldn't detect, use a reasonable default
    if (processor_count == 0) {
        return 4;
    }
    
    // For packet processing, using N or N-1 threads is usually best
    // where N is the number of physical cores
    if (processor_count > 4) {
        return processor_count - 1; // Leave one core for OS and other tasks
    }
    
    return processor_count;
}

// Function to adjust buffer parameters based on system capabilities
void adjust_buffer_parameters() {
    // Physical memory size detection
    size_t mem_size = 0;
    
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    mem_size = status.ullTotalPhys;
#else
    // Linux and other UNIX-like systems
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        mem_size = pages * page_size;
    }
#endif

    // Adjust buffer parameters based on available memory
    // Use at most 5% of physical memory for buffers
    size_t max_buffer_memory = mem_size * 0.05;
    
    // Default: 128 buffers of 8KB each = 1MB total
    size_t buffer_size = 8192;
    size_t buffer_count = 128;
    
    // For low memory systems (less than 1GB)
    if (mem_size < 1073741824) {
        buffer_size = 4096;
        buffer_count = 64;
    }
    // For high memory systems (more than 8GB)
    else if (mem_size > 8589934592) {
        buffer_size = 16384;
        buffer_count = 512;
    }
    
    // Ensure we don't exceed our memory limit
    size_t total_buffer_memory = buffer_size * buffer_count;
    if (total_buffer_memory > max_buffer_memory) {
        buffer_count = max_buffer_memory / buffer_size;
        if (buffer_count < 32) {
            buffer_count = 32; // Minimum number of buffers
            buffer_size = max_buffer_memory / buffer_count;
        }
    }
    
    // Set the global parameters for buffer pool
    g_zero_copy_buffer_pool.buffer_size = buffer_size;
    g_zero_copy_buffer_pool.num_buffers = buffer_count;
    
    mylog(log_info, "Adjusted buffer parameters: %zu buffers of %zu bytes each (total: %.2f MB)\n", 
          buffer_count, buffer_size, (buffer_size * buffer_count) / (1024.0 * 1024.0));
}

// Signal handler to set termination flag
void set_program_terminated() {
    program_terminated = true;
}

void sigpipe_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigpipe, ignored");
}

void sigterm_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigterm, exit");
    set_program_terminated();
    myexit(0);
}

void sigint_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigint, exit");
    set_program_terminated();
    myexit(0);
}

int client_event_loop();
int server_event_loop();

int main(int argc, char *argv[]) {
    assert(sizeof(unsigned short) == 2);
    assert(sizeof(unsigned int) == 4);
    assert(sizeof(unsigned long long) == 8);

#if defined(_WIN32) || defined(__MINGW32__)
    init_ws();
    enable_log_color = 0;
#endif

    dup2(1, 2);  // redirect stderr to stdout

    pre_process_arg(argc, argv);

    // Optimize thread count and buffer parameters for the system
    adjust_buffer_parameters();
    unsigned int num_threads = determine_optimal_thread_count();
    
    // Initialize thread pool with optimized thread count
    g_thread_pool = std::unique_ptr<ThreadPool>(new ThreadPool(num_threads));
    mylog(log_info, "Initialized thread pool with %u threads (optimized for this system)\n", num_threads);

    // Initialize zero-copy buffer pool with optimized parameters
    if (init_zero_copy_buffers(g_zero_copy_buffer_pool) != 0) {
        mylog(log_warn, "Failed to initialize zero-copy buffers, falling back to standard mode\n");
    } else {
        mylog(log_info, "Zero-copy buffer mode enabled for improved performance\n");
    }

    ev_signal signal_watcher_sigpipe;
    ev_signal signal_watcher_sigterm;
    ev_signal signal_watcher_sigint;

    if (program_mode == client_mode) {
        struct ev_loop *loop = ev_default_loop(0);
#if !defined(_WIN32) && !defined(__MINGW32__)
        ev_signal_init(&signal_watcher_sigpipe, sigpipe_cb, SIGPIPE);
        ev_signal_start(loop, &signal_watcher_sigpipe);
#endif
        ev_signal_init(&signal_watcher_sigterm, sigterm_cb, SIGTERM);
        ev_signal_start(loop, &signal_watcher_sigterm);

        ev_signal_init(&signal_watcher_sigint, sigint_cb, SIGINT);
        ev_signal_start(loop, &signal_watcher_sigint);
    } else {
#ifdef UDP2RAW_LINUX
        signal(SIGINT, signal_handler);
        signal(SIGHUP, signal_handler);
        signal(SIGKILL, signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGQUIT, signal_handler);
#else
        mylog(log_fatal, "server mode not supported in multi-platform version\n");
        myexit(-1);
#endif
    }

#if !defined(_WIN32) && !defined(__MINGW32__)
    if (geteuid() != 0) {
        mylog(log_warn, "root check failed, it seems like you are using a non-root account. we can try to continue, but it may fail. If you want to run udp2raw as non-root, you have to add iptables rule manually, and grant udp2raw CAP_NET_RAW capability, check README.md in repo for more info.\n");
    } else {
        mylog(log_warn, "you can run udp2raw with non-root account for better security. check README.md in repo for more info.\n");
    }
#endif

    mylog(log_info, "remote_ip=[%s], make sure this is a vaild IP address\n", remote_addr.get_ip());

    // init_random_number_fd();
    srand(get_true_random_number_nz());
    const_id = get_true_random_number_nz();

    mylog(log_info, "const_id:%x\n", const_id);

    my_init_keys(key_string, program_mode == client_mode ? 1 : 0);

#ifdef UDP2RAW_LINUX
    iptables_rule();
    init_raw_socket();
#endif

    if (program_mode == client_mode) {
        client_event_loop();
    } else {
#ifdef UDP2RAW_LINUX
        server_event_loop();
#else
        mylog(log_fatal, "server mode not supported in multi-platform version\n");
        myexit(-1);
#endif
    }

    // Thread pool will be automatically cleaned up when program exits
    cleanup_zero_copy_buffers(g_zero_copy_buffer_pool);
    return 0;
}
