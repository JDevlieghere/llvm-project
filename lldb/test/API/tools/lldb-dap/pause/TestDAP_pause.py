"""
Test stop hooks
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import line_number
from lldbsuite.test.lldbtest import *
import lldbdap_testcase


class TestDAP_pause(lldbdap_testcase.DAPTestCaseBase):
    def test_pause(self):
        """Test that we can pause the process."""
        program = self.getBuildArtifact("a.out")
        self.build_and_launch(program)

        self.dap_server.request_continue()
        self.assertTrue(self.dap_server.request_pause()["success"])

        stopped_events = self.dap_server.wait_for_stopped()
        for stopped_event in stopped_events:
            if "body" in stopped_event:
                body = stopped_event["body"]
                if "reason" in body:
                    reason = body["reason"]
                    self.assertEqual(reason, "exception")
