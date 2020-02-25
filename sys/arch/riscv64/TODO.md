freebsd uses two sets of 32 floating point registers in reg.h. Not sure why
check whether necessary to support variable {M,S,U}XLEN in riscvreg.h
check sigcontext struct (stolen from freebsd) in include/signal.h
review if necessary to include FP regs in trapframe in include/frame.h
revisit settings in param.h (FDT / ACPI / U-Area / Clusters / Buffer)
atomic instruction constraints in atomic.h may need to switch to +A (if LLVM allows)

/u/bin/src/OpenBSD/riscv/sys/arch/riscv64/compile/GENERIC/obj/machine/atomic.h:28:19: error: invalid operand for instruction
        __asm __volatile("amoor.w zero, %1, %0"
                         ^
<inline asm>:1:25: note: instantiated into assembly here
        amoor.w zero, 32, 0(a0)
                               ^

are physmap vars necessary in bootconfig.h ???


DDB is completely unimplemented. Don't expect it to work.
Look into implementing riscv64/cpufunc.c, if necessary


Look into implementing riscv64/cpufunc.c, if necessary

DDB is completely unimplemented. Don't expect it to work.
PMAP is missing quite a few pieces (TLB Flush, etc.)
Mainbus does not attach sub-devices. It should probably attach the "cpu" device

Interrupts Unimplemented (intr.c)
Interrupt diagnostics unimplemented (intr.h > splassert)

machdep.c missing quite a few methods (should be obvious by linker errors)

switchframe not being initialized on cpu_fork

child_return logic is best-guess. Consider re-evaluting at later point in time
