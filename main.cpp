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

// Global thread pool
std::unique_ptr<ThreadPool> g_thread_pool;

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

    // Initialize thread pool with number of CPU cores
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4; // fallback if detection fails
    g_thread_pool = std::make_unique<ThreadPool>(num_threads);
    mylog(log_info, "Initialized thread pool with %u threads\n", num_threads);

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
    return 0;
}
