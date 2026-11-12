[English](../en_us/ci.md) · 中文

[文档首页](README.md) › 持续集成

# 持续集成

对 `main` 的改动、拉取请求以及手动运行都使用 GitHub Actions。本页说明每个
工作流验证什么、如何在本地复现这些检查，以及合并保护如何把它们串起来。命令本身
——Forge 引导、`./make` 目标、测试框架——见[构建、测试与打包](build-and-test.md)。

## 触发条件

| 事件 | 工作流 |
| --- | --- |
| 推送到 `main` | [ci.yml](../../.github/workflows/ci.yml) 与 [codeql.yml](../../.github/workflows/codeql.yml) |
| 任意分支上的 `pull_request` | `ci.yml` 与 `codeql.yml` |
| `workflow_dispatch`（手动运行） | `ci.yml` 与 `codeql.yml` |
| `schedule` | 仅 `codeql.yml`（每周） |

工作流对仓库只有只读访问权限，唯一例外是 CodeQL 任务发布代码扫描结果所需的权限。
拉取请求中的代码绝不会通过 `pull_request_target` 运行，且不保留检出凭据。

## 检查项

| 检查 | 验证内容 |
| --- | --- |
| 构建与测试（gcc-14 / clang-19） | 在 Ubuntu 24.04 上用 GCC 14 与 Clang 19 做真实构建，运行完整测试套件、ASan、UBSan 与泄漏检测 |
| 编译器警告 | 每个生产 C 翻译单元都以 `-Wall -Wextra -Werror` 编译通过 |
| GCC 静态分析 | 用 `-fanalyzer` 检查生产翻译单元；出现诊断即令任务失败 |
| 工作流与密钥检查 | 对完整 Git 历史运行 Actionlint、ShellCheck、空白错误检查与 Gitleaks，输出中的密钥已脱敏 |
| CodeQL（C/C++） | 手动构建追踪，使用 `security-and-quality` 查询；发现显示在 Security 页与拉取请求中 |
| CI | 稳定的必需检查：任何构建、分析或质量任务失败或被取消时它都会失败 |

GCC 分析使用 `-O1 -fno-analyzer-state-merge`：GCC 14 默认的状态合并会在这份
代码上产生虚假的分配/清理诊断。没有屏蔽任何诊断类别。分析器失败应当被调查，
而不是用 `continue-on-error` 绕过。

所有外部 Action 都固定到不可变的提交 SHA。Dependabot 每周提出 GitHub Actions
更新。Actionlint 与 Gitleaks CLI 的版本和 SHA-256 校验和固定在
[.github/scripts/install-tools.sh](../../.github/scripts/install-tools.sh) 中；
升级它们时要同时更新这两处值。

## CodeQL

CodeQL 面向 C/C++ 配置，使用手动构建追踪与 `security-and-quality` 查询集。
它分析由固定的 Forge 工具链构建出的代码，把发现发布到 Security 页，并在拉取
请求中报告。

CodeQL 还会在每周一 11:17 Asia/Shanghai（03:17 UTC）运行。任务状态为成功只表示
分析完成，**并不**表示没有告警。请用代码扫描合并规则来强制告警级别——见
[启用合并保护](#启用合并保护)。

## 启用合并保护

仅靠工作流文件无法启用分支保护；这些仓库设置是维护者的一次性配置。首次成功
运行后，为 `main` 创建分支规则集：

1. 要求拉取请求，且 `CI` 与 `CodeQL (C/C++)` 状态检查通过。
2. 要求分支在合并前保持最新。
3. 添加“Require code scanning results”，选择 CodeQL，并阻止高危/严重安全告警
   与错误级质量告警。在分诊现有发现后可以收紧这些阈值。
4. 使用仓库中检入的高级 CodeQL 工作流，而不要启用重复的默认设置扫描。

CI 权限、检查项与依赖固定点的改动仍然值得人工评审。

## 本地复现

安装 Linux 构建依赖与 C23 编译器（GCC 14+ 或 Clang 19+）。运行 `configure.sh`
前请保存本地 `build.conf` 修改：它会更新编译器选择，并在该文件中追加
`sanitize=1`。

```sh
CC=gcc bash .github/scripts/configure.sh  # 或 CC=clang
CC=gcc bash .github/scripts/check-c.sh
CC=gcc bash .github/scripts/check-c.sh analyze
```

`analyze` 请使用 GCC。运行时检查需设置：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

构建依赖是 Forge 提交
`b6826ba762a1e9e337895e01ee604b943c682d23`：CI 把它检出到 `.ci-deps/forge`，
并在不安装系统文件的情况下引导。PCRE2 来自 Ubuntu 的 `libpcre2-dev` 包。

本地有已引导的 Forge 检出时：

```sh
export PATH="/path/to/forge/build/output:$PATH"
export CPATH="/path/to/forge/build/output/include"
forge .
./make
./make test -j 2
```

CI 还会安装到临时前缀，用 `pkg-config` 把现有正则测试编译并链接到**已安装的
共享库**后运行，并检查生成的 Debian 包。这样除源码级行为外，还验证了公开头文件、
导出符号、包元数据与 PCRE2 链接。对应命令在 `ci.yml` 中；它们使用的布局记录在
[构建、测试与打包](build-and-test.md)。

## 固定的 Forge 提交

两个工作流文件都把 Forge 固定到提交
`b6826ba762a1e9e337895e01ee604b943c682d23`，检出到 `.ci-deps/forge`，并用
`sh .ci-deps/forge/bootstrap.sh --no-test` 在不安装系统文件的情况下引导。更换
Forge 版本时，必须在**两个**工作流文件中更新这个经过评审的固定点。它有意独立于
Forge 不断变动的 `main` 分支，也不会被 Dependabot 更新。

## 评审与失败处理

打开失败的检查，查看编译器诊断、内存检查器堆栈或密钥扫描位置。构建失败会保留
依赖与命令元数据七天；Actions 日志包含完整的构建/测试输出。CodeQL 告警会附带
受影响的代码行，并在可用时给出数据流。

自动化覆盖机械性回归和许多常见的内存安全缺陷。人工评审应集中在需求、API 契约、
所有权、架构以及这些工具报告的问题上。

---

[← 上一页](contributing.md) · [文档首页](README.md)
