# UDP2RAW Performance Profiler

This Discord-integrated profiling system provides real-time monitoring and performance analysis for the UDP2RAW application. It allows visualization of important metrics such as CPU usage, memory, network activity, and packet statistics through a Discord webhook.

## Features

- **Real-time monitoring**: Metrics updated every 5 seconds (configurable)
- **Discord visualization**: Reports formatted in Discord embeds
- **Comprehensive metrics**:
  - CPU usage
  - Memory consumption
  - Packet statistics (processed/dropped)
  - Network traffic (bytes sent/received)
  - Thread utilization
  - Function execution times
- **Trend graphs**: Visualization of historical trends
- **Low impact**: Designed to have minimal performance impact

## Requirements

- libcurl
- nlohmann/json

## Installation

### Windows

Run the dependencies installation script:

```
install_profiler_deps.bat
```

### Linux

Run the dependencies installation script:

```
chmod +x install_profiler_deps.sh
./install_profiler_deps.sh
```

## Configuration

The profiler is already configured to send data to the provided Discord webhook. If you need to change the webhook, modify the URL in `main.cpp`:

```cpp
Profiler::getInstance().initialize(
    "https://discord.com/api/webhooks/YOUR_WEBHOOK_URL",
    5000  // Reporting interval in milliseconds
);
```

## Usage

The profiler starts automatically when the application runs. No additional configuration is required.

To profile specific functions, use the `PROFILE_FUNCTION()` macro at the beginning of the function:

```cpp
void my_function() {
    PROFILE_FUNCTION();
    
    // Function code...
}
```

To record custom events:

```cpp
// Record network activity
Profiler::getInstance().recordNetworkActivity(bytes_sent, bytes_received);

// Record packet statistics
Profiler::getInstance().recordPacketStats(packets_processed, packets_dropped);

// Record custom metric
Profiler::getInstance().recordCustomMetric("name", "category", value, "units");
```

## Structure of Discord Reports

Reports include:

1. **General Information**:
   - Runtime
   - Date and time of collection

2. **System Metrics**:
   - CPU usage (%)
   - Memory consumption (MB)

3. **Network Statistics**:
   - Packets processed
   - Packets dropped (%)
   - Network traffic (MB)

4. **Thread Pool Status**:
   - Active/total threads
   - Utilization percentage

5. **Trend Graphs**:
   - Visualization of key metric trends

## Adaptation and Extension

The profiling system is designed to be extensible. You can add new metrics by implementing additional methods in the Profiler class or modify the display format in the `formatDiscordMessage()` function.

## Troubleshooting

- **Reports don't appear in Discord**: Verify that the webhook URL is correct and that you have permissions to post messages.
- **Compilation error**: Make sure you have installed all dependencies correctly.
- **High CPU usage**: Reduce the reporting frequency by increasing the reporting interval value. 