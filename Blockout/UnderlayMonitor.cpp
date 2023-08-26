#include <windows.h>
#include <TlHelp32.h>
#include <oleacc.h>

#include "UnderlayMonitor.h"

namespace details 
{
    // HACK - how to pass state into hwnd callback?
    // Do not close this HWND, we do not own it
    static HWND g_overlay;

    struct Target 
    {

        Target() : pid(0),
            underlayHandle(INVALID_HANDLE_VALUE), 
            underlayWindow(0),
            windowArea(0)
        {};
        
        ~Target() {
            if (underlayHandle != nullptr) {
                CloseHandle(underlayHandle);
            }
        }

        DWORD pid;
        HANDLE underlayHandle;      ///< Must be closed
        HWND underlayWindow;        ///< Do not close
        int windowArea;             ///< Cached window area
    };

    /**
    * @Returns the area of a rectangle
    */
    int RectArea(RECT a)
    {
        // Origin is top left
        const int right = max(a.right, a.left);
        const int left = min(a.right, a.left);
        const int top = min(a.top, a.bottom);
        const int bottom = max(a.top, a.bottom);

        return (right - left) * (bottom - top);
    }

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

            if (area > pTarget->windowArea) {
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
    * @param result receives pid and handle. Both set to 0 on error.
    */
    bool FindTarget(const std::wstring processName, Target* result) 
    {
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

            result->pid = pid;
            result->underlayHandle = handle;
            EnumWindows(&FindTargetWindow, reinterpret_cast<LPARAM>(result));
        }

        return result;
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
    details::Target target;             ///< Underlay target
    HWINEVENTHOOK hook;                 ///< Registered hook, remember to unhook

    /**
    * @brief Move and resize overlay to match underlay
    * @see https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wineventproc
    */
    static void CALLBACK callback(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD dwEventThread,
        DWORD dwmsEventTime);

    /**
    * @brief Update overlay to match underlay
    * @param rect underlay window location relative to screen
    */
    static void UpdateOverlay(RECT rect);
};

UnderlayMonitor::Impl::Impl(HWND overlay) :
    currentProcessName(L""),
    target(),
    hook(nullptr) {

    details::g_overlay = overlay;
}

UnderlayMonitor::Impl::~Impl() {
    this->Disconnect();
}


bool UnderlayMonitor::Impl::Connect(const std::wstring processName) 
{
    details::Target target;
    if (!details::FindTarget(processName, &target)) 
    {
        return false;
    }
    if (target.underlayWindow == nullptr) 
    {
        return false;
    }

    // Set initial location and size
    RECT rect;
    GetWindowRect(target.underlayWindow, &rect);
    Impl::UpdateOverlay(rect);

    // Register callback to detect future underlay changes
    hook = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,    // eventMin
        EVENT_OBJECT_LOCATIONCHANGE,    // eventMax
        NULL,                           // hmodWinWventProc
        Impl::callback,                 // pfnEventProc
        target.pid,                     // idProcess
        0,                              // idThread,
        WINEVENT_OUTOFCONTEXT           // dwFlags
    );

    return this->hook != nullptr;
}

void UnderlayMonitor::Impl::Disconnect()
{
    if (this->hook != nullptr)
    {
        UnhookWinEvent(this->hook);
        this->hook = nullptr;
    }
}

void UnderlayMonitor::Impl::UpdateOverlay(RECT rect)
{
    // HACK - leave underlay title bar exposed but let's be dynamic please
    const int title = 50;
    int top = rect.top + title;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top - title;

    SetWindowPos(
        details::g_overlay, // hWnd
        0,                  // hWndInsertAfter
        rect.left,          // X
        top,                // Y
        width,              // cx
        height,             // cy
        0                   // uFlags
    );
}

void UnderlayMonitor::Impl::callback(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD dwEventThread,
    DWORD dwmsEventTime)
{

    RECT rect;
    if (hwnd)
    {
        GetWindowRect(hwnd, &rect);
        Impl::UpdateOverlay(rect);
    }
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
