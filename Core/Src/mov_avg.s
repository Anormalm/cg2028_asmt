/*
 * mov_avg.s
 *
 * CG2028 Assignment starter file.
 */
.syntax unified
.cpu cortex-m4
.thumb
.global ewma_filter
.type ewma_filter, %function

.text
.align 2

@ CG2028 Assignment
@ (c) ECE NUS
@ Write Student 1's Name here: ABCD (A1234567R)
@ Write Student 2's Name here: WXYZ (A0000007X)
@
@ Function prototype:
@   int ewma_filter(int new_data, int old_output, int alpha_percent);
@
@ ARM calling convention:
@   R0 = new_data       (signed integer sensor sample)
@   R1 = old_output     (previous filtered output)
@   R2 = alpha_percent  (integer from 0 to 100)
@   Return R0 = (alpha_percent * new_data
@                + (100 - alpha_percent) * old_output) / 100
@
@ Notes:
@ - Use signed integer arithmetic.
@ - Integer division must truncate towards zero, matching C integer division.
@ - Preserve all callee-saved registers that you use (R4-R11).
@ - Do not call a C helper function and do not use floating-point instructions.
@
@ Register table:
@   R0 = new_data / return filtered output
@   R1 = old_output
@   R2 = alpha_percent
@   R3 = 100 - alpha_percent
@   R4 = alpha_percent * new_data, then numerator
@   R5 = (100 - alpha_percent) * old_output
@   R6 = constant 100 for division
@   R7 = unused
@
@ Write your program from here.
ewma_filter:
    PUSH {r4-r7, lr}

    @ TODO: Implement the EWMA low-pass filter in pure ARM assembly.
    MOV r3, #100
    SUB r3, r3, r2 @ r3 = (100 - alpha_percent)
    MUL r4, r0, r2 @ r4 = new_data * alpha_percent
    MUL r5, r1, r3 @ r5 = old_output * (100 - alpha_percent)
    ADD r4, r4, r5 @ r4 = nominator

    MOV r6, #100
    SDIV r0, r4, r6 @ r0 = result

    POP  {r4-r7, pc}

.size ewma_filter, .-ewma_filter
