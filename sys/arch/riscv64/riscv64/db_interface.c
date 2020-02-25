/*
 * Copyright (c) 1996 Scott K. Stevens
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 *	From: db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Interface to new debugger.
 */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/exec.h>

#include <uvm/uvm_extern.h>

#include <riscv64/db_machdep.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_variables.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <dev/cons.h>

struct db_variable db_regs[] = {
	// XXX
};

extern label_t       *db_recover;

struct db_variable * db_eregs = db_regs + nitems(db_regs);

#ifdef DDB
/*
 *  kdb_trap - field a TRACE or BPT trap
 */
int
kdb_trap(int type, db_regs_t *regs)
{
	// XXX
	panic("kdb_trap");
}
#endif

#define INKERNEL(va)	(((vaddr_t)(va)) & (1ULL << 63))

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(db_addr_t addr, size_t size, char *data)
{
	// XXX
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(db_addr_t addr, size_t size, char *data)
{
	// XXX
}

void
db_enter(void)
{
	// XXX
}

extern vaddr_t esym;
extern vaddr_t end;

void
db_machine_init(void)
{
	// XXX
}

db_addr_t
db_branch_taken(u_int insn, db_addr_t pc, db_regs_t *db_regs)
{
	// XXX
	return pc + 4;
}
