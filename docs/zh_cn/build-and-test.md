[English](../en_us/build-and-test.md) · 中文

[文档首页](README.md) › 构建、测试与打包

# 构建、测试与打包

String X 使用 [Forge](https://github.com/minelogy-dev/forge) 构建。本页介绍
环境要求、`./make` 任务、测试框架与内存检查器（sanitizer）、安装、pkg-config
与 Debian 打包。CI 见[持续集成](ci.md)。

## 环境要求

| 依赖 | 说明 |
| --- | --- |
| Linux | CI 使用 Ubuntu 24.04；其他 Linux 发行版同样可用。 |
| C23 编译器 | GCC 14+ 或 Clang 19+。 |
| `libpcre2-dev` | PCRE2 10.46，8 位，正则层与每个测试都需要。运行时库需要 `libpcre2-8-0`。 |
| `pkg-config` | 推荐使用者安装；CI 还用它链接已安装的库。 |
| Forge | [github.com/minelogy-dev/forge](https://github.com/minelogy-dev/forge)；`./make` 是生成物，不纳入版本库。 |
| `tar` 与 `ar` | 只有 `./make gen_deb` 需要；`dpkg-deb` 仅用于检查产物。 |

命令在仓库根目录执行；`build.c`、`build.conf` 相对该目录读取。

## 构建工具

从 [Forge](https://github.com/minelogy-dev/forge) 仓库安装 Forge。运行一次
`forge .` 生成 `./make` 后，日常命令为：

```sh
forge .        # 生成 ./make
./make         # 构建 -> build/output/libstringx.so（头文件随附于 build/output/include）
./make test    # 编译并运行 tests/ 下每个测试（ASan + UBSan）
```

`./make` 是生成物且被 gitignore；修改 `build.c` 或 `build.conf` 后重跑
`forge .`。

## 目标与命令

| 命令 | 作用 |
| --- | --- |
| `forge .` | 由 `build.c` 与 `build.conf` 生成 `./make`。 |
| `./make` | 默认的 `build` 任务：把 `src/` 编译成 `build/output/libstringx.so`，并把 `include/*.h` 同步到 `build/output/include/`。 |
| `./make check` | 用警告选项和 GCC 分析器编译库目标，但不做头文件同步。 |
| `./make test` | 在 ASan + UBSan + LSan 下构建并运行 `tests/` 下的每个文件。 |
| `./make test -j 2` | 同上，并并行编译每个测试的源文件（这里是 2 路并行）。 |
| `./make install <prefix>` | 先构建，再安装库、头文件、声明文件和 `libstringx.pc`；前缀默认为 `/usr/local`。 |
| `./make gen_deb` | 先构建，再暂存并生成 `build/deb/libstringx_<版本>_<架构>.deb`。 |

`-v` 让 `./make test` 与 `./make check` 打印完整编译命令；不加时只显示
`PASSED`/`FAILED` 行与汇总。`-j N` 并行执行一个目标的编译阶段。

产物：

| 路径 | 由谁生成 | 内容 |
| --- | --- | --- |
| `build/output/libstringx.so` | `./make`、`./make check`、`./make install`、`./make gen_deb` | 共享库 |
| `build/output/include/` | `./make`、`./make install`、`./make gen_deb` | `sx.h`、`sx_core.h`、`sx_raw.h` 的同步副本 |
| `build/exe-obj/` | `./make`、`./make check` | 各源文件的目标文件及其 `.d`/`.meta` 增量信息 |
| `test/<名称>` | `./make test` | 每个测试文件一个可执行文件（`tests/new.c` → `test/new`） |
| `build/opt/` | `./make test` | 测试目标文件及其 `.d`/`.meta` 文件 |
| `build/deb/` | `./make gen_deb` | Debian 暂存目录与最终 `.deb` |

`./make test` 无需先运行 `./make`：每个测试直接编译库源码。

## build.conf

`build.conf` 在生成 `./make` 时读取；每个 `KEY=VALUE` 行变成
`-DCONF_KEY=VALUE` 宏，`#` 开头为注释。修改后重跑 `forge .`。

| 键 | 取值 | 作用 |
| --- | --- | --- |
| `compiler` | `GCC` 或 `CLANG` | 工具链后端。`build.c` 把 `CONF_compiler` 传给 `set_toolchain`，因此它同时决定库与测试使用的编译器。 |
| `compiler_path` | 绝对路径，例如 `/usr/bin/gcc` | 生成 `./make` 时使用的编译器二进制文件。 |
| `version` | `v0.5` | 项目版本的唯一来源。`build.c` 把它字符串化为 `SX_VERSION`；安装与打包会去掉开头的 `v`。 |
| `sanitize` | `1` | CI 中由 `.github/scripts/configure.sh` 追加。内存检查器选项本身由每个测试在头部 DSL 中声明。 |

## 运行测试

`./make test` 递归扫描 `tests/`，把每个 `tests/*.c` 编译成 `test/<名称>` 并
运行。成功打印 `PASSED: <文件>`，首个断言失败打印
`FAILED: <文件>:<行号>: <表达式>`。汇总形如：

```text
forge: 24 tests, 24 passed, 0 failed
```

测试编译或运行失败时，任务以非零状态退出。

测试不链接 `libstringx.so`：各自直接编译库源码，并在头部声明选项，包括
`-fsanitize=address,undefined` 与 `-fno-omit-frame-pointer`。LeakSanitizer 与
ASan 一起运行，忘记释放缓冲区或数组的测试会失败。少数测试在普通配置与强制
配置的兄弟目标间共享本地用例头（`find_differential.h`、
`write_overflow_cases.h`）；每个 `.c` 仍是独立目标。

设置 CI 的内存检查器选项：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

单独重跑已构建的测试：

```sh
./test/reserve_stress
```

### 精确预留压力测试

`tests/reserve_stress.c` 与 `tests/write_overflow_exact.c` 使用
`-DSX_FORCE_EXACT_RESERVE` 构建，头部以
`// Option -DSX_FORCE_EXACT_RESERVE` 声明。该开关把 `src/sx_raw.c` 的预留助手
从几何增长改为精确分配（`cap == need + 1`）：分配正好止于结尾符，越过 `len`
的访问立刻命中 ASan 红区，多余的容量无法掩盖差一错误或过期长度。压力测试用
确定性 LCG 驱动三个活动缓冲区，每次修改都断言完整内容（模型不超过 512 字节）；
写入溢出测试在精确分配下重跑其 `SIZE_MAX` 范围用例。其余测试使用几何增长。

## 测试文件与头部 DSL

每个测试都是自包含程序。开头连续的 `//` 行是测试运行器读取的头部
DSL，第一行非 `//` 内容结束。

| 条目 | 作用 |
| --- | --- |
| `// Include <目录>` | 添加 `-I<目录>`。每个测试都包含 `include`、`src` 和 `/usr/lib/forge/include`；最后一项在 `CPATH` 未提供时用于找到 `<build.h>`。 |
| `// Source <文件>` | 把一个源文件加入测试可执行文件。测试都会加入 `src/sx_raw.c`、`src/sx_core.c` 与 `src/sx_adv.c`。 |
| `// Link <库>` | 添加 `-l<库>`。只有 `tests/reg.c` 加入 `pcre2-8`；其他测试不使用正则层。 |
| `// Library <目录>` | 添加 `-L<目录>`。测试加入 `/usr/local/lib`。 |
| `// Option <选项>` | 添加编译器、链接器与汇编器选项。测试使用 `-std=c23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`，以及诸如 `-DSX_FORCE_EXACT_RESERVE` 的测试专用宏。 |

不带关键字的行延续当前作用域（初始为 `Source`）；测试为清晰起见写出了每个
关键字。

每个测试都编译 `src/sx_adv.c`，因此 `build.c` 通过保留的 `LANGUAGE` 覆盖项
为测试目标附加 `pcre2-8`；`tests/reg.c` 还声明了 `// Link pcre2-8`。

程序包含被测头文件（`"sx.h"`，仅核心层用 `"sx_core.h"`）与提供两个测试宏的
`<build.h>`：

| 宏 | 作用 |
| --- | --- |
| `TEST(表达式)` | 失败时打印 `FAILED: <文件>:<行号>: <表达式>` 并以 1 退出；成功时不输出并继续。 |
| `PASS()` | 打印 `PASSED: <文件>`；在 `main` 末尾调用。 |

### 添加测试

1. 创建 `tests/<名称>.c` —— 每个文件只覆盖一个行为领域。
2. 从现有测试复制头部 DSL 块，按需调整 `Source`、`Link` 与 `Option`。
3. 写明行为边界：锁定什么（`In scope (contract)`），不覆盖什么
   （`Out of contract (not exercised)`）。
4. 包含被测头文件，用 `TEST` 断言，释放每个缓冲区与数组，最后调用 `PASS()`。
5. 运行 `./make test`；新文件会被自动发现。

```c
// Include include
// Include src
// Include /usr/lib/forge/include
// Source src/sx_raw.c
// Source src/sx_core.c
// Source src/sx_adv.c
// Option -std=c23
// Option -O1
// Option -g
// Option -fsanitize=address,undefined
// Option -fno-omit-frame-pointer
// Library /usr/local/lib
#include "sx.h"
#include <build.h>

/*
 * Behavior boundaries — sx_erase (example)
 * ===================================================================
 * In scope (contract):
 *   · sx_erase(h, idx, n) removes the n bytes starting at idx,
 *     shifts the tail left, keeps ptr[len] == '\0', and returns h.
 *     n == 0 is a no-op; idx + n > len is a no-op returning h.
 * Out of contract (not exercised):
 *   · h == NULL or freed; erase idx > len with n such that idx + n
 *     wraps; keeping sx_buf_mut pointers across edits.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("hello");
  TEST(h != NULL);
  TEST(sx_erase(h, 1, 3) == h);
  TEST(sx_cmp_cstr(h, "ho") == 0);
  sx_free(h);
  PASS();
  return 0;
}
```

## 安装

`./make install` 先构建（含头文件同步），再复制：

| 目标位置 | 内容 |
| --- | --- |
| `<prefix>/lib/libstringx.so` | 共享库 |
| `<prefix>/include/sx.h` | 公开 API 头文件 |
| `<prefix>/include/sx_core.h` | 被 `sx.h` 包含的核心层 |
| `<prefix>/include/sx_raw.h` | 内部内存分配头文件；不属于受支持的 API |
| `<prefix>/lib/pkgconfig/libstringx.pc` | pkg-config 元数据 |
| `<prefix>/share/doc/libstringx/LICENSE` | MIT 许可全文 |
| `<prefix>/share/doc/libstringx/THIRD_PARTY_NOTICES.md` | PCRE2 归属与许可全文 |
| `<prefix>/share/doc/libstringx/copyright` | `LICENSE` 的副本，Debian 惯用文件名 |

默认前缀 `/usr/local`；可传入无需 root 即可写入的前缀，例如：

```sh
./make install "$HOME/.local"
```

pkg-config 文件在安装时生成：`prefix=` 为安装前缀，`Version` 为 `build.conf`
版本去掉开头 `v`（即 `0.5`）。

## 使用已安装的库

使用 pkg-config：

```sh
cc -std=c23 hello.c $(pkg-config --cflags --libs libstringx) -o hello
./hello
```

不使用 pkg-config：

```sh
cc -std=c23 hello.c -I<prefix>/include -L<prefix>/lib -lstringx -o hello
```

非标准前缀时，让 pkg-config 与运行期加载器指向它，或写入 rpath：

```sh
export PKG_CONFIG_PATH="<prefix>/lib/pkgconfig"
cc -std=c23 hello.c $(pkg-config --cflags --libs libstringx) \
  -Wl,-rpath,"<prefix>/lib" -o hello
```

库自身已链接 `pcre2-8`，使用者无需 `-lpcre2-8`。完整程序：

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *h = sx_new_from_cstr("hello, world");
  if (h == NULL)
    return 1;
  printf("len=%d cap=%d: %.*s\n", sx_len(h), sx_cap(h), sx_len(h),
         sx_buf_mut(h));
  sx_free(h);
  return 0;
}
```

## Debian 包

`./make gen_deb` 先构建，再用 `tar` 与 `ar` 暂存并打包 Debian `.deb`
（不依赖 `dpkg-deb`）：

| 路径 | 内容 |
| --- | --- |
| `build/deb/data/usr/` | 位于 FHS 根下的包数据树：`lib/`、`include/` 与 `share/doc/libstringx/` |
| `build/deb/control-dir/control` | 包元数据 |
| `build/deb/debian-binary`、`control.tar.gz`、`data.tar.gz` | 包的各组成部分 |
| `build/deb/libstringx_<版本>_<架构>.deb` | 最终包 |

版本取自 `build.conf` 去掉开头 `v`；架构取自 `uname -m`（`x86_64` →
`amd64`，`aarch64` → `arm64`，其他值不变）。包装到 `/usr` 下，声明
`Depends: libc6, libpcre2-8-0`，包内 `libstringx.pc` 以 `/usr` 为前缀，暂存
路径不会写入。

检查安装：

```sh
dpkg-deb --info build/deb/libstringx_*.deb
dpkg-deb --contents build/deb/libstringx_*.deb
sudo apt install ./build/deb/libstringx_*.deb
```

## 故障排查

| 现象 | 原因与处理 |
| --- | --- |
| `forge: command not found` | Forge 不在 `PATH` 中；按 Forge 仓库的安装说明配置。 |
| `pcre2.h: No such file or directory` 或 `cannot find -lpcre2-8` | 安装 `libpcre2-dev`（Ubuntu：`sudo apt-get install libpcre2-dev`）。 |
| 克隆后出现 `./make: No such file or directory` | `./make` 是生成物且被 gitignore。先运行 `forge .`。 |
| 修改 `build.c` 或 `build.conf` 后没有生效 | `./make` 由旧脚本生成。重新运行 `forge .`。 |
| Clang 下测试链接因 `-flto` 失败 | 固定版本的构建工具在并行 Clang 链接时会输出仅适用于 GCC 的 `-flto=N`，因此 `build.c` 中的测试目标关闭 LTO；生产目标保留 LTO。 |
| `install` 报告权限错误 | 前缀不可写。传入你有权限的前缀，例如 `./make install "$HOME/.local"`。 |
| 测试因泄漏报告失败 | 有缓冲区或数组未释放。每个 `sx_t` 都要用 `sx_free` 释放，每个返回的数组都要用 `free` 释放。 |
| 使用者程序找不到 `libstringx.so` | 把 `LD_LIBRARY_PATH` 设为 `<prefix>/lib`，用 `-Wl,-rpath,<prefix>/lib` 链接，或安装到标准前缀。 |

不要在库源码或程序中包含 `<build.h>`：它是构建脚本的 DSL 头文件，非
String X API。程序只包含 `<sx.h>`。

## 持续集成

运行这些命令的工作流（推送与拉取请求）——构建矩阵、分析器、质量检查、CodeQL
与合并保护——见[持续集成](ci.md)。

---

[← 上一页](design.md) · [文档首页](README.md) · [下一页 →](contributing.md)
