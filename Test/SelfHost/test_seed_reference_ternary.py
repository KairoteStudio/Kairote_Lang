"""The C bootstrap seed must preserve the declared carrier of ternary arms."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC


@unittest.skipUnless(KRTC.is_file(), "build the C seed first")
class SeedReferenceTernaryTests(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="kairote-seed-reference-ternary-") as directory:
            work = Path(directory)
            code, program = work / "program.krt", work / "program"
            code.write_text(source)
            for level in (0, 2, 3):
                with self.subTest(level=level):
                    compiled = subprocess.run(
                        [str(KRTC), f"-O{level}", str(code), "output", str(program)],
                        cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    executed = subprocess.run([str(program)], capture_output=True, timeout=10)
                    self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_member_nested_new_this_and_null_reference_arms_keep_aliases(self):
        self.compile_run("""
class Item {
    public int64 value;
    public Item left;
    public Item right;
    Item ChooseSelf(bool first) { return first ? this : left; }
}
Item Pick(bool first, Item tree) { return first ? tree.left : tree.right; }
Item Mixed(bool first, Item tree) { return first ? tree : tree.left; }
Item Nested(bool first, bool second, Item tree) {
    return first ? (second ? tree.left : tree.right) : tree;
}
Item Fresh(bool first, Item tree) { return first ? new Item() : tree.left; }
Item Maybe(bool first, Item tree) { return first ? tree.left : null; }
Item ReverseMaybe(bool first, Item tree) { return first ? null : tree.right; }
int32 main() {
    Item root = new Item(); root.left = new Item(); root.right = new Item();
    root.value = 11; root.left.value = 17; root.right.value = 25;
    if (Pick(true, root) != root.left || Pick(false, root) != root.right) { return 1; }
    if (Mixed(true, root) != root || Mixed(false, root) != root.left) { return 2; }
    if (Nested(true, true, root) != root.left || Nested(true, false, root) != root.right ||
        Nested(false, true, root) != root) { return 3; }
    if (Fresh(true, root).value != 0 || Fresh(false, root) != root.left) { return 4; }
    if (root.ChooseSelf(true) != root || root.ChooseSelf(false) != root.left) { return 5; }
    if (Maybe(true, root) != root.left || Maybe(false, root) != null ||
        ReverseMaybe(true, root) != null || ReverseMaybe(false, root) != root.right) { return 6; }
    Item selected = Pick(false, root); selected.value = 4294967351;
    if (root.right.value != 4294967351) { return 7; }
    return 0;
}
""")

    def test_integer_and_floating_member_arms_keep_their_carriers(self):
        self.compile_run("""
enum Choice { First=17, Second=25 }
class Numbers {
    public uint64 unsigned_value;
    public int64 signed_value;
    public float32 narrow;
    public float64 wide;
    public Choice choice;
}
uint64 Unsigned(bool first, Numbers a, Numbers b) {
    return first ? a.unsigned_value : b.unsigned_value;
}
int64 Signed(bool first, Numbers a, Numbers b) { return first ? a.signed_value : b.signed_value; }
float32 Narrow(bool first, Numbers a, Numbers b) { return first ? a.narrow : b.narrow; }
float64 Wide(bool first, Numbers a, Numbers b) { return first ? a.wide : b.wide; }
float64 Mixed(bool first, Numbers a) { return first ? a.narrow : a.wide; }
Choice NamedScalar(bool first, Numbers a, Numbers b) { return first ? a.choice : b.choice; }
int32 main() {
    Numbers a = new Numbers(); Numbers b = new Numbers();
    a.unsigned_value = 9223372036854775901; b.unsigned_value = 4294967353;
    a.signed_value = -4294967351; b.signed_value = 4294967355;
    a.narrow = 1.25; b.narrow = -2.5; a.wide = 7.75; b.wide = -9.125;
    a.choice = Choice.First; b.choice = Choice.Second;
    if (Unsigned(true, a, b) != 9223372036854775901 || Unsigned(false, a, b) != 4294967353) { return 1; }
    if (Signed(true, a, b) != -4294967351 || Signed(false, a, b) != 4294967355) { return 2; }
    if (Narrow(true, a, b) != 1.25 || Narrow(false, a, b) != -2.5) { return 3; }
    if (Wide(true, a, b) != 7.75 || Wide(false, a, b) != -9.125) { return 4; }
    if (Mixed(true, a) != 1.25 || Mixed(false, a) != 7.75) { return 5; }
    if (NamedScalar(true, a, b) != Choice.First || NamedScalar(false, a, b) != Choice.Second) { return 6; }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
