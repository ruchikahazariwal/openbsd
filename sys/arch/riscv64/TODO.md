# Work Items
* CPU Exception Handler (trap.c)
    * Interrupts (Supervisor & User)
    * Exceptions (Supervisor & User)
* Periodic Clock (clock.c)
    * cpu\_initclocks()
    * cpu\_startclock()
    * setstatclockrate()
* Autoconf
    * CPU Attachment 

# Misc Notes / TODOs
* Registers
    * freebsd uses two sets of 32 floating point registers in reg.h. Not sure why
    * check whether necessary to support variable {M,S,U}XLEN in riscvreg.h
    * review if necessary to include FP regs in trapframe in include/frame.h
* DDB
    * Completely unimplemented. Don't expect it to work.
* PMAP
    * Do not expect page table switch to work yet.
    * PMAP structure's pm\_mode is not set. This will definitely cause issues later.
    * Consolidate pm\_mode / pm\_asid / pm\_ppn into single var -- pm\_satp?
    * Physmap vars necessary in bootconfig.h?
* Interrupts
    * check sigcontext struct (stolen from freebsd) in include/signal.h
    * Interrupts Unimplemented (intr.c)
    * Interrupt diagnostics unimplemented (intr.h > splassert)
* Context Switch
    * switchframe not being initialized on cpu\_fork
    * child\_return logic is best-guess. Consider re-evaluting at later point in time
* Misc
    * revisit settings in param.h (FDT / ACPI / U-Area / Clusters / Buffer)
    * Look into implementing riscv64/cpufunc.c, if necessary
