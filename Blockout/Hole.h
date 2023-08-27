#pragma once
#include <Windows.h>

//
// Defines a transparent hole in a window
//
// A hole uses standard rectangle coordinates:
//   x1, y1: upper left point
//   x2, y2: lower right point
//
//   Remember that origin is at the top left of the screen
//
class Hole
{
  public:
    Hole() : _start({0, 0}), _end({0, 0}), _cur({0, 0}), _isUpdating(false), _rect({0, 0, 0, 0})
    {
    }

    ~Hole()
    {
    }

    //
    // Start defining a hole
    void Start(HWND hWnd, POINT pt)
    {
        _start = _cur = pt;
        _isUpdating = true;

        _rect.left = _cur.x;
        _rect.right = _cur.x;
        _rect.top = _cur.y;
        _rect.bottom = _cur.y;

        InvalidateRect(hWnd, NULL, true);
    }

    //
    // Extend all bounds of current hole if update in progress
    void Drag(HWND hWnd, POINT pt)
    {
        if (_isUpdating)
        {
            _cur = pt;

            _rect.left = min(_cur.x, _start.x);
            _rect.right = max(_cur.x, _start.x);
            _rect.top = min(_cur.y, _start.y);
            _rect.bottom = max(_cur.y, _start.y);

            InvalidateRect(hWnd, NULL, true);
        }
    }

    //
    // End definition of hole
    void End(HWND hWnd, POINT pt)
    {
        if (_isUpdating)
        {
            _end = pt;
            _isUpdating = false;

            _rect.left = min(_end.x, _start.x);
            _rect.right = max(_end.x, _start.x);
            _rect.top = min(_end.y, _start.y);
            _rect.bottom = max(_end.y, _start.y);

            InvalidateRect(hWnd, NULL, true);
        }
    }

    //
    // Draw hole in hWnd
    void DrawHole(HWND hWnd)
    {
        HRGN windowRgn;
        HRGN holeRgn;
        RECT windowRect;
        LONG windowWidth, windowHeight;

        GetWindowRect(hWnd, &windowRect);
        windowWidth = windowRect.right - windowRect.left;
        windowHeight = windowRect.bottom - windowRect.top;
        windowRgn = CreateRectRgn(0, 0, windowWidth, windowHeight);

        holeRgn = CreateRectRgn(_rect.left, _rect.top, _rect.right, _rect.bottom);

        CombineRgn(windowRgn, windowRgn, holeRgn, RGN_DIFF);
        SetWindowRgn(hWnd, windowRgn, TRUE);
    }

  private:
    POINT _start;
    POINT _end;
    POINT _cur;
    RECT _rect;
    BOOL _isUpdating;
};
