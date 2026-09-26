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
@ Divide each input into a signed quotient and remainder first:
@ x = 100*qx + rx, y = 100*qy + ry.
@ The weighted quotient fits int32 for alpha in [0,100]; weighted remainders
@ are bounded by +/-9900. This avoids overflow without floating point/helpers.
@ R3=100, R4=weighted quotient, R5=scratch quotient, R6=100-alpha.
@ R0=result, R1=final residual; R4-R6 are saved, all other callee-saved untouched.
.thumb_func
ewma_filter:
    PUSH {r4-r6, lr}
    MOV r3, #100
    SDIV r4, r0, r3
    MLS r0, r4, r3, r0       @ new_data remainder
    SDIV r5, r1, r3
    MLS r1, r5, r3, r1       @ old_output remainder
    RSB r6, r2, #100
    MUL r4, r4, r2
    MLA r4, r5, r6, r4       @ weighted quotient
    MUL r0, r0, r2
    MLA r0, r1, r6, r0       @ weighted remainder numerator
    SDIV r5, r0, r3
    MLS r1, r5, r3, r0       @ final residual in [-99,99]
    ADD r0, r4, r5

    @ Separate truncations need correction if integer and residual disagree.
    CMP r0, #0
    BEQ .Ldone
    BLT .Lnegative
    CMP r1, #0
    IT LT
    SUBLT r0, r0, #1
    B .Ldone
.Lnegative:
    CMP r1, #0
    IT GT
    ADDGT r0, r0, #1
.Ldone:
    POP {r4-r6, pc}
.size ewma_filter, .-ewma_filter
