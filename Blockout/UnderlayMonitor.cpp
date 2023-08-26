#include <windows.h>
#include <TlHelp32.h>
#include <oleacc.h>
#include <algorithm>
#include <vector>

#include "UnderlayMonitor.h"

namespace details
{
    /**
    * @Returns the area of a rectangle
    */
    int RectArea(const RECT a)
    {
        // Origin is top left
        const int right = max(a.right, a.left);
        const int left = min(a.right, a.left);
        const int top = min(a.top, a.bottom);
        const int bottom = max(a.top, a.bottom);

        return (right - left) * (bottom - top);
    }

    /**
    * @struct Target helps find target process window
    * @brieft Tracks process attributes and automatically cleans up handles
    */
    struct Target
    {

        Target(HWND overlayWindow) : pid(0),
            overlayWindow(overlayWindow),
            underlayWindow(0),
            underlayHandle(INVALID_HANDLE_VALUE),
            windowArea(0)
        {
        };

        ~Target()
        {
            if (underlayHandle != nullptr)
            {
                CloseHandle(underlayHandle);
            }
        }


        /**
        * @brief Relocate and resize overlay to match underlay
        */
        void UpdateOverlay()
        {
            UpdateOverlay(underlayWindow);
        }

        /**
        * @brief Relocate and resize overlay without overlapping focusWindow
        */
        void UpdateOverlay(HWND focusWindow)
        {
            RECT rect;
            GetWindowRect(underlayWindow, &rect);

            // 2x the caption - one each for the underlay and overlay
            const int captitionSizePixels = GetSystemMetrics(SM_CYCAPTION) * 2;
            int top = rect.top + captitionSizePixels;
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top - captitionSizePixels;

            // If either the over or underlay are focused, overlay must be topmost. Otherwise draw behind focused window.
            HWND after = (focusWindow == underlayWindow || focusWindow == overlayWindow) ?
                         HWND_TOPMOST : focusWindow;

            SetWindowPos(
                overlayWindow,      // hWnd
                after,               // hWndInsertAfter
                rect.left,          // X
                top,                // Y
                width,              // cx
                height,             // cy
                0                   // uFlags
            );
        }

        HWND overlayWindow;         ///< Do not close
        HWND underlayWindow;        ///< Do not close
        HANDLE underlayHandle;      ///< Must be closed
        DWORD pid;

        // TODO - this is only used for FindTarget, this should be tracked elsewhere
        int windowArea;             ///< Cached window area
    };

    std::unique_ptr<Target> g_target;

    /**
    * @brief Callback returns false once a window for the given pid is found
    * @param lparam Target* containing the pid to locate. Sets underlay field when found.
    *   If the process has multiple windows, the larget window is returned.
    * @returns false once found
    */
    BOOL CALLBACK FindTargetWindow(HWND hwnd, LPARAM lparam)
    {
        auto pTarget = reinterpret_cast<Target*>(lparam);
        DWORD lpdwProcessId;
        GetWindowThreadProcessId(hwnd, &lpdwProcessId);
        if (pTarget->pid == lpdwProcessId)
        {
            RECT rect;
            GetWindowRect(hwnd, &rect);
            const int area = RectArea(rect);

            if (area > pTarget->windowArea)
            {
                pTarget->underlayWindow = hwnd;
                pTarget->windowArea = area;
                return true;
            }
        }
        return true;
    }

    /**
    * @brief Find target process by name and return process id and handle
    * @param processName full name of process (e.g. notepad++.exe)
    * @param result receives target metadata
    * @returns True on success when all target metadata is found
    */
    bool FindTarget(const std::wstring processName, Target* result)
    {
        bool okay = false;
        DWORD pid = 0;
        HANDLE handle = nullptr;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        PROCESSENTRY32 process;
        ZeroMemory(&process, sizeof(process));
        process.dwSize = sizeof(process);

        if (Process32First(snapshot, &process))
        {
            do
            {
                if (std::wstring(process.szExeFile) == processName)
                {
                    pid = process.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &process));
        }

        CloseHandle(snapshot);

        if (pid != 0)
        {
            handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
            if (handle != INVALID_HANDLE_VALUE)
            {
                result->pid = pid;
                result->underlayHandle = handle;
                EnumWindows(&FindTargetWindow, reinterpret_cast<LPARAM>(result));

                okay = result->underlayWindow != nullptr;
            }
        }

        return okay;
    }

}; // namespace details

struct UnderlayMonitor::Impl
{
    Impl(HWND overlay);
    ~Impl();

    /**
    * @brief Create an underlay of this process
    */
    bool Connect(const std::wstring processName);

    /**
    * @brief Remove underlay monitor and cleanup
    */
    void Disconnect();

    std::wstring currentProcessName;    ///< Underlay process name, if any
    std::vector<HWINEVENTHOOK> hooks;   ///< Registered hooks, remember to unhook

    /**
    * @brief Move and resize overlay to match underlay
    * @see https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wineventproc
    */
    static void CALLBACK UnderlayChanged(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD dwEventThread,
        DWORD dwmsEventTime);
};

UnderlayMonitor::Impl::Impl(HWND overlay) :
    currentProcessName(L"")
{
    details::g_target = std::make_unique<details::Target>(overlay);
}

UnderlayMonitor::Impl::~Impl()
{
    this->Disconnect();
}


bool UnderlayMonitor::Impl::Connect(const std::wstring processName)
{
    if (!details::FindTarget(processName, details::g_target.get()))
    {
        return false;
    }

    // Set initial location and size
    details::g_target->UpdateOverlay();

    // Register callback to detect future underlay changes
    hooks.push_back(
        SetWinEventHook(
            EVENT_OBJECT_LOCATIONCHANGE,    // eventMin
            EVENT_OBJECT_LOCATIONCHANGE,    // eventMax
            NULL,                           // hmodWinWventProc
            &Impl::UnderlayChanged,         // pfnEventProc
            details::g_target->pid,         // idProcess
            0,                              // idThread,
            WINEVENT_OUTOFCONTEXT           // dwFlags
        )
    );

    // Register callback to detect focus changes
    hooks.push_back(
        SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,        // eventMin
            EVENT_SYSTEM_FOREGROUND,        // eventMax
            NULL,                           // hmodWinWventProc
            &Impl::UnderlayChanged,         // pfnEventProc
            0,                              // idProcess
            0,                              // idThread,
            WINEVENT_OUTOFCONTEXT           // dwFlags
        )
    );

    auto success = std::all_of(hooks.begin(), hooks.end(), [](HWINEVENTHOOK hook)
    {
        return hook != nullptr;
    });

    return success;
}

void UnderlayMonitor::Impl::Disconnect()
{
    for (auto i = 0; i < hooks.size(); ++i)
    {
        if (hooks.at(i) != nullptr)
        {
            UnhookWinEvent(hooks.at(i));
            hooks.at(i) = nullptr;
        }
    }
    hooks.clear();
}

void UnderlayMonitor::Impl::UnderlayChanged(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD dwEventThread,
    DWORD dwmsEventTime)
{
    // TODO - this should instead enqueue an event
    details::g_target->UpdateOverlay(hwnd);
}

UnderlayMonitor::UnderlayMonitor(HWND overlay)
{
    this->impl = std::make_unique<Impl>(overlay);
}

UnderlayMonitor::~UnderlayMonitor()
{
    StopMonitor();
}

bool UnderlayMonitor::StartMonitor(const std::wstring processName)
{
    return impl->Connect(processName);
}

void UnderlayMonitor::StopMonitor()
{
    impl->Disconnect();
}

std::wstring UnderlayMonitor::CurrentProcessName() const
{
    return impl->currentProcessName;
}
