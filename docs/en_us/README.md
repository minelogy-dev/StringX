# String X documentation (English)

**Version v0.5** · [简体中文](../zh_cn/README.md) ·
[Repository](https://github.com/minelogy-dev/StringX)

String X is a small C string library built around one idea: every string is a
single NUL-terminated buffer, and every slice of it is a view that copies
nothing. The public surface is one header, `<sx.h>`, and one library,
`libstringx` (`-lstringx`); regular expressions are an optional advanced layer
backed by PCRE2.

## Start here

| If you want to… | Read |
| --- | --- |
| Install String X and write your first program | [Getting started](getting-started.md) |
| Understand the buffer/view/ownership model and see recipes | [User guide](guide.md) |
| Look up a buffer, view or mutation function | [API reference: buffers & views](api-buffer.md) |
| Look up a search, compare or split function | [API reference: search, compare & split](api-search.md) |
| Use the regular-expression layer | [Regular expressions](regex.md) |
| Understand how the library works inside | [Design & internals](design.md) |
| Build, test, install or package it | [Build, test & package](build-and-test.md) |
| Send a change | [Contributing](contributing.md) |
| Understand the CI gates | [Continuous integration](ci.md) |

## All documents

- [Getting started](getting-started.md) — requirements, build, install, first
  program, ten-minute tour, documentation map.
- [User guide](guide.md) — mental model, ownership rules, binary safety, error
  handling, task recipes, pitfalls, performance notes.
- [API reference: buffers & views](api-buffer.md) — creation and destruction,
  capacity, accessors, mutation, substring, view navigation and trimming.
- [API reference: search, compare & split](api-search.md) — `sx_find*`,
  `sx_rfind*`, `sx_find_all*`, `sx_cmp*`, `sx_starts_with*`, `sx_ends_with*`,
  `sx_split`.
- [Regular expressions](regex.md) — the `sx_reg_*` families: match, replace and
  split, with the full contract of each call.
- [Design & internals](design.md) — data layout, growth policy, search
  algorithms, regex wrapper internals, portability limits, testing strategy.
- [Build, test & package](build-and-test.md) — Forge, `./make` targets, the
  test harness, sanitizers, installation and Debian packaging.
- [Contributing](contributing.md) — workflow, contract-first documentation,
  code style, tests, licensing.
- [Continuous integration](ci.md) — workflows, required checks, CodeQL,
  merge protection, local reproduction.

## Documentation conventions

- The API reference covers the public headers only: `sx.h` and the
  `sx_core.h` it includes. `sx_raw.h` is an internal allocation/debug API and
  is not part of the supported public surface.
- Every behavior statement is derived from the public header contracts, the
  implementation, and the behavior-boundary comments in `tests/*.c`.
- **Out of contract** means the call is outside the library's documented
  guarantees and its behavior is not defined; the tests deliberately do not
  exercise it, and your program must not rely on it.
- Code examples target C23 on Linux and link `-lstringx` (the regex examples
  need no extra flags: the library links `pcre2-8` itself).
- The documentation describes **v0.5**; `build.conf` is the single source of
  truth for the version.

## License

The library is MIT licensed — see [LICENSE](../../LICENSE) and the SPDX
headers in the sources. The regex layer depends on PCRE2 (BSD-3-Clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
