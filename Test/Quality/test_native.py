"""Check branch lowering without sacrificing signed, unsigned or floating comparisons."""
from pathlib import Path
import sys
import operator
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Performance"))
import test_compiler_hotspots as hotspots


class NativeQuality(unittest.TestCase):
    def test_generated_heap_alignment_and_array_length(self):
        hotspots.NativeCodeSize().compile_run("""
int32 main() {
    unsafe(using krt.mem;) {
        int128[] wide = new int128[4];
        int128* p = (int128*)wide;
        if ((usize)p % 16 != 0) { return 1; }
        *p = (int128)1 << 100;
        if (*p != (int128)1 << 100) { return 2; }
        int32[] values = [3, 4, 5];
        if ((usize)values % 16 != 0) { return 3; }
        int32 sum = 0;
        foreach (int32 value in values) { sum += value; }
        if (sum != 12) { return 4; }
        int32[] empty = new int32[0];
        foreach (int32 value in empty) { return 5; }
    }
    return 0;
}
""")

    def test_comparisons_and_fallthrough(self):
        functions, checks = [], []
        for token, values in [("int32", [-2147483648, -1, 0, 1, 2147483647]),
                              ("uint32", [0, 1, 2147483648, 4294967295])]:
            for index, (symbol, compare) in enumerate([("<", operator.lt), ("<=", operator.le), ("==", operator.eq),
                                                     ("!=", operator.ne), (">=", operator.ge), (">", operator.gt)]):
                name = f"Compare{token}{index}"
                functions.append(f"int32 {name}({token} a, {token} b) {{ if (a {symbol} b) {{ return 17; }} return 23; }}")
                for left in values:
                    for right in values:
                        expected = 17 if compare(left, right) else 23
                        checks.append(f"if ({name}(({token}){left}, ({token}){right}) != {expected}) {{ return 1; }}")
        source = "\n".join(functions) + "\nint32 main() {\n" + "\n".join(checks) + "\nreturn 0; }"
        assembly = hotspots.NativeCodeSize().compile_run(source)
        functions_body = assembly.split("<main>:", 1)[0]
        self.assertNotRegex(functions_body, r"\bset(?:l|le|e|ne|ge|g|b|be|ae|a)\s")

    def test_materialized_booleans_wide_and_nan_keep_semantics(self):
        hotspots.NativeCodeSize().compile_run("""
int32 Probe(int128 a, int128 b) {
    bool result = a < b;
    int32 score = result ? 4 : 7;
    if (result) { score += 5; }
    return score;
}
int32 FloatProbe(float64 a, float64 b) {
    if (a != b) { return 1; }
    return 0;
}
int32 main() {
    int128 high = (int128)1 << 100;
    if (Probe(high, high + 1) != 9 || Probe(high + 1, high) != 7) { return 1; }
    float64 zero = 0.0;
    float64 nan = zero / zero;
    if (FloatProbe(nan, nan) != 1 || FloatProbe(0.0, -0.0) != 0) { return 2; }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
