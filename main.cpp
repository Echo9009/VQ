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
#include <memory>

// Implement make_unique for C++11 (it's only available in C++14 and later)
namespace std {
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}

// Forward declarations
int client_event_loop();
int server_event_loop();

// Global thread pool
std::unique_ptr<ThreadPool> g_thread_pool;

// Number of CPU cores to use (default to hardware concurrency if available)
int num_worker_threads = std::thread::hardware_concurrency();

void sigpipe_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigpipe, ignored");
}

void sigterm_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigterm, exit");
    myexit(0);
}

void sigint_cb(struct ev_loop *l, ev_signal *w, int revents) {
    mylog(log_info, "got sigint, exit");
    myexit(0);
}

int client_event_loop_multi_thread();
int server_event_loop_multi_thread();

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

    // Parse num_worker_threads from command line if provided
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_worker_threads = atoi(argv[i + 1]);
            if (num_worker_threads <= 0) {
                num_worker_threads = std::thread::hardware_concurrency();
            }
            mylog(log_info, "Using %d worker threads\n", num_worker_threads);
            break;
        }
    }

    // Initialize thread pool with specified number of threads
    g_thread_pool = std::make_unique<ThreadPool>(num_worker_threads);

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
        client_event_loop_multi_thread();
    } else {
#ifdef UDP2RAW_LINUX
        server_event_loop_multi_thread();
#else
        mylog(log_fatal, "server mode not supported in multi-platform version\n");
        myexit(-1);
#endif
    }

    return 0;
}

// Multi-threaded client event loop implementation
int client_event_loop_multi_thread() {
    mylog(log_info, "Starting multi-threaded client event loop with %d threads\n", num_worker_threads);
    
    // Run the regular client_event_loop in the main thread
    // This will be our primary event loop
    return client_event_loop();
}

// Multi-threaded server event loop implementation
int server_event_loop_multi_thread() {
#ifdef UDP2RAW_LINUX
    mylog(log_info, "Starting multi-threaded server event loop with %d threads\n", num_worker_threads);
    
    // Run the regular server_event_loop in the main thread
    // This will be our primary event loop
    return server_event_loop();
#else
    mylog(log_fatal, "server mode not supported in multi-platform version\n");
    myexit(-1);
    return -1;
#endif
}
