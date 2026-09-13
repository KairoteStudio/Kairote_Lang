"""Real-import byte-string and builder regressions at every optimization level."""
import random
import unittest

from Support import LibraryTestCase


PRELUDE = """
using System.String;
using System.StringBuilder;

bool owned_text(string actual, string expected) {
    unsafe(using krt.mem;) {
        if (actual == null || expected == null) { return false; }
        byte* data = (byte*)Sys.InternalStringPtr(actual);
        byte* wanted = (byte*)Sys.InternalStringPtr(expected);
        if ((uint64)data == (uint64)wanted || ((uint64)data & 4095) != 0) { return false; }
        int64 index = 0;
        while (data[index] != 0 || wanted[index] != 0) {
            if (data[index] != wanted[index]) { return false; }
            index = index + 1;
        }
        return Memory.TryFree((void*)data, index + 1);
    }
}

bool buffer_text(int64 buffer, int32 length, string expected) {
    unsafe(using krt.mem;) {
        if (buffer == 0 || length < 0 || expected == null) { return false; }
        byte* data = (byte*)buffer;
        byte* wanted = (byte*)Sys.InternalStringPtr(expected);
        int32 index = 0;
        while (index < length) {
            if (wanted[index] == 0 || wanted[index] != data[index]) { return false; }
            index = index + 1;
        }
        return data[length] == 0 && wanted[length] == 0;
    }
}
"""


class Strings(LibraryTestCase):
    def run_body(self, body):
        return self.run_source(PRELUDE + "\nint32 main() { unsafe(using krt.mem;) {\n" +
                               body + "\n} return 0; }\n")

    def test_language_string_null_equality_and_single_evaluation(self):
        self.run_source(PRELUDE + '''
            static int32 evaluations = 0;
            string next_text(int32 choice) {
                evaluations = evaluations + 1;
                if (choice == 0) { return null; }
                if (choice == 1) { return ""; }
                return "abc";
            }
            int32 main() {
                string missing = next_text(0);
                string empty = next_text(1);
                string value = next_text(2);
                if (!(missing == null) || !(null == missing) || missing != null || null != missing) { return 1; }
                if (missing == empty || empty == missing || missing == value || value == missing) { return 2; }
                if (!(missing != empty) || !(empty != missing) || !(missing != value) || !(value != missing)) { return 3; }
                string copy = StringOps.Substring(value, 0);
                if (copy != value || !(copy == value) || empty == value) { return 4; }
                evaluations = 0;
                bool both_null = next_text(0) == next_text(0);
                if (!both_null || evaluations != 2) { return 5; }
                evaluations = 0;
                bool left_null = next_text(0) != next_text(2);
                if (!left_null || evaluations != 2) { return 6; }
                evaluations = 0;
                bool right_null = next_text(2) == next_text(0);
                if (right_null || evaluations != 2) { return 7; }
                evaluations = 0;
                bool same_text = next_text(2) == next_text(2);
                if (!same_text || evaluations != 2) { return 8; }
                if (!owned_text(copy, "abc")) { return 9; }
                return 0;
            }
        ''')

    def test_length_null_order_and_unsigned_bytes(self):
        self.run_body('''
            string missing = null;
            if (StringOps.Length(missing) != 0 || StringOps.Length("") != 0) { return 1; }
            if (StringOps.Length("abcdef") != 6) { return 2; }
            if (!StringOps.IsNullOrEmpty(missing) || !StringOps.IsNullOrEmpty("")) { return 3; }
            if (StringOps.IsNullOrEmpty(" ")) { return 4; }
            if (StringOps.Compare(missing, missing) != 0 || StringOps.Equals(missing, "")) { return 5; }
            if (StringOps.Compare(missing, "") >= 0 || StringOps.Compare("", missing) <= 0) { return 6; }
            if (StringOps.Compare("ab", "abc") >= 0 || StringOps.Compare("abd", "abc") <= 0) { return 7; }
            if (!StringOps.Equals("same", "same") || StringOps.Equals("same", "some")) { return 8; }
            byte* data = stackalloc byte[4];
            data[0] = 128; data[1] = 255; data[2] = 0; data[3] = 127;
            string high = (string)(int64)data;
            if (StringOps.Length(high) != 2 || StringOps.Compare(high, "z") <= 0) { return 9; }
            if (StringOps.IndexOf(high, (char)255) != 1 || StringOps.LastIndexOf(high, (char)128) != 0) { return 10; }
        ''')

    def test_concat_and_empty_results_are_owned(self):
        self.run_body('''
            string missing = null;
            if (!owned_text(StringOps.Concat("ab", "cd"), "abcd")) { return 1; }
            if (!owned_text(StringOps.Concat(missing, "x"), "x")) { return 2; }
            if (!owned_text(StringOps.Concat("x", missing), "x")) { return 3; }
            if (!owned_text(StringOps.Concat(missing, missing), "")) { return 4; }
            if (!owned_text(StringOps.Concat("", ""), "")) { return 5; }
            if (!owned_text(StringOps.Substring("abc", 3), "")) { return 6; }
            if (!owned_text(StringOps.Trim("   "), "")) { return 7; }
            if (!owned_text(StringOps.ToUpper(""), "") || !owned_text(StringOps.ToLower(""), "")) { return 8; }
        ''')

    def test_integer_formatting_extremes_and_differential_values(self):
        random_values = random.Random(1947)
        signed = [0, 1, -1, 9, -10, 2147483647, -2147483648,
                  (1 << 63) - 1, -(1 << 63)]
        signed.extend(random_values.randrange(-(1 << 63), 1 << 63) for _ in range(12))
        unsigned = [0, 9, 10, (1 << 32) - 1, 1 << 63, (1 << 64) - 1]
        unsigned.extend(random_values.randrange(1 << 64) for _ in range(12))
        checks = []
        for value in signed:
            checks.append(f'if (!owned_text(StringOps.FromInt64((int64){value}), "{value}")) {{ return 1; }}')
        for value in unsigned:
            checks.append(f'if (!owned_text(StringOps.FromUInt64((uint64){value}), "{value}")) {{ return 2; }}')
        for value in [0, -1, 1, -(1 << 31), (1 << 31) - 1]:
            checks.append(f'if (!owned_text(StringOps.FromInt32((int32){value}), "{value}")) {{ return 3; }}')
        self.run_body("\n".join(checks))

    def test_substring_clamping_overflow_and_copy_independence(self):
        self.run_body('''
            string missing = null;
            if (StringOps.Substring(missing, 0) != null) { return 1; }
            if (!owned_text(StringOps.Substring("abcdef", -5, 2), "ab")) { return 2; }
            if (!owned_text(StringOps.Substring("abcdef", 2), "cdef")) { return 3; }
            if (!owned_text(StringOps.Substring("abcdef", 2, 2147483647), "cdef")) { return 4; }
            if (!owned_text(StringOps.Substring("abc", 2147483647, 2147483647), "")) { return 5; }
            if (!owned_text(StringOps.Substring("abc", 1, -2147483648), "")) { return 6; }
            if (!owned_text(StringOps.Substring("", -1), "")) { return 7; }
            byte* source = stackalloc byte[4];
            source[0] = 97; source[1] = 98; source[2] = 99; source[3] = 0;
            string copy = StringOps.Substring((string)(int64)source, 1, 2);
            source[1] = 120;
            if (!owned_text(copy, "bc")) { return 8; }
        ''')

    def test_character_and_substring_search_boundaries(self):
        self.run_body('''
            string missing = null;
            if (StringOps.IndexOf("ababa", 'a') != 0 || StringOps.LastIndexOf("ababa", 'a') != 4) { return 1; }
            if (StringOps.IndexOf("", 'a') != -1 || StringOps.LastIndexOf(missing, 'a') != -1) { return 2; }
            if (StringOps.IndexOf("abc", (char)0) != -1 || StringOps.LastIndexOf("abc", (char)0) != -1) { return 3; }
            if (StringOps.IndexOf("abababa", "aba") != 0 || StringOps.LastIndexOf("abababa", "aba") != 4) { return 4; }
            if (StringOps.IndexOf("abababa", "aba", 1) != 2) { return 5; }
            if (StringOps.IndexOf("abc", "abcd") != -1 || StringOps.LastIndexOf("abc", "abcd") != -1) { return 6; }
            if (StringOps.IndexOf("abc", "", 3) != 3 || StringOps.LastIndexOf("abc", "") != 3) { return 7; }
            if (StringOps.IndexOf("abc", "", 4) != -1 || StringOps.IndexOf("abc", "", -1) != -1) { return 8; }
            if (StringOps.IndexOf("", "") != 0 || StringOps.LastIndexOf("", "") != 0) { return 9; }
            if (StringOps.IndexOf(missing, "") != -1 || StringOps.IndexOf("abc", missing) != -1) { return 10; }
            if (StringOps.Contains(missing, "") || StringOps.Contains("abc", missing)) { return 11; }
            if (!StringOps.Contains("abc", "") || !StringOps.Contains("abc", "bc")) { return 12; }
            if (StringOps.IndexOf("aaaab", "aab") != 2 || StringOps.Contains("abc", "ac")) { return 13; }
        ''')

    def test_prefix_suffix_and_nulls(self):
        self.run_body('''
            string missing = null;
            if (!StringOps.StartsWith("abc", "ab") || !StringOps.EndsWith("abc", "bc")) { return 1; }
            if (StringOps.StartsWith("abc", "bc") || StringOps.EndsWith("abc", "ab")) { return 2; }
            if (StringOps.StartsWith("abc", "abcd") || StringOps.EndsWith("abc", "abcd")) { return 3; }
            if (!StringOps.StartsWith("", "") || !StringOps.EndsWith("", "")) { return 4; }
            if (StringOps.StartsWith(missing, "") || StringOps.EndsWith(missing, "")) { return 5; }
            if (StringOps.StartsWith("abc", missing) || StringOps.EndsWith("abc", missing)) { return 6; }
        ''')

    def test_trim_ascii_whitespace_and_preserve_non_ascii(self):
        self.run_body(r'''
            string missing = null;
            if (StringOps.Trim(missing) != null || StringOps.TrimStart(missing) != null || StringOps.TrimEnd(missing) != null) { return 1; }
            if (!StringOps.IsNullOrWhiteSpace(missing) || !StringOps.IsNullOrWhiteSpace("")) { return 2; }
            if (!StringOps.IsNullOrWhiteSpace(" \t\r\n") || StringOps.IsNullOrWhiteSpace(" a ")) { return 3; }
            if (!owned_text(StringOps.Trim(" \talpha beta\r\n"), "alpha beta")) { return 4; }
            if (!owned_text(StringOps.TrimStart(" \talpha \n"), "alpha \n")) { return 5; }
            if (!owned_text(StringOps.TrimEnd(" \talpha \n"), " \talpha")) { return 6; }
            byte* data = stackalloc byte[5];
            data[0] = 11; data[1] = 12; data[2] = 32; data[3] = 9; data[4] = 0;
            if (!StringOps.IsNullOrWhiteSpace((string)(int64)data)) { return 7; }
            if (!owned_text(StringOps.Trim((string)(int64)data), "")) { return 8; }
            data[0] = 194; data[1] = 160; data[2] = 0;
            if (StringOps.IsNullOrWhiteSpace((string)(int64)data)) { return 9; }
            if (!owned_text(StringOps.Trim((string)(int64)data), (string)(int64)data)) { return 10; }
        ''')

    def test_ascii_case_all_nonzero_bytes(self):
        self.run_body('''
            string missing = null;
            if (StringOps.ToUpper(missing) != null || StringOps.ToLower(missing) != null) { return 1; }
            byte* data = stackalloc byte[2];
            byte* expected = stackalloc byte[2];
            data[1] = 0; expected[1] = 0;
            int32 value = 1;
            while (value <= 255) {
                data[0] = (byte)value;
                expected[0] = (byte)(value >= 97 && value <= 122 ? value - 32 : value);
                if (!owned_text(StringOps.ToUpper((string)(int64)data), (string)(int64)expected)) { return 2; }
                expected[0] = (byte)(value >= 65 && value <= 90 ? value + 32 : value);
                if (!owned_text(StringOps.ToLower((string)(int64)data), (string)(int64)expected)) { return 3; }
                if (data[0] != (byte)value) { return 4; }
                value = value + 1;
            }
        ''')

    def test_builder_append_numbers_and_terminators(self):
        self.run_body('''
            int64 buffer = StringBuilder.AllocateBuffer(128);
            if (!buffer_text(buffer, 0, "")) { return 1; }
            int32 length = StringBuilder.AppendString(buffer, 128, 0, "x:");
            length = StringBuilder.AppendInt32(buffer, 128, length, (int32)-2147483648);
            length = StringBuilder.AppendChar(buffer, 128, length, ',');
            length = StringBuilder.AppendInt64(buffer, 128, length, (int64)-9223372036854775808);
            length = StringBuilder.AppendChar(buffer, 128, length, ',');
            length = StringBuilder.AppendUInt64(buffer, 128, length, (uint64)18446744073709551615);
            length = StringBuilder.AppendBool(buffer, 128, length, true);
            length = StringBuilder.AppendBool(buffer, 128, length, false);
            if (!buffer_text(buffer, length, "x:-2147483648,-9223372036854775808,18446744073709551615TrueFalse")) { return 2; }
            string copy = StringBuilder.BufferToString(buffer, length);
            ((byte*)buffer)[0] = 121;
            if (!owned_text(copy, "x:-2147483648,-9223372036854775808,18446744073709551615TrueFalse")) { return 3; }
            length = StringBuilder.Clear(length);
            length = StringBuilder.AppendInt64(buffer, 128, length, 0);
            if (!buffer_text(buffer, length, "0")) { return 4; }
            StringBuilder.FreeBuffer(buffer, 128);
        ''')

    def test_builder_capacity_invalid_ranges_and_atomic_line(self):
        self.run_body(r'''
            if (StringBuilder.AllocateBuffer(-1) != 0) { return 1; }
            int64 empty = StringBuilder.AllocateBuffer(0);
            if (empty == 0 || StringBuilder.AppendString(empty, 0, 0, "") != 0) { return 2; }
            if (StringBuilder.AppendChar(empty, 0, 0, 'a') != -1) { return 3; }
            if (!owned_text(StringBuilder.BufferToString(empty, 0), "")) { return 4; }
            StringBuilder.FreeBuffer(empty, 0);
            int64 buffer = StringBuilder.AllocateBuffer(4);
            int32 length = StringBuilder.AppendString(buffer, 4, 0, "ab");
            if (StringBuilder.AppendLine(buffer, 4, length, "cd") != -1) { return 5; }
            if (!buffer_text(buffer, 2, "ab")) { return 6; }
            if (StringBuilder.AppendString(buffer, 4, length, "cde") != -1) { return 7; }
            if (StringBuilder.AppendString(buffer, -1, 0, "") != -1 || StringBuilder.AppendString(buffer, 4, -1, "") != -1) { return 8; }
            if (StringBuilder.AppendChar(buffer, 4, 5, 'x') != -1 || StringBuilder.AppendString(0, 4, 0, "") != -1) { return 9; }
            if (StringBuilder.AppendString(buffer, 2147483647, 2147483647, "x") != -1) { return 10; }
            if (StringBuilder.AppendLine(buffer, 2147483647, 2147483646, "x") != -1) { return 11; }
            if (StringBuilder.Insert(buffer, 2147483647, 2147483646, 0, "xy") != -1) { return 12; }
            if (!buffer_text(buffer, 2, "ab")) { return 13; }
            length = StringBuilder.AppendLine(buffer, 4, length, "c");
            if (!buffer_text(buffer, length, "abc\n")) { return 14; }
            if (StringBuilder.AppendLine(buffer, 4, length) != -1) { return 15; }
            if (StringBuilder.BufferToString(buffer, -1) != null || StringBuilder.BufferToString(0, 1) != null) { return 16; }
            if (!owned_text(StringBuilder.BufferToString(0, 0), "")) { return 17; }
            StringBuilder.FreeBuffer(buffer, 4);
            StringBuilder.FreeBuffer(0, 0);
        ''')

    def test_builder_insert_self_overlap_and_remove_overflow(self):
        self.run_body('''
            int64 buffer = StringBuilder.AllocateBuffer(32);
            int32 length = StringBuilder.AppendString(buffer, 32, 0, "abcd");
            length = StringBuilder.Insert(buffer, 32, length, 1, (string)buffer);
            if (!buffer_text(buffer, length, "aabcdbcd")) { return 1; }
            length = StringBuilder.Remove(buffer, length, 1, 2147483647);
            if (!buffer_text(buffer, length, "a")) { return 2; }
            length = StringBuilder.Insert(buffer, 32, length, -1, "xy");
            length = StringBuilder.Insert(buffer, 32, length, 2147483647, "z");
            if (!buffer_text(buffer, length, "xyaz")) { return 3; }
            length = StringBuilder.AppendString(buffer, 32, 0, "abcd");
            length = StringBuilder.Insert(buffer, 32, length, 0, (string)(buffer + 1));
            if (!buffer_text(buffer, length, "bcdabcd")) { return 4; }
            length = StringBuilder.Remove(buffer, length, 2, 3);
            if (!buffer_text(buffer, length, "bccd")) { return 5; }
            if (StringBuilder.Remove(buffer, length, -1, 2) != length) { return 6; }
            if (StringBuilder.Remove(buffer, length, 0, -1) != length) { return 7; }
            if (StringBuilder.Insert(buffer, 3, length, 0, "") != -1) { return 8; }
            if (!buffer_text(buffer, length, "bccd")) { return 9; }
            StringBuilder.FreeBuffer(buffer, 32);
            int64 outer = StringBuilder.AllocateBuffer(32);
            if (StringBuilder.AppendString(outer, 32, 0, "abcdef") != 6) { return 10; }
            int64 inner = outer + 2;
            int32 inner_length = StringBuilder.Insert(inner, 30, 4, 1, (string)outer);
            if (!buffer_text(inner, inner_length, "cabcdefdef")) { return 11; }
            StringBuilder.FreeBuffer(outer, 32);
        ''')

    def test_builder_append_overlap_null_empty_and_embedded_zero(self):
        self.run_body('''
            int64 buffer = StringBuilder.AllocateBuffer(32);
            int32 length = StringBuilder.AppendString(buffer, 32, 0, "abcd");
            length = StringBuilder.AppendString(buffer, 32, length, (string)(buffer + 1));
            if (!buffer_text(buffer, length, "abcdbcd")) { return 1; }
            length = StringBuilder.AppendString(buffer, 32, 0, (string)(buffer + 1));
            if (!buffer_text(buffer, length, "bcdbcd")) { return 10; }
            length = StringBuilder.Clear(length);
            string missing = null;
            length = StringBuilder.AppendString(buffer, 32, length, missing);
            if (!buffer_text(buffer, length, "")) { return 2; }
            length = StringBuilder.Insert(buffer, 32, length, 0, missing);
            if (length != 0) { return 3; }
            length = StringBuilder.AppendChar(buffer, 32, length, 'a');
            length = StringBuilder.AppendChar(buffer, 32, length, (char)0);
            length = StringBuilder.AppendChar(buffer, 32, length, 'a');
            if (length != 3 || StringBuilder.IndexOf(buffer, length, (char)0) != 1) { return 4; }
            if (StringBuilder.LastIndexOf(buffer, length, 'a') != 2) { return 5; }
            if (StringBuilder.Replace(buffer, length, 'a', 'z') != 2) { return 6; }
            byte* data = (byte*)buffer;
            if (data[0] != 122 || data[1] != 0 || data[2] != 122 || data[3] != 0) { return 7; }
            if (StringBuilder.IndexOf(0, 0, 'a') != -1 || StringBuilder.LastIndexOf(buffer, -2147483648, 'a') != -1) { return 8; }
            if (StringBuilder.Replace(buffer, -1, 'a', 'b') != -1 || StringBuilder.Remove(0, 0, 0, 1) != -1) { return 9; }
            StringBuilder.FreeBuffer(buffer, 32);
        ''')

    def test_builder_numeric_temporaries_are_released_on_success_and_failure(self):
        self.run_body('''
            int64 buffer = StringBuilder.AllocateBuffer(32);
            if (buffer == 0) { return 1; }
            int64* limits = stackalloc int64[2];
            limits[0] = 16777216; limits[1] = 16777216;
            if (Sys.syscall(160, 9, (int64)limits, 0, 0, 0, 0) != 0) { return 2; }
            int32 index = 0;
            while (index < 5000) {
                if (StringBuilder.AppendInt64(buffer, 32, 0, 0) != 1) { return 3; }
                if (StringBuilder.AppendUInt64(buffer, 32, 32, (uint64)-1) != -1) { return 4; }
                index = index + 1;
            }
            if (!buffer_text(buffer, 1, "0")) { return 5; }
            if (!owned_text(StringOps.FromUInt64((uint64)-1), "18446744073709551615")) { return 6; }
            StringBuilder.FreeBuffer(buffer, 32);
        ''')

    def test_allocation_failure_is_reported_without_partial_writes(self):
        self.run_body('''
            int64 buffer = StringBuilder.AllocateBuffer(32);
            if (buffer == 0 || StringBuilder.AppendString(buffer, 32, 0, "abc") != 3) { return 1; }
            int64* limits = stackalloc int64[2];
            limits[0] = 1; limits[1] = 1;
            if (Sys.syscall(160, 9, (int64)limits, 0, 0, 0, 0) != 0) { return 2; }
            if (StringOps.Concat("a", "b") != null || StringOps.Concat("", "") != null) { return 3; }
            if (StringOps.FromInt64(-1) != null || StringOps.FromUInt64((uint64)-1) != null) { return 4; }
            if (StringOps.Substring("abc", 0) != null || StringOps.Substring("", 0) != null) { return 5; }
            if (StringOps.ToUpper("x") != null || StringOps.ToLower("x") != null || StringOps.Trim("x") != null) { return 6; }
            if (StringBuilder.AllocateBuffer(0) != 0 || StringBuilder.BufferToString(buffer, 3) != null) { return 7; }
            if (StringBuilder.AppendInt64(buffer, 32, 3, -1) != -1) { return 8; }
            if (StringBuilder.Insert(buffer, 32, 3, 1, (string)buffer) != -1) { return 9; }
            if (!buffer_text(buffer, 3, "abc")) { return 10; }
            StringBuilder.FreeBuffer(buffer, 32);
        ''')


if __name__ == "__main__":
    unittest.main()
