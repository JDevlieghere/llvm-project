# Model Context Protocol (MCP)

LLDB supports the [Model Context Protocol](https://modelcontextprotocol.io)
(MCP). This structured, machine-friendly protocol allows AI models to access
and interact with external tools, for example debuggers. Using MCP, an AI agent
can execute LLDB commands to control the debugger: set breakpoints, inspect
memory, step through code. This can range from helping you run a specific
command you cannot immediately remember, to a fully agent-driven debugging
experience.

## MCP Server

To start the MCP server in LLDB, use the `protocol-server start` command.
Specify `MCP` as the protocol and provide a URI to listen on. For example, to
start listening for local TCP connections on port `59999`, use the following
command:

```
(lldb) protocol-server start MCP listen://localhost:59999
MCP server started with connection listeners: connection://[::1]:59999, connection://[127.0.0.1]:59999
```

The server will automatically stop when exiting LLDB, or it can be stopped
explicitly with the `protocol-server stop` command.

```
(lldb) protocol-server stop MCP
```

The commands will fail if a server is already running or not running
respectively.

## MCP Client

MCP uses standard input/output (stdio) for communication between client and
server. The exact configuration depends on the client, but most applications
allow you to specify an MCP server as a binary and arguments. LLDB ships with
`lldb-mcp`, a small helper that bridges stdio to LLDB's MCP server socket.

```
┌──────────┐               ┌──────────┐               ┌──────────┐
│          │               │          │               │          │
│   LLDB   ├─────socket────┤ lldb-mcp ├─────stdio─────┤MCP Client│
│          │               │          │               │          │
└──────────┘               └──────────┘               └──────────┘
```

`lldb-mcp` automatically discovers a running LLDB MCP server, so there is no
need to specify a port. If no server is running, it will launch `lldb` in the
background and connect to it. The `lldb` binary located next to `lldb-mcp` is
used by default; set the `LLDB_EXE_PATH` environment variable to override this.

Configuration example for [Claude Code](https://modelcontextprotocol.io/quickstart/user):

```
claude mcp add --transport stdio -- lldb-mcp /path/to/lldb-mcp
```

Configuration example (`mcp.json`) for [Visual Studio Code](https://code.visualstudio.com/docs/copilot/chat/mcp-servers):

```json
{
  "servers": {
    "lldb": {
      "type": "stdio",
      "command": "/path/to/lldb-mcp"
    }
  }
}
```

## Tools

Tools are a primitive in the Model Context Protocol that enable servers to
expose functionality to clients.

LLDB's MCP integration exposes the following tools:

- `command` runs an LLDB command, just as a user would type it in the command
  interpreter. It takes the command and its arguments as a string and,
  optionally, the debugger ID or URI of the session to run it in. When no
  debugger is specified, the first one is used.
- `debugger_list` lists the active debugger instances and their URIs.
- `debugger_create` creates a new debugger instance and returns its URI. This
  lets an agent provision its own debug session, which is especially useful when
  `lldb-mcp` launched LLDB on its behalf.
- `debugger_delete` deletes a debugger instance. To protect a user's own
  sessions, only debuggers created through `debugger_create` can be deleted.

## Resources

Resources are a primitive in the Model Context Protocol that allow servers to
expose content that can be read by clients.

LLDB's MCP integration exposes a resource for each debugger and target
instance. Debugger resources are accessible using the following URI:

```
lldb://debugger/<debugger id>
```

Example output:

```json
{
  "contents": [
    {
      "uri": "lldb://debugger/1",
      "mimeType": "application/json",
      "text": "{\"debugger_id\":1,\"name\":\"debugger_1\",\"num_targets\":1}"
    }
  ]
}
```

Debuggers can contain one or more targets, which are accessible using the
following URI:

```
lldb://debugger/<debugger id>/target/<target idx>
```

Example output:

```json
{
  "contents": [
    {
      "uri": "lldb://debugger/1/target/0",
      "mimeType": "application/json",
      "text": "{\"arch\":\"arm64-apple-macosx26.0.0\",\"debugger_id\":1,\"dummy\":false,\"path\":\"/bin/count\",\"platform\":\"host\",\"selected\":true,\"target_idx\":0}"
    }
  ]
}
```

Note that unlike the debugger id, which is unique, the target index is not
stable and may be reused when a target is removed and a new target is added.

## Troubleshooting

The MCP server uses the `Host` log channel. You can enable logging with the
`log enable` command.

```
(lldb) log enable lldb host
```
