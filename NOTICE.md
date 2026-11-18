# Third-party notices

## MinHook

Static-linked into `!00_AE27CharsetFixer.aex` (built with `/MT`).

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 TsudaKageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

MinHook vendors the HDE64 disassembler (Copyright (C) 2008-2009 Vyacheslav
Patuk, BSD-style terms as stated inside `hde64.c`).

## After Effects SDK

The Adobe After Effects SDK headers are **not** distributed in this
repository. Obtain them from Adobe and point `AE_SDK` at the extracted
folder before building. Redistributing plugins built with the SDK is
governed by Adobe's SDK licence terms.

## Original project

This plugin derives from [AdoreLowen/AE-UTF8-Fixer](https://github.com/AdoreLowen/AE-UTF8-Fixer)
(MIT): it reuses the AEGP hook skeleton, the `PF_ParamDef` field walk and the
MinHook integration approach. The conversion direction, the per-module
dispatch, the allow-list logic and the parameter sweep were rewritten.
