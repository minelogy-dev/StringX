[English](../en_us/contributing.md) · 中文

[文档首页](README.md) › 贡献指南

# 贡献指南

String X 是一个契约详细的小型 C23 库，请保证文档、测试、改动的同步。本页说明
工作流程与项目约定。

## 工作流程

1. Fork 仓库并从 `main` 创建分支，分支名体现改动内容
   （`fix/split-end-guard`、`docs/build-targets`）。
2. 完成改动，包括行为测试与中英文文档更新。
3. 提交拉取请求前先运行本地检查：

   ```sh
   ./make test -j 2
   bash .github/scripts/check-c.sh
   bash .github/scripts/check-c.sh analyze   # 仅 GCC
   ```

4. 向 `main` 提交拉取请求，说明行为变化、对应测试与更新的文档页。
5. 等待必需检查：`main` 的合并保护要求 `CI` 与 `CodeQL (C/C++)` 通过且分支
   保持最新，见[持续集成](ci.md)。按路径过滤的 write-safety、
   search-performance 与 benchmark 工作流也会在匹配的改动上运行。
6. 用追加提交回应评审意见；让拉取请求保持聚焦。

`bash .github/scripts/configure.sh` 可以复现 CI 的编译器选择，但会重写
`build.conf` 中的 `compiler=`、`compiler_path=` 与 `sanitize=`；请先保存本地
修改。

## 契约优先的文档规则

行为边界记录在两处：

- 公开头文件 `include/sx.h` 与 `include/sx_core.h` 的注释；
- 每个 `tests/*.c` 顶部的 `Behavior boundaries` 注释，含
  `In scope (contract)` 与 `Out of contract (not exercised)` 列表。

行为变化要同时更新：

1. 受影响函数的头文件注释；
2. 测试实现及其行为边界注释；
3. `docs/en_us/` 与 `docs/zh_cn/` 的中英文页面。

不要让行为未文档化或停留在旧描述。若来源矛盾，运行时以测试为准，签名以头文件
为准，构建与打包以 `build.c` 为准——应报告真正的矛盾，而非悄悄取舍。

## 代码风格

- C23；库与测试都以 `-std=c23` 构建。
- 两空格缩进；左花括号与语句同行。
- 遵循现有命名与分层：核心在 `src/sx_core.c`，正则在 `src/sx_adv.c`，分配在
  `src/sx_raw.c`。不要在 `sx.h`/`sx_core.h` 之外新增公开函数。
- 偏移与长度用 `size_t`，沿用现有哨兵约定（`(size_t)-1`、`ref == NULL` 的
  视图、NULL 数组）。
- 把警告当作错误：`./make check` 使用 `-Wall -Wextra -Wpedantic -Werror`，
  GCC 还加 `-fanalyzer`；CI 的直接编译检查使用
  `-std=c23 -O1 -Wall -Wextra -Werror`。不要屏蔽诊断；修正代码或在评审中
  解释分析器的发现。

## 测试

每个行为改动都需要测试。`tests/` 下的测试是自包含程序，直接编译库源码，并在
头部 DSL（`// Include`、`// Source`、`// Link`、`// Library`、`// Option`）中
声明依赖；DSL 与示例见[构建、测试与打包](build-and-test.md)。

新增或修改测试时：

- 保持 `Behavior boundaries` 注释准确——它是测试执行的契约，包括
  `Out of contract (not exercised)`；
- 断言可观察结果（字节、长度、偏移、哨兵），而不只是非 NULL；
- 每个缓冲区用 `sx_free`、每个返回数组用 `free` 释放；测试套件在
  AddressSanitizer、UBSan 与 LeakSanitizer 下运行，泄漏或越界都会失败；
- 在测试头部保留内存检查器选项与 `-fno-omit-frame-pointer`，使本地与 CI
  运行一致；
- 若改动内存分配或扩容，请运行 `tests/reserve_stress.c` 的精确预留配置
  （`-DSX_FORCE_EXACT_RESERVE`），它能暴露被多余容量掩盖的越界访问。

用 CI 的环境运行测试套件：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

## 提交与拉取请求

- 每次提交只做一个逻辑改动，保持差异聚焦；避免无关的重新格式化。
- 标题用简短祈使句，正文说明原因与受影响的行为边界或文档；有关联 issue 请注明。
- 代码、测试与两种语言的文档放在同一个拉取请求中。
- 不要把构建产物放进差异：`build/`、`test/`、`./make` 与 `.ci-deps/` 均被
  gitignore。
- 必需检查通过、评审意见全部解决后，拉取请求才可合并。

## 许可与归属

- 库与项目源码使用 MIT 许可。新增 C 源码与头文件以
  `// SPDX-License-Identifier: MIT` 开头（如 `include/sx.h`、`src/sx_adv.c`
  与 `tests/reg.c`）；已有 SPDX 标注请勿改动。
- 构建脚本与源自 Forge 的 `src/export.h` 适用 Forge 构建脚本例外条款
  `GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception`。
  `build.conf` 与 `src/export.h` 带有该标识，`build.c` 同样适用；不要把这
  部分构建工具改为 MIT 或删除标注。
- 正则层使用 PCRE2（BSD-3-Clause），归属见
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)；新增第三方依赖也
  要在其中补充声明与许可。
- 项目许可全文见 [LICENSE](../../LICENSE)。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| [include/](../../include) | 公开头文件：`sx.h`（进阶层与 `sx_split`）以及它包含的 `sx_core.h` |
| [src/](../../src) | 实现：`sx_core.c`、`sx_adv.c`、`sx_raw.c`，以及内部 `_sx.h` 与 `export.h` |
| [tests/](../../tests) | 自包含的测试套件 |
| [benchmarks/](../../benchmarks) | 经审计的 StringX / SDS / bstrlib 基准与复跑工具 |
| [build.c](../../build.c)、[build.conf](../../build.conf) | 构建脚本及其配置 |
| [.github/workflows/](../../.github/workflows) | CI 与 CodeQL 工作流 |
| [.github/scripts/](../../.github/scripts) | CI 使用的编译、分析与工具检查脚本 |
| [docs/](../) | 本文档，含英文与简体中文 |
| [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) | 第三方许可与归属 |
| [README.md](../../README.md) | 项目概览与快速上手 |

---

[← 上一页](build-and-test.md) · [文档首页](README.md) · [下一页 →](ci.md)
