#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stddef.h>

/*
 * tinyfd_notifyPopupW
 *
 * Compatible with Windows XP through Windows 11.
 * Requires Shell32.lib and User32.lib.
 *
 * Uses the legacy balloon API on all versions.
 * Returns immediately; a worker thread manages the notification.
 */

#ifndef NOTIFYICONDATA_V2_SIZE
#define NOTIFYICONDATA_V2_SIZE \
    ((DWORD)offsetof(NOTIFYICONDATAW, guidItem))
#endif

#define TFD_NOTIFY_CB   (WM_APP + 101)
#define TFD_NOTIFY_ID   1
#define TFD_NOTIFY_TIMER 1

typedef struct TFD_NOTIFY_CONTEXT {
    wchar_t title[64];
    wchar_t message[256];
    wchar_t iconType[16];
    HWND hwnd;
    BOOL iconAdded;
    BOOL classRegistered;
} TFD_NOTIFY_CONTEXT;

static volatile LONG tfd_notify_busy = 0;

static LRESULT CALLBACK tfd_notify_WndProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TFD_NOTIFY_CONTEXT *ctx =
        (TFD_NOTIFY_CONTEXT *)GetWindowLongPtrW(
            hwnd, GWLP_USERDATA);

    if (msg == TFD_NOTIFY_CB) {
        switch ((UINT)lp) {
        case NIN_BALLOONHIDE:
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONUSERCLICK:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    if (msg == WM_TIMER && wp == TFD_NOTIFY_TIMER) {
        KillTimer(hwnd, TFD_NOTIFY_TIMER);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }

    if (msg == WM_CLOSE) {
        if (ctx && ctx->iconAdded) {
            NOTIFYICONDATAW nid;
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = NOTIFYICONDATA_V2_SIZE;
            nid.hWnd = hwnd;
            nid.uID = TFD_NOTIFY_ID;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            ctx->iconAdded = FALSE;
        }

        DestroyWindow(hwnd);
        return 0;
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI tfd_notify_Thread(LPVOID param)
{
    TFD_NOTIFY_CONTEXT *ctx = (TFD_NOTIFY_CONTEXT *)param;
    HINSTANCE instance = GetModuleHandleW(NULL);
    const wchar_t className[] = L"tinyfd_notifyPopupW_Class";
    WNDCLASSW wc;
    NOTIFYICONDATAW nid;
    MSG msg;
    BOOL registered = FALSE;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = tfd_notify_WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;

    if (RegisterClassW(&wc)) {
        registered = TRUE;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        goto cleanup;
    }

    ctx->classRegistered = registered;

    ctx->hwnd = CreateWindowExW(
        0, className, L"", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, instance, NULL);

    if (!ctx->hwnd)
        goto cleanup;

    SetWindowLongPtrW(
        ctx->hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = ctx->hwnd;
    nid.uID = TFD_NOTIFY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.uCallbackMessage = TFD_NOTIFY_CB;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
        goto cleanup;

    ctx->iconAdded = TRUE;

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = ctx->hwnd;
    nid.uID = TFD_NOTIFY_ID;
    nid.uFlags = NIF_INFO;
    nid.uTimeout = 10000;

    wcsncpy_s(nid.szInfoTitle,
        ARRAYSIZE(nid.szInfoTitle), ctx->title, _TRUNCATE);
    wcsncpy_s(nid.szInfo,
        ARRAYSIZE(nid.szInfo), ctx->message, _TRUNCATE);

    if (_wcsicmp(ctx->iconType, L"warning") == 0)
        nid.dwInfoFlags = NIIF_WARNING;
    else if (_wcsicmp(ctx->iconType, L"error") == 0)
        nid.dwInfoFlags = NIIF_ERROR;
    else
        nid.dwInfoFlags = NIIF_INFO;

    if (!SetTimer(ctx->hwnd, TFD_NOTIFY_TIMER, 35000, NULL))
        goto cleanup;

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid))
        goto cleanup;

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

cleanup:
    if (ctx->hwnd && IsWindow(ctx->hwnd)) {
        KillTimer(ctx->hwnd, TFD_NOTIFY_TIMER);

        if (ctx->iconAdded) {
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = NOTIFYICONDATA_V2_SIZE;
            nid.hWnd = ctx->hwnd;
            nid.uID = TFD_NOTIFY_ID;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            ctx->iconAdded = FALSE;
        }

        DestroyWindow(ctx->hwnd);
    }

    if (ctx->classRegistered)
        UnregisterClassW(className, instance);

    HeapFree(GetProcessHeap(), 0, ctx);
    InterlockedExchange(&tfd_notify_busy, 0);
    return 0;
}

void tinyfd_notifyPopupW(
    wchar_t const* aTitle,
    wchar_t const* aMessage,
    wchar_t const* aIconType)
{
    TFD_NOTIFY_CONTEXT *ctx;
    HANDLE thread;

    if (InterlockedCompareExchange(
            &tfd_notify_busy, 1, 0) != 0)
        return;

    ctx = (TFD_NOTIFY_CONTEXT *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ctx));

    if (!ctx) {
        InterlockedExchange(&tfd_notify_busy, 0);
        return;
    }

    wcsncpy_s(ctx->title, ARRAYSIZE(ctx->title),
        aTitle ? aTitle : L"", _TRUNCATE);
    wcsncpy_s(ctx->message, ARRAYSIZE(ctx->message),
        aMessage ? aMessage : L"", _TRUNCATE);
    wcsncpy_s(ctx->iconType, ARRAYSIZE(ctx->iconType),
        aIconType ? aIconType : L"info", _TRUNCATE);

    thread = CreateThread(
        NULL, 0, tfd_notify_Thread, ctx, 0, NULL);

    if (!thread) {
        HeapFree(GetProcessHeap(), 0, ctx);
        InterlockedExchange(&tfd_notify_busy, 0);
        return;
    }

    CloseHandle(thread);
}
