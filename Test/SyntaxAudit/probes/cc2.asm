0000000000401092 <main>:
  401092:	push   %rbp
  401093:	mov    %rsp,%rbp
  401096:	sub    $0xe0,%rsp
  40109d:	movabs $0x0,%rax
  4010a7:	mov    %rax,-0x10(%rbp)
  4010ae:	jmp    4010b3 <__krt_bb_ea90e208_2>

00000000004010b3 <__krt_bb_ea90e208_2>:
  4010b3:	mov    -0x10(%rbp),%rax
  4010ba:	movabs $0x5,%rcx
  4010c4:	cmp    %rcx,%rax
  4010c7:	setl   %al
  4010ca:	movzbq %al,%rax
  4010ce:	mov    %rax,-0x18(%rbp)
  4010d5:	mov    -0x18(%rbp),%rax
  4010dc:	test   %rax,%rax
  4010df:	jne    4010e5 <__krt_bb_ea90e208_3>

00000000004010e5 <__krt_bb_ea90e208_3>:
  4010e5:	mov    -0x10(%rbp),%rax
  4010ec:	movabs $0x1,%rcx
  4010f6:	add    %rcx,%rax
  4010f9:	mov    %rax,-0x20(%rbp)
  401100:	mov    -0x20(%rbp),%rax
  401107:	mov    %rax,-0x10(%rbp)
  40110e:	mov    -0x10(%rbp),%rax
  401115:	movabs $0x2,%rcx
  40111f:	cmp    %rcx,%rax
  401122:	sete   %al
  401125:	movzbq %al,%rax
  401129:	mov    %rax,-0x28(%rbp)
  401130:	mov    -0x28(%rbp),%rax
  401137:	test   %rax,%rax
  40113a:	jne    401190 <__krt_bb_ea90e208_7>
  401140:	jmp    401156 <__krt_bb_ea90e208_5>
  401145:	movabs $0x1,%rax
  40114f:	mov    %rax,-0x30(%rbp)

0000000000401156 <__krt_bb_ea90e208_5>:
  401156:	mov    -0x10(%rbp),%rax
  40115d:	movabs $0x4,%rcx
  401167:	cmp    %rcx,%rax
  40116a:	sete   %al
  40116d:	movzbq %al,%rax
  401171:	mov    %rax,-0x38(%rbp)
  401178:	mov    -0x38(%rbp),%rax
  40117f:	mov    %rax,-0x30(%rbp)
  401186:	jmp    401190 <__krt_bb_ea90e208_7>
  40118b:	jmp    401190 <__krt_bb_ea90e208_7>

0000000000401190 <__krt_bb_ea90e208_7>:
  401190:	mov    -0x30(%rbp),%rax
  401197:	test   %rax,%rax
  40119a:	jne    4011a5 <__krt_bb_ea90e208_8>
  4011a0:	jmp    4011af <__krt_bb_ea90e208_9>

00000000004011a5 <__krt_bb_ea90e208_8>:
  4011a5:	jmp    4010b3 <__krt_bb_ea90e208_2>
  4011aa:	jmp    4010b3 <__krt_bb_ea90e208_2>

00000000004011af <__krt_bb_ea90e208_9>:
  4011af:	jmp    4011b4 <__krt_bb_ea90e208_10>

00000000004011b4 <__krt_bb_ea90e208_10>:
  4011b4:	movabs $0x402000,%rdi
  4011be:	movabs $0x1,%rsi
  4011c8:	call   401000 <_ZN1PEri>
  4011cd:	mov    %rax,-0x40(%rbp)
  4011d4:	jmp    4010b3 <__krt_bb_ea90e208_2>
  4011d9:	movabs $0x0,%rax
  4011e3:	mov    %rbp,%rsp
  4011e6:	pop    %rbp
  4011e7:	mov    %rax,%rdi
  4011ea:	mov    $0x3c,%rax
  4011f1:	syscall

00000000004011f3 <_start>:
  4011f3:	call   401092 <main>
  4011f8:	mov    %rax,%rdi
  4011fb:	mov    $0x3c,%rax
  401202:	syscall
