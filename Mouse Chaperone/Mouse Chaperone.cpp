/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <windows.h>
#undef max
#include <conio.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <condition_variable>

#define VERBOSE

HHOOK Mouse_Hook;

POINT Last_Pos;
POINT Teleport_Pos;
std::condition_variable Teleport_Cv;
std::mutex Teleport_Mx;

LRESULT CALLBACK MouseHookProc(
  int nCode, WPARAM wParam, LPARAM lParam
) {
    PKBDLLHOOKSTRUCT k = (PKBDLLHOOKSTRUCT)(lParam);
    POINT p;

    static bool teleported = false;
    bool restore = false;
    static const int range = 100;
    static int teleport_ignore_ticks = 5;
    
    switch (wParam) {
      default:
        break;
      case WM_LBUTTONDOWN:
        ::GetCursorPos(&p);

          // If we teleport and click we interpret this as a
          // touch event; now any mouseup will telport the
          // cursor back and we forget about the ignore
        if (teleported) {
            ::SetConsoleTitleW(L"Mouse Chaperone ☟");
            teleport_ignore_ticks = std::numeric_limits<int>::max();
        }
        break;

      case WM_MOUSEMOVE:
        ::GetCursorPos(&p);

          // If we teleported but are not clicking we may be
          // responding to a drawing tablet and do not want to
          // teleport back upon mouse release; if we get enough
          // mouse move events we just forget the teleport
        if (teleported) {
            if (teleport_ignore_ticks-- > 0)
              break;
            Last_Pos = p;
            teleported = false;
          #ifdef VERBOSE
            std::cout << "Teleport was not due to touch" << std::endl;
          #endif
        }

          // If we are not teleporting and stay in range we
          // remember our last cursor position; otherwise we
          // set ourselves to the teleportation state
        if ((std::abs(Last_Pos.x - p.x) + std::abs(Last_Pos.y - p.y)) < range) {
            Last_Pos = p;
            break;
        }
        
      #ifdef VERBOSE
        std::cout << "Teleported to " << p.x << " " << p.y << std::endl;
      #endif

        teleported = true;
        teleport_ignore_ticks = 5;
        break;

      case WM_LBUTTONUP:
          // Finger release is a left mouse button up; when it is fired
          // and we remember this was a teleport we can begin the restoration
        restore = teleported;
        break;
    };

      // If we restore we notify the main thread which can
      // build in a little delay; this is needed as the Windows
      // task bar expects a little more time
    if (restore) {
        ::SetConsoleTitleA("Mouse Chaperone");
        std::unique_lock<std::mutex> lk(Teleport_Mx);
        Teleport_Pos = Last_Pos;
        lk.unlock();
        Teleport_Cv.notify_one();

        teleported = false;
    }

      // Let other hooks be handled
    return CallNextHookEx(Mouse_Hook, nCode, wParam, lParam);
}

int main(
) {
    using namespace std::chrono_literals;

    std::cout << "*-*-* Mouse Chaperone *-*-*" << std::endl;

    Teleport_Pos.x = std::numeric_limits<long>::max();
    Teleport_Pos.y = 0;

      // The proc thread blocks the entire windows mouse handling;
      // we run it as a separate thread to be as transparent as possible
    std::thread proc([&]{
        ::GetCursorPos(&Last_Pos);

        Mouse_Hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, 0, 0);

        MSG msg;
        while (GetMessage(&msg, 0, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        UnhookWindowsHookEx(Mouse_Hook);
    });

    while (true) {
        std::unique_lock<std::mutex> lk(Teleport_Mx);
        Teleport_Cv.wait(lk, []{ return Teleport_Pos.x != std::numeric_limits<long>::max(); });

          // Give processes a little time to handle the mouse
        std::this_thread::sleep_for(100ms);
        
      #ifdef VERBOSE
        std::cout << "Restored to " << Teleport_Pos.x << " " << Teleport_Pos.y << std::endl;
      #endif

        ::SetCursorPos(Teleport_Pos.x, Teleport_Pos.y);
        Teleport_Pos.x = std::numeric_limits<long>::max();
    }

    return 0;
}