# Multi-Core Server Architecture for udp2raw

## مقدمه
این داکیومنت توضیح می‌دهد که چگونه سرور udp2raw به صورت واقعی و حرفه‌ای مولتی‌کور (چند-هسته‌ای) شده است، چه تغییراتی در ساختار پروژه داده شده و چه نکاتی برای توسعه آینده باید رعایت شود.

---

## ۱. هدف
هدف این تغییرات، استفاده بهینه از تمام هسته‌های CPU برای افزایش کارایی و توان عملیاتی سرور udp2raw است. با این ساختار، هر هسته می‌تواند یک worker مستقل داشته باشد که یک event loop کامل سرور را اجرا می‌کند.

---

## ۲. ساختار جدید سرور

### الف) تابع `start_server_workers`
- این تابع در فایل `server.cpp` قرار دارد.
- به تعداد هسته‌های CPU (یا مقدار دلخواه)، thread (worker) ایجاد می‌کند.
- هر worker تابع `server_worker` را اجرا می‌کند.

### ب) تابع `server_worker`
- هر worker به صورت اختیاری به یک هسته خاص CPU پین می‌شود (برای بهبود locality و کارایی).
- هر worker یک instance کامل از event loop سرور (`server_event_loop`) را اجرا می‌کند.
- منابع مورد نیاز هر worker باید جداگانه یا به صورت thread-safe مدیریت شوند.

### ج) تغییر در `main.cpp`
- اگر برنامه در حالت سرور اجرا شود، به جای اجرای یک event loop، تابع `start_server_workers` با تعداد ترد مناسب فراخوانی می‌شود.
- ساختار کلاینت بدون تغییر باقی مانده است.

---

## ۳. نکات مهم توسعه و نگهداری

- **منابع Global:** اگر متغیر یا fd یا ساختار داده‌ای داری که بین تردها مشترک است و thread-safe نیست، باید آن را جداگانه برای هر worker تعریف کنی یا thread-safe کنی.
- **init های Global:** کارهایی مثل `iptables_rule` و `init_raw_socket` فقط باید یک بار (در اولین worker) انجام شوند. برای این کار از mutex استفاده شده است.
- **پین کردن تردها:** پین کردن هر worker به یک هسته خاص اختیاری است اما برای کارایی بهتر توصیه می‌شود.
- **گسترش آینده:** می‌توانی منطق هر worker را گسترش دهی (مثلاً تقسیم بار هوشمند، مدیریت sessionها، یا بهینه‌سازی منابع).

---

## ۴. راهنمای استفاده و توسعه

- برای تغییر تعداد workerها، می‌توانی مقدار `num_workers` را در `main.cpp` تغییر دهی (پیش‌فرض: تعداد هسته‌های CPU).
- برای افزودن منطق جدید به هر worker، کافی است کد مربوطه را در `server_worker` یا `server_event_loop` اضافه کنی.
- اگر نیاز به منابع اشتراکی داشتی، حتماً thread-safe بودن آن‌ها را بررسی کن.

---

## ۵. مزایای این ساختار
- استفاده کامل از توان CPU
- مقیاس‌پذیری بالا برای ترافیک زیاد
- آماده برای توسعه‌های آینده (featureهای جدید، بهینه‌سازی و ...)

---

## ۶. نمونه کد کلیدی

### main.cpp
```cpp
#ifdef UDP2RAW_LINUX
    int num_workers = std::thread::hardware_concurrency();
    if(num_workers <= 0) num_workers = 2; // fallback
    int port = local_addr.get_port();
    start_server_workers(port, num_workers);
#endif
```

### server.cpp
```cpp
#ifdef UDP2RAW_LINUX
std::mutex global_init_mutex;
bool global_init_done = false;

void server_worker(int port, int worker_id) {
    // ...
    server_event_loop();
}

void start_server_workers(int port, int num_workers) {
    std::vector<std::thread> threads;
    for (int i = 0; i < num_workers; ++i) {
        threads.emplace_back(server_worker, port, i);
    }
    for (auto &t : threads) t.join();
}
#endif
```

---

## ۷. نتیجه‌گیری
این ساختار سرور udp2raw را برای بارهای سنگین و توسعه‌های آینده آماده می‌کند و به راحتی می‌توان آن را گسترش داد یا بهینه کرد.

---

**تهیه و تنظیم: تیم توسعه udp2raw (با کمک هوش مصنوعی)** 