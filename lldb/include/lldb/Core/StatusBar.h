//===-- StatusBar.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Debugger.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

#ifndef LLDB_CORE_STATUSBAR_H
#define LLDB_CORE_STATUSBAR_H

namespace lldb_private {

class StatusBar {
public:
  StatusBar(Debugger &debugger);
  ~StatusBar();

  void Enable();
  void Disable();
  void Refresh();

  void SetStatus(std::string status);

private:
  // Update terminal dimensions.
  void UpdateTerminalProperties();

  // Set the scroll window to the given height.
  void SetScrollWindow(uint64_t height);

  // Write at the given column.
  void AddAtPosition(uint64_t col, llvm::StringRef str);

  // Draw the status bar.
  void Draw();

  // Clear the status bar.
  void Clear();

  Debugger &m_debugger;
  uint64_t m_terminal_width = 0;
  uint64_t m_terminal_height = 0;
  uint64_t m_scroll_height = 0;
  bool m_use_color = false;
  std::string m_ansi_prefix = "${ansi.bg.yellow}${ansi.fg.black}";
  std::string m_ansi_suffix = "${ansi.normal}";

  std::string m_status;
};

} // namespace lldb_private

#endif // LLDB_CORE_STATUSBAR_H
