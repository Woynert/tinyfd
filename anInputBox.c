#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    wchar_t result[1024];
    HWND edit;
    HINSTANCE instance;
    int cancelled;
} InputDialogState;


static LRESULT CALLBACK
InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    InputDialogState* state = (InputDialogState*)
        GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;

        state = (InputDialogState*)cs->lpCreateParams;

        SetWindowLongPtrW(
            hwnd, GWLP_USERDATA, (LONG_PTR)state);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_CREATE:
    {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND prompt = CreateWindowExW(
            0, L"STATIC", L"Enter a value:",
            WS_CHILD | WS_VISIBLE,
            12, 12, 356, 22,
            hwnd, NULL, state->instance, NULL);

        state->edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            ES_AUTOHSCROLL,
            12, 38, 356, 25,
            hwnd, (HMENU)1001, state->instance, NULL);

        HWND ok = CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            BS_DEFPUSHBUTTON,
            192, 76, 84, 27,
            hwnd, (HMENU)IDOK, state->instance, NULL);

        HWND cancel = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            284, 76, 84, 27,
            hwnd, (HMENU)IDCANCEL, state->instance, NULL);

        SendMessageW(prompt, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(state->edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(ok, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(cancel, WM_SETFONT, (WPARAM)font, TRUE);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            GetWindowTextW(
                state->edit,
                state->result,
                (int)(sizeof(state->result) /
                    sizeof(state->result[0]))
            );

            DestroyWindow(hwnd);
            return 0;

        case IDCANCEL:
            state->cancelled = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        state->cancelled = 1;
        DestroyWindow(hwnd);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


/*
 * Returns a dynamically allocated wide-character string on success.
 * Returns NULL on cancellation or error.
 *
 * The caller must free() the returned string.
 */
wchar_t*
tinyfd_inputBoxW(
    const wchar_t* aTitle,
    const wchar_t* aMessage,
    const wchar_t* aDefaultInput)
{
    const wchar_t* lClassName = L"TinyInputDialogClass";

    InputDialogState state = { 0 };
    WNDCLASSW wc = { 0 };
    MSG msg;
    RECT rc;
    HWND dialog;
    BOOL registered = FALSE;

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP |
        WS_VISIBLE;

    state.instance = GetModuleHandleW(NULL);

    wc.lpfnWndProc = InputDialogProc;
    wc.hInstance = state.instance;
    wc.lpszClassName = lClassName;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (RegisterClassW(&wc))
    {
        registered = TRUE;
    }
    else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return NULL;
    }

    dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        lClassName,
        aTitle ? aTitle : L"Input",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, 392, 145,
        GetActiveWindow(), NULL,
        state.instance, &state);

    if (!dialog)
    {
        if (registered)
            UnregisterClassW(lClassName, state.instance);

        return NULL;
    }

    HWND prompt = FindWindowExW(
        dialog, NULL, L"STATIC", NULL);

    if (prompt)
        SetWindowTextW(
            prompt,
            aMessage ? aMessage : L"Enter a value:");

    if (state.edit && aDefaultInput)
        SetWindowTextW(state.edit, aDefaultInput);

    GetWindowRect(dialog, &rc);

    SetWindowPos(
        dialog, HWND_TOP,
        (GetSystemMetrics(SM_CXSCREEN) -
            (rc.right - rc.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) -
            (rc.bottom - rc.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    SetForegroundWindow(dialog);
    SetFocus(state.edit);

    while (IsWindow(dialog))
    {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);

        if (r <= 0)
        {
            state.cancelled = 1;

            if (r == 0)
                PostQuitMessage((int)msg.wParam);

            if (IsWindow(dialog))
                DestroyWindow(dialog);

            break;
        }

        if (!IsDialogMessageW(dialog, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (registered)
        UnregisterClassW(lClassName, state.instance);

    if (state.cancelled)
        return NULL;

    size_t len = wcslen(state.result) + 1;

    wchar_t* result = (wchar_t*)malloc(
        len * sizeof(wchar_t));

    if (result)
        memcpy(result, state.result, len * sizeof(wchar_t));

    return result;
}


int main(void)
{
    wchar_t* lCharString = tinyfd_inputBoxW(
        L"mon Titre",
        L"mon Message",
        L"et ta soeur ?"
    );

    if (lCharString != NULL)
    {
        wprintf(L"le return : %ls\n", lCharString);
        free(lCharString);
    }
    else
    {
        wprintf(L"Dialogue annule ou erreur.\n");
    }

    return 0;
}

#endif