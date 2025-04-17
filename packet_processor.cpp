#include "packet_processor.h"
#include "log.h"
#include "common.h"
#include "thread_pool.h"
#include <cstring>
#include <cstdint>

// تعریف ساختارهای مورد نیاز برای هدرهای پروتکل
struct tcp_header {
    uint16_t source;
    uint16_t dest;
    uint32_t seq;
    uint32_t ack_seq;
    uint16_t flags;
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
};

struct udp_header {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
};

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
};

extern std::unique_ptr<ThreadPool> g_thread_pool;

PacketProcessor::PacketProcessor() : raw_mode_(-1), initialized_(false) {
    // مقداردهی اولیه بافرها
    memset(inbound_buffer_, 0, sizeof(inbound_buffer_));
    memset(outbound_buffer_, 0, sizeof(outbound_buffer_));

#if !defined(__MINGW32__) && !defined(_WIN32)
    // مقداردهی اولیه ساختارهای iovec
    vec_in_[0].iov_base = inbound_buffer_;
    vec_in_[0].iov_len = sizeof(inbound_buffer_);
    vec_out_[0].iov_base = outbound_buffer_;
    vec_out_[0].iov_len = sizeof(outbound_buffer_);
#endif
}

PacketProcessor::~PacketProcessor() {
    // در صورت نیاز، منابع را آزاد کنید
}

int PacketProcessor::initialize(int mode) {
    if (mode != mode_faketcp && mode != mode_udp && mode != mode_icmp) {
        mylog(log_error, "Invalid raw mode for PacketProcessor: %d\n", mode);
        return -1;
    }
    
    raw_mode_ = mode;
    initialized_ = true;
    
    mylog(log_info, "PacketProcessor initialized with mode %d\n", mode);
    return 0;
}

void PacketProcessor::set_raw_info(const raw_info_t& info) {
    raw_info_ = info;
}

const raw_info_t& PacketProcessor::get_raw_info() const {
    return raw_info_;
}

int PacketProcessor::process_incoming_packet(const char* input, int len, char* output, int& output_len) {
    if (!initialized_) {
        mylog(log_error, "PacketProcessor not initialized\n");
        return -1;
    }
    
    if (input == nullptr || len <= 0 || output == nullptr) {
        mylog(log_error, "Invalid parameters for process_incoming_packet\n");
        return -1;
    }
    
    // استفاده از thread pool برای پردازش پکت
    auto future = g_thread_pool->enqueue([this, input, len, output, &output_len]() {
        // کپی کردن داده ورودی به بافر داخلی
        memcpy(inbound_buffer_, input, len);
        
        // پردازش پکت براساس نوع پروتکل
        return process_packet_by_protocol(inbound_buffer_, len, output, output_len);
    });
    
    // منتظر اتمام پردازش
    return future.get();
}

int PacketProcessor::process_outgoing_packet(const char* input, int len, char* output, int& output_len) {
    if (!initialized_) {
        mylog(log_error, "PacketProcessor not initialized\n");
        return -1;
    }
    
    if (input == nullptr || len <= 0 || output == nullptr) {
        mylog(log_error, "Invalid parameters for process_outgoing_packet\n");
        return -1;
    }
    
    // استفاده از thread pool برای پردازش پکت
    auto future = g_thread_pool->enqueue([this, input, len, output, &output_len]() {
        // کپی کردن داده ورودی به بافر داخلی
        memcpy(inbound_buffer_, input, len);
        
        // پردازش پکت براساس نوع پروتکل
        packet_info_t &send_info = raw_info_.send_info;
        packet_info_t &recv_info = raw_info_.recv_info;
        
        switch (raw_mode_) {
            case mode_faketcp:
                // پردازش پکت TCP
                if (len + sizeof(tcp_header) > MAX_PACKET_SIZE) {
                    mylog(log_error, "Packet too large for TCP processing\n");
                    return -1;
                }
                
                // ساخت هدر TCP و کپی داده
                memcpy(output + sizeof(tcp_header), input, len);
                output_len = len + sizeof(tcp_header);
                break;
                
            case mode_udp:
                // پردازش پکت UDP
                if (len + sizeof(udp_header) > MAX_PACKET_SIZE) {
                    mylog(log_error, "Packet too large for UDP processing\n");
                    return -1;
                }
                
                // ساخت هدر UDP و کپی داده
                memcpy(output + sizeof(udp_header), input, len);
                output_len = len + sizeof(udp_header);
                break;
                
            case mode_icmp:
                // پردازش پکت ICMP
                if (len + sizeof(icmp_header) > MAX_PACKET_SIZE) {
                    mylog(log_error, "Packet too large for ICMP processing\n");
                    return -1;
                }
                
                // ساخت هدر ICMP و کپی داده
                memcpy(output + sizeof(icmp_header), input, len);
                output_len = len + sizeof(icmp_header);
                break;
                
            default:
                mylog(log_error, "Unknown raw mode: %d\n", raw_mode_);
                return -1;
        }
        
        return 0;
    });
    
    // منتظر اتمام پردازش
    return future.get();
}

int PacketProcessor::process_packet_by_protocol(const char* input, int len, char* output, int& output_len) {
    // پردازش پکت براساس نوع پروتکل
    switch (raw_mode_) {
        case mode_faketcp:
            // پردازش پکت TCP
            if (len < (int)sizeof(tcp_header)) {
                mylog(log_error, "TCP packet too small: %d\n", len);
                return -1;
            }
            
            // استخراج داده از هدر TCP
            memcpy(output, input + sizeof(tcp_header), len - sizeof(tcp_header));
            output_len = len - sizeof(tcp_header);
            break;
            
        case mode_udp:
            // پردازش پکت UDP
            if (len < (int)sizeof(udp_header)) {
                mylog(log_error, "UDP packet too small: %d\n", len);
                return -1;
            }
            
            // استخراج داده از هدر UDP
            memcpy(output, input + sizeof(udp_header), len - sizeof(udp_header));
            output_len = len - sizeof(udp_header);
            break;
            
        case mode_icmp:
            // پردازش پکت ICMP
            if (len < (int)sizeof(icmp_header)) {
                mylog(log_error, "ICMP packet too small: %d\n", len);
                return -1;
            }
            
            // استخراج داده از هدر ICMP
            memcpy(output, input + sizeof(icmp_header), len - sizeof(icmp_header));
            output_len = len - sizeof(icmp_header);
            break;
            
        default:
            mylog(log_error, "Unknown raw mode: %d\n", raw_mode_);
            return -1;
    }
    
    return 0;
} 