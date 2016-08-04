// main.cpp
//
// Windows front-end for The Great Escape
//
// Copyright (c) David Thomas, 2016. <dave@davespace.co.uk>
//

#include <stdio.h>

#include <windows.h>

#include "ZXSpectrum/Spectrum.h"
#include "ZXSpectrum/Keyboard.h"

#include "TheGreatEscape/TheGreatEscape.h"

///////////////////////////////////////////////////////////////////////////////

// these will need to be passed in rather than hard-coded
#define WIDTH  256
#define HEIGHT 192

///////////////////////////////////////////////////////////////////////////////

static const WCHAR szGameWindowClassName[] = L"TheGreatEscapeWindowsApp";
static const WCHAR szGameWindowTitle[]     = L"The Great Escape";

///////////////////////////////////////////////////////////////////////////////

typedef struct gamewin
{
  HINSTANCE     instance;
  HWND          window;
  BITMAPINFO    bitmapinfo;

  zxspectrum_t *zx;
  tgestate_t   *game;

  HANDLE        thread;
  DWORD         threadId;

  zxkeyset_t    keys;
  unsigned int *pixels;
}
gamewin_t;

///////////////////////////////////////////////////////////////////////////////

static void draw_handler(unsigned int *pixels, void *opaque)
{
  gamewin_t *gamewin = (gamewin_t *) opaque;
  RECT       rect;

  gamewin->pixels = pixels;

  rect.left   = 0;
  rect.top    = 0;
  rect.right  = 65536; // FIXME: Find a symbol for whole window dimension.
  rect.bottom = 65536;

  // kick the window
  InvalidateRect(gamewin->window, &rect, FALSE);
}

static void sleep_handler(int duration, sleeptype_t sleeptype, void *opaque)
{
  Sleep(duration / 1000); // duration is taken literally for now
}

static int key_handler(uint16_t port, void *opaque)
{
  gamewin_t *gamewin = (gamewin_t *) opaque;

  return zxkeyset_for_port(port, gamewin->keys);
}

///////////////////////////////////////////////////////////////////////////////

static DWORD WINAPI gamewin_thread(LPVOID lpParam)
{
  tgestate_t *game = (tgestate_t *) lpParam;

  tge_setup(game);

  // we arrive here once the menu screen has run its course

  for (;;)
    tge_main(game);

  return NULL;
}

static int CreateGame(gamewin_t *gamewin)
{
  static const tgeconfig_t tgeconfig =
  {
    WIDTH  / 8,
    HEIGHT / 8
  };

  zxconfig_t        zxconfig;
  zxspectrum_tw    *zx;
  tgestate_t       *tge;
  HANDLE            thread;
  DWORD             threadId;
  BITMAPINFOHEADER *bmih;

  zxconfig.opaque = gamewin;
  zxconfig.draw   = draw_handler;
  zxconfig.sleep  = sleep_handler;
  zxconfig.key    = key_handler;

  zx = zxspectrum_create(&zxconfig);
  if (zx == NULL)
    goto failure;

  tge = tge_create(zx, &tgeconfig);
  if (tge == NULL)
    goto failure;

  thread = CreateThread(NULL,           // default security attributes
                        0,              // use default stack size
                        gamewin_thread, // thread function name
                        tge,            // argument to thread function
                        0,              // use default creation flags
                        &threadId);     // returns the thread identifier
  if (thread == NULL)
    goto failure;

  bmih = &gamewin->bitmapinfo.bmiHeader;
  bmih->biSize          = sizeof(BITMAPINFOHEADER);
  bmih->biWidth         = WIDTH;
  bmih->biHeight        = -HEIGHT; // negative height flips the image
  bmih->biPlanes        = 1;
  bmih->biBitCount      = 32;
  bmih->biCompression   = BI_RGB;
  bmih->biSizeImage     = 0; // set to zero for BI_RGB bitmaps
  bmih->biXPelsPerMeter = 0;
  bmih->biYPelsPerMeter = 0;
  bmih->biClrUsed       = 0;
  bmih->biClrImportant  = 0;

  gamewin->zx       = zx;
  gamewin->game     = tge;

  gamewin->thread   = thread;
  gamewin->threadId = threadId;

  gamewin->keys     = 0;
  gamewin->pixels   = NULL;

  return 0;


failure:
  return 1;
}

static void DestroyGame(gamewin_t *doomed)
{
  TerminateThread(doomed->thread, 0);

  tge_destroy(doomed->game);
  zxspectrum_destroy(doomed->zx);
}

///////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK GameWindowProcedure(HWND   hwnd,
                                     UINT   message,
                                     WPARAM wParam,
                                     LPARAM lParam)
{
  gamewin_t *gamewin;

  gamewin = (gamewin_t *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (message)
  {
    case WM_CREATE:
      {
        CREATESTRUCT *pCreateStruct = reinterpret_cast<CREATESTRUCT *>(lParam);
        gamewin = reinterpret_cast<gamewin_t *>(pCreateStruct->lpCreateParams);

        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) gamewin);

        /////////

        int rc;

        rc = CreateGame(gamewin);
        // if (rc)
        //   error;
      }
      break;

    case WM_DESTROY:
      DestroyGame(gamewin);
      PostQuitMessage(0);
      break;

    case WM_PAINT:
      {
        PAINTSTRUCT ps;
        HDC         hdc;
        RECT        clientrect;

        hdc = BeginPaint(hwnd, &ps);

        GetClientRect(hwnd, &clientrect);

        StretchDIBits(hdc,
                      0,
                      0,
                      clientrect.right - clientrect.left,
                      -(clientrect.top - clientrect.bottom),
                      0,
                      0,
                      WIDTH,
                      HEIGHT,
                      gamewin->pixels,
                      &gamewin->bitmapinfo,
                      DIB_RGB_COLORS,
                      SRCCOPY);

        EndPaint(hwnd, &ps);
      }
      break;

      // later:
      // case WM_CLOSE: // sent to the window when "X" is pressed
      //     if (MessageBox(hwnd, L"Really quit?", szGameWindowTitle, MB_OKCANCEL) == IDOK)
      //         DestroyWindow(hwnd); // this is the default anyway
      //     break;

    case WM_KEYDOWN:
    case WM_KEYUP:
      {
        bool down = (message == WM_KEYDOWN);

        if (wParam == VK_CONTROL)
          zxkeyset_assign(&gamewin->keys, zxkey_CAPS_SHIFT,   down);
        else if (wParam == VK_SHIFT)
          zxkeyset_assign(&gamewin->keys, zxkey_SYMBOL_SHIFT, down);
        else
        {
          if (down)
            gamewin->keys = zxkeyset_setchar(gamewin->keys, wParam);
          else
            gamewin->keys = zxkeyset_clearchar(gamewin->keys, wParam);
        }
      }
      break;

      // later:
      // case WM_COMMAND:
      //     {
      //         int wmId = LOWORD(wParam);
      //         switch (wmId)
      //         {
      //             case 0:// temp
      //             //case ...:
      //             //  break;
      //
      //             default:
      //                 return DefWindowProc(hwnd, message, wParam, lParam);
      //         }
      //     }
      //     break;

    default:
      return DefWindowProc(hwnd, message, wParam, lParam);
  }

  return 0;
}

BOOL RegisterGameWindowClass(HINSTANCE hInstance)
{
  WNDCLASSEX wcx;
  ATOM       atom;

  wcx.cbSize        = sizeof(wcx);
  wcx.style         = CS_HREDRAW | CS_VREDRAW; // redraw the entire window whenever its size changes
  wcx.lpfnWndProc   = GameWindowProcedure;
  wcx.cbClsExtra    = 0; // no extra bytes after this class
  wcx.cbWndExtra    = 0;
  wcx.hInstance     = hInstance;
  wcx.hIcon         = LoadIcon(NULL, IDI_APPLICATION); // default application icon
  wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wcx.hbrBackground = (HBRUSH) COLOR_BACKGROUND;
  wcx.lpszMenuName  = NULL;
  wcx.lpszClassName = szGameWindowClassName;
  wcx.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

  atom = RegisterClassEx(&wcx);
  if (!atom)
    return FALSE;

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

BOOL CreateGameWindow(HINSTANCE hInstance, int nCmdShow, gamewin_t **new_gamewin)
{
  gamewin_t *gamewin;
  RECT       rect;
  BOOL       result;
  HWND       window;

  *new_gamewin = NULL;

  gamewin = (gamewin_t *) malloc(sizeof(*gamewin));
  if (gamewin == NULL)
    return FALSE;

  // Required window dimensions
  rect.left   = 0;
  rect.top    = 0;
  rect.right  = WIDTH;
  rect.bottom = HEIGHT;

  // Adjust window dimensions for window furniture
  result = AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  if (result == 0)
    goto failure;

  window = CreateWindowEx(0,
                          szGameWindowClassName,
                          szGameWindowTitle,
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          rect.right - rect.left, rect.bottom - rect.top,
                          NULL,
                          NULL,
                          hInstance,
                          gamewin);
  if (!window)
    goto failure;

  ShowWindow(window, nCmdShow);
  UpdateWindow(window);

  gamewin->window = window;

  *new_gamewin = gamewin;

  return TRUE;


failure:
  free(gamewin);

  return FALSE;
}

void DestroyGameWindow(gamewin_t *doomed)
{
  free(doomed);
}

///////////////////////////////////////////////////////////////////////////////

int WINAPI WinMain(__in     HINSTANCE hInstance,
                   __in_opt HINSTANCE hPrevInstance,
                   __in     LPSTR     lpszCmdParam,
                   __in     int       nCmdShow)
{
  int        rc;
  gamewin_t *game1;
  MSG        msg;

  rc = RegisterGameWindowClass(hInstance);
  if (!rc)
    return 0;

  rc = CreateGameWindow(hInstance, nCmdShow, &game1);
  if (!rc)
    return 0;

  while (GetMessage(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  DestroyGameWindow(game1);

  return msg.wParam;
}

// vim: ts=8 sts=2 sw=2 et
