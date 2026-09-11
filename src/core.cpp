// ============================================================================
//  PDF / Word 页面导出工具 —— 核心逻辑（原生 Win32）
//  依赖：pdfium.dll（可选；缺失时只能处理扫描版 PDF，且不支持 Word）
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>
#include <utility>

#include "fpdfview.h"
#include "fpdf_edit.h"

#include "app.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ============================================================================
//  PDFium 动态绑定
// ============================================================================
#define PDF_FUNCS(X)                    \
    X(FPDF_InitLibrary)                 \
    X(FPDF_DestroyLibrary)              \
    X(FPDF_GetLastError)                \
    X(FPDF_LoadDocument)                \
    X(FPDF_CloseDocument)               \
    X(FPDF_GetPageCount)                \
    X(FPDF_LoadPage)                    \
    X(FPDF_ClosePage)                   \
    X(FPDF_GetPageWidthF)               \
    X(FPDF_GetPageHeightF)              \
    X(FPDFBitmap_CreateEx)              \
    X(FPDFBitmap_FillRect)              \
    X(FPDFBitmap_Destroy)               \
    X(FPDF_RenderPageBitmap)            \
    X(FPDFPage_CountObjects)            \
    X(FPDFPage_GetObject)               \
    X(FPDFPageObj_GetType)              \
    X(FPDFPageObj_GetBounds)            \
    X(FPDFImageObj_GetImageDataRaw)     \
    X(FPDFImageObj_GetImageFilterCount) \
    X(FPDFImageObj_GetImageFilter)      \
    X(FPDFImageObj_GetImageMetadata)

#define DECL_PDF(n) static decltype(&n) p_##n = nullptr;
PDF_FUNCS(DECL_PDF)
#undef DECL_PDF

static HMODULE g_pdfium = nullptr;

bool LoadPdfium(const std::wstring &dll)
{
    if (g_pdfium)
        return true;
    g_pdfium = LoadLibraryW(dll.c_str());
    if (!g_pdfium)
        return false;
#define BIND_PDF(n)                                     \
    p_##n = (decltype(&n))GetProcAddress(g_pdfium, #n);  \
    if (!p_##n)                                          \
    {                                                    \
        FreeLibrary(g_pdfium);                           \
        g_pdfium = nullptr;                              \
        return false;                                    \
    }
    PDF_FUNCS(BIND_PDF)
#undef BIND_PDF
    p_FPDF_InitLibrary();
    return true;
}

bool PdfiumReady() { return g_pdfium != nullptr; }

// ============================================================================
//  基础工具
// ============================================================================
std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring &w)
{
    if (w.empty())
        return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Fmt(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

std::wstring Trim(const std::wstring &s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos)
        return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t k = p.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? L"." : p.substr(0, k);
}

std::wstring FileName(const std::wstring &p)
{
    size_t k = p.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? p : p.substr(k + 1);
}

std::wstring Stem(const std::wstring &p)
{
    std::wstring n = FileName(p);
    size_t k = n.find_last_of(L'.');
    return (k == std::wstring::npos || k == 0) ? n : n.substr(0, k);
}

std::wstring ExtLower(const std::wstring &p)
{
    std::wstring n = FileName(p);
    size_t k = n.find_last_of(L'.');
    if (k == std::wstring::npos)
        return L"";
    std::wstring e = n.substr(k);
    for (size_t i = 0; i < e.size(); ++i)
        e[i] = towlower(e[i]);
    return e;
}

bool FileExists(const std::wstring &p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring &p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::wstring &p)
{
    if (DirExists(p))
        return true;
    return CreateDirectoryW(p.c_str(), nullptr) != 0;
}

bool WriteBytes(const std::wstring &path, const void *data, size_t n)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == n;
}

unsigned long long FileSize(const std::wstring &p)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    ZeroMemory(&d, sizeof(d));
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d))
        return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// ---- 日志：有窗口就投递到界面线程，没窗口就打到控制台（便于无界面调用）----
void Ctx::Log(const std::wstring &s)
{
    if (!hwnd)
    {
        fwprintf(stdout, L"%ls\n", s.c_str());
        fflush(stdout);
        return;
    }
    std::wstring *p = new std::wstring(s);
    if (!PostMessageW(hwnd, WM_APP_LOG, (WPARAM)p, 0))
        delete p;
}

void Ctx::Log(const std::string &s)
{
    Log(Utf8ToWide(s));
}

// ============================================================================
//  页码解析
// ============================================================================
bool ParsePages(const std::wstring &text, int total, std::vector<int> &out, std::wstring &err)
{
    out.clear();
    std::wstring t = Trim(text);
    if (t.empty() || t == L"全部" || t == L"*" || t == L"所有" || _wcsicmp(t.c_str(), L"all") == 0)
    {
        for (int i = 1; i <= total; ++i)
            out.push_back(i);
        return true;
    }

    std::vector<std::wstring> parts;
    std::wstring cur;
    for (size_t i = 0; i < t.size(); ++i)
    {
        wchar_t c = t[i];
        if (c == L',' || c == L'，' || c == L';' || c == L'；' || c == L'、' ||
            c == L' ' || c == L'\t')
        {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        }
        else
            cur.push_back(c);
    }
    if (!cur.empty())
        parts.push_back(cur);

    std::vector<int> pages;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const std::wstring &s = parts[i];
        size_t dash = s.find_first_of(L"-~～到至");
        if (dash != std::wstring::npos)
        {
            int a = _wtoi(s.substr(0, dash).c_str());
            int b = _wtoi(s.substr(dash + 1).c_str());
            if (a <= 0 || b <= 0) { err = L"看不懂的页码：" + s; return false; }
            if (a > b) { int tmp = a; a = b; b = tmp; }
            for (int k = a; k <= b; ++k) pages.push_back(k);
        }
        else
        {
            int a = _wtoi(s.c_str());
            if (a <= 0) { err = L"看不懂的页码：" + s; return false; }
            pages.push_back(a);
        }
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (pages[i] < 1 || pages[i] > total)
        {
            err = Fmt(L"页码超出范围（这份文档共 %d 页）：%d", total, pages[i]);
            return false;
        }
    }
    out = pages;
    return true;
}

std::wstring CompactPages(const std::vector<int> &pages)
{
    std::wstring s;
    size_t i = 0;
    while (i < pages.size())
    {
        size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1)
            ++j;
        if (!s.empty()) s += L",";
        if (j == i) s += std::to_wstring(pages[i]);
        else s += std::to_wstring(pages[i]) + L"-" + std::to_wstring(pages[j]);
        i = j + 1;
    }
    return s;
}

// ============================================================================
//  GDI+ 编码
// ============================================================================
static int GetEncoderClsid(const wchar_t *mime, CLSID *clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size)
        return -1;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo *info = (Gdiplus::ImageCodecInfo *)buf.data();
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; return (int)i; }
    return -1;
}

bool SaveImage(const std::wstring &path, void *bgra, int w, int h, int stride,
               bool jpg, int quality)
{
    Gdiplus::Bitmap bmp(w, h, stride, PixelFormat32bppARGB, (BYTE *)bgra);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;
    CLSID clsid;
    if (GetEncoderClsid(jpg ? L"image/jpeg" : L"image/png", &clsid) < 0)
        return false;
    if (jpg)
    {
        int q = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
        ULONG qv = (ULONG)q;
        Gdiplus::EncoderParameters ep;
        ep.Count = 1;
        ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
        ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ep.Parameter[0].Value = &qv;
        return bmp.Save(path.c_str(), &clsid, &ep) == Gdiplus::Ok;
    }
    return bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

// ============================================================================
//  Word -> PDF
// ============================================================================
// 递归删除目录（纯 Win32，不依赖 shell/COM）
bool RemoveTree(const std::wstring &dir)
{
    if (dir.empty() || !DirExists(dir))
        return true;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
                continue;
            std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(full);
            else
            {
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}

static std::wstring FindPowerShell()
{
    wchar_t sys[MAX_PATH] = {0};
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring p = std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (FileExists(p))
        return p;
    return L"powershell.exe";
}

static std::wstring PsQuote(const std::wstring &w)
{
    std::string u = WideToUtf8(w);
    std::string r = "'";
    for (size_t i = 0; i < u.size(); ++i)
    {
        r += u[i];
        if (u[i] == '\'')
            r += '\'';
    }
    r += "'";
    return Utf8ToWide(r);
}

static bool ConvertWordToPdf(const std::wstring &src, const std::wstring &dst,
                             const std::wstring &tmpDir, std::wstring &err)
{
    std::wstring ps1 = tmpDir + L"\\w2p.ps1";

    // 路径直接写进脚本。ps1 存成 UTF-8 带 BOM，PowerShell 5.1 才认中文路径。
    std::string body;
    body += "$ErrorActionPreference = \"Stop\"\r\n";
    body += "$src = " + WideToUtf8(PsQuote(src)) + "\r\n";
    body += "$dst = " + WideToUtf8(PsQuote(dst)) + "\r\n";
    body += "$word = $null\r\n";
    body += "try {\r\n";
    body += "    $word = New-Object -ComObject Word.Application\r\n";
    body += "    $word.Visible = $false\r\n";
    body += "    $word.DisplayAlerts = 0\r\n";
    body += "    $word.AutomationSecurity = 3\r\n";
    body += "    $doc = $word.Documents.Open($src, $false, $true)\r\n";
    body += "    $doc.ExportAsFixedFormat($dst, 17)\r\n";
    body += "    $doc.Close(0)\r\n";
    body += "    $doc = $null\r\n";
    body += "} finally {\r\n";
    body += "    if ($word) { try { $word.Quit() } catch { } }\r\n";
    body += "}\r\n";
    std::string withBom = "\xEF\xBB\xBF" + body;
    if (!WriteBytes(ps1, withBom.data(), withBom.size()))
    {
        err = L"无法写入临时脚本";
        return false;
    }

    std::wstring cmd = L"\"" + FindPowerShell() +
                       L"\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
                       ps1 + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        err = Fmt(L"启动 PowerShell 失败（错误 %lu）", GetLastError());
        return false;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 300000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(ps1.c_str());

    if (wait == WAIT_TIMEOUT)
    {
        err = L"Word 转换超时（5 分钟）。可能 Word 弹了对话框，或文档有问题。";
        return false;
    }
    if (!FileExists(dst) || FileSize(dst) == 0)
    {
        err = L"Word 转换失败。请确认本机装了 Microsoft Word，且文档未加密、未损坏。";
        return false;
    }
    return true;
}

bool IsWordExt(const std::wstring &ext)
{
    return ext == L".docx" || ext == L".doc" || ext == L".docm" ||
           ext == L".rtf" || ext == L".odt";
}

bool IsSupportedExt(const std::wstring &ext)
{
    return ext == L".pdf" || IsWordExt(ext);
}

// ============================================================================
//  页面分析
// ============================================================================
struct PageInfo
{
    FPDF_PAGEOBJECT obj = nullptr;
    double dpi = 0;
    double coverage = 0;
    int count = 0;
};

static PageInfo AnalyzePage(FPDF_PAGE page)
{
    PageInfo r;
    int n = p_FPDFPage_CountObjects(page);
    double pageArea = (double)p_FPDF_GetPageWidthF(page) * p_FPDF_GetPageHeightF(page);
    double bestArea = -1;
    for (int i = 0; i < n; ++i)
    {
        FPDF_PAGEOBJECT ob = p_FPDFPage_GetObject(page, i);
        if (!ob || p_FPDFPageObj_GetType(ob) != FPDF_PAGEOBJ_IMAGE)
            continue;
        r.count++;
        FPDF_IMAGEOBJ_METADATA md;
        ZeroMemory(&md, sizeof(md));
        if (!p_FPDFImageObj_GetImageMetadata(ob, page, &md))
            continue;
        float l = 0, b = 0, rr = 0, t = 0;
        if (!p_FPDFPageObj_GetBounds(ob, &l, &b, &rr, &t))
            continue;
        double area = (double)(rr - l) * (t - b);
        if (area > bestArea)
        {
            bestArea = area;
            r.obj = ob;
            r.dpi = md.horizontal_dpi > 0 ? md.horizontal_dpi : md.vertical_dpi;
            r.coverage = pageArea > 0 ? area / pageArea : 0.0;
        }
    }
    return r;
}

// ============================================================================
//  导出单个文档
// ============================================================================
int ExportOneFile(const std::wstring &src, const Params &pm, Ctx &ctx, std::wstring &err)
{
    std::wstring stem = Stem(src);
    std::wstring pdfPath = src;
    std::wstring tmpRoot, tmpPdf;
    bool word = IsWordExt(ExtLower(src));

    if (word && !PdfiumReady())
    {
        err = L"处理 Word 文档需要 pdfium.dll，但它不在程序旁边。";
        return -1;
    }

    if (word)
    {
        wchar_t tmp[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tmp);
        tmpRoot = std::wstring(tmp) + L"pdfex_" + std::to_wstring(GetTickCount());
        if (!EnsureDir(tmpRoot))
        {
            err = L"无法创建临时目录";
            return -1;
        }
        tmpPdf = tmpRoot + L"\\" + stem + L".pdf";
        ctx.Log(L"检测到 Word 文档，调用 Word 转换中…");
        if (!ConvertWordToPdf(src, tmpPdf, tmpRoot, err))
        {
            RemoveTree(tmpRoot);
            return -1;
        }
        ctx.Log(L"转换完成");
        pdfPath = tmpPdf;
    }

    std::string pdfA = WideToUtf8(pdfPath);
    FPDF_DOCUMENT doc = p_FPDF_LoadDocument(pdfA.c_str(), nullptr);
    if (!doc)
    {
        if (word) RemoveTree(tmpRoot);
        err = L"打不开这个 PDF（可能已损坏或被加密）。";
        return -1;
    }

    int total = p_FPDF_GetPageCount(doc);
    std::vector<int> pages;
    if (!ParsePages(pm.pages, total, pages, err))
    {
        p_FPDF_CloseDocument(doc);
        if (word) RemoveTree(tmpRoot);
        return -1;
    }

    std::wstring baseDir = pm.outDir.empty() ? ExeDir() : pm.outDir;
    std::wstring outDir = baseDir + L"\\" + stem;
    if (!EnsureDir(outDir))
    {
        p_FPDF_CloseDocument(doc);
        if (word) RemoveTree(tmpRoot);
        err = L"无法创建输出目录：" + outDir;
        return -1;
    }

    ctx.Log(Fmt(L"共 %d 页，导出第 %s 页", total, CompactPages(pages).c_str()));
    ctx.Log(L"输出目录：" + outDir);

    int done = 0;
    for (size_t idx = 0; idx < pages.size(); ++idx)
    {
        int pno = pages[idx];
        FPDF_PAGE page = p_FPDF_LoadPage(doc, pno - 1);
        if (!page)
        {
            ctx.Log(Fmt(L"  第%d页读取失败，跳过", pno));
            continue;
        }

        PageInfo info = AnalyzePage(page);
        double dpi = pm.dpi > 0 ? (double)pm.dpi : (info.dpi > 0 ? info.dpi : 300.0);
        if (dpi < 72) dpi = 72;
        if (dpi > 1200) dpi = 1200;

        std::wstring name = Fmt(L"%s-第%d页.%s", stem.c_str(), pno, pm.jpg ? L"jpg" : L"png");
        std::wstring path = outDir + L"\\" + name;

        bool ok = false;
        std::wstring how;

        // 扫描页：直接抽原始 JPEG，无损且瞬时
        if (pm.jpg && pm.quality >= 90 && info.count == 1 && info.obj && info.coverage >= 0.95)
        {
            char filter[64] = {0};
            int fc = p_FPDFImageObj_GetImageFilterCount(info.obj);
            if (fc > 0)
                p_FPDFImageObj_GetImageFilter(info.obj, 0, filter, (unsigned long)sizeof(filter) - 1);
            if (strcmp(filter, "DCTDecode") == 0)
            {
                unsigned long need = p_FPDFImageObj_GetImageDataRaw(info.obj, nullptr, 0);
                if (need > 0)
                {
                    std::vector<unsigned char> raw(need);
                    unsigned long got = p_FPDFImageObj_GetImageDataRaw(info.obj, raw.data(), need);
                    if (got == need && WriteBytes(path, raw.data(), need))
                    {
                        ok = true;
                        how = L"无损提取";
                    }
                }
            }
        }

        if (!ok)
        {
            int w = (int)(p_FPDF_GetPageWidthF(page) / 72.0 * dpi + 0.5);
            int h = (int)(p_FPDF_GetPageHeightF(page) / 72.0 * dpi + 0.5);
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            int stride = w * 4;
            std::vector<unsigned char> buf((size_t)stride * h, 255);
            FPDF_BITMAP bmp = p_FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, buf.data(), stride);
            if (bmp)
            {
                p_FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
                p_FPDF_RenderPageBitmap(bmp, page, 0, 0, w, h, 0, 0);
                if (SaveImage(path, buf.data(), w, h, stride, pm.jpg, pm.quality))
                {
                    ok = true;
                    how = Fmt(L"渲染 %ddpi", (int)(dpi + 0.5));
                }
                p_FPDFBitmap_Destroy(bmp);
            }
        }

        p_FPDF_ClosePage(page);

        if (ok)
        {
            double mb = FileSize(path) / 1048576.0;
            ctx.Log(Fmt(L"  [%d/%d] %s   %s   %.2f MB", (int)idx + 1, (int)pages.size(),
                        name.c_str(), how.c_str(), mb));
            done++;
        }
        else
        {
            ctx.Log(Fmt(L"  第%d页导出失败", pno));
        }
    }

    p_FPDF_CloseDocument(doc);
    if (word)
    {
        RemoveTree(tmpRoot);
    }
    return done;
}

// ============================================================================
//  后台线程
// ============================================================================
DWORD WINAPI WorkerProc(LPVOID lp)
{
    Job *job = (Job *)lp;
    Params pm = job->pm;
    HWND hwnd = job->hwnd;
    delete job;

    Ctx ctx;
    ctx.hwnd = hwnd;

    int okFiles = 0, images = 0;
    bool multi = pm.files.size() > 1;
    std::wstring baseOut = pm.outDir.empty() ? ExeDir() : pm.outDir;
    std::wstring lastOut;

    for (size_t i = 0; i < pm.files.size(); ++i)
    {
        const std::wstring &f = pm.files[i];
        if (multi)
        {
            ctx.Log(L"");
            ctx.Log(Fmt(L"===== [%d/%d] %s =====", (int)i + 1, (int)pm.files.size(),
                        FileName(f).c_str()));
        }
        std::wstring err;
        int n = ExportOneFile(f, pm, ctx, err);
        if (n >= 0)
        {
            okFiles++;
            images += n;
            lastOut = baseOut + L"\\" + Stem(f);
        }
        else
        {
            std::wstring *p = new std::wstring(L">>> 这个文件失败：" + FileName(f) + L"（" + err + L"）");
            PostMessageW(hwnd, WM_APP_LOGERR, (WPARAM)p, 0);
        }
    }

    DoneInfo *di = new DoneInfo();
    di->okFiles = okFiles;
    di->images = images;
    di->total = (int)pm.files.size();
    di->lastOut = lastOut;
    PostMessageW(hwnd, WM_APP_DONE, (WPARAM)di, 0);
    return 0;
}
