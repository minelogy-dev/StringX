[English](../en_us/design.md) · 中文

[文档首页](README.md) › 设计与内部实现

# 设计与内部实现

本页面向需要推敲库的保证或进行移植的贡献者：数据布局、算法、所有权模型
以及 `<sx.h>` 契约背后的取舍。契约本身见
[缓冲区与视图 API 参考](api-buffer.md)、[查找 API 参考](api-search.md) 和
[正则表达式](regex.md)。

## 目标与非目标

设计源自一组简短目标：

- **字节精确的契约。** 操作定义在字节上，边界都写明。查找、比较和正则
  按长度工作，内嵌 NUL 是普通数据。
- **零拷贝访问。** 切片是别名到缓冲区的 `sx_view_t` 值；查看子切片不拷贝。
- **每个缓冲区一个稳定句柄。** `sx_t *` 在生命期内标识缓冲区；修改返回
  `self`，即使数组重新分配，调用也能串联。
- **可预测的失败。** 文档化的非法输入产生固定哨兵而非崩溃；其余输入属于
  契约之外。没有异常、错误码或全局错误状态。
- **小接口面。** 一个头文件 `<sx.h>`、一个库、一个由库自身链接的高级依赖
  （PCRE2）。

同样刻意设定的非目标：

- **不提供文本编码层。** 不解码 UTF-8、不做规范化，也不提供字符或字素
  操作。字符串就是字节；正则层运行在 PCRE2 的 8 位非 UTF 模式。
- **不感知 locale。** 比较、裁剪和字符类都按字节与固定 ASCII 集合定义，
  与 `LC_*` 无关。
- **缓冲区不是线程安全的。** 已编译正则不可变且可共享；可变缓冲区是普通
  内存，需要外部同步。
- **不支持自定义分配器、rope/流式结构或惰性视图。** 每个缓冲区都是一块
  连续分配。
- **不自带正则引擎。** 模式语义属于 PCRE2；库只提供生命周期、扫描和替换
  管线。

## 分层与头文件

### 头文件一览

| 头文件 | 层 | 内容 | 受众 |
| --- | --- | --- | --- |
| `include/sx_core.h` | 核心层 | `sx_t`、`sx_view_t`、缓冲区/视图/修改/查找/比较原型 | 公开；被 `<sx.h>` 包含 |
| `include/sx.h` | 高级层 | 包含 `sx_core.h`；声明 `sx_split` 和 `sx_reg_*` | 公开入口 |
| `include/sx_raw.h` | 内部 | 不透明 `_sx` 以及原始分配辅助函数 | 不受支持 |
| `src/_sx.h` | 内部 | `struct _sx` 的定义 | 仅库内部 |
| `src/export.h` | Forge 派生 | `SYMBOL_PUBLIC` 可见性宏（已入库） | 仅库内部 |

使用者包含 `<sx.h>`；`sx_core.h` 公开只是因为 `<sx.h>` 包含它。
`sx_split` 和正则层只在 `sx.h` 中声明。

实现与头文件的分层相对应：

| 翻译单元 | 实现内容 |
| --- | --- |
| `src/sx_core.c` | 缓冲区、视图、修改、查找、比较 |
| `src/sx_raw.c` | 分配原语（`sx__new`、`sx__reserve` 等） |
| `src/sx_adv.c` | `sx_split` 与 PCRE2 封装 |

三个库翻译单元都直接使用私有 `_sx` 布局；客户端代码只消费 `sx.h` 和
`sx_core.h`。

### 为什么 sx_split 位于高级层

`sx_split` 在 `sx.h` 中、包含 `sx_core.h` 之后声明，实现在
`src/sx_adv.c`；位置是刻意的：

- 它返回 `malloc` 分配的视图数组，迭代约定与 `sx_reg_match_all`/
  `sx_reg_split` 相同（token 按序、`.ref == NULL` 结束哨兵、调用方
  `free`）。把返回数组的视图 API 放在一起，让所有权约定集中一处，而不必
  给核心层增加分配数组的 API。
- 扫描自包含：核心的 KMP 原语是 `sx_core.c` 内的 `static` 实现，高级层
  无法调用；分割扫描本来也被输出规模约束。源码注释直说这一取舍：
  `O(n*m)` 字节精确扫描"对高级层足够，并让该翻译单元保持独立"。
- 它不依赖 PCRE2：只是普通分隔符查找，按 API 形态（而非依赖）归入高级
  翻译单元。

### 为什么 sx_raw.h 是内部头文件

`include/sx_raw.h` 暴露私有分配层：

```c
typedef struct _sx _sx;

_sx *sx__new(size_t cap);
_sx *sx__dup(const _sx *src);
int sx__reserve(_sx *s, size_t need);
void sx__free(_sx *s);
```

内部原因有三：

- 它交出 `_sx`——`sx_core.h` 只做前置声明的私有缓冲区结构；
- 函数使用保留的双下划线前缀，并绕开公开错误模型（`sx__reserve` 返回
  `int`，而非哨兵 `sx_t *`）；它们服务于库自身翻译单元，不是客户端；
- 它不提供稳定性承诺。安装后的头文件集合包含它，是因为构建会复制整个
  `include/*.h`，但它不属于受支持 API，客户端不得使用。四个 `sx__*`
  辅助函数声明为 `SYMBOL_PUBLIC`，因此会出现在共享库动态符号表中；导出
  不代表受支持。

## 缓冲区布局与不变式

公开的 `sx_t` 是一个只有单个字段的句柄，指向私有的 `_sx`：

```text
sx_t (public handle)                _sx (private)
+------------+                +------------+
| ref        | -------------> | ptr        | ----> one allocation of cap bytes
+------------+                | len        |       [ H e l l o \0 ? ? ... ]
                              | cap        |
                              +------------+

content:      bytes [0, len)        terminator: ptr[len] == '\0'
capacity:     bytes [0, cap)        invariant:  len + 1 <= cap, cap >= 1
```

视图是一个值，而不是一次分配：

```text
sx_view_t v = { ref = h->ref, offset = 2, len = 3 }

buffer:  a  b  c  d  e  f  \0        view covers  c  d  e
               ^        ^
          v.offset   v.offset + v.len
```

每个函数都保持以下不变式：

1. 对每个存活缓冲区都有 `ptr != NULL` 且 `cap >= 1`。
2. `len + 1 <= cap`：留出结尾符位置。
3. 始终有 `ptr[len] == '\0'`。内容可含 `'\0'`；结尾符是内容之后的第一个
   NUL。
4. `len` 计内容字节数，不含结尾符；`sx_len` 以 `int` 返回（见
   [整数访问器](#整数访问器与-size_max)）。
5. 库返回的视图满足 `ref = buffer.ref` 且 `offset + len <= buffer len`。
   API 不校验调用方构造的视图；越出缓冲区的视图属于契约之外。
6. `sx_t *` 句柄地址在生命期内稳定；`sx_buf_mut` 返回的 `ptr` 在增长时
   **不**稳定，因为 `realloc` 可能移动它。
7. `[len, cap)` 的字节未指定（陈旧或未初始化）；只有 `ptr[len]` 属于
   契约。

不变式由结构保证：每个修改函数结尾都执行 `ptr[len] = '\0'`，
`sx__reserve` 是唯一改变 `cap` 或 `ptr` 的地方。`sx_buf_mut` 可重写
`[0, len)` 而不改变 `len`；结尾符在该区间之外，不变式仍成立。

## 分配与增长

### 初始容量

每个缓冲区都是 `sx__new(cap)` 的一次分配：`_sx` 头加 `cap` 字节数组
（最少 1，让 `ptr[0]` 有落脚处）。各构造函数的 `cap`：

| 构造函数 | 初始 `cap` | 最大内容字节数 |
| --- | --- | --- |
| `sx_new()` | 32 | 31 |
| `sx_new_with_cap(n)` | `n`；`n == 0` 时为 1 | `n - 1`（`n <= 1` 时为 0） |
| `sx_new_from_cstr(s)` | `strlen(s) + 1` | `strlen(s)` |
| `sx_new_from_view(v)` | `v.len + 1` | `v.len` |
| `sx_dup(src)` | `src->len + 1` | `src->len` |
| `sx_substr(h, idx, n)` | `n + 1` | `n` |

`sx_new()` 经 `sx_new_with_cap(32)` 实现：32 字节起点让最初几次追加不
分配，也不浪费太多内存。从内容构造的构造函数精确分配 `内容长度 + 1`，
新建或拷贝的缓冲区容量是紧的。

### 增长策略

所有增长都经过 `sx__reserve(_sx *s, size_t need)`，`need` 是此后必须
容纳的**内容字节数**。内部必须得到 `cap > need`，即为 `need` 字节加结尾符
留空间：

| 情况 | 新的 `cap` |
| --- | --- |
| `cap > need` | 不变（空操作） |
| `need == SIZE_MAX` | 拒绝（整数溢出保护） |
| 默认，`cap < 0x10000` | `max(cap * 2, need + 1)` |
| 默认，`cap >= 0x10000` | `max(cap + 0x10000, need + 1)` |
| `-DSX_FORCE_EXACT_RESERVE` | `need + 1` |

默认策略在分配小于 64 KiB 时翻倍，此后固定加 `+0x10000`。翻倍让每个
追加字节的代价摊还为常数；固定步进避免仅为追加几个字节就把大块分配
（例如 8 MiB）翻到 16 MiB。与 `need + 1` 取 `max` 覆盖单次调用超过下一个
几何步长的情况。

公开的 `sx_reserve(self, n)` 原样转发 `n`，保证 `cap >= n + 1`：调用后
缓冲区总共可容纳 `n` 个内容字节，无需再次重新分配。`sx_append`、
`sx_write`、`sx_insert` 在拷贝前调用 `sx__reserve(最终内容长度)`：

- append：`need = len + 追加长度`；
- write：`need = idx + 写入长度`；此后的 `len` 为 `max(len, idx + n)`；
- insert：`need = len + 插入长度`；尾部用 `memmove` 后移。

增长单调；收缩从不重新分配：`sx_truncate` 只在上调时预留，`sx_erase`
原地移动字节，`sx_clear` 只重写 `ptr[0]`。精确预留测试对这两个性质都有
断言（`tests/reserve_stress.c`）。

### SX_FORCE_EXACT_RESERVE

`-DSX_FORCE_EXACT_RESERVE` 用 `cap = need + 1` 取代增长策略。分配正好
结束在结尾符处，越过 `len` 的访问会立刻落入 AddressSanitizer 红区，而非
藏在多余容量后。代价是每次增长都重新分配（逐字节追加在拷贝次数上接近
平方级）且没有余量，因此是调试/测试模式。`tests/reserve_stress.c` 是
专用配置：每个缓冲区从 `cap == 1` 开始，固定 LCG 驱动 1500 次伪随机修改，
与字节精确模型对照，并在每次重新分配后断言 `cap == len + 1`、收缩从不
重新分配。

## 视图与别名

视图是核心抽象：`sx_view_t` 保存缓冲区引用加 `(offset, len)` 窗口。返回
视图从不拷贝、分配或修改缓冲区。同一表示服务于整缓冲区视图（`sx_view`）、
视图移动（`sx_view_advance`、`sx_view_range` 等）、分割 token 和正则
匹配数组。

调用方必须遵守的规则：

- **视图是快照。** 修改不调整已捕获视图；长度变化后应重新派生。
- **增长使裸指针失效。** `sx_buf_mut`/`sx_buf_view_mut` 借用数组，之后
  使缓冲区增长的修改可能 `realloc` 它。`sx_t *` 句柄稳定，指针不稳定。
- **视图不得比缓冲区活得久。** `ref` 是裸 `_sx *`；释放缓冲区即释放
  被引用对象。返回视图数组不转移缓冲区所有权。
- **视图使用期间不要修改**——`sx_split` token、`sx_reg_match_all` 数组
  和用户视图同理。
- **`NULL` 视图表示"无"。** `{ref = NULL, offset = 0, len = 0}` 是哨兵；
  其他库返回视图指向存活缓冲区。

别名修改需小心。源视图与目标指向同一缓冲区时，`sx_append_view`、
`sx_write_view` 和 `sx_insert_view` 用 `memmove` 而非 `memcpy`。
append/write 是普通重叠安全移动，任何契约内源视图都可用。`sx_insert_view`
更窄：

1. 先为**最终**内容长度预留空间（保持源指针有效：`realloc` 原地更新
   `_sx.ptr`，之后取得的指针看到新分配）；
2. 尾部右移 `v.len` 字节，写入 `[idx + n, len + n)`；
3. 在 `idx` 处用 `memmove` 写入插入字节。

尾部移动使自别名有条件：起点在插入点或之前的源窗口
（`v.offset <= idx`）结束于 `idx + n` 或更早，移动不会覆盖它，第 3 步的
`memmove` 处理剩余重叠。被测试覆盖的是 `v.offset == idx`：把视图
`[2, 4)`（`"cd"`）插入 `"abcdef"` 的索引 2，得到 `"abcdcdef"`。起点
**在 `idx` 之后**的窗口在被读取前就被覆盖：把 `[3, 5)`（`"de"`）插入
`"abcdef"` 的索引 2，得到 `"abdccdef"` 而非 `"abdecdef"`；该调用属于
契约之外。`tests/reserve_stress.c` 末尾的回归用例覆盖强制增长下的
`v.offset == idx`，锁定"先预留、后取指针"的顺序。带 `owned` 参数的变体
（`sx_append(self, self)` 等）会释放自己的参数，自别名属于契约之外。

## 所有权与错误模型

所有权刻意保持显式且小：谁收到拥有所有权的指针，谁负责释放。

| 操作 | 所有权 |
| --- | --- |
| `sx_new*`、`sx_dup`、`sx_substr`、`sx_reg_replace*` | 返回调用方拥有的 `sx_t *`；用 `sx_free` 释放 |
| `sx_append`、`sx_write`、`sx_insert`（`owned` 参数） | 消费该参数：由库释放 |
| `sx_split`、`sx_find_all*`、`sx_reg_match_all`、`sx_reg_split` | 返回 `malloc` 分配的数组；用 `free` 释放 |
| 匹配视图、token、整缓冲区视图 | 借用；无需释放 |

需要注意的细节：

- **消费型调用在失败时也会消费。** `sx_append`/`sx_write`/`sx_insert`
  先尝试修改，再无条件释放 `owned`；修改失败时调用返回 `NULL`，但参数
  仍被消费。参数为 `NULL` 时返回 `NULL`，不释放任何东西。
- **写入范围拒绝是文档化行为。** 若 `idx + n` 使 `size_t` 溢出，或结果
  末端无法预留（内容为 `SIZE_MAX` 时放不下结尾符），`sx_write*` 在拷贝前
  返回 `NULL`，`self` 保持不变。`sx_write_view` 在保留别名的预预留之前
  检查，因此被拒绝的范围不会改变容量。`tests/write_overflow.c` 与其精确
  预留兄弟测试锁定该契约。
- **句柄同一性。** 每次修改都返回 `self`，成功修改可以串联：
  `sx_append_cstr(sx_append_cstr(s, "a"), "b") == s`。句柄在增长前后稳定；
  只有数组移动。
- **数组不拥有元素所指的缓冲区。** 释放 `sx_split`/`sx_reg_match_all`
  数组不会释放主体；视图只是变得不可达。

错误模型基于哨兵值：没有 `errno`、错误对象或错误返回码。

| 情况 | 哨兵值 |
| --- | --- |
| 偏移查找（`sx_find*`、`sx_rfind*`） | `(size_t)-1` |
| "无出现"（`sx_find_all*`） | `NULL` 数组 |
| "无匹配"视图（`sx_reg_match`） | `{ref = NULL, offset = 0, len = 0}` |
| "无匹配"数组（`sx_reg_match_all`） | `NULL` |
| 非法/空分隔符、非法正则、数组/字符串工厂分配失败 | `NULL` |
| 写入范围溢出或末端无法预留 | `NULL`；`self` 不变（`owned` 输入被消耗） |
| 布尔谓词与 `sx_reg_is_match` | `0` |

两个后果是有意为之，并且都有文档：

- **"无结果"与"内存不足"可能无法区分。** `find_all` 和 `sx_reg_*`
  数组家族对两者都返回 `NULL`，因为没有第三条通道。替换能区分：无匹配
  返回原样拷贝，`NULL` 表示非法输入或失败。
- **`NULL` 句柄参数大多属于契约之外。** 显式检查的家族——`sx_new_from_cstr`、
  `sx_dup`、cstr/owned 修改参数、`sx_find*`、`sx_rfind*`、
  `sx_find_all*`、`sx_cmp*`/`sx_starts_with*`/`sx_ends_with*`、
  `sx_split`、`sx_reg_*`——有定义的 `NULL` 行为。访问器（`sx_len`、
  `sx_cap`、`sx_buf_mut`、`sx_view` 等）、视图移动函数和 `sx_free` 会
  解引用句柄、不接受 `NULL`；头文件按函数写明边界。

错误不会留下半成品缓冲区：失败的预留在任何拷贝前发生，`self` 保持原内容
与单 NUL 不变式。唯一例外是上述被消费的 `owned` 参数。

## 查找内部实现

整个查找家族建立在 `src/sx_core.c` 中唯一的 `static` 原语之上：

```c
static inline find_ret_t find(const sx_t *h, const char *needle, int len,
                              size_t boundary, int opt);
```

返回类型可能是位置或指向结果数组的指针，因此实现选择宽度足以往返指针的
整数类型：

```c
#if SIZE_MAX > UINTPTR_MAX
typedef size_t find_ret_t;
#else
typedef uintptr_t find_ret_t;
#endif
```

### 区域模型

`opt` 选择方向和结果形态：

- 第 0 位：`1` = 向右查找，`0` = 向左查找；
- 第 1 位：`1` = 单个结果（标量），`0` = 全部结果（数组）。

查找区域由调用方的 boundary 计算：

```text
left search  (sx_find*):    region = [boundary, H)
right search (sx_rfind*):   region = [0, min(boundary, H))

H = content length of h
a match window [p, p+m) counts only if p >= region_start and p+m <= region_end
```

各封装填入文档化边界：`sx_find*` 用 `boundary = 0`，`sx_rfind*` 用
`boundary = len(h)` 并带向右标志，有界变体 `sx_find_view_b`/
`sx_rfind_view_b` 传调用方 boundary。`len <= 0`、空区域或短于 needle 的
区域报告"未找到"——标量封装返回 `(size_t)-1`，数组封装在内部返回计数 0
的数组（再由 `sx_find_all*` 转成 `NULL`）。

`len` 是 needle 内容长度（`sx_t` needle）、`v.len`（视图 needle）或
`(int)strlen(s)`（cstr needle——窄化转换是一个实际 `int` 限制）。
needle 字节直接从视图或缓冲区读取；查找是字节精确的，类似 `memcmp`。

### 单字节 needle：memchr 与 memrchr

当 `m == 1` 时，原语使用 C 库的向量化扫描：

- **最左：** `memchr(t + l, c, er - l)`；
- **最右：** glibc 上用 `memrchr(t, c, er)`。它是 GNU 扩展而非 ISO C，
  因此 `sx_core.c` 手动声明，并只在定义了 `__GLIBC__` 且未定义
  `SX_FORCE_NO_MEMRCHR` 时使用。回退是逐字节反向循环，在到达 `l` 前保留
  最后一个（最右）命中。

最右快路径可从 `0` 搜索，因为向右查找总设置 `l = 0`；`er` 已携带
boundary。`SX_FORCE_NO_MEMRCHR` 用于在 glibc 上构建并测试可移植路径。

`m == 1` 的"全部"模式从上一个命中 + 1 处反复调用 `memchr`，因此报告
**重叠**位置，正是 `sx_find_all*` 承诺的行为（`"aa"` 在 `"aaaa"` 中报告
0、1、2）。

### 最左原生快路径

最左单结果调用（`sx_find`、`sx_find_view`、`sx_find_view_b`、
`sx_find_cstr`）经过 `find_first()`；在 glibc 上它可能先走原生路径，而不
进入可移植的 KMP 实现。`try_native_find_first` 标记为 `noinline`，其分支
不会改变回退实现的优化：

1. 拒绝空/负长度 needle 或放不下一个窗口的区域；
2. `m == 1`：一次 `memchr`；
3. 先比较区域起点处的窗口（首段命中无需预处理）；
4. 从下一个偏移用 `memchr` 找 needle 首字节，再用 `memchr` 找末字节，
   把候选起点收窄到窗口可能结束的位置；
5. 对 `m <= 32` 的 needle 在剩余区域调用 `memmem`；否则放弃。

放弃时回退到 KMP。原生路径不分配内存，只有回退的前缀表会分配。非 glibc
目标或定义 `-DSX_FORCE_NO_MEMMEM` 时该路径不编译；`tests/find_differential_fallback.c`
让相同差分用例走强制可移植构建。

### 更长的 needle：KMP

当 `m >= 2` 时，回退路径（以及 `sx_rfind*` 与 `sx_find_all*` 使用的
原语）计算 KMP 前缀表（`pi`，一次 `malloc(m * sizeof(size_t))`），并在
`O(n + m)` 时间内扫描区域：

- **最左优先与全部模式**在 `[l, er)` 上正向 KMP。完整匹配时单结果模式
  立即返回；全部模式记录起点并以 `q = pi[m - 1]` 继续，因此报告重叠出现。
- **最右优先**用反向 KMP：以索引映射在*反向* needle 上计算同一前缀表
  （不拷贝），从 `er - 1` 向下扫描到 `l`。已匹配后缀达到 `m` 时，当前
  索引就是窗口起点。

全部模式数组从容量 16 开始，带溢出保护翻倍（`cap > SIZE_MAX / 2 /
sizeof(*r)` 时以 `NULL` 中止），返回前用 `realloc` 收缩一次。布局为
`{count, pos0, pos1, ...}`：`arr[0]` 是计数，不是位置。

### 复杂度

| 情况 | 技术 | 时间 | 额外内存 |
| --- | --- | --- | --- |
| 单字节 needle，最左单结果 | `memchr` | `O(n)` | 无 |
| 单字节 needle，最右单结果 | `memrchr` 或反向扫描 | `O(n)` | 无 |
| needle 长度 >= 2，最左单结果（原生路径） | 字节检查 + `memmem`（`m <= 32`） | 典型 `O(n + m)` | 无 |
| needle 长度 >= 2，最左单结果（回退） | KMP | `O(n + m)` | `O(m)` 前缀表 |
| needle 长度 >= 2，最右单结果 | 反向 KMP | `O(n + m)` | `O(m)` 前缀表 |
| `sx_find_all*`，单字节 | 反复 `memchr` | `O(n)` 扫描 | `O(k)` 结果 |
| `sx_find_all*`，更长 needle | KMP | `O(n + m)` 扫描 | `O(m) + O(k)` 结果 |
| `sx_split` | 两遍 `is_sep` | 最坏 `O(H * m)` | `O(tokens)` 结果 |

`n` 是区域长度，`m` 是 needle 长度，`k` 是匹配数。数组封装只分配结果。
`sx_find*` 在原生路径上不分配；KMP 回退（以及所有多字节 `sx_rfind*`）
会分配一张临时前缀表，并在返回前释放。

## 正则封装内部实现

高级正则层刻意保持很薄。它的全部状态就是一个已编译的 PCRE2 代码对象：

```c
typedef struct sx_reg_t {
  pcre2_code *code;
} sx_reg_t;
```

### 编译与释放

`sx_reg_compile` 调用
`pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, 0, ...)`。`0` 是选项字
（没有 `CASELESS`、没有 `UTF`），`PCRE2_ZERO_TERMINATED` 表示 C 字符串
模式。PCRE2 错误码和位置只为满足签名而接收并丢弃，因此所有编译失败都
归结为 `NULL`。编译分配代码对象和小 `sx_reg_t`；`sx_reg_free` 释放两者，
接受 `NULL`。

### 每次调用的匹配数据与扫描规则

PCRE2 把已编译模式与每次调用的匹配数据分开。封装用
`pcre2_match_data_create_from_pattern(r->code, NULL)` 创建匹配数据并在
返回前释放，句柄不保存匹配状态，因此可跨线程共享。

单步匹配是 `pcre2_match(code, subject, len, offset, 0, md, NULL)`：主体
以显式**长度**传入而非 C 字符串，`NULL` 匹配上下文意味适用 PCRE2 默认
匹配限制。负返回值（无匹配，或匹配限制之类匹配期错误）表示"没有更多
匹配"；不暴露匹配期错误码。

`sx_reg_match_all` 和 `sx_reg_split` 共用的扫描推进逻辑是：

```c
static size_t reg_next_at(size_t start, size_t end) {
  return end > start ? end : start + 1;
}
```

下一次搜索从匹配结尾开始；零长匹配则从起点后一字节开始，因此扫描保证
前进。每步用 `offset` 调用 `pcre2_match`；越过主体长度时 PCRE2 报错、
循环结束。因此 `a*` 对 `"bbb"` 报告偏移 0–3 的空匹配，`$` 对 `"abc"`
报告偏移 3 的一个空匹配。

### match_all 与 split 的两遍定长策略

两个数组家族都扫描两遍：

```text
pass 1: count matches             -> cnt
malloc((cnt + 1) * sizeof view)   -> exact size, no realloc
pass 2: repeat the same scan      -> fill views, then arr[cnt] = {0} guard
```

扫描是确定性的（相同模式、主体和起始偏移），两遍看到相同位置。
`sx_reg_split` 还跟踪空隙起点 `seg`：每个 token 是 `[seg, match_start)`，
`seg` 移到 `match_end`；最后一个 token 是 `[seg, len)`。这正好产生
`匹配数 + 1` 个 token，包括空的首尾 token 和相邻或零长匹配产生的空 token。

### 替换：两次 pcre2_substitute 调用

替换由 PCRE2 的替换例程构建，并固定两个标志：

```c
options |= PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_UNKNOWN_UNSET;
```

`UNSET_EMPTY` 让未参与匹配的组展开为空；`UNKNOWN_UNSET` 让不存在的组号
展开为空，而不是让替换失败。`sx_reg_replace` 额外传
`PCRE2_SUBSTITUTE_GLOBAL`；`sx_reg_replace_first` 不传。

由于事先不知道输出长度，封装替换两次：

```text
call 1: output buffer NULL, PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, &needed
        -> PCRE2_ERROR_NOMEMORY, `needed` = required size including NUL
malloc(needed)                                       (single temporary)
call 2: fill the buffer, &used = needed
        -> rc >= 0, `used` = bytes written excluding NUL
sx_new_with_cap(used + 1)                            (new public buffer)
memcpy(ptr, buf, used); len = used; ptr[used] = '\0'
```

拷贝后释放临时缓冲区。第一次调用未返回 `PCRE2_ERROR_NOMEMORY` 说明
模板/替换对被拒绝；封装返回 `NULL` 且不改写主体。无匹配时 PCRE2 不替换
任何内容，输出是主体的原样拷贝——正常成功，不是错误。

### 分配一览

| 调用 | 结果分配 | 说明 |
| --- | --- | --- |
| `sx_reg_compile` | PCRE2 代码对象 + `sx_reg_t` | 一次性成本 |
| `sx_reg_free` | 无 | 释放编译时的两次分配 |
| `sx_reg_is_match` | 每次调用的匹配数据 | 不分配结果 |
| `sx_reg_match` | 每次调用的匹配数据 | 结果是视图值 |
| `sx_reg_match_all` | 匹配数据 + 精确的 `(cnt + 1)` 数组 | 两遍扫描 |
| `sx_reg_split` | 匹配数据 + 精确的 `(matches + 2)` 数组 | 两遍扫描 |
| `sx_reg_replace*` | PCRE2 内部匹配数据 + 临时缓冲区 + `sx_t` | 两次替换调用 |

封装不启用 PCRE2 JIT（从不调用 `pcre2_jit_compile`），也不暴露编译或
匹配选项；模式级特性（例如内联选项）仍生效，因为它们属于模式而非封装
策略。

## 并发

- **已编译正则句柄不可变且可共享。** 每次调用的状态在调用内部创建、归
  调用方所有的匹配数据中；封装没有全局变量，一个 `sx_reg_t *` 可服务多个
  线程。
- **缓冲区没有同步。** 它们是普通堆内存：无人修改时并发读安全；修改需要
  外部同步。视图更严格——与改变长度的修改并发读视图是数据竞争
  （契约之外）。
- **所有权需要协调。** 单一所有者的缓冲区不能被两个线程同时消费；数组是
  单一所有者的结果。

库从不修改全局状态，因此只要调用方同步每个缓冲区的访问，多线程程序中
使用就是安全的。

## 可移植性与限制

### memrchr、memmem 与可移植开关

查找路径使用两个 glibc 扩展：`memrchr`（最右单字节扫描）和 `memmem`
（最左原生快路径）。`sx_core.c` 手动声明两者，并只在定义了 `__GLIBC__`
时编译进去。`-DSX_FORCE_NO_MEMRCHR` 与 `-DSX_FORCE_NO_MEMMEM` 强制使用
可移植路径；仓库测试除 glibc 路径外，还运行强制可移植的差分查找测试，
因此两条回退路径都能在 glibc 上构建并检查。

### 整数访问器与 SIZE_MAX

公开访问器返回 `int`，而偏移和 needle 使用 `size_t`：

| 函数 | 返回类型 |
| --- | --- |
| `sx_len`、`sx_len_view` | `int`（内容字节数） |
| `sx_cap` | `int`（总分配量） |
| 偏移（`sx_find*`、视图字段、正则视图） | `size_t` |

内容字节数超过 `INT_MAX` 的缓冲区在测试契约之外。这是面向中小字符串的
有意简化，不是缺陷；实际上限约 2 GiB。同样的 `int` 窄化出现在查找封装把
基于 `strlen` 的 needle 长度转换为 `int` 的地方。

`SIZE_MAX` 保护出现在尺寸运算可能回绕的地方：

- `sx__reserve` 拒绝 `need == SIZE_MAX`（否则无法再加结尾符）；
- `sx_find_all*` 数组增长在翻倍前检查 `cap > SIZE_MAX / 2 / sizeof(*r)`；
- 没有通用溢出检查：极端 `n` 或 `off` 可能让无符号尺寸运算回绕，这类
  调用在测试契约之外。

### 平台假设

- 库使用 C23，参考工具链为 Linux 上的 GCC 14+ 与 Clang 19+（CI：
  Ubuntu 24.04、GCC 14、Clang 19）。除 `SYMBOL_PUBLIC` 和 glibc
  `memrchr` 外，源码不依赖编译器特定扩展。
- `src/export.h` 是已入库的 Forge 派生头文件，把 `SYMBOL_PUBLIC` 定义为
  GCC/Clang 的 `__attribute__((visibility("default")))` 与 MSVC 的
  `_declspec(dllexport)`。构建使用隐藏默认可见性，因此恰好
  `SYMBOL_PUBLIC` 函数从 `libstringx.so` 导出。
- 正则层需要 PCRE2 10.46（`pcre2-8`，8 位、非 UTF）。构建把它链接进
  `libstringx`，使用者无需添加 PCRE2 链接参数，但它仍是共享库的运行时
  依赖。
- 安装与 Debian 打包目标由 `build.c` 中的 `LINUX` 宏保护；库本身按普通
  C23 编译。

## 测试策略

`tests/` 下每个文件都是自包含程序。文件头注释声明构建方式（由固定版本
构建系统消费的 `// Include`、`// Source`、`// Option`、`// Link`、
`// Library` 行），并记录**行为边界**："In scope (contract)"列表和
"Out of contract (not exercised)"列表。文件头也是契约的一部分：行为变化
必须同步更新边界列表（见[贡献指南](contributing.md)）。

测试套件（24 个）按主题一文件：

| 测试 | 锁定的内容 |
| --- | --- |
| `access.c` | `sx_len` / `sx_cap` / 缓冲区访问器 / 整缓冲区视图 |
| `append.c` | append 家族、所有权转移、别名追加 |
| `chain.c` | 修改串联、句柄同一性、零泄漏所有权 |
| `cmp.c` | 按字节的字典序比较 |
| `ends_with.c` | 后缀谓词 |
| `find.c` | 最左查找、有界区域、`strlen` needle |
| `find_all.c` | 全部出现，包含重叠位置 |
| `find_differential.c` | 最左查找对照独立窗口 oracle（原生路径） |
| `find_differential_fallback.c` | 相同用例，使用 `-DSX_FORCE_NO_MEMMEM`（可移植 KMP 路径） |
| `insert.c` | 插入、跨增长时的别名 |
| `new.c` | 构造函数、`sx_dup`、容量边界情况 |
| `reg.c` | 正则家族、二进制安全主体、零长匹配处理 |
| `reserve.c` | `sx_reserve` 的单调性与不收缩规则 |
| `reserve_stress.c` | 精确预留的模型化压力测试（1500 次随机修改） |
| `rfind.c` | 最右查找与有界区域 |
| `shrink.c` | truncate / erase / clear 边界情况 |
| `split.c` | 分割形态、结束哨兵、零拷贝 token |
| `starts_with.c` | 前缀谓词 |
| `substr.c` | 双向的拷贝独立性 |
| `view_move.c` | advance / retreat / shift / range 的钳制 |
| `view_trim.c` | 空白裁剪集合与空视图钳制 |
| `write.c` | 覆盖、尾部追加与填充语义 |
| `write_overflow.c` | `SIZE_MAX` 写入范围返回 `NULL`、`self` 不变、消耗 `owned` 输入 |
| `write_overflow_exact.c` | 相同用例，使用 `-DSX_FORCE_EXACT_RESERVE` |

部分测试在普通配置与强制配置之间共享本地用例头（`find_differential.h`、
`write_overflow_cases.h`）；每个 `.c` 仍然是独立目标。

测试如何运行：

- 每个测试用其文件头选项**直接编译库源码**。内存检查器开启：
  AddressSanitizer、UBSan，以及在 Linux 上通过 ASan 启用的
  LeakSanitizer——泄漏检查器是所有所有权规则的见证。CI 设置
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` 和
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。
- `tests/reserve_stress.c` 与 `tests/write_overflow_exact.c` 使用
  `-DSX_FORCE_EXACT_RESERVE` 构建：前者用确定性 LCG 驱动 append、insert、
  erase、write、truncate 和别名视图修改，与字节精确模型对照，包含别名
  `insert_view` 增长回归；后者以精确大小分配重跑溢出用例。
- CI 还运行专项审计工作流：Write safety 在不可变旧实现上复现溢出并比较
  写入成本；Search performance 对比基线；benchmarks 重跑经审计的
  StringX/SDS/bstrlib 基准（见 [ci.md](ci.md)）。
- CI 还把库安装到临时前缀，通过 `pkg-config` 把 `tests/reg.c` 链接到
  **安装后的共享库**（带 rpath）运行，并检查生成的 Debian 包——验证公开
  头文件、导出符号和 PCRE2 链接。
- 没有故障注入，因此不覆盖分配失败分支；预留测试注释指出没有故障注入
  无法表达这种测试。文档化的 OOM 哨兵由代码推得，未在测试中观察。
- `./make check` 目标用 `-Wall -Wextra -Wpedantic -Werror` 编译生产代码；
  GCC 构建还运行 `-fanalyzer`（CI 中用
  `-O1 -fno-analyzer-state-merge`），CodeQL 用 `security-and-quality`
  查询扫描。
- 测试禁用 LTO（`build.c` 中 `set(TARGET_LTO, 1, DISABLE)`），因为
  固定版本的构建工具为并行 Clang 链接发出仅 GCC 支持的 `-flto=N`；生产
  库目标保留 LTO。

## 设计取舍

| 选择 | 收益 | 代价 |
| --- | --- | --- |
| 单 NUL 结尾的单块分配 | `cstr` 互操作便宜，一次 `free`，内容连续 | 一个字节开销；cstr 变体在内嵌 NUL 处截断 |
| `sx_t` 句柄指向私有 `_sx` | 增长时句柄稳定；布局私有 | 多一层间接，并为每个缓冲区分配头结构 |
| `(ref, offset, len)` 视图 | 零拷贝切片；分割和正则结果统一表示 | 生命周期规则；修改后视图过时；增长使裸指针失效 |
| 几何增长 | 追加的摊还常数成本 | 多余容量；峰值内存高于 `len` |
| 精确预留调试模式 | 红区紧贴的分配能暴露差一错误 | 慢；仅供测试 |
| `int` 长度访问器 | 紧凑、简单的 API | 实际约 2 GiB 的缓冲区上限；窄化转换 |
| 哨兵错误模型 | 无全局状态、无错误传递、确定性 | "无结果"与 OOM 可能混淆；没有诊断信息 |
| 数组由调用方释放，句柄由库释放 | 所有权显式、可预测 | 需要知道两个释放函数（`free` 与 `sx_free`） |
| 最左原生快路径 | glibc 上常见查找更快，`m <= 32` 时不分配内存 | 额外的 libc 特定代码，需要强制可移植测试 |
| 受检的写入范围算术 | 算术溢出返回文档化的 `NULL`，而不是破坏内存 | 每次写入多一个分支；`owned` 输入仍被消耗 |
| KMP + `memchr`/`memrchr` | 最坏 `O(n + m)` 有保证，代码简单，单字节路径向量化 | `m >= 2` 时需要前缀表分配；平均速度低于优化过的子串查找 |
| `sx_find_all*` 报告重叠结果 | 完整位置集合；对字节扫描有用 | 调用方必须预期偏移会重叠 |
| 正则非重叠扫描 | 标准 PCRE2 行为；保证前进 | 无法报告重叠的正则匹配 |
| 两遍精确定长 | 一次分配、不靠 realloc 增长、确定性 | 数组 API 的匹配工作量大约翻倍 |
| 薄 PCRE2 封装，无选项/JIT | 接口面极小、完整的 PCRE2 语义、单一依赖 | 没有封装层标志或调优；PCRE2 在运行时链接 |
| 已编译正则不可变且无内部锁 | 句柄可共享且无需同步 | 缓冲区仍需调用方同步 |
| 构建期配置宏 | 增长/memrchr 策略零运行时成本 | 改变策略需要重新构建 |

这些是取舍，不是意外：测试锁定可观察后果，头文件注释写明契约之外的一面。

## 许可

项目源码采用 MIT 许可（见 [LICENSE](../../LICENSE) 与源码 SPDX 标注）。
Forge 派生的构建工具遵循构建脚本例外：`build.conf`、`build.c` 与
`src/export.h` 带有
`GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception` SPDX
标识；库源码本身仍为 MIT。正则层使用 PCRE2（BSD-3-Clause）；见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。

---

[← 上一页](regex.md) · [文档首页](README.md) · [下一页 →](build-and-test.md)
