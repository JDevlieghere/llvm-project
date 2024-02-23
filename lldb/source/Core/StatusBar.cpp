//===-- StatusBar.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/StatusBar.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Utility/AnsiTerminal.h"

#include <sys/ioctl.h>
#include <termios.h>

#define ESCAPE "\x1b"
#define ANSI_SAVE_CURSOR ESCAPE "7"
#define ANSI_RESTORE_CURSOR ESCAPE "8"
#define ANSI_CLEAR_BELOW ESCAPE "[J"

#define ANSI_SET_SCROLL_ROWS ESCAPE "[0;%ur"
#define ANSI_TO_START_OF_ROW ESCAPE "[%u;0f"
#define ANSI_UP_ROWS ESCAPE "[%dA"
#define ANSI_DOWN_ROWS ESCAPE "[%dB"
#define ANSI_FORWARD_COLS ESCAPE "\033[%dC"
#define ANSI_BACKWARD_COLS ESCAPE "\033[%dD"

using namespace lldb;
using namespace lldb_private;

StatusBar::StatusBar(Debugger &debugger) : m_debugger(debugger) {}

StatusBar::~StatusBar() { Disable(); }

void StatusBar::Enable() {
  UpdateTerminalProperties();

  // Reduce the scroll window to make space for the status bar below.
  SetScrollWindow(m_terminal_height - 1);
}

void StatusBar::Disable() {
  UpdateTerminalProperties();

  // Clear the previous status bar if any.
  Clear();

  // Extend the scroll window to cover the status bar.
  SetScrollWindow(m_terminal_height);
}

void StatusBar::Refresh() {
  UpdateTerminalProperties();

  Clear();
  Draw();
}

void StatusBar::Draw() {
  StreamFile &out = m_debugger.GetOutputStream();

  out << ANSI_SAVE_CURSOR;
  out.Printf(ANSI_TO_START_OF_ROW, static_cast<unsigned>(m_terminal_height));

  out << ansi::FormatAnsiTerminalCodes(m_ansi_prefix, m_use_color);
  out << m_status;
  out << std::string(m_terminal_width - m_status.size(), ' ');
  out << ansi::FormatAnsiTerminalCodes(m_ansi_suffix, m_use_color);

  out << ANSI_RESTORE_CURSOR;
}

void StatusBar::Clear() {
  // Extend the scroll window to cover the status bar.
  SetScrollWindow(m_scroll_height);
}

void StatusBar::SetStatus(std::string status) {
  m_status = std::move(status);
  Refresh();
}

void StatusBar::UpdateTerminalProperties() {
  // Purposely ignore the terminal settings. If the setting doesn't match
  // reality and we draw the status bar over existing text, we have no way to
  // recover. However we must still get called when the setting changes, as
  // we cannot install our own SIGWINCH handler.
  struct winsize window_size;
  if ((isatty(STDIN_FILENO) != 0) &&
      ::ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0) {
    m_terminal_width = window_size.ws_col;
    m_terminal_height = window_size.ws_row;
  }

  m_use_color = m_debugger.GetUseColor();
}

void StatusBar::SetScrollWindow(uint64_t height) {
  StreamFile &out = m_debugger.GetOutputStream();

  out << '\n';

  // Save the cursor.
  out << ANSI_SAVE_CURSOR;

  // Set the scroll window to the given height.
  out.Printf(ANSI_SET_SCROLL_ROWS, static_cast<unsigned>(height));

  // Restore the cursor.
  out << ANSI_RESTORE_CURSOR;

  // Move cursor back inside the scroll window.
  out.Printf(ANSI_UP_ROWS, 1);

  // Clear everything below.
  out << ANSI_CLEAR_BELOW;

  // Flush.
  out.Flush();

  m_scroll_height = height;
}
