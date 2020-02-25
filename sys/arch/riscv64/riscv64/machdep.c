/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/timetc.h>
#include <sys/sched.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/reboot.h>
#include <sys/mount.h>
#include <sys/exec.h>
#include <sys/user.h>
#include <sys/conf.h>
#include <sys/kcore.h>
#include <sys/core.h>
#include <sys/msgbuf.h>
#include <sys/buf.h>
#include <sys/termios.h>
#include <sys/sensors.h>
#include <sys/syscallargs.h>

#include <net/if.h>
#include <uvm/uvm.h>
#include <dev/cons.h>
#include <dev/clock_subr.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <machine/param.h>
#include <machine/bootconfig.h>
#include <machine/bus.h>
#include <machine/riscv64var.h>

#include <machine/db_machdep.h>
#include <ddb/db_extern.h>

#include <dev/acpi/efi.h>

#include "softraid.h"
#if NSOFTRAID > 0
#include <dev/softraidvar.h>
#endif

char *boot_args = NULL;
char *boot_file = "";

uint8_t *bootmac = NULL;

extern uint64_t esym;

int stdout_node;
int stdout_speed;

void (*cpuresetfn)(void);
void (*powerdownfn)(void);

int cold = 1;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

int physmem;

struct consdev *cn_tab;

caddr_t msgbufaddr;
paddr_t msgbufphys;

struct user *proc0paddr;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

/* the following is used externally (sysctl_hw) */
char    machine[] = MACHINE;            /* from <machine/param.h> */
extern todr_chip_handle_t todr_handle;

int safepri = 0;

struct cpu_info cpu_info_primary;
struct cpu_info *cpu_info[MAXCPUS] = { &cpu_info_primary };

//copied from arm64 directly
#if 0
extern void	com_fdt_init_cons(void);
extern void	imxuart_init_cons(void);
extern void	pluart_init_cons(void);
extern void	simplefb_init_cons(bus_space_tag_t);
#endif

void
consinit(void)
{
	static int consinit_called = 0;

	if (consinit_called != 0)
		return;

	consinit_called = 1;

//XXX TODO: need to check how to reference them
#if 0
	com_fdt_init_cons();
	imxuart_init_cons();
	pluart_init_cons();
	simplefb_init_cons(&riscv64_bs_tag);
#endif
}

//XXX TODO: need to populate console for qemu
struct consdev constab[] = {
	{ NULL }
};

void
cpu_idle_enter()
{
}

void
cpu_idle_cycle()
{
	// XXX Enable interrupts?
	// XXX Data Sync Barrier? (Maybe SFENCE???)
	__asm volatile("wfi");
}

void
cpu_idle_leave()
{
}


// XXX what? - not really used
struct trapframe  proc0tf;
void
cpu_startup()
{
	u_int loop;
	paddr_t minaddr;
	paddr_t maxaddr;

	proc0.p_addr = proc0paddr;

	/*
	 * Give pmap a chance to set up a few more things now the vm
	 * is initialised
	 */
	pmap_postinit();

	/*
	 * Initialize error message buffer (at end of core).
	 */

	/* msgbufphys was setup during the secondary boot strap */
	for (loop = 0; loop < atop(MSGBUFSIZE); ++loop)
		pmap_kenter_pa((vaddr_t)msgbufaddr + loop * PAGE_SIZE,
		    msgbufphys + loop * PAGE_SIZE, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());
	initmsgbuf(msgbufaddr, round_page(MSGBUFSIZE));

	/*
	 * Identify ourselves for the msgbuf (everything printed earlier will
	 * not be buffered).
	 */
	printf("%s", version);

	printf("real mem  = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);


	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   VM_PHYS_SIZE, 0, FALSE, NULL);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024/1024);

	curpcb = &proc0.p_addr->u_pcb;
	curpcb->pcb_flags = 0;
	curpcb->pcb_tf = &proc0tf;

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
		// none supported currently
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

int	waittime = -1;

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown(curproc);

		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}
	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

	if ((howto & RB_DUMP) != 0)
		//dumpsys();//XXX no dump so far. CMPE295

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0) {
			printf("\nAttempting to power down...\n");
			delay(500000);
			if (powerdownfn)
				(*powerdownfn)();
		}

		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cngetc();
	}

doreset:
	printf("rebooting...\n");
	delay(500000);
	if (cpuresetfn)
		(*cpuresetfn)();
	printf("reboot failed; spinning\n");
	for (;;)
		continue;
	/* NOTREACHED */
}

//Copied from ARM64, removed some registers. XXX
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    register_t *retval)
{
	struct trapframe *tf;

	/* If we were using the FPU, forget about it. */
#if 0	// XXX ignore fp for now
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		vfp_discard(p);
#endif
	p->p_addr->u_pcb.pcb_flags &= ~PCB_FPU;

	tf = p->p_addr->u_pcb.pcb_tf;

	memset (tf,0, sizeof(*tf));
	tf->tf_sp = stack;
	tf->tf_ra = pack->ep_entry;
	tf->tf_gp = pack->ep_entry; //XXX 

	retval[1] = 0;
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		aston(ci->ci_curproc);
		cpu_kick(ci);
	}
}


/// XXX ?
/*
 * Size of memory segments, before any memory is stolen.
 */
phys_ram_seg_t mem_clusters[VM_PHYSSEG_MAX];
int     mem_cluster_cnt;
/// XXX ?
/*
 * cpu_dumpsize: calculate size of machine-dependent kernel core dump headers.
 */
int
cpu_dumpsize(void)
{
	int size;

	size = ALIGN(sizeof(kcore_seg_t)) +
	    ALIGN(mem_cluster_cnt * sizeof(phys_ram_seg_t));
	if (roundup(size, dbtob(1)) != dbtob(1))
		return (-1);

	return (1);
}

u_long
cpu_dump_mempagecnt()
{
	return 0;
}

//Copied from ARM64
/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

/*
 * This is called by main to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks, dumpblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpblks = cpu_dumpsize();
	if (dumpblks < 0)
		return;
	dumpblks += ctod(cpu_dump_mempagecnt());

	/* If dump won't fit (incl. room for possible label), punt. */
	if (dumpblks > (nblks - ctod(1)))
		return;

	/* Put dump at end of partition */
	dumplo = nblks - dumpblks;

	/* dumpsize is in page units, and doesn't include headers. */
	dumpsize = cpu_dump_mempagecnt();
}

//copied from arm64/sys_machdep.h
int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysarch_args /* {
		syscallarg(int) op;
		syscallarg(void *) parms;
	} */ *uap = v;
	int error = 0;

	switch(SCARG(uap, op)) {
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

uint64_t mmap_start;
uint32_t mmap_size;
uint32_t mmap_desc_size;
uint32_t mmap_desc_ver;

void	collect_kernel_args(char *);
void	process_kernel_args(void);

void
initriscv(struct riscv_bootparams *rbp)
{
#if 0	// XXX not yet used
	// vaddr_t vstart, vend;
	// struct cpu_info *pcpup;
	// long kvo = 0x0; // XXX VA --> PA delta
	// paddr_t memstart, memend;
#endif  // 0
#if 0
	// XXX What?
	int (*map_func_save)(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
#endif  // 0
	// NOTE: FDT is already mapped (rpb->dtbp_virt & rpb->dtbp_phys)
	
	// struct fdt_reg reg; // XXX not yet used
	void *node;

	node = fdt_find_node("/chosen");
	if (node != NULL) {
		char *prop;
		int len;
		static uint8_t lladdr[6];

		len = fdt_node_property(node, "bootargs", &prop);
		if (len > 0)
			collect_kernel_args(prop);

		len = fdt_node_property(node, "openbsd,bootduid", &prop);
		if (len == sizeof(bootduid))
			memcpy(bootduid, prop, sizeof(bootduid));

		len = fdt_node_property(node, "openbsd,bootmac", &prop);
		if (len == sizeof(lladdr)) {
			memcpy(lladdr, prop, sizeof(lladdr));
			bootmac = lladdr;
		}

		len = fdt_node_property(node, "openbsd,sr-bootuuid", &prop);
#if NSOFTRAID > 0
		if (len == sizeof(sr_bootuuid))
			memcpy(&sr_bootuuid, prop, sizeof(sr_bootuuid));
#endif
		if (len > 0)
			explicit_bzero(prop, len);

		len = fdt_node_property(node, "openbsd,sr-bootkey", &prop);
#if NSOFTRAID > 0
		if (len == sizeof(sr_bootkey))
			memcpy(&sr_bootkey, prop, sizeof(sr_bootkey));
#endif
		if (len > 0)
			explicit_bzero(prop, len);

		len = fdt_node_property(node, "openbsd,uefi-mmap-start", &prop);
		if (len == sizeof(mmap_start))
			mmap_start = bemtoh64((uint64_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-size", &prop);
		if (len == sizeof(mmap_size))
			mmap_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-size", &prop);
		if (len == sizeof(mmap_desc_size))
			mmap_desc_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-ver", &prop);
		if (len == sizeof(mmap_desc_ver))
			mmap_desc_ver = bemtoh32((uint32_t *)prop);

#if 0
		// XXX What?
		len = fdt_node_property(node, "openbsd,uefi-system-table", &prop);
		if (len == sizeof(system_table))
			system_table = bemtoh64((uint64_t *)prop);
#endif
	}

#if 0
	/* Set the pcpu data, this is needed by pmap_bootstrap */
	// smp
	pcpup = &cpu_info_primary;

	/*
	 * Set the pcpu pointer with a backup in tpidr_el1 to be
	 * loaded when entering the kernel from userland.
	 */
	__asm __volatile(
	    "mov x18, %0 \n"
	    "msr tpidr_el1, %0" :: "r"(pcpup));

	cache_setup();

	process_kernel_args();

	void _start(void);
	long kernbase = (long)&_start & ~0x00fff;

	/* The bootloader has loaded us into a 64MB block. */
	memstart = KERNBASE + kvo;
	memend = memstart + 64 * 1024 * 1024;

	/* Bootstrap enough of pmap to enter the kernel proper. */
	vstart = pmap_bootstrap(kvo, rbp->kern_l1pt,
	    kernbase, esym, memstart, memend);

	// XX correctly sized?
	proc0paddr = (struct user *)rbp->kern_stack;

	msgbufaddr = (caddr_t)vstart;
	msgbufphys = pmap_steal_avail(round_page(MSGBUFSIZE), PAGE_SIZE, NULL);
	vstart += round_page(MSGBUFSIZE);

	zero_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;
	copy_src_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;
	copy_dst_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;

	/* Relocate the FDT to safe memory. */
	if (fdt_get_size(config) != 0) {
		uint32_t csize, size = round_page(fdt_get_size(config));
		paddr_t pa;
		vaddr_t va;

		pa = pmap_steal_avail(size, PAGE_SIZE, NULL);
		memcpy((void *)pa, config, size); /* copy to physical */
		for (va = vstart, csize = size; csize > 0;
		    csize -= PAGE_SIZE, va += PAGE_SIZE, pa += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ, PMAP_CACHE_WB);

		fdt = (void *)vstart;
		vstart += size;
	}

	/* Relocate the EFI memory map too. */
	if (mmap_start != 0) {
		uint32_t csize, size = round_page(mmap_size);
		paddr_t pa, startpa, endpa;
		vaddr_t va;

		startpa = trunc_page(mmap_start);
		endpa = round_page(mmap_start + mmap_size);
		for (pa = startpa, va = vstart; pa < endpa;
		    pa += PAGE_SIZE, va += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ, PMAP_CACHE_WB);
		pa = pmap_steal_avail(size, PAGE_SIZE, NULL);
		memcpy((void *)pa, (caddr_t)vstart + (mmap_start - startpa),
		    mmap_size); /* copy to physical */
		pmap_kremove(vstart, endpa - startpa);

		for (va = vstart, csize = size; csize > 0;
		    csize -= PAGE_SIZE, va += PAGE_SIZE, pa += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, PMAP_CACHE_WB);

		mmap = (void *)vstart;
		vstart += size;
	}

	/*
	 * Managed KVM space is what we have claimed up to end of
	 * mapped kernel buffers.
	 */
	{
	// export back to pmap
	extern vaddr_t virtual_avail, virtual_end;
	virtual_avail = vstart;
	vend = VM_MAX_KERNEL_ADDRESS; // XXX
	virtual_end = vend;
	}

	/* Now we can reinit the FDT, using the virtual address. */
	if (fdt)
		fdt_init(fdt);

	// XXX
	int pmap_bootstrap_bs_map(bus_space_tag_t t, bus_addr_t bpa,
	    bus_size_t size, int flags, bus_space_handle_t *bshp);

	map_func_save = arm64_bs_tag._space_map;
	arm64_bs_tag._space_map = pmap_bootstrap_bs_map;

	// cninit
	consinit();

	arm64_bs_tag._space_map = map_func_save;

	/* Remap EFI runtime. */
	if (mmap_start != 0 && system_table != 0)
		remap_efi_runtime(system_table);

	/* XXX */
	pmap_avail_fixup();

	uvmexp.pagesize = PAGE_SIZE;
	uvm_setpagesize();

	/* Make what's left of the initial 64MB block available to UVM. */
	pmap_physload_avail();

	/* Make all other physical memory available to UVM. */
	if (mmap && mmap_desc_ver == EFI_MEMORY_DESCRIPTOR_VERSION) {
		EFI_MEMORY_DESCRIPTOR *desc = mmap;
		int i;

		/*
		 * Load all memory marked as EfiConventionalMemory.
		 * Don't bother with blocks smaller than 64KB.  The
		 * initial 64MB memory block should be marked as
		 * EfiLoaderData so it won't be added again here.
		 */
		for (i = 0; i < mmap_size / mmap_desc_size; i++) {
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
			if (desc->Type == EfiConventionalMemory &&
			    desc->NumberOfPages >= 16) {
				uvm_page_physload(atop(desc->PhysicalStart),
				    atop(desc->PhysicalStart) +
				    desc->NumberOfPages,
				    atop(desc->PhysicalStart),
				    atop(desc->PhysicalStart) +
				    desc->NumberOfPages, 0);
				physmem += desc->NumberOfPages;
			}
			desc = NextMemoryDescriptor(desc, mmap_desc_size);
		}
	} else {
		paddr_t start, end;
		int i;

		node = fdt_find_node("/memory");
		if (node == NULL)
			panic("%s: no memory specified", __func__);

		for (i = 0; i < VM_PHYSSEG_MAX; i++) {
			if (fdt_get_reg(node, i, &reg))
				break;
			if (reg.size == 0)
				continue;

			start = reg.addr;
			end = MIN(reg.addr + reg.size, (paddr_t)-PAGE_SIZE);

			/*
			 * The intial 64MB block is not excluded, so we need
			 * to make sure we don't add it here.
			 */
			if (start < memend && end > memstart) {
				if (start < memstart) {
					uvm_page_physload(atop(start),
					    atop(memstart), atop(start),
					    atop(memstart), 0);
					physmem += atop(memstart - start);
				}
				if (end > memend) {
					uvm_page_physload(atop(memend),
					    atop(end), atop(memend),
					    atop(end), 0);
					physmem += atop(end - memend);
				}
			} else {
				uvm_page_physload(atop(start), atop(end),
				    atop(start), atop(end), 0);
				physmem += atop(end - start);
			}
		}
	}

	/*
	 * Make sure that we have enough KVA to initialize UVM.  In
	 * particular, we need enough KVA to be able to allocate the
	 * vm_page structures.
	 */
	pmap_growkernel(VM_MIN_KERNEL_ADDRESS + 1024 * 1024 * 1024 +
	    physmem * sizeof(struct vm_page));

#ifdef DDB
	db_machine_init();

	/* Firmware doesn't load symbols. */
	ddb_init();

	if (boothowto & RB_KDB)
		db_enter();
#endif

	softintr_init();
	splraise(IPL_IPI);
#endif
}

char bootargs[256];

void
collect_kernel_args(char *args)
{
	/* Make a local copy of the bootargs */
	strlcpy(bootargs, args, sizeof(bootargs));
}

void
process_kernel_args(void)
{
	char *cp = bootargs;

	if (cp[0] == '\0') {
		boothowto = RB_AUTOBOOT;
		return;
	}

	boothowto = 0;
	boot_file = bootargs;

	/* Skip the kernel image filename */
	while (*cp != ' ' && *cp != 0)
		++cp;

	if (*cp != 0)
		*cp++ = 0;

	while (*cp == ' ')
		++cp;

	boot_args = cp;

	printf("bootfile: %s\n", boot_file);
	printf("bootargs: %s\n", boot_args);

	/* Setup pointer to boot flags */
	while (*cp != '-')
		if (*cp++ == '\0')
			return;

	for (;*++cp;) {
		int fl;

		fl = 0;
		switch(*cp) {
		case 'a':
			fl |= RB_ASKNAME;
			break;
		case 'c':
			fl |= RB_CONFIG;
			break;
		case 'd':
			fl |= RB_KDB;
			break;
		case 's':
			fl |= RB_SINGLE;
			break;
		default:
			printf("unknown option `%c'\n", *cp);
			break;
		}
		boothowto |= fl;
	}
}
