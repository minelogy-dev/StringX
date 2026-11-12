# String X 文档（简体中文）

**版本 v0.5** · [English](../en_us/README.md) ·
[项目仓库](https://github.com/minelogy-dev/StringX)

String X 是一个轻量的 C 字符串库，核心思路只有一个：每个字符串都是一段
以单个 NUL 结尾的缓冲区，而它的每一段切片都是一个不拷贝任何数据的视图。
公开接口只有一个头文件 `<sx.h>` 和一个库 `libstringx`（`-lstringx`）；
正则表达式是由 PCRE2 支持的进阶层，可按需使用。

## 从这里开始

| 你想…… | 请阅读 |
| --- | --- |
| 安装 String X 并写第一个程序 | [快速上手](getting-started.md) |
| 理解缓冲区 / 视图 / 所有权模型并查找用法示例 | [用户指南](guide.md) |
| 查询缓冲区、视图与修改类函数 | [API 参考：缓冲区与视图](api-buffer.md) |
| 查询查找、比较与分割类函数 | [API 参考：查找、比较与分割](api-search.md) |
| 使用正则表达式层 | [正则表达式](regex.md) |
| 了解库的内部实现 | [设计与内部实现](design.md) |
| 构建、测试、安装或打包 | [构建、测试与打包](build-and-test.md) |
| 提交一处修改 | [贡献指南](contributing.md) |
| 了解 CI 检查 | [持续集成](ci.md) |

## 全部文档

- [快速上手](getting-started.md) —— 环境要求、构建、安装、第一个程序、
  十分钟概览、文档索引。
- [用户指南](guide.md) —— 心智模型、所有权规则、二进制安全、错误处理、
  常用任务示例、常见陷阱、性能说明。
- [API 参考：缓冲区与视图](api-buffer.md) —— 创建与释放、容量、访问器、
  修改、子串、视图移动与裁剪。
- [API 参考：查找、比较与分割](api-search.md) —— `sx_find*`、`sx_rfind*`、
  `sx_find_all*`、`sx_cmp*`、`sx_starts_with*`、`sx_ends_with*`、`sx_split`。
- [正则表达式](regex.md) —— `sx_reg_*` 三个家族：匹配、替换与分割，以及
  每个调用的完整契约。
- [设计与内部实现](design.md) —— 数据布局、扩容策略、查找算法、正则封装
  的内部细节、可移植性限制、测试策略。
- [构建、测试与打包](build-and-test.md) —— `./make` 各目标、测试框架、
  内存检查器、安装与 Debian 打包。
- [贡献指南](contributing.md) —— 工作流程、契约优先的文档规则、代码风格、
  测试、许可。
- [持续集成](ci.md) —— 工作流、必需检查、CodeQL、合并保护、本地复现。

## 文档约定

- API 参考只覆盖公开头文件：`sx.h` 以及它包含的 `sx_core.h`。`sx_raw.h`
  是内部的分配/调试接口，不属于受支持的公开接口。
- 文档中的每一条行为描述都来自公开头文件中的契约、实现代码，以及
  `tests/*.c` 中的行为边界注释。
- **契约之外（out of contract）**表示该调用不在库的文档化保证范围内，
  行为未定义；测试有意不覆盖这类调用，你的程序也不应依赖它。
- 代码示例面向 Linux 上的 C23，并链接 `-lstringx`（正则示例无需额外
  参数：库自身已链接 `pcre2-8`）。
- 本文档描述 **v0.5**；版本号的唯一来源是 `build.conf`。

## 许可

库本身以 MIT 许可发布 —— 见 [LICENSE](../../LICENSE) 以及源文件中的 SPDX
标注。正则层依赖 PCRE2（BSD-3-Clause），详见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。
