# String X

一个轻量的 C 字符串库：**单头 API** + 字节精确、零拷贝的视图模型。
版本 **v0.5**。库本身以 **libstringx** 构建并链接（`-lstringx`），项目主页：
[github.com/minelogy-dev/StringX](https://github.com/minelogy-dev/StringX)。

使用时只需：

```c
#include <sx.h>
```

并链接 `-lstringx` —— 一个头文件，一个库。

## 设计

- **缓冲区模型。** 每个 `sx_t` 拥有一个单 NUL 结尾的缓冲区：
  `ptr[0 .. len-1]` 是内容，`ptr[len]` 恒为 `'\0'`，`cap` 是总分配量。
  `len` 计的是**内容字节数**（不含结尾符），一切匹配/查找按长度进行——
  内嵌 NUL 是普通数据（二进制安全）。
- **视图。** `sx_view_t {ref, offset, len}` 是缓冲区某一段的别名：
  **不拷贝、不修改**原串。多数 API 返回视图（`ref == NULL` 哨兵表示"无"）。
- **分层。** core 层（经 `sx.h` 可达）提供缓冲区、视图、查找、比较、
  split 等核心操作；adv 层基于 **PCRE2** 提供正则（8 位非 UTF 模式，
  `\d \w \b …` 仅匹配 ASCII）。
- **确定性契约。** 每个函数在 `sx.h` 里写明精确的行为边界；非法输入不会
  崩溃——返回文档化的"未找到/出错"值。

## 依赖

- C23 编译器（GCC / Clang），Linux
- 构建需要 `forge` 构建工具（https://github.com/minelogy-dev/forge —— 自定义
  构建系统：它会将 `build.c` 编译成本地 `./make` 入口）
- `libpcre2-dev`（PCRE2 8 位）——正则层的依赖；共享库自身已链接
  `pcre2-8`，使用者只需 `-lstringx`

## 构建

```sh
forge .        # 把 build.c 编译成 ./make（改动 build.c 后需重跑）
./make         # 构建 -> build/output/libstringx.so（头文件随附于 build/output/include）
./make test    # 编译并运行 tests/ 下每个测试（ASan + UBSan）
```

## 快速上手

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *msg = sx_new_from_cstr("hello, world");
  size_t at = sx_find_cstr(msg, ", ");
  sx_t *hi = sx_substr(msg, 0, at);
  fwrite(sx_buf_mut(hi), 1, (size_t)sx_len(hi), stdout);
  putchar('\n');                        /* hello */
  sx_free(hi);
  sx_free(msg);
  return 0;
}
```

编译：

```sh
cc app.c -I<头文件路径> -L<库路径> -lstringx
```

## 正则（adv 层：`sx_reg_*`）

编译一次模式，对任意多个串执行，最后释放：

```c
sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+)");
sx_t *h = sx_new_from_cstr("bob@example.com and alice@example.org");

if (sx_reg_is_match(re, h)) { ... }            /* 0/1，最便宜 */
sx_view_t m = sx_reg_match(re, h);             /* 最左匹配，返回视图    */
sx_view_t *all = sx_reg_match_all(re, h);      /* 数组，ref==NULL 收尾  */
sx_t *out = sx_reg_replace(re, h, "[$2: $1]"); /* 新串，$n 反向引用     */
sx_view_t *tok = sx_reg_split(re, h);          /* 匹配之间的 token      */

sx_reg_free(re);
```

契约要点（详见 `sx.h`）：

- **match 家族** —— `sx_reg_is_match`（单次匹配，零分配）/ `sx_reg_match`
  （最左匹配）/ `sx_reg_match_all`（从左到右非重叠扫描）。
- **replace 家族** —— `sx_reg_replace`（全部替换）/ `sx_reg_replace_first`
  （仅首处）；模板用 PCRE2 替换语法（`$0`/`$n`）；未参与或不存在的分组
  展开为空串；原串永不修改。
- **split 家族** —— `sx_reg_split`；形态与 `sx_split` 完全一致（空 token
  保留，token 数 == 匹配数 + 1）。
- subject 按字节处理，双向二进制安全；零长匹配照常报告并把扫描推进一字节
  （`a*` 这类模式不会卡死扫描）；编译后的正则不可变，可跨线程共享。

## API 一览

| 分组 | 函数 |
|---|---|
| 构造 / 释放 | `sx_new`, `sx_new_from_cstr`, `sx_new_from_view`, `sx_new_with_cap`, `sx_dup`, `sx_free` |
| 访问器 | `sx_len`, `sx_len_view`, `sx_cap`, `sx_buf_mut`, `sx_buf_view_mut`, `sx_view` |
| 修改 | `sx_append*`, `sx_write*`, `sx_insert*`, `sx_truncate`, `sx_erase`, `sx_clear`, `sx_reserve`, `sx_substr` |
| 视图操作 | `sx_view_advance`, `sx_view_retreat`, `sx_view_shift`, `sx_view_range`, `sx_view_trim…` |
| 查找 | `sx_find*`, `sx_rfind*`, `sx_find_all*` |
| 比较 | `sx_cmp*`, `sx_starts_with*`, `sx_ends_with*` |
| 分隔 | `sx_split(sep)` |
| 正则 | `sx_reg_compile`, `sx_reg_free`, `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all`, `sx_reg_replace`, `sx_reg_replace_first`, `sx_reg_split` |

## 测试

```sh
./make test
```

`tests/` 下每个文件都是自包含程序（依赖在其头注释中声明），直接编译库
源码运行，全程开启 AddressSanitizer + UBSan + LeakSanitizer。每个测试文件
都在文档头写明其锁定的行为边界。

## 许可

MIT —— 见 [LICENSE](LICENSE) 及源码文件中的 SPDX 标注。正则层依赖
**PCRE2**（BSD-3-Clause），其归属与许可全文见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。