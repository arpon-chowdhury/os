#if ON_APP_TEST
.section .rodata
.balign 16
.global app_probe_start,app_probe_end
app_probe_start: .incbin ON_APP_PROBE_PATH
app_probe_end:
.balign 16
.global app_fault_start,app_fault_end
app_fault_start: .incbin ON_APP_FAULT_PATH
app_fault_end:
.balign 16
.global app_return_start,app_return_end
app_return_start: .incbin ON_APP_RETURN_PATH
app_return_end:
.balign 16
.global app_load_start,app_load_end
app_load_start: .incbin ON_APP_LOAD_PATH
app_load_end:
#endif
.section .note.GNU-stack,"",@progbits
