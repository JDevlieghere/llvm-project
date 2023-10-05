import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from lldbsuite.test.gdbclientutils import *
from lldbsuite.test.lldbgdbclient import GDBRemoteTestBase

class TestPlatformListProcesses(GDBRemoteTestBase):
    @skipIfRemote
    @skipIfWindows
    def test_attach(self):
        """Test listing processes"""

        class MyPlatformResponder(MockGDBServerResponder):
            def __init__(self, port):
                MockGDBServerResponder.__init__(self)
                self.port = port

            def qLaunchGDBServer(self, _):
                return "pid:1337;port:{};".format(self.port)

            def qfProcessInfo(self, packet):
                print(packet)
                return "pid:95117;name:666f6f;"

        class MyGDBResponder(MockGDBServerResponder):
            def __init__(self):
                MockGDBServerResponder.__init__(self)

            def vAttach(self, _):
                return "OK"

        self.server.responder = MyGDBResponder()
        port = self.server._socket._server_socket.getsockname()[1]

        platform_socket = TCPServerSocket()
        platform_server = MockGDBServer(platform_socket)
        platform_server.responder = MyPlatformResponder(port)
        platform_server.start()

        error = lldb.SBError()
        platform = lldb.SBPlatform("remote-linux")
        self.dbg.SetSelectedPlatform(platform)

        error = platform.ConnectRemote(
            lldb.SBPlatformConnectOptions(platform_server.get_connect_url())
        )
        self.assertSuccess(error)
        self.assertTrue(platform.IsConnected())

        processes = platform.GetAllProcesses(error)
        self.assertSuccess(error)

        process_info = lldb.SBProcessInfo()
        processes.GetProcessInfoAtIndex(0, process_info)

        self.assertEqual(process_info.GetProcessID(), 95117)

        platform.DisconnectRemote()
