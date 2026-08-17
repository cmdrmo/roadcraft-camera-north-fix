    .intel_syntax noprefix
    .text
    .globl WrappedYawDeltaStubStart
WrappedYawDeltaStubStart:
    # Replaces: subss xmm1, DWORD PTR [rdi + 0x04]
    # XMM1 enters with desired yaw. XMM0 is dead at this point and the next
    # original instruction overwrites it. Flags from RVA 0x9E60CF remain live
    # until 0x9E6115, so preserve them across all instrumentation.
    pushfq
    movss xmm0, DWORD PTR [rip + last_desired]
    movss DWORD PTR [rip + previous_desired], xmm0
    movss DWORD PTR [rip + last_desired], xmm1
    movss xmm0, DWORD PTR [rip + last_current]
    movss DWORD PTR [rip + previous_current], xmm0
    movss xmm0, DWORD PTR [rdi + 0x04]
    movss DWORD PTR [rip + last_current], xmm0
    subss xmm1, xmm0
    movss DWORD PTR [rip + last_raw], xmm1
    lock inc DWORD PTR [rip + total_writes]

    movaps xmm0, xmm1
    andps xmm0, XMMWORD PTR [rip + absolute_mask]
    comiss xmm0, DWORD PTR [rip + maximum_delta]
    jbe count_large
    movss DWORD PTR [rip + maximum_delta], xmm0
count_large:
    comiss xmm0, DWORD PTR [rip + positive_20]
    jbe test_enabled
    lock inc DWORD PTR [rip + over_20]

test_enabled:
    cmp DWORD PTR [rip + wrap_enabled], 0
    je no_wrap
    comiss xmm1, DWORD PTR [rip + positive_180]
    jp no_wrap
    jbe test_negative

positive_wrap:
    lock inc DWORD PTR [rip + wrapped_frames]
    lock inc DWORD PTR [rip + positive_wraps]
    cmp DWORD PTR [rip + current_run], 0
    jne positive_run_ready
    mov DWORD PTR [rip + transition_direction], 1
    call capture_transition_start
positive_run_ready:
    lock inc DWORD PTR [rip + current_run]
positive_loop:
    subss xmm1, DWORD PTR [rip + positive_360]
    comiss xmm1, DWORD PTR [rip + positive_180]
    ja positive_loop
    jmp wrapped_ready

test_negative:
    comiss xmm1, DWORD PTR [rip + negative_180]
    jae no_wrap

negative_wrap:
    lock inc DWORD PTR [rip + wrapped_frames]
    lock inc DWORD PTR [rip + negative_wraps]
    cmp DWORD PTR [rip + current_run], 0
    jne negative_run_ready
    mov DWORD PTR [rip + transition_direction], -1
    call capture_transition_start
negative_run_ready:
    lock inc DWORD PTR [rip + current_run]
negative_loop:
    addss xmm1, DWORD PTR [rip + positive_360]
    comiss xmm1, DWORD PTR [rip + negative_180]
    jb negative_loop

wrapped_ready:
    movss DWORD PTR [rip + last_wrapped], xmm1
    cmp DWORD PTR [rip + current_run], 1
    jne done
    movss DWORD PTR [rip + transition_wrapped], xmm1
    jmp done

no_wrap:
    mov DWORD PTR [rip + current_run], 0
    movss DWORD PTR [rip + last_wrapped], xmm1
    jmp done

capture_transition_start:
    lock inc DWORD PTR [rip + transition_count]
    movss xmm0, DWORD PTR [rip + last_desired]
    movss DWORD PTR [rip + transition_desired], xmm0
    movss xmm0, DWORD PTR [rip + last_current]
    movss DWORD PTR [rip + transition_current], xmm0
    movss xmm0, DWORD PTR [rip + last_raw]
    movss DWORD PTR [rip + transition_raw], xmm0
    movss xmm0, DWORD PTR [rip + previous_desired]
    movss DWORD PTR [rip + transition_previous_desired], xmm0
    movss xmm0, DWORD PTR [rip + previous_current]
    movss DWORD PTR [rip + transition_previous_current], xmm0
    ret

done:
    popfq
    ret

    .p2align 4
absolute_mask:
    .long 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF
positive_20:  .float 20.0
positive_180: .float 180.0
negative_180: .float -180.0
positive_360: .float 360.0

    .p2align 3
    .ascii "OYGUARD1"
total_writes:         .long 0
wrapped_frames:       .long 0
over_20:              .long 0
positive_wraps:       .long 0
maximum_delta:        .float 0.0
wrap_enabled:         .long 0
negative_wraps:       .long 0
current_run:          .long 0
transition_count:     .long 0
transition_direction: .long 0
transition_desired:   .float 0.0
transition_current:   .float 0.0
transition_raw:       .float 0.0
transition_wrapped:   .float 0.0
transition_previous_desired: .float 0.0
transition_previous_current: .float 0.0
last_desired:         .float 0.0
last_current:         .float 0.0
last_raw:             .float 0.0
last_wrapped:         .float 0.0
previous_desired:     .float 0.0
previous_current:     .float 0.0
