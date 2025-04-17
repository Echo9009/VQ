#include "worker.h"
#include "common.h"
#include "log.h"
#include "misc.h"
#include "network.h"
#include "connection.h"

// Forward declarations for server functions
extern int server_on_raw_recv_handshake1(conn_info_t &conn_info, char *ip_port, char *data, int data_len);
extern int server_on_raw_recv_ready(conn_info_t &conn_info, char *ip_port, char type, char *data, int data_len);

// Don't redefine these globals - they're already defined in network.cpp
// extern char g_packet_buf[huge_buf_len];
// extern int g_packet_buf_len;
// extern int g_packet_buf_cnt;

// Implementation of server_on_raw_recv that was declared in worker.h
int server_on_raw_recv(conn_info_t &conn_info) {
    // Extract the packet data from the global buffer
    char *data;
    int data_len;
    
    // Extract packet info from connection info
    packet_info_t &send_info = conn_info.raw_info.send_info;
    packet_info_t &recv_info = conn_info.raw_info.recv_info;
    raw_info_t &raw_info = conn_info.raw_info;
    
    mylog(log_trace, "<server_on_raw_recv>");
    
    // Get pointer to first byte in our buffer
    char *buf = g_packet_buf;
    data_len = g_packet_buf_len;
    
    // Get a buffer to format IP address for logging
    char ip_port[max_addr_len];
    address_t addr;
    addr.from_ip_port_new(raw_ip_version, &recv_info.new_src_ip, recv_info.src_port);
    addr.to_str(ip_port);
    
    // Handle packets based on the connection state
    if (conn_info.state.server_current_state == server_handshake1) {
        // For handshake1 state, we need to process the handshake data
        server_on_raw_recv_handshake1(conn_info, ip_port, buf, data_len);
        return 0;
    } 
    else if (conn_info.state.server_current_state == server_ready) {
        // For ready state, the first byte is the packet type
        char type = buf[0];
        char *data = buf + 1;
        int actual_data_len = data_len - 1;
        
        // Process the packet data
        server_on_raw_recv_ready(conn_info, ip_port, type, data, actual_data_len);
        return 0;
    }
    
    // If we reach here, the packet wasn't processed due to invalid state
    mylog(log_debug, "packet not processed, connection in state: %d", 
          conn_info.state.server_current_state);
    return -1;
} 