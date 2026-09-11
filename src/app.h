// ============================================================================
//  app.h —— 公共声明
// ============================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <string>
#include <vector>

// 自定义消息：wparam 一律是 new std::wstring* / new DoneInfo*，由界面负责释放
#define WM_APP_LOG (WM_APP + 1)    // 普通日志
#define WM_APP_LOGERR (WM_APP + 2) // 错误日志（红色）
#define WM_APP_DONE (WM_APP + 3)   // 整批完成，wparam = DoneInfo*

struct Params
{
    std::vector<std::wstring> files;
    std::wstring pages;
    std::wstring outDir;
    bool jpg = true;
    int quality = 85;
    int dpi = 0; // 0 = 自动识别
};

struct DoneInfo
{
    int okFiles = 0;
    int images = 0;
    int total = 0;
    std::wstring lastOut;
};

struct Ctx
{
    HWND hwnd = nullptr;
    void Log(const std::wstring &s);
    void Log(const std::string &s);
};

struct Job
{
    Params pm;
    HWND hwnd = nullptr;
};

// ---- 工具函数 ----
std::wstring Utf8ToWide(const std::string &s);
std::string WideToUtf8(const std::wstring &w);
std::wstring Fmt(const wchar_t *fmt, ...);
std::wstring Trim(const std::wstring &s);
std::wstring ExeDir();
std::wstring FileName(const std::wstring &p);
std::wstring Stem(const std::wstring &p);
std::wstring ExtLower(const std::wstring &p);
bool FileExists(const std::wstring &p);
bool DirExists(const std::wstring &p);
bool EnsureDir(const std::wstring &p);
bool WriteBytes(const std::wstring &path, const void *data, size_t n);
bool RemoveTree(const std::wstring &dir);
unsigned long long FileSize(const std::wstring &p);

// ---- 业务 ----
bool ParsePages(const std::wstring &text, int total, std::vector<int> &out, std::wstring &err);
std::wstring CompactPages(const std::vector<int> &pages);
bool SaveImage(const std::wstring &path, void *bgra, int w, int h, int stride,
               bool jpg, int quality);
bool IsWordExt(const std::wstring &ext);
bool IsSupportedExt(const std::wstring &ext);
bool LoadPdfium(const std::wstring &dll);
bool PdfiumReady();
int ExportOneFile(const std::wstring &src, const Params &pm, Ctx &ctx, std::wstring &err);
DWORD WINAPI WorkerProc(LPVOID lp);
