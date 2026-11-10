#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Eikon
{
    // Minimal single-threaded multicast event. All Lucida services raise events on the UI thread.
    template <typename... Args>
    class Event
    {
    public:
        using Handler = std::function<void(Args...)>;
        using Token = std::uint64_t;

        Token Add(Handler handler)
        {
            const Token token = ++m_lastToken;
            m_handlers.emplace_back(token, std::move(handler));
            return token;
        }

        void Remove(Token token)
        {
            std::erase_if(m_handlers, [token](auto const& entry) { return entry.first == token; });
        }

        void operator()(Args... args) const
        {
            // Copy so handlers may add/remove handlers while being invoked.
            auto handlers = m_handlers;
            for (auto const& [token, handler] : handlers)
            {
                handler(args...);
            }
        }

    private:
        std::vector<std::pair<Token, Handler>> m_handlers;
        Token m_lastToken = 0;
    };
}
