"""String representation, owned integer formatting and console integration."""
from Support import LibraryTestCase


class RuntimeTests(LibraryTestCase):
    def test_string_byte_length_and_owned_int32_results(self):
        self.run_source("""
using System;
int32 main() {
    if (Sys.InternalStringLength(null) != 0 || Sys.InternalStringLength("") != 0) {
        return 1;
    }
    if (Sys.InternalStringLength("中文😀") != 10) {
        return 2;
    }
    int32[] numbers = [-2147483648, -100, -1, 0, 1, 100, 2147483647];
    for (int32 pass = 0; pass < 20; pass++) {
        for (int32 index = 0; index < 7; index++) {
            string text = Sys.InternalInt32ToString(numbers[index]);
            int32 parsed = 0;
            if (!Convert.TryToInt32(text, ref parsed) || parsed != numbers[index]) {
                return 3;
            }
            unsafe(using krt.mem;) {
                if ((uint64)Sys.InternalStringPtr(text) % 4096 != 0) {
                    return 4;
                }
                if (!Memory.TryFree((void*)Sys.InternalStringPtr(text), Sys.InternalStringLength(text) + 1)) {
                    return 5;
                }
            }
        }
    }
    return 0;
}
""")

    def test_console_numeric_extremes_and_utf8_format(self):
        self.run_source("""
using System.Console;
int32 main() {
    Console.WriteLine((int32)-2147483648);
    Console.WriteLine((int32)2147483647);
    Console.WriteLine((int64)-9223372036854775808);
    Console.WriteLine((int64)9223372036854775807);
    Console.WriteLine((uint32)4294967295);
    Console.WriteLine((uint64)18446744073709551615);
    Console.Write((int32)0);
    Console.Write((uint64)0);
    Console.WriteLine(true);
    Console.WriteLine(false);
    Console.WriteLineFormat("中文{0}😀{0}!", "值");
    Console.WriteLineFormat("a{0}b{1}{", null);
    Console.WriteFormat(null, "unused");
    return 0;
}
""", expected_stdout="-2147483648\n2147483647\n-9223372036854775808\n9223372036854775807\n"
                       "4294967295\n18446744073709551615\n00True\nFalse\n中文值😀值!\nab{1}{\n")

    def test_repeated_numeric_output(self):
        self.run_source("""
using System.Console;
int32 main() {
    for (int32 value = -500; value < 500; value++) {
        Console.WriteLine(value);
    }
    return 0;
}
""", expected_stdout="".join(f"{value}\n" for value in range(-500, 500)))

    def test_class_overloads_follow_cast_and_conditional_types(self):
        self.run_source("""
public static class OverloadProbe {
    private static int32 pick(string value) {
        return 3;
    }
    private static int32 pick(int32 value) {
        return 1;
    }
    public static int32 CastArgument(int32 value) {
        return pick((int64)value);
    }
    private static int32 pick(int64 value) {
        return 2;
    }
    public static int32 ConditionalArgument(bool choice) {
        return pick(choice ? "left" : "right");
    }
    public static int32 NarrowArgument(int64 value) {
        return pick((int32)value);
    }
}
int32 main() {
    if (OverloadProbe.CastArgument(-500) != 2) {
        return 1;
    }
    if (OverloadProbe.ConditionalArgument(true) != 3 || OverloadProbe.ConditionalArgument(false) != 3) {
        return 2;
    }
    if (OverloadProbe.NarrowArgument(500) != 1) {
        return 3;
    }
    return 0;
}
""")

    def test_conditional_early_return_keeps_function_fallthrough(self):
        self.run_source("""
static int32 visits = 0;

void conditional_work(bool stop) {
    if (stop) {
        return;
    }
    int32 index = 0;
    while (index < 3) {
        if (index == 1) {
            visits = visits + 1;
        }
        index = index + 1;
    }
    visits = visits + 10;
}

void partial_else(bool choice) {
    if (choice) {
        return;
    } else {
        visits = visits + 2;
    }
    visits = visits + 3;
}

int32 complete_else(bool choice) {
    if (choice) {
        return 17;
    } else {
        return 23;
    }
}

int32 loop_return(bool enter) {
    while (enter) {
        return 31;
    }
    return 37;
}

public static class FallthroughProbe {
    public static void Work(bool stop, ref int32 count) {
        if (stop) {
            return;
        }
        for (int32 index = 0; index < 2; index++) {
            if (index >= 0 && index < 2) {
                count = count + 1;
            }
        }
        count = count + 4;
    }
}

int32 main() {
    conditional_work(false);
    conditional_work(true);
    if (visits != 11) {
        return 1;
    }
    partial_else(false);
    partial_else(true);
    if (visits != 16) {
        return 2;
    }
    if (complete_else(true) != 17 || complete_else(false) != 23) {
        return 3;
    }
    if (loop_return(true) != 31 || loop_return(false) != 37) {
        return 4;
    }
    FallthroughProbe.Work(false, ref visits);
    FallthroughProbe.Work(true, ref visits);
    if (visits != 22) {
        return 5;
    }
    return 0;
}
""")

    def test_class_scope_direct_ref_and_indirect_call_abi(self):
        self.run_source("""
public static class ScopeProbe {
    public static int64 DirectRef() {
        int64 value = 41;
        return increase(ref value);
    }
    public static uint64 UnsignedArgument() {
        uint32 value = (uint32)4294967295;
        return widen(value);
    }
    public static int32 IndirectArgument(fn(int32*) -> int32 callback) {
        unsafe(using krt.mem;) {
            int32 value = 17;
            return callback(&value);
        }
    }
}
int64 increase(ref int64 value) {
    value = value + 1;
    return value;
}
uint64 widen(uint64 value) {
    return value;
}
int32 read_value(int32* value) {
    unsafe(using krt.mem;) {
        return *value;
    }
}
int32 main() {
    if (ScopeProbe.DirectRef() != 42) {
        return 1;
    }
    if (ScopeProbe.UnsignedArgument() != (uint64)4294967295) {
        return 2;
    }
    unsafe(using krt.mem;) {
        if (ScopeProbe.IndirectArgument(&read_value) != 17) {
            return 3;
        }
    }
    return 0;
}
""")
