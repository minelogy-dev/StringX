[English](../en_us/build-and-test.md) · 中文

[文档首页](README.md) › 构建、测试与打包

# 构建、测试与打包

String X 使用 [Forge](https://github.com/minelogy-dev/forge) 构建，这是一个
构建脚本本身就是 C 程序的构建系统。本页介绍工具链要求、每个 `./make` 任务、
测试框架与内存检查器（sanitizer）、安装、pkg-config 以及 Debian 打包。运行这些
命令的 CI 流水线见[持续集成](ci.md)。

## 环境要求

| 依赖 | 说明 |
| --- | --- |
| Linux | CI 使用 Ubuntu 24.04；其他 Linux 发行版同样可用。 |
| C23 编译器 | GCC 14+ 或 Clang 19+。 |
| `libpcre2-dev` | PCRE2 10.46，8 位，正则层与每个测试都需要。运行时库需要 `libpcre2-8-0`。 |
| `pkg-config` | 推荐使用者安装；CI 还用它链接已安装的库。 |
| Forge | 构建工具；从源码引导安装（见下一节）。`./make` 是生成物，不纳入版本库。 |
| `tar` 与 `ar` | 只有 `./make gen_deb` 需要；`dpkg-deb` 仅用于检查产物。 |

所有命令都在仓库根目录执行：`build.c` 与 `build.conf` 都相对于该目录读取。

## Forge 一分钟上手

Forge 把 `build.c` 编译成本地可执行文件 `./make`。这个生成文件就是构建入口：
下文每个任务都是 `build.c` 里的一个函数，以 `./make <任务>` 调用。它不是 GNU
make。只要修改了 `build.c` 或 `build.conf`，就要重新运行 `forge .`。

从源码引导 Forge，并把它的产物加入搜索路径：

```sh
git clone https://github.com/minelogy-dev/forge.git
cd forge
./bootstrap.sh --no-test
export PATH="$PWD/build/output:$PATH"
export CPATH="$PWD/build/output/include"
cd /path/to/StringX
forge .
```

CI 将 Forge 固定到提交
`b6826ba762a1e9e337895e01ee604b943c682d23`。若要精确复现 CI，请把该提交
检出到 `.ci-deps/forge`，并在不安装系统文件的情况下引导它：

```sh
git clone https://github.com/minelogy-dev/forge.git .ci-deps/forge
git -C .ci-deps/forge checkout b6826ba762a1e9e337895e01ee604b943c682d23
sh .ci-deps/forge/bootstrap.sh --no-test
export PATH="$PWD/.ci-deps/forge/build/output:$PATH"
export CPATH="$PWD/.ci-deps/forge/build/output/include"
forge .
```

更换 Forge 版本时，两个工作流文件 `.github/workflows/ci.yml` 与
`.github/workflows/codeql.yml` 中的固定提交必须保持一致。该固定点有意独立于
Forge 的 `main` 分支，也不会被 Dependabot 更新。

把 Forge 加入 `PATH` 后，日常命令与 README 一致：

```sh
forge .        # 把 build.c 编译成 ./make（改动 build.c 后需重跑）
./make         # 构建 -> build/output/libstringx.so（头文件随附于 build/output/include）
./make test    # 编译并运行 tests/ 下每个测试（ASan + UBSan）
```

## 目标与命令

| 命令 | 作用 |
| --- | --- |
| `forge .` | 按 `build.conf` 的设置把 `build.c` 编译成本地 `./make`。 |
| `./make` | 默认的 `build` 任务：把 `src/` 编译成 `build/output/libstringx.so`，并把 `include/*.h` 同步到 `build/output/include/`。 |
| `./make check` | 用警告选项和 GCC 分析器编译库目标，但不做头文件同步。 |
| `./make test` | 在 ASan + UBSan + LSan 下构建并运行 `tests/` 下的每个文件。 |
| `./make test -j 2` | 同上，并并行编译每个测试的源文件（这里是 2 路并行）。 |
| `./make install <prefix>` | 先构建，再安装库、头文件、声明文件和 `libstringx.pc`；前缀默认为 `/usr/local`。 |
| `./make gen_deb` | 先构建，再暂存并生成 `build/deb/libstringx_<版本>_<架构>.deb`。 |

`-v` 会让 `./make test` 与 `./make check` 打印完整的编译命令；不加时只显示
`PASSED`/`FAILED` 行和最终汇总。`-j N` 并行执行一个目标的编译阶段。

产物位置如下：

| 路径 | 由谁生成 | 内容 |
| --- | --- | --- |
| `build/output/libstringx.so` | `./make`、`./make check`、`./make install`、`./make gen_deb` | 共享库 |
| `build/output/include/` | `./make`、`./make install`、`./make gen_deb` | `sx.h`、`sx_core.h`、`sx_raw.h` 的同步副本 |
| `build/exe-obj/` | `./make`、`./make check` | 各源文件的目标文件及其 `.d`/`.meta` 增量信息 |
| `test/<名称>` | `./make test` | 每个测试文件一个可执行文件（`tests/new.c` → `test/new`） |
| `build/opt/` | `./make test` | 测试目标文件及其 `.d`/`.meta` 文件 |
| `build/deb/` | `./make gen_deb` | Debian 暂存目录与最终 `.deb` |

`./make test` 不需要先运行 `./make`：每个测试都直接编译库源码。

## build.conf

`build.conf` 在 Forge 生成 `./make` 时读取；每个 `KEY=VALUE` 行都会变成构建脚本
的 `-DCONF_KEY=VALUE` 宏。以 `#` 开头的行是注释。修改后需要重新运行 `forge .`。

| 键 | 取值 | 作用 |
| --- | --- | --- |
| `compiler` | `GCC` 或 `CLANG` | 工具链后端。`build.c` 把 `CONF_compiler` 传给 `set_toolchain`，因此它同时决定库与测试使用的编译器。 |
| `compiler_path` | 绝对路径，例如 `/usr/bin/gcc` | Forge 编译生成的 `./make` 时使用的编译器二进制文件。 |
| `version` | `v0.5` | 项目版本的唯一来源。`build.c` 把它字符串化为 `SX_VERSION`；安装与打包会去掉开头的 `v`。 |
| `sanitize` | `1` | CI 中由 `.github/scripts/configure.sh` 追加。内存检查器选项本身由每个测试在头部 DSL 中声明。 |

## 运行测试

`./make test` 递归扫描 `tests/`，把每个 `tests/*.c` 编译成 `test/<名称>` 并
运行。测试成功时打印 `PASSED: <文件>`，第一个断言失败时打印
`FAILED: <文件>:<行号>: <表达式>`。任务结束时输出类似下面的汇总：

```text
forge: 20 tests, 20 passed, 0 failed
```

只要有测试编译或运行失败，任务就以非零状态退出。

测试不链接 `libstringx.so`：每个测试直接编译库源码，并在自己的头部声明选项，
包括 `-fsanitize=address,undefined` 与 `-fno-omit-frame-pointer`。LeakSanitizer
与 ASan 一起运行，因此测试若忘记释放缓冲区或返回的数组就会失败。

请设置与 CI 相同的内存检查器选项：

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

单独重跑某个已构建的测试：

```sh
./test/reserve_stress
```

### 精确预留压力测试

`tests/reserve_stress.c` 是 `-DSX_FORCE_EXACT_RESERVE` 的专用配置，该宏在其
头部以 `// Option -DSX_FORCE_EXACT_RESERVE` 声明。这个开关把
`src/sx_raw.c` 内部的预留辅助函数从几何增长改为精确分配
（`cap == need + 1`），于是分配正好在结尾符处结束，任何越过 `len` 的访问都会
立刻命中 ASan 的红区——多余的容量无法掩盖差一错误或过期的长度。测试用确定性
的 LCG 驱动三个活动缓冲区，并在每次修改后断言完整内容；其模型不超过 512 字节。
其余所有测试都在默认的几何增长模式下构建。

## 测试文件与头部 DSL

每个测试都是自包含程序。文件开头连续若干行 `//` 构成 Forge 测试运行器读取的
头部 DSL；遇到第一行非 `//` 的内容时头部结束。

| 条目 | 作用 |
| --- | --- |
| `// Include <目录>` | 添加 `-I<目录>`。每个测试都包含 `include`、`src` 和 `/usr/lib/forge/include` —— 最后一项是 `<build.h>` 所需的 Forge 头文件；若通过 `CPATH` 提供，这一项也无害。 |
| `// Source <文件>` | 把一个源文件加入测试可执行文件。测试都会加入 `src/sx_raw.c`、`src/sx_core.c` 与 `src/sx_adv.c`。 |
| `// Link <库>` | 添加 `-l<库>`。只有 `tests/reg.c` 加入 `pcre2-8`；其他测试不使用正则层。 |
| `// Library <目录>` | 添加 `-L<目录>`。测试加入 `/usr/local/lib`。 |
| `// Option <选项>` | 添加编译器、链接器与汇编器选项。测试使用 `-std=c23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`，以及诸如 `-DSX_FORCE_EXACT_RESERVE` 的测试专用宏。 |

不带关键字的行会延续当前作用域（初始为 `Source`）；测试为了清晰都写出了每个
关键字。

由于每个测试都会编译 `src/sx_adv.c`，`build.c` 通过其保留的 `LANGUAGE`
覆盖项为每个测试目标附加 `pcre2-8`；`tests/reg.c` 还显式声明了
`// Link pcre2-8`。

程序本身包含被测头文件（`"sx.h"`，仅测核心层时用 `"sx_core.h"`）以及提供两个
测试宏的 `<build.h>`：

| 宏 | 作用 |
| --- | --- |
| `TEST(表达式)` | 失败时打印 `FAILED: <文件>:<行号>: <表达式>` 并以 1 退出；成功时不输出并继续。 |
| `PASS()` | 打印 `PASSED: <文件>`；在 `main` 末尾调用。 |

### 添加测试

1. 创建 `tests/<名称>.c` —— 每个文件只覆盖一个行为领域。
2. 从现有测试复制头部 DSL 块，并按所需依赖调整 `Source`、`Link` 与 `Option`。
3. 在文件顶部写明行为边界：测试锁定了什么（`In scope (contract)`），以及
   有意不覆盖什么（`Out of contract (not exercised)`）。
4. 包含被测头文件，用 `TEST` 断言，释放每个缓冲区与返回的数组，最后以
   `PASS()` 结束。
5. 运行 `./make test`。新文件会被自动发现。

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

`./make install` 会先构建（包括头文件同步），然后复制：

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

默认前缀为 `/usr/local`。可以传入一个无需 root 即可写入的前缀，例如：

```sh
./make install "$HOME/.local"
```

pkg-config 文件在安装时生成。它的 `prefix=` 就是安装前缀，`Version` 是
`build.conf` 版本去掉开头的 `v`（即 `0.5`）。

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

对于非标准前缀，让 pkg-config 与运行期加载器指向它，或者写入 rpath：

```sh
export PKG_CONFIG_PATH="<prefix>/lib/pkgconfig"
cc -std=c23 hello.c $(pkg-config --cflags --libs libstringx) \
  -Wl,-rpath,"<prefix>/lib" -o hello
```

库自身已链接 `pcre2-8`，使用者无需 `-lpcre2-8`。完整程序如下：

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

`./make gen_deb` 会先构建，然后用 `tar` 与 `ar` 暂存并打包出 Debian `.deb`
（不依赖 `dpkg-deb`）：

| 路径 | 内容 |
| --- | --- |
| `build/deb/data/usr/` | 位于 FHS 根下的包数据树：`lib/`、`include/` 与 `share/doc/libstringx/` |
| `build/deb/control-dir/control` | 包元数据 |
| `build/deb/debian-binary`、`control.tar.gz`、`data.tar.gz` | 包的各组成部分 |
| `build/deb/libstringx_<版本>_<架构>.deb` | 最终包 |

版本取自 `build.conf` 并去掉开头的 `v`；架构取自 `uname -m`
（`x86_64` → `amd64`，`aarch64` → `arm64`，其他值保持不变）。包装到 `/usr`
下，并声明 `Depends: libc6, libpcre2-8-0`。包内的 `libstringx.pc` 以 `/usr`
作为前缀，暂存路径不会写进去。

检查并安装：

```sh
dpkg-deb --info build/deb/libstringx_*.deb
dpkg-deb --contents build/deb/libstringx_*.deb
sudo apt install ./build/deb/libstringx_*.deb
```

## 故障排查

| 现象 | 原因与处理 |
| --- | --- |
| `forge: command not found` | Forge 不在 `PATH` 中。引导它并导出 `PATH=<forge>/build/output`。 |
| `build.h: No such file or directory` | Forge 的构建脚本头文件不可见。像 CI 那样导出 `CPATH=<forge>/build/output/include`，或系统级安装 Forge。`<build.h>` 只存在于 Forge 构建环境中。 |
| `pcre2.h: No such file or directory` 或 `cannot find -lpcre2-8` | 安装 `libpcre2-dev`（Ubuntu：`sudo apt-get install libpcre2-dev`）。 |
| 克隆后出现 `./make: No such file or directory` | `./make` 是生成物且被 gitignore。先运行 `forge .`。 |
| 修改 `build.c` 或 `build.conf` 后没有生效 | `./make` 由旧脚本生成。重新运行 `forge .`。 |
| Clang 下测试链接因 `-flto` 失败 | 固定的 Forge 在并行 Clang 链接时会输出仅适用于 GCC 的 `-flto=N`，因此 `build.c` 中的测试目标特意关闭 LTO；生产目标保留 LTO。用 Clang 运行测试套件时请保持测试关闭 LTO。 |
| `install` 报告权限错误 | 前缀不可写。传入你有权限的前缀，例如 `./make install "$HOME/.local"`。 |
| 测试因泄漏报告失败 | 有缓冲区或数组未释放。每个 `sx_t` 都要用 `sx_free` 释放，每个返回的数组都要用 `free` 释放。 |
| 使用者程序找不到 `libstringx.so` | 把 `LD_LIBRARY_PATH` 设为 `<prefix>/lib`，用 `-Wl,-rpath,<prefix>/lib` 链接，或安装到标准前缀。 |

不要在库源码或应用程序中包含 `<build.h>`：它是 Forge 的构建脚本 DSL 头文件，
不属于 String X API。应用程序只包含 `<sx.h>`。

## 持续集成

在每次推送与拉取请求上运行这些命令的工作流——构建矩阵、分析器、质量检查、
CodeQL 与合并保护——见[持续集成](ci.md)。

---

[← 上一页](design.md) · [文档首页](README.md) · [下一页 →](contributing.md)
