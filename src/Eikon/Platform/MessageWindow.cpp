#include "pch.h"
#include "Platform/MessageWindow.h"

#pragma comment(lib, "user32.lib")

namespace Eikon
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"EikonMessageWindow";
    }

    MessageWindow::MessageWindow()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        static const bool registered = [instance] {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &MessageWindow::WndProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kClassName;
            if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                OutputDebugStringW(L"Eikon: RegisterClassExW(EikonMessageWindow) failed\n");
                return false;
            }
            return true;
        }();
        (void)registered;

        // A hidden top-level popup (never shown) rather than HWND_MESSAGE so that it receives
        // broadcasts such as TaskbarCreated and WM_SETTINGCHANGE.
        const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Eikon", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!hwnd)
        {
            OutputDebugStringW(L"Eikon: creating the message window failed\n");
        }
        // m_hwnd is assigned in WM_NCCREATE.
    }

    MessageWindow::~MessageWindow()
    {
        m_handlers.clear();
        if (m_hwnd)
        {
            const HWND hwnd = m_hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            m_hwnd = nullptr;
            DestroyWindow(hwnd);
        }
    }

    std::uint64_t MessageWindow::AddHandler(Handler handler)
    {
        const std::uint64_t token = ++m_lastToken;
        m_handlers.emplace_back(token, std::move(handler));
        return token;
    }

    void MessageWindow::RemoveHandler(std::uint64_t token)
    {
        std::erase_if(m_handlers, [token](auto const& entry) { return entry.first == token; });
    }

    LRESULT CALLBACK MessageWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        try
        {
            if (message == WM_NCCREATE)
            {
                auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
                auto* self = static_cast<MessageWindow*>(create->lpCreateParams);
                if (self)
                {
                    self->m_hwnd = hwnd;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }

            auto* self = reinterpret_cast<MessageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (message == WM_NCDESTROY)
            {
                if (self)
                {
                    self->m_hwnd = nullptr;
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }

            if (self && !self->m_handlers.empty())
            {
                // Copy: handlers may add/remove handlers (or services may be torn down) while dispatching.
                std::vector<std::pair<std::uint64_t, Handler>> handlers = self->m_handlers;
                for (auto const& [token, handler] : handlers)
                {
                    // The window (or this handler) may have gone away during an earlier handler.
                    self = reinterpret_cast<MessageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                    if (!self)
                    {
                        break;
                    }
                    const std::uint64_t current = token;
                    const bool stillRegistered = std::any_of(self->m_handlers.begin(), self->m_handlers.end(),
                        [current](auto const& entry) { return entry.first == current; });
                    if (!handler || !stillRegistered)
                    {
                        continue;
                    }
                    try
                    {
                        if (auto result = handler(message, wParam, lParam))
                        {
                            return *result;
                        }
                    }
                    catch (...)
                    {
                        OutputDebugStringW(L"Eikon: exception in a message window handler\n");
                    }
                }
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: exception in MessageWindow::WndProc\n");
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
