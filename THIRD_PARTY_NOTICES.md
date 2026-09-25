# Third-Party Notices

This project is distributed under the MIT license (see LICENSE).  The
following third-party components are used and their licenses are
reproduced below as required.

## PCRE2 (pcre2-8)

- Component: PCRE2 — Perl Compatible Regular Expressions, 8-bit library
- Version: 10.46 (system package `libpcre2-8-0` / `libpcre2-dev`)
- Homepage: https://www.pcre.org/ — source: https://github.com/PCRE2Project/pcre2
- Usage: dynamically linked by libsx for the `sx_reg_*` regex API
  (`sx_reg_compile` / `sx_reg_match*` / `sx_reg_replace*` / `sx_reg_split`).
- License: BSD 3-Clause (reproduced below)

### PCRE2 LICENCE — BSD 3-Clause

PCRE2 is a library of functions to support regular expressions whose
syntax and semantics are as close as possible to those of the Perl 5
language.  Release 10 of PCRE2 was released in 2015, and PCRE2 is now
the preferred version of PCRE.

The upstream licence file carries the following copyrights:

    Copyright (c) 1997-2024 University of Cambridge
    Copyright (c) 2016-2024 Zoltan Herczeg
    Copyright (c) 2009-2024 PCRE2 Contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the
   distribution.

3. The names of the copyright holders may not be used to endorse or
   promote products derived from this software without specific prior
   written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

End of PCRE2 LICENCE.