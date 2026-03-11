#ifndef INACTIVITY_MONITOR_H
#define INACTIVITY_MONITOR_H

#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <iostream>

class InactivityMonitor {
public:
    using Callback = std::function<void()>;

    explicit InactivityMonitor(int inactivitySeconds = 300);
    ~InactivityMonitor();

    void Start();
    void Stop();
    void ResetTimer();
    void SetCallback(Callback cb);
    void SetInactivityDuration(int seconds);
    int GetSecondsUntilTrigger() const;
    int GetInactivityDuration() const;
    bool IsRunning() const;

private:
    void MonitorLoop();

private:
    std::atomic<bool> m_running;
    std::atomic<bool> m_shouldStop;
    std::chrono::steady_clock::time_point m_lastActivity;
    int m_inactivitySeconds;

    Callback m_callback;
    std::thread m_monitorThread;
    mutable std::mutex m_mutex;
};

#endif // INACTIVITY_MONITOR_H
