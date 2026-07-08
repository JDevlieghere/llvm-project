import json
import os
import socket
import tempfile
import unittest

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *

# To be safe and portable, Unix domain socket paths should be kept at or below
# 108 characters on Linux, and around 104 characters on macOS:
MAX_SOCKET_PATH_LENGTH = 104


class MCPClient:
    """A minimal MCP JSON-RPC client speaking newline-delimited messages."""

    def __init__(self, socket_file):
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(30)
        self._sock.connect(socket_file)
        self._buffer = b""
        self._id = 0

    def close(self):
        self._sock.close()

    def request(self, method, params=None):
        self._id += 1
        message = {"jsonrpc": "2.0", "id": self._id, "method": method}
        if params is not None:
            message["params"] = params
        self._sock.sendall((json.dumps(message) + "\n").encode())
        while b"\n" not in self._buffer:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise EOFError("MCP server closed the connection")
            self._buffer += chunk
        line, self._buffer = self._buffer.split(b"\n", 1)
        return json.loads(line)

    def call_tool(self, name, arguments):
        """Returns (text, is_error) for a tools/call, asserting no RPC error."""
        response = self.request("tools/call", {"name": name, "arguments": arguments})
        assert "error" not in response, response
        result = response["result"]
        text = "\n".join(c.get("text", "") for c in result.get("content", []))
        return text, result.get("isError", False)


class MCPUnixSocketCommandTestCase(TestBase):
    @skipIfWindows
    @skipIfRemote
    @no_debug_info_test
    def test_unix_socket(self):
        """
        Test if we can start an MCP protocol-server accepting unix sockets
        """

        temp_directory = tempfile.TemporaryDirectory()
        socket_file = os.path.join(temp_directory.name, "mcp.sock")

        if len(socket_file) >= MAX_SOCKET_PATH_LENGTH:
            self.skipTest(
                f"Socket path {socket_file} exceeds the {MAX_SOCKET_PATH_LENGTH} character limit"
            )

        self.expect(
            f"protocol-server start MCP accept://{socket_file}",
            startstr="MCP server started with connection listeners:",
            substrs=[f"unix-connect://{socket_file}"],
        )

        self.expect(
            "protocol-server get MCP",
            startstr="MCP server connection listeners:",
            substrs=[f"unix-connect://{socket_file}"],
        )

        self.runCmd("protocol-server stop MCP", check=False)
        self.expect(
            "protocol-server get MCP",
            error=True,
            substrs=["MCP server is not running"],
        )

    @skipIfWindows
    @skipIfRemote
    @no_debug_info_test
    def test_debugger_session_tools(self):
        """
        Test creating and deleting debugger sessions through MCP, and that a
        client cannot delete a debugger it did not create.
        """

        temp_directory = tempfile.TemporaryDirectory()
        socket_file = os.path.join(temp_directory.name, "mcp.sock")

        if len(socket_file) >= MAX_SOCKET_PATH_LENGTH:
            self.skipTest(
                f"Socket path {socket_file} exceeds the {MAX_SOCKET_PATH_LENGTH} character limit"
            )

        self.runCmd(f"protocol-server start MCP accept://{socket_file}")
        self.addTearDownHook(
            lambda: self.runCmd("protocol-server stop MCP", check=False)
        )

        client = MCPClient(socket_file)
        self.addTearDownHook(lambda: client.close())

        client.request(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "lldb-api-test", "version": "0.1.0"},
            },
        )

        # The test's own debugger is the host: it must never be deletable.
        host_uri = f"lldb-mcp://debugger/{self.dbg.GetID()}"

        # Create a new session and confirm it is reported by debugger_list.
        created_uri, is_error = client.call_tool("debugger_create", {})
        self.assertFalse(is_error, created_uri)
        self.assertTrue(created_uri.startswith("lldb-mcp://debugger/"), created_uri)

        listing, is_error = client.call_tool("debugger_list", {})
        self.assertFalse(is_error)
        self.assertIn(created_uri, listing)

        # The created session is usable through the command tool.
        version, is_error = client.call_tool(
            "command", {"debugger": created_uri, "command": "version"}
        )
        self.assertFalse(is_error, version)
        self.assertIn("lldb", version)

        # Deleting the host debugger is refused.
        message, is_error = client.call_tool("debugger_delete", {"debugger": host_uri})
        self.assertTrue(is_error, message)
        self.assertIn("not created by MCP", message)

        listing, _ = client.call_tool("debugger_list", {})
        self.assertIn(host_uri, listing)

        # Deleting the MCP-created session succeeds and removes it.
        message, is_error = client.call_tool(
            "debugger_delete", {"debugger": created_uri}
        )
        self.assertFalse(is_error, message)

        listing, _ = client.call_tool("debugger_list", {})
        self.assertNotIn(created_uri, listing)

        # Deleting it again is reported as a tool error, not a crash.
        message, is_error = client.call_tool(
            "debugger_delete", {"debugger": created_uri}
        )
        self.assertTrue(is_error, message)
