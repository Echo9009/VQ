#ifndef PACKET_PROCESSOR_H
#define PACKET_PROCESSOR_H

#include "misc.h"
#include <memory>
#include <cstdint>

// تعریف اندازه حداکثر پکت
#define MAX_PACKET_SIZE 4096

// ساختارهای header پروتکل‌ها از قبل در packet_processor.cpp تعریف شده‌اند
struct tcp_header;
struct udp_header;
struct icmp_header;

/**
 * کلاس PacketProcessor برای پردازش پکت‌ها با پروتکل‌های مختلف
 */
class PacketProcessor {
public:
    PacketProcessor();
    ~PacketProcessor();
    
    /**
     * مقداردهی اولیه کلاس با حالت مناسب
     * @param mode حالت پردازش (TCP، UDP، یا ICMP)
     * @return 0 در صورت موفقیت، -1 در صورت خطا
     */
    int initialize(int mode);
    
    /**
     * تنظیم اطلاعات raw برای پردازش
     * @param info اطلاعات raw
     */
    void set_raw_info(const raw_info_t& info);
    
    /**
     * دریافت اطلاعات raw
     * @return ساختار اطلاعات raw
     */
    const raw_info_t& get_raw_info() const;
    
    /**
     * پردازش پکت ورودی
     * @param input بافر ورودی
     * @param len طول داده ورودی
     * @param output بافر خروجی
     * @param output_len طول داده خروجی (به عنوان مرجع تغییر می‌کند)
     * @return 0 در صورت موفقیت، -1 در صورت خطا
     */
    int process_incoming_packet(const char* input, int len, char* output, int& output_len);
    
    /**
     * پردازش پکت خروجی
     * @param input بافر ورودی
     * @param len طول داده ورودی
     * @param output بافر خروجی
     * @param output_len طول داده خروجی (به عنوان مرجع تغییر می‌کند)
     * @return 0 در صورت موفقیت، -1 در صورت خطا
     */
    int process_outgoing_packet(const char* input, int len, char* output, int& output_len);
    
private:
    /**
     * پردازش پکت براساس نوع پروتکل
     * @param input بافر ورودی
     * @param len طول داده ورودی
     * @param output بافر خروجی
     * @param output_len طول داده خروجی (به عنوان مرجع تغییر می‌کند)
     * @return 0 در صورت موفقیت، -1 در صورت خطا
     */
    int process_packet_by_protocol(const char* input, int len, char* output, int& output_len);
    
    // متغیرهای عضو کلاس
    raw_info_t raw_info_; // اطلاعات خام مرتبط با پکت
    int raw_mode_; // حالت پردازش (TCP، UDP، یا ICMP)
    bool initialized_; // آیا کلاس مقداردهی اولیه شده است
    
    // بافرهای داخلی برای کاهش تخصیص پویا حافظه
    char inbound_buffer_[MAX_PACKET_SIZE];
    char outbound_buffer_[MAX_PACKET_SIZE];
    
#if !defined(__MINGW32__) && !defined(_WIN32)
    // برای استفاده از readv/writev در لینوکس
    struct iovec vec_in_[1];
    struct iovec vec_out_[1];
#endif
};

// متغیر سراسری برای PacketProcessor
extern PacketProcessor g_packet_processor;

#endif // PACKET_PROCESSOR_H 