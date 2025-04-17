#include "batch_processor.h"
#include "network.h"
#include "log.h"

// Global batch processor instance
std::unique_ptr<BatchProcessor> g_packet_batch_processor;

// Initialize the global batch processor
void init_batch_processor(
    size_t maxBatchSize,
    std::chrono::milliseconds maxDelay,
    size_t numWorkerThreads
) {
    g_packet_batch_processor = std::make_unique<BatchProcessor>(
        maxBatchSize, maxDelay, numWorkerThreads
    );
    
    // Start the batch processor with a processing function
    g_packet_batch_processor->start([](const PacketBatch& batch) {
        // Process each packet in the batch
        for (size_t i = 0; i < batch.size(); ++i) {
            const auto& buffer = batch.packets[i];
            int length = batch.packet_lengths[i];
            const auto& raw_info = batch.raw_infos[i];
            
            // Process based on protocol
            int ret = 0;
            switch(raw_info.send_info.protocol) {
                case IPPROTO_TCP:
                    ret = send_raw_tcp(const_cast<raw_info_t&>(raw_info), buffer->data(), length);
                    break;
                case IPPROTO_UDP:
                    ret = send_raw_udp(const_cast<raw_info_t&>(raw_info), buffer->data(), length);
                    break;
                case IPPROTO_ICMP:
                    ret = send_raw_icmp(const_cast<raw_info_t&>(raw_info), buffer->data(), length);
                    break;
                default:
                    mylog(log_warn, "Unknown protocol: %d\n", raw_info.send_info.protocol);
                    ret = -1;
            }
            
            if (ret < 0) {
                mylog(log_debug, "send_raw_* error for packet %zu in batch of %zu\n", i, batch.size());
            }
        }
        
        mylog(log_trace, "Processed batch of %zu packets\n", batch.size());
    });
    
    mylog(log_info, "Initialized batch processor with %zu worker threads, max batch size: %zu, max delay: %lld ms\n", 
          numWorkerThreads, maxBatchSize, maxDelay.count());
} 