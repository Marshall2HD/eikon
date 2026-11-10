#include "pch.h"
#include "Platform/HotKeyService.h"

#pragma comment(lib, "user32.lib")

namespace Eikon
{
    namespace
    {
        UINT ToHotKeyModifiers(Modifiers modifiers) noexcept
        {
            UINT result = 0;
            if (HasFlag(modifiers, Modifiers::Ctrl))
            {
                result |= MOD_CONTROL;
            }
            if (HasFlag(modifiers, Modifiers::Alt))
            {
                result |= MOD_ALT;
            }
            if (HasFlag(modifiers, Modifiers::Shift))
            {
                result |= MOD_SHIFT;
            }
            if (HasFlag(modifiers, Modifiers::Win))
            {
                result |= MOD_WIN;
            }
            return result;
        }
    }

    HotKeyService::HotKeyService(MessageWindow& window) : m_window(window)
    {
        m_handlerToken = m_window.AddHandler([this](UINT message, WPARAM wParam, LPARAM) -> std::optional<LRESULT> {
            if (message != WM_HOTKEY)
            {
                return std::nullopt;
            }
            const int id = static_cast<int>(wParam);
            auto it = m_callbacks.find(id);
            if (it == m_callbacks.end())
            {
                return std::nullopt;
            }
            auto callback = it->second; // copy: the callback may re-register hot keys
            try
            {
                if (callback)
                {
                    callback();
                }
            }
            catch (...)
            {
                OutputDebugStringW(L"Eikon: exception in a hot key callback\n");
            }
            return LRESULT{ 0 };
        });
    }

    HotKeyService::~HotKeyService()
    {
        for (auto const& [id, callback] : m_callbacks)
        {
            UnregisterHotKey(m_window.Handle(), id);
        }
        m_callbacks.clear();
        m_window.RemoveHandler(m_handlerToken);
    }

    bool HotKeyService::Register(int id, KeyShortcut const& shortcut, std::function<void()> callback)
    {
        Unregister(id);
        if (shortcut.IsEmpty())
        {
            return true;
        }
        const UINT modifiers = ToHotKeyModifiers(shortcut.modifiers) | MOD_NOREPEAT;
        if (!RegisterHotKey(m_window.Handle(), id, modifiers, shortcut.key))
        {
            OutputDebugStringW(L"Eikon: RegisterHotKey failed\n");
            return false;
        }
        m_callbacks[id] = std::move(callback);
        return true;
    }

    void HotKeyService::Unregister(int id)
    {
        if (m_callbacks.erase(id) > 0)
        {
            UnregisterHotKey(m_window.Handle(), id);
        }
    }
}
