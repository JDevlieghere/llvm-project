//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Server.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_rwi;

using lldb_private::MainLoopBase;
using lldb_private::Socket;
using lldb_private::Status;

using lldb_private::Status;

Server::~Server() {}

llvm::Error Server::Start() {
  Status status;

  // Create a socket.
  m_listener = Socket::Create(Socket::SocketProtocol::ProtocolTcp, status);
  if (status.Fail())
    return status.ToError();

  // Listen on the given port.
  std::string host_and_port = llvm::formatv("localhost:{0}", m_port).str();
  status = m_listener->Listen(host_and_port,
                              /*backlog=*/5);
  if (status.Fail())
    return status.ToError();

  // Create read handles.
  auto handles = m_listener->Accept(m_loop, [&](std::unique_ptr<Socket> sock) {
    llvm::outs() << "New connection\n";
    m_connections.push_back(std::make_unique<Connection>());

    Connection &connection = *m_connections.back();
    connection.io_sp = std::move(sock);
    connection.read_handle = m_loop.RegisterReadObject(
        connection.io_sp,
        [&](MainLoopBase &loop) {
          if (Error error = Read(connection)) {
            llvm::WithColor::error() << std::move(error) << '\n';
            connection.read_handle.reset();
          }
        },
        status);
  });
  if (!handles)
    return handles.takeError();

  m_read_handles = std::move(*handles);
  return llvm::Error::success();
}
