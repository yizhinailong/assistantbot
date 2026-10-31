# Doxygen：生成 API 文档

[English](../en/doxygen.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

Doxygen 从源码注释生成类、函数、参数和返回值说明，并将 Markdown 使用指南纳入同一站点。
项目配置位于根目录的 `Doxyfile`，按 Doxygen 1.18.0 的模板生成，并已用该版本验证。
安装方法见 [Doxygen 官方安装说明](https://www.doxygen.nl/manual/install.html)。

## 生成与查看

从仓库根目录执行：

```sh
just docs
```

它先创建 `build/doxygen`，再执行 `doxygen Doxyfile`。Doxygen 不会递归创建输出路径，不使用 just 时需要手动创建该目录：

```sh
mkdir -p build/doxygen
doxygen Doxyfile
```

见 [官方生成说明](https://www.doxygen.nl/manual/starting.html#step2)。
使用浏览器打开 `build/doxygen/html/index.html`。
修改源码注释或 Markdown 后，再次运行 `just docs` 即可更新。
输出位于 Git 已忽略的 `build/` 下；不带参数的 `just clean` 会一并删除。

较旧的 Doxygen（例如 1.15）也能生成。它会对 1.18 新增的配置项提示 `ignoring unsupported tag`，可以忽略。

## GitHub Pages 自动发布

[Documentation 工作流](../../.github/workflows/docs.yml) 在推送到 `master` 时运行，前提是修改了以下内容之一：

- `src/`、`docs/`；
- `README.md`、`README.zh-CN.md`、`AGENTS.md`、`Doxyfile`；
- 工作流本身。

它也支持从 Actions 页面手动运行；只有 `master` 分支的运行会发布网站。

构建任务的步骤：

1. 在 `ubuntu-24.04` 上下载 Doxygen 1.18.0 的官方 Linux 二进制包，按 [官方下载页](https://www.doxygen.nl/download.html) 的 SHA-256 校验。
2. 创建输出目录，生成文档。生成时追加 `WARN_AS_ERROR = FAIL_ON_WARNINGS`，任何注释警告都会让工作流失败，见下文。
3. 将 `build/doxygen/html/` 上传为 Pages artifact。

独立的部署任务在构建成功后发布该 artifact。生成的 HTML 通过工作流产物传递，不提交到 Git；文档构建也不需要 vcpkg、drogon 或 C++ 工具链。
升级 Doxygen 时，应同时更新工作流中的版本与校验和。

首次启用时，在 GitHub 仓库中完成以下配置：

1. 打开 **Settings → Pages**。
2. 在 **Build and deployment → Source** 中选择 **GitHub Actions**。
3. 将工作流及相关文档推送到 `master`，或在 **Actions → Documentation → Run workflow** 中选择 `master` 并运行。
4. 等待构建与部署任务成功，在 **Settings → Pages** 或工作流的 `github-pages` 部署环境中查看网站地址。

仓库需允许 GitHub Actions 运行。工作流使用内置的 `GITHUB_TOKEN`：构建任务只有 `contents: read`，部署任务声明 `pages: write` 和 `id-token: write`，无需添加 PAT、Secrets 或修改仓库的默认令牌写权限。

关于 `github-pages` 部署环境：

- 如果限制了部署分支，应允许 `master`；
- 如果配置了必需审批者，部署会等待审批。

发布源、权限和环境的要求见 [GitHub Pages 自定义工作流说明](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)。

## 项目配置

下面是影响本项目文档生成的主要配置，可在 Doxyfile 中搜索同名选项修改。
相对路径以运行 Doxygen 时的工作目录为准，因此应始终从仓库根目录运行。

| 配置 | 当前值 | 作用 |
| --- | --- | --- |
| `PROJECT_NAME` | `assistantbot` | 页面中的项目名称 |
| `PROJECT_BRIEF` | 项目的英文简介 | 页面顶部的项目说明 |
| `OUTPUT_DIRECTORY` | `build/doxygen` | 生成文件的存放目录 |
| `OUTPUT_LANGUAGE` | `English` | 使用英文导航和内置标题，与英文首页一致 |
| `INPUT_ENCODING` | `UTF-8` | 源码与 Markdown 的字符编码 |
| `INPUT` | `src README.md README.zh-CN.md docs AGENTS.md` | 源码、中英文 README、使用指南与仓库规范 |
| `FILE_PATTERNS` | `*.hpp *.cpp *.md` | 扫描的文件扩展名 |
| `RECURSIVE` | `YES` | 递归扫描输入目录 |
| `STRIP_FROM_INC_PATH` | `src` | 类页面显示相对 `src/` 的头文件路径，例如 `controllers/onebot/assistant_bot.hpp`，与源码中的写法一致 |
| `EXCLUDE_SYMBOLS` | `ADD_METHOD_TO WS_PATH_ADD` | 避免 drogon 的路由宏被当作每个控制器里未写文档的成员函数 |
| `USE_MDFILE_AS_MAINPAGE` | `README.md` | 使用英文 README 作为首页；简体中文版 `README.zh-CN.md` 生成为单独的页面 |
| `SOURCE_BROWSER` | `YES` | 生成可跳转的源码页面 |
| `GENERATE_HTML` | `YES` | 生成浏览器可查看的文档 |
| `GENERATE_LATEX` | `NO` | 不生成 LaTeX 文档 |
| `GENERATE_TREEVIEW` | `YES` | 显示树状导航 |
| `HAVE_DOT` | `NO` | 默认无需安装 Graphviz |

`OUTPUT_LANGUAGE` 只决定导航和内置标题的语言，不翻译页面正文：首页、源码注释和 `docs/en/` 是英文，`README.zh-CN.md` 与 `docs/zh-CN/` 是简体中文。
新增使用指南时，在 `docs/en/` 和 `docs/zh-CN/` 下各写一份同名文件，并加入两边的文档索引（本目录的见 [文档索引](README.md)）。

查看与 Doxygen 默认配置不同的选项：

```sh
doxygen -x Doxyfile
```

完整选项含义见 [官方配置参考](https://www.doxygen.nl/manual/config.html)。

## 扫描范围

- `INPUT` 显式列出源码（`src/`）和说明文件，不扫描构建目录、`tests/` 中的测试程序或 `scripts/`。
- 所有类型都只在应用内部使用：`src/controllers/` 中的 HTTP 控制器和 bot 插件（命名空间 `controllers`），`src/workflow/` 中 bot 的对话流程（命名空间 `workflow`），以及 `src/common/` 中的文本处理函数（命名空间 `common`）。
- `.cpp` 中匿名命名空间里的辅助函数不生成文档（`EXTRACT_ANON_NSPACES = NO`），但可以在源码页面中查看。
- Doxygen 直接解析头文件和注释，不需要先构建，也不读取 vcpkg 中的 drogon 头文件。`CLANG_ASSISTED_PARSING` 保持 `NO`，配置中不绑定本机编译器或依赖路径。
- 生成文档不能代替编译和测试。

## 文档警告

保留以下设置，用于发现缺失或不匹配的注释：

```ini
EXTRACT_ALL           = NO
EXTRACT_PRIVATE       = NO
WARNINGS              = YES
WARN_IF_UNDOCUMENTED  = YES
WARN_IF_DOC_ERROR     = YES
WARN_AS_ERROR         = NO
```

当前源码没有注释警告。Documentation 工作流从标准输入追加 `WARN_AS_ERROR = FAIL_ON_WARNINGS`，出现任何警告即失败：

```sh
( cat Doxyfile; echo 'WARN_AS_ERROR = FAIL_ON_WARNINGS' ) | doxygen -
```

Doxyfile 本身保持 `NO`，因为较旧的 Doxygen 会对未知配置项告警，本地生成不应因此失败。

几个常见的警告来源：

- 新增的公开类型、函数或成员缺少注释。
- 声明与定义的注释块不一致。项目规范要求在定义处重复声明的注释；两者不同时，Doxygen 会报告 `has multiple @param documentation sections`。
- Markdown 链接指向未纳入 `INPUT` 的文件。

`EXTRACT_ALL = YES` 会将未写文档的实体也视为已文档化，并关闭缺失文档警告，因此需要检查注释完整性时应保留 `NO`。

Markdown 中提到注释标签时使用行内代码，例如 `@param`，避免被解释为文档命令。
使用指南中的 Markdown 页面链接会转换为生成站点内的链接；指向未纳入文档输入的配置、脚本或工作流文件的相对链接，仍需在源码仓库中查看。

## 可选的关系图

需要 Graphviz 绘制类关系图时，先安装 Graphviz，确认 `dot -V` 可执行，再设置：

```ini
HAVE_DOT = YES
```

默认 HTML 文档和源码浏览不依赖 Graphviz。
