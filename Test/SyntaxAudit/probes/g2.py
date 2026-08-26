import gdb
gdb.execute("set pagination off")
gdb.execute("break _ZN3Sys21InternalInt32ToStringEi")
gdb.execute("run")
print("=== at entry ===")
gdb.execute("x/4gx 0x407000")
gdb.execute("watch -l *(long*)0x407020")
# step over the whole syscall sequence manually
for i in range(30):
    gdb.execute("si", to_string=True)
    try:
        v=gdb.parse_and_eval("*(long*)0x407020")
        pc=gdb.parse_and_eval("$pc")
    except Exception as e:
        break
print("=== after 30 si, pc/value ===")
gdb.execute("x/1gx 0x407020")
gdb.execute("info registers rax rip")
