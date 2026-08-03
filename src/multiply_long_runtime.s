# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

.intel_syntax noprefix
.text

.section .note.GNU-stack,"",%progbits

# Return to the text section for code/symbols
.text

# Minimal dummy function to provide a symbol for linking tests / build.

.globl multiply_long_runtime
.type multiply_long_runtime, @function

#   rdi -> p_result
#   rsi -> p_a
#   rdx -> len_a
#   rcx -> p_b
#   r8  -> len_b

multiply_long_runtime:
.cfi_startproc

    push    rbx
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbx, 0

    push    r12
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r12, 0

    push    r13
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r13, 0

    push    r14
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r14, 0

.Lgeneric_x64_start:

    xor     ebx, ebx
    xchg    rbx, rdx    # free up rdx for mul instruction

    # now len_a in rbx

    mov     r12, rbx    # r12 = len_a
    shl     r12, 3      # r12 *= 8

    mov     r13, rbx
    shr     rbx, 2      # rbx = len_a / 4
    and     r13, 3      # r13 = len_a % 4

.p2align 4
.Lgeneric_x64_outer_loop_start:

    mov     r10, QWORD PTR [rcx]    # r10 = b[j]
    mov     r14, rbx                # loop_counter = len_a / 4
    xor     r11d, r11d              # temp = 0
    test    r14, r14
    jz      .Lgeneric_x64_before_inner_loop_rmdr

.p2align 4
.Lgeneric_x64_inner_loop_4x_unroll:

.set i, 0
.rept 4

    mov     rax, r10                        # rax = b[j]
    mul     QWORD PTR [rsi + i * 8]         # rdx : rax = a[i] * b[j]

    add     r11, rax                        # temp += low64
    adc     rdx, 0                          # high64 += carry_flag
    add     QWORD PTR [rdi + i * 8], r11    # result[i + j] += temp
    adc     rdx, 0                          # high64 += carry_flag

    mov     r11, rdx                        # temp = high64

.set i, i + 1
.endr

    lea     rsi, [rsi + 32]                     # increment ptr
    lea     rdi, [rdi + 32]                     # increment ptr
    dec     r14                                 # loop_counter--
    jnz     .Lgeneric_x64_inner_loop_4x_unroll  # if (loop_counter != 0) { goto unroll_loop_start; }

.p2align 4
.Lgeneric_x64_before_inner_loop_rmdr:

    mov     r14, r13                # loop_counter = len_a % 4
    test    r14, r14
    jz      .Lgeneric_x64_outer_loop_end

.p2align 4
.Lgeneric_x64_inner_loop_rmdr:

    mov     rax, r10                # rax = b[j]
    mul     QWORD PTR [rsi]         # rdx : rax = a[i] * b[j]

    add     r11, rax                # temp += low64
    adc     rdx, 0                  # high64 += carry_flag
    add     QWORD PTR [rdi], r11    # result[i + j] += temp
    adc     rdx, 0                  # high64 += carry_flag

    mov     r11, rdx                # temp = high64

    lea     rsi, [rsi + 8]                  # increment ptr
    lea     rdi, [rdi + 8]                  # increment ptr
    dec     r14                             # loop_counter--
    jnz     .Lgeneric_x64_inner_loop_rmdr   # if (loop_counter != 0) { goto rmdr_loop_start; }

.p2align 4
.Lgeneric_x64_outer_loop_end:

    mov     QWORD PTR [rdi], r11        # result[len_a + j] = temp
    sub     rsi, r12                    # rollback the a ptr
    sub     rdi, r12                    # rollback the result ptr

    add     rdi, 8                          # increment the result ptr for next iter
    add     rcx, 8                          # increment the b ptr for next iter
    dec     r8                              # len_b--
    jnz     .Lgeneric_x64_outer_loop_start  # if (len_b != 0) { goto outer_loop_start; }

.Lgeneric_x64_end:

    pop     r14
.cfi_adjust_cfa_offset -8

    pop     r13
.cfi_adjust_cfa_offset -8

    pop     r12
.cfi_adjust_cfa_offset -8

    pop     rbx
.cfi_adjust_cfa_offset -8
    ret

.cfi_endproc
.size multiply_long_runtime, .-multiply_long_runtime
