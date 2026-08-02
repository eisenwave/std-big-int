.text

.section .note.GNU-stack,"",%progbits

/* Return to the text section for code/symbols. */
.text

/* Minimal dummy function to provide a symbol for linking tests / build. */

.globl multiply_long_runtime_dummy
.type multiply_long_runtime_dummy, @function
multiply_long_runtime_dummy:
    /*
     * On x86_64 SysV the first six integer/pointer arguments are in:
     *   rdi, rsi, rdx, rcx, r8, r9
     * Touch the registers with harmless `test` instructions so the
     * assembler / static analyzers can see the args are referenced if
     * that's desired; these instructions do not modify the registers.
     */
    test %rdi, %rdi
    test %rsi, %rsi
    test %rdx, %rdx
    test %rcx, %rcx
    test %r8, %r8
    ret
.size multiply_long_runtime_dummy, .-multiply_long_runtime_dummy
