//===-- Transport.h -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Debug Adapter Protocol transport layer for encoding and decoding protocol
// messages.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_TRANSPORT_H
#define LLDB_TOOLS_LLDB_DAP_TRANSPORT_H

#include "DAPForward.h"
#include "lldb/Protocol/DAP/ProtocolBase.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <chrono>
#include <system_error>

namespace lldb_dap {

class EndOfFileError : public llvm::ErrorInfo<EndOfFileError> {
public:
  static char ID;

  EndOfFileError() = default;

  void log(llvm::raw_ostream &OS) const override {
    OS << "end of file reached";
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

class TimeoutError : public llvm::ErrorInfo<TimeoutError> {
public:
  static char ID;

  TimeoutError() = default;

  void log(llvm::raw_ostream &OS) const override {
    OS << "operation timed out";
  }
  std::error_code convertToErrorCode() const override {
    return std::make_error_code(std::errc::timed_out);
  }
};

/// A transport class that performs the Debug Adapter Protocol communication
/// with the client.
class Transport {
public:
  Transport(llvm::StringRef client_name, Log *log, lldb::IOObjectSP input,
            lldb::IOObjectSP output);
  ~Transport() = default;

  /// Transport is not copyable.
  /// @{
  Transport(const Transport &rhs) = delete;
  void operator=(const Transport &rhs) = delete;
  /// @}

  /// Writes a Debug Adater Protocol message to the output stream.
  llvm::Error Write(const lldb_private::protocol::dap::Message &M);

  /// Reads the next Debug Adater Protocol message from the input stream.
  ///
  /// \param timeout[in]
  ///     A timeout to wait for reading the initial header. Once a message
  ///     header is recieved, this will block until the full message is
  ///     read.
  ///
  /// \returns Returns the next protocol message.
  llvm::Expected<lldb_private::protocol::dap::Message>
  Read(const std::chrono::microseconds &timeout);

  /// Returns the name of this transport client, for example `stdin/stdout` or
  /// `client_1`.
  llvm::StringRef GetClientName() { return m_client_name; }

private:
  llvm::StringRef m_client_name;
  Log *m_log;
  lldb::IOObjectSP m_input;
  lldb::IOObjectSP m_output;
};

} // namespace lldb_dap

#endif
