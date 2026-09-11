// ============================================================================
//  main.cpp —— Win32 界面
// ============================================================================
#include "app.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <richedit.h>
#include <objbase.h>
#include <algorithm>
#include <stdio.h>

#pragma comment(lib, "comctl32.lib")

// ---- 控件 ID ----
enum
{
    IDC_LIST = 1001,
    IDC_ADD,
    IDC_REMOVE,
    IDC_CLEAR,
    IDC_PAGES,
    IDC_OUTDIR,
    IDC_BROWSE,
    IDC_FMT,
    IDC_QUALITY,
    IDC_QHINT,
    IDC_DPI,
    IDC_OPENAFTER,
    IDC_START,
    IDC_ABOUT,
    IDC_LOG,
    IDC_HINT
};

// ---- 配色 ----
static const COLORREF CLR_OK = RGB(0x1E, 0x7D, 0x1E);
static const COLORREF CLR_WARN = RGB(0xA0, 0x60, 0x00);
static const COLORREF CLR_ERR = RGB(0xC0, 0x20, 0x20);
static const COLORREF CLR_DIM = RGB(0x70, 0x70, 0x70);
static const COLORREF CLR_TXT = RGB(0x20, 0x20, 0x20);

// ---- 全局 ----
static HINSTANCE g_inst = nullptr;
static HWND g_hList, g_hPages, g_hOutDir, g_hFmt, g_hQuality, g_hQHint, g_hDpi;
static HWND g_hOpenAfter, g_hStart, g_hLog, g_hHint;
static HFONT g_font = nullptr;
static bool g_isRich = false;
static bool g_running = false;
static std::vector<std::wstring> g_files;

static const wchar_t WND_CLASS[] = L"PdfPageExporterWnd";
static const wchar_t APP_NAME[] = L"PDF / Word 页面导出工具";
static const wchar_t APP_VERSION[] = L"1.1.0";
static const wchar_t REPO_URL[] = L"https://github.com/Thesouth-EricWang/pdf-page-exporter";
static const wchar_t APP_TITLE[] = L"PDF / Word 页面导出工具";

static const wchar_t FILTER[] =
    L"PDF / Word 文档\0*.pdf;*.docx;*.doc;*.docm;*.rtf;*.odt\0"
    L"PDF 文件\0*.pdf\0"
    L"Word 文档\0*.docx;*.doc;*.docm\0"
    L"所有文件\0*.*\0";

// ---------------------------------------------------------------------------
static void AppendLog(const std::wstring &s, COLORREF color, bool bold = false)
{
    if (!g_hLog)
        return;
    std::wstring line = s + L"\r\n";
    int len = GetWindowTextLengthW(g_hLog);
    CHARRANGE cr;
    cr.cpMin = len;
    cr.cpMax = len;
    SendMessageW(g_hLog, EM_EXSETSEL, 0, (LPARAM)&cr);
    if (g_isRich)
    {
        CHARFORMAT2W cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_FACE | CFM_SIZE;
        cf.dwEffects = bold ? CFE_BOLD : 0;
        cf.crTextColor = color;
        cf.yHeight = 180; // 9pt
        wcscpy_s(cf.szFaceName, L"Consolas");
        SendMessageW(g_hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}

static void OpenFolder(const std::wstring &dir)
{
    if (dir.empty())
        return;
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void ShowAbout(HWND hwnd)
{
    std::wstring msg = std::wstring(APP_NAME) + L"\r\n版本 " + APP_VERSION +
                       L"\r\n\r\n把 PDF 或 Word 文档的指定页导出成图片。"
                       L"\r\n\r\n仓库：\r\n" + REPO_URL +
                       L"\r\n\r\n（点「确定」用浏览器打开仓库）";
    if (MessageBoxW(hwnd, msg.c_str(), APP_TITLE, MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
        ShellExecuteW(nullptr, L"open", REPO_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

static void RefreshList()
{
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_files.size(); ++i)
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)FileName(g_files[i]).c_str());
}

static void AddFiles(HWND hwnd, const std::vector<std::wstring> &paths)
{
    int added = 0, skipped = 0;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const std::wstring &p = paths[i];
        if (!IsSupportedExt(ExtLower(p)))
        {
            skipped++;
            continue;
        }
        bool dup = false;
        for (size_t k = 0; k < g_files.size(); ++k)
            if (_wcsicmp(g_files[k].c_str(), p.c_str()) == 0)
            {
                dup = true;
                break;
            }
        if (dup)
        {
            skipped++;
            continue;
        }
        g_files.push_back(p);
        added++;
    }
    RefreshList();
    if (added)
        AppendLog(Fmt(L"已添加 %d 个文件，当前共 %d 个。", added, (int)g_files.size()), CLR_TXT);
    if (skipped)
        AppendLog(Fmt(L"跳过 %d 个（不支持的类型或已在列表里）。", skipped), CLR_WARN);
    (void)hwnd;
}

static void BrowseFiles(HWND hwnd)
{
    std::vector<wchar_t> buf(262144, 0);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = FILTER;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = L"选择 PDF 或 Word 文档（可多选）";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn))
        return;

    std::vector<std::wstring> out;
    const wchar_t *p = buf.data();
    std::wstring dir = p;
    p += dir.size() + 1;
    if (*p == 0)
        out.push_back(dir);
    else
        while (*p)
        {
            std::wstring f = p;
            out.push_back(dir + L"\\" + f);
            p += f.size() + 1;
        }
    AddFiles(hwnd, out);
}

static void BrowseOutDir(HWND hwnd)
{
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"选择输出目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;
    wchar_t path[MAX_PATH] = {0};
    if (SHGetPathFromIDListW(pidl, path))
    {
        std::wstring old;
        wchar_t cur[4096];
        GetWindowTextW(g_hOutDir, cur, 4096);
        old = cur;
        SetWindowTextW(g_hOutDir, path);
        (void)old;
    }
    CoTaskMemFree(pidl);
}

static void StartExport(HWND hwnd)
{
    if (g_running)
        return;
    if (g_files.empty())
    {
        AppendLog(L"请先添加 PDF 或 Word 文档。", CLR_WARN);
        return;
    }
    for (size_t i = 0; i < g_files.size(); ++i)
        if (!FileExists(g_files[i]))
        {
            AppendLog(L"这些文件找不到了，请从列表里移除：" + FileName(g_files[i]), CLR_ERR);
            return;
        }

    Job *job = new Job();
    job->hwnd = hwnd;
    job->pm.files = g_files;

    wchar_t buf[8192];
    GetWindowTextW(g_hPages, buf, 8192);
    job->pm.pages = buf;
    GetWindowTextW(g_hOutDir, buf, 8192);
    job->pm.outDir = Trim(buf);

    int sel = (int)SendMessageW(g_hFmt, CB_GETCURSEL, 0, 0);
    job->pm.jpg = (sel != 1);

    GetWindowTextW(g_hQuality, buf, 8192);
    job->pm.quality = _wtoi(buf);
    if (job->pm.quality < 1 || job->pm.quality > 100)
        job->pm.quality = 85;

    GetWindowTextW(g_hDpi, buf, 8192);
    std::wstring d = Trim(buf);
    job->pm.dpi = (d.empty() || d == L"自动") ? 0 : _wtoi(d.c_str());
    if (job->pm.dpi < 0)
        job->pm.dpi = 0;

    g_running = true;
    EnableWindow(g_hStart, FALSE);
    AppendLog(L"", CLR_DIM);
    HANDLE h = CreateThread(nullptr, 0, WorkerProc, job, 0, nullptr);
    if (h)
        CloseHandle(h);
    else
    {
        g_running = false;
        EnableWindow(g_hStart, TRUE);
        delete job;
        AppendLog(L"无法创建后台线程。", CLR_ERR);
    }
}

// ---------------------------------------------------------------------------
static void CreateControls(HWND hwnd)
{
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    HFONT logFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                FIXED_PITCH | FF_MODERN, L"Consolas");

    struct
    {
        const wchar_t *cls, *text;
        DWORD style;
        int id, x, y, w, h;
    } items[] = {
        {L"STATIC", L"源文件（PDF / Word，可多选）", WS_CHILD | WS_VISIBLE, 0, 10, 10, 400, 18},
        {L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY |
                            LBS_EXTENDEDSEL, IDC_LIST, 10, 30, 600, 96},
        {L"BUTTON", L"添加…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_ADD, 620, 30, 150, 26},
        {L"BUTTON", L"移除选中", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_REMOVE, 620, 60, 150, 26},
        {L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_CLEAR, 620, 90, 150, 26},
        {L"STATIC", L"提示：把 PDF 或 Word 文档直接拖进这个窗口，可一次拖多个",
         WS_CHILD | WS_VISIBLE, IDC_HINT, 10, 130, 760, 18},
        {L"BUTTON", L"导出设置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 10, 152, 760, 150},
        {L"STATIC", L"页码范围", WS_CHILD | WS_VISIBLE, 0, 22, 178, 70, 20},
        {L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, IDC_PAGES, 95, 176, 200, 22},
        {L"STATIC", L"例：1,3,5-7（留空 = 全部）", WS_CHILD | WS_VISIBLE, 0, 305, 180, 300, 18},
        {L"STATIC", L"输出目录", WS_CHILD | WS_VISIBLE, 0, 22, 208, 70, 20},
        {L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, IDC_OUTDIR, 95, 206, 540, 22},
        {L"BUTTON", L"选择…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_BROWSE, 645, 206, 110, 22},
        {L"STATIC", L"格式", WS_CHILD | WS_VISIBLE, 0, 22, 238, 40, 20},
        {L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                             CBS_HASSTRINGS, IDC_FMT, 65, 236, 80, 200},
        {L"STATIC", L"压缩", WS_CHILD | WS_VISIBLE, 0, 160, 238, 40, 20},
        {L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL |
                            ES_NUMBER, IDC_QUALITY, 205, 236, 50, 22},
        {L"STATIC", L"", WS_CHILD | WS_VISIBLE, IDC_QHINT, 262, 240, 120, 18},
        {L"STATIC", L"分辨率", WS_CHILD | WS_VISIBLE, 0, 390, 238, 56, 20},
        {L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN |
                             CBS_HASSTRINGS, IDC_DPI, 450, 236, 90, 200},
        {L"BUTTON", L"完成后打开文件夹", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
         IDC_OPENAFTER, 560, 236, 190, 22},
        {L"STATIC", L"压缩档 1-100：数值越小文件越小（默认 85）", WS_CHILD | WS_VISIBLE,
         IDC_HINT + 100, 22, 266, 700, 18},
        {L"BUTTON", L"开始导出", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_START, 10, 305, 110, 30},
        {L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_ABOUT, 130, 305, 80, 30},
        {L"STATIC", L"日志", WS_CHILD | WS_VISIBLE, 0, 10, 345, 60, 18},
    };

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
    {
        HWND h = CreateWindowExW(
            0, items[i].cls, items[i].text, items[i].style,
            items[i].x, items[i].y, items[i].w, items[i].h,
            hwnd, (HMENU)(INT_PTR)items[i].id, g_inst, nullptr);
        if (h && g_font)
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    }

    // 日志框（优先 RichEdit，失败退回普通 EDIT）
    const wchar_t *logCls = g_isRich ? L"RICHEDIT50W" : L"EDIT";
    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, logCls, L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                 ES_READONLY | ES_AUTOVSCROLL,
                             10, 365, 760, 260, hwnd, (HMENU)IDC_LOG, g_inst, nullptr);
    if (g_hLog)
    {
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)(logFont ? logFont : g_font), TRUE);
        if (g_isRich)
        {
            CHARFORMAT2W cf;
            ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
            cf.yHeight = 180;
            wcscpy_s(cf.szFaceName, L"Consolas");
            SendMessageW(g_hLog, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
            SendMessageW(g_hLog, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0xFA, 0xFA, 0xFA));
        }
    }

    // 下拉内容
    SendMessageW(g_hFmt = GetDlgItem(hwnd, IDC_FMT), CB_ADDSTRING, 0, (LPARAM)L"JPG");
    SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"PNG");
    SendMessageW(g_hFmt, CB_SETCURSEL, 0, 0);

    g_hDpi = GetDlgItem(hwnd, IDC_DPI);
    const wchar_t *dpiItems[] = {L"自动", L"150", L"200", L"300", L"400", L"600"};
    for (int i = 0; i < 6; ++i)
        SendMessageW(g_hDpi, CB_ADDSTRING, 0, (LPARAM)dpiItems[i]);
    SetWindowTextW(g_hDpi, L"自动");

    g_hList = GetDlgItem(hwnd, IDC_LIST);
    g_hPages = GetDlgItem(hwnd, IDC_PAGES);
    g_hOutDir = GetDlgItem(hwnd, IDC_OUTDIR);
    g_hQuality = GetDlgItem(hwnd, IDC_QUALITY);
    g_hQHint = GetDlgItem(hwnd, IDC_QHINT);
    g_hOpenAfter = GetDlgItem(hwnd, IDC_OPENAFTER);
    g_hStart = GetDlgItem(hwnd, IDC_START);
    SetWindowTextW(g_hQuality, L"85");
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateControls(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        AppendLog(Fmt(L"%s  v%s", APP_NAME, APP_VERSION), CLR_DIM, true);
        AppendLog(L"仓库：" + std::wstring(REPO_URL), CLR_DIM);
        AppendLog(L"就绪。把 PDF 或 Word 文档拖进来，或点「添加…」。", CLR_DIM);
        return 0;

    case WM_DROPFILES:
    {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < n; ++i)
        {
            wchar_t buf[32768];
            if (DragQueryFileW(hd, i, buf, 32768))
                paths.push_back(buf);
        }
        DragFinish(hd);
        AddFiles(hwnd, paths);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_ADD:
            BrowseFiles(hwnd);
            return 0;
        case IDC_REMOVE:
        {
            int cnt = (int)SendMessageW(g_hList, LB_GETSELCOUNT, 0, 0);
            if (cnt <= 0)
                break;
            std::vector<int> sel(cnt);
            SendMessageW(g_hList, LB_GETSELITEMS, cnt, (LPARAM)sel.data());
            std::sort(sel.begin(), sel.end());
            for (int i = (int)sel.size() - 1; i >= 0; --i)
            {
                int idx = sel[i];
                if (idx >= 0 && idx < (int)g_files.size())
                    g_files.erase(g_files.begin() + idx);
            }
            RefreshList();
            AppendLog(Fmt(L"当前共 %d 个文件。", (int)g_files.size()), CLR_TXT);
            return 0;
        }
        case IDC_CLEAR:
            g_files.clear();
            RefreshList();
            AppendLog(L"已清空文件列表。", CLR_TXT);
            return 0;
        case IDC_BROWSE:
            BrowseOutDir(hwnd);
            return 0;
        case IDC_START:
            StartExport(hwnd);
            return 0;
        case IDC_ABOUT:
            ShowAbout(hwnd);
            return 0;
        case IDC_FMT:
            if (HIWORD(wp) == CBN_SELCHANGE)
            {
                bool jpg = (SendMessageW(g_hFmt, CB_GETCURSEL, 0, 0) != 1);
                EnableWindow(g_hQuality, jpg);
                SetWindowTextW(g_hQHint, jpg ? L"1-100，越小文件越小"
                                             : L"PNG 无损，压缩档不生效");
            }
            return 0;
        }
        break;

    case WM_APP_LOG:
    {
        std::wstring *p = (std::wstring *)wp;
        if (p)
        {
            AppendLog(*p, CLR_TXT);
            delete p;
        }
        return 0;
    }
    case WM_APP_LOGERR:
    {
        std::wstring *p = (std::wstring *)wp;
        if (p)
        {
            AppendLog(*p, CLR_ERR);
            delete p;
        }
        return 0;
    }
    case WM_APP_DONE:
    {
        DoneInfo *di = (DoneInfo *)wp;
        g_running = false;
        EnableWindow(g_hStart, TRUE);
        if (di)
        {
            if (di->okFiles == di->total)
                AppendLog(Fmt(L">>> 完成：%d 个文件，共 %d 张图片",
                              di->okFiles, di->images), CLR_OK, true);
            else
                AppendLog(Fmt(L">>> 完成：%d 个文件成功，%d 个失败，共 %d 张图片",
                              di->okFiles, di->total - di->okFiles, di->images),
                          CLR_WARN, true);
            if (SendMessageW(g_hOpenAfter, BM_GETCHECK, 0, 0) == BST_CHECKED)
                OpenFolder(di->lastOut);
            delete di;
        }
        return 0;
    }

    case WM_CLOSE:
        if (g_running &&
            MessageBoxW(hwnd, L"还在导出中，确定要关闭吗？", APP_TITLE,
                        MB_ICONQUESTION | MB_YESNO) != IDYES)
            return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    g_inst = inst;

    // 设 DPI 感知
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore)
    {
        typedef HRESULT(WINAPI * Fn)(int);
        Fn fn = (Fn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (fn)
            fn(1);
    }

    // 富文本日志（失败就退回普通编辑框）
    g_isRich = (LoadLibraryW(L"Msftedit.dll") != nullptr);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 找 pdfium.dll：先看程序目录，再看当前目录
    std::wstring dll = ExeDir() + L"\\pdfium.dll";
    if (!FileExists(dll))
        dll = L"pdfium.dll";
    LoadPdfium(dll);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT rc = {0, 0, 780, 640};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    std::wstring title = std::wstring(APP_NAME) + L"  v" + APP_VERSION;
    HWND hwnd = CreateWindowExW(
        0, WND_CLASS, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd)
        return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 命令行带入的文件
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1)
    {
        std::vector<std::wstring> files;
        for (int i = 1; i < argc; ++i)
            if (FileExists(argv[i]))
                files.push_back(argv[i]);
        if (!files.empty())
            AddFiles(hwnd, files);
    }
    if (argv)
        LocalFree(argv);

    if (!PdfiumReady())
        AppendLog(L"没找到 pdfium.dll。缺少它时只能处理扫描版 PDF，且不支持 Word 文档。",
                  CLR_WARN);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
