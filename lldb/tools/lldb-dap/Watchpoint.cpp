//===-- Watchpoint.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Watchpoint.h"
#include "DAP.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Protocol/DAP/ProtocolTypes.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>

using namespace lldb_private::protocol;

namespace lldb_dap {
Watchpoint::Watchpoint(DAP &d, const dap::DataBreakpoint &breakpoint)
    : BreakpointBase(d, breakpoint.condition, breakpoint.hitCondition) {
  llvm::StringRef dataId = breakpoint.dataId;
  auto [addr_str, size_str] = dataId.split('/');
  llvm::to_integer(addr_str, m_addr, 16);
  llvm::to_integer(size_str, m_size);
  m_options.SetWatchpointTypeRead(breakpoint.accessType !=
                                  dap::eDataBreakpointAccessTypeWrite);
  if (breakpoint.accessType != dap::eDataBreakpointAccessTypeRead)
    m_options.SetWatchpointTypeWrite(lldb::eWatchpointWriteTypeOnModify);
}

void Watchpoint::SetCondition() { m_wp.SetCondition(m_condition.c_str()); }

void Watchpoint::SetHitCondition() {
  uint64_t hitCount = 0;
  if (llvm::to_integer(m_hit_condition, hitCount))
    m_wp.SetIgnoreCount(hitCount - 1);
}

dap::Breakpoint Watchpoint::ToProtocolBreakpoint() {
  dap::Breakpoint breakpoint;
  if (!m_error.IsValid() || m_error.Fail()) {
    breakpoint.verified = false;
    if (m_error.Fail())
      breakpoint.message = m_error.GetCString();
  } else {
    breakpoint.verified = true;
  }

  return breakpoint;
}

void Watchpoint::SetWatchpoint() {
  m_wp = m_dap.target.WatchpointCreateByAddress(m_addr, m_size, m_options,
                                                m_error);
  if (!m_condition.empty())
    SetCondition();
  if (!m_hit_condition.empty())
    SetHitCondition();
}
} // namespace lldb_dap
