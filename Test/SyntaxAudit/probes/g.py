import gdb
gdb.execute("set pagination off")
gdb.execute("break _ZN3Sys21InternalInt32ToStringEi")
gdb.execute("run")
print("=== first entry, setting watch ===")
gdb.execute("watch -l *(long*)0x407020")
try:
    gdb.execute("continue")
except gdb.error as e:
    print("err",e)
print("=== stopped ===")
try:
    print("pc:", hex(int(gdb.parse_and_eval("$pc"))))
    gdb.execute("bt 5")
    gdb.execute("info registers rax rdx rsi rdi")
except Exception as e:
    print(e)
