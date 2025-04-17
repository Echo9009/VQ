#include "lock_free_queue.h"

// Define the thread_local variable here
thread_local int ThreadQueueManager::thread_id = -1; 