"""Strict decimal parsing, legacy prefix compatibility, and integer formatting."""
import json
import random
import unittest

from Support import LibraryTestCase


class ConvertLibrary(LibraryTestCase):
    def program(self, checks):
        return "using System;\nint32 main() {\n" + "\n".join(checks) + "\nreturn 0;\n}\n"

    def test_signed_decimal_limits_and_signs(self):
        checks = []
        for bits in (32, 64):
            kind = f"int{bits}"
            low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            values = [(str(low), low), (str(high), high), ("0", 0), ("-0", 0), ("+0", 0),
                      ("0000012", 12), ("+0012", 12), ("-0012", -12), ("-1", -1)]
            randomizer = random.Random(bits)
            values.extend((str(value), value) for value in (randomizer.randint(low, high) for _ in range(30)))
            checks.append(f"{kind} result{bits} = 99;")
            for text, expected in values:
                checks.append(f"if (!Convert.TryToInt{bits}({json.dumps(text)}, ref result{bits}) || result{bits} != ({kind}){expected}) {{ return 1; }}")
            for text in (str(low - 1), str(high + 1), "9" * 100, "-" + "9" * 100):
                checks.append(f"result{bits} = 99; if (Convert.TryToInt{bits}({json.dumps(text)}, ref result{bits}) || result{bits} != 0) {{ return 2; }}")
        self.run_source(self.program(checks))

    def test_unsigned_limits_and_rejected_negative_zero(self):
        checks = []
        for bits in (32, 64):
            kind = f"uint{bits}"
            high = (1 << bits) - 1
            values = [(str(high), high), ("0", 0), ("+0", 0), ("00012", 12), ("+0012", 12)]
            randomizer = random.Random(bits + 1)
            values.extend((str(value), value) for value in (randomizer.randint(0, high) for _ in range(30)))
            checks.append(f"{kind} result{bits} = 99;")
            for text, expected in values:
                checks.append(f"if (!Convert.TryToUInt{bits}({json.dumps(text)}, ref result{bits}) || result{bits} != ({kind}){expected}) {{ return 1; }}")
            for text in (str(high + 1), "-0", "-1", "-" + str(high), "9" * 100):
                checks.append(f"result{bits} = 99; if (Convert.TryToUInt{bits}({json.dumps(text)}, ref result{bits}) || result{bits} != 0) {{ return 2; }}")
        self.run_source(self.program(checks))

    def test_strict_parser_rejects_partial_and_non_decimal_text(self):
        checks = []
        invalid = ["", "+", "-", " 1", "1 ", "\t1", "1\n", "1x", "x1", "1_0", "1,000",
                   "1.0", "1e3", "0x10", "0b10", "++1", "+-1", "--1", "１２", "١٢"]
        for name, kind in (("Int32", "int32"), ("Int64", "int64"), ("UInt32", "uint32"), ("UInt64", "uint64")):
            checks.append(f"{kind} result_{kind} = 99;")
            for value in [None, *invalid]:
                text = "null" if value is None else json.dumps(value, ensure_ascii=False)
                checks.append(f"result_{kind} = 99; if (Convert.TryTo{name}({text}, ref result_{kind}) || result_{kind} != 0) {{ return 1; }}")
        self.run_source(self.program(checks))

    def test_legacy_prefix_parsing_and_wrap_are_preserved(self):
        checks = [
            'if (Convert.ToInt32(null) != 0 || Convert.ToInt64("") != 0) { return 1; }',
            'if (Convert.ToInt32("123tail") != 123 || Convert.ToInt64("-456tail") != -456) { return 2; }',
            'if (Convert.ToInt32("+12") != 0 || Convert.ToInt64(" 12") != 0 || Convert.ToInt32("-") != 0) { return 3; }',
            'if (Convert.ToInt32("2147483648") != (int32)-2147483648 || Convert.ToInt32("4294967295") != -1) { return 4; }',
            'if (Convert.ToInt64("9223372036854775808") != (int64)-9223372036854775808 || Convert.ToInt64("18446744073709551615") != -1) { return 5; }',
        ]
        self.run_source(self.program(checks))

    def test_format_and_parse_round_trips_at_all_width_boundaries(self):
        checks = []
        for kind, method, values in [
            ("int32", "Int32", [-(1 << 31), -1, 0, 1, (1 << 31) - 1]),
            ("int64", "Int64", [-(1 << 63), -(1 << 53) - 1, -1, 0, 1, (1 << 53) + 1, (1 << 63) - 1]),
            ("uint32", "UInt32", [0, 1, 1 << 31, (1 << 32) - 1]),
            ("uint64", "UInt64", [0, 1, (1 << 53) + 1, 1 << 63, (1 << 64) - 1]),
        ]:
            checks.append(f"{kind} result_{kind} = 0;")
            for index, value in enumerate(values):
                text = f"text_{kind}_{index}"
                checks.append(f"string {text} = Convert.ToString(({kind}){value});")
                checks.append(f'if (!StringOps.Equals({text}, "{value}")) {{ return 1; }}')
                checks.append(f"if (!Convert.TryTo{method}({text}, ref result_{kind}) || result_{kind} != ({kind}){value}) {{ return 2; }}")
                checks.append(f"unsafe(using krt.mem;) {{ Memory.Free((void*)(int64){text}, (int64)StringOps.Length({text}) + 1); }}")
        checks.append('if (!StringOps.Equals(Convert.ToString(true), "True") || !StringOps.Equals(Convert.ToString(false), "False")) { return 3; }')
        self.run_source(self.program(checks))


if __name__ == "__main__":
    unittest.main()
