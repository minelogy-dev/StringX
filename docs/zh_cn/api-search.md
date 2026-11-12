[English](../en_us/api-search.md) · 中文

[文档首页](README.md) › API 参考：查找、比较与分割

# 查找、比较与分割

本页是查找家族（`sx_find*`、`sx_rfind*`、`sx_find_all*`）、比较家族
（`sx_cmp*`、`sx_starts_with*`、`sx_ends_with*`）以及 `sx_split` 的完整
参考。它们全部字节精确，并按内容长度工作，因此内嵌的 NUL 字节是普通
数据。缓冲区的创建、修改与视图见[缓冲区与视图](api-buffer.md)。

## 约定

**needle 的形式。** 每个查找家族都有同样的四种 needle 形态：

| 后缀 | 参数类型 | 匹配的字节 | NUL 行为 |
| --- | --- | --- | --- |
| 无 | `const sx_t *` | 该缓冲区的内容字节 | 内嵌 NUL 是数据 |
| `_view` | `sx_view_t` | `v.ref->ptr + v.offset` 处的 `v.len` 个字节 | 内嵌 NUL 是数据 |
| `_cstr` | `const char *` | `strlen(...)` 个字节 | 第一个 NUL 结束 needle |
| `_char` | `char` | 恰好一个字节 | `'\0'` 表示一个 NUL 内容字节 |

owned 形式和 view 形式是二进制安全的：它们精确读取 needle 的长度，因此
内嵌的 `'\0'` 会作为数据参与匹配。`_cstr` 形式使用 `strlen`，所以
`sx_find_cstr(h, "a\0c")` 实际查找的是 `"a"`；要查找二进制模式，请使用
owned 或 view 形式的 needle。空的 needle（长度为 0）永远不会被找到。

**有界区域。** 设 `H` 为被查找缓冲区的内容长度，needle 长度为 `m`，一次匹配是窗口
`[p, p + m)`，它必须完全位于被搜索的区域之内：

| 函数 | 搜索区域 | 窗口条件 |
| --- | --- | --- |
| `sx_find*`、`sx_rfind*`（无界） | `[0, H)` | `p + m <= H` |
| `sx_find_view_b(h, needle, boundary)` | `[boundary, H)` | `p >= boundary` 且 `p + m <= H` |
| `sx_rfind_view_b(h, needle, boundary)` | `[0, min(boundary, H))` | `p + m <= min(boundary, H)` |

两个有界形式都接收 `size_t boundary`，但方向相反：`sx_find_view_b` 从
左侧限定区域，起点早于 `boundary` 的窗口不计入；`sx_rfind_view_b` 从右侧
限定区域，终点晚于边界的窗口不计入。下面是测试中的具体用例，取
`h = "ababa"`，needle 为 `"ba"`：

- `sx_find_view_b(h, nd, 2)` 为 3 —— 偏移 1 处的匹配起点在边界之前，不计
  入；boundary 为 4 时返回 `(size_t)-1`，因为剩余区域 `"a"` 放不下一个
  窗口。
- `sx_rfind_view_b(h, nd, 3)` 为 1 —— 偏移 3 处的匹配终止于 5，越过了
  边界；boundary 为 2 时返回 `(size_t)-1`；若 needle 为 `"aba"` 且
  boundary 为 3，窗口 `[0, 3)` 恰好终止于边界，结果为 0。
- 对 `sx_find_view_b` 而言，`boundary >= H` 是空区域，返回 `(size_t)-1`。
  对 `sx_rfind_view_b` 而言，大于等于 `H` 的 boundary 会夹紧到 `H`，
  等价于无界的最右查找。

**二进制安全。** 被查找的缓冲区始终按内容长度（`H`）处理，而不是按
C 字符串：内嵌的 `'\0'` 字节是普通字节，`ptr[H]` 处的结尾符不属于搜索
区域。特别地，`sx_find_char(h, '\0')` 只报告内嵌的 NUL 内容字节，永远不会
匹配结尾符。owned 和 view 形式的 needle 同样按长度读取；只有 `_cstr`
形式的 needle 会在第一个 NUL 处停止。

**哨兵值与 NULL。**

| 家族 | 未找到 / 拒绝输入时的结果 |
| --- | --- |
| `sx_find*`、`sx_rfind*` | `(size_t)-1` |
| `sx_find_all*` | `NULL` |
| `sx_cmp*` | 任一操作数为 NULL 时返回 `0` |
| `sx_starts_with*`、`sx_ends_with*` | `0` |
| `sx_split` | `NULL` |

查找函数会检查指针参数：`h` 为 NULL 或 needle 参数为 NULL 时返回该家族
的哨兵值，而不是崩溃。对 view 形式而言，`v.ref == NULL` 属于契约之外 ——
指针在任何长度检查之前就已形成，因此务必传入指向有效缓冲区的视图。
`sx_find_all*` 和 `sx_split` 用与“无匹配”相同的哨兵值报告分配失败；按
契约这两者无法区分。`(size_t)-1` 不会与真实的偏移冲突，因为偏移都小于
`INT_MAX` 个内容字节。

**查找策略。** 单字节 needle 使用 `memchr`（向前）或逐字节反向扫描（可用
时使用 glibc 的 `memrchr`），不进行分配。更长的 needle 使用 KMP：调用
期间会分配一张含 `m` 个 `size_t` 的临时前缀表，因此一次查找的最坏情况为
O(n + m)。`sx_find_all*` 还会额外返回一个 malloc 分配的结果数组，并报告
每一次出现，包括相互重叠的出现。

## 如何选择调用

| 你需要 | 使用 |
| --- | --- |
| 整串中最左的出现 | `sx_find*` |
| 整串中最右的出现 | `sx_rfind*` |
| 每一次出现，含重叠 | `sx_find_all*` |
| 边界处或之后最左的出现 | `sx_find_view_b` |
| 边界处或之前结束的最右出现 | `sx_rfind_view_b` |
| 相等或排序比较 | `sx_cmp*` |
| 前缀或后缀判定 | `sx_starts_with*`、`sx_ends_with*` |
| 分割为若干 token | `sx_split` |

## 最左查找：`sx_find*`

### sx_find

```c
size_t sx_find(const sx_t *h, const sx_t *needle);
```

查找另一个缓冲区的内容在 `h` 中最左的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 最左匹配的字节偏移；没有匹配时返回 `(size_t)-1`。
- **所有权：** 无；不修改任一缓冲区。needle 长于一个字节时，调用内部会
  分配并释放一张临时的 KMP 前缀表。
- **边界情况：** `h` 或 `needle` 为 NULL 时返回 `(size_t)-1`；空
  needle 返回 `(size_t)-1`；比 `h` 更长的 needle 永远不会匹配。匹配按
  内容长度字节精确进行，内嵌 NUL 同样参与匹配。

### sx_find_view

```c
size_t sx_find_view(const sx_t *h, sx_view_t needle);
```

查找视图中的字节在整串中最左的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 其 `needle.len` 个
  字节被查找的窗口。
- **返回：** 最左匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** 等价于 `sx_find_view_b(h, needle, 0)`。空视图
  （`len == 0`）永远不会被找到。`needle.ref == NULL` 或越界窗口属于
  契约之外。

### sx_find_view_b

```c
size_t sx_find_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

查找视图中的字节在 `boundary` 处或之后最左的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 要查找的窗口；
  `boundary` —— 区域起始偏移。
- **返回：** `[boundary, H)` 内最左匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** 匹配必须起始于 `boundary` 处或之后，并终止于 `H` 处或
  之前；跨过边界的窗口不算匹配。`boundary >= H`、区域放不下一个窗口、
  以及空 needle 都返回 `(size_t)-1`。`needle.ref == NULL`、越界窗口，或
  `boundary + needle.len` 使 `size_t` 溢出，属于契约之外。

```c
sx_t *h = sx_new_from_cstr("ababa");
sx_t *ba = sx_new_from_cstr("ba");
size_t at = sx_find_view_b(h, sx_view(ba), 2); /* 3, not 1 */
sx_free(ba);
sx_free(h);
```

### sx_find_cstr

```c
size_t sx_find_cstr(const sx_t *h, const char *needle);
```

查找 C 字符串在 `h` 中最左的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 以 NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 最左匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** `h` 或 `needle` 为 NULL 时返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`。needle 中不能包含字面 NUL；二进制模式请使用
  `sx_find_view`。被查找的缓冲区可以包含 NUL。

### sx_find_char

```c
size_t sx_find_char(const sx_t *h, char c);
```

查找单个字节最左的出现。

- **参数：** `h` —— 被查找的缓冲区；`c` —— 要查找的字节。
- **返回：** 最左出现的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** `h` 为 NULL 时返回 `(size_t)-1`。搜索只覆盖内容
  （`[0, H)`），因此 `c == '\0'` 会找到内嵌的 NUL 内容字节，但永远不会
  匹配结尾符。

## 最右查找：`sx_rfind*`

### sx_rfind

```c
size_t sx_rfind(const sx_t *h, const sx_t *needle);
```

查找另一个缓冲区的内容在 `h` 中最右的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；不修改任一缓冲区。needle 长于一个字节时，调用内部会
  分配并释放一张临时的 KMP 前缀表。
- **边界情况：** `h` 或 `needle` 为 NULL 时返回 `(size_t)-1`；空
  needle 返回 `(size_t)-1`；比 `h` 更长的 needle 永远不会匹配。字节
  精确且二进制安全。

### sx_rfind_view

```c
size_t sx_rfind_view(const sx_t *h, sx_view_t needle);
```

查找视图中的字节在整串中最右的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 其 `needle.len` 个
  字节被查找的窗口。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** 等价于 `sx_rfind_view_b(h, needle, H)`。空视图永远不会
  被找到。`needle.ref == NULL` 或越界窗口属于契约之外。

### sx_rfind_view_b

```c
size_t sx_rfind_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

查找视图中的字节在 `boundary` 处或之前结束的最右出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 要查找的窗口；
  `boundary` —— 区域终止偏移（不含）。
- **返回：** 窗口终止于 `[0, min(boundary, H))` 之内的最右匹配的字节
  偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** 匹配必须终止于 `boundary` 处或之前；伸出边界之外的
  窗口不算匹配。boundary 为 0、区域放不下一个窗口、以及空 needle 都返回
  `(size_t)-1`。大于等于 `H` 的 boundary 会夹紧到 `H`，等价于无界的最右
  查找。`needle.ref == NULL` 和越界窗口属于契约之外。

```c
sx_t *h = sx_new_from_cstr("ababa");
sx_t *aba = sx_new_from_cstr("aba");
size_t at = sx_rfind_view_b(h, sx_view(aba), 3); /* 0: ends exactly at 3 */
sx_free(aba);
sx_free(h);
```

### sx_rfind_cstr

```c
size_t sx_rfind_cstr(const sx_t *h, const char *needle);
```

查找 C 字符串在 `h` 中最右的出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 以 NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** `h` 或 `needle` 为 NULL 时返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`。needle 中不能包含字面 NUL。

### sx_rfind_char

```c
size_t sx_rfind_char(const sx_t *h, char c);
```

查找单个字节最右的出现。

- **参数：** `h` —— 被查找的缓冲区；`c` —— 要查找的字节。
- **返回：** 最右出现的字节偏移，或 `(size_t)-1`。
- **所有权：** 无；只读取。
- **边界情况：** `h` 为 NULL 时返回 `(size_t)-1`。搜索只覆盖内容，
  因此 `c == '\0'` 永远不会匹配结尾符。

## 全部出现：`sx_find_all*`

这些函数返回一个 malloc 分配的 `size_t` 数组，采用“计数在前”的形式：

```text
r[0]          number of matches
r[1 .. r[0]]  ascending byte offsets of every match
```

重叠的匹配会全部报告：`"aaaa"` 中的 `"aa"` 会在偏移 0、1、2 处各找到
一次。这里没有结束哨兵（与 `sx_split` 和 `sx_reg_match_all` 不同），
以计数为准。

- **返回：** 没有匹配时返回 `NULL` —— 包括空 needle 的情况 —— 参数为
  NULL 或分配失败时同样返回 `NULL`。按契约这两种失败原因无法区分。
- **所有权：** 调用方拥有数组，用 `free`（而不是 `sx_free`）释放。不要
  读取超过 `r[0] + 1` 个元素。

```c
sx_t *h = sx_new_from_cstr("ababab");
size_t *hits = sx_find_all_cstr(h, "ab");
/* hits[0] == 3; hits[1] == 0, hits[2] == 2, hits[3] == 4 */
free(hits);
sx_free(h);
```

### sx_find_all

```c
size_t *sx_find_all(const sx_t *h, const sx_t *needle);
```

收集另一个缓冲区内容的所有出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空 needle、NULL 参数或
  分配失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `free` 释放。
- **边界情况：** 二进制安全；偏移按升序排列，并包含重叠的匹配。

### sx_find_all_view

```c
size_t *sx_find_all_view(const sx_t *h, sx_view_t needle);
```

收集视图中字节的所有出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 要查找的窗口。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空视图、NULL 参数或分配
  失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `free` 释放。
- **边界情况：** `needle.ref == NULL` 或越界窗口属于契约之外。

### sx_find_all_cstr

```c
size_t *sx_find_all_cstr(const sx_t *h, const char *needle);
```

收集 C 字符串的所有出现。

- **参数：** `h` —— 被查找的缓冲区；`needle` —— 以 NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空 needle、NULL 参数或
  分配失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `free` 释放。
- **边界情况：** needle 中不能包含字面 NUL；被查找的缓冲区可以包含。

### sx_find_all_char

```c
size_t *sx_find_all_char(const sx_t *h, char c);
```

收集单个字节的所有出现。

- **参数：** `h` —— 被查找的缓冲区；`c` —— 要查找的字节。
- **返回：** 计数在前的 `size_t` 数组；无匹配、NULL 参数或分配失败时
  返回 `NULL`。
- **所有权：** 调用方所有；用 `free` 释放。
- **边界情况：** 只搜索内容，因此 `'\0'` needle 会找到内嵌的 NUL 内容
  字节，永远不会匹配结尾符。

## 比较：`sx_cmp*`

### sx_cmp

```c
int sx_cmp(const sx_t *h, const sx_t *other);
```

按内容字节对两个缓冲区做字典序比较。

- **参数：** `h` —— 左操作数；`other` —— 右操作数。
- **返回：** `h` 排在 `other` 之前时返回负数，相等返回 0，排在之后返回
  正数。只有符号有意义。
- **所有权：** 无；不修改任一缓冲区。
- **边界情况：** 比较按 `memcmp` 顺序逐字节进行（无符号字节值），因此
  `"10" < "2"`、`"Z" < "中"`。若较短字符串是另一方的真前缀，则它排在
  前面（`"abc" < "abcd"`）。内嵌 NUL 字节按数据参与比较。任一操作数为
  NULL 时结果为 0（视为相等）—— 不要把该结果当作有意义的排序依据。

### sx_cmp_view

```c
int sx_cmp_view(const sx_t *h, sx_view_t other);
```

把一个缓冲区与视图中的字节比较。

- **参数：** `h` —— 左操作数；`other` —— 右操作数窗口，比较其
  `other.len` 个字节。
- **返回：** 与 `sx_cmp` 相同的负值、0 或正值。
- **所有权：** 无；只读取。
- **边界情况：** `h` 为 NULL 时返回 0。`other.ref == NULL` 或越界窗口
  属于契约之外。

### sx_cmp_cstr

```c
int sx_cmp_cstr(const sx_t *h, const char *s);
```

把一个缓冲区与 C 字符串比较。

- **参数：** `h` —— 左操作数；`s` —— 以 NUL 结尾的右操作数，按
  `strlen` 读取。
- **返回：** 与 `sx_cmp` 相同的负值、0 或正值。
- **所有权：** 无；只读取。
- **边界情况：** `h` 或 `s` 为 NULL 时返回 0。`s` 中的内嵌 NUL 会缩短
  参与比较的范围。

## 前缀与后缀判定

### sx_starts_with

```c
int sx_starts_with(const sx_t *h, const sx_t *prefix);
```

判断 `h` 是否以另一个缓冲区的内容开头。

- **参数：** `h` —— 缓冲区；`prefix` —— 内容作为前缀的缓冲区。
- **返回：** `h` 的前 `len(prefix)` 个内容字节与 prefix 逐字节相等时返回
  1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空前缀总是匹配；比 `h` 更长的 prefix 永远不匹配。字节
  精确且二进制安全。`h` 或 `prefix` 为 NULL 时返回 0。

### sx_starts_with_view

```c
int sx_starts_with_view(const sx_t *h, sx_view_t prefix);
```

判断 `h` 是否以视图中的字节开头。

- **参数：** `h` —— 缓冲区；`prefix` —— 按 `prefix.len` 个字节读取的
  窗口。
- **返回：** 匹配时返回 1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空视图总是匹配。`h` 为 NULL 时返回 0；`prefix.ref ==
  NULL` 或越界窗口属于契约之外。

### sx_starts_with_cstr

```c
int sx_starts_with_cstr(const sx_t *h, const char *prefix);
```

判断 `h` 是否以 C 字符串开头。

- **参数：** `h` —— 缓冲区；`prefix` —— 以 NUL 结尾的前缀，按 `strlen`
  读取。
- **返回：** 匹配时返回 1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空字符串总是匹配；`h` 或 `prefix` 为 NULL 时返回 0。

### sx_ends_with

```c
int sx_ends_with(const sx_t *h, const sx_t *suffix);
```

判断 `h` 是否以另一个缓冲区的内容结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— 内容作为后缀的缓冲区。
- **返回：** `h` 的后 `len(suffix)` 个内容字节与 suffix 逐字节相等时返回
  1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空后缀总是匹配；比 `h` 更长的 suffix 永远不匹配。字节
  精确且二进制安全。`h` 或 `suffix` 为 NULL 时返回 0。

### sx_ends_with_view

```c
int sx_ends_with_view(const sx_t *h, sx_view_t suffix);
```

判断 `h` 是否以视图中的字节结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— 按 `suffix.len` 个字节读取的
  窗口。
- **返回：** 匹配时返回 1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空视图总是匹配。`h` 为 NULL 时返回 0；`suffix.ref ==
  NULL` 或越界窗口属于契约之外。

### sx_ends_with_cstr

```c
int sx_ends_with_cstr(const sx_t *h, const char *suffix);
```

判断 `h` 是否以 C 字符串结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— 以 NUL 结尾的后缀，按 `strlen`
  读取。
- **返回：** 匹配时返回 1，否则返回 0。
- **所有权：** 无；只读取。
- **边界情况：** 空字符串总是匹配；`h` 或 `suffix` 为 NULL 时返回 0。

## 分割：sx_split

### sx_split

```c
sx_view_t *sx_split(const sx_t *h, const char *sep);
```

按 C 字符串分隔符把缓冲区分割成一组 token 视图。该声明位于 `sx.h`
（进阶层），不在 `sx_core.h` 中。

- **参数：** `h` —— 要分割的缓冲区；`sep` —— 以 NUL 结尾的分隔符，按
  `strlen` 读取。空分隔符会被拒绝，而不是按“每个字节”处理。
- **返回：** 一个 malloc 分配的 `sx_view_t` 数组，采用经典的 `char **`
  形态；`h` 为 NULL、`sep` 为 NULL、`sep` 为空，或分配失败时返回
  `NULL`。
- **所有权：** 调用方拥有数组，用 `free`（而不是 `sx_free`）释放。token
  是 `h` 上的视图，绝不能单独释放。
- **契约说明：**
  - 数组恰有 `count + 1` 个元素：`arr[0] .. arr[count-1]` 是按顺序排列
    的 token，`arr[count]` 是 `.ref == NULL` 的结束哨兵。遍历方式为
    `for (size_t i = 0; arr[i].ref != NULL; i++)`。
  - `count == 1 + (分隔符出现次数)`。分隔符从左到右非重叠匹配：命中后
    扫描越过该分隔符继续，因此用 `"aa"` 分割 `"aaaa"` 得到三个空 token，
    而不是两个。
  - 每个位置的空字段都会保留：`"a,,b"` 得到 `"a"`、`""`、`"b"`；开头或
    结尾的分隔符会产生空的第一个或最后一个 token；空 `h` 分割为单个空
    token `""`；内容与分隔符完全相同的字符串分割为两个空 token。
  - 空 token 不是结束哨兵：它的 `ref != NULL` 且 `len == 0`，而哨兵的
    `ref == NULL`。
  - token 是 `h` 内容的别名（`ref == h->ref`，以及 `offset`、`len`）：
    调用过程中不拷贝、不修改、也不重新分配，因此内容指针按位保持不变。
    之后对 `h` 的修改会使 token 失效；token 在使用时不要修改 `h`。
  - 匹配是字节精确的，因此 token 中可能包含内嵌的 NUL 字节（分隔符本身
    是 C 字符串，不能包含）。打印这类 token 请使用 `fwrite` 或带长度的
    `"%.*s"`，不要使用 `"%s"`。

正则层的 `sx_reg_split` 返回完全相同的形态，见[正则表达式](regex.md)。

完整示例 —— 分割、遍历到结束哨兵，然后释放数组和缓冲区：

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("alpha,beta,,gamma,");
  sx_view_t *fields = sx_split(csv, ",");

  if (!fields)
    return 1;

  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("field %zu: \"%.*s\" (offset %zu, len %zu)\n", i,
           (int)fields[i].len, sx_buf_view_mut(fields[i]),
           fields[i].offset, fields[i].len);

  free(fields); /* the array, not the tokens */
  sx_free(csv);
  return 0;
}
```

预期输出（结尾的分隔符产生一个空的最后字段）：

```text
field 0: "alpha" (offset 0, len 5)
field 1: "beta" (offset 6, len 4)
field 2: "" (offset 11, len 0)
field 3: "gamma" (offset 12, len 5)
field 4: "" (offset 18, len 0)
```

---

[← 上一页](api-buffer.md) · [文档首页](README.md) · [下一页 →](regex.md)
