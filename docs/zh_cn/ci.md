[English](../en_us/ci.md) · 中文

[文档首页](README.md) › 持续集成

# 持续集成

对 `main` 的改动、拉取请求与手动运行都使用 GitHub Actions。本页介绍工作流、
本地复现与合并保护；构建命令、`./make` 目标与测试框架见
[构建、测试与打包](build-and-test.md)。

## 触发条件

| 事件 | 工作流 |
| --- | --- |
| 推送到 `main` | [ci.yml](../../.github/workflows/ci.yml) 与 [codeql.yml](../../.github/workflows/codeql.yml) |
| 任意分支上的 `pull_request` | `ci.yml` 与 `codeql.yml` |
| 按路径过滤的 `pull_request` | [write-safety.yml](../../.github/workflows/write-safety.yml)（`src/sx_core.c`、`tests/write_overflow*`、其脚本）、[search-performance.yml](../../.github/workflows/search-performance.yml)（`src/sx_core.c`、其脚本）、[benchmarks.yml](../../.github/workflows/benchmarks.yml)（`benchmarks/**`）、[benchmark-report.yml](../../.github/workflows/benchmark-report.yml)（仅其工作流文件） |
| `workflow_dispatch`（手动运行） | `ci.yml`、`codeql.yml`、`write-safety.yml`、`search-performance.yml`、`benchmarks.yml`、`benchmark-report.yml` |
| `schedule` | 仅 `codeql.yml`（每周） |

工作流对仓库只有只读权限，唯一例外是 CodeQL 发布代码扫描结果所需的权限。
拉取请求代码绝不会通过 `pull_request_target` 运行，且不保留检出凭据。

## 检查项

| 检查 | 验证内容 |
| --- | --- |
| 构建与测试（gcc-14 / clang-19） | 在 Ubuntu 24.04 上用 GCC 14 与 Clang 19 做真实构建，运行完整测试套件、ASan、UBSan 与泄漏检测 |
| 编译器警告 | 每个生产 C 翻译单元都以 `-Wall -Wextra -Werror` 编译通过 |
| GCC 静态分析 | 用 `-fanalyzer` 检查生产翻译单元；出现诊断即令任务失败 |
| 工作流与密钥检查 | 对完整 Git 历史运行 Actionlint、ShellCheck、空白错误检查与 Gitleaks，输出中的密钥已脱敏 |
| CodeQL（C/C++） | 手动构建追踪，使用 `security-and-quality` 查询；发现显示在 Security 页与拉取请求中 |
| Write safety 与成本 | 在不可变旧实现上复现写入溢出回归（该实现必须出现 sanitizer 失败），用 GCC 14 与 Clang 19 运行两种修复配置，并与 SDS、bstrlib 比较等长写入成本 |
| Search performance | 针对 `src/sx_core.c` 改动，用 GCC 与 Clang 做改动前后查找基准 |
| 经审计的基准 | 当 `benchmarks/` 变化时重跑固定版本的 StringX / SDS / bstrlib 基准 |
| 基准结果报告 | 把手动保存的基准产物渲染成报告 |
| CI | 稳定的必需检查：任何构建、分析或质量任务失败或被取消时它都会失败 |

四个审计/基准工作流按路径过滤，独立于聚合的 `CI` 检查；只有分支保护显式要求
时它们才参与合并门禁。

GCC 分析使用 `-O1 -fno-analyzer-state-merge`：GCC 14 默认的状态合并会在这份
代码上产生虚假的分配/清理诊断。未屏蔽任何诊断类别；分析器失败应调查，而不是
用 `continue-on-error` 绕过。

外部 Action 固定到不可变的提交 SHA；Dependabot 每周提出更新。Actionlint 与
Gitleaks CLI 的版本和 SHA-256 校验和固定在
[.github/scripts/install-tools.sh](../../.github/scripts/install-tools.sh) 中；
升级时两处值要同时更新。

## CodeQL

CodeQL 面向 C/C++，使用手动构建追踪与 `security-and-quality` 查询。它分析由
固定工具链构建的代码，把发现发布到 Security 页与拉取请求。

CodeQL 还在每周一 11:17 Asia/Shanghai（03:17 UTC）运行。成功只表示分析完成，
**并不**表示没有告警；用代码扫描合并规则强制告警级别——见
[启用合并保护](#启用合并保护)。

## 启用合并保护

仅靠工作流文件无法启用分支保护；这些是维护者的一次性设置。首次成功运行后，
为 `main` 创建分支规则集：

1. 要求拉取请求，且 `CI` 与 `CodeQL (C/C++)` 状态检查通过。
2. 要求分支在合并前保持最新。
3. 添加“Require code scanning results”，选择 CodeQL，阻止高危/严重安全告警
   与错误级质量告警；分诊现有发现后可收紧阈值。
4. 使用仓库中检入的高级 CodeQL 工作流，而不要启用重复的默认设置扫描。

CI 权限、检查项与依赖固定点的改动仍然值得人工评审。

## 本地复现

安装 Linux 构建依赖与 C23 编译器（GCC 14+ 或 Clang 19+）。运行 `configure.sh`
前保存本地 `build.conf` 修改：它会更新编译器选择并追加 `sanitize=1`。

```sh
CC=gcc bash .github/scripts/configure.sh  # 或 CC=clang
CC=gcc bash .github/scripts/check-c.sh
CC=gcc bash .github/scripts/check-c.sh analyze
```

`analyze` 用 GCC；运行时检查设置：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

CI 将 Forge 固定到经过评审的提交（检出到 `.ci-deps/forge`）；两个工作流文件
包含该固定点与相关命令，更换时须同步更新。PCRE2 来自 Ubuntu 的
`libpcre2-dev` 包。已安装 Forge 时：

```sh
forge .
./make
./make test -j 2
```

专项审计脚本位于 `.github/scripts/`（`check-write-safety.py`、
`compare-search.py`）；`benchmarks/` 自带基准框架与复跑说明。

CI 还会安装到临时前缀，用 `pkg-config` 把现有正则测试编译并链接到**已安装的
共享库**后运行，并检查生成的 Debian `.deb`。这样除源码级行为外，还验证公开头文件、
导出符号、包元数据与 PCRE2 链接。命令在 `ci.yml` 中，布局见
[构建、测试与打包](build-and-test.md)。

## 评审与失败处理

打开失败的检查，查看编译器诊断、内存检查器堆栈或密钥扫描位置。构建失败保留依赖
与命令元数据七天；Actions 日志包含完整输出。CodeQL 告警附带受影响的代码行，
并在可用时给出数据流。

自动化覆盖机械性回归与许多内存安全缺陷；人工评审应聚焦需求、API 契约、所有权、
架构以及工具报告的问题。

---

[← 上一页](contributing.md) · [文档首页](README.md)
