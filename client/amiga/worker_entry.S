/* Kickstart 1.3 CreateProc BCPL seglist convention, as used by FujiBlast. */
        .text
        .balign 4
        .long 16
        .globl _ba_worker_seglist
_ba_worker_seglist:
        .long 0
        jsr _ba_worker_entry
        rts
