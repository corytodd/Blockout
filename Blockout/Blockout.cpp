// Blockout.cpp : Defines the entry point for the application.
//

#include "framework.h"

#include <memory>
#include <shellapi.h>

#include "Blockout.h"
#include "Hole.h"
#include "UnderlayMonitor.h"

#define MAX_LOADSTRING 100
#define ALPHA_PERCENT 92
#define BLOCKOUT_COLOR RGB(0, 0, 0)

// Convert message pump event to relative point inside window
#define TRUE_POINT(lParam) { LOWORD(lParam) , HIWORD(lParam) + GetSystemMetrics(SM_CYCAPTION) }

struct Args {
    std::wstring targetProcessName;
};

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
Args args;                                      // command line args
std::unique_ptr<Hole> pHole;                    // window hole instance
std::unique_ptr<UnderlayMonitor> pMonitor;      // target process monitor

// Forward declarations of functions included in this code module:
ATOM                RegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                ParseArgs();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    ParseArgs();

    pHole = std::make_unique<Hole>();
    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_BLOCKOUT, szWindowClass, MAX_LOADSTRING);
    RegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, SW_MAXIMIZE))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_BLOCKOUT));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
ATOM RegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLOCKOUT));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(BLOCKOUT_COLOR);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_BLOCKOUT);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_BLOCKOUT));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED,                          // dwExStyle
        szWindowClass,                          // lpClassName
        szTitle,                                // lpWindowName
        WS_TILEDWINDOW |
        WS_MAXIMIZE,                            // dwStyle
        CW_USEDEFAULT,                          // x
        0,                                      // y
        CW_USEDEFAULT,                          // nWidth
        0,                                      // nHeight
        nullptr,                                // hWndParent
        nullptr,                                // hMenu
        hInstance,                              // hInstance      
        nullptr                                 // lpParam
    );

    if (!hWnd)
    {
        return FALSE;
    }

    SetLayeredWindowAttributes(hWnd, 0, (255 * ALPHA_PERCENT) / 100, LWA_ALPHA);

    ShowWindow(hWnd, nCmdShow | nCmdShow);
    UpdateWindow(hWnd);

    if (!args.targetProcessName.empty()) {
        pMonitor = std::make_unique<UnderlayMonitor>(hWnd);
        pMonitor->StartMonitor(args.targetProcessName);

        // TODO set target app name in title bar?
    }

    return TRUE;
}

//
// Function ParseArgs()
void ParseArgs()
{
    LPWSTR* szArglist;
    int nArgs;

    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist != NULL && nArgs > 1)
    {
        args.targetProcessName = szArglist[1];
    }
    LocalFree(szArglist);
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        pHole->DrawHole(hWnd);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_LBUTTONDOWN:
        pHole->Start(hWnd, TRUE_POINT(lParam));
        return 1;
    case WM_LBUTTONUP:
        pHole->End(hWnd, TRUE_POINT(lParam));
        return 1;
    case WM_MOUSEMOVE:
        pHole->Drag(hWnd, TRUE_POINT(lParam));
        return 0;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

