[English](../en_us/regex.md) · 中文

[文档首页](README.md) › 正则表达式

# 正则表达式

## 概述

`<sx.h>` 在核心缓冲区 API 之上加入了一个高级层：对 **PCRE2 8 位库**
（`pcre2-8`，版本 10.46）的一层薄封装，配置固定。它提供编译/释放这一对
函数，以及匹配、替换、分割三个家族，并且有意不自带任何策略：本页没有
说明的行为，一律是 PCRE2 的默认行为。

这一层的使用方式围绕一个生命周期：

1. 把一个模式编译成不透明的 `sx_reg_t *` 句柄；
2. 对任意多个 `sx_t` 主体（subject）执行任意多次调用；
3. 最后用 `sx_reg_free` 释放该句柄一次。

已编译的句柄不可变，可以跨线程共享；每次调用使用自己的匹配数据。主体按
**字节长度**匹配，因此内嵌 NUL 是普通数据（二进制安全）；模式和替换模板
都是 C 字符串，不能包含字面 NUL。

该层属于 `libstringx`，由库自身链接 `pcre2-8`：使用者只需链接
`-lstringx`，无需额外的 PCRE2 链接参数。PCRE2 采用 BSD-3-Clause 许可，
归属声明见 [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。
`$n` 替换规则见[替换模板](#替换模板)；按任务组织的用法示例见
[用户指南](guide.md)。

各家族一览：

| 家族 | 函数 | 结果 |
| --- | --- | --- |
| 编译 / 释放 | `sx_reg_compile`, `sx_reg_free` | 句柄 |
| 匹配 | `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all` | 0/1、单个视图或数组 |
| 替换 | `sx_reg_replace`, `sx_reg_replace_first` | 新的 `sx_t` |
| 分割 | `sx_reg_split` | 视图数组 |

## 生命周期

在使用它的循环之前把模式编译一次，并保留句柄。模式字符串本身不会被保留，
因此编译后可以立即释放。句柄不再需要时，用 `sx_reg_free` 释放。

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

让这套 API 可以安全地用于长期运行代码的，是下面三点：

- 句柄独立于主体：一个已编译模式可以服务任意多个缓冲区；
- 别名到主体的结果是视图，因此只在该主体存活且长度不变期间有效；
- `sx_reg_match_all` 和 `sx_reg_split` 返回的数组归调用方所有：用
  `free()` 释放，而不是 `sx_free`。

### sx_reg_compile

```c
sx_reg_t *sx_reg_compile(const char *pattern);
```

用 PCRE2 编译一个模式，返回不透明句柄。

| 参数 | 含义 |
| --- | --- |
| `pattern` | 以 NUL 结尾的 PCRE2 模式；句柄不会保留该字符串 |

返回新句柄；当 `pattern` 为 `NULL`、模式语法无效或分配失败时返回
`NULL`。封装层不暴露 PCRE2 的错误码和错误位置，因此这几种情况无法区分：
开发阶段请先校验模式，并把 `NULL` 一律当作"不要使用这个句柄"。

说明：

- 模式字符串可以在调用后立即释放或修改。
- 不会向 PCRE2 传递任何编译选项。模式内部选用的 PCRE2 特性（例如内联
  选项 `(?i)`）仍然生效。
- 相对于单次匹配调用，编译才是这一层里昂贵的一步，所以应当编译一次并
  复用句柄（[性能](#性能)）。

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

释放已编译句柄。允许传入 `NULL`，此时为空操作。

说明：

- 只释放一次，且要在最后一次使用它的调用之后。
- 之前调用返回的数据不依赖该句柄：匹配视图别名到主体，数组归调用方所有。
- 再次使用已释放的句柄属于契约之外。

```c
sx_reg_free(re); /* 最后一次使用后释放句柄 */
```

## 匹配家族

三个调用都从左侧开始扫描主体的内容字节，遵循 PCRE2 的**最左优先**
（leftmost-first）语义：起始位置最早的匹配获胜；在同一字节起始的多个
候选中，由 PCRE2 的偏好顺序决定。零长匹配也是真正的匹配。视图别名到
主体：不拷贝任何字节，也从不修改 `h`。

### sx_reg_is_match

```c
int sx_reg_is_match(const sx_reg_t *r, const sx_t *h);
```

回答"模式是否在 `h` 中任意位置匹配？"。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

若 `h` 中存在匹配返回 `1`，否则返回 `0`——包括 `r` 或 `h` 为 `NULL`、
或内部分配失败的情况。零长匹配也算匹配，因此 `a*` 能匹配空主体。

说明：

- 最便宜的存在性检查：一次 PCRE2 匹配调用，不分配结果。每次调用仍会创建
  并释放自己的小型 PCRE2 匹配数据块。
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

把最左匹配作为视图返回，指向 `h` 内部。

| 参数 | 含义 |
| --- | --- |
| `r` | 已编译模式 |
| `h` | 主体缓冲区 |

成功时结果是 `h` 缓冲区上的视图：`.ref` 指向主体，`.offset` / `.len`
选出匹配到的字节。无匹配、参数为 `NULL` 或分配失败时返回空视图
`{ref = NULL, offset = 0, len = 0}`。零长匹配仍是匹配：
`.ref != NULL` 且 `.len == 0`。

说明：

- 最左匹配，而不是"最长匹配"：候选之间的先后由 PCRE2 决定，例如
  `b|bc` 对 `abc` 会匹配偏移 1 处的 `b`。
- 只做一次 PCRE2 匹配调用；返回的视图不是拷贝。
- 锚点遵循 PCRE2 默认规则：`^` 匹配主体起点（偏移 0），`$` 匹配主体
  结尾；默认情况下 `$` 也能匹配最后一个换行符之前的位置。
- 在 `h` 被释放后，或在修改 `h` 长度之后使用该视图，属于契约之外。

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

返回 `malloc` 分配的视图数组，形态与 `sx_split` 一致：
`arr[0 .. count-1]` 是各次匹配，`arr[count].ref == NULL` 是结束哨兵。
调用方用 `free()` 释放数组；视图别名到 `h`，在 `h` 存活且长度不变期间
有效。

完全没有匹配、`r` 或 `h` 为 `NULL`、或分配失败时返回 `NULL`。该 API
无法区分这三种情况。

说明：

- 匹配之间不重叠：每次匹配后从匹配结尾继续扫描；零长匹配则再向右移动
  一个字节（[扫描规则](#扫描规则)）。
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

- 匹配按从左到右的顺序取，且互不重叠；
- 一次匹配之后，下一次搜索从该匹配的结尾开始；
- 零长匹配之后，下一次搜索从匹配起点**向右一个字节**开始，因此能匹配
  空串的模式不会让扫描停滞；
- 匹配可以正好结束于 `len(h)`（例如 `$`）；随后扫描停止，因为 PCRE2
  拒绝超出主体的起始偏移。

| 模式 | 主体 | 报告的匹配（偏移, 长度） |
| --- | --- | --- |
| `aa\|aaa` | `aaaa` | (0,2), (2,2) —— `aaa` 没有机会 |
| `a*` | `bbb` | (0,0), (1,0), (2,0), (3,0) |
| `$` | `abc` | (3,0) |
| `bc` | `abcb` | (1,2) |

同一条规则决定了 `sx_reg_split` 的 token 数量：始终等于
`匹配数 + 1`。

## 替换家族

两个替换调用都把 C 字符串模板代入匹配处，并返回**新的**、归调用方所有的
`sx_t`；`h` 永远不会被修改。结果与其他缓冲区一样用 `sx_free` 释放。

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

返回新缓冲区；当 `r`、`h` 或 `repl` 为 `NULL`、PCRE2 拒绝该模板、或
分配失败时返回 `NULL`。参数合法但没有匹配时，结果是 `h` 的**原样拷贝**，
绝不会是 `NULL`。

说明：

- 模板会展开 `$0` / `$n`；见[替换模板](#替换模板)。
- 零长匹配也会被替换：`a*` 对 `bbb`、模板为 `-` 时得到 `-b-b-b-`。
- 空模板会删除每一处匹配，因此模式匹配整个主体时结果为空缓冲区。
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

契约与 `sx_reg_replace` 相同，但只替换**最左**的一处匹配；其余字节原样
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

两个替换调用都按 PCRE2 的替换语法解释 `repl`，并固定设置两个选项，使缺失
的捕获组不会造成问题。

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

说明：

- 封装层传入 PCRE2 的 `PCRE2_SUBSTITUTE_UNSET_EMPTY` 和
  `PCRE2_SUBSTITUTE_UNKNOWN_UNSET`：未参与匹配的组和根本不存在的组都
  展开为空串。因此模板不会仅仅因为缺少某个捕获组而被拒绝。
- `$n` 指向每一次被替换的匹配自己的捕获组；第 0 组是整个匹配。
- `repl` 是 C 字符串，因此其中不能出现字面 NUL。匹配到的 NUL 字节仍会
  通过 `$0` / `$n` 进入结果。
- 完整的替换语法属于 PCRE2；见
  [PCRE2 文档](https://www.pcre.org/)。

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

返回 `malloc` 分配的数组，形态与 `sx_split` 完全一致：
`arr[0 .. count-1]` 是按顺序排列的 token，`arr[count].ref == NULL` 是
结束哨兵，且 `count == 匹配数 + 1`。各边界处以及内部产生的空 token 都会
保留。调用方用 `free()` 释放数组；token 是 `h` 上的视图。

参数为 `NULL` 或分配失败时返回 `NULL`。没有匹配不是错误：此时数组中只有
整个主体这一个 token。

说明：

- 零长匹配会在每个位置分割：`a*` 对 `bbb` 得到 `""`、`"b"`、`"b"`、
  `"b"`、`""`，共五个 token。
- 匹配覆盖整个主体时，产生两个空 token。
- 开头和结尾的匹配会产生空的首尾 token；相邻匹配之间产生内部空 token。
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
  ASCII。组成非 ASCII 字符的字节（例如 UTF-8 字节 `C2 A5`）永远不会匹配
  `\d`。
- 在 8 位非 UTF 模式下，`.` 匹配除换行以外的任意字节，包括内嵌 NUL：
  模式 `A.B` 能匹配三个字节 `A\0B`。
- 替换在两个方向上都是二进制安全的：主体 `a\0b`、模式 `b`、模板 `B`
  得到 `a\0B`；对同一主体用 `.` 搭配模板 `X` 得到 `XXX`。
- 由于模式是 C 字符串，主体中的 NUL 字节只能用 PCRE2 转义描述；见
  [PCRE2 文档](https://www.pcre.org/)。

## 错误哨兵

任何有文档的调用都不会因为非法输入、无匹配或分配失败而崩溃；每个家族用
固定的哨兵值报告这些情况。

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

说明：

- 对 `sx_reg_match_all` 和 `sx_reg_split`，"无匹配"和分配失败都返回
  `NULL`；数组家族无法区分二者。
- 对替换家族，"无匹配"是正常成功：得到 `h` 的原样拷贝。
- 数组由 `malloc` 分配，用 `free()` 释放；替换家族返回的缓冲区是
  `sx_t *`，用 `sx_free()` 释放。

## 线程安全

- 已编译的 `sx_reg_t` 在 `sx_reg_compile` 之后不可变。它没有可变状态，
  封装层也不保存全局状态，因此同一个句柄可以被多个线程同时使用。
- 每个匹配/分割调用都会创建自己的 PCRE2 匹配数据块
  （`pcre2_match_data_create_from_pattern`）并在返回前释放；替换调用委托
  给 `pcre2_substitute`，同样使用每次调用自己的匹配数据。两个线程不会
  共享匹配状态。
- 主体缓冲区是普通内存，没有加锁：并发只读没有问题，任何修改都需要外部
  同步。
- 视图和数组是主体的快照。某个线程仍在读取视图时不要修改主体；其他线程
  仍在使用句柄时不要释放它。

## 性能

| 调用 | PCRE2 工作量 | 封装层分配 | 结果 |
| --- | --- | --- | --- |
| `sx_reg_compile` | 一次编译 | 代码加句柄 | 句柄 |
| `sx_reg_is_match` | 一次匹配调用 | 仅匹配数据 | `0` / `1` |
| `sx_reg_match` | 一次匹配调用 | 仅匹配数据 | 一个视图 |
| `sx_reg_match_all` | 扫描两遍 | 匹配数据加一个精确大小的数组 | 数组 |
| `sx_reg_split` | 扫描两遍 | 匹配数据加一个精确大小的数组 | 数组 |
| `sx_reg_replace*` | 两次替换调用 | PCRE2 内部匹配数据、一个临时缓冲区、一个 `sx_t` | 新缓冲区 |

说明：

- 编译一次：模式编译的成本远高于单次匹配，在循环里反复编译是唯一本可
  避免的性能错误。
- `sx_reg_is_match` 是最便宜的存在性检查；`sx_reg_match` 只多出一个返回
  视图。
- `sx_reg_match_all` 和 `sx_reg_split` 扫描两遍（先计数、再填充），因此
  结果数组只分配一次，绝不通过 `realloc` 增长。
- 替换家族先用第一次 `pcre2_substitute` 调用探测精确输出长度，再用第二次
  调用填充合适大小的缓冲区；缓冲区随后一次性拷贝进返回的 `sx_t`。
- 每次调用都会分配并释放匹配数据；调用之间不缓存匹配数据，封装层也不启用
  PCRE2 JIT 或任何编译选项。
- 主体永远不会被拷贝；只有替换家族会分配结果缓冲区。

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
  在默认（未开启 `DOTALL`）模式下，`.` 不匹配换行。
- **不暴露选项标志**：封装层不传编译选项、不传匹配选项，也不传匹配上下文。
  写在模式内部的 PCRE2 特性（例如内联选项）仍然生效；大小写不敏感、多行
  匹配等行为用 PCRE2 的模式内语法请求。
- **以 PCRE2 默认行为为准**：最左优先的选择、贪婪/懒惰量词、锚点、反向
  引用以及其他所有模式语义都属于 PCRE2，而不是封装层。
- 封装层不暴露 PCRE2 错误码或编译错误位置。

模式语法与语义由 PCRE2 项目在 [pcre.org](https://www.pcre.org/) 记录；
本库不会重复记录它们。随附的 PCRE2 版本为 10.46；该依赖采用
BSD-3-Clause 许可，归属声明见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。

---

[← 上一页](api-search.md) · [文档首页](README.md) · [下一页 →](design.md)
