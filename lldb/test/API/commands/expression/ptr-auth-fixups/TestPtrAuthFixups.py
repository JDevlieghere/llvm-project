import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestPtrAuthFixups(TestBase):
    @skipUnlessDarwin
    @skipUnlessArch("arm64")
    def test_static_function_pointer(self):
        """Test that a static function pointer initialized in an expression
        gets correctly signed on arm64e via the pointer signing fixup pass."""
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.c", False)
        )

        self.expect_expr(
            "static int (*fp)(int, int) = &add; fp(5, 6);",
            result_type="int",
            result_value="11",
        )

        self.expect_expr(
            "static int (*fp)(int, int) = &mul; fp(4, 5);",
            result_type="int",
            result_value="20",
        )
