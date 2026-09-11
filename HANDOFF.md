# 跨对话交接

> 换一个对话 / 换一个 agent 时，先读这份。目标是**不用重新问用户任何事**就能接手。

**最后更新**：2026-09-12

---

## 一句话现状

**已完成并发布。** 32 位 + 64 位双架构构建，代码已推送，交付包待打包。

---

## 当前状态

| 项 | 状态 |
|---|---|
| 核心功能 | ✅ 可用（PDF 导出、Word 转换、批量、无损提取、自动 dpi） |
| 界面 | ✅ 可用（Win32，含拖放、文件列表、日志着色、「关于」入口） |
| 打包 | ✅ 双架构 exe + pdfium.dll |
| 交付包 | ⬜ 未做（应放 `OH-WorkSpace\工具仓库\PDF页面导出工具\`） |
| 仓库 | ✅ https://github.com/Thesouth-EricWang/pdf-page-exporter |
| 版本 | 1.1.0 |

---

## 已经做过什么（按时间倒序）

1. 补上版本号（`APP_VERSION` + `version.rc`）、窗口标题带版本、「关于」按钮（点确定打开仓库）
2. 增加 32 位构建；`build.bat` 支持架构参数
3. 修复：递归删目录改用纯 Win32（`SHFileOperation` 静默失败）；Word 转换参数写进 `.ps1`（环境块报 error 87）
4. 建立 git 仓库并推送（含 exe 和 dll，方便直接下载）
5. 双架构构建、验证无损提取（输出与源内嵌图逐字节一致）

**前史**：这个工具的第一版是 Python + tkinter + PyMuPDF，功能完整但打包 53 MB 且窗口不显示，因此整体重写为原生。Python 版仍在 `OH-WorkSpace\pdf-page-exporter\`，用户自己还在用，**不要动**。

---

## 下一步该做什么

- [ ] 打交付包：`工具仓库\PDF页面导出工具\PDF页面导出工具.zip`，内含中文名 exe + pdfium.dll + 使用说明.txt
- [ ] 按 `tool-forge` skill 的 `templates/使用说明.txt` 写使用说明（必须含 SmartScreen 绕过方法）
- [ ] 可选项：加图标（`version.rc` 里加 `ICON`）、加"输出到固定子目录"开关

---

## 关键决策（别推翻，除非有明确理由）

- **原生 C++ 而非 Python**：体积 53MB → 7.2MB，且零依赖。详见 `ARCHITECTURE.md` 决策 1
- **exe + dll 两个文件而非单 exe**：自解压必然往 `%TEMP%` 吐文件，那是"残留"的来源。决策 2
- **压缩档 ≥ 90 时走无损提取**：无损且快；低于 90 走渲染，因为用户明确要压小。决策 3
- **PDFium 动态加载**：缺 dll 时不至于整个起不来。决策 4

---

## 雷区 / 已知坑

- **PDFium 调用必须带 `p_` 前缀**，否则链接失败
- **编译必须带 `/utf-8`**，否则中文源码被 GBK 解析吃坏代码结构
- **版本号有两处**（`main.cpp` + `version.rc`），要同步改
- **`build.bat` 注释只能英文**；`.ps1` 若含中文必须 UTF-8 带 BOM
- **不要相信“程序说它删了临时目录”**，去 `%TEMP%` 看一眼（`pdfex_*` 应该没有）

**打包预判**：已是最终形态。若将来要换成"单文件发布"，只能靠外部 zip 压缩，不要引入自解压壳。

---

## 环境依赖

- **构建需要**：Visual Studio 2019/2022 + C++ 工作负载 + Windows SDK。运行 `build.bat`（默认 x64）或 `build.bat x86`
- **运行需要**：无。Windows 7 及以上
- **处理 Word 需要**：本机装 Microsoft Word
- **推送需要**：系统 Git Credential Manager 的凭证。⚠️ `gh` 的细粒度 PAT **没有 Contents 写权限**（能建仓库、不能推代码）；且 `gh auth setup-git` 会把 gh 助手插到 GCM 前面导致 push 403，用完要撤回

---

## 未决问题 / 待用户确认

- 交付包是否也要放一份到 GitHub Releases（方便别人直接下载 exe，不用下整个仓库，仓库现在 14 MB）
