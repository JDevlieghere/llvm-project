//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBServer.h"
#include "PlatformServer.h"
#include "lldb/Host/MainLoop.h"
#include "lldb/Host/MainLoopBase.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_rwi;

using lldb_private::MainLoop;
using lldb_private::MainLoopBase;

llvm::cl::opt<uint64_t> platform_port("p", llvm::cl::desc("The platform port."),
                                      llvm::cl::value_desc("port"));

llvm::cl::opt<uint64_t> gdb_port("g", llvm::cl::desc("The gdb-remote port."),
                                 llvm::cl::value_desc("port"));

int main(int argc, char *argv[]) {
  llvm::InitLLVM IL(argc, argv, /*InstallPipeSignalExitHandler=*/false);
  llvm::setBugReportMsg("PLEASE submit a bug report to " LLDB_BUG_REPORT_URL
                        " and include the crash report from "
                        "~/Library/Logs/DiagnosticReports/.\n");
  llvm::cl::ParseCommandLineOptions(argc, argv);

  static MainLoop loop;
  sys::SetInterruptFunction([]() {
    loop.AddPendingCallback(
        [](MainLoopBase &loop) { loop.RequestTermination(); });
  });

  auto server = platform_port
                    ? std::unique_ptr<Server>(
                          std::make_unique<PlatformServer>(loop, platform_port))
                    : std::unique_ptr<Server>(
                          std::make_unique<GDBServer>(loop, gdb_port));

  if (llvm::Error error = server->Start()) {
    llvm::WithColor::error() << llvm::toString(std::move(error)) << '\n';
    return EXIT_FAILURE;
  }

  loop.Run();

  return 0;
}
