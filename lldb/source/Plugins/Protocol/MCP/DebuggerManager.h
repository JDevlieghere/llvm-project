//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROTOCOL_MCP_DEBUGGERMANAGER_H
#define LLDB_SOURCE_PLUGINS_PROTOCOL_MCP_DEBUGGERMANAGER_H

#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Error.h"
#include <mutex>

namespace lldb_private::mcp {

/// Tracks the debuggers created through MCP so their lifetime can be managed
/// independently of debuggers that already exist in the process, such as a
/// developer's interactive session. A command interpreter is bound to a single
/// debugger and therefore cannot create one, so debugger lifecycle lives here,
/// at the process-global MCP layer, instead. Only debuggers created here may be
/// destroyed here: an MCP client is a guest and must never tear down a debugger
/// it did not create.
class DebuggerManager {
public:
  DebuggerManager() = default;
  ~DebuggerManager();

  DebuggerManager(const DebuggerManager &) = delete;
  DebuggerManager &operator=(const DebuggerManager &) = delete;

  /// Create a new debugger, record it as MCP-owned, and silence its I/O so it
  /// does not leak into the host process. Command output is captured through
  /// the command's return object, not the debugger's streams.
  llvm::Expected<lldb::DebuggerSP> CreateDebugger();

  /// Destroy a debugger previously created here. Returns an error if the id is
  /// unknown or was not created through MCP.
  llvm::Error DestroyDebugger(lldb::user_id_t debugger_id);

  /// Destroy all remaining MCP-created debuggers.
  void DestroyAll();

private:
  std::mutex m_mutex;
  llvm::DenseSet<lldb::user_id_t> m_debugger_ids;
};

} // namespace lldb_private::mcp

#endif
