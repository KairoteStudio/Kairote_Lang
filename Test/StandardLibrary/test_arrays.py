"""Array ABI, overloads, checked ranges and overlapping copies."""
import unittest

from Support import LibraryTestCase


INTEGER_CASES = (
    ("byte", "201"),
    ("int8", "-100"),
    ("int16", "-12345"),
    ("uint16", "60000"),
    ("int32", "-123456"),
    ("uint32", "4000000000"),
    ("int64", "-10000000000"),
    ("uint64", "14000000000000000000"),
    ("int128", "-36893488147419103232"),
    ("uint128", "340282366920938463463374607431768211455"),
)


class ArrayTests(LibraryTestCase):
    def run_body(self, body):
        self.run_source("using System.Array;\nint main() {\n" + body + "\nreturn 0;\n}\n")

    def test_float_array_parameters_keep_addresses_in_registers_and_on_stack(self):
        self.run_source('''using System.Array;
            public static class ArrayAbi {
                public static int32 Check(float32[] first, int64 marker_a, int64 marker_b,
                                          int64 marker_c, int64 marker_d, int64 marker_e,
                                          float32[] last, float32 scalar) {
                    unsafe(using krt.mem;) {
                        if ((int64)first != (int64)last) { return 1; }
                        int64* header = (int64*)(int64)first;
                        if (header[-1] != 28) { return 2; }
                    }
                    if (marker_a != 11 || marker_b != 22 || marker_c != 33 || marker_d != 44 || marker_e != 55) { return 3; }
                    if (first[0] != (float32)1.5 || last[2] != (float32)3.5) { return 4; }
                    if (scalar != (float32)2.25) { return 5; }
                    last[1] = scalar;
                    return 0;
                }
            }
            int main() {
                float32[] values = new float32[3];
                values[0] = (float32)1.5;
                values[1] = (float32)0.0;
                values[2] = (float32)3.5;
                int32 result = ArrayAbi.Check(values, 11, 22, 33, 44, 55, values, (float32)2.25);
                if (result != 0) { return result; }
                if (values[1] != (float32)2.25) { return 6; }
                return 0;
            }
        ''')

    def test_integer_overloads_preserve_element_width_and_parameter_types(self):
        body = ""
        for index, (kind, value) in enumerate(INTEGER_CASES):
            body += f'''
                {kind}[] values_{index} = new {kind}[5];
                {kind}[] copy_{index} = new {kind}[7];
                if (ArrayOps.Length(values_{index}) != 5) {{ return {index * 10 + 1}; }}
                if (!ArrayOps.Fill(values_{index}, ({kind}){value})) {{ return {index * 10 + 2}; }}
                if (values_{index}[0] != ({kind}){value} || values_{index}[4] != ({kind}){value}) {{ return {index * 10 + 3}; }}
                values_{index}[2] = ({kind})17;
                if (ArrayOps.IndexOf(values_{index}, ({kind})17) != 2) {{ return {index * 10 + 4}; }}
                if (!ArrayOps.Copy(values_{index}, copy_{index})) {{ return {index * 10 + 5}; }}
                if (copy_{index}[4] != ({kind}){value} || copy_{index}[5] != 0 || copy_{index}[6] != 0) {{ return {index * 10 + 6}; }}
                if (!ArrayOps.Reverse(copy_{index}, 0, 5) || copy_{index}[2] != 17) {{ return {index * 10 + 7}; }}
                if (!ArrayOps.Clear(values_{index}) || values_{index}[0] != 0 || values_{index}[4] != 0) {{ return {index * 10 + 8}; }}
                if (!ArrayOps.Contains(copy_{index}, ({kind})17)) {{ return {index * 10 + 9}; }}
            '''
        self.run_body(body)

    def test_overlapping_copy_partial_fill_clear_reverse_and_search(self):
        self.run_body('''
            int32[] values = new int32[8];
            int32 index = 0;
            while (index < 8) { values[index] = index; index = index + 1; }
            if (!ArrayOps.Copy(values, 0, values, 2, 6)) { return 1; }
            index = 2;
            while (index < 8) {
                if (values[index] != index - 2) { return 2; }
                index = index + 1;
            }
            if (!ArrayOps.Copy(values, 2, values, 0, 6)) { return 3; }
            if (!ArrayOps.Reverse(values, 1, 5)) { return 4; }
            if (values[0] != 0 || values[1] != 5 || values[2] != 4 || values[5] != 1 || values[7] != 5) { return 5; }
            if (!ArrayOps.Fill(values, -7, 2, 2) || !ArrayOps.Clear(values, 3, 2)) { return 6; }
            if (values[1] != 5 || values[2] != -7 || values[3] != 0 || values[4] != 0 || values[5] != 1) { return 7; }
            if (ArrayOps.IndexOf(values, -7) != 2 || ArrayOps.IndexOf(values, 5, 2, 6) != 7) { return 8; }
            if (ArrayOps.IndexOf(values, 99) != -1 || ArrayOps.Contains(values, 99)) { return 9; }
            if (!ArrayOps.Copy(values, values, 8)) { return 10; }
        ''')

    def test_invalid_ranges_do_not_partially_write(self):
        self.run_body('''
            int64[] source = new int64[4];
            int64[] destination = new int64[4];
            ArrayOps.Fill(source, (int64)17);
            ArrayOps.Fill(destination, (int64)91);
            if (ArrayOps.Copy(source, -1, destination, 0, 1)) { return 1; }
            if (ArrayOps.Copy(source, 0, destination, -1, 1)) { return 2; }
            if (ArrayOps.Copy(source, 0, destination, 0, -1)) { return 3; }
            if (ArrayOps.Copy(source, 0, destination, 3, 2)) { return 4; }
            if (ArrayOps.Copy(source, 2147483647, destination, 0, 1)) { return 5; }
            if (ArrayOps.Copy(source, 0, destination, 2147483647, 2147483647)) { return 6; }
            if (ArrayOps.Fill(destination, (int64)0, 1, 2147483647)) { return 7; }
            if (ArrayOps.Clear(destination, 0, -2147483648)) { return 8; }
            if (ArrayOps.Reverse(destination, 3, 2)) { return 9; }
            if (ArrayOps.IndexOf(destination, (int64)91, -1, 1) != -2) { return 10; }
            if (!ArrayOps.Copy(source, 4, destination, 4, 0)) { return 11; }
            if (!ArrayOps.Fill(destination, (int64)0, 4, 0) || !ArrayOps.Clear(destination, 4, 0)) { return 12; }
            if (!ArrayOps.Reverse(destination, 4, 0) || ArrayOps.IndexOf(destination, (int64)91, 4, 0) != -1) { return 13; }
            int32 index = 0;
            while (index < 4) {
                if (destination[index] != 91 || source[index] != 17) { return 14; }
                index = index + 1;
            }
        ''')

    def test_null_empty_and_single_element_arrays(self):
        self.run_body('''
            int32[] absent = null;
            int32[] empty = new int32[0];
            int32[] single = new int32[1];
            single[0] = 42;
            if (ArrayOps.Length(absent) != 0 || ArrayOps.Length(empty) != 0) { return 1; }
            if (!ArrayOps.Copy(absent, empty, 0) || !ArrayOps.Copy(empty, absent, 0)) { return 2; }
            if (!ArrayOps.Clear(absent) || !ArrayOps.Fill(empty, 17) || !ArrayOps.Reverse(absent)) { return 3; }
            if (ArrayOps.IndexOf(absent, 1) != -1 || ArrayOps.Contains(empty, 0)) { return 4; }
            if (ArrayOps.Copy(single, absent, 1) || ArrayOps.Fill(absent, 1, 0, 1)) { return 5; }
            if (!ArrayOps.Reverse(single) || single[0] != 42) { return 6; }
            if (ArrayOps.Length(single) != 1 || ArrayOps.IndexOf(single, 42) != 0) { return 7; }
        ''')

    def test_boolean_and_floating_arrays(self):
        self.run_body('''
            bool[] flags = new bool[4];
            if (ArrayOps.Length(flags) != 4 || !ArrayOps.Fill(flags, true)) { return 1; }
            if (!flags[0] || !flags[3]) { return 2; }
            if (!ArrayOps.Clear(flags, 1, 2) || flags[1] || flags[2]) { return 3; }
            if (ArrayOps.IndexOf(flags, false) != 1 || !ArrayOps.Contains(flags, true)) { return 4; }
            float32[] singles = new float32[5];
            float32[] copied = new float32[5];
            if (!ArrayOps.Fill(singles, (float32)1.25)) { return 5; }
            singles[0] = (float32)-0.0;
            singles[4] = (float32)-3.5;
            if (!ArrayOps.Copy(singles, copied) || !ArrayOps.Reverse(copied)) { return 6; }
            if (copied[0] != (float32)-3.5 || copied[4] != (float32)0.0) { return 7; }
            if (ArrayOps.IndexOf(copied, (float32)0.0) != 4) { return 8; }
            float64[] doubles = new float64[3];
            if (!ArrayOps.Fill(doubles, (float64)123456.75)) { return 9; }
            doubles[1] = (float64)-0.0;
            if (ArrayOps.IndexOf(doubles, (float64)0.0) != 1 || doubles[2] != (float64)123456.75) { return 10; }
            if (!ArrayOps.Clear(doubles) || doubles[2] != (float64)0.0) { return 11; }
            if (ArrayOps.Length(singles) != 5 || ArrayOps.Length(doubles) != 3) { return 12; }
        ''')

    def test_nan_search_uses_ieee_equality(self):
        self.run_body('''
            float32[] singles = new float32[3];
            float64[] doubles = new float64[3];
            unsafe(using krt.mem;) {
                uint32* single_bits = (uint32*)(int64)singles;
                uint64* double_bits = (uint64*)(int64)doubles;
                single_bits[1] = (uint32)2143289344;
                double_bits[1] = (uint64)9221120237041090560;
            }
            float32 single_nan = singles[1];
            float64 double_nan = doubles[1];
            if (ArrayOps.IndexOf(singles, single_nan) != -1) { return 1; }
            if (ArrayOps.IndexOf(doubles, double_nan) != -1) { return 2; }
            if (ArrayOps.Contains(singles, single_nan) || ArrayOps.Contains(doubles, double_nan)) { return 3; }
            if (ArrayOps.IndexOf(singles, (float32)0.0) != 0 || ArrayOps.IndexOf(doubles, (float64)0.0) != 0) { return 4; }
        ''')

    def test_invalid_metadata_is_rejected_before_accessing_elements(self):
        self.run_body('''
            int32[] values = new int32[3];
            values[0] = 71;
            unsafe(using krt.mem;) {
                int64* header = (int64*)values;
                int64 original_size = header[-1];
                header[-1] = 15;
                if (ArrayOps.Length(values) != -1 || ArrayOps.Fill(values, 0)) { return 1; }
                header[-1] = 17;
                if (ArrayOps.Length(values) != -1 || ArrayOps.Clear(values)) { return 2; }
                header[-1] = 8589934608;
                if (ArrayOps.Length(values) != -1 || ArrayOps.Reverse(values)) { return 3; }
                if (ArrayOps.IndexOf(values, 71) != -2) { return 4; }
                header[-1] = original_size;
            }
            if (ArrayOps.Length(values) != 3 || values[0] != 71) { return 5; }
        ''')


if __name__ == "__main__":
    unittest.main()
