    .intel_syntax noprefix
    .text
    .globl OverlapYawGuardStubStart
OverlapYawGuardStubStart:
    pushfq
    push rax
    push rcx
    push rdx
    sub rsp, 0x20
    movdqu XMMWORD PTR [rsp], xmm0
    movdqu XMMWORD PTR [rsp + 0x10], xmm1

    # RAX contains two floats copied from controller +0x58/+0x5C. Its high
    # dword is the candidate that the original qword store places at +0x68.
    mov rdx, rax
    shr rdx, 32
    movd xmm0, edx
    movaps xmm1, xmm0
    addss xmm1, DWORD PTR [rbx + 0x114A8]
    subss xmm1, DWORD PTR [rbx + 0x54]
normalize_high:
    comiss xmm1, DWORD PTR [rip + positive_180]
    jp accept_original
    jbe normalize_low
    subss xmm1, DWORD PTR [rip + positive_360]
    jmp normalize_high
normalize_low:
    comiss xmm1, DWORD PTR [rip + negative_180]
    jae observe
    addss xmm1, DWORD PTR [rip + positive_360]
    jmp normalize_low

observe:
    lock inc DWORD PTR [rip + total_writes]
    andps xmm1, XMMWORD PTR [rip + absolute_mask]
    comiss xmm1, DWORD PTR [rip + maximum_delta]
    jbe test_event
    movss DWORD PTR [rip + maximum_delta], xmm1
test_event:
    comiss xmm1, DWORD PTR [rip + positive_20]
    jbe accept_original
    lock inc DWORD PTR [rip + over_20]

    mov eax, 1
    lock xadd DWORD PTR [rip + event_index], eax
    and eax, 63
    shl eax, 6
    lea rcx, [rip + event_ring]
    add rcx, rax
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov QWORD PTR [rcx], rax
    mov QWORD PTR [rcx + 0x08], rbx
    mov edx, DWORD PTR [rsp + 0x30]
    mov DWORD PTR [rcx + 0x10], edx
    mov edx, DWORD PTR [rbx + 0x68]
    mov DWORD PTR [rcx + 0x14], edx
    movss DWORD PTR [rcx + 0x18], xmm0
    mov edx, DWORD PTR [rbx + 0x114A8]
    mov DWORD PTR [rcx + 0x1C], edx
    mov edx, DWORD PTR [rbx + 0x54]
    mov DWORD PTR [rcx + 0x20], edx
    movss DWORD PTR [rcx + 0x24], xmm1

    cmp BYTE PTR [rip + guard_enabled], 0
    je accept_original
    lock inc DWORD PTR [rip + rejected_writes]
    movss xmm0, DWORD PTR [rbx + 0x68]
rebase_high:
    comiss xmm0, DWORD PTR [rip + positive_180]
    jbe rebase_low
    subss xmm0, DWORD PTR [rip + positive_360]
    jmp rebase_high
rebase_low:
    comiss xmm0, DWORD PTR [rip + negative_180]
    jae rebase_ready
    addss xmm0, DWORD PTR [rip + positive_360]
    jmp rebase_low
rebase_ready:
    movss DWORD PTR [rip + replacement_local], xmm0
    jmp rebase_high_dword

accept_original:
    movdqu xmm0, XMMWORD PTR [rsp]
    movdqu xmm1, XMMWORD PTR [rsp + 0x10]
    add rsp, 0x20
    pop rdx
    pop rcx
    pop rax
    popfq
    mov QWORD PTR [r14 + 0x04], rax
    mov BYTE PTR [r15], 1
    ret

rebase_high_dword:
    movdqu xmm0, XMMWORD PTR [rsp]
    movdqu xmm1, XMMWORD PTR [rsp + 0x10]
    add rsp, 0x20
    pop rdx
    pop rcx
    pop rax
    popfq
    mov DWORD PTR [r14 + 0x04], eax
    push rdx
    mov edx, DWORD PTR [rip + replacement_local]
    mov DWORD PTR [r14 + 0x08], edx
    pop rdx
    mov BYTE PTR [r15], 1
    ret

    .p2align 4
absolute_mask:
    .long 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF
positive_180: .float 180.0
negative_180: .float -180.0
positive_360: .float 360.0
positive_20:  .float 20.0

    .p2align 3
    .ascii "OYGUARD1"
total_writes:    .long 0
rejected_writes: .long 0
over_20:         .long 0
event_index:     .long 0
maximum_delta:   .float 0.0
guard_enabled:   .long 0
replacement_local: .float 0.0
reserved1:       .long 0
event_ring:
    .space 4096, 0
