""" Testing symbol loading via JSON file. """
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TargetSymbolsFileJSON(TestBase):

    def setUp(self):
        TestBase.setUp(self)
        self.source = 'main.c'

    @no_debug_info_test  # Prevent the genaration of the dwarf version of this test
    def test_symbol_file_json(self):
        """Test that 'target symbols add' can load the symbols from a JSON file."""

        self.build()
        stripped = self.getBuildArtifact("stripped.out")
        unstripped = self.getBuildArtifact("a.out")

        # Create a JSON symbol file from the unstripped target.
        unstripped_target = self.dbg.CreateTarget(unstripped)
        self.assertTrue(unstripped_target, VALID_TARGET)

        unstripped_module = unstripped_target.GetModuleAtIndex(0)

        data = {
            "triple": unstripped_module.GetTriple(),
            "uuid": unstripped_module.GetUUIDString(),
            "symbols": list()
        }
        data['symbols'].append({
            "name":
            "main",
            "addr":
            unstripped_module.FindSymbol("main").addr.GetFileAddress()
        })
        data['symbols'].append({
            "name":
            "foo",
            "addr":
            unstripped_module.FindSymbol("foo").addr.GetFileAddress()
        })

        json_object = json.dumps(data, indent=4)
        json_symbol_file = self.getBuildArtifact("a.json")
        with open(json_symbol_file, "w") as outfile:
            outfile.write(json_object)

        # Create a stripped target.
        stripped_target = self.dbg.CreateTarget(stripped)
        self.assertTrue(stripped_target, VALID_TARGET)

        # Ensure there's no symbol for main and foo.
        stripped_module = stripped_target.GetModuleAtIndex(0)
        self.assertFalse(stripped_module.FindSymbol("main").IsValid())
        self.assertFalse(stripped_module.FindSymbol("foo").IsValid())

        main_bp = stripped_target.BreakpointCreateByName(
            "main", "stripped.out")
        self.assertTrue(main_bp, VALID_BREAKPOINT)
        self.assertEqual(main_bp.num_locations, 0)

        # Load the JSON symbol file.
        self.runCmd("target symbols add -s %s %s" %
                    (stripped, self.getBuildArtifact("a.json")))

        # Ensure main and foo are available now.
        self.assertTrue(stripped_module.FindSymbol("main").IsValid())
        self.assertTrue(stripped_module.FindSymbol("foo").IsValid())
        self.assertEqual(main_bp.num_locations, 1)

        # Ensure the file address matches between the stripped and unstripped target.
        self.assertEqual(
            stripped_module.FindSymbol("main").addr.GetFileAddress(),
            unstripped_module.FindSymbol("main").addr.GetFileAddress())
        self.assertEqual(
            stripped_module.FindSymbol("foo").addr.GetFileAddress(),
            unstripped_module.FindSymbol("foo").addr.GetFileAddress())
