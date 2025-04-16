 - 
+ # Multi-Core Support for udp2raw
+ 
+ This version of udp2raw has been enhanced with multi-core processing capability to improve scalability and performance on systems with multiple CPU cores.
+ 
+ ## Features
+ 
+ - Utilizes all available CPU cores by default
+ - Distributes network processing workload across multiple threads
+ - Handles connections concurrently for improved throughput
+ - Fully backward compatible with the original codebase
+ 
+ ## Usage
+ 
+ The program will automatically detect and use all available CPU cores. You can also specify the number of threads to use with the `--threads` command-line parameter:
+ 
+ ```
+ ./udp2raw --threads 4 [other parameters]
+ ```
+ 
+ ## Implementation Details
+ 
+ The multi-core support is implemented using a thread pool that distributes work across multiple worker threads:
+ 
+ 1. Each worker thread has its own event loop for processing connections
+ 2. The main thread acts as a dispatcher, distributing incoming packets to worker threads
+ 3. Thread-safe mechanisms (mutex locks) protect shared resources
+ 4. Event processing is distributed randomly across worker threads for load balancing
+ 
+ ## Performance Considerations
+ 
+ - The optimal number of threads is typically equal to the number of CPU cores in your system
+ - For very high packet rates, using slightly more threads than cores can improve performance
+ - For low-latency applications, using exactly one thread per core may provide better results
+ 
+ ## Building
+ 
+ To build with multi-core support, use one of the following methods:
+ 
+ ### Using CMake
+ 
+ ```
+ mkdir build
+ cd build
+ cmake ..
+ make
+ ```
+ 
+ ### Using the provided Makefile
+ 
+ ```
+ make
+ ```
+ 
+ ## Troubleshooting
+ 
+ If you encounter any issues with the multi-core functionality:
+ 
+ 1. Try setting the number of threads to 1 to disable multi-threading
+ 2. Check system resource usage to ensure threads are being properly utilized
+ 3. Verify that your system supports the C++11 threading features required
+ 
+ ## License
+ 
+ Same as the original udp2raw project.
```</function_results>

Now, let's build the project: