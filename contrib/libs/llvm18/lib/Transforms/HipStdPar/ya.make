# Generated by devtools/yamaker.

LIBRARY()

VERSION(18.1.8)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/llvm18
    contrib/libs/llvm18/lib/Analysis
    contrib/libs/llvm18/lib/IR
    contrib/libs/llvm18/lib/Support
    contrib/libs/llvm18/lib/Transforms/Utils
)

ADDINCL(
    contrib/libs/llvm18/lib/Transforms/HipStdPar
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    HipStdPar.cpp
)

END()
