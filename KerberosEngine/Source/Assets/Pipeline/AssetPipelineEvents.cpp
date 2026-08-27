#include "AssetPipelineEvents.hpp"

#include <ranges>

namespace Kerberos {

AssetEventDebouncer::AssetEventDebouncer(const std::chrono::milliseconds delay) 
    : m_Delay(delay) 
{}

AssetEventDebouncer::~AssetEventDebouncer() 
{ 
    Stop(); 
}

void AssetEventDebouncer::Start()
{
    if (!m_Worker.joinable())
        m_Worker = std::jthread([this](const std::stop_token& t) { Run(t); });
}

void AssetEventDebouncer::SetCallback(Callback callback)
{ 
    std::scoped_lock _(m_Mutex); 
    m_Callback = std::move(callback); 
}

void AssetEventDebouncer::Push(AssetFileEvent event) 
{ 
    std::scoped_lock _(m_Mutex);
    m_Pending[event.Path] = std::move(event);
    m_Condition.notify_one(); 
}

void AssetEventDebouncer::Stop() 
{ 
    if (m_Worker.joinable()) 
    { 
        m_Worker.request_stop(); m_Condition.notify_all(); m_Worker.join(); 
    } 
}

void AssetEventDebouncer::Run(const std::stop_token& token)
{
    while (!token.stop_requested())
    {
        std::unique_lock lock(m_Mutex);
        m_Condition.wait(lock, token, [this]{ return !m_Pending.empty(); });

        if (token.stop_requested()) 
            break;

        m_Condition.wait_for(lock, m_Delay);

        if (token.stop_requested())
            break;

        auto pending = std::move(m_Pending); 
        m_Pending.clear(); 
        auto callback = m_Callback;

        lock.unlock();

        if (callback) {
            for (const auto& event : pending | std::views::values) {
                callback(event);
            }
        }
    }
}

}
