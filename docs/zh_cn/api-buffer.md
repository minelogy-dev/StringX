[English](../en_us/api-buffer.md) · 中文

[文档首页](README.md) › API 参考：缓冲区与视图

# 缓冲区与视图 API 参考

`include/sx_core.h` 的核心缓冲区与视图 API：创建、状态、修改、子串、
视图移动与裁剪。查找、比较与分割见[查找、比较与分割](api-search.md)。
行为描述来自公开头文件、实现代码与 `tests/*.c` 的边界注释。

## 约定

**头文件与分层。** 包含唯一的公开头文件：

```c
#include <sx.h>
```

`sx.h` 包含 `sx_core.h`（缓冲区、视图、查找、比较），并声明 `sx_split`
和 `sx_reg_*`；链接用 `-lstringx`。本页函数都声明在 `sx_core.h` 中。
`sx_raw.h` 是内部调试接口，不属于公开接口。

**每个缓冲区一次分配。** `sx_t` 拥有一段 NUL 结尾的字节数组：
`ptr[0 .. len-1]` 是内容，`ptr[len]` 恒为 `'\0'`，`cap` 是分配大小，
`cap >= len + 1`。`len` 统计内容字节，内嵌 `'\0'` 是数据而非结尾符，
天然二进制安全。

**所有权。**

| 值 | 所有者 | 释放方式 |
| --- | --- | --- |
| `sx_new*`、`sx_dup`、`sx_substr` 返回的缓冲区 | 调用方 | `sx_free` |
| `sx_append`、`sx_write`、`sx_insert` 的 `owned` 参数 | 转移给库 | 无需操作：调用会释放它 |
| 修改类函数返回的缓冲区 | 与 `self` 是同一个句柄 | 对最初的句柄调用一次 `sx_free` |
| `sx_view_t` | 无：它仅别名一段缓冲区 | 无需释放；视图从不单独释放 |
| `sx_find_all*` 或 `sx_split` 返回的数组 | 调用方 | `free`（见 [api-search.md](api-search.md)） |

每次修改都返回 `self`，调用可以链式书写，句柄保持稳定。增长可能搬移底层
数组：`sx_buf_mut`/`sx_buf_view_mut` 的指针在任何可能重新分配的调用后
失效，必须重新获取。修改失败时 `owned` 参数同样会被消耗：调用返回
`NULL`、释放该参数，`self` 保持不变。

**NULL 与契约之外。** 文档化的 NULL 处理：`sx_new_from_cstr(NULL)` 和
`sx_dup(NULL)` 返回 `NULL`；`sx_append`、`sx_write`、`sx_insert` 的
`owned` 形式及所有 `_cstr` 形式在第二个参数为 `NULL` 时返回 `NULL` 且
`self` 不变。其余函数解引用缓冲区或窗口：`sx_free`、`sx_len`、`sx_cap`、
`sx_buf_mut`、`sx_view`、视图移动家族和修改类函数不接受 NULL 句柄，
`sx_new_from_view`、`sx_buf_view_mut`、`sx_view_*` 不接受 `ref == NULL`
的视图（`sx_len_view` 只读存储的长度）。**契约之外**（行为未定义，而非
错误返回值）：NULL 句柄或 `v.ref`、已释放句柄、越界视图、重复释放、
消耗后继续使用、把 `self` 作为 `owned`，以及违反条目条件的下标。

**哨兵值。**

| 情形 | 取值 |
| --- | --- |
| 分配失败 | `NULL` |
| 拒绝 NULL 操作数（有文档记录处） | `NULL` |
| “没有视图” | `.ref == NULL` |
| 未找到 / 无匹配（查找页） | `(size_t)-1` |
| 无匹配 / 拒绝分割（查找页） | `NULL` |

**大小是 `int`。** `sx_len`、`sx_len_view`、`sx_cap` 返回 `int`；视图字段
和查找偏移是 `size_t`。超过 `INT_MAX` 字节不在测试契约内；混用时转换为
`(size_t)`，例如
`fwrite(sx_buf_mut(h), 1, (size_t)sx_len(h), stdout)`。

**视图是快照。** `sx_view_t {ref, offset, len}` 是 `ref` 所指缓冲区内的
窗口 `[offset, offset + len)`；不拷贝、不修改，合法窗口满足
`offset + len <= len(ref)`。之后任何重新分配或改变长度的修改都会使其
失效：重新推导视图；有视图（如 `sx_split` 数组）使用时不要修改底层
缓冲区。`ref == NULL` 表示“没有视图”，不是可用缓冲区。

**容量增长。** `sx_new` 初始容量 32 字节；增长为几何式（先翻倍，达到
64 KiB 后改为 `+0x10000`），`-DSX_FORCE_EXACT_RESERVE` 调试构建则把
每次分配精确设为 `need + 1`。容量从不收缩（`sx_truncate`、`sx_clear`
保留容量），也没有函数承诺所述保证之外的空闲容量。

## 创建与释放

### sx_new

```c
sx_t *sx_new(void);
```

创建容量为 32 字节的空缓冲区。

- **参数：** 无。
- **返回：** 新缓冲区（`len == 0`、`cap == 32`、`ptr[0] == '\0'`）；
  分配失败返回 `NULL`。
- **所有权：** 调用方所有；`sx_free` 释放。
- **边界情况：** 只有 `cap >= 1` 有保证；初始 32 字节属于实现细节。

### sx_new_from_cstr

```c
sx_t *sx_new_from_cstr(const char *s);
```

创建存放 C 字符串 `s` 副本的缓冲区。

- **参数：** `s` —— NUL 结尾字符串，按 `strlen` 读取。
- **返回：** 新缓冲区；`s == NULL` 或分配失败返回 `NULL`。
- **所有权：** 调用方所有；`sx_free` 释放。
- **边界情况：** `strlen` 语义，内嵌 `'\0'` 截断副本；二进制内容用
  `sx_new_from_view`。

### sx_new_from_view

```c
sx_t *sx_new_from_view(sx_view_t v);
```

把视图中的字节拷贝成新的独立缓冲区。

- **参数：** `v` —— 源窗口；从 `v.ref->ptr + v.offset` 读取 `v.len`
  个字节。
- **返回：** 新缓冲区；分配失败返回 `NULL`。
- **所有权：** 调用方所有；`sx_free` 释放。
- **边界情况：** 字节精确且二进制安全（内嵌 `'\0'` 会拷贝）；两个缓冲区
  无别名，修改任一方不影响另一方。`v.ref == NULL` 或越界窗口属于契约
  之外。

### sx_new_with_cap

```c
sx_t *sx_new_with_cap(size_t n);
```

创建恰好分配 `n` 个字节的空缓冲区。

- **参数：** `n` —— 分配的总字节数。
- **返回：** `n >= 1` 时 `cap == n` 的新缓冲区；分配失败返回 `NULL`。
- **所有权：** 调用方所有；`sx_free` 释放。
- **边界情况：** 一个字节留给结尾符，最多容纳 `n - 1` 个内容字节；
  `n == 0` 仍分配一个字节（`cap == 1`），句柄始终有效。

```c
sx_t *buf = sx_new_with_cap(64); /* room for 63 content bytes */
buf = sx_append_cstr(buf, "hello"); /* no reallocation */
sx_free(buf);
```

### sx_dup

```c
sx_t *sx_dup(const sx_t *src);
```

创建完全独立的深拷贝。

- **参数：** `src` —— 源缓冲区。
- **返回：** 内容相同的新缓冲区；`src == NULL` 或分配失败返回 `NULL`。
- **所有权：** 调用方所有；副本与源分别用 `sx_free` 释放。
- **边界情况：** 二进制安全；v0.5 中副本容量是 `len + 1`，而非源的容量。

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

释放缓冲区：内容数组和句柄。

- **参数：** `h` —— 调用方持有的有效句柄。
- **返回：** 无。
- **所有权：** 消耗 `h`；指向它的视图和 `sx_buf_mut` 指针全部失效。
- **边界情况：** `h == NULL` 属于契约之外 —— 函数会解引用句柄（正则层
  `sx_reg_free(NULL)` 的空操作不适用于此）。重复释放、释放后使用也属于
  契约之外。

## 访问器

### sx_len

```c
int sx_len(sx_t *h);
```

返回内容字节数。

- **参数：** `h` —— 缓冲区。
- **返回：** `len`，内嵌 `'\0'` 也计入；结尾符从不计入；不修改缓冲区。
- **边界情况：** `h == NULL` 属于契约之外；返回值是 `int`（见大小说明）。

### sx_len_view

```c
int sx_len_view(sx_view_t v);
```

返回视图存储的长度。

- **参数：** `v` —— 要读取长度的视图。
- **返回：** `(int)v.len`；不查看底层缓冲区，也不校验窗口边界。
- **边界情况：** `v.len` 超过 `INT_MAX` 不在测试契约内；“没有视图”哨兵值
  （`ref == NULL`）的 `len` 为 0。

### sx_cap

```c
int sx_cap(sx_t *h);
```

返回内容数组的总分配字节数。

- **参数：** `h` —— 缓冲区。
- **返回：** `cap`，始终至少为 `len + 1`（单 NUL 结尾不变式）；不修改
  缓冲区。
- **边界情况：** `h == NULL` 属于契约之外；`len + 1` 之外的空闲容量取决于
  增长策略，不构成契约。

### sx_buf_mut

```c
char *sx_buf_mut(sx_t *h);
```

返回内容数组的可写指针。

- **参数：** `h` —— 缓冲区。
- **返回：** `ptr`；任何时刻 `ptr[len] == '\0'`。
- **所有权：** 借用；可在 `[0, len)` 内改写字节而不改变 `len`。
- **边界情况：** 无长度变更时不得在 `len` 及之后写入；也不得跨过可能
  重新分配的调用保存指针（`sx_append*`、`sx_write*`、`sx_insert*`、
  `sx_reserve`、会增长的 `sx_truncate`）。`h == NULL` 属于契约之外。

```c
char *p = sx_buf_mut(buf);
p[0] = 'H'; /* len unchanged; p[len] must stay '\0' */
```

### sx_buf_view_mut

```c
char *sx_buf_view_mut(sx_view_t v);
```

返回视图窗口首字节的可写指针。

- **参数：** `v` —— 指向缓冲区的视图。
- **返回：** `v.ref->ptr + v.offset`。
- **所有权：** 借用；写入落到底层缓冲区。
- **边界情况：** 指针不携带长度 —— 配合 `sx_len_view(v)` 停留在窗口内。
  `v.ref == NULL` 或越界窗口属于契约之外；底层缓冲区重新分配后指针失效。

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
- **返回：** `{ref: h->ref, offset: 0, len: sx_len(h)}`；视图别名 `h`，
  从不单独释放。
- **边界情况：** 零拷贝且只读；`h == NULL` 属于契约之外。

## 容量

### sx_reserve

```c
sx_t *sx_reserve(sx_t *self, size_t min_cap);
```

确保容纳至少 `min_cap` 个内容字节外加结尾符。

- **参数：** `self` —— 缓冲区；`min_cap` —— 后续操作中必须直接容纳的内容
  字节数。
- **返回：** 成功时（含无需操作的请求）返回 `self`；分配失败返回 `NULL`。
- **所有权：** `self` 仍归调用方所有；内容与长度不变。
- **边界情况：** 成功后 `cap >= min_cap + 1`；当前容量已满足的请求不会
  缩小容量。容量只增不减（`sx_truncate`、`sx_clear` 从不回收），且底层
  数组可能搬移，之后必须重新获取 `sx_buf_mut` 指针。

```c
buf = sx_reserve(buf, 4096); /* cap >= 4097 */
```

## 追加

### sx_append

```c
sx_t *sx_append(sx_t *self, sx_t *owned);
```

追加另一缓冲区的内容并消耗该缓冲区。

- **参数：** `self` —— 目标；`owned` —— 内容被追加的源。
- **返回：** `self`；`owned == NULL` 或分配失败返回 `NULL`。
- **所有权：** 所有权转移 —— 复制 `owned` 的内容后将其释放；此后不得使用
  或释放 `owned`。
- **边界情况：** 按内容长度字节精确处理，内嵌 `'\0'` 也会追加；
  `owned == NULL` 时不做修改；`self` 可能重新分配；把 `self` 作为
  `owned` 传入属于契约之外（会在使用中被释放）。

### sx_append_cstr

```c
sx_t *sx_append_cstr(sx_t *self, const char *s);
```

追加 C 字符串。

- **参数：** `self` —— 目标；`s` —— NUL 结尾的源。
- **返回：** `self`；`s == NULL` 或分配失败返回 `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义，内嵌 `'\0'` 截断追加字节；追加 `""` 是
  空操作，返回 `self`。

### sx_append_view

```c
sx_t *sx_append_view(sx_t *self, sx_view_t v);
```

追加视图中的字节。

- **参数：** `self` —— 目标；`v` —— 源窗口；从
  `v.ref->ptr + v.offset` 读取 `v.len` 个字节。
- **返回：** `self`；分配失败返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全；`v` 可为 `self` 的别名，增长时
  也一样 —— `sx_append_view(h, sx_view(h))` 原地翻倍受支持。
  `v.ref == NULL` 或越界窗口属于契约之外。

```c
buf = sx_append_view(buf, sx_view(buf)); /* doubles in place */
```

## 写入

### sx_write

```c
sx_t *sx_write(sx_t *self, size_t idx, sx_t *owned);
```

从 `idx` 起覆盖写入，必要时扩展长度，并消耗源缓冲区。

- **参数：** `self` —— 目标；`idx` —— 首个被覆盖字节；`owned` —— 内容
  被写入的源。
- **返回：** `self`；`owned == NULL`、写入范围无法预留或分配失败时返回
  `NULL`。
- **所有权：** 所有权转移 —— 复制内容后释放 `owned`；此后不得使用或
  释放它。即使写入失败，`owned` 也会被释放。
- **边界情况：** `len' = max(len, idx + owned.len)`，写入从不缩短缓冲区。
  `idx == len` 等价追加；`idx < len` 原地覆盖；`idx > len` 会扩展，且
  `[len, idx)` 为**未指定**。若 `idx + n` 使 `size_t` 溢出，或结果末端
  无法预留（总内容长度为 `SIZE_MAX` 时放不下结尾符），调用在拷贝前返回
  `NULL`，`self` 保持不变。把 `self` 作为 `owned` 传入属于契约之外。

```c
buf = sx_new_from_cstr("abc");
buf = sx_write_cstr(buf, 6, "XY"); /* len 8; bytes 3..5 unspecified */
```

### sx_write_cstr

```c
sx_t *sx_write_cstr(sx_t *self, size_t idx, const char *s);
```

在 `idx` 处写入 C 字符串。

- **参数：** `self` —— 目标；`idx` —— 首个写入位置；`s` —— NUL 结尾的
  源。
- **返回：** `self`；`s == NULL`、写入范围无法预留或分配失败时返回
  `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义（内嵌 `'\0'` 截断写入字节）；`s == NULL`
  时 `self` 不变。范围校验、`idx` 与填充规则同 `sx_write`。

### sx_write_view

```c
sx_t *sx_write_view(sx_t *self, size_t idx, sx_view_t v);
```

在 `idx` 处写入视图中的字节。

- **参数：** `self` —— 目标；`idx` —— 首个写入位置；`v` —— 源窗口。
- **返回：** `self`；写入范围无法预留或分配失败时返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全；`v` 可为 `self` 的别名，写入需要
  增长时也一样（源字节在预留空间后仍有效）。溢出检查先于保留别名的预
  预留执行，因此被拒绝的范围不会改变目标容量。`idx` 与填充规则同
  `sx_write`；越界窗口属于契约之外。

## 插入

### sx_insert

```c
sx_t *sx_insert(sx_t *self, size_t idx, sx_t *owned);
```

在 `idx` 处插入另一缓冲区的内容，尾部右移，并消耗该缓冲区。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `owned` —— 内容被插入的源。
- **返回：** `self`；`owned == NULL` 或分配失败返回 `NULL`。
- **所有权：** 所有权转移 —— 复制内容后释放 `owned`；此后不得使用或
  释放它。
- **边界情况：** 结果为 `[0, idx)` ++ 插入字节 ++ `[idx, len)`，
  `len' = len + n`；`idx == len` 等价追加。`idx > len` 属于契约之外
  （尾部移动会下溢）；把 `self` 作为 `owned` 传入也属于契约之外。

```c
buf = sx_insert_cstr(buf, 3, "-"); /* shift the tail right */
```

### sx_insert_cstr

```c
sx_t *sx_insert_cstr(sx_t *self, size_t idx, const char *s);
```

在 `idx` 处插入 C 字符串。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `s` —— NUL 结尾的源。
- **返回：** `self`；`s == NULL` 或分配失败返回 `NULL`。
- **所有权：** 无；只读取 `s`。
- **边界情况：** `strlen` 语义；`s == NULL` 时 `self` 不变。`idx > len`
  属于契约之外。

### sx_insert_view

```c
sx_t *sx_insert_view(sx_t *self, size_t idx, sx_view_t v);
```

在 `idx` 处插入视图中的字节。

- **参数：** `self` —— 目标；`idx` —— 插入点，要求 `0 <= idx <= len`；
  `v` —— 源窗口。
- **返回：** `self`；分配失败返回 `NULL`。
- **所有权：** 无；只读取 `v`。
- **边界情况：** 字节精确且二进制安全；`v` 可为 `self` 的别名，插入增长
  时也一样，但窗口起始偏移不得大于 `idx`（测试覆盖插入点处的切片）。
  起始偏移大于 `idx` 的自别名窗口会被尾部右移覆盖，属于**契约之外** ——
  先用 `sx_new_from_view` 复制该切片。`idx > len` 和越界视图属于契约
  之外。

## 截断、删除与清空

### sx_truncate

```c
sx_t *sx_truncate(sx_t *self, size_t new_len);
```

把内容长度设为 `new_len`。

- **参数：** `self` —— 缓冲区；`new_len` —— 新的内容字节长度。
- **返回：** `self`；增长中分配失败返回 `NULL`。
- **边界情况：** 更短的 `new_len` 保留前缀、丢弃尾部；相同值无操作。
  更长的 `new_len` 会扩展：`len` 变为 `new_len`，`ptr[new_len] == '\0'`，
  且 `[old_len, new_len)` 为**未指定** —— 覆盖前不要读取。收缩不回收
  容量。

### sx_erase

```c
sx_t *sx_erase(sx_t *self, size_t idx, size_t n);
```

删除从 `idx` 开始的 `n` 个字节，尾部左移。

- **参数：** `self` —— 缓冲区；`idx` —— 首个被删除字节；`n` —— 删除
  字节数。
- **返回：** `self`；`self` 本身必须有效。
- **边界情况：** `idx + n == len` 删除后缀；`idx + n > len` 是空操作，原样
  返回 `self`；`n == 0` 是空操作。调用不分配，因此不会失败。
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
- **返回：** `self`（仍归调用方所有）。
- **边界情况：** 设置 `len = 0`、`ptr[0] = '\0'`；分配与容量保留，缓冲区
  可立即复用。

```c
buf = sx_clear(buf); /* len 0, capacity kept */
```

## 子串

### sx_substr

```c
sx_t *sx_substr(const sx_t *h, size_t idx, size_t n);
```

把 `[idx, idx + n)` 拷贝为新的独立缓冲区。

- **参数：** `h` —— 源缓冲区；`idx` —— 起始偏移；`n` —— 字节数，前置
  条件 `idx + n <= len(h)`。
- **返回：** 新缓冲区；分配失败返回 `NULL`。（`h` 为 NULL 或区间越界
  属于契约之外，而非错误返回值。）
- **所有权：** 调用方所有；`sx_free` 释放。
- **边界情况：** `n == 0` 得到空缓冲区；字节精确且二进制安全（含内嵌
  `'\0'`）。与视图移动不同，这里是拷贝：任一侧修改都不影响另一侧。

```c
sx_t *word = sx_substr(buf, 6, 5); /* a copy, not a view */
sx_free(word);
```

## 视图移动

四个函数都在已有缓冲区上重新推导 `(offset, len)` 窗口；不拷贝、不修改、
不转移所有权，只返回新视图值。输入必须是合法视图（`ref != NULL` 且窗口
在缓冲区内），否则属于契约之外。

### sx_view_advance

```c
sx_view_t sx_view_advance(sx_view_t v, size_t n);
```

把窗口起点右移 `n`。

- **参数：** `v` —— 输入视图；`n` —— 前进字节数。
- **返回：** 同一缓冲区上调整后的视图。
- **边界情况：** 设 `H = len(ref)`：
  - 若 `offset + len + n < H`，终点不动，`len` 减少 `n`；
  - 否则若 `offset + n < H`，起点变为 `offset + n`，终点夹紧到缓冲区
    末尾（`len = H - offset`）；
  - 否则结果为空视图 `(H, 0)`。
  `n` 再大也不会越过缓冲区末尾；把子视图推进到自身终点之外时，会按
  第二种情况扩展到缓冲区末尾。

```c
sx_view_t rest = sx_view_advance(sx_view(buf), 2); /* start +2 */
```

### sx_view_retreat

```c
sx_view_t sx_view_retreat(sx_view_t v, size_t n);
```

把窗口起点左移 `n`，终点不动。

- **参数：** `v` —— 输入视图；`n` —— 后退字节数。
- **返回：** 同一缓冲区上调整后的视图。
- **边界情况：** `len` 增加量等于偏移减少量；移动在偏移 0 处饱和
  （`len` 变为原 `offset + len`），窗口不会从缓冲区之前开始。

### sx_view_shift

```c
sx_view_t sx_view_shift(sx_view_t v, ptrdiff_t off);
```

按有符号偏移移动窗口，保持长度。

- **参数：** `v` —— 输入视图；`off` —— 有符号字节偏移。
- **返回：** 同一缓冲区上调整后的视图。
- **边界情况：** `len` 不变（不同于 advance/retreat）。正 `off` 若把终点
  推过缓冲区，则夹紧为 `offset = H - len`（停在缓冲区末尾）；负 `off`
  使起点饱和在 0。空缓冲区上结果为 `(0, 0)`。

### sx_view_range

```c
sx_view_t sx_view_range(sx_view_t v, size_t off, size_t len);
```

按绝对坐标重新锚定窗口，忽略输入视图的偏移和长度。

- **参数：** `v` —— 只提供底层缓冲区；`off` —— 绝对起始偏移，前置条件
  `off <= H`；`len` —— 请求长度。
- **返回：** 视图 `(off, min(len, H - off))`。
- **边界情况：** 长度夹紧到缓冲区末尾；`off == H` 得 `(H, 0)`；
  `off > H` 属于契约之外（`H - off` 下溢）。

```c
sx_view_t slice = sx_view_range(sx_view(buf), 2, 3); /* absolute (2, 3) */
```

## 视图裁剪

裁剪家族移除视图两端的 ASCII 空白。空白集合恰为
`{space, '\t', '\v', '\f', '\r', '\n'}`。不修改缓冲区、不涉及所有权，
只检查窗口内字节。

### sx_view_trim

```c
sx_view_t sx_view_trim(sx_view_t v);
```

裁剪视图两端。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉首尾空白的视图。
- **边界情况：** 等价于 `sx_view_trim_back(sx_view_trim_front(v))`。两端
  无空白的视图原样返回。全空白视图变为窗口末尾处的空视图（偏移
  `offset + len`，长度 0）—— 整缓冲区视图即 `(H, 0)`。

```c
sx_view_t field = sx_view_trim(sx_view(buf)); /* strip ASCII whitespace */
```

### sx_view_trim_front

```c
sx_view_t sx_view_trim_front(sx_view_t v);
```

去掉前导空白。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉前导空白的视图。
- **边界情况：** 偏移右移、`len` 相应减少，窗口终点不动。扫描受视图自身
  长度限制，即使窗口之后缓冲区仍有空白，也不会越过窗口末尾。

### sx_view_trim_back

```c
sx_view_t sx_view_trim_back(sx_view_t v);
```

去掉尾部空白。

- **参数：** `v` —— 输入视图。
- **返回：** 去掉尾部空白的视图。
- **边界情况：** 只检查窗口最后一个字节（`offset + len - 1`），`len` 向左
  收缩；偏移不变，全空白窗口变为 `(offset, 0)`。窗口之外的字节永不检查。

## 完整示例

下面程序构造字符串、裁剪其视图、把裁剪窗口拷贝为新缓冲区、原地修改并
释放两个缓冲区：

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
