import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestPtrAuthFixups(TestBase):
    @skipUnlessDarwin
    @skipUnlessArch("arm64e")
    def test_static_function_pointer(self):
        """On arm64e, function pointers are automatically signed (PAC).
        Test that we can call a function through a function pointer from the
        expression evaluator, which requires "fixing up" the pointer signing."""
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
