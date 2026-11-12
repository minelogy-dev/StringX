[English](../en_us/getting-started.md) · 中文

[文档首页](README.md) › 快速上手

# 快速上手

String X 是一个轻量的 C23 字符串库：每个字符串是一段 NUL 结尾的缓冲区，
切片是零拷贝视图，全部按字节长度处理（内嵌 NUL 是普通数据）。公开接口只有
一个头文件 `<sx.h>` 和一个库 `libstringx`（`-lstringx`）；可选的 PCRE2 进阶层
提供正则表达式。版本：**v0.5**。

## 环境要求

| 要求 | 说明 |
| --- | --- |
| Linux | 经过测试的平台；构建与测试均面向 Linux |
| C23 编译器 | GCC 14+ 或 Clang 19+；CI 在 Ubuntu 24.04 上使用 GCC 14 与 Clang 19 |
| `forge` | [github.com/minelogy-dev/forge](https://github.com/minelogy-dev/forge) |
| `libpcre2-dev` | PCRE2 10.46，8 位 API；构建时需要；库自身会链接 `pcre2-8` |
| `pkg-config` | 推荐使用者使用；安装包自带 `libstringx.pc` |

## 从源码构建

```sh
forge .        # generate ./make
./make         # build -> build/output/libstringx.so
./make test    # build and run every test under tests/ (ASan + UBSan + LSan)
```

`./make` 会把公开头文件同步到 `build/output/include`；`./make test` 把每个
`tests/*.c` 当作独立程序在 AddressSanitizer、UBSan 与 LeakSanitizer 下运行
（`-j 2` 并行）。`build.conf` 选择编译器，也是版本号的唯一来源。

## 安装

```sh
./make install                  # default prefix: /usr/local (may need sudo)
./make install "$HOME/.local"   # user-owned prefix
```

install 目标会先构建，然后复制产物：

| `<prefix>` 下的路径 | 内容 |
| --- | --- |
| `lib/libstringx.so` | 共享库 |
| `include/sx.h` | 公开头文件，以及 `sx_core.h`、`sx_raw.h` |
| `lib/pkgconfig/libstringx.pc` | pkg-config 元数据 |
| `share/doc/libstringx/` | `LICENSE`、`THIRD_PARTY_NOTICES.md`、`copyright` |

### 使用已安装的库

```sh
cc app.c $(pkg-config --cflags --libs libstringx) -o app
```

前缀不在默认搜索路径时，设置 `PKG_CONFIG_PATH` 让 pkg-config 找到元数据，
并设置运行时库路径：

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
cc app.c $(pkg-config --cflags --libs libstringx) -o app
LD_LIBRARY_PATH="$HOME/.local/lib" ./app
```

不使用 pkg-config 时，等价的构建命令是：

```sh
cc app.c -I<prefix>/include -L<prefix>/lib -lstringx -o app
```

只需链接 `-lstringx`：`libstringx` 已链接 `pcre2-8`，无需额外库参数，但运行时
需要 `libpcre2-8`。

### Debian 软件包

```sh
./make gen_deb
sudo apt install ./build/deb/libstringx_0.5_amd64.deb
```

`gen_deb` 生成 `build/deb/libstringx_<version>_<arch>.deb`：版本号取自
`build.conf` 并去掉前缀 `v`，架构名取自 `uname -m`（`x86_64` → `amd64`，
`aarch64` → `arm64`）；依赖 `libc6` 与 `libpcre2-8-0`。

## Hello, String X

README 中的快速上手程序：每次修改都返回同一句柄，因此整条链是一个表达式；
`sx_append` 消费临时缓冲区，结果只需一次 `sx_free`。扩容后句柄仍然有效，
且每次调用后缓冲区都以 NUL 结尾。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  /* One expression: every call returns the same handle, and the temporary
     buffer is consumed by sx_append, so there is nothing else to free. */
  sx_t *msg = sx_append(sx_append_cstr(sx_new(), "Hello, "),
                        sx_new_from_cstr("String X!"));

  printf("%s (%d bytes)\n", sx_buf_mut(msg), sx_len(msg));
  sx_free(msg);   /* one buffer, one free */
  return 0;
}
```

按上文完成安装后，用下面的命令构建并运行：

```sh
cc hello.c $(pkg-config --cflags --libs libstringx) -o hello
./hello
```

```text
Hello, String X! (16 bytes)
```

在安装之前，也可以直接使用源码树的构建输出：

```sh
cc hello.c -Ibuild/output/include -Lbuild/output -lstringx -o hello
LD_LIBRARY_PATH=build/output ./hello
```

## 十分钟概览

用户最常做的五件事，每个一个完整程序，编译方式同 `hello.c`。

### 创建缓冲区

`sx_new` 返回空缓冲区，`sx_new_from_cstr` 复制一个 C 字符串，`sx_dup` 生成
一份独立的深拷贝。所有结果都归调用方所有。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *empty = sx_new();                  /* len 0 */
  sx_t *hello = sx_new_from_cstr("hello");
  sx_t *copy = sx_dup(hello);
  printf("empty: len=%d cap=%d\n", sx_len(empty), sx_cap(empty));
  printf("hello: len=%d [%s]\n", sx_len(hello), sx_buf_mut(hello));
  printf("copy:  len=%d [%s]\n", sx_len(copy), sx_buf_mut(copy));
  sx_free(empty);
  sx_free(hello);
  sx_free(copy);
  return 0;
}
```

```text
empty: len=0 cap=32
hello: len=5 [hello]
copy:  len=5 [hello]
```

### 追加与插入

每次修改都返回传入的那个缓冲区，因此修改可以链式书写；即使底层数组重新分配，
句柄也保持不变。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("abc");
  s = sx_append_cstr(s, "def");    /* "abcdef" */
  s = sx_insert_cstr(s, 3, "-");   /* "abc-def" */
  s = sx_write_cstr(s, 0, "A");    /* "Abc-def" */
  printf("%s (len %d)\n", sx_buf_mut(s), sx_len(s));
  sx_free(s);
  return 0;
}
```

```text
Abc-def (len 7)
```

### 查找

`sx_find*` 返回最左匹配的字节偏移，`sx_rfind*` 返回最右匹配；未找到的哨兵值
是 `(size_t)-1`。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("one two one");
  size_t first = sx_find_cstr(s, "one");
  size_t last = sx_rfind_cstr(s, "one");
  printf("first=%zu last=%zu\n", first, last);
  if (sx_find_cstr(s, "three") == (size_t)-1)
    printf("three: not found\n");
  if (sx_starts_with_cstr(s, "one"))
    printf("starts with one\n");
  sx_free(s);
  return 0;
}
```

```text
first=0 last=8
three: not found
starts with one
```

### 视图与分割

视图是 `{ref, offset, len}`，它别名一段缓冲区而不拷贝。`sx_split` 返回由这类
视图组成的数组，数组末尾是一个 `ref` 为 `NULL` 的结束哨兵；数组本身用
`free` 释放。

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("id,name,email");
  sx_view_t head = sx_view_advance(sx_view(csv), 0);
  head.len = 2;                        /* "id" - a view, no copy */
  printf("head: %.*s\n", sx_len_view(head), sx_buf_view_mut(head));

  sx_view_t *fields = sx_split(csv, ",");
  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("field %zu: %.*s\n", i, (int)fields[i].len,
           sx_buf_view_mut(fields[i]));
  free(fields);                        /* the array, not the views */
  sx_free(csv);
  return 0;
}
```

```text
head: id
field 0: id
field 1: name
field 2: email
```

### 正则

进阶层封装了 PCRE2。编译一次模式，对任意多个缓冲区执行，最后释放。
`sx_reg_replace` 返回新缓冲区，永不修改原串。

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+\\.\\w+)");
  if (!re)
    return 1;
  sx_t *msg = sx_new_from_cstr("bob@example.com, alice@example.org");
  if (sx_reg_is_match(re, msg)) {
    sx_view_t m = sx_reg_match(re, msg);
    printf("first match: %.*s\n", (int)m.len, sx_buf_view_mut(m));
  }
  sx_t *out = sx_reg_replace(re, msg, "$1 at $2");
  printf("replaced: %s\n", sx_buf_mut(out));
  sx_free(out);
  sx_free(msg);
  sx_reg_free(re);
  return 0;
}
```

```text
first match: bob@example.com
replaced: bob at example.com, alice at example.org
```

模式是 C 字符串，按 PCRE2 的 8 位非 UTF 模式匹配，因此 `\w` 等字符类只
匹配 ASCII。subject 按字节长度匹配，可以包含任意字节，包括 NUL。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [文档首页](README.md) | 语言选择与完整文档列表 |
| [用户指南](guide.md) | 心智模型、所有权、二进制安全、示例与陷阱 |
| [API 参考：缓冲区与视图](api-buffer.md) | 创建、容量、访问器、修改、视图 |
| [API 参考：查找、比较与分割](api-search.md) | `sx_find*`、`sx_rfind*`、`sx_cmp*`、`sx_split` |
| [正则表达式](regex.md) | `sx_reg_*` 的匹配、替换与分割家族 |
| [设计与内部实现](design.md) | 数据布局、扩容策略、算法与限制 |
| [构建、测试与打包](build-and-test.md) | 构建目标、测试框架、安装 |
| [贡献指南](contributing.md) | 工作流程、代码风格、测试、许可 |
| [持续集成](ci.md) | 工作流、必需检查、CodeQL |

---

[文档首页](README.md) · [下一页 →](guide.md)
