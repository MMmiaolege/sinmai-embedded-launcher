#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <tlhelp32.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// 多显示器支持：存储所有窗口和位图
struct DisplayWindow {
    HWND hwnd;
    HBITMAP hBitmap;
    int displayIndex;
    wchar_t deviceName[32];
};

std::vector<DisplayWindow> g_displayWindows;
DWORD g_sinmaiPid = 0; // 保存sinmai的进程ID
bool g_sinmaiRunning = false; // sinmai运行状态

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void PumpMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// 设置显示器为纵向
bool SetDisplayToPortrait(const wchar_t* deviceName) {
    DEVMODE dm = {};
    dm.dmSize = sizeof(DEVMODE);
    
    if (EnumDisplaySettings(deviceName, ENUM_CURRENT_SETTINGS, &dm) == 0) {
        return false;
    }
    
    DWORD temp = dm.dmPelsWidth;
    dm.dmPelsWidth = dm.dmPelsHeight;
    dm.dmPelsHeight = temp;
    
    dm.dmDisplayOrientation = DMDO_90;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYORIENTATION;
    
    LONG result = ChangeDisplaySettingsEx(deviceName, &dm, nullptr, CDS_TEST, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        return false;
    }
    
    result = ChangeDisplaySettingsEx(deviceName, &dm, nullptr, 0, nullptr);
    return result == DISP_CHANGE_SUCCESSFUL;
}

// 为单个窗口加载并显示图片
bool LoadAndShowImageForWindow(HWND hwnd, HBITMAP& hBitmap, const wchar_t* imagePath) {
    if (!hwnd) return false;
    
    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = nullptr;
    }
    
    hBitmap = (HBITMAP)LoadImageW(nullptr, imagePath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    
    if (hBitmap) {
        HDC hdcWindow = GetDC(hwnd);
        HDC hdcMem = CreateCompatibleDC(hdcWindow);
        SelectObject(hdcMem, hBitmap);
        
        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);
        
        RECT windowRect;
        GetWindowRect(hwnd, &windowRect);
        int windowWidth = windowRect.right - windowRect.left;
        int windowHeight = windowRect.bottom - windowRect.top;
        
        int x = (windowWidth - bmp.bmWidth) / 2;
        int y = (windowHeight - bmp.bmHeight) / 2;
        
        RECT rect = {0, 0, windowWidth, windowHeight};
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdcWindow, &rect, blackBrush);
        DeleteObject(blackBrush);
        
        BitBlt(hdcWindow, x, y, bmp.bmWidth, bmp.bmHeight, hdcMem, 0, 0, SRCCOPY);
        
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        return true;
    }
    return false;
}

// 为所有显示器窗口加载并显示图片
bool LoadAndShowImageForAllDisplays(const wchar_t* imagePath) {
    bool success = true;
    for (auto& displayWindow : g_displayWindows) {
        if (!LoadAndShowImageForWindow(displayWindow.hwnd, displayWindow.hBitmap, imagePath)) {
            success = false;
        }
    }
    return success;
}

// 枚举显示器回调函数
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    int* displayIndex = (int*)dwData;
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    MONITORINFOEX monitorInfo;
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfo(hMonitor, &monitorInfo);
    
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"MultiDisplaySplash";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClass(&wc);
        classRegistered = true;
    }
    
    int width = lprcMonitor->right - lprcMonitor->left;
    int height = lprcMonitor->bottom - lprcMonitor->top;
    
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST, L"MultiDisplaySplash", L"", WS_POPUP | WS_VISIBLE,
        lprcMonitor->left, lprcMonitor->top, width, height,
        nullptr, nullptr, hInstance, nullptr
    );
    
    if (hwnd) {
        DisplayWindow displayWindow;
        displayWindow.hwnd = hwnd;
        displayWindow.hBitmap = nullptr;
        displayWindow.displayIndex = (*displayIndex)++;
        wcscpy(displayWindow.deviceName, monitorInfo.szDevice);
        g_displayWindows.push_back(displayWindow);
        
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    
    return TRUE;
}

bool CreateMultiDisplayWindows() {
    int displayIndex = 1;
    g_displayWindows.clear();
    
    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&displayIndex)) {
        return false;
    }
    
    return !g_displayWindows.empty();
}

void CloseAllDisplayWindows() {
    for (auto& displayWindow : g_displayWindows) {
        if (displayWindow.hBitmap) DeleteObject(displayWindow.hBitmap);
        if (displayWindow.hwnd) DestroyWindow(displayWindow.hwnd);
    }
    g_displayWindows.clear();
}

// 设置所有显示器为纵向
bool SetAllDisplaysToPortrait() {
    bool allSuccess = true;
    for (const auto& displayWindow : g_displayWindows) {
        if (!SetDisplayToPortrait(displayWindow.deviceName)) {
            allSuccess = false;
        }
        Sleep(1000);
    }
    Sleep(2000);
    return allSuccess;
}

// 检查进程是否在运行
bool IsProcessRunning(DWORD pid) {
    if (pid == 0) return false;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess) {
        DWORD exitCode;
        bool result = (GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE);
        CloseHandle(hProcess);
        return result;
    }
    return false;
}

// 等待进程启动并返回进程ID（改进版：等待窗口出现）
DWORD WaitForProcessAndWindow(const std::wstring& processName, int timeoutSeconds = 60) {
    auto start = std::chrono::steady_clock::now();
    DWORD pid = 0;
    
    // 第一阶段：等待进程出现
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeoutSeconds)) {
        PumpMessages();
        
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);
            
            if (Process32FirstW(hSnapshot, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                        pid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(hSnapshot, &pe));
            }
            CloseHandle(hSnapshot);
        }
        
        if (pid != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (pid == 0) return 0;
    
    // 第二阶段：等待窗口出现（额外等待10秒）
    auto windowStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - windowStart < std::chrono::seconds(10)) {
        PumpMessages();
        
        // 查找该进程的可见窗口
        HWND hwnd = nullptr;
        while ((hwnd = FindWindowEx(nullptr, hwnd, nullptr, nullptr)) != nullptr) {
            DWORD windowPid;
            GetWindowThreadProcessId(hwnd, &windowPid);
            
            if (windowPid == pid && IsWindowVisible(hwnd)) {
                // 找到可见窗口，再等待2秒确保窗口完全显示
                for (int i = 0; i < 200; i++) {
                    PumpMessages();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return pid;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return pid; // 返回进程ID，即使没找到窗口
}

// 检查sinmai是否有可见窗口
bool HasSinmaiVisibleWindow(DWORD pid) {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowEx(nullptr, hwnd, nullptr, nullptr)) != nullptr) {
        DWORD windowPid;
        GetWindowThreadProcessId(hwnd, &windowPid);
        
        if (windowPid == pid && IsWindowVisible(hwnd)) {
            return true;
        }
    }
    return false;
}

// 置顶窗口
bool BringWindowToFront(DWORD pid) {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowEx(nullptr, hwnd, nullptr, nullptr)) != nullptr) {
        DWORD windowPid;
        GetWindowThreadProcessId(hwnd, &windowPid);
        
        if (windowPid == pid && IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return true;
        }
    }
    return false;
}

// 调整启动器窗口层级（在sinmai后面显示）
void AdjustLauncherZOrder() {
    for (const auto& displayWindow : g_displayWindows) {
        if (displayWindow.hwnd) {
            SetWindowPos(displayWindow.hwnd, HWND_BOTTOM, 0, 0, 0, 0, 
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

// 置顶所有启动器窗口（sinmai退出时显示Error2）
void BringLauncherToFront() {
    for (const auto& displayWindow : g_displayWindows) {
        if (displayWindow.hwnd) {
            SetWindowPos(displayWindow.hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(displayWindow.hwnd, SW_RESTORE);
            SetForegroundWindow(displayWindow.hwnd);
        }
    }
    // Sinmai退出时显示Error2
    LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError2.bmp");
}

// 检查网络连接
bool CheckNetworkConnection() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    
    int timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    struct hostent* host = gethostbyname("at.sys-allnet.cn");
    if (host == nullptr) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(80);
    serverAddr.sin_addr.s_addr = *((unsigned long*)host->h_addr);
    
    bool connected = (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == 0);
    
    closesocket(sock);
    WSACleanup();
    return connected;
}

// 带重试的网络检测
bool CheckNetworkWithRetry() {
    while (true) {
        if (CheckNetworkConnection()) return true;
        LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError.bmp");
        for (int i = 0; i < 500; i++) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return false;
}

int main() {
    if (!CreateMultiDisplayWindows()) return 1;
    
    SetAllDisplaysToPortrait();
    
    for (int i = 0; i < 100; i++) {
        PumpMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (!CheckNetworkWithRetry()) {
        while (true) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    if (!LoadAndShowImageForAllDisplays(L"C:\\SplashScreen.bmp")) {
        while (true) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    for (int i = 0; i < 300; i++) {
        PumpMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    LoadAndShowImageForAllDisplays(L"C:\\SplashScreen2.bmp");
    
    for (int i = 0; i < 200; i++) {
        PumpMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"start.bat", nullptr, nullptr, SW_HIDE);
    
    if ((INT_PTR)result <= 32) {
        // 启动失败显示Error1
        LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError1.bmp");
        while (true) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // 使用改进的等待函数
    g_sinmaiPid = WaitForProcessAndWindow(L"sinmai.exe", 60);
    
    if (g_sinmaiPid != 0) {
        g_sinmaiRunning = true;
        
        // 额外等待确保窗口完全显示
        for (int i = 0; i < 300; i++) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 置顶 sinmai
        BringWindowToFront(g_sinmaiPid);
        
        for (int i = 0; i < 200; i++) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        AdjustLauncherZOrder();
        
        for (int i = 0; i < 100; i++) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 正常运行时显示Error2（作为背景）
        LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError2.bmp");
        
        // 监控循环
        while (true) {
            PumpMessages();
            
            if (!IsProcessRunning(g_sinmaiPid)) {
                if (g_sinmaiRunning) {
                    g_sinmaiRunning = false;
                    for (int i = 0; i < 100; i++) {
                        PumpMessages();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    // Sinmai退出时显示Error2
                    BringLauncherToFront();
                }
            } else {
                if (!g_sinmaiRunning) {
                    g_sinmaiRunning = true;
                    for (int i = 0; i < 200; i++) {
                        PumpMessages();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    BringWindowToFront(g_sinmaiPid);
                    for (int i = 0; i < 100; i++) {
                        PumpMessages();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    AdjustLauncherZOrder();
                    // 恢复正常显示Error2
                    LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError2.bmp");
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
    } else {
        // 启动超时显示Error1
        LoadAndShowImageForAllDisplays(L"C:\\SplashScreenError1.bmp");
        while (true) {
            PumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    return 0;
}