.intel_syntax noprefix
.text
.globl fib_asm
.type fib_asm, @function
fib_asm:
    cmp edi, 1
    jg .Lrecurse
    mov eax, edi
    ret
.Lrecurse:
    push rbx
    push r12
    sub rsp, 8                  /* Keep the System V call stack aligned to 16. */
    mov ebx, edi
    sub edi, 1
    call fib_asm
    mov r12d, eax
    lea edi, [rbx - 2]
    call fib_asm
    add eax, r12d
    add rsp, 8
    pop r12
    pop rbx
    ret
.size fib_asm, .-fib_asm
.section .note.GNU-stack,"",@progbits
