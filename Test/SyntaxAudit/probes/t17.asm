section .data

section .rodata
string_const_0: db 10, 0

section .text

extern exit
extern _print_int
extern _print_int64
extern _print_float
extern _print_string
extern _println_string
extern Console__WriteLine
extern Console__Write
extern Console__WriteInt
extern KrtMalloc
extern KrtFree
extern KrtRealloc
extern KrtStrcat
extern KrtIntToString
extern KrtDoubleToString
extern KrtPow
extern timer_start
extern timer_start_int
extern timer_elapsed
extern timer_elapsed_int
extern timer_current
extern timer_current_int
extern KrtIsInstance
extern KrtAsInstance
extern KrtNullCoalesce
extern KrtNullConditional
extern KrtCastToInt32
extern KrtCastToInt64
extern KrtCastToFloat32
extern KrtCastToFloat64
extern KrtCastToBool
extern KrtCastToString
extern KrtSizeOfInt32
extern KrtSizeOfInt64
extern KrtSizeOfFloat32
extern KrtSizeOfFloat64
extern KrtSizeOfPointer
extern KrtStringConcat
extern KrtInt32ToString
extern KrtInt64ToString
extern KrtFloat32ToString
extern KrtFloat64ToString
extern KrtBoolToString
extern KrtCreateTuple
extern KrtTupleSetElement
extern KrtTupleGetElement
extern KrtLoadPtr
extern KrtStorePtr
extern KrtStackAlloc
extern KrtPinObject
extern KrtUnpinObject
extern KrtAwaitTask
extern KrtCreateTask
extern KrtCompleteTask
extern KrtLinqWhere
extern KrtLinqOrderBy
extern KrtLinqGroupBy
extern KrtLinqSelect
extern KrtCreateDelegate
extern KrtInvokeDelegate
extern Monitor_Enter
extern Monitor_Exit
extern KrtThrowException
extern KrtRethrowException
extern KrtGetGenericStaticField
extern KrtSetGenericStaticField
extern KrtClearGenericStaticFields
extern KrtIsClass
extern KrtIsStruct
extern KrtImplementsInterface
extern KrtInheritsFrom
extern KrtCheckGenericConstraint


global _ZN1T5ProbeEi
; Function: _ZN1T5ProbeEi
_ZN1T5ProbeEi:
    push rbp
    mov rbp, rsp
    sub rsp, 312
    mov [rbp - 8], rdi
_ZN1T5ProbeEi_entry:
    mov rax, -1
    mov r11, rax
    mov rax, r10
    mov [rbp - 16], rax
    mov rax, [rbp - 16]
    xor r9, r9
    cmp rax, r9
    setl al
    movzx rax, al
    mov r10, rax
_ZN1T5ProbeEi_if_true_0:
    xor rax, rax
    jmp _ZN1T5ProbeEi_epilogue
_ZN1T5ProbeEi_if_false_1:
_ZN1T5ProbeEi_if_end_2:
    mov rax, [rbp - 16]
    mov [rbp - 24], rax
    mov rax, 11
    mov [rbp - 32], rax
    mov rax, [rbp - 8]
    xor r9, r9
    cmp rax, r9
    sete al
    movzx rax, al
    mov r10, rax
_ZN1T5ProbeEi_if_true_3:
    mov rcx, [rbp - 24]
    mov rdx, 10
    mov rax, 48
    mov [rcx + rdx*8], rax
    mov rax, [rbp - 16]
    mov r8, 10
    add rax, r8
    mov r10, rax
    mov rax, r10
    jmp _ZN1T5ProbeEi_epilogue
_ZN1T5ProbeEi_if_false_4:
_ZN1T5ProbeEi_if_end_5:
    mov rax, [rbp - 8]
    mov [rbp - 40], rax
_ZN1T5ProbeEi_while_cond_6:
    mov rax, [rbp - 40]
    xor r9, r9
    cmp rax, r9
    setg al
    movzx rax, al
    mov r10, rax
_ZN1T5ProbeEi_while_body_7:
    mov rax, [rbp - 40]
    mov r8, 10
    test r8, r8
    jz .Ldiv_by_zero__ZN1T5ProbeEi
    xor rdx, rdx
    idiv r8
    mov rax, rdx
    mov r10, rax
    mov rax, r10
    mov [rbp - 48], rax
    mov rax, [rbp - 32]
    mov r8, 1
    sub rax, r8
    mov r10, rax
    mov rax, r10
    mov [rbp - 32], rax
    mov rax, 48
    mov r8, [rbp - 48]
    add rax, r8
    mov r10, rax
    mov rcx, [rbp - 24]
    mov rdx, [rbp - 32]
    mov rax, r10
    mov [rcx + rdx*8], rax
    mov rax, [rbp - 40]
    mov r8, 10
    test r8, r8
    jz .Ldiv_by_zero__ZN1T5ProbeEi
    xor rdx, rdx
    idiv r8
    mov r10, rax
    mov rax, r10
    mov [rbp - 40], rax
_ZN1T5ProbeEi_while_end_8:
    mov rax, [rbp - 16]
    mov r8, [rbp - 32]
    add rax, r8
    mov r10, rax
    mov rax, r10
    jmp _ZN1T5ProbeEi_epilogue
_ZN1T5ProbeEi_epilogue:
    mov rsp, rbp
    pop rbp
    ret
.Ldiv_by_zero__ZN1T5ProbeEi:
    mov rax, -1
    jmp _ZN1T5ProbeEi_epilogue

global _ZN7zz_tailEl
; Function: _ZN7zz_tailEl
_ZN7zz_tailEl:
    push rbp
    mov rbp, rsp
    sub rsp, 312
    mov [rbp - 8], rdi
_ZN7zz_tailEl_entry:
    mov rax, [rbp - 8]
    mov r8, 1
    add rax, r8
    mov r10, rax
    mov rax, r10
    jmp _ZN7zz_tailEl_epilogue
_ZN7zz_tailEl_epilogue:
    mov rsp, rbp
    pop rbp
    ret

global main
; Function: main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 296
main_entry:
    push r10
    mov rdi, 42
    call _ZN1T5ProbeEi
    pop r10
    mov r10, rax
    mov rax, r10
    mov [rbp - 8], rax
    push r10
    lea rdi, [rel string_const_0]
    call _ZN7Console5WriteEr
    pop r10
    mov r10, rax
    xor rdi, rdi
    mov rax, 60
    syscall
main_epilogue:
    mov rsp, rbp
    pop rbp
    ret
