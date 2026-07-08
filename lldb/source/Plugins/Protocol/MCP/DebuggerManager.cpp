//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DebuggerManager.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FormatVariadic.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::mcp;
using namespace llvm;

DebuggerManager::~DebuggerManager() { DestroyAll(); }

Expected<DebuggerSP> DebuggerManager::CreateDebugger() {
  DebuggerSP debugger_sp = Debugger::CreateInstance();
  if (!debugger_sp)
    return createStringError("failed to create debugger");

  // CreateInstance already registered the debugger in the global list. If setup
  // fails before we track it below, destroy it so it is not left orphaned there
  // (untracked, and thus never reclaimed by DestroyAll).
  auto destroy_on_error =
      llvm::make_scope_exit([&] { Debugger::Destroy(debugger_sp); });

  // Redirect the new debugger's I/O to the null device. Its command output is
  // captured through the CommandReturnObject, so its own streams would only
  // leak into the host process (for example a developer's interactive
  // terminal) if left pointing at stdin/stdout/stderr.
  FileSystem &fs = FileSystem::Instance();
  Expected<std::unique_ptr<File>> nullin =
      fs.Open(FileSpec(FileSystem::DEV_NULL), File::eOpenOptionReadOnly);
  if (!nullin)
    return nullin.takeError();
  Expected<std::unique_ptr<File>> nullout =
      fs.Open(FileSpec(FileSystem::DEV_NULL), File::eOpenOptionWriteOnly);
  if (!nullout)
    return nullout.takeError();
  Expected<std::unique_ptr<File>> nullerr =
      fs.Open(FileSpec(FileSystem::DEV_NULL), File::eOpenOptionWriteOnly);
  if (!nullerr)
    return nullerr.takeError();

  debugger_sp->SetInputFile(std::move(*nullin));
  debugger_sp->SetOutputFile(std::move(*nullout));
  debugger_sp->SetErrorFile(std::move(*nullerr));

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_debugger_ids.insert(debugger_sp->GetID());
  }

  destroy_on_error.release();
  return debugger_sp;
}

Error DebuggerManager::DestroyDebugger(user_id_t debugger_id) {
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_debugger_ids.erase(debugger_id))
      return createStringError(
          formatv("debugger {0} was not created by MCP and cannot be deleted",
                  debugger_id));
  }

  DebuggerSP debugger_sp = Debugger::FindDebuggerWithID(debugger_id);
  if (!debugger_sp)
    return createStringError(formatv("debugger {0} not found", debugger_id));

  Debugger::Destroy(debugger_sp);
  return Error::success();
}

void DebuggerManager::DestroyAll() {
  SmallVector<user_id_t, 4> debugger_ids;
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    debugger_ids.assign(m_debugger_ids.begin(), m_debugger_ids.end());
    m_debugger_ids.clear();
  }

  // Destroy outside the lock: Debugger::Destroy takes the global debugger list
  // mutex and there is no need to hold ours while it runs.
  for (user_id_t debugger_id : debugger_ids) {
    if (DebuggerSP debugger_sp = Debugger::FindDebuggerWithID(debugger_id))
      Debugger::Destroy(debugger_sp);
  }
}
