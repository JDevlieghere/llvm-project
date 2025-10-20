//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_RWI_PLATFORMSERVER_H
#define LLDB_TOOLS_LLDB_RWI_PLATFORMSERVER_H

#include "Server.h"

namespace lldb_rwi {

class PlatformServer : public Server {
public:
  PlatformServer(lldb_private::MainLoopBase &loop, uint64_t port)
      : Server(loop, port) {}
  llvm::Error Read(Connection &connection) override;
};

} // namespace lldb_rwi

#endif
