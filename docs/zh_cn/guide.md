[English](../en_us/guide.md) · 中文

[文档首页](README.md) › 用户指南

# 用户指南

面向能够构建并链接 String X 的 C 程序员（见[快速上手](getting-started.md)）。
函数签名与细节见 API 参考；本页讲心智模型、示例与契约之外的调用。

## 心智模型

### 缓冲区

每个字符串都是一个 `sx_t` 句柄，指向一块底层分配：

```text
sx_t* ──► _sx { ptr: [ h e l l o \0 . . . . ] , len: 5, cap: 32 }
```

- `ptr[0 .. len-1]` 是内容字节；`ptr[len]` 恒为 `'\0'`。
- `len` 是字节数，永远不是字符数，也不会在内嵌 NUL 处停下。
- `cap` 是分配的字节数，始终满足 `cap >= len + 1`。
- `sx_new()` 的初始容量为 32；对于 `n >= 1`，`sx_new_with_cap(n)` 的容量
  恰好是 `n`，因此最多容纳 `n - 1` 个内容字节（`n == 0` 时仍分配一个字节）。

句柄本身是稳定的：扩容可能移动底层数组，但绝不会更换 `sx_t`。请把句柄视为
不透明对象，不要按值复制该结构体。

### 视图

视图别名缓冲区的一段，不进行拷贝：

```text
sx_view_t { ref: the same _sx, offset: 1, len: 3 } ──► "ell"
```

- `ref == NULL` 表示"无视图"：匹配失败或"没有"的哨兵值。
- 创建或移动视图不分配内存，因此视图永远不需要释放。
- 视图是 `(offset, len)` 的快照：之后的修改不会调整它，长度变化会使它失效。

## 所有权

同一时刻只有一个地方拥有缓冲区；各调用的结果与释放方式如下：

| 调用 | 结果 | 释放方式 |
| --- | --- | --- |
| `sx_new`、`sx_new_from_cstr`、`sx_new_from_view`、`sx_new_with_cap`、`sx_dup` | 新缓冲区 | `sx_free` |
| `sx_substr` | 新缓冲区 | `sx_free` |
| `sx_reg_replace`、`sx_reg_replace_first` | 新缓冲区 | `sx_free` |
| `sx_append`、`sx_write`、`sx_insert`（`sx_t *owned` 形式） | `self`；实参被消费并释放 | 对 `self` 调用 `sx_free` |
| `sx_find_all*`、`sx_split`、`sx_reg_match_all`、`sx_reg_split` | malloc 分配的数组 | `free` |
| `sx_view`、`sx_view_*`、`sx_reg_match`、上述数组中的 token | 视图 | 无 |
| `sx_reg_compile` | 已编译模式 | `sx_reg_free` |

- 修改返回 `self`，可链式书写：`sx_append_cstr(sx_append_cstr(s, "a"), "b")`
  返回 `s`；`sx_free` 次数等于创建的缓冲区数，而不是修改次数。
- 消费型调用结束源的生命周期：`sx_append(s, part)` 之后不得再使用或释放
  `part`——内容已复制进 `s`，即使分配失败它也会被释放。

`sx_append_cstr`、`sx_append_view`、`sx_write_cstr`、`sx_write_view`、
`sx_insert_cstr`、`sx_insert_view` 接收字符串或视图，永不消费缓冲区。

## 二进制安全与 cstr 变体

凡是显式携带长度的接口都是二进制安全的：

| 输入形式 | 长度规则 | 内嵌 NUL |
| --- | --- | --- |
| `sx_*_cstr`、`sx_new_from_cstr`、`sx_split` 分隔符、正则模式 | `strlen` | 终止输入 |
| `sx_view_t` 形式、`sx_new_from_view`、自有 `sx_t *` 形式 | 显式 `len` | 普通字节 |
| 正则 subject（`sx_t *h`） | `h` 的字节长度 | 普通字节 |

`sx_append_cstr` 遇 NUL 即停；`sx_append_view`、`sx_append`、`sx_new_from_view`
复制每个内容字节。二进制缓冲区按长度打印，不要用 `printf("%s", ...)`：
`fwrite(sx_buf_mut(h), 1, (size_t)sx_len(h), stdout)`。

比较与查找同理：`_cstr` 形式用 `strlen`，自有与视图形式比较内容字节。正则按
字节长度匹配 subject，模式是 C 字符串，不能包含字面 NUL。

## 不变式与失效指针

- 每次公开调用之后都有 `ptr[len] == '\0'`、`cap >= len + 1`，且 `len` 统计
  内容字节。只有会改变长度的调用才会改动这些值。
- 任何增长修改都可能 `realloc` 底层数组；此前从 `sx_buf_mut` /
  `sx_buf_view_mut` 取得的 `char *` 全部失效，需重新获取；`sx_t *` 句柄仍有效。
- 通过 `sx_buf_mut` 写入必须保持在 `[0, len)` 内，除非随后有配套的改长度调用；
  `ptr[len]` 是结尾符，不是备用字节。
- 视图是快照：视图（或 `sx_split` / `sx_reg_*` 数组）仍在使用时不要修改缓冲区；
  任何长度变化之后都需重新派生视图。
- 访问器返回 `int`（`sx_len`、`sx_len_view`、`sx_cap`），而偏移是 `size_t`。
  内容超过 `INT_MAX` 字节的缓冲区不在受测契约之内。
- 容量只增：`sx_truncate`、`sx_erase`、`sx_clear` 都不释放容量。

## 错误与哨兵值

库中没有异常，也没有错误码。每个家族用一个哨兵值表示无结果：

| 情况 | 结果 |
| --- | --- |
| `sx_find*` / `sx_rfind*`：未找到 | `(size_t)-1` |
| `sx_find_all*`：未找到、空 needle 或分配失败 | `NULL` 数组 |
| `sx_split`：句柄或分隔符为 NULL、空分隔符、分配失败 | `NULL` 数组 |
| `sx_reg_match`：无匹配 | `ref == NULL` 的视图 |
| `sx_reg_match_all` / `sx_reg_split`：无匹配或分配失败 | `NULL` 数组 |
| `sx_reg_replace*`：参数为 NULL 或模板被拒绝 | `NULL` 缓冲区 |
| `sx_write*`：范围末端使 `size_t` 溢出或无法预留 | `NULL`；`self` 不变（`owned` 输入被消耗） |
| 分配失败 | `NULL` |

`NULL` 表示"没有结果"；对返回数组的调用而言，分配失败与"无匹配"无法区分。
文档化的 NULL 实参处理是有限的：

- `sx_new_from_cstr(NULL)` 与 `sx_dup(NULL)` 返回 `NULL`。
- append/write/insert 的字符串/owned 实参返回 `NULL`，`self` 不变。
- `sx_write*` 在 `idx + len` 使 `size_t` 溢出、或末端无法预留时返回
  `NULL`；`self` 保持原内容，`owned` 实参仍被释放。
- `sx_cmp*`、`sx_starts_with*` 与 `sx_ends_with*` 返回 `0`。
- `sx_find*` / `sx_rfind*` 返回 `(size_t)-1`；`sx_find_all*` 返回 `NULL`。
- `sx_split` 在句柄/分隔符为 NULL 或分隔符为空时返回 `NULL`。
- `sx_reg_compile(NULL)` 返回 `NULL`；`sx_reg_free(NULL)` 是空操作；
  match、replace 与 split 家族在参数为 NULL 时返回各自的未找到值。

其余接口都不接受 NULL 句柄：`sx_len`、`sx_cap`、`sx_buf_mut`、`sx_view`、
`sx_free`、`sx_substr`、`sx_reserve`、`sx_erase` 等都会解引用句柄，传 NULL
属于契约之外，可能崩溃。`sx_free(NULL)` 不是空操作，`ref == NULL` 的视图
也不能传给视图操作函数。

## 常用示例

每个示例都是完整 C23 程序，按[快速上手](getting-started.md)中 `hello.c` 的
方式构建，或使用：

```sh
cc recipe.c $(pkg-config --cflags --libs libstringx) -o recipe
```

### 由片段构建字符串

混合使用 C 字符串、视图与自有缓冲区；自有缓冲区会被消费。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *path = sx_new();
  path = sx_append_cstr(path, "/usr");
  path = sx_append_cstr(path, "/local");
  sx_t *part = sx_new_from_cstr("/bin");
  path = sx_append(path, part);            /* part is consumed */
  path = sx_insert_cstr(path, 0, "/opt:"); /* prefix */
  printf("%s (%d bytes)\n", sx_buf_mut(path), sx_len(path));
  sx_free(path);
  return 0;
}
```

```text
/opt:/usr/local/bin (19 bytes)
```

### 查找并提取子串

使用偏移前先检查 `(size_t)-1` 哨兵值；`sx_substr` 会拷贝（结果独立于源），
`sx_find_view_b` 只查从边界开始的区域。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *line = sx_new_from_cstr("key=value;flag=on");
  size_t eq = sx_find_cstr(line, "=");
  size_t semi = sx_find_cstr(line, ";");
  sx_t *nod = sx_new_from_cstr("=");
  size_t later = sx_find_view_b(line, sx_view(nod), 10); /* region [10, 17) */

  if (eq == (size_t)-1 || semi == (size_t)-1)
    return 1;
  sx_t *key = sx_substr(line, 0, eq);
  sx_t *value = sx_substr(line, eq + 1, semi - eq - 1);
  printf("first '='=%zu, next '=' from 10=%zu\n", eq, later);
  printf("key=%s value=%s\n", sx_buf_mut(key), sx_buf_mut(value));
  sx_free(key);
  sx_free(value);
  sx_free(nod);
  sx_free(line);
  return 0;
}
```

```text
first '='=3, next '=' from 10=14
key=key value=value
```

### 分割并遍历

`sx_split` 返回 malloc 分配的视图数组，以 `ref == NULL` 结束哨兵收尾，空字段
保留；用 `free` 释放数组，读取 token 期间保持缓冲区存活。

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("id,,name,");
  sx_view_t *fields = sx_split(csv, ",");
  if (!fields)
    return 1;
  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("[%zu] \"%.*s\" (offset %zu, len %zu)\n", i,
           (int)fields[i].len, sx_buf_view_mut(fields[i]),
           fields[i].offset, fields[i].len);
  free(fields);
  sx_free(csv);
  return 0;
}
```

```text
[0] "id" (offset 0, len 2)
[1] "" (offset 3, len 0)
[2] "name" (offset 4, len 4)
[3] "" (offset 9, len 0)
```

### 裁剪输入

`sx_view_trim` 从两端去掉 ASCII 空白集合 `{space, \t, \v, \f, \r, \n}`，
不进行拷贝。若缓冲区之后可能变化，请把裁剪后的视图复制成自有缓冲区。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *raw = sx_new_from_cstr("  \t hello world \r\n");
  sx_view_t trimmed = sx_view_trim(sx_view(raw));
  printf("[%.*s]\n", sx_len_view(trimmed), sx_buf_view_mut(trimmed));

  sx_t *copy = sx_new_from_view(trimmed); /* independent copy */
  printf("[%s] len=%d\n", sx_buf_mut(copy), sx_len(copy));
  sx_free(copy);
  sx_free(raw);
  return 0;
}
```

```text
[hello world]
[hello world] len=11
```

只含空白的视图裁剪成其槽位末尾的空视图；整缓冲区视图即
`(offset = len(h), len = 0)`。

### 原地修改

`sx_write` 覆盖写入但不缩短，`sx_erase` 删除一段，`sx_insert` 右移尾部。
三者都返回 `self`。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("the quick brown fox");
  s = sx_write_cstr(s, 10, "green");         /* same length: overwrite */
  s = sx_erase(s, 0, 4);                     /* remove "the " */
  s = sx_insert_cstr(s, 0, "red ");          /* prepend */
  s = sx_truncate(s, (size_t)sx_len(s) - 4); /* drop " fox" */
  printf("%s (len %d)\n", sx_buf_mut(s), sx_len(s));
  s = sx_clear(s);                           /* len 0, capacity kept */
  printf("after clear: len=%d cap=%d\n", sx_len(s), sx_cap(s));
  sx_free(s);
  return 0;
}
```

```text
red quick green (len 15)
after clear: len=0 cap=20
```

`sx_erase` 在 `idx + n > len` 时是返回 `self` 的空操作，`n == 0` 同样如此。
`sx_insert` 要求 `0 <= idx <= len`；`idx > len` 属于契约之外。增长的
`sx_truncate` 与越界的 `sx_write` 会留下未指定的间隙字节。

### 正则提取、替换与分割

编译一次、复用、最后释放。匹配与 token 结果是指向 subject 的视图；替换返回
新缓冲区，永不修改 subject。

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_reg_t *re = sx_reg_compile("[a-z]+@[a-z.]+");
  if (!re)
    return 1;
  sx_t *msg = sx_new_from_cstr("bob@example.com;alice@example.org");

  sx_view_t m = sx_reg_match(re, msg); /* leftmost match */
  if (m.ref != NULL)
    printf("first: %.*s\n", (int)m.len, sx_buf_view_mut(m));

  sx_view_t *hits = sx_reg_match_all(re, msg); /* non-overlapping */
  for (size_t i = 0; hits && hits[i].ref != NULL; i++)
    printf("hit %zu: %.*s\n", i, (int)hits[i].len, sx_buf_view_mut(hits[i]));
  free(hits);

  sx_t *out = sx_reg_replace(re, msg, "<$0>"); /* $0 = whole match */
  printf("replaced: %s\n", sx_buf_mut(out));
  sx_free(out);

  sx_view_t *tokens = sx_reg_split(re, msg); /* gaps between matches */
  for (size_t i = 0; tokens && tokens[i].ref != NULL; i++)
    printf("token %zu: \"%.*s\"\n", i, (int)tokens[i].len,
           sx_buf_view_mut(tokens[i]));
  free(tokens);

  sx_free(msg);
  sx_reg_free(re);
  return 0;
}
```

```text
first: bob@example.com
hit 0: bob@example.com
hit 1: alice@example.org
replaced: <bob@example.com>;<alice@example.org>
token 0: ""
token 1: ";"
token 2: ""
```

## 常见陷阱

测试有意列为契约之外的调用与习惯，以及容易忽略的边界情况：

- 把缓冲区交给消费型调用之后继续使用：`sx_append(s, part)`、
  `sx_write(s, idx, part)` 与 `sx_insert(s, idx, part)` 都会释放 `part`。
- 通过 owned 形式把缓冲区传给它自己（`sx_append(s, s)`、`sx_write(s, idx, s)`、
  `sx_insert(s, idx, s)`）属于契约之外。原地翻倍请用
  `sx_append_view(s, sx_view(s))` 或 `sx_write_view`；`sx_insert_view` 仅当
  切片起点不晚于 `idx`（测试覆盖的情形）才可用，起点晚于 `idx` 的自别名切片
  会被尾部右移覆盖，请先复制。
- `sx_insert*` 的 `idx > len`：尾部右移会下溢。受测契约是 `0 <= idx <= len`。
  `sx_write*` 不同：它会填充并扩展。
- 读取增长 `sx_truncate` 后 `[old_len, n)` 内的未指定字节，或越界 `sx_write`
  后 `[len, idx)` 内的字节；读取前先把间隙写满。
- 在 `idx + len` 接近 `SIZE_MAX` 时调用 `sx_write*` 却假定写入已发生：
  算术溢出会被拒绝，返回 `NULL` 且 `self` 不变。请检查返回值。
- 在 `sx_split`、`sx_reg_match_all`、`sx_reg_split` 或 `sx_find_all*` 返回的
  视图/数组仍在使用时修改缓冲区，或在长度变化之后继续保留视图。视图不会被调整，
  扩容后可能指向已释放的内存。
- 把 `sx_find_all*` 的结果读到 `arr[0] + 1` 项之外；它的形状是
  `{count, positions...}`。
- 把 `sx_erase` 在 `idx + n > len` 时当成错误：它是有文档说明的空操作。
- 向不检查 NULL 的函数传 NULL 句柄：`sx_free(NULL)`、`sx_len(NULL)` 属于
  契约之外（见[错误与哨兵值](#错误与哨兵值)）。
- 忘记释放：缓冲区用 `sx_free`，视图数组用 `free`，已编译模式用 `sx_reg_free`。
  视图不需要释放。
- `sx_split` 分隔符为空时返回 `NULL`，不会每个字节都分割。
- 正则模式是 C 字符串，不能包含字面 NUL。subject 可以包含。
- `sx_new_from_cstr` 与所有 `_cstr` 实参都在第一个 NUL 处停下；二进制内容请用
  视图。
- `sx_substr` 要求 `idx + n <= len(h)`；`sx_view_range` 要求
  `off <= len(底层缓冲区)`（`len` 会被钳制到缓冲区末尾，`off` 不会）。两者都不会
  钳制前置条件本身。
- 多线程共享同一个 `sx_t` 且不加同步：库内部没有锁，只有已编译的 `sx_reg_t`
  文档化为不可变、可共享。

## 性能说明

- **已知追加量时先预留。** `sx_reserve(self, n)` 保证 `cap >= n + 1` 且不缩小
  容量，`n` 个内容字节无需重新分配即可容纳；否则扩容仍摊还：翻倍，达到
  64 KiB 后改为每次增加 `0x10000`。
- **视图不拷贝。** `sx_view`、`sx_view_advance` 等视图移动函数都是 O(1) 且不
  分配；只在需要独立缓冲区时才用 `sx_substr` / `sx_new_from_view`。
- **查找是线性的。** 单字节 needle 使用 `memchr` / `memrchr`；更长的 needle
  使用正向或反向 KMP，复杂度 O(n + m)。`sx_find_all*` 会报告重叠匹配，并按
  匹配数精确分配数组。
- **分割与正则扫描都是两遍。** 先统计 token/匹配数，再一次性精确分配；
  对 subject 扫描两遍。
- **正则只编译一次。** `sx_reg_compile` 是开销最大的调用；match、replace、split
  复用已编译模式，每次只分配自己的匹配数据；`sx_reg_is_match` 最便宜。
- **容量不会归还。** `sx_truncate`、`sx_erase` 与 `sx_clear` 都保留分配，
  这使清空后的缓冲区可以复用，但也意味着峰值分配会一直保留到 `sx_free`。

## 常见问题

**使用缓冲区需要 PCRE2 吗？** 构建时需要：源码树链接 `pcre2-8`，`./make` 需要
`libpcre2-dev`；已安装的 `libstringx` 只需 `-lstringx`，共享库自带 PCRE2 依赖。

**所有函数都支持 NULL 吗？** 不。只有[错误与哨兵值](#错误与哨兵值)中列出的
情况有定义；其他情况下 NULL 句柄都属于契约之外。

**String X 感知 UTF-8 吗？** 不：它是字节库，长度、比较与查找都按字节精确处理。
正则运行于 PCRE2 8 位非 UTF 模式，`\d`、`\w`、`\s`、`\b` 仅匹配 ASCII。

**如何复制一个视图？** `sx_new_from_view(v)` 返回自有缓冲区，含视图 `len` 字节的
精确副本。

**如何取得子串？** `sx_view_range`/`sx_view_advance` 给零拷贝视图；
`sx_substr(h, idx, n)` 给自有副本。

**为什么 `sx_len` 返回 `int`？** 这是库的实际大小上限：内容超过 `INT_MAX`
字节不在受测契约之内。偏移是 `size_t`，因此查找与视图位置仍是全宽度。

**已编译的正则可以在线程间共享吗？** 可以：已编译的 `sx_reg_t` 不可变，每次
调用使用各自的匹配数据。在线程间共享 `sx_t` 缓冲区则不受支持。

**零长匹配是什么样子？** `sx_reg_match` 返回 `ref != NULL`、`len == 0` 的
视图；`sx_reg_match_all` 与 `sx_reg_split` 照常报告，并把扫描推进一个字节。

---

[← 上一页](getting-started.md) · [文档首页](README.md) · [下一页 →](api-buffer.md)
