"""Memory contracts through the real System import and native backend."""
import unittest

from Support import LibraryTestCase


class MemoryTests(LibraryTestCase):
    def run_body(self, body):
        self.run_source("using System.Memory;\nint main() { unsafe(using krt.mem;) {\n" + body + "\nreturn 0;\n} }\n")

    def test_allocate_zeroes_storage_and_rejects_invalid_sizes(self):
        self.run_body('''
            if (Memory.Allocate(0) != null || Memory.Allocate(-1) != null) { return 1; }
            if (Memory.Allocate((int64)-9223372036854775808) != null) { return 2; }
            if (Memory.Allocate(9223372036854775807) != null) { return 3; }
            if (Memory.Allocate(4611686018427387904) != null) { return 4; }
            byte* bytes = (byte*)Memory.Allocate(4097);
            if (bytes == null || ((uint64)bytes & 4095) != 0) { return 5; }
            int32 index = 0;
            while (index < 4097) {
                if (bytes[index] != 0) { return 6; }
                bytes[index] = (byte)index;
                index = index + 1;
            }
            if (Memory.TryFree(bytes + 1, 4096)) { return 7; }
            if (Memory.TryFree(bytes, -1)) { return 8; }
            if (bytes[4095] != 255 || bytes[4096] != 0) { return 9; }
            if (!Memory.TryFree(bytes, 4097)) { return 10; }
        ''')

    def test_set_clear_copy_and_unsigned_compare_preserve_guards(self):
        self.run_body('''
            byte* storage = (byte*)Memory.Allocate(128);
            if (storage == null) { return 1; }
            Memory.Set(storage, (char)90, 128);
            if (!Memory.TrySet(storage + 1, (byte)255, 17)) { return 2; }
            if (storage[0] != 90 || storage[18] != 90 || storage[17] != 255) { return 3; }
            Memory.Copy(storage + 65, storage + 1, 17);
            if (Memory.Compare(storage + 1, storage + 65, 17) != 0) { return 4; }
            storage[65] = 128;
            if (Memory.Compare(storage + 1, storage + 65, 17) != 1) { return 5; }
            if (Memory.Compare(storage + 65, storage + 1, 17) != -1) { return 6; }
            Memory.Clear(storage + 1, 17);
            int32 index = 1;
            while (index < 18) {
                if (storage[index] != 0) { return 7; }
                index = index + 1;
            }
            if (storage[0] != 90 || storage[18] != 90 || storage[64] != 90 || storage[82] != 90) { return 8; }
            Memory.Free(storage, 128);
        ''')

    def test_overlap_in_both_directions_and_self_copy(self):
        self.run_body('''
            byte* bytes = (byte*)Memory.Allocate(64);
            if (bytes == null) { return 1; }
            int32 index = 0;
            while (index < 64) { bytes[index] = (byte)index; index = index + 1; }
            Memory.Move(bytes + 3, bytes, 40);
            index = 0;
            while (index < 40) {
                if (bytes[index + 3] != (byte)index) { return 2; }
                index = index + 1;
            }
            Memory.Copy(bytes, bytes + 3, 40);
            if (!Memory.TryCopy(bytes, bytes, 40)) { return 3; }
            index = 0;
            while (index < 40) {
                if (bytes[index] != (byte)index) { return 4; }
                index = index + 1;
            }
            if (bytes[43] != 43 || bytes[63] != 63) { return 5; }
            Memory.Free(bytes, 64);
        ''')

    def test_zero_negative_null_and_overflow_ranges(self):
        self.run_body('''
            byte* bytes = (byte*)Memory.Allocate(8);
            if (bytes == null) { return 1; }
            Memory.Set(bytes, (char)77, 8);
            void* absent = (void*)0;
            void* overflowing = (void*)(uint64)-4;
            if (!Memory.TryMove(absent, absent, 0) || !Memory.TryClear(absent, 0)) { return 2; }
            if (!Memory.TryFree(absent, 0) || Memory.TryFree(absent, 1)) { return 3; }
            if (Memory.TryCopy(bytes, absent, 1) || Memory.TryCopy(absent, bytes, 1)) { return 4; }
            if (Memory.TrySet(bytes, (byte)0, -1) || Memory.TryClear(bytes, -1)) { return 5; }
            if (Memory.TryMove(bytes, bytes, (int64)-9223372036854775808)) { return 6; }
            if (Memory.TryMove(bytes, overflowing, 8) || Memory.TryMove(overflowing, bytes, 8)) { return 7; }
            if (Memory.TrySet(overflowing, (byte)0, 8)) { return 8; }
            Memory.Copy(bytes, absent, 2);
            Memory.Clear(bytes, -1);
            Memory.Set(bytes, (char)0, -1);
            if (Memory.Compare(absent, absent, 0) != 0) { return 9; }
            if (Memory.Compare(bytes, absent, 1) != -2 || Memory.Compare(bytes, bytes, -1) != -2) { return 10; }
            if (Memory.Compare(overflowing, bytes, 8) != -2) { return 11; }
            if (bytes[0] != 77 || bytes[7] != 77) { return 12; }
            Memory.Free(bytes, 8);
        ''')


if __name__ == "__main__":
    unittest.main()
