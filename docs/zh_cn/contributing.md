[English](../en_us/contributing.md) · 中文

[文档首页](README.md) › 贡献指南

# 贡献指南

String X 是一个契约写得很细的小型 C23 库，因此改动越小、测试与文档越同步，
评审就越容易，也越容易通过 CI 运行的同一套检查。本页说明工作流程与项目约定。

## 工作流程

1. Fork 仓库，并从 `main` 创建分支。分支名应体现改动内容
   （`fix/split-end-guard`、`docs/build-targets`）。
2. 完成改动，包括行为测试，以及英文和中文文档的更新。
3. 提交拉取请求前先在本地运行检查：

   ```sh
   ./make test -j 2
   bash .github/scripts/check-c.sh
   bash .github/scripts/check-c.sh analyze   # 仅 GCC
   ```

4. 向 `main` 提交拉取请求，说明行为变化、覆盖它的测试，以及更新了哪些文档页。
5. 等待必需检查通过。`main` 的合并保护要求 `CI` 与 `CodeQL (C/C++)` 状态检查
   通过且分支保持最新。检查内容见[持续集成](ci.md)。
6. 通过追加提交来回应评审意见；让拉取请求始终聚焦于一个改动。

`bash .github/scripts/configure.sh` 可以复现 CI 的编译器选择，但它会重写
`build.conf` 中的 `compiler=`、`compiler_path=` 与 `sanitize=`，因此请先保存
该文件的本地修改。

## 契约优先的文档规则

行为边界记录在两个地方：

- 公开头文件 `include/sx.h` 与 `include/sx_core.h` 中的注释；
- 每个 `tests/*.c` 顶部的 `Behavior boundaries` 注释，其中列出
  `In scope (contract)` 与 `Out of contract (not exercised)`。

任何可观察行为的变化都必须同时更新：

1. 受影响函数的头文件注释；
2. 测试实现及其行为边界注释；
3. `docs/en_us/` 与 `docs/zh_cn/` 下的英文和中文页面。

不要把行为留成未文档化，或仍按旧行为描述。若不同来源互相矛盾，运行时行为以
测试为准，函数签名以头文件为准，构建与打包行为以 `build.c` 为准——遇到真正的
矛盾应报告，而不是悄悄选择其中一个。

## 代码风格

- C23；库与测试都以 `-std=c23` 构建。
- 两空格缩进；左花括号与语句同行。
- 遵循现有命名与分层：核心函数放在 `src/sx_core.c`，正则进阶层放在
  `src/sx_adv.c`，内部内存分配放在 `src/sx_raw.c`。不要在
  `sx.h`/`sx_core.h` 之外新增公开函数。
- 偏移与长度使用 `size_t`，并沿用现有的哨兵约定（`(size_t)-1`、`ref == NULL`
  的视图、NULL 数组）。
- 把警告当作错误。`./make check` 目标使用 `-Wall -Wextra -Wpedantic -Werror`
  编译库，GCC 构建还会加上 `-fanalyzer`；CI 的直接编译检查使用
  `-std=c23 -O1 -Wall -Wextra -Werror`。不要为了让构建通过而屏蔽诊断信息；
  应修正代码，或在评审中解释分析器的发现。

## 测试

每个行为改动都需要测试。测试是 `tests/` 下自包含的程序，直接编译库源码，并在
头部 DSL（`// Include`、`// Source`、`// Link`、`// Library`、`// Option`）中
声明依赖。DSL 与完整示例见[构建、测试与打包](build-and-test.md)。

新增或修改测试时：

- 保持 `Behavior boundaries` 注释准确——它是测试实际执行的书面契约，包括
  `Out of contract (not exercised)` 列表；
- 断言可观察的结果（精确字节、长度、偏移与哨兵），而不只是调用返回了非 NULL；
- 每个缓冲区都用 `sx_free` 释放，每个返回的数组都用 `free` 释放；测试套件在
  AddressSanitizer、UBSan 与 LeakSanitizer 下运行，泄漏或越界访问都会导致
  测试失败；
- 在测试头部保留内存检查器选项与 `-fno-omit-frame-pointer`，使测试在本地与
  CI 中运行方式一致；
- 若改动了内存分配或扩容，请运行 `tests/reserve_stress.c` 的精确预留配置
  （`-DSX_FORCE_EXACT_RESERVE`），它能暴露被多余容量掩盖的越界访问。

用与 CI 相同的环境运行测试套件：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

## 提交与拉取请求

- 每次提交只做一个逻辑改动，保持差异聚焦；避免无关的重新格式化与顺手修改。
- 提交标题用简短祈使句，正文说明为什么需要该改动，以及它影响哪些行为边界或
  文档。若有关联 issue 请注明。
- 代码、测试与两种语言的文档放在同一个拉取请求中。
- 不要把构建产物放进差异：`build/`、`test/`、`./make` 与 `.ci-deps/` 都是
  生成物且被 gitignore。
- 必需检查通过、评审意见全部解决后，拉取请求才可合并。

## 许可与归属

- 库与项目源码使用 MIT 许可。新增的 C 源码与头文件应以
  `// SPDX-License-Identifier: MIT` 开头，就像 `include/sx.h`、`src/sx_adv.c`
  与 `tests/reg.c` 那样；已有的 SPDX 标注请勿改动。
- 构建脚本以及源自 Forge 的 `src/export.h` 适用 Forge 的构建脚本例外条款，
  标识为 `GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception`。
  `build.conf` 与 `src/export.h` 已带有该标识，`build.c` 同样适用该例外；
  不要把这部分构建工具改为 MIT，也不要删除这些标注。
- 正则层使用 PCRE2（BSD-3-Clause）。其归属与许可全文位于
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。若新增第三方依赖，
  也要在该文件中补充它的声明与许可。
- 项目许可全文见 [LICENSE](../../LICENSE)。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| [include/](../../include) | 公开头文件：`sx.h`（进阶层与 `sx_split`）以及它包含的 `sx_core.h` |
| [src/](../../src) | 实现：`sx_core.c`、`sx_adv.c`、`sx_raw.c`，以及内部 `_sx.h` 与 `export.h` |
| [tests/](../../tests) | 自包含的测试套件 |
| [build.c](../../build.c)、[build.conf](../../build.conf) | Forge 构建脚本及其配置 |
| [.github/workflows/](../../.github/workflows) | CI 与 CodeQL 工作流 |
| [.github/scripts/](../../.github/scripts) | CI 使用的编译、分析与工具检查脚本 |
| [docs/](../) | 本文档，含英文与简体中文 |
| [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) | 第三方许可与归属 |
| [README.md](../../README.md) | 项目概览与快速上手 |

---

[← 上一页](build-and-test.md) · [文档首页](README.md) · [下一页 →](ci.md)
