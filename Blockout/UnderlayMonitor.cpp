#include <windows.h>
#include <TlHelp32.h>
#include <oleacc.h>

#include "UnderlayMonitor.h"

namespace details 
{
    // HACK - how to pass state into hwnd callback?
    static HWND g_overlay;

    struct Target 
    {
        DWORD pid;
        HANDLE handle;
    };

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
        }

        result->pid = pid;
        result->handle = nullptr;

        return result;
    }

}; // namespace details

struct UnderlayMonitor::Impl 
{
    Impl(HWND overlay);
    ~Impl() = default;

    /**
    * @brief Create an underlay of this process
    */
    bool Connect(const std::wstring processName);

    /**
    * @brief Remove underlay monitor and cleanup
    */
    void Disconnect();

    std::wstring currentProcessName;    ///< Underlay process name, if any
    HANDLE currentHandle;               ///< Handle for current process, if any
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

};

UnderlayMonitor::Impl::Impl(HWND overlay) :
    currentProcessName(L""),
    currentHandle(nullptr),
    hook(nullptr) {

    details::g_overlay = overlay;
}


bool UnderlayMonitor::Impl::Connect(const std::wstring processName) 
{
    details::Target target;
    if (!details::FindTarget(processName, &target)) 
    {
        return false;
    }

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
