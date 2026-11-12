[English](../en_us/regex.md) · 中文

[文档首页](README.md) › 正则表达式

# 正则表达式

## 概述

`<sx.h>` 在核心缓冲区 API 之上增加了一个高级层：对 **PCRE2 8 位库**
（`pcre2-8`，版本 10.46）的薄封装，配置固定。它提供编译/释放函数，以及
匹配、替换、分割三个家族，不自带策略：本页未说明的行为一律是 PCRE2 默认
行为。

生命周期：

1. 把一个模式编译成不透明的 `sx_reg_t *` 句柄；
2. 对任意多个 `sx_t` 主体（subject）执行任意多次调用；
3. 最后用 `sx_reg_free` 释放该句柄一次。

已编译句柄不可变，可跨线程共享；每次调用使用自己的匹配数据。主体按
**字节长度**匹配，因此内嵌 NUL 是普通数据（二进制安全）；模式和替换模板
都是 C 字符串，不能包含字面 NUL。

该层属于 `libstringx`，库自身链接 `pcre2-8`：使用者只需链接 `-lstringx`，
无需额外 PCRE2 参数。PCRE2 采用 BSD-3-Clause 许可，归属见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。`$n` 替换规则见
[替换模板](#替换模板)；按任务的用法示例见
[用户指南](guide.md)。

各家族一览：

| 家族 | 函数 | 结果 |
| --- | --- | --- |
| 编译 / 释放 | `sx_reg_compile`, `sx_reg_free` | 句柄 |
| 匹配 | `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all` | 0/1、单个视图或数组 |
| 替换 | `sx_reg_replace`, `sx_reg_replace_first` | 新的 `sx_t` |
| 分割 | `sx_reg_split` | 视图数组 |

## 生命周期

在循环之前把模式编译一次。模式字符串不会被保留，编译后可立即释放；句柄
不再需要时用 `sx_reg_free` 释放。

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

static void show(const char *label, sx_view_t v) {
  printf("%s: ", label);
  if (v.ref == NULL) {
    printf("<none>\n");
    return;
  }
  fwrite(sx_buf_view_mut(v), 1, v.len, stdout);
  putchar('\n');
}

int main(void) {
  sx_t *h = sx_new_from_cstr("bob@example.com and alice@example.org");
  sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+\\.\\w+)");
  if (!h || !re)
    return 1;

  printf("is_match: %d\n", sx_reg_is_match(re, h));
  show("first", sx_reg_match(re, h));

  sx_view_t *all = sx_reg_match_all(re, h);
  for (int i = 0; all && all[i].ref != NULL; i++)
    printf("match %d: %zu bytes at offset %zu\n", i, all[i].len,
           all[i].offset);
  free(all);

  sx_t *out = sx_reg_replace(re, h, "[$2: $1]");
  if (out) {
    printf("replaced: ");
    fwrite(sx_buf_mut(out), 1, (size_t)sx_len(out), stdout);
    putchar('\n');
    sx_free(out);
  }

  sx_view_t *tok = sx_reg_split(re, h);
  for (int i = 0; tok && tok[i].ref != NULL; i++)
    printf("token %d: %zu bytes\n", i, tok[i].len);
  free(tok);

  sx_reg_free(re);
  sx_free(h);
  return 0;
}
```

三点特性让这套 API 可安全用于长期代码：

- 句柄独立于主体：一个已编译模式可服务任意多个缓冲区。
- 别名到主体的结果是视图，仅在该主体存活且长度不变时有效。
- `sx_reg_match_all` 和 `sx_reg_split` 返回的数组归调用方所有：用 `free()`
  释放，而不是 `sx_free`。

### sx_reg_compile

```c
sx_reg_t *sx_reg_compile(const char *pattern);
```

用 PCRE2 编译一个模式，返回不透明句柄。

| 参数 | 含义 |
| --- | --- |
| `pattern` | 以 NUL 结尾的 PCRE2 模式；句柄不会保留该字符串 |

返回新句柄；`pattern` 为 `NULL`、语法无效或分配失败时返回 `NULL`。封装层
不暴露 PCRE2 错误码和位置，这几种情况无法区分：开发时先校验模式，把
`NULL` 一律视为不可用。

- 模式字符串可在调用后立即释放或修改。
- 不向 PCRE2 传递编译选项；模式内部选用的特性（如内联选项 `(?i)`）仍
  生效。
- 编译是这一层昂贵的一步，应编译一次并复用句柄（[性能](#性能)）。

```c
sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+)"); /* 输入非法时为 NULL */
/* re == NULL 表示：pattern 为 NULL、语法无效，或内存不足 */
/* ... 执行匹配 / 替换 / 分割调用 ... */
sx_reg_free(re); /* 最后一次使用后释放句柄 */
```

### sx_reg_free

```c
void sx_reg_free(sx_reg_t *r);
```

释放已编译句柄；`NULL` 为空操作。

- 只释放一次，且在最后一次使用之后。
- 先前结果不依赖该句柄：匹配视图别名到主体，数组归调用方所有。
- 再次使用已释放句柄属于契约之外。

```c
sx_reg_free(re); /* 最后一次使用后释放句柄 */
```

## 匹配家族

三个调用都从左侧扫描内容字节，遵循 PCRE2 的**最左优先**
（leftmost-first）语义：起点最早者获胜；同一起点的候选按 PCRE2 的偏好顺序
决定。零长匹配也是真正的匹配。视图别名到主体，`h` 从不被修改。

### sx_reg_is_match

```c
int sx_reg_is_match(const sx_reg_t *r, const sx_t *h);
```

回答"模式是否匹配 `h` 的任意位置？"。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

若 `h` 中任意位置存在匹配返回 `1`，否则返回 `0`——包括 `r`/`h` 为 `NULL`
或内部分配失败。零长匹配也算匹配，因此 `a*` 能匹配空主体。

- 最便宜的存在性检查：一次 PCRE2 匹配调用、不分配结果；每次调用仍会创建
  并释放小型 PCRE2 匹配数据块。
- 主体按字节长度搜索；锚点指向字节偏移。

```c
sx_reg_t *re = sx_reg_compile("a*");
sx_t *empty = sx_new();
int yes = sx_reg_is_match(re, empty); /* 1：零长匹配也算匹配 */
sx_free(empty);
sx_reg_free(re);
```

### sx_reg_match

```c
sx_view_t sx_reg_match(const sx_reg_t *r, const sx_t *h);
```

把最左匹配作为指向 `h` 内部的视图返回。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

成功时结果是 `h` 上的视图：`.ref`、`.offset`、`.len` 选出匹配到的字节。
无匹配、参数为 `NULL` 或分配失败时返回空视图
`{ref = NULL, offset = 0, len = 0}`；零长匹配仍有 `.ref != NULL` 且
`.len == 0`。

- 最左而非最长：候选先后由 PCRE2 决定，例如 `b|bc` 对 `abc` 匹配偏移 1
  处的 `b`。
- 只做一次 PCRE2 匹配调用；返回的视图不是拷贝。
- 锚点遵循 PCRE2 默认规则：`^` 匹配主体起点（偏移 0），`$` 匹配主体
  结尾；默认情况下 `$` 也能匹配最后一个换行符之前的位置。
- 在 `h` 被释放或长度改变后使用该视图属于契约之外。

```c
sx_reg_t *re = sx_reg_compile("bc");
sx_t *h = sx_new_from_cstr("abc");
sx_view_t v = sx_reg_match(re, h); /* v.offset == 1，v.len == 2（"bc"） */
sx_free(h);
sx_reg_free(re);
```

### sx_reg_match_all

```c
sx_view_t *sx_reg_match_all(const sx_reg_t *r, const sx_t *h);
```

从左到右扫描一遍，收集全部匹配。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

返回 `malloc` 分配的数组，形态与 `sx_split` 一致：`arr[0 .. count-1]` 是
各次匹配，`arr[count].ref == NULL` 是结束哨兵。用 `free()` 释放；视图在
`h` 存活且长度不变期间别名到 `h`。无匹配、参数为 `NULL` 或分配失败时返回
`NULL`——该 API 无法区分。

- 匹配不重叠：每次匹配后从结尾继续扫描；零长匹配则右移一个字节（[扫描规则](#扫描规则)）。
- 计数和填充是同一确定性扫描的两遍，因此数组只按最终大小分配一次。
- `sx_find_all*` 报告的是可能重叠的位置；`sx_reg_match_all` 报告的是
  正则匹配，它们不可能重叠。

```c
sx_reg_t *re = sx_reg_compile("a*");
sx_t *h = sx_new_from_cstr("bbb");
sx_view_t *arr = sx_reg_match_all(re, h);
int n = 0;
while (arr[n].ref != NULL)
  n++; /* n == 4：偏移 0、1、2、3 处各有一个零长匹配 */
free(arr);
sx_free(h);
sx_reg_free(re);
```

## 扫描规则

`sx_reg_match_all` 和 `sx_reg_split` 共用同一套扫描规则：

- 匹配从左到右取，互不重叠；
- 一次匹配后，下一次搜索从匹配结尾开始；
- 零长匹配后，下一次搜索从匹配起点**向右一个字节**开始，能匹配空串的模式
  不会让扫描停滞；
- 匹配可正好结束于 `len(h)`（例如 `$`），随后扫描停止，因为 PCRE2 拒绝
  超出主体的起始偏移。

| 模式 | 主体 | 报告的匹配（偏移, 长度） |
| --- | --- | --- |
| `aa\|aaa` | `aaaa` | (0,2), (2,2) —— `aaa` 没有机会 |
| `a*` | `bbb` | (0,0), (1,0), (2,0), (3,0) |
| `$` | `abc` | (3,0) |
| `bc` | `abcb` | (1,2) |

同一条规则决定 `sx_reg_split` 的 token 数：始终为 `匹配数 + 1`。

## 替换家族

两个替换调用把 C 字符串模板代入匹配处，返回**新的**、归调用方所有的
`sx_t`；`h` 永不修改。结果用 `sx_free` 释放。

### sx_reg_replace

```c
sx_t *sx_reg_replace(const sx_reg_t *r, const sx_t *h, const char *repl);
```

用 `repl` 替换 `h` 中的每一处匹配。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区；不会被修改 |
| `repl` | 以 NUL 结尾的替换模板；不会被保留 |

返回新缓冲区；参数为 `NULL`、PCRE2 拒绝模板或分配失败时返回 `NULL`。
参数合法但无匹配时，结果是 `h` 的**原样拷贝**，绝不是 `NULL`。

- 模板会展开 `$0` / `$n`；见[替换模板](#替换模板)。
- 零长匹配也会被替换：`a*` 对 `bbb`、模板为 `-` 时得到 `-b-b-b-`。
- 空模板删除每一处匹配；因此模式匹配整个主体时结果为空缓冲区。
- 字节精确：`h` 中的内嵌 NUL 是普通匹配数据，并会原样复制进结果。

```c
sx_reg_t *re = sx_reg_compile("(\\w+) (\\w+)");
sx_t *h = sx_new_from_cstr("hello world");
sx_t *swapped = sx_reg_replace(re, h, "$2 $1"); /* "world hello" */
sx_free(swapped);
sx_free(h);
sx_reg_free(re);
```

### sx_reg_replace_first

```c
sx_t *sx_reg_replace_first(const sx_reg_t *r, const sx_t *h,
                           const char *repl);
```

契约与 `sx_reg_replace` 相同，但只替换**最左**一处匹配，其余字节原样
拷贝。

```c
sx_reg_t *re = sx_reg_compile("x");
sx_t *h = sx_new_from_cstr("xax");
sx_t *out = sx_reg_replace_first(re, h, "<$0>"); /* "<x>ax" */
sx_free(out);
sx_free(h);
sx_reg_free(re);
```

## 替换模板

两个替换调用都按 PCRE2 的替换语法解释 `repl`，并固定设置两个使缺失捕获组
无害的选项。

| 模板元素 | 展开为 |
| --- | --- |
| `$0` | 整个匹配 |
| `$n`（`n >= 1`） | 第 `n` 个捕获组；该组未参与匹配或不存在时展开为空串 |
| 其他文本 | 原样拷贝；`$` 和 `\` 的字面写法仍由 PCRE2 替换语法定义 |

| 模式 | 主体 | 模板 | 结果 |
| --- | --- | --- | --- |
| `(\w+) (\w+)` | `hello world` | `$2 $1` | `world hello` |
| `(a)(b)` | `ab` | `$1$3` | `a` |
| `a(b)?` | `a` | `[$1]` | `[]` |
| `abc` | `abc` | （空模板） | （空缓冲区） |

- 封装层设置 `PCRE2_SUBSTITUTE_UNSET_EMPTY` 和
  `PCRE2_SUBSTITUTE_UNKNOWN_UNSET`：未参与匹配的组和不存在的组号都展开为
  空，因此模板不会仅因缺少捕获组而被拒绝。
- `$n` 指向每一次被替换的匹配自己的捕获组；第 0 组是整个匹配。
- `repl` 是 C 字符串，不能含字面 NUL；匹配到的 NUL 字节仍通过 `$0`/`$n`
  进入结果。
- 完整语法属于 PCRE2；见 [PCRE2 文档](https://www.pcre.org/)。

## 分割家族

### sx_reg_split

```c
sx_view_t *sx_reg_split(const sx_reg_t *r, const sx_t *h);
```

把 `h` 分割为非重叠匹配之间的空隙。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

返回 `malloc` 分配的数组，形态与 `sx_split` 完全一致：token 按顺序位于
`arr[0 .. count-1]`，`arr[count].ref == NULL` 是结束哨兵，且
`count == 匹配数 + 1`。各边界和内部空 token 都保留；用 `free()` 释放数组；
token 是 `h` 上的视图。参数为 `NULL` 或分配失败时返回 `NULL`；没有匹配不是
错误：整个主体就是单个 token。

- 零长匹配在每个位置分割：`a*` 对 `bbb` 得到 `""`、`"b"`、`"b"`、`"b"`、
  `""`，共五个 token。
- 匹配覆盖整个主体时产生两个空 token。
- 首尾匹配产生空的首尾 token；相邻匹配之间产生内部空 token。
- token 使用期间不要修改 `h`（视图是快照；见[用户指南](guide.md)）。

```c
sx_reg_t *re = sx_reg_compile("[0-9]+");
sx_t *h = sx_new_from_cstr("a1b22c");
sx_view_t *tok = sx_reg_split(re, h);
/* 3 个 token："a" (0,1)、"b" (2,1)、"c" (5,1) —— "22" 是一处匹配 */
free(tok);
sx_free(h);
sx_reg_free(re);
```

## 二进制安全

| 方向 | 规则 |
| --- | --- |
| 主体 `h` | 按字节长度匹配；内嵌 NUL 和任意字节值都是普通数据 |
| 模式 | 是 C 字符串：以 NUL 结尾，因此模式内部不能有字面 NUL |
| 模板 `repl` | 是 C 字符串：不能有字面 NUL；捕获组可以把 NUL 字节带入结果 |

- PCRE2 运行在 **8 位非 UTF 模式**：`\d`、`\w`、`\s`、`\b` 等只匹配
  ASCII；组成非 ASCII 字符的字节（例如 UTF-8 `C2 A5`）永远不匹配 `\d`。
- 在 8 位非 UTF 模式下，`.` 匹配除换行以外的任意字节，包括内嵌 NUL：
  模式 `A.B` 能匹配三个字节 `A\0B`。
- 替换在两个方向上都是二进制安全的：主体 `a\0b`、模式 `b`、模板 `B`
  得到 `a\0B`；对同一主体用 `.` 搭配模板 `X` 得到 `XXX`。
- 由于模式是 C 字符串，主体中的 NUL 字节只能用 PCRE2 转义描述；见
  [PCRE2 文档](https://www.pcre.org/)。

## 错误哨兵

有文档的调用不会因非法输入、无匹配或分配失败而崩溃；每个家族用固定哨兵
报告。

| 调用 | 无匹配 | `NULL` 参数 / 非法输入 | 分配失败 |
| --- | --- | --- | --- |
| `sx_reg_compile` | —— | `NULL`（语法错误也如此） | `NULL` |
| `sx_reg_free` | —— | `NULL` 为空操作 | —— |
| `sx_reg_is_match` | `0` | `0` | `0` |
| `sx_reg_match` | 空视图 `{ref = NULL}` | 空视图 | 空视图 |
| `sx_reg_match_all` | `NULL` | `NULL` | `NULL` |
| `sx_reg_replace` | 原样拷贝 | `NULL` | `NULL` |
| `sx_reg_replace_first` | 原样拷贝 | `NULL` | `NULL` |
| `sx_reg_split` | 单 token 数组（整个主体） | `NULL` | `NULL` |

- 对 `sx_reg_match_all` 和 `sx_reg_split`，无匹配和分配失败都返回 `NULL`；
  数组家族无法区分。
- 对替换家族，无匹配是正常成功：`h` 的原样拷贝。
- 数组由 `malloc` 分配并用 `free()` 释放；替换结果是 `sx_t *`，用
  `sx_free()` 释放。

## 线程安全

- 已编译的 `sx_reg_t` 在 `sx_reg_compile` 后不可变，没有可变状态，封装层也
  不保存全局状态，因此同一句柄可被多个线程同时使用。
- 每个匹配/分割调用创建并释放自己的 PCRE2 匹配数据块；替换委托给
  `pcre2_substitute`，同样使用每次调用自己的匹配数据。线程之间不共享匹配状态。
- 主体缓冲区是普通内存、无加锁：并发只读安全，任何修改需要外部同步。
- 视图和数组是快照：某线程仍在读取视图时不要修改主体；其他线程仍在使用
  句柄时不要释放它。

## 性能

| 调用 | PCRE2 工作量 | 封装层分配 | 结果 |
| --- | --- | --- | --- |
| `sx_reg_compile` | 一次编译 | 代码加句柄 | 句柄 |
| `sx_reg_is_match` | 一次匹配调用 | 仅匹配数据 | `0` / `1` |
| `sx_reg_match` | 一次匹配调用 | 仅匹配数据 | 一个视图 |
| `sx_reg_match_all` | 扫描两遍 | 匹配数据加一个精确大小的数组 | 数组 |
| `sx_reg_split` | 扫描两遍 | 匹配数据加一个精确大小的数组 | 数组 |
| `sx_reg_replace*` | 两次替换调用 | PCRE2 内部匹配数据、一个临时缓冲区、一个 `sx_t` | 新缓冲区 |

- 编译一次：编译成本远高于单次匹配，在循环里反复编译是唯一可避免的性能
  错误。
- `sx_reg_is_match` 是最便宜的存在性检查；`sx_reg_match` 只多出一个返回
  视图。
- `sx_reg_match_all` 和 `sx_reg_split` 扫描两遍（先计数、再填充），结果数
  组只按最终大小分配一次，不靠 `realloc` 增长。
- 替换先用第一次 `pcre2_substitute` 探测精确输出长度，再用第二次填充合适
  缓冲区；随后一次性拷贝进返回的 `sx_t`。
- 每次调用都分配并释放匹配数据；调用之间不缓存，封装层不启用 PCRE2 JIT 或
  编译选项。
- 主体从不拷贝；只有替换家族分配结果缓冲区。

## 选择合适的调用

| 需求 | 调用 | 返回 | 所有权 |
| --- | --- | --- | --- |
| 是否有任意匹配？ | `sx_reg_is_match` | `int` 0/1 | 无 |
| 最左匹配 | `sx_reg_match` | 指向 `h` 的视图 | 无；视图是快照 |
| 全部匹配 | `sx_reg_match_all` | `malloc` 数组加结束哨兵 | 调用方：`free()` |
| 替换全部匹配 | `sx_reg_replace` | 新的 `sx_t` | 调用方：`sx_free()` |
| 只替换第一处匹配 | `sx_reg_replace_first` | 新的 `sx_t` | 调用方：`sx_free()` |
| 分割成字段 | `sx_reg_split` | `malloc` 数组加结束哨兵 | 调用方：`free()` |

## PCRE2 语义

封装层固定了一组简单、刻意的配置：

- **8 位、非 UTF**：模式和主体都是字节，字符类是 ASCII，不做 UTF 校验。
  默认（未开启 `DOTALL`）下 `.` 不匹配换行。
- **不暴露选项标志**：不传编译选项、匹配选项或匹配上下文。模式内部的内联
  选项等特性仍然生效，因此大小写不敏感、多行匹配等使用 PCRE2 自己的语法。
- **以 PCRE2 默认行为为准**：候选顺序、贪婪/懒惰量词、锚点、反向引用及其
  他模式语义都属于 PCRE2，而非封装层。
- 封装层不暴露 PCRE2 错误码或编译错误位置。

模式语法与语义由 PCRE2 项目在 [pcre.org](https://www.pcre.org/) 记录，本库
不重复记录。随附的 PCRE2 版本为 10.46；依赖采用 BSD-3-Clause 许可，归属见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。

---

[← 上一页](api-search.md) · [文档首页](README.md) · [下一页 →](design.md)
