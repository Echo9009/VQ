#include "common.h"
#include "network.h"
#include "connection.h"
#include "misc.h"
#include "log.h"

// Basic implementations just to satisfy the linker
int server_on_raw_recv_handshake1(conn_info_t &conn_info, char *ip_port, char *data, int data_len) {
    mylog(log_debug, "[%s] Received handshake1 packet in packet worker, len=%d", ip_port, data_len);
    // Simplified implementation - this would be expanded with actual functionality
    return 0;
}

int server_on_raw_recv_ready(conn_info_t &conn_info, char *ip_port, char type, char *data, int data_len) {
    mylog(log_debug, "[%s] Received packet in ready state, type=%c, len=%d", ip_port, type, data_len);
    // Simplified implementation - this would be expanded with actual functionality
    return 0;
} 