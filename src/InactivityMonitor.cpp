#include "InactivityMonitor.h"

InactivityMonitor::InactivityMonitor(int inactivitySeconds)
    : m_running(false),
      m_shouldStop(false),
      m_lastActivity(std::chrono::steady_clock::now()),
      m_inactivitySeconds(inactivitySeconds) {}

InactivityMonitor::~InactivityMonitor() {
    Stop();
}

void InactivityMonitor::Start() {
    if (m_running) return;
    m_running = true;
    m_shouldStop = false;
    m_lastActivity = std::chrono::steady_clock::now();
    m_monitorThread = std::thread(&InactivityMonitor::MonitorLoop, this);
}

void InactivityMonitor::Stop() {
    m_shouldStop = true;
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    m_running = false;
}

void InactivityMonitor::ResetTimer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastActivity = std::chrono::steady_clock::now();
}

void InactivityMonitor::SetCallback(Callback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(cb);
}

void InactivityMonitor::SetInactivityDuration(int seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inactivitySeconds = seconds;
    m_lastActivity = std::chrono::steady_clock::now();
}

int InactivityMonitor::GetSecondsUntilTrigger() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastActivity).count();
    int remaining = m_inactivitySeconds - static_cast<int>(elapsed);
    return (remaining > 0) ? remaining : 0;
}

int InactivityMonitor::GetInactivityDuration() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inactivitySeconds;
}

bool InactivityMonitor::IsRunning() const {
    return m_running;
}

void InactivityMonitor::MonitorLoop() {
    while (!m_shouldStop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        Callback cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_callback || m_inactivitySeconds <= 0) continue;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastActivity).count();

            if (elapsed >= m_inactivitySeconds) {
                cb = m_callback;
                // Reset timer slightly to prevent tight-loop triggering before user responds
                m_lastActivity = now - std::chrono::seconds(m_inactivitySeconds - 5);
            }
        }

        if (cb) {
            try {
                cb();
            } catch (...) {
                std::cerr << "[InactivityMonitor] Error in callback." << std::endl;
            }
        }
    }
}
