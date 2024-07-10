//===-- StackProviderPython.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_STACKPROVIDER_PYTHON_STACKPROVIDERPYTHON_H
#define LLDB_SOURCE_PLUGINS_STACKPROVIDER_PYTHON_STACKPROVIDERPYTHON_H

#include "lldb/Target/ExecutionContext.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/lldb-defines.h"

namespace lldb_private {

class StackProvider {};

class StackProviderPython : public StackProvider {
public:
  static void Initialize();
  static void Terminate();

public:
  llvm::Error GetTraceback(ExecutionContext &exe_ctx);
  llvm::Expected<std::string> RunPythonCode(ExecutionContext &exe_ctx,
                                            std::string str);

private:
  std::unique_ptr<UtilityFunction> m_utility_fn;
  FunctionCaller *m_utility_fn_caller = nullptr;
  lldb::addr_t m_utility_fn_buffer_addr = LLDB_INVALID_ADDRESS;
};
} // namespace lldb_private

#endif
