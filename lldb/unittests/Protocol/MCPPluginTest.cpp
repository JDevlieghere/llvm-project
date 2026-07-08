//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Platform/MacOSX/PlatformMacOSX.h"
#include "Plugins/Platform/MacOSX/PlatformRemoteMacOSX.h"
#include "Plugins/Protocol/MCP/DebuggerManager.h"
#include "Plugins/Protocol/MCP/ProtocolServerMCP.h"
#include "Plugins/Protocol/MCP/Resource.h"
#include "Plugins/Protocol/MCP/Tool.h"
#include "Plugins/ScriptInterpreter/None/ScriptInterpreterNone.h"
#include "TestingSupport/SubsystemRAII.h"
#include "TestingSupport/TestUtilities.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ProtocolServer.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/Socket.h"
#include "lldb/Protocol/MCP/MCPError.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::mcp;
using namespace lldb_protocol::mcp;

#ifndef _WIN32

namespace {
class MCPPluginTest : public testing::Test {
public:
  SubsystemRAII<FileSystem, HostInfo, PlatformMacOSX, ScriptInterpreterNone,
                Socket>
      subsystems;
  DebuggerSP m_debugger_sp;

  void SetUp() override {
    std::call_once(TestUtilities::g_debugger_initialize_flag,
                   []() { Debugger::Initialize(nullptr); });
    ArchSpec arch("x86_64-apple-macosx-");
    Platform::SetHostPlatform(
        PlatformRemoteMacOSX::CreateInstance(true, &arch));
    m_debugger_sp = Debugger::CreateInstance();
  }

  void TearDown() override {
    Debugger::Destroy(m_debugger_sp);
    m_debugger_sp.reset();
  }

  TargetSP CreateTarget() {
    ArchSpec arch("x86_64-apple-macosx-");
    PlatformSP platform_sp;
    TargetSP target_sp;
    m_debugger_sp->GetTargetList().CreateTarget(
        *m_debugger_sp, "", arch, eLoadDependentsNo, platform_sp, target_sp);
    return target_sp;
  }

  TargetSP CreateTargetWithExecutable(StringRef path) {
    TargetSP target_sp = CreateTarget();
    ArchSpec arch("x86_64-apple-macosx-");
    // Use the FileSpec/ArchSpec constructor so the module keeps its file spec
    // even though the path doesn't point at a real object file.
    ModuleSP module_sp = std::make_shared<Module>(FileSpec(path), arch);
    target_sp->SetExecutableModule(module_sp, eLoadDependentsNo);
    return target_sp;
  }
};

void ExpectUnsupportedURI(Expected<ReadResourceResult> result) {
  ASSERT_FALSE(static_cast<bool>(result));
  llvm::Error err = result.takeError();
  EXPECT_TRUE(err.isA<UnsupportedURI>());
  consumeError(std::move(err));
}
} // namespace

//===----------------------------------------------------------------------===//
// DebuggerResourceProvider
//===----------------------------------------------------------------------===//

TEST_F(MCPPluginTest, GetResources) {
  CreateTarget();
  CreateTargetWithExecutable("/tmp/lldb-mcp-test/my_executable");

  DebuggerResourceProvider provider;
  std::vector<Resource> resources = provider.GetResources();

  std::string debugger_uri =
      formatv("lldb://debugger/{0}", m_debugger_sp->GetID()).str();
  std::string target0_uri =
      formatv("lldb://debugger/{0}/target/0", m_debugger_sp->GetID()).str();
  std::string target1_uri =
      formatv("lldb://debugger/{0}/target/1", m_debugger_sp->GetID()).str();

  std::vector<std::string> uris;
  std::string exe_target_name;
  for (const Resource &resource : resources) {
    uris.push_back(resource.uri);
    if (resource.uri == target1_uri)
      exe_target_name = resource.name;
  }

  EXPECT_THAT(uris, testing::Contains(debugger_uri));
  EXPECT_THAT(uris, testing::Contains(target0_uri));
  EXPECT_THAT(uris, testing::Contains(target1_uri));
  // The target with an executable module is named after the executable.
  EXPECT_EQ(exe_target_name, "my_executable");
}

TEST_F(MCPPluginTest, ReadResourceUnsupportedScheme) {
  DebuggerResourceProvider provider;
  ExpectUnsupportedURI(provider.ReadResource("http://example.com"));
}

TEST_F(MCPPluginTest, ReadResourceTooFewComponents) {
  DebuggerResourceProvider provider;
  ExpectUnsupportedURI(provider.ReadResource("lldb://x"));
}

TEST_F(MCPPluginTest, ReadResourceNotDebugger) {
  DebuggerResourceProvider provider;
  ExpectUnsupportedURI(provider.ReadResource("lldb://session/0"));
}

TEST_F(MCPPluginTest, ReadResourceInvalidDebuggerId) {
  DebuggerResourceProvider provider;
  EXPECT_THAT_EXPECTED(
      provider.ReadResource("lldb://debugger/abc"),
      FailedWithMessage("invalid debugger id 'abc': debugger/abc"));
}

TEST_F(MCPPluginTest, ReadResourceUnknownDebugger) {
  DebuggerResourceProvider provider;
  EXPECT_THAT_EXPECTED(provider.ReadResource("lldb://debugger/999999"),
                       FailedWithMessage("invalid debugger id: 999999"));
}

TEST_F(MCPPluginTest, ReadResourceDebugger) {
  CreateTarget();

  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}", m_debugger_sp->GetID()).str();
  Expected<ReadResourceResult> result = provider.ReadResource(uri);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  ASSERT_EQ(result->contents.size(), 1u);
  EXPECT_EQ(result->contents[0].uri, uri);
  EXPECT_EQ(result->contents[0].mimeType, "application/json");

  Expected<json::Value> json = json::parse(result->contents[0].text);
  ASSERT_THAT_EXPECTED(json, Succeeded());
  const json::Object *obj = json->getAsObject();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->getInteger("debugger_id"),
            static_cast<int64_t>(m_debugger_sp->GetID()));
  EXPECT_EQ(obj->getInteger("num_targets"), 1);
}

TEST_F(MCPPluginTest, ReadResourceTargetNotTarget) {
  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}/session/0", m_debugger_sp->GetID()).str();
  ExpectUnsupportedURI(provider.ReadResource(uri));
}

TEST_F(MCPPluginTest, ReadResourceInvalidTargetId) {
  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}/target/abc", m_debugger_sp->GetID()).str();
  std::string path =
      formatv("debugger/{0}/target/abc", m_debugger_sp->GetID()).str();
  EXPECT_THAT_EXPECTED(
      provider.ReadResource(uri),
      FailedWithMessage(formatv("invalid target id 'abc': {0}", path).str()));
}

TEST_F(MCPPluginTest, ReadResourceTargetUnknownDebugger) {
  DebuggerResourceProvider provider;
  EXPECT_THAT_EXPECTED(provider.ReadResource("lldb://debugger/999999/target/0"),
                       FailedWithMessage("invalid debugger id: 999999"));
}

TEST_F(MCPPluginTest, ReadResourceUnknownTarget) {
  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}/target/999", m_debugger_sp->GetID()).str();
  EXPECT_THAT_EXPECTED(provider.ReadResource(uri),
                       FailedWithMessage("invalid target idx: 999"));
}

TEST_F(MCPPluginTest, ReadResourceTarget) {
  CreateTarget();

  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}/target/0", m_debugger_sp->GetID()).str();
  Expected<ReadResourceResult> result = provider.ReadResource(uri);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  ASSERT_EQ(result->contents.size(), 1u);

  Expected<json::Value> json = json::parse(result->contents[0].text);
  ASSERT_THAT_EXPECTED(json, Succeeded());
  const json::Object *obj = json->getAsObject();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->getInteger("debugger_id"),
            static_cast<int64_t>(m_debugger_sp->GetID()));
  EXPECT_EQ(obj->getInteger("target_idx"), 0);
  // No executable module: there is no "path" entry.
  EXPECT_EQ(obj->get("path"), nullptr);
}

TEST_F(MCPPluginTest, ReadResourceTargetWithExecutable) {
  CreateTargetWithExecutable("/tmp/lldb-mcp-test/my_executable");

  DebuggerResourceProvider provider;
  std::string uri =
      formatv("lldb://debugger/{0}/target/0", m_debugger_sp->GetID()).str();
  Expected<ReadResourceResult> result = provider.ReadResource(uri);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  ASSERT_EQ(result->contents.size(), 1u);

  Expected<json::Value> json = json::parse(result->contents[0].text);
  ASSERT_THAT_EXPECTED(json, Succeeded());
  const json::Object *obj = json->getAsObject();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->getString("path"), "/tmp/lldb-mcp-test/my_executable");
}

//===----------------------------------------------------------------------===//
// CommandTool
//===----------------------------------------------------------------------===//

TEST_F(MCPPluginTest, CommandToolRequiresArguments) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args; // std::monostate
  EXPECT_THAT_EXPECTED(tool.Call(args),
                       FailedWithMessage("CommandTool requires arguments"));
}

TEST_F(MCPPluginTest, CommandToolInvalidArguments) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(42);
  EXPECT_THAT_EXPECTED(tool.Call(args), Failed());
}

TEST_F(MCPPluginTest, CommandToolMalformedDebugger) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(
      json::Object{{"debugger", "notanumber"}, {"command", "version"}});
  EXPECT_THAT_EXPECTED(
      tool.Call(args),
      FailedWithMessage("malformed debugger specifier notanumber"));
}

TEST_F(MCPPluginTest, CommandToolNoDebuggerFound) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args =
      json::Value(json::Object{{"debugger", "999999"}, {"command", "version"}});
  EXPECT_THAT_EXPECTED(tool.Call(args), FailedWithMessage("no debugger found"));
}

TEST_F(MCPPluginTest, CommandToolRunsCommandForDebuggerId) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(
      json::Object{{"debugger", std::to_string(m_debugger_sp->GetID())},
                   {"command", "version"}});

  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_FALSE(result->isError);
  ASSERT_EQ(result->content.size(), 1u);
  EXPECT_THAT(result->content[0].text, testing::HasSubstr("lldb"));
}

TEST_F(MCPPluginTest, CommandToolRunsCommandForDebuggerURI) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(json::Object{
      {"debugger",
       formatv("lldb-mcp://debugger/{0}", m_debugger_sp->GetID()).str()},
      {"command", "version"}});

  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_FALSE(result->isError);
}

TEST_F(MCPPluginTest, CommandToolUsesFirstDebuggerWhenUnspecified) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(json::Object{{"command", "version"}});

  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_FALSE(result->isError);
}

TEST_F(MCPPluginTest, CommandToolReportsCommandError) {
  CommandTool tool("command", "Run an lldb command.");
  ToolArguments args = json::Value(
      json::Object{{"debugger", std::to_string(m_debugger_sp->GetID())},
                   {"command", "this-is-not-a-real-command"}});

  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_TRUE(result->isError);
  ASSERT_EQ(result->content.size(), 1u);
  EXPECT_FALSE(result->content[0].text.empty());
}

TEST_F(MCPPluginTest, CommandToolSchema) {
  CommandTool tool("command", "Run an lldb command.");
  std::optional<json::Value> schema = tool.GetSchema();
  ASSERT_TRUE(schema.has_value());
  const json::Object *obj = schema->getAsObject();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->getString("type"), "object");
  EXPECT_NE(obj->getObject("properties"), nullptr);
}

//===----------------------------------------------------------------------===//
// DebuggerListTool
//===----------------------------------------------------------------------===//

TEST_F(MCPPluginTest, DebuggerListTool) {
  DebuggerListTool tool("debugger_list", "List debugger instances.");
  ToolArguments args;
  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_FALSE(result->isError);
  ASSERT_EQ(result->content.size(), 1u);
  EXPECT_THAT(
      result->content[0].text,
      testing::HasSubstr(
          formatv("lldb-mcp://debugger/{0}", m_debugger_sp->GetID()).str()));
}

//===----------------------------------------------------------------------===//
// DebuggerCreateTool / DebuggerDeleteTool / DebuggerManager
//===----------------------------------------------------------------------===//

TEST_F(MCPPluginTest, DebuggerCreateTool) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerCreateTool tool("debugger_create", "Create a debugger.", manager);

  const size_t num_before = Debugger::GetNumDebuggers();
  ToolArguments args; // std::monostate: the tool takes no arguments.
  Expected<CallToolResult> result = tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_FALSE(result->isError);
  ASSERT_EQ(result->content.size(), 1u);
  EXPECT_THAT(result->content[0].text,
              testing::StartsWith("lldb-mcp://debugger/"));
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before + 1);
}

TEST_F(MCPPluginTest, DebuggerCreateToolResultUsableByCommandTool) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerCreateTool create_tool("debugger_create", "Create a debugger.",
                                 manager);
  ToolArguments create_args;
  Expected<CallToolResult> create_result = create_tool.Call(create_args);
  ASSERT_THAT_EXPECTED(create_result, Succeeded());
  ASSERT_EQ(create_result->content.size(), 1u);
  const std::string uri = create_result->content[0].text;

  // The URI returned by debugger_create can be handed straight to the command
  // tool's "debugger" argument.
  CommandTool command_tool("command", "Run an lldb command.");
  ToolArguments command_args =
      json::Value(json::Object{{"debugger", uri}, {"command", "version"}});
  Expected<CallToolResult> command_result = command_tool.Call(command_args);
  ASSERT_THAT_EXPECTED(command_result, Succeeded());
  EXPECT_FALSE(command_result->isError);
}

TEST_F(MCPPluginTest, DebuggerDeleteTool) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerCreateTool create_tool("debugger_create", "Create a debugger.",
                                 manager);
  DebuggerDeleteTool delete_tool("debugger_delete", "Delete a debugger.",
                                 manager);

  const size_t num_before = Debugger::GetNumDebuggers();

  ToolArguments create_args;
  Expected<CallToolResult> create_result = create_tool.Call(create_args);
  ASSERT_THAT_EXPECTED(create_result, Succeeded());
  ASSERT_EQ(create_result->content.size(), 1u);
  const std::string uri = create_result->content[0].text;
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before + 1);

  ToolArguments delete_args = json::Value(json::Object{{"debugger", uri}});
  Expected<CallToolResult> delete_result = delete_tool.Call(delete_args);
  ASSERT_THAT_EXPECTED(delete_result, Succeeded());
  EXPECT_FALSE(delete_result->isError);
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before);

  // Deleting the same debugger again is reported as a tool error, not a crash.
  Expected<CallToolResult> second_delete = delete_tool.Call(delete_args);
  ASSERT_THAT_EXPECTED(second_delete, Succeeded());
  EXPECT_TRUE(second_delete->isError);
}

TEST_F(MCPPluginTest, DebuggerDeleteToolRefusesUnmanagedDebugger) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerDeleteTool delete_tool("debugger_delete", "Delete a debugger.",
                                 manager);

  // The fixture's debugger was not created through MCP, so an MCP client must
  // not be able to delete it.
  ToolArguments args = json::Value(
      json::Object{{"debugger", std::to_string(m_debugger_sp->GetID())}});
  Expected<CallToolResult> result = delete_tool.Call(args);
  ASSERT_THAT_EXPECTED(result, Succeeded());
  EXPECT_TRUE(result->isError);
  ASSERT_EQ(result->content.size(), 1u);
  EXPECT_THAT(result->content[0].text,
              testing::HasSubstr("not created by MCP"));
  EXPECT_TRUE(
      static_cast<bool>(Debugger::FindDebuggerWithID(m_debugger_sp->GetID())));
}

TEST_F(MCPPluginTest, DebuggerDeleteToolRequiresArguments) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerDeleteTool delete_tool("debugger_delete", "Delete a debugger.",
                                 manager);
  ToolArguments args; // std::monostate
  EXPECT_THAT_EXPECTED(
      delete_tool.Call(args),
      FailedWithMessage("DebuggerDeleteTool requires arguments"));
}

TEST_F(MCPPluginTest, DebuggerDeleteToolMalformedDebugger) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerDeleteTool delete_tool("debugger_delete", "Delete a debugger.",
                                 manager);
  ToolArguments args = json::Value(json::Object{{"debugger", "notanumber"}});
  EXPECT_THAT_EXPECTED(
      delete_tool.Call(args),
      FailedWithMessage("malformed debugger specifier notanumber"));
}

TEST_F(MCPPluginTest, DebuggerDeleteToolRejectsTrailingJunk) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerDeleteTool delete_tool("debugger_delete", "Delete a debugger.",
                                 manager);
  // A target uri must not be silently truncated to the debugger id and delete
  // the whole debugger.
  ToolArguments args =
      json::Value(json::Object{{"debugger", "lldb-mcp://debugger/1/target/0"}});
  EXPECT_THAT_EXPECTED(delete_tool.Call(args),
                       FailedWithMessage("malformed debugger specifier "
                                         "lldb-mcp://debugger/1/target/0"));
}

TEST_F(MCPPluginTest, DebuggerDeleteToolSchema) {
  auto manager = std::make_shared<DebuggerManager>();
  DebuggerDeleteTool tool("debugger_delete", "Delete a debugger.", manager);
  std::optional<json::Value> schema = tool.GetSchema();
  ASSERT_TRUE(schema.has_value());
  const json::Object *obj = schema->getAsObject();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->getString("type"), "object");
  EXPECT_NE(obj->getObject("properties"), nullptr);
}

TEST_F(MCPPluginTest, DebuggerManagerCreateAndDestroyAll) {
  const size_t num_before = Debugger::GetNumDebuggers();
  DebuggerManager manager;

  Expected<DebuggerSP> first = manager.CreateDebugger();
  ASSERT_THAT_EXPECTED(first, Succeeded());
  Expected<DebuggerSP> second = manager.CreateDebugger();
  ASSERT_THAT_EXPECTED(second, Succeeded());
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before + 2);

  manager.DestroyAll();
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before);
}

TEST_F(MCPPluginTest, DebuggerManagerReclaimsOnDestruction) {
  const size_t num_before = Debugger::GetNumDebuggers();
  {
    DebuggerManager manager;
    ASSERT_THAT_EXPECTED(manager.CreateDebugger(), Succeeded());
    ASSERT_THAT_EXPECTED(manager.CreateDebugger(), Succeeded());
    EXPECT_EQ(Debugger::GetNumDebuggers(), num_before + 2);
    // Intentionally do not call DestroyAll. Destroying the manager (the same
    // teardown ProtocolServerMCP::Stop relies on) must reclaim the debuggers.
  }
  EXPECT_EQ(Debugger::GetNumDebuggers(), num_before);
}

//===----------------------------------------------------------------------===//
// ProtocolServerMCP
//===----------------------------------------------------------------------===//

namespace {
/// Exposes the protected Extend hook for testing.
class ExtendableProtocolServerMCP : public ProtocolServerMCP {
public:
  using ProtocolServerMCP::Extend;
};
} // namespace

TEST_F(MCPPluginTest, ProtocolServerExtend) {
  ExtendableProtocolServerMCP server;
  lldb_protocol::mcp::Server mcp_server("lldb-mcp", "0.1.0");
  // Extend registers the "command", "debugger_list", "debugger_create" and
  // "debugger_delete" tools and the debugger resource provider on the server.
  server.Extend(mcp_server);
}

TEST_F(MCPPluginTest, ProtocolServerStaticPluginInfo) {
  EXPECT_EQ(ProtocolServerMCP::GetPluginNameStatic(), "MCP");
  EXPECT_EQ(ProtocolServerMCP::GetPluginDescriptionStatic(), "MCP Server.");
}

TEST_F(MCPPluginTest, ProtocolServerCreateInstance) {
  ProtocolServerUP server = ProtocolServerMCP::CreateInstance();
  ASSERT_TRUE(server);
  EXPECT_EQ(server->GetPluginName(), "MCP");
  EXPECT_EQ(server->GetSocket(), nullptr);
}

TEST_F(MCPPluginTest, ProtocolServerInitializeAndTerminate) {
  ProtocolServerMCP::Initialize();

  ProtocolServer *server = ProtocolServer::GetOrCreate("MCP");
  EXPECT_NE(server, nullptr);

  std::vector<StringRef> protocols = ProtocolServer::GetSupportedProtocols();
  EXPECT_THAT(protocols, testing::Contains(StringRef("MCP")));

  ProtocolServerMCP::Terminate();
}

TEST_F(MCPPluginTest, ProtocolServerStopNotRunning) {
  ProtocolServerUP server = ProtocolServerMCP::CreateInstance();
  ASSERT_TRUE(server);
  // Stopping a server that was never started is an error.
  EXPECT_THAT_ERROR(server->Stop(), Failed());
}

// NOTE: ProtocolServerMCP::Start (and therefore AcceptCallback) binds a
// listening socket and writes the server info into the real `~/.lldb`
// directory, so it can't be exercised hermetically yet. It is covered by the
// API test (TestMCPUnixSocket.py) and should get unit coverage once the
// socket and filesystem layers can be mocked.

#endif
