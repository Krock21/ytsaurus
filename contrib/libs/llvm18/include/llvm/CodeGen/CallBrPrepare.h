#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===-- CallBrPrepare - Prepare callbr for code generation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALLBRPREPARE_H
#define LLVM_CODEGEN_CALLBRPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class CallBrPreparePass : public PassInfoMixin<CallBrPreparePass> {
public:
  PreservedAnalyses run(Function &Fn, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_CALLBRPREPARE_H

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
