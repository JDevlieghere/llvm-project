//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformServer.h"

using namespace lldb_rwi;

using lldb_private::Status;

static llvm::Error Reply(Server::Connection &connection,
                         llvm::StringRef response) {
  std::size_t bytes = response.size();
  return connection.io_sp->Write(response.data(), bytes).ToError();
}

static llvm::Error HandleAck(Server::Connection &connection,
                             llvm::StringRef packet) {
  return llvm::Error::success();
}

static llvm::Error HandleQStartNoAckMode(Server::Connection &connection,
                                         llvm::StringRef packet) {
  if (llvm::Error error = Reply(connection, "+"))
    return error;
  return Reply(connection, "$OK#9a");
}

static llvm::Error HandleqHostInfo(Server::Connection &connection,
                                   llvm::StringRef packet) {
  return Reply(connection, "$OK#9a");
}

static llvm::Error HandleqGetWorkingDir(Server::Connection &connection,
                                        llvm::StringRef packet) {
  return Reply(connection, "$OK#9a");
}

static llvm::Error HandleqQueryGDBServer(Server::Connection &connection,
                                         llvm::StringRef packet) {
  return Reply(connection, "$OK#9a");
}

static llvm::Error HandlePacket(Server::Connection &connection,
                                llvm::StringRef packet) {

  if (packet == "+")
    return HandleAck(connection, packet);

  if (packet.starts_with("$QStartNoAckMode"))
    return HandleQStartNoAckMode(connection, packet);

  if (packet.starts_with("$qHostInfo"))
    return HandleqHostInfo(connection, packet);

  if (packet.starts_with("$qGetWorkingDir"))
    return HandleqGetWorkingDir(connection, packet);

  if (packet.starts_with("$qQueryGDBServer"))
    return HandleqQueryGDBServer(connection, packet);

  return Reply(connection, "$OK#9a");
}

static std::pair<llvm::StringRef, llvm::StringRef>
GetPacketFromBuffer(llvm::StringRef buffer) {
  if (buffer.starts_with("+"))
    return {"+", buffer.drop_front(1)};

  auto [head, tail] = buffer.split('#');
  if (head.empty() || tail.size() < 2)
    return {"", buffer};

  if (!isxdigit(tail[0]) || !isxdigit(tail[1]))
    return {"", buffer};

  return {head, tail.drop_front(2)};
}

llvm::Error PlatformServer::Read(Connection &connection) {
  char chunk[kChunkSize];
  size_t bytes_read = sizeof(chunk);
  if (Status status = connection.io_sp->Read(chunk, bytes_read); status.Fail())
    return status.takeError();

  connection.buffer.append(chunk, bytes_read);

  llvm::StringRef packet, new_buffer;
  do {
    std::tie(packet, new_buffer) = GetPacketFromBuffer(connection.buffer);
    if (!packet.empty()) {
      llvm::outs() << "Got packet: " << packet << '\n';
      if (llvm::Error error = HandlePacket(connection, packet))
        return error;
      connection.buffer = new_buffer.str();
    }
  } while (!packet.empty());

  return llvm::Error::success();
}
