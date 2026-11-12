[English](../en_us/design.md) · 中文

[文档首页](README.md) › 设计与内部实现

# 设计与内部实现

本页面向贡献者，以及需要推敲库的保证或进行移植的读者。它描述数据布局、
算法、所有权模型，以及 `<sx.h>` 公开契约背后的取舍。契约本身记录在
[缓冲区与视图 API 参考](api-buffer.md)、[查找 API 参考](api-search.md)
和[正则表达式](regex.md)页面中。

## 目标与非目标

整个设计源自一组简短的目标：

- **字节精确的契约。** 每个操作都定义在字节上，其行为边界都写明。查找、
  比较和正则都按长度工作，因此内嵌 NUL 字节是普通数据。
- **零拷贝访问。** 切片是别名到缓冲区的 `sx_view_t` 值；查看或遍历子切片
  不拷贝任何东西。
- **每个缓冲区一个稳定句柄。** 一个 `sx_t *` 在其整个生命周期内标识该
  缓冲区；修改函数返回 `self`，因此即使底层数组被重新分配，调用也能串联
  起来。
- **可预测的失败。** 文档化的非法输入会产生固定哨兵，而不是崩溃；超出
  文档边界的输入属于契约之外。没有异常、错误码或全局错误状态。
- **小接口面。** 一个公开头文件 `<sx.h>`、一个库、一个可选的由库自身链接
  的高级依赖（PCRE2）。

同样刻意设定的非目标：

- **不提供文本编码层。** 库不解码 UTF-8、不做文本规范化，也不提供字符/
  字素操作。字符串就是字节；正则层运行在 PCRE2 的 8 位非 UTF 模式。
- **不感知 locale。** 比较、裁剪和字符类都定义在字节与固定 ASCII 集合上，
  与 `LC_*` 无关。
- **缓冲区不是线程安全的。** 已编译正则是不可变且可共享的；可变缓冲区
  就是普通内存，需要外部同步。
- **不支持自定义分配器、rope/流式结构或惰性视图。** 每个缓冲区都是一块
  连续分配。
- **不自带正则引擎。** 模式语义属于 PCRE2；库只提供生命周期、扫描和替换
  这些管线。

## 分层与头文件

### 头文件一览

| 头文件 | 层 | 内容 | 受众 |
| --- | --- | --- | --- |
| `include/sx_core.h` | 核心层 | `sx_t`、`sx_view_t`、缓冲区/视图/修改/查找/比较原型 | 公开；被 `<sx.h>` 包含 |
| `include/sx.h` | 高级层 | 包含 `sx_core.h`；声明 `sx_split` 和 `sx_reg_*` | 公开入口 |
| `include/sx_raw.h` | 内部 | 不透明 `_sx` 以及原始分配辅助函数 | 不受支持 |
| `src/_sx.h` | 内部 | `struct _sx` 的定义 | 仅库内部 |
| `src/export.h` | Forge 派生 | `SYMBOL_PUBLIC` 可见性宏（已入库） | 仅库内部 |

使用者包含 `<sx.h>`。`sx_core.h` 之所以是公开的，是因为 `<sx.h>` 包含
它，但它不是第二个入口：`sx_split` 和正则层只在 `sx.h` 中声明。

实现与头文件的分层相对应：

| 翻译单元 | 实现内容 |
| --- | --- |
| `src/sx_core.c` | 缓冲区、视图、修改、查找、比较 |
| `src/sx_raw.c` | 分配原语（`sx__new`、`sx__reserve` 等） |
| `src/sx_adv.c` | `sx_split` 与 PCRE2 封装 |

三个库翻译单元都直接使用私有的 `_sx` 布局；客户端代码只消费 `sx.h` 和
`sx_core.h`。

### 为什么 sx_split 位于高级层

`sx_split` 在 `sx.h` 中、包含 `sx_core.h` 之后声明，并在
`src/sx_adv.c` 中实现。这一位置是有意为之：

- 它返回 `malloc` 分配的视图数组，迭代约定与 `sx_reg_match_all`、
  `sx_reg_split` 相同：token 按顺序排列，随后是一个 `.ref == NULL` 的
  结束哨兵，调用方用 `free()` 释放数组。把返回数组的视图 API 放在一起，
  可以让这套所有权约定集中在一处，而不必给核心层增加一个分配数组的 API。
- `sx_split` 的扫描是自包含的。核心的查找原语是 `sx_core.c` 内部
  `static` 的 KMP 实现；高级层无法调用它，也不需要调用，因为分割扫描
  本来就被输出规模所约束。源码注释直接说明了这一取舍：`O(n*m)` 的
  字节精确扫描"对高级层来说足够，并且让这个翻译单元保持独立"。
- 分割扫描不依赖 PCRE2：它只是普通的分隔符查找。它位于高级层的翻译单元
  中，是按 API 形态归类，而不是按依赖归类。

### 为什么 sx_raw.h 是内部头文件

`include/sx_raw.h` 暴露私有分配层：

```c
typedef struct _sx _sx;

_sx *sx__new(size_t cap);
_sx *sx__dup(const _sx *src);
int sx__reserve(_sx *s, size_t need);
void sx__free(_sx *s);
```

它属于内部有三个原因：

- 它交出 `_sx`——私有缓冲区结构，而 `sx_core.h` 只对其做前置声明；
- 它的函数用保留的双下划线前缀命名，并绕开公开错误模型
  （`sx__reserve` 返回 `int`，而公开 API 返回哨兵 `sx_t *`）——它们服务
  于库自己的翻译单元，而不是客户端；
- 它不提供稳定性承诺。安装后的头文件集合包含它，是因为 Forge 会复制
  整个 `include/*.h`（构建脚本同步整个目录），但它不属于受支持的公开
  API，客户端代码不得使用。四个 `sx__*` 辅助函数声明为 `SYMBOL_PUBLIC`，
  因此确实会出现在共享库的动态符号表中；但这种导出并不代表它们属于受支持
  的 API。

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
2. `len + 1 <= cap`：始终留出结尾符的位置。
3. 始终有 `ptr[len] == '\0'`。内容字节本身可以是 `'\0'`；结尾符是内容
   之后的第一个 NUL。
4. `len` 计的是内容字节数，不含结尾符。`sx_len` 以 `int` 返回它（见
   [整数访问器](#整数访问器与-size_max)）。
5. 库返回的视图满足 `ref = buffer.ref` 且 `offset + len <= buffer len`。
   API 不校验调用方自行构造的视图；边界超出所属缓冲区的视图属于契约之外。
6. `sx_t *` 句柄地址在缓冲区生命期内稳定。`sx_buf_mut` 返回的 `ptr`
   在增长时**不**稳定：`realloc` 可能移动它。
7. `[len, cap)` 区间的字节未指定：它们可能是陈旧内容，也可能是从未初始化
   的内存。只有 `ptr[len]` 属于契约的一部分。

不变式是结构性保证的：每个修改函数结尾都会执行 `ptr[len] = '\0'`，而
`sx__reserve` 是唯一改变 `cap` 或 `ptr` 的地方。`sx_buf_mut` 允许调用方
重写 `[0, len)` 而不改变 `len`；由于结尾符位于该区间之外，不变式仍然
成立。

## 分配与增长

### 初始容量

每个缓冲区都是 `sx__new(cap)` 的一次分配：它分配 `_sx` 头和 `cap` 字节
的数组（最少按 1 处理，好让 `ptr[0]` 有落脚处）。各构造函数的 `cap`
选择如下：

| 构造函数 | 初始 `cap` | 最大内容字节数 |
| --- | --- | --- |
| `sx_new()` | 32 | 31 |
| `sx_new_with_cap(n)` | `n`；`n == 0` 时为 1 | `n - 1`（`n <= 1` 时为 0） |
| `sx_new_from_cstr(s)` | `strlen(s) + 1` | `strlen(s)` |
| `sx_new_from_view(v)` | `v.len + 1` | `v.len` |
| `sx_dup(src)` | `src->len + 1` | `src->len` |
| `sx_substr(h, idx, n)` | `n + 1` | `n` |

`sx_new()` 经由 `sx_new_with_cap(32)` 实现；32 字节的起点让最初几次追加
不产生分配，同时不会浪费太多内存。从内容构造的构造函数精确分配
`内容长度 + 1`，因此新建或拷贝出来的缓冲区容量是紧的。

### 增长策略

所有增长都经过 `sx__reserve(_sx *s, size_t need)`，它把 `need` 解释为
调用方此后必须能够容纳的**内容字节数**。内部必须产生 `cap > need`，
即为 `need` 个字节加上结尾符留出空间。决策如下：

| 情况 | 新的 `cap` |
| --- | --- |
| `cap > need` | 不变（空操作） |
| `need == SIZE_MAX` | 拒绝（整数溢出保护） |
| 默认，`cap < 0x10000` | `max(cap * 2, need + 1)` |
| 默认，`cap >= 0x10000` | `max(cap + 0x10000, need + 1)` |
| `-DSX_FORCE_EXACT_RESERVE` | `need + 1` |

默认策略在分配小于 64 KiB 时按几何级数翻倍，此后改为固定的
`+0x10000` 步进。翻倍使每个追加字节的代价摊还为常数；固定步进则避免
仅仅为了追加几个字节就把大块分配（例如 8 MiB）翻倍到 16 MiB。与
`need + 1` 取 `max` 覆盖了单次调用需要超过下一个几何步长的情况。

公开的 `sx_reserve(self, n)` 原样转发 `n`，因此保证 `cap >= n + 1`：
调用之后，缓冲区总共能容纳 `n` 个内容字节而无需再次重新分配。
`sx_append`、`sx_write` 和 `sx_insert` 在拷贝前调用
`sx__reserve(最终内容长度)`：

- append：`need = len + 追加长度`；
- write：`need = idx + 写入长度`；此后的 `len` 为 `max(len, idx + n)`；
- insert：`need = len + 插入长度`；尾部用 `memmove` 后移。

增长是单调的。收缩操作从不重新分配：`sx_truncate` 只在新长度更大时才
预留，`sx_erase` 在既有分配内部移动字节，`sx_clear` 只重写 `ptr[0]`。
精确预留测试对这两个性质都有断言（`tests/reserve_stress.c`）。

### SX_FORCE_EXACT_RESERVE

`-DSX_FORCE_EXACT_RESERVE` 用 `cap = need + 1` 取代增长策略。此时分配
正好在结尾符处结束，因此任何越过 `len` 的访问都会立刻落入
AddressSanitizer 的红区，而不是藏在多余的容量后面。代价是每次增长都
重新分配（逐字节追加的工作负载在拷贝次数上接近平方级），并且没有余量，
所以它是调试/测试模式，而不是生产模式。`tests/reserve_stress.c` 是它的
专用配置：每个缓冲区都从 `cap == 1` 开始，用固定 LCG 驱动 1500 次伪随机
修改，并与字节精确模型对照；每次重新分配后断言 `cap == len + 1`，同时
断言"收缩从不重新分配"。

## 视图与别名

视图是本库的核心抽象：`sx_view_t` 保存一个缓冲区引用加上一个
`(offset, len)` 窗口。返回视图从不拷贝、从不分配，也从不修改缓冲区。
同一套表示用于整缓冲区视图（`sx_view`）、视图移动
（`sx_view_advance`、`sx_view_range` 等）、分割 token 和正则匹配数组。

调用方必须遵守的规则：

- **视图是快照。** 修改不会调整已捕获的视图。若长度发生变化，先前捕获的
  偏移和长度可能不再描述同样的字节；修改后应重新派生视图。
- **增长会使裸指针失效。** `sx_buf_mut` 和 `sx_buf_view_mut` 借用底层
  数组；之后使缓冲区增长的修改可能 `realloc` 它。`sx_t *` 句柄稳定，
  指针不稳定。
- **视图不得比缓冲区活得更久。** `ref` 是裸 `_sx *`；释放缓冲区就释放
  了被引用对象。返回视图数组不会转移底层缓冲区的所有权。
- **视图使用期间不要修改。** 对 `sx_split` token、
  `sx_reg_match_all` 数组和用户持有的视图，规则相同。
- **`NULL` 视图表示"无"。** `{ref = NULL, offset = 0, len = 0}` 是
  哨兵值；除此之外，库返回的视图都指向存活缓冲区。

别名修改是最微妙的部分。当源视图与目标引用同一个缓冲区时，
`sx_append_view`、`sx_write_view` 和 `sx_insert_view` 用 `memmove`
而不是 `memcpy` 拷贝。对 append 和 write 来说，这只是普通的、能安全
处理重叠的移动，因此任何契约内的源视图都可以使用。`sx_insert_view`
则更窄，原因在于它的操作顺序：

1. 先为**最终**内容长度预留空间（这正是源指针保持有效的原因：
   `realloc` 会原地更新 `_sx.ptr`，因此之后取得的指针看到的是新分配）；
2. 把尾部向右移动 `v.len` 个字节，写入 `[idx + n, len + n)`；
3. 在 `idx` 处用 `memmove` 写入插入的字节。

尾部移动使自别名带有条件：起点在插入点之前或正好在插入点
（`v.offset <= idx`）的源窗口结束于 `idx + n` 或更早，因此移动不会
覆盖它，第 3 步的 `memmove` 再负责处理剩余的重叠。已被测试覆盖的情形是
`v.offset == idx`：把视图 `[2, 4)`（`"cd"`）插入 `"abcdef"` 的索引 2，
得到 `"abcdcdef"`。起点**在 `idx` 之后**的自别名窗口会在被读取之前就被
尾部移动覆盖：把视图 `[3, 5)`（`"de"`）插入 `"abcdef"` 的索引 2，得到
`"abdccdef"` 而不是 `"abdecdef"`；这类调用属于契约之外。
`tests/reserve_stress.c` 末尾的回归用例覆盖的正是强制增长下的
`v.offset == idx` 情形，锁定"先预留、后取指针"的顺序。带 `owned` 参数的
变体（`sx_append(self, self)` 等）没有这层保护——它们会释放自己的参数，
对自别名而言属于契约之外。

## 所有权与错误模型

所有权刻意保持显式且简单：谁收到拥有所有权的指针，谁负责释放。

| 操作 | 所有权 |
| --- | --- |
| `sx_new*`、`sx_dup`、`sx_substr`、`sx_reg_replace*` | 返回调用方拥有的 `sx_t *`；用 `sx_free` 释放 |
| `sx_append`、`sx_write`、`sx_insert`（`owned` 参数） | 消费该参数：由库释放 |
| `sx_split`、`sx_find_all*`、`sx_reg_match_all`、`sx_reg_split` | 返回 `malloc` 分配的数组；用 `free` 释放 |
| 匹配视图、token、整缓冲区视图 | 借用；无需释放 |

需要注意的细节：

- **消费型调用在失败时也会消费。** `sx_append`/`sx_write`/`sx_insert`
  先尝试修改，然后无条件释放 `owned`。如果修改本身失败（分配失败），
  调用返回 `NULL`，但参数仍已被消费。参数为 `NULL` 时返回 `NULL`，
  不释放任何东西。
- **句柄同一性。** 每次修改都返回 `self`，因此成功的修改可以串联：
  `sx_append_cstr(sx_append_cstr(s, "a"), "b") == s`。句柄在增长前后
  稳定；只有底层数组会移动。
- **数组不拥有元素所指的缓冲区。** 释放 `sx_split` 或
  `sx_reg_match_all` 返回的数组并不是释放主体；视图只是变得不可达。

错误模型基于哨兵值，没有 `errno`、没有错误对象，也没有错误返回码：

| 情况 | 哨兵值 |
| --- | --- |
| 偏移查找（`sx_find*`、`sx_rfind*`） | `(size_t)-1` |
| "无出现"（`sx_find_all*`） | `NULL` 数组 |
| "无匹配"视图（`sx_reg_match`） | `{ref = NULL, offset = 0, len = 0}` |
| "无匹配"数组（`sx_reg_match_all`） | `NULL` |
| 非法/空分隔符、非法正则、数组/字符串工厂分配失败 | `NULL` |
| 布尔谓词与 `sx_reg_is_match` | `0` |

两个后果是有意为之，并且都有文档：

- **"无结果"与"内存不足"可能无法区分。** `find_all` 家族和 `sx_reg_*`
  数组家族对两者都返回 `NULL`，因为契约没有给它们第三条通道。正则替换
  家族则能区分：无匹配返回原样拷贝，`NULL` 表示非法输入或失败。
- **`NULL` 句柄参数大多属于契约之外。** 只有显式检查的那些家族有定义的
  `NULL` 行为——`sx_new_from_cstr`、`sx_dup`、cstr/owned 修改参数、
  `sx_find*`、`sx_rfind*`、`sx_find_all*`、
  `sx_cmp*`/`sx_starts_with*`/`sx_ends_with*` 组、`sx_split` 以及
  `sx_reg_*` 调用。访问器（`sx_len`、`sx_cap`、`sx_buf_mut`、`sx_view`
  等）、视图移动函数和 `sx_free` 会解引用句柄，不接受 `NULL`。公开头
  文件按函数写明这些边界。

错误不会留下构建到一半的缓冲区：失败的预留发生在拷贝任何字节之前，
因此 `self` 保持原内容与单 NUL 结尾不变式。唯一可见的例外是上文所述的
被消费的 `owned` 参数。

## 查找内部实现

整个查找家族建立在 `src/sx_core.c` 中唯一的 `static` 原语之上：

```c
static inline find_ret_t find(const sx_t *h, const char *needle, int len,
                              size_t boundary, int opt);
```

返回类型既可能是位置，也可能是指向结果数组的指针，因此实现选择一种宽度
足以往返指针的整数类型：

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

各封装填入文档化的边界：`sx_find*` 使用 `boundary = 0`；`sx_rfind*` 使用
`boundary = len(h)` 并带向右标志；`sx_find_view_b` 和
`sx_rfind_view_b` 传递调用方的 boundary。`len <= 0`、空区域或短于
needle 的区域都按模式要求报告"未找到"——标量封装返回 `(size_t)-1`，
数组封装在内部返回计数为 0 的数组（再由 `sx_find_all*` 转成 `NULL`）。

`len` 是 needle 的内容长度（`sx_t` needle）、`v.len`（视图 needle），
或 `(int)strlen(s)`（cstr needle——注意这里的窄化转换，它属于 `int`
实际限制之一）。needle 字节直接从视图或缓冲区读取；查找是字节精确的，
类似 `memcmp`。

### 单字节 needle：memchr 与 memrchr

当 `m == 1` 时，原语使用 C 库的向量化扫描：

- **最左：** `memchr(t + l, c, er - l)`；
- **最右：** 在 glibc 上使用 `memrchr(t, c, er)`。`memrchr` 是 GNU
  扩展而非 ISO C 函数，因此 `sx_core.c` 手动声明它，并且只在定义了
  `__GLIBC__` 且未定义 `SX_FORCE_NO_MEMRCHR` 时使用。回退方案是逐字节
  反向循环，在到达 `l` 之前保留最后一个（最右的）命中。

最右快路径可以从 `0` 开始搜索，因为向右查找总是设置 `l = 0`；`er`
已经携带了调用方的 boundary。`SX_FORCE_NO_MEMRCHR` 用于在 glibc 系统上
构建并测试可移植路径。

对 `m == 1` 的"全部"模式，实现从上一个命中 + 1 处反复调用 `memchr`。
这样会报告**重叠**位置，而这正是 `sx_find_all*` 承诺的行为
（`"aa"` 在 `"aaaa"` 中报告 0、1 和 2）。

### 更长的 needle：KMP

当 `m >= 2` 时，原语计算 KMP 前缀表（`pi`，一次
`malloc(m * sizeof(size_t))`），并在 `O(n + m)` 时间内扫描区域：

- **最左优先与全部模式**在 `[l, er)` 上使用正向 KMP。完整匹配时，单结果
  模式立即返回；全部模式记录起始偏移，并以 `q = pi[m - 1]` 继续，这正
  是它能报告重叠出现的原因。
- **最右优先**使用反向 KMP：用索引映射在*反向* needle 上计算同一张前缀
  表（不拷贝 needle），再从 `er - 1` 向下扫描到 `l`。当已匹配后缀长度
  达到 `m` 时，当前文本索引就是窗口起点。

全部模式的结果数组从容量 16 开始，带溢出保护地翻倍
（`cap > SIZE_MAX / 2 / sizeof(*r)` 时以 `NULL` 中止），返回前再用
`realloc` 收缩一次。其布局为 `{count, pos0, pos1, ...}`：`arr[0]` 是
计数，不是位置。

### 复杂度

| 情况 | 技术 | 时间 | 额外内存 |
| --- | --- | --- | --- |
| 单字节 needle，最左单结果 | `memchr` | `O(n)` | 无 |
| 单字节 needle，最右单结果 | `memrchr` 或反向扫描 | `O(n)` | 无 |
| needle 长度 >= 2，单结果 | KMP | `O(n + m)` | `O(m)` 前缀表 |
| `sx_find_all*`，单字节 | 反复 `memchr` | `O(n)` 扫描 | `O(k)` 结果 |
| `sx_find_all*`，更长 needle | KMP | `O(n + m)` 扫描 | `O(m) + O(k)` 结果 |
| `sx_split` | 两遍 `is_sep` | 最坏 `O(H * m)` | `O(tokens)` 结果 |

`n` 是区域长度，`m` 是 needle 长度，`k` 是匹配数。数组封装只分配结果；
`sx_find*` / `sx_rfind*` 返回标量，不分配任何东西（除了 KMP 表，它在
返回前释放）。

## 正则封装内部实现

高级正则层刻意保持很薄。它的全部状态就是一个已编译的 PCRE2 代码对象：

```c
typedef struct sx_reg_t {
  pcre2_code *code;
} sx_reg_t;
```

### 编译与释放

`sx_reg_compile` 用固定参数调用 PCRE2：
`pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, 0, ...)`。这里的 `0`
是选项字——没有 `CASELESS`、没有 `UTF`、什么都没有——而
`PCRE2_ZERO_TERMINATED` 表示模式是 C 字符串。PCRE2 的错误码和错误位置
只是为了满足函数签名而接收，随后被丢弃，因此所有编译失败都归结为
`NULL`。编译会分配 PCRE2 代码对象和小的 `sx_reg_t` 封装；`sx_reg_free`
释放两者，并接受 `NULL`。

### 每次调用的匹配数据与扫描规则

PCRE2 把已编译模式与每次调用的匹配数据分开。封装在每次调用时用
`pcre2_match_data_create_from_pattern(r->code, NULL)` 创建匹配数据，
并在返回前释放。这正是已编译句柄可以跨线程共享的原因：句柄中不保存
任何匹配状态。

单步匹配是 `pcre2_match(code, subject, len, offset, 0, md, NULL)`。注意
主体以显式**长度**传入，而不是当作 C 字符串；最后一个参数（匹配上下文）
是 `NULL`，因此适用 PCRE2 的默认匹配限制。负返回值（无匹配，或诸如匹配
限制之类的 PCRE2 匹配期错误）被当作"没有更多匹配"；封装不暴露匹配期
错误码。

`sx_reg_match_all` 和 `sx_reg_split` 共用的扫描推进逻辑是：

```c
static size_t reg_next_at(size_t start, size_t end) {
  return end > start ? end : start + 1;
}
```

下一次搜索从匹配结尾开始；对零长匹配则从起点后一个字节开始，因此扫描
保证前进。每一步都用 `offset` 调用 `pcre2_match`；当 `offset` 越过主体
长度时，PCRE2 返回错误，循环结束。这就是 `a*` 对 `"bbb"` 报告偏移 0、
1、2、3 处的空匹配，以及 `$` 对 `"abc"` 报告偏移 3 处单个空匹配的原因。

### match_all 与 split 的两遍定长策略

两个数组家族都扫描两遍：

```text
pass 1: count matches             -> cnt
malloc((cnt + 1) * sizeof view)   -> exact size, no realloc
pass 2: repeat the same scan      -> fill views, then arr[cnt] = {0} guard
```

扫描是确定性的（相同模式、相同主体、相同起始偏移），因此两遍看到完全
相同的位置。`sx_reg_split` 还额外跟踪空隙起点 `seg`：每个 token 是
`[seg, match_start)`，随后 `seg` 移动到 `match_end`；最后一个 token 是
`[seg, len)`。这样正好产生 `匹配数 + 1` 个 token，包括空的首尾 token，
以及相邻匹配或零长匹配产生的空 token。

### 替换：两次 pcre2_substitute 调用

替换由 PCRE2 的替换例程构建，并固定两个标志：

```c
options |= PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_UNKNOWN_UNSET;
```

`UNSET_EMPTY` 让未参与匹配的组展开为空；`UNKNOWN_UNSET` 让不存在的组号
展开为空，而不是让整个替换失败。`sx_reg_replace` 额外传入
`PCRE2_SUBSTITUTE_GLOBAL`；`sx_reg_replace_first` 不传。

由于替换前不知道输出长度，封装调用两次 `pcre2_substitute`：

```text
call 1: output buffer NULL, PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, &needed
        -> PCRE2_ERROR_NOMEMORY, `needed` = required size including NUL
malloc(needed)                                       (single temporary)
call 2: fill the buffer, &used = needed
        -> rc >= 0, `used` = bytes written excluding NUL
sx_new_with_cap(used + 1)                            (new public buffer)
memcpy(ptr, buf, used); len = used; ptr[used] = '\0'
```

拷贝完成后释放临时缓冲区。若第一次调用没有返回 `PCRE2_ERROR_NOMEMORY`，
说明模板/替换对被拒绝；封装返回 `NULL`，且从不改写主体。没有匹配时，
PCRE2 不替换任何内容，输出就是主体的原样拷贝——这是正常成功，不是错误。

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
匹配选项。模式级特性（例如内联选项）仍然生效，因为它们是模式的一部分，
而不是封装层的策略。

## 并发

- **已编译正则句柄不可变且可共享。** 每次调用的状态都保存在调用内部
  创建的、归调用方所有的匹配数据中；封装没有全局变量。一个
  `sx_reg_t *` 可以同时服务多个线程的调用。
- **缓冲区没有同步。** 它们是普通堆内存。多个线程并发读取同一缓冲区没
  问题，前提是无人修改；修改需要外部同步。视图让规则更严格：与改变长度
  的修改并发读取视图构成数据竞争（契约之外）。
- **所有权需要协调。** 只有一个所有者的缓冲区不能被两个线程同时消费；
  数组是单一所有者的结果。

库本身从不修改全局状态，因此只要调用方对每个缓冲区的访问做了同步，在多
线程程序中使用它就是安全的。

## 可移植性与限制

### memrchr 与 SX_FORCE_NO_MEMRCHR

查找路径中唯一的非 ISO C API 是 `memrchr`，一个 glibc 扩展。
`sx_core.c` 手动声明它，而不依赖特性测试宏，并且只在定义了
`__GLIBC__` 且构建未定义 `SX_FORCE_NO_MEMRCHR` 时编译进去。在其他平台
（以及强制开关打开时），最右单字节查找回退到可移植的逐字节反向循环。
仓库内的测试套件运行默认的 glibc 路径；该开关存在的目的，是让回退路径
也能在 glibc 上构建并检查。

### 整数访问器与 SIZE_MAX

公开访问器返回 `int`，而偏移和 needle 使用 `size_t`：

| 函数 | 返回类型 |
| --- | --- |
| `sx_len`、`sx_len_view` | `int`（内容字节数） |
| `sx_cap` | `int`（总分配量） |
| 偏移（`sx_find*`、视图字段、正则视图） | `size_t` |

内容字节数超过 `INT_MAX` 的缓冲区在测试契约之外。这是有意的 API 简化
（目标场景是中小字符串），不是缺陷；常见平台上每个缓冲区的实际上限约为
2 GiB。同样的 `int` 窄化还出现在查找封装把基于 `strlen` 的 needle 长度
转换为 `int` 的地方。

`SIZE_MAX` 保护出现在尺寸运算可能回绕的地方：

- `sx__reserve` 拒绝 `need == SIZE_MAX`（否则无法再加上结尾符）；
- `sx_find_all*` 的数组增长在翻倍前检查
  `cap > SIZE_MAX / 2 / sizeof(*r)`；
- 没有通用的溢出检查：极端的 `n` 或 `off` 参数可能让无符号尺寸运算
  回绕，这类调用在测试契约之外。

### 平台假设

- 库使用 C23。参考工具链是 Linux 上的 GCC 14+ 与 Clang 19+（CI 使用
  Ubuntu 24.04、GCC 14 和 Clang 19）；除 `SYMBOL_PUBLIC` 和 glibc
  `memrchr` 快路径外，源码不依赖编译器特定扩展。
- `src/export.h` 是已入库的 Forge 派生头文件。它把 `SYMBOL_PUBLIC`
  定义为 GCC/Clang 的 `__attribute__((visibility("default")))`，以及
  MSVC 的 `_declspec(dllexport)`；构建采用隐藏的默认可见性，因此恰好是
  `SYMBOL_PUBLIC` 函数从 `libstringx.so` 导出。
- 正则层需要 PCRE2 10.46（`pcre2-8`），8 位、非 UTF 模式。构建把它
  链接进 `libstringx`，因此使用者无需添加 PCRE2 链接参数，但 PCRE2 是
  共享库的运行时依赖。
- 安装与 Debian 打包目标在 `build.c` 中由 `LINUX` 宏保护；库本身作为
  普通 C23 代码编译。

## 测试策略

`tests/` 下的每个文件都是自包含程序。其文件头注释声明构建方式
（由固定版本的 Forge 构建系统消费的 `// Include`、`// Source`、
`// Option`、`// Link`、`// Library` 行），并记录它的**行为边界**：
一个"In scope (contract)"列表和一个"Out of contract (not exercised)"
列表。文件头本身也是契约的一部分：行为变化时，边界列表要随之修改
（见[贡献指南](contributing.md)）。

测试套件（20 个测试）按主题一文件：

| 测试 | 锁定的内容 |
| --- | --- |
| `access.c` | `sx_len` / `sx_cap` / 缓冲区访问器 / 整缓冲区视图 |
| `append.c` | append 家族、所有权转移、别名追加 |
| `chain.c` | 修改串联、句柄同一性、零泄漏所有权 |
| `cmp.c` | 按字节的字典序比较 |
| `ends_with.c` | 后缀谓词 |
| `find.c` | 最左查找、有界区域、`strlen` needle |
| `find_all.c` | 全部出现，包含重叠位置 |
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

测试如何运行：

- 每个测试都**直接编译库源码**，并使用其文件头中的每测试选项。
  内存检查器开启：AddressSanitizer、UBSan，以及在 Linux 上通过 ASan
  启用的 LeakSanitizer，因此泄漏检查器是所有所有权规则的见证者。CI 设置
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` 和
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。
- `tests/reserve_stress.c` 是唯一用 `-DSX_FORCE_EXACT_RESERVE` 构建的
  测试。它用确定性 LCG 驱动 append、insert、erase、write、truncate 和
  别名视图修改，与字节精确模型对照，并包含别名 `insert_view` 增长的回归
  用例。
- CI 还会把构建好的库安装到临时前缀，通过 `pkg-config` 把
  `tests/reg.c` 链接到**安装后的共享库**（带 rpath）并运行。这样除了
  源码级行为外，还验证了公开头文件、导出符号和 PCRE2 链接；随后检查生成
  的 Debian 包。
- 没有故障注入，因此测试套件不会覆盖分配失败分支（预留测试的注释指出，
  没有故障注入就无法表达这样的测试）。文档化的 OOM 哨兵是从代码推得，
  而不是在测试中观察到的。
- `./make check` 目标用 `-Wall -Wextra -Wpedantic -Werror` 编译生产代码；
  GCC 构建还运行 `-fanalyzer`（CI 中使用
  `-O1 -fno-analyzer-state-merge`），CodeQL 用 `security-and-quality`
  查询扫描。
- 测试禁用 LTO（`build.c` 中的 `set(TARGET_LTO, 1, DISABLE)`），因为
  固定版本的 Forge 为并行 Clang 链接发出仅 GCC 支持的 `-flto=N` 标志。
  生产库目标保留 LTO。

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
| KMP + `memchr`/`memrchr` | 最坏 `O(n + m)` 有保证，代码简单，单字节路径向量化 | `m >= 2` 时需要前缀表分配；平均速度低于优化过的子串查找 |
| `sx_find_all*` 报告重叠结果 | 完整位置集合；对字节扫描有用 | 调用方必须预期偏移会重叠 |
| 正则非重叠扫描 | 标准 PCRE2 行为；保证前进 | 无法报告重叠的正则匹配 |
| 两遍精确定长 | 一次分配、不靠 realloc 增长、确定性 | 数组 API 的匹配工作量大约翻倍 |
| 薄 PCRE2 封装，无选项/JIT | 接口面极小、完整的 PCRE2 语义、单一依赖 | 没有封装层标志或调优；PCRE2 在运行时链接 |
| 已编译正则不可变且无内部锁 | 句柄可共享且无需同步 | 缓冲区仍需调用方同步 |
| 构建期配置宏 | 增长/memrchr 策略零运行时成本 | 改变策略需要重新构建 |

这些是取舍，不是意外。测试锁定可观察的后果，头文件注释写明每条规则
契约之外的那一面。

## 许可

项目源码采用 MIT 许可（见 [LICENSE](../../LICENSE) 以及源码中的 SPDX
标注）。Forge 派生的构建工具遵循构建脚本例外：`build.conf` 与
`src/export.h` 带有
GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception 的 SPDX
标识，`build.c` 也由同一例外覆盖；库源码本身仍为 MIT。正则层使用 PCRE2
（BSD-3-Clause）；见
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。

---

[← 上一页](regex.md) · [文档首页](README.md) · [下一页 →](build-and-test.md)
