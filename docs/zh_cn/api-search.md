[English](../en_us/api-search.md) · 中文

[文档首页](README.md) › API 参考：查找、比较与分割

# 查找、比较与分割

本页涵盖查找（`sx_find*`、`sx_rfind*`、`sx_find_all*`）、比较
（`sx_cmp*`、`sx_starts_with*`、`sx_ends_with*`）与 `sx_split` API。全部
字节精确并按内容长度工作，内嵌 NUL 字节是普通数据；缓冲区、修改与视图
见[缓冲区与视图](api-buffer.md)。

## 约定

**needle 的形式。** 每个查找家族都有同样的四种 needle 形态：

| 后缀 | 参数类型 | 匹配的字节 | NUL 行为 |
| --- | --- | --- | --- |
| 无 | `const sx_t *` | 该缓冲区的内容字节 | 内嵌 NUL 是数据 |
| `_view` | `sx_view_t` | `v.ref->ptr + v.offset` 处的 `v.len` 个字节 | 内嵌 NUL 是数据 |
| `_cstr` | `const char *` | `strlen(...)` 个字节 | 第一个 NUL 结束 needle |
| `_char` | `char` | 恰好一个字节 | `'\0'` 表示一个 NUL 内容字节 |

owned 与 view 形式二进制安全：精确读取 needle 长度，内嵌 `'\0'` 作为
数据参与匹配。`_cstr` 形式使用 `strlen`，所以
`sx_find_cstr(h, "a\0c")` 实际查找 `"a"`；二进制模式请用 owned/view
needle。长度为 0 的 needle 永远不会被找到。

**有界区域。** 设 `H` 为被查找缓冲区的内容长度、needle 长度为 `m`，一次
匹配是窗口 `[p, p + m)`，必须完全位于被搜索区域之内：

| 函数 | 搜索区域 | 窗口条件 |
| --- | --- | --- |
| `sx_find*`、`sx_rfind*`（无界） | `[0, H)` | `p + m <= H` |
| `sx_find_view_b(h, needle, boundary)` | `[boundary, H)` | `p >= boundary` 且 `p + m <= H` |
| `sx_rfind_view_b(h, needle, boundary)` | `[0, min(boundary, H))` | `p + m <= min(boundary, H)` |

两个有界形式都接收 `size_t boundary`，方向相反：`sx_find_view_b` 从左侧
限定区域（起点早于 `boundary` 的窗口不计入）；`sx_rfind_view_b` 从右侧
限定区域（终点晚于边界的窗口不计入）。测试用例取 `h = "ababa"`、needle
为 `"ba"`：

- `sx_find_view_b(h, nd, 2)` 为 3（偏移 1 处的匹配起点在边界之前）；
  boundary 为 4 时返回 `(size_t)-1` —— 只剩 `"a"`，放不下一个窗口。
- `sx_rfind_view_b(h, nd, 3)` 为 1（偏移 3 处的匹配终止于 5，越过边界）；
  boundary 为 2 时返回 `(size_t)-1`；needle 为 `"aba"`、boundary 为 3 时
  结果为 0 —— 窗口 `[0, 3)` 恰好终止于边界。
- 对 `sx_find_view_b`，`boundary >= H` 是空区域，返回 `(size_t)-1`；对
  `sx_rfind_view_b`，大于等于 `H` 的 boundary 夹紧到 `H`，等价于无界
  最右查找。

**二进制安全。** 被查找缓冲区始终按内容长度（`H`）处理而非 C 字符串：
内嵌 `'\0'` 是普通字节，`ptr[H]` 处的结尾符不属于搜索区域。因此
`sx_find_char(h, '\0')` 只找到内嵌 NUL 内容字节。owned/view needle 也按
长度读取；只有 `_cstr` needle 在第一个 NUL 处停止。

**哨兵值与 NULL。**

| 家族 | 未找到 / 拒绝输入时的结果 |
| --- | --- |
| `sx_find*`、`sx_rfind*` | `(size_t)-1` |
| `sx_find_all*` | `NULL` |
| `sx_cmp*` | 任一操作数为 NULL 时返回 `0` |
| `sx_starts_with*`、`sx_ends_with*` | `0` |
| `sx_split` | `NULL` |

查找函数检查指针参数：`h` 或 needle 参数为 NULL 时返回家族哨兵值，而不是
崩溃。对 view 形式，`v.ref == NULL` 属于契约之外 —— 指针在任何长度检查
之前已形成，务必传入指向有效缓冲区的视图。所有调用只读取输入、不转移
所有权；只有 `sx_find_all*` 和 `sx_split` 的数组归调用方所有。这两个函数
也用与“无匹配”相同的哨兵值报告分配失败，按契约两者无法区分。
`(size_t)-1` 不会与真实偏移冲突，因为偏移都小于 `INT_MAX` 个内容字节。

**查找策略。** 单字节 needle 使用 `memchr`（向前）或逐字节反向扫描（可用
时用 glibc 的 `memrchr`），不分配内存。更长的 needle 使用 KMP：调用期间
分配一张含 `m` 个 `size_t` 的临时前缀表并在返回前释放，一次查找最坏为
O(n + m)。`sx_find_all*` 还额外分配结果数组，并报告每一次出现（含重叠）。

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

查找另一缓冲区的内容在 `h` 中最左的出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 最左匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 或 `needle` 为 NULL 返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`；比 `h` 更长的 needle 永不匹配。按内容长度字节精确
  匹配，内嵌 NUL 也参与。

### sx_find_view

```c
size_t sx_find_view(const sx_t *h, sx_view_t needle);
```

查找视图字节在整串中最左的出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 其 `needle.len` 个字节被
  查找的窗口。
- **返回：** 最左匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** 等价于 `sx_find_view_b(h, needle, 0)`。空视图
  （`len == 0`）永不被找到。`needle.ref == NULL` 或越界窗口属于契约
  之外。

### sx_find_view_b

```c
size_t sx_find_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

查找视图字节在 `boundary` 处或之后最左的出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 要查找的窗口；
  `boundary` —— 区域起始偏移。
- **返回：** `[boundary, H)` 内最左匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** 匹配必须起始于 `boundary` 处或之后、终止于 `H` 处或
  之前；跨边界的窗口不算。`boundary >= H`、区域放不下一个窗口、空
  needle 都返回 `(size_t)-1`。`needle.ref == NULL`、越界窗口，或
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

- **参数：** `h` —— 被查找缓冲区；`needle` —— NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 最左匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 或 `needle` 为 NULL 返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`。needle 不能含字面 NUL，二进制模式用
  `sx_find_view`；被查找缓冲区可以含 NUL。

### sx_find_char

```c
size_t sx_find_char(const sx_t *h, char c);
```

查找单个字节最左的出现。

- **参数：** `h` —— 被查找缓冲区；`c` —— 要查找的字节。
- **返回：** 最左出现的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 为 NULL 返回 `(size_t)-1`。只搜索内容（`[0, H)`），
  因此 `c == '\0'` 找到内嵌 NUL 内容字节，永不匹配结尾符。

## 最右查找：`sx_rfind*`

### sx_rfind

```c
size_t sx_rfind(const sx_t *h, const sx_t *needle);
```

查找另一缓冲区的内容在 `h` 中最右的出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 或 `needle` 为 NULL 返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`；比 `h` 更长的 needle 永不匹配。字节精确且二进制
  安全。

### sx_rfind_view

```c
size_t sx_rfind_view(const sx_t *h, sx_view_t needle);
```

查找视图字节在整串中最右的出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 其 `needle.len` 个字节被
  查找的窗口。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** 等价于 `sx_rfind_view_b(h, needle, H)`。空视图永不被
  找到。`needle.ref == NULL` 或越界窗口属于契约之外。

### sx_rfind_view_b

```c
size_t sx_rfind_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

查找视图字节在 `boundary` 处或之前结束的最右出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 要查找的窗口；
  `boundary` —— 区域终止偏移（不含）。
- **返回：** 窗口终止于 `[0, min(boundary, H))` 内的最右匹配的字节偏移，
  或 `(size_t)-1`。
- **边界情况：** 匹配必须终止于 `boundary` 处或之前；伸出边界的窗口不
  算。boundary 为 0、区域放不下一个窗口、空 needle 都返回
  `(size_t)-1`；大于等于 `H` 的 boundary 夹紧到 `H`，等价于无界最右
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

- **参数：** `h` —— 被查找缓冲区；`needle` —— NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 最右匹配的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 或 `needle` 为 NULL 返回 `(size_t)-1`；空 needle
  返回 `(size_t)-1`。needle 不能含字面 NUL。

### sx_rfind_char

```c
size_t sx_rfind_char(const sx_t *h, char c);
```

查找单个字节最右的出现。

- **参数：** `h` —— 被查找缓冲区；`c` —— 要查找的字节。
- **返回：** 最右出现的字节偏移，或 `(size_t)-1`。
- **边界情况：** `h` 为 NULL 返回 `(size_t)-1`。只搜索内容，因此
  `c == '\0'` 永不匹配结尾符。

## 全部出现：`sx_find_all*`

这些函数返回 malloc 分配的 `size_t` 数组，采用“计数在前”形式：

```text
r[0]          number of matches
r[1 .. r[0]]  ascending byte offsets of every match
```

重叠匹配全部报告：`"aaaa"` 中的 `"aa"` 在偏移 0、1、2 处各找到一次。
这里没有结束哨兵（与 `sx_split`、`sx_reg_match_all` 不同），以计数为准。

- **返回：** 无匹配时返回 `NULL`（含空 needle）、参数为 NULL 或分配失败
  时同样返回 `NULL`；无匹配与分配失败按契约无法区分。
- **所有权：** 调用方拥有数组，用 `free`（而非 `sx_free`）释放；不要读取
  超过 `r[0] + 1` 个元素。

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

收集另一缓冲区内容的所有出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 内容被查找的缓冲区。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空 needle、NULL 参数或
  分配失败返回 `NULL`。
- **边界情况：** 二进制安全；偏移升序且包含重叠匹配。

### sx_find_all_view

```c
size_t *sx_find_all_view(const sx_t *h, sx_view_t needle);
```

收集视图字节的所有出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— 要查找的窗口。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空视图、NULL 参数或分配
  失败返回 `NULL`。
- **边界情况：** `needle.ref == NULL` 或越界窗口属于契约之外。

### sx_find_all_cstr

```c
size_t *sx_find_all_cstr(const sx_t *h, const char *needle);
```

收集 C 字符串的所有出现。

- **参数：** `h` —— 被查找缓冲区；`needle` —— NUL 结尾的模式，按
  `strlen` 读取。
- **返回：** 计数在前的 `size_t` 数组；无匹配、空 needle、NULL 参数或
  分配失败返回 `NULL`。
- **边界情况：** needle 不能含字面 NUL；被查找缓冲区可以含。

### sx_find_all_char

```c
size_t *sx_find_all_char(const sx_t *h, char c);
```

收集单个字节的所有出现。

- **参数：** `h` —— 被查找缓冲区；`c` —— 要查找的字节。
- **返回：** 计数在前的 `size_t` 数组；无匹配、NULL 参数或分配失败返回
  `NULL`。
- **边界情况：** 只搜索内容，因此 `'\0'` needle 找到内嵌 NUL 内容字节，
  永不匹配结尾符。

## 比较：`sx_cmp*`

### sx_cmp

```c
int sx_cmp(const sx_t *h, const sx_t *other);
```

按内容字节对两个缓冲区做字典序比较。

- **参数：** `h` —— 左操作数；`other` —— 右操作数。
- **返回：** `h` 排在 `other` 之前返回负数，相等返回 0，之后返回正数；
  只有符号有意义。
- **边界情况：** 按 `memcmp` 顺序逐字节比较（无符号字节值），因此
  `"10" < "2"`、`"Z" < "中"`。较短字符串若是另一方的真前缀则排在前
  （`"abc" < "abcd"`）。内嵌 NUL 按数据比较。任一操作数为 NULL 时结果
  为 0（视为相等）—— 不要当作有意义的排序依据。

### sx_cmp_view

```c
int sx_cmp_view(const sx_t *h, sx_view_t other);
```

把缓冲区与视图字节比较。

- **参数：** `h` —— 左操作数；`other` —— 右操作数窗口，比较其
  `other.len` 个字节。
- **返回：** 与 `sx_cmp` 相同的负值、0 或正值。
- **边界情况：** `h` 为 NULL 返回 0。`other.ref == NULL` 或越界窗口属于
  契约之外。

### sx_cmp_cstr

```c
int sx_cmp_cstr(const sx_t *h, const char *s);
```

把缓冲区与 C 字符串比较。

- **参数：** `h` —— 左操作数；`s` —— NUL 结尾的右操作数，按 `strlen`
  读取。
- **返回：** 与 `sx_cmp` 相同的负值、0 或正值。
- **边界情况：** `h` 或 `s` 为 NULL 返回 0。`s` 中的内嵌 NUL 缩短参与
  比较的范围。

## 前缀与后缀判定

### sx_starts_with

```c
int sx_starts_with(const sx_t *h, const sx_t *prefix);
```

判断 `h` 是否以另一缓冲区的内容开头。

- **参数：** `h` —— 缓冲区；`prefix` —— 内容作为前缀的缓冲区。
- **返回：** `h` 的前 `len(prefix)` 个内容字节逐字节等于 prefix 时返回
  1，否则返回 0。
- **边界情况：** 空前缀总匹配；比 `h` 更长的 prefix 永不匹配。字节精确
  且二进制安全。`h` 或 `prefix` 为 NULL 返回 0。

### sx_starts_with_view

```c
int sx_starts_with_view(const sx_t *h, sx_view_t prefix);
```

判断 `h` 是否以视图字节开头。

- **参数：** `h` —— 缓冲区；`prefix` —— 按 `prefix.len` 个字节读取的
  窗口。
- **返回：** 匹配返回 1，否则返回 0。
- **边界情况：** 空视图总匹配。`h` 为 NULL 返回 0；`prefix.ref == NULL`
  或越界窗口属于契约之外。

### sx_starts_with_cstr

```c
int sx_starts_with_cstr(const sx_t *h, const char *prefix);
```

判断 `h` 是否以 C 字符串开头。

- **参数：** `h` —— 缓冲区；`prefix` —— NUL 结尾的前缀，按 `strlen`
  读取。
- **返回：** 匹配返回 1，否则返回 0。
- **边界情况：** 空字符串总匹配；`h` 或 `prefix` 为 NULL 返回 0。

### sx_ends_with

```c
int sx_ends_with(const sx_t *h, const sx_t *suffix);
```

判断 `h` 是否以另一缓冲区的内容结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— 内容作为后缀的缓冲区。
- **返回：** `h` 的后 `len(suffix)` 个内容字节逐字节等于 suffix 时返回
  1，否则返回 0。
- **边界情况：** 空后缀总匹配；比 `h` 更长的 suffix 永不匹配。字节精确
  且二进制安全。`h` 或 `suffix` 为 NULL 返回 0。

### sx_ends_with_view

```c
int sx_ends_with_view(const sx_t *h, sx_view_t suffix);
```

判断 `h` 是否以视图字节结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— 按 `suffix.len` 个字节读取的
  窗口。
- **返回：** 匹配返回 1，否则返回 0。
- **边界情况：** 空视图总匹配。`h` 为 NULL 返回 0；`suffix.ref == NULL`
  或越界窗口属于契约之外。

### sx_ends_with_cstr

```c
int sx_ends_with_cstr(const sx_t *h, const char *suffix);
```

判断 `h` 是否以 C 字符串结尾。

- **参数：** `h` —— 缓冲区；`suffix` —— NUL 结尾的后缀，按 `strlen`
  读取。
- **返回：** 匹配返回 1，否则返回 0。
- **边界情况：** 空字符串总匹配；`h` 或 `suffix` 为 NULL 返回 0。

## 分割：sx_split

### sx_split

```c
sx_view_t *sx_split(const sx_t *h, const char *sep);
```

按 C 字符串分隔符把缓冲区分割成 token 视图数组。声明位于 `sx.h`
（进阶层），不在 `sx_core.h`。

- **参数：** `h` —— 要分割的缓冲区；`sep` —— NUL 结尾的分隔符，按
  `strlen` 读取。空分隔符会被拒绝，而非按“每个字节”处理。
- **返回：** malloc 分配的 `sx_view_t` 数组（经典 `char **` 形态）；
  `h` 为 NULL、`sep` 为 NULL、`sep` 为空或分配失败时返回 `NULL`。
- **所有权：** 调用方拥有数组，用 `free`（而非 `sx_free`）释放；token 是
  `h` 上的视图，绝不能单独释放。
- **契约说明：**
  - 数组恰有 `count + 1` 个元素：`arr[0] .. arr[count-1]` 为按序 token，
    `arr[count]` 是 `.ref == NULL` 的结束哨兵。遍历方式为
    `for (size_t i = 0; arr[i].ref != NULL; i++)`。
  - `count == 1 + (分隔符出现次数)`。分隔符从左到右非重叠匹配：命中后
    扫描越过它继续，因此用 `"aa"` 分割 `"aaaa"` 得到三个空 token 而非
    两个。
  - 每个位置的空字段都保留：`"a,,b"` 得到 `"a"`、`""`、`"b"`；开头或
    结尾分隔符产生空的第一个或最后一个 token；空 `h` 分割为单个空 token
    `""`；与分隔符完全相同的字符串分割为两个空 token。
  - 空 token 不是结束哨兵：其 `ref != NULL` 且 `len == 0`，哨兵则是
    `ref == NULL`。
  - token 是 `h` 内容的别名（`ref == h->ref`，以及 `offset`、`len`）：
    不拷贝、不修改、不重新分配，内容指针按位不变。之后对 `h` 的修改会使
    token 失效；使用期间不要修改 `h`。
  - 匹配字节精确，token 中可能含内嵌 NUL 字节（分隔符本身是 C 字符串，
    不能含）。打印这类 token 用 `fwrite` 或带长度的 `"%.*s"`，不要用
    `"%s"`。

正则层的 `sx_reg_split` 返回完全相同的形态，见[正则表达式](regex.md)。

完整示例 —— 分割、遍历到结束哨兵，再释放数组和缓冲区：

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

预期输出（结尾分隔符产生空的最后字段）：

```text
field 0: "alpha" (offset 0, len 5)
field 1: "beta" (offset 6, len 4)
field 2: "" (offset 11, len 0)
field 3: "gamma" (offset 12, len 5)
field 4: "" (offset 18, len 0)
```

---

[← 上一页](api-buffer.md) · [文档首页](README.md) · [下一页 →](regex.md)
