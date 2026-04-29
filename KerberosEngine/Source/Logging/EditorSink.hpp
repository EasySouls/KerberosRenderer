#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>
#include <vector>
#include <string>

namespace Kerberos
{
    struct ConsoleMessage
    {
        spdlog::level::level_enum Level;
        std::string Text;
    };

    template<typename Mutex>
    class EditorSink : public spdlog::sinks::base_sink<Mutex>
    {
    public:
        const size_t maxMessages = 1000;
        std::vector<ConsoleMessage> Messages;

        std::mutex& GetMutex() { return this->mutex_; }

        void Clear() 
        {
            std::lock_guard<Mutex> lock(this->mutex_);
            Messages.clear();
        }

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override 
        {
            spdlog::memory_buf_t formatted;
            this->formatter_->format(msg, formatted);

            Messages.push_back({ msg.level, fmt::to_string(formatted) });

            if (Messages.size() > maxMessages) {
                Messages.erase(Messages.begin());
            }
        }

        void flush_() override 
        {
            // Nothing to flush for a memory buffer
        }
    };

    using EditorSinkMultithreaded = EditorSink<std::mutex>;
}