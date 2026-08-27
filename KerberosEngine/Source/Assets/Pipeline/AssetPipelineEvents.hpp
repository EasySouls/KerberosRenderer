#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Kerberos {

enum class AssetFileEventType : uint8_t
{ 
    Added, 
    Modified, 
    Removed, 
    Renamed
};

struct AssetFileEvent 
{ 
    AssetFileEventType Type; 
    std::filesystem::path Path; 
    std::filesystem::path OldPath;
};

class AssetEventDebouncer
{
public:
    using Callback = std::function<void(const AssetFileEvent&)>;

    explicit AssetEventDebouncer(std::chrono::milliseconds delay = std::chrono::milliseconds(100));
    ~AssetEventDebouncer();

    void Start();
    void SetCallback(Callback callback);
    void Push(AssetFileEvent event);
    void Stop();

private:
    void Run(const std::stop_token& token);

private:
    std::chrono::milliseconds m_Delay;
    Callback m_Callback;
    std::mutex m_Mutex;
    std::condition_variable_any m_Condition;
    std::unordered_map<std::filesystem::path, AssetFileEvent> m_Pending;
    std::jthread m_Worker{};
};

}
