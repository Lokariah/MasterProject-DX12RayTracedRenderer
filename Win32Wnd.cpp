//#include "Win32Wnd.h"
//
//HWND Win32Wnd::m_mainWindowHWND = nullptr;
//
//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd) {
//    Win32Wnd wnd;
//    if (!wnd.InitWinApp(hInstance, nShowCmd)) return 0;
//    return wnd.Run();
//}
//
//bool Win32Wnd::InitWinApp(HINSTANCE hInstance, int show)
//{
//    WNDCLASS wndClass;
//    wndClass.style = CS_HREDRAW | CS_VREDRAW;
//    wndClass.lpfnWndProc = WndProc;
//    wndClass.cbClsExtra = 0;
//    wndClass.cbWndExtra = 0;
//    wndClass.hInstance = hInstance;
//    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
//    wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
//    wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
//    wndClass.lpszMenuName = 0;
//    wndClass.lpszClassName = L"MastersProjectRendererClass";
//
//    if (!RegisterClass(&wndClass)) {
//        MessageBox(0, L"RegisterClass FAILED", 0, 0);
//        return false;
//    }
//
//    m_mainWindowHWND = CreateWindow(
//        wndClass.lpszClassName,
//        L"MastersProjectRenderer",
//        WS_OVERLAPPEDWINDOW,
//        CW_USEDEFAULT,
//        CW_USEDEFAULT,
//        CW_USEDEFAULT,
//        CW_USEDEFAULT,
//        nullptr,
//        nullptr,
//        hInstance,
//        0);
//
//    if (m_mainWindowHWND == 0) {
//        MessageBox(0, L"CreateWindow FAILED", 0, 0);
//        return false;
//    }
//
//    ShowWindow(m_mainWindowHWND, show);
//    UpdateWindow(m_mainWindowHWND);
//    return true;
//}
//
//int Win32Wnd::Run()
//{
//    MSG msg = {};
//    bool bRet = 1;
//    while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0) {
//        if (bRet == -1) {
//            MessageBox(0, L"GetMessage FAILED", L"Error", MB_OK);
//            break;
//        }
//        else {
//            TranslateMessage(&msg);
//            DispatchMessage(&msg);
//        }
//    }
//    return (int)msg.wParam;
//}
//
//LRESULT Win32Wnd::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
//{
//    switch (msg) {
//    case WM_LBUTTONDOWN:
//        MessageBox(0, L"Test", 0, 0);
//        return 0;
//    case WM_KEYDOWN:
//        if (wParam == VK_ESCAPE) DestroyWindow(m_mainWindowHWND);
//        return 0;
//    case WM_DESTROY:
//        PostQuitMessage(0);
//        return 0;
//    }
//    return DefWindowProc(hWnd, msg, wParam, lParam);
//}
