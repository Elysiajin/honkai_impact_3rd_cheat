/* x64 Windows 通用调用跳板。
 * void ov_x64_call(uint64_t fn, const OvCallArgs* args, OvCallResult* result)
 *   OvCallArgs  = { uint64_t gpr[8]; float fpr[4]; }
 *   OvCallResult= { uint64_t rax; float xmm0; }
 * gpr[0..3] 对应 RCX/RDX/R8/R9，gpr[4..7] 压栈；fpr[0..3] 对应 XMM0..3。
 */
    .text
    .globl ov_x64_call
    .def ov_x64_call; .scl 2; .type 32; .endef
    .seh_proc ov_x64_call
ov_x64_call:
    pushq %rbp
    .seh_pushreg %rbp
    movq %rsp, %rbp
    .seh_setframe %rbp, 0
    pushq %rbx
    .seh_pushreg %rbx
    pushq %rsi
    .seh_pushreg %rsi
    pushq %rdi
    .seh_pushreg %rdi
    subq $0x68, %rsp
    .seh_stackalloc 0x68
    .seh_endprologue

    movq %rcx, %r10              /* fn */
    movq %rdx, %r11              /* args */
    movq %r8, %rbx               /* result */

    movss 64(%r11), %xmm0
    movss 68(%r11), %xmm1
    movss 72(%r11), %xmm2
    movss 76(%r11), %xmm3

    movq 32(%r11), %rax
    movq %rax, 32(%rsp)
    movq 40(%r11), %rax
    movq %rax, 40(%rsp)
    movq 48(%r11), %rax
    movq %rax, 48(%rsp)
    movq 56(%r11), %rax
    movq %rax, 56(%rsp)

    movq 0(%r11), %rcx
    movq 8(%r11), %rdx
    movq 16(%r11), %r8
    movq 24(%r11), %r9

    call *%r10

    movq %rax, 0(%rbx)
    movss %xmm0, 8(%rbx)

    addq $0x68, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    popq %rbp
    ret
    .seh_endproc
