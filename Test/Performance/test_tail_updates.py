"""Dependency-ordered tail parameter writes preserve simultaneous assignment."""
import unittest
import test_register_optimization as registers


class TailParameterUpdates(unittest.TestCase):
    run_source = registers.RegisterOptimization.run_source
    function_body = registers.RegisterOptimization.function_body

    def test_accumulation_updates_eliminate_argument_snapshots(self):
        assembly = self.run_source("""
uint64 Sum(uint64 n, uint64 value) {
    if (n == 0) { return value; } return Sum(n-1,value+n);
}
int32 main() {
    if (Sum(0,3) != 3 || Sum(10,3) != 58 || Sum(13,3) != 94) { return 1; }
    return Sum(10000,0) == 50005000 ? 0 : 2;
}
""")
        self.assertRegex(self.function_body(assembly[2], "Sum"), r"\br1[34]\b")
        self.assertNotRegex(self.function_body(assembly[3], "Sum"), r"\br1[345]\b")

    def test_dependency_direction_and_overwritten_right_operand(self):
        a, b = 3, 7
        for _ in range(25):
            a, b = a + b, b + 1
        self.run_source(f"""
uint64 Chain(uint64 n, uint64 a, uint64 b) {{
    if (n == 0) {{ return a^b; }} return Chain(n-1,a+b,b+1);
}}
uint64 Mirror(uint64 n, uint64 a, uint64 b) {{
    if (n == 0) {{ return a+b; }} return Mirror(n-1,a,(uint64)5-b);
}}
uint64 Commuted(uint64 n, uint64 a) {{
    if (n == 0) {{ return a; }} return Commuted(n-1,(uint64)3+a);
}}
uint64 OldValue(uint64 n, uint64 a, uint64 b) {{
    if (n == 0) {{ return b; }} return OldValue(n-1,(uint64)18446744073709551615,a+1);
}}
uint64 Square(uint64 n, uint64 a) {{
    if (n == 0) {{ return a; }} return Square(n-1,a*a);
}}
uint64 Zero(uint64 n, uint64 a) {{
    if (n == 0) {{ return a; }} return Zero(n-1,a-a);
}}
int32 main() {{
    if (Chain(25,3,7) != (uint64){a ^ b}) {{ return 1; }}
    if (Mirror(20,10,2) != 12 || Mirror(21,10,2) != 13) {{ return 2; }}
    if (Commuted(7,2) != 23 || OldValue(1,17,99) != 18 || OldValue(2,17,99) != 0) {{ return 3; }}
    if (Square(3,2) != 256 || Zero(3,(uint64)18446744073709551615) != 0) {{ return 4; }}
    return 0;
}}
""")

    def test_full_width_immediates_and_modular_arithmetic(self):
        operations = (("Add", "+", lambda a, b: a + b), ("Sub", "-", lambda a, b: a - b),
                      ("Mul", "*", lambda a, b: a * b), ("And", "&", lambda a, b: a & b),
                      ("Or", "|", lambda a, b: a | b), ("Xor", "^", lambda a, b: a ^ b))
        constants = (3, -1, 2147483648, -2147483648, 9223372036854775807, -9223372036854775808)
        for unsigned in (False, True):
            kind = "uint64" if unsigned else "int64"
            definitions, checks = [], []
            for index, constant in enumerate(constants):
                for name, symbol, operation in operations:
                    function = f"{name}{index}"
                    definitions.append(f"{kind} {function}({kind} n, {kind} a) {{ "
                                       f"if (n == 0) {{ return a; }} return {function}(n-1,a {symbol} ({kind}){constant}); }}")
                    for value in (0, 7, -1, -(1 << 63)):
                        expected = registers.wrap(value, 64, unsigned)
                        for _ in range(3):
                            expected = registers.wrap(operation(expected, registers.wrap(constant, 64, unsigned)), 64, unsigned)
                        checks.append(f"if ({function}(3,({kind}){value}) != ({kind}){expected}) {{ return 1; }}")
            with self.subTest(unsigned=unsigned):
                self.run_source("\n".join(definitions) + "\nint32 main() {\n" +
                                "\n".join(checks) + "\nreturn 0; }")

    def test_parallel_copy_cycles_keep_original_values(self):
        self.run_source("""
uint64 Swap(uint64 n, uint64 a, uint64 b) {
    if (n == 0) { return a*10+b; } return Swap(n-1,b,a);
}
uint64 Computed(uint64 n, uint64 a, uint64 b) {
    if (n == 0) { return a*1000+b; } return Computed(n-1,b+1,a+2);
}
uint64 Three(uint64 n, uint64 a, uint64 b, uint64 c) {
    if (n == 0) { return a+10*b+100*c; } return Three(n-1,b,c,a);
}
int32 main() {
    if (Swap(10001,1,2) != 21 || Computed(101,1,2) != 153153) { return 1; }
    if (Three(10000,1,2,3) != 132) { return 2; }
    return 0;
}
""")

    def test_calls_shifts_addresses_and_non_native_widths_fall_back(self):
        self.run_source("""
static uint64 observed=0;
uint64 Touch(uint64 n) { observed=observed*10+n; return n; }
uint64 Calls(uint64 n, uint64 a) {
    if (n == 0) { return a; } return Calls(n-1,a+Touch(n));
}
uint64 Shifted(uint64 n, uint64 a) {
    if (n == 0) { return a; } return Shifted(n-1,a<<1);
}
uint64 Address(uint64 n, uint64 a) {
    unsafe(using krt.mem;) {
        let p=&n;
        if (*p == 0) { return a; } return Address(n-1,a+*p);
    }
}
int64 Packed(int64 n, int2 a, int30 b, uint32 c) {
    if (n == 0) { return (int64)a+(int64)b+(int64)c; }
    return Packed(n-1,(int2)(a+1),(int30)(b+1),(uint32)(c+1));
}
uint128 Wide(uint64 n, uint128 a) {
    if (n == 0) { return a; } return Wide(n-1,a+(uint128)18446744073709551616);
}
uint64 Six(uint64 n, uint64 a, uint64 b, uint64 c, uint64 d, uint64 e) {
    if (n == 0) { return a+10*b+100*c+1000*d+10000*e; }
    return Six(n-1,b,c,d,e,a);
}
int32 main() {
    if (Calls(4,7) != 17 || observed != 4321) { return 1; }
    if (Shifted(9,3) != 1536 || Address(10,3) != 58) { return 2; }
    if (Packed(5,-1,-123,(uint32)4294967295) != -114) { return 3; }
    if (Wide(3,(uint128)9) != (uint128)55340232221128654857) { return 4; }
    if (Six(11,1,2,3,4,5) != 15432) { return 5; }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
