global main

extern malloc
extern puts
extern strlen

	SYS_EXIT    equ 60

section .data
stack_frame: dq 0x0
message: db "Hello, world", 0x10, 0x0

section .text
main:
	push rbp
	mov rbp, rsp
	sub rsp, 20

	mov rdi, message
	call puts

	mov qword [rsp-4], 69
	mov rdi, qword [rsp-4]

	call _exit

_exit:
	mov rax, SYS_EXIT
    syscall
