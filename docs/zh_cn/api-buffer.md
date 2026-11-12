[English](../en_us/api-buffer.md) · 中文

[文档首页](README.md) › API 参考：缓冲区与视图

# 缓冲区与视图 API 参考

本页是 `include/sx_core.h` 中核心缓冲区与视图 API 的完整参考：创建与释放
缓冲区、读取状态、修改内容、截取子串，以及移动和裁剪视图。查找、比较与
分割由[查找、比较与分割](api-search.md)一并说明。

下文每一条行为描述都来自公开头文件中的契约、实现代码，以及
`tests/*.c` 中的行为边界注释。

## 约定

**头文件与分层。** 包含唯一的公开头文件：

```c
#include <sx.h>
```

`sx.h` 包含 `sx_core.h`（缓冲区、视图、查找、比较），随后声明 `sx_split`
和 `sx_reg_*` 家族。链接库时使用 `-lstringx`。本页所有函数都声明在
`sx_core.h` 中。`sx_raw.h` 是内部的分配/调试接口，不属于受支持的公开
接口。

**每个缓冲区一次分配。** 一个 `sx_t` 句柄拥有恰好一段以 NUL 结尾的字节
数组：`ptr[0 .. len-1]` 是内容字节，`ptr[len]` 恒为 `'\0'`，`cap` 是分配
大小，且 `cap >= len + 1`。`len` 统计内容字节，因此内嵌的 `'\0'` 是普通
数据而不是结尾符 —— 该模型天然二进制安全。

**所有权。**

| 值 | 所有者 | 释放方式 |
| --- | --- | --- |
| `sx_new*`、`sx_dup`、`sx_substr` 返回的缓冲区 | 调用方 | `sx_free` |
| `sx_append`、`sx_write`、`sx_insert` 的 `owned` 参数 | 转移给库 | 无需操作：调用会释放它 |
| 修改类函数返回的缓冲区 | 与 `self` 是同一个句柄 | 对最初的句柄调用一次 `sx_free` |
| `sx_view_t` | 无：它仅别名一段缓冲区 | 无需释放；视图从不单独释放 |
| `sx_find_all*` 或 `sx_split` 返回的数组 | 调用方 | `free`（见 [api-search.md](api-search.md)） |

每次修改都返回 `self`，因此调用可以链式书写，句柄保持稳定。缓冲区增长时
底层数组可能搬移：`sx_buf_mut`/`sx_buf_view_mut` 返回的指针在任何可能
重新分配的调用之后都会失效，必须重新获取。

**NULL 与契约之外。** 本页明确记录的 NULL 处理只有：`sx_new_from_cstr(NULL)`
和 `sx_dup(NULL)` 返回 `NULL`；`sx_append`、`sx_write`、`sx_insert` 的
`owned` 形式以及所有 `_cstr` 形式在第二个参数为 `NULL` 时返回 `NULL` 且
保持 `self` 不变。其余函数都会解引用缓冲区或窗口：`sx_free`、`sx_len`、
`sx_cap`、`sx_buf_mut`、`sx_view`、视图移动家族以及修改类函数都不接受
NULL 句柄；`sx_new_from_view`、`sx_buf_view_mut` 和 `sx_view_*` 也不接受
`ref == NULL` 的视图（`sx_len_view` 只读取存储的长度）。NULL 句柄或
`v.ref`、已释放的句柄、越出底层缓冲区的视图、重复释放、在消耗所有权的
调用之后继续使用、把 `self` 作为 `owned` 参数传入，以及违反各条目所述
条件的下标，全部属于**契约之外**：行为未定义，而不是有文档化的错误
返回值。

**哨兵值。**

| 情形 | 取值 |
| --- | --- |
| 分配失败 | `NULL` |
| 拒绝 NULL 操作数（有文档记录处） | `NULL` |
| “没有视图” | `.ref == NULL` |
| 未找到 / 无匹配（查找页） | `(size_t)-1` |
| 无匹配 / 拒绝分割（查找页） | `NULL` |

**大小是 `int`。** `sx_len`、`sx_len_view` 和 `sx_cap` 返回 `int`，而视图
字段和查找偏移是 `size_t`。内容超过 `INT_MAX` 字节的情况不在测试契约
范围内；两者混用时请转换为 `(size_t)`，例如
`fwrite(sx_buf_mut(h), 1, (size_t)sx_len(h), stdout)`。

**视图是快照。** `sx_view_t {ref, offset, len}` 表示 `ref` 所指缓冲区内的
字节窗口 `[offset, offset + len)`；它不拷贝、也不修改任何东西。合法的窗口
必须位于缓冲区内（`offset + len <= len(ref)`）。之后任何重新分配或改变
长度的修改都会使视图失去意义：这类修改之后要重新推导视图；有视图（例如
`sx_split` 数组）在使用时不要修改其底层缓冲区。`ref == NULL` 表示“没有
视图”，它不是可用的缓冲区。

**容量增长。** `sx_new` 的初始容量是 32 字节。增长是几何式的（先翻倍，
容量达到 64 KiB 之后改为 `+0x10000`）；使用 `-DSX_FORCE_EXACT_RESERVE`
的调试构建会把每次分配精确设为 `need + 1`。容量从不收缩 —— `sx_truncate`
和 `sx_clear` 都会保留容量 —— 任何函数也不会承诺其所述保证之外的空闲
容量。

## 创建与释放

### sx_new

```c
sx_t *sx_new(void);
```

创建一个初始容量为 32 字节的空缓冲区。

- **参数：** 无。
- **返回：** 新缓冲区（`len == 0`、`cap == 32`、`ptr[0] == '\0'`），
  分配失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `sx_free` 释放。
- **边界情况：** 确切的初始容量属于实现细节；只有 `cap >= 1` 有保证。

### sx_new_from_cstr

```c
sx_t *sx_new_from_cstr(const char *s);
```

创建一个存放 C 字符串 `s` 副本的缓冲区。

- **参数：** `s` —— 以 NUL 结尾的源字符串，按 `strlen` 读取。
- **返回：** 新的调用方所有的缓冲区；`s == NULL` 或分配失败时返回
  `NULL`。
- **所有权：** 调用方所有；用 `sx_free` 释放。
- **边界情况：** 采用 `strlen` 语义 —— 内嵌的 `'\0'` 会截断副本。二进制
  内容请使用 `sx_new_from_view`。

### sx_new_from_view

```c
sx_t *sx_new_from_view(sx_view_t v);
```

把视图中的字节拷贝成一个新的独立缓冲区。

- **参数：** `v` —— 源窗口；从 `v.ref->ptr + v.offset` 读取 `v.len`
  个字节。
- **返回：** 新的调用方所有的缓冲区；分配失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `sx_free` 释放。
- **边界情况：** 字节精确且二进制安全：内嵌的 `'\0'` 字节会被拷贝。两个
  缓冲区之间不存在别名 —— 修改任一方永远不影响另一方。`v.ref == NULL` 或
  越界窗口属于契约之外。

### sx_new_with_cap

```c
sx_t *sx_new_with_cap(size_t n);
```

创建一个恰好分配 `n` 个字节的空缓冲区。

- **参数：** `n` —— 分配的总字节数。
- **返回：** `n >= 1` 时 `cap == n` 的新缓冲区；分配失败时返回 `NULL`。
- **所有权：** 调用方所有；用 `sx_free` 释放。
- **边界情况：** 其中一个字节留给结尾符，因此最多容纳 `n - 1` 个内容
  字节。`n == 0` 时仍会分配一个字节（`cap == 1`），句柄始终有效。

```c
sx_t *buf = sx_new_with_cap(64); /* room for 63 content bytes */
buf = sx_append_cstr(buf, "hello"); /* no reallocation */
sx_free(buf);
```

### sx_dup

```c
sx_t *sx_dup(const sx_t *src);
```

创建缓冲区的深拷贝，副本完全独立。

- **参数：** `src` —— 源缓冲区。
- **返回：** 内容相同的新缓冲区；`src == NULL` 或分配失败时返回
  `NULL`。
- **所有权：** 调用方所有；副本与源分别用 `sx_free` 释放。
- **边界情况：** 二进制安全。v0.5 中副本的容量是 `len + 1`，而不是源的
  容量。

```c
sx_t *src = sx_new_from_cstr("hello");
sx_t *copy = sx_dup(src); /* independent: free both */
sx_free(copy);
sx_free(src);
```

### sx_free

```c
void sx_free(sx_t *h);
```

释放缓冲区：内容数组和句柄本身。

- **参数：** `h` —— 调用方持有所有权的有效句柄。
- **返回：** 无。
- **所有权：** 消耗 `h`；所有指向该缓冲区的视图和 `sx_buf_mut` 指针都
  随之失效。
- **边界情况：** `h == NULL` 属于契约之外 —— 该函数会解引用句柄（正则层
  的 `sx_reg_free(NULL)` 空操作不适用于这里）。重复释放和释放后使用同样
  属于契约之外。

## 访问器

### sx_len

```c
int sx_len(sx_t *h);
```

返回内容字节数。

- **参数：** `h` —— 缓冲区。
- **返回：** `len`，内嵌的 `'\0'` 字节也计入；结尾符从不计入。
- **所有权：** 无；不修改缓冲区。
- **边界情况：** `h == NULL` 属于契约之外。返回值是 `int`；参见上文的
  大小说明。

### sx_len_view

```c
int sx_len_view(sx_view_t v);
```

返回视图中存储的长度。

- **参数：** `v` —— 要读取长度的视图。
- **返回：** `(int)v.len`；不会查看底层缓冲区，也不会校验窗口边界。
- **所有权：** 无。
- **边界情况：** `v.len` 超过 `INT_MAX` 不在测试契约范围内。“没有视图”
  哨兵值（`ref == NULL`）的 `len` 为 0。

### sx_cap

```c
int sx_cap(sx_t *h);
```

返回为内容数组分配的总字节数。

- **参数：** `h` —— 缓冲区。
- **返回：** `cap`，它始终至少为 `len + 1`（单 NUL 结尾不变式）。
- **所有权：** 无；不修改缓冲区。
- **边界情况：** `h == NULL` 属于契约之外。`len + 1` 之外的空闲容量取决于
  增长策略，不构成契约。

### sx_buf_mut

```c
char *sx_buf_mut(sx_t *h);
```

返回指向内容数组的可写指针。

- **参数：** `h` —— 缓冲区。
- **返回：** `ptr`；任何时刻都有 `ptr[len] == '\0'`。
- **所有权：** 借用；调用方可以在 `[0, len)` 内改写字节而不改变 `len`。
- **边界情况：** 没有改变长度的调用时，不得在 `len` 及其之后写入；也不得
  跨过任何可能重新分配的调用保存该指针（`sx_append*`、`sx_write*`、
  `sx_insert*`、`sx_reserve`、会增长的 `sx_truncate`）。`h == NULL` 属于
  契约之外。

```c
char *p = sx_buf_mut(buf);
p[0] = 'H'; /* len unchanged; p[len] must stay '\0' */
```

### sx_buf_view_mut

```c
char *sx_buf_view_mut(sx_view_t v);
```

返回指向视图窗口首字节的可写指针。

- **参数：** `v` —— 指向某个缓冲区的视图。
- **返回：** `v.ref->ptr + v.offset`。
- **所有权：** 借用；写入会落到底层缓冲区上。
- **边界情况：** 该指针不携带长度信息 —— 请配合 `sx_len_view(v)` 使用并
  停留在窗口内。`v.ref == NULL` 或越界窗口属于契约之外；底层缓冲区重新
  分配后该指针失效。

```c
sx_view_t word = sx_view_range(sx_view(buf), 6, 5);
char *w = sx_buf_view_mut(word); /* == sx_buf_mut(buf) + 6 */
w[0] = 'W';
```

### sx_view

```c
sx_view_t sx_view(const sx_t *h);
```

返回整个缓冲区的视图。

- **参数：** `h` —— 缓冲区。
- **返回：** `{ref: h->ref, offset: 0, len: sx_len(h)}`。
- **所有权：** 无；视图为 `h` 的别名，从不单独释放。
- **边界情况：** 零拷贝且只读；`h == NULL` 属于契约之外。

## 容量

### sx_reserve

```c
sx_t *sx_reserve(sx_t *self, size_t min_cap);
```

确保能容纳至少 `min_cap` 个内容字节外加结尾符。

- **参数：** `self` —— 缓冲区；`min_cap` —— 必须能在后续操作中直接容纳的
  内容字节数。
- **返回：** 成功时（包括无需操作的请求）返回 `self`；分配失败时返回
  `NULL`。
- **所有权：** 返回的 `self` 仍归调用方所有；内容与长度保持不变。
- **边界情况：** 成功后 `cap >= min_cap + 1`。当前容量已经满足的请求不会
  缩小或改变容量。容量只增不减；`sx_truncate` 和 `sx_clear` 从不回收。
  底层数组可能搬移，之后必须重新获取所有 `sx_buf_mut` 指针。

```c
buf = sx_reserve(buf, 4096); /* cap >= 4097 */
```

## 追加

### sx_append

```c
sx_t *sx_append(sx_t *self, sx_t *owned);
```

追加另一个缓冲区的内容，并消耗该缓冲区。

- **参数：** `self` —— 目标；`owned` —— 内容被追加的源。
- **返回：** `self`；`owned == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 所有权转移：调用读取 `owned` 的内容后将其释放。此后不得
  再使用或释放 `owned`。
- **边界情况：** 字节精确：按内容长度处理，内嵌的 `'\0'` 字节也会被追加。
  `owned == NULL` 时不做任何修改。`self` 可能重新分配。把 `self` 作为
  `owned` 传入属于契约之外（它会在使用中被释放）。

### sx_append_cstr

```c
sx_t *sx_append_cstr(sx_t *self, const char *s);
```

追加一个 C 字符串。

- **参数：** `self` —— 目标；`s` —— 以 NUL 结尾的源。
- **返回：** `self`；`s == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义 —— 内嵌的 `'\0'` 会截断追加的字节。追加
  `""` 是空操作，返回 `self`。

### sx_append_view

```c
sx_t *sx_append_view(sx_t *self, sx_view_t v);
```

追加视图中的字节。

- **参数：** `self` —— 目标；`v` —— 源窗口；从 `v.ref->ptr + v.offset`
  读取 `v.len` 个字节。
- **返回：** `self`；分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全。`v` 可以是 `self` 的别名，追加导致
  缓冲区增长时也一样：用 `sx_append_view(h, sx_view(h))` 原地翻倍是支持的。
  `v.ref == NULL` 或越界窗口属于契约之外。

```c
buf = sx_append_view(buf, sx_view(buf)); /* doubles in place */
```

## 写入

### sx_write

```c
sx_t *sx_write(sx_t *self, size_t idx, sx_t *owned);
```

从 `idx` 开始覆盖写入字节，必要时扩展长度，并消耗源缓冲区。

- **参数：** `self` —— 目标；`idx` —— 第一个被覆盖的字节；`owned` ——
  内容被写入的源。
- **返回：** `self`；`owned == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 所有权转移：调用读取 `owned` 的内容后将其释放。此后不得
  再使用或释放它。
- **边界情况：** 调用后 `len' = max(len, idx + owned.len)` —— 写入从不
  缩短缓冲区。`idx == len` 等价于追加；`idx < len` 原地覆盖；`idx > len`
  会扩展长度，且 `[len, idx)` 区间的字节是**未指定的**。把 `self` 作为
  `owned` 传入，或 `idx + n` 使 `size_t` 溢出，都属于契约之外。

```c
buf = sx_new_from_cstr("abc");
buf = sx_write_cstr(buf, 6, "XY"); /* len 8; bytes 3..5 unspecified */
```

### sx_write_cstr

```c
sx_t *sx_write_cstr(sx_t *self, size_t idx, const char *s);
```

在 `idx` 处写入一个 C 字符串。

- **参数：** `self` —— 目标；`idx` —— 第一个写入位置；`s` —— 以 NUL
  结尾的源。
- **返回：** `self`；`s == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义（内嵌的 `'\0'` 会截断写入的字节）。
  `s == NULL` 时 `self` 保持不变。`idx` 与填充规则同 `sx_write`。

### sx_write_view

```c
sx_t *sx_write_view(sx_t *self, size_t idx, sx_view_t v);
```

在 `idx` 处写入视图中的字节。

- **参数：** `self` —— 目标；`idx` —— 第一个写入位置；`v` —— 源窗口。
- **返回：** `self`；分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全。`v` 可以是 `self` 的别名，写入需要
  增长缓冲区时也一样（源字节在预留空间的过程中保持有效）。`idx` 与填充
  规则同 `sx_write`；越界窗口属于契约之外。

## 插入

### sx_insert

```c
sx_t *sx_insert(sx_t *self, size_t idx, sx_t *owned);
```

在 `idx` 处插入另一个缓冲区的内容，尾部右移，并消耗该缓冲区。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `owned` —— 内容被插入的源。
- **返回：** `self`；`owned == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 所有权转移：调用读取 `owned` 的内容后将其释放。此后不得
  再使用或释放它。
- **边界情况：** 结果为 `[0, idx)` ++ 插入字节 ++ `[idx, len)`，
  且 `len' = len + n`。`idx == len` 等价于追加。`idx > len` 属于契约之外
  （尾部移动会下溢）；把 `self` 作为 `owned` 传入属于契约之外。

```c
buf = sx_insert_cstr(buf, 3, "-"); /* shift the tail right */
```

### sx_insert_cstr

```c
sx_t *sx_insert_cstr(sx_t *self, size_t idx, const char *s);
```

在 `idx` 处插入一个 C 字符串。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `s` —— 以 NUL 结尾的源。
- **返回：** `self`；`s == NULL` 或分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义；`s == NULL` 时 `self` 保持不变。`idx > len`
  属于契约之外。

### sx_insert_view

```c
sx_t *sx_insert_view(sx_t *self, size_t idx, sx_view_t v);
```

在 `idx` 处插入视图中的字节。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `v` —— 源窗口。
- **返回：** `self`；分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全。`v` 可以是 `self` 的别名，插入
  导致缓冲区增长时也一样，但前提是视图窗口的起始偏移不大于 `idx`（测试
  覆盖的即插入点处的切片）；起始偏移大于 `idx` 的自别名窗口会被尾部右移
  覆盖，属于**契约之外** —— 请先用 `sx_new_from_view` 复制该切片。
  `idx > len` 和越界视图属于契约之外。

## 截断、删除与清空

### sx_truncate

```c
sx_t *sx_truncate(sx_t *self, size_t new_len);
```

把内容长度设为 `new_len`。

- **参数：** `self` —— 缓冲区；`new_len` —— 新的内容字节长度。
- **返回：** 成功时返回 `self`；增长过程中分配失败时返回 `NULL`。
- **所有权：** 返回的 `self` 仍归调用方所有。
- **边界情况：** 更短的 `new_len` 保留前缀并丢弃尾部；相同的 `new_len`
  是空操作。更长的 `new_len` 会扩展缓冲区：`len` 变为 `new_len`，
  `ptr[new_len] == '\0'`，而 `[old_len, new_len)` 区间的字节是**未指定的**
  —— 在覆盖它们之前不要读取。收缩不会回收容量。

### sx_erase

```c
sx_t *sx_erase(sx_t *self, size_t idx, size_t n);
```

删除从 `idx` 开始的 `n` 个字节，尾部左移。

- **参数：** `self` —— 缓冲区；`idx` —— 第一个被删除的字节；`n` —— 要
  删除的字节数。
- **返回：** `self`。
- **所有权：** 返回的 `self` 仍归调用方所有；`self` 本身必须有效。
- **边界情况：** `idx + n == len` 删除后缀；`idx + n > len` 是空操作，原样
  返回 `self`；`n == 0` 是空操作。调用不进行分配，因此不会失败。
  `idx + n` 使 `size_t` 溢出属于契约之外。

```c
buf = sx_erase(buf, 2, 100); /* idx+n > len: no-op, returns self */
```

### sx_clear

```c
sx_t *sx_clear(sx_t *self);
```

清空缓冲区，保留容量。

- **参数：** `self` —— 缓冲区。
- **返回：** `self`。
- **所有权：** 返回的 `self` 仍归调用方所有。
- **边界情况：** 设置 `len = 0` 和 `ptr[0] = '\0'`；分配及其容量保留，
  缓冲区可以立即复用。

```c
buf = sx_clear(buf); /* len 0, capacity kept */
```

## 子串

### sx_substr

```c
sx_t *sx_substr(const sx_t *h, size_t idx, size_t n);
```

把字节区间 `[idx, idx + n)` 拷贝到一个新的独立缓冲区。

- **参数：** `h` —— 源缓冲区；`idx` —— 起始偏移；`n` —— 字节数，前置
  条件为 `idx + n <= len(h)`。
- **返回：** 新的调用方所有的缓冲区；分配失败时返回 `NULL`。（`h` 为
  NULL 或区间越界属于契约之外，而不是错误返回值。）
- **所有权：** 调用方所有；用 `sx_free` 释放。
- **边界情况：** `n == 0` 得到空缓冲区。字节精确且二进制安全，包括内嵌的
  `'\0'` 字节。与视图移动不同，这里是拷贝：任一侧的修改都不会影响另一侧。

```c
sx_t *word = sx_substr(buf, 6, 5); /* a copy, not a view */
sx_free(word);
```

## 视图移动

这四个函数都在已有缓冲区上重新推导 `(offset, len)` 窗口。它们都不拷贝
字节、不修改缓冲区，只返回新的视图值。输入必须是合法视图（`ref != NULL`
且窗口位于缓冲区内）；非法输入属于契约之外。

### sx_view_advance

```c
sx_view_t sx_view_advance(sx_view_t v, size_t n);
```

把窗口起点向右移动 `n`。

- **参数：** `v` —— 输入视图；`n` —— 前进的字节数。
- **返回：** 同一缓冲区上调整后的视图。
- **所有权：** 无。
- **边界情况：** 设 `H = len(ref)`：
  - 若 `offset + len + n < H`，保持终点不动，`len` 精确减少 `n`；
  - 否则若 `offset + n < H`，起点变为 `offset + n`，终点夹紧到缓冲区
    末尾（`len = H - offset`）；
  - 否则结果为空视图 `(H, 0)`。
  即使 `n` 很大，前进也永远不会越过缓冲区末尾；而把子视图推进到其自身
  终点之外时，会按第二种情况扩展到缓冲区末尾，这是契约规定的行为。

```c
sx_view_t rest = sx_view_advance(sx_view(buf), 2); /* start +2 */
```

### sx_view_retreat

```c
sx_view_t sx_view_retreat(sx_view_t v, size_t n);
```

把窗口起点向左移动 `n`，保持终点不动。

- **参数：** `v` —— 输入视图；`n` —— 后退的字节数。
- **返回：** 同一缓冲区上调整后的视图。
- **所有权：** 无。
- **边界情况：** `len` 增加的量等于偏移减少的量；移动在偏移 0 处饱和
  （`len` 变为原来的 `offset + len`），因此窗口永远不会从缓冲区之前开始。

### sx_view_shift

```c
sx_view_t sx_view_shift(sx_view_t v, ptrdiff_t off);
```

按有符号偏移移动窗口，保持长度不变。

- **参数：** `v` —— 输入视图；`off` —— 有符号字节偏移。
- **返回：** 同一缓冲区上调整后的视图。
- **所有权：** 无。
- **边界情况：** 与 advance/retreat 不同，`len` 保持不变。正的 `off` 若会
  把终点推过缓冲区，则夹紧为 `offset = H - len`（窗口停在缓冲区末尾）；
  负的 `off` 使起点饱和在 0。空缓冲区上的结果是 `(0, 0)`。

### sx_view_range

```c
sx_view_t sx_view_range(sx_view_t v, size_t off, size_t len);
```

按绝对坐标重新锚定窗口，忽略输入视图的偏移和长度。

- **参数：** `v` —— 只提供底层缓冲区；`off` —— 绝对起始偏移，前置条件为
  `off <= H`；`len` —— 请求的长度。
- **返回：** 视图 `(off, min(len, H - off))`。
- **所有权：** 无。
- **边界情况：** 长度会夹紧到缓冲区末尾；`off == H` 得到 `(H, 0)`；
  `off > H` 属于契约之外（`H - off` 下溢）。

```c
sx_view_t slice = sx_view_range(sx_view(buf), 2, 3); /* absolute (2, 3) */
```

## 视图裁剪

裁剪家族从视图两端移除 ASCII 空白字符。空白集合恰为
`{space, '\t', '\v', '\f', '\r', '\n'}`。不会修改任何缓冲区字节，也只
检查窗口内部的字节。

### sx_view_trim

```c
sx_view_t sx_view_trim(sx_view_t v);
```

裁剪视图的两端。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉首尾空白后的视图。
- **所有权：** 无。
- **边界情况：** 等价于 `sx_view_trim_back(sx_view_trim_front(v))`。两端都
  没有空白的视图原样返回。内容全为空白的视图会变成窗口末尾处的空视图
  （偏移为 `offset + len`，长度为 0）—— 对整个缓冲区的视图而言就是
  `(H, 0)`。

```c
sx_view_t field = sx_view_trim(sx_view(buf)); /* strip ASCII whitespace */
```

### sx_view_trim_front

```c
sx_view_t sx_view_trim_front(sx_view_t v);
```

去掉前导空白。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉前导空白后的视图。
- **所有权：** 无。
- **边界情况：** 偏移右移，`len` 相应减少，因此窗口终点保持不动。扫描受
  视图自身长度的限制，即使窗口之后缓冲区里仍有空白，也不会越过窗口末尾。

### sx_view_trim_back

```c
sx_view_t sx_view_trim_back(sx_view_t v);
```

去掉尾部空白。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉尾部空白后的视图。
- **所有权：** 无。
- **边界情况：** 只检查窗口的最后一个字节（`offset + len - 1`），`len`
  向左收缩；偏移保持不变，因此全为空白的窗口会变成 `(offset, 0)`。窗口
  之外的字节永远不会被检查。

## 完整示例

下面这个程序构造一个字符串，裁剪它的一个视图，把裁剪后的窗口拷贝到新
缓冲区，原地修改该缓冲区，最后释放两个缓冲区。按原样与库源码一起即可
编译，也是普通应用程序的典型写法：

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *h = sx_new_from_cstr("  hello, world  ");

  sx_view_t trimmed = sx_view_trim(sx_view(h)); /* (2, 12) */
  sx_t *clean = sx_new_from_view(trimmed);      /* independent copy */

  clean = sx_append_cstr(clean, "!");
  clean = sx_insert_cstr(clean, 0, "> ");

  printf("%s (%d bytes)\n", sx_buf_mut(clean), sx_len(clean));

  sx_free(clean);
  sx_free(h);
  return 0;
}
```

预期输出：

```text
> hello, world! (15 bytes)
```

---

[← 上一页](guide.md) · [文档首页](README.md) · [下一页 →](api-search.md)
