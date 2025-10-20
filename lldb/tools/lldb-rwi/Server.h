//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_RWI_SERVER_H
#define LLDB_TOOLS_LLDB_RWI_SERVER_H

#include "lldb/Host/MainLoopBase.h"
#include "lldb/Host/Socket.h"

namespace lldb_rwi {

class Server {
public:
  Server(lldb_private::MainLoopBase &loop, uint64_t port)
      : m_loop(loop), m_port(port) {}
  virtual ~Server();

  struct Connection {
    lldb::IOObjectSP io_sp;
    lldb_private::MainLoopBase::ReadHandleUP read_handle;
    std::string buffer;
  };

  llvm::Error Start();

  static constexpr size_t kChunkSize = 1024;

protected:
  virtual llvm::Error Read(Connection &connection) = 0;

  lldb_private::MainLoopBase &m_loop;
  const uint64_t m_port;

private:
  std::unique_ptr<lldb_private::Socket> m_listener;
  std::vector<lldb_private::MainLoopBase::ReadHandleUP> m_read_handles;
  std::vector<std::unique_ptr<Connection>> m_connections;
};

} // namespace lldb_rwi

#endif
