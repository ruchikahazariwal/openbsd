/*
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2008-2009,2014-2016 Dale Rahn <drahn@dalerahn.com>
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
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/pool.h>
#include <sys/atomic.h>

#include <uvm/uvm.h>

#include "machine/vmparam.h"
#include "machine/pmap.h"
#include "machine/cpufunc.h"
#include "machine/pcb.h"

#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_output.h>

struct pmap kernel_pmap_;
struct pmap pmap_tramp;

LIST_HEAD(pted_pv_head, pte_desc);

struct pte_desc {
	LIST_ENTRY(pte_desc) pted_pv_list;
	uint64_t pted_pte;
	pmap_t pted_pmap;
	vaddr_t pted_va;
};

struct pmapvp0 {
	uint64_t l0[VP_IDX0_CNT];
	struct pmapvp1 *vp[VP_IDX0_CNT];
};

struct pmapvp1 {
	uint64_t l1[VP_IDX1_CNT];
	struct pmapvp2 *vp[VP_IDX1_CNT];
};

struct pmapvp2 {
	uint64_t l2[VP_IDX2_CNT];
	struct pte_desc *vp[VP_IDX2_CNT];
};

CTASSERT(sizeof(struct pmapvp0) == sizeof(struct pmapvp1));
CTASSERT(sizeof(struct pmapvp0) == sizeof(struct pmapvp2));

void *pmap_vp_page_alloc(struct pool *, int, int *);
void pmap_vp_page_free(struct pool *, void *);

struct pool_allocator pmap_vp_allocator = {
	pmap_vp_page_alloc, pmap_vp_page_free, sizeof(struct pmapvp0)
};

void pmap_kenter_pa_internal(vaddr_t va, paddr_t pa, vm_prot_t prot, int flags, int cache);

void pmap_remove_pted(pmap_t pm, struct pte_desc *pted);
void pmap_kremove_pg(vaddr_t va);
void pmap_set_l1(struct pmap *, uint64_t, struct pmapvp1 *);
void pmap_set_l2(struct pmap *, uint64_t, struct pmapvp1 *, struct pmapvp2 *);

void pmap_fill_pte(pmap_t pm, vaddr_t va, paddr_t pa, struct pte_desc *pted, vm_prot_t prot, int flags, int cache);
void pmap_pte_insert(struct pte_desc *pted);
void pmap_pte_update(struct pte_desc *pted, uint64_t *pl3);
void pmap_pte_remove(struct pte_desc *pted, int remove_pted);
void pmap_page_ro(pmap_t pm, vaddr_t va, vm_prot_t prot);

void pmap_pinit(pmap_t pm);
void pmap_release(pmap_t pm);

void pmap_enter_pv(struct pte_desc *pted, struct vm_page *);
void pmap_remove_pv(struct pte_desc *pted);

void pmap_reference(pmap_t pm);
void pmap_allocate_asid(pmap_t pm);
void pmap_free_asid(pmap_t pm);

vaddr_t vmmap;
vaddr_t zero_page;
vaddr_t copy_src_page;
vaddr_t copy_dst_page;

/* XXX - panic on pool get failures? */
struct pool pmap_pmap_pool;
struct pool pmap_pted_pool;
struct pool pmap_vp_pool;

int pmap_initialized = 0;

static inline void
pmap_lock(struct pmap *pmap)
{
	if (pmap != pmap_kernel())
		mtx_enter(&pmap->pm_mtx);
}

static inline void
pmap_unlock(struct pmap *pmap)
{
	if (pmap != pmap_kernel())
		mtx_leave(&pmap->pm_mtx);
}

static inline int
VP_IDX0(vaddr_t va)
{
	return (va >> VP_IDX0_POS) & VP_IDX0_MASK;
}

static inline int
VP_IDX1(vaddr_t va)
{
	return (va >> VP_IDX1_POS) & VP_IDX1_MASK;
}

static inline int
VP_IDX2(vaddr_t va)
{
	return (va >> VP_IDX2_POS) & VP_IDX2_MASK;
}

void
pmap_pinit(pmap_t pm)
{
	vaddr_t l0va;

	while (pm->pm_vp0 == NULL) {
		pm->pm_vp0 = pool_get(&pmap_vp_pool, PR_WAITOK | PR_ZERO);
		l0va = (vaddr_t)pm->pm_vp0;
	}

	pmap_extract(pmap_kernel(), l0va, (paddr_t *)&pm->pm_pa0);
	pmap_allocate_asid(pm);
	pmap_reference(pm);
}

#define NUM_ASID (1 << 16)
uint32_t pmap_asid[NUM_ASID / 32];

void
pmap_allocate_asid(pmap_t pm)
{
	uint32_t bits;
	int asid, bit;

	for (;;) {
		do {
			asid = arc4random() & (NUM_ASID - 2);
			bit = (asid & (32 - 1));
			bits = pmap_asid[asid / 32];
		} while (asid == 0 || (bits & (3U << bit)));

		if (atomic_cas_uint(&pmap_asid[asid / 32], bits,
		    bits | (3U << bit)) == bits)
			break;
	}
	pm->pm_asid = asid;
}

void
pmap_free_asid(pmap_t pm)
{
	uint32_t bits;
	int bit;

	KASSERT(pm != curcpu()->ci_curpm);
	// XXX TLB Flush?
	// cpu_tlb_flush_asid_all((uint64_t)pm->pm_asid << 48);
	// cpu_tlb_flush_asid_all((uint64_t)(pm->pm_asid | ASID_USER) << 48);

	bit = (pm->pm_asid & (32 - 1));
	for (;;) {
		bits = pmap_asid[pm->pm_asid / 32];
		if (atomic_cas_uint(&pmap_asid[pm->pm_asid / 32], bits,
		    bits & ~(3U << bit)) == bits)
			break;
	}
}

struct pte_desc *
pmap_vp_lookup(pmap_t pm, vaddr_t va, uint64_t **pl2entry)
{
	struct pmapvp1 *vp1;
	struct pmapvp2 *vp2;
	struct pte_desc *pted;

	vp1 = pm->pm_vp0->vp[VP_IDX0(va)];
	if (vp1 == NULL) {
		return NULL;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		return NULL;
	}

	pted = vp2->vp[VP_IDX2(va)];
	if (pl2entry != NULL)
		*pl2entry = &(vp2->l2[VP_IDX2(va)]);

	return pted;
}

struct pte_desc *
pmap_vp_remove(pmap_t pm, vaddr_t va)
{
	struct pmapvp1 *vp1;
	struct pmapvp2 *vp2;
	struct pte_desc *pted;

	vp1 = pm->pm_vp0->vp[VP_IDX0(va)];
	if (vp1 == NULL) {
		return NULL;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		return NULL;
	}

	pted = vp2->vp[VP_IDX2(va)];
	vp2->vp[VP_IDX2(va)] = NULL;

	return pted;
}

int
pmap_vp_enter(pmap_t pm, vaddr_t va, struct pte_desc *pted, int flags)
{
	struct pmapvp1 *vp1;
	struct pmapvp2 *vp2;

	vp1 = pm->pm_vp0->vp[VP_IDX0(va)];
	if (vp1 == NULL) {
		vp1 = pool_get(&pmap_vp_pool, PR_NOWAIT | PR_ZERO);
		if (vp1 == NULL) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("%s: unable to allocate L1", __func__);
			return ENOMEM;
		}
		pmap_set_l1(pm, va, vp1);
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		vp2 = pool_get(&pmap_vp_pool, PR_NOWAIT | PR_ZERO);
		if (vp2 == NULL) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("%s: unable to allocate L2", __func__);
			return ENOMEM;
		}
		pmap_set_l2(pm, va, vp1, vp2);
	}

	vp2->vp[VP_IDX2(va)] = pted;
	return 0;
}

void *
pmap_vp_page_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_trylock = ISSET(flags, PR_NOWAIT);
	kd.kd_slowdown = slowdown;

	return km_alloc(pp->pr_pgsize, &kv_any, &kp_dirty, &kd);
}

void
pmap_vp_page_free(struct pool *pp, void *v)
{
	km_free(v, pp->pr_pgsize, &kv_any, &kp_dirty);
}

u_int32_t PTED_MANAGED(struct pte_desc *pted);
u_int32_t PTED_WIRED(struct pte_desc *pted);
u_int32_t PTED_VALID(struct pte_desc *pted);

u_int32_t
PTED_MANAGED(struct pte_desc *pted)
{
	return (pted->pted_va & PTED_VA_MANAGED_M);
}

u_int32_t
PTED_WIRED(struct pte_desc *pted)
{
	return (pted->pted_va & PTED_VA_WIRED_M);
}

u_int32_t
PTED_VALID(struct pte_desc *pted)
{
	return (pted->pted_pte != 0);
}

void
pmap_enter_pv(struct pte_desc *pted, struct vm_page *pg)
{
	/*
	 * XXX does this test mean that some pages try to be managed,
	 * but this is called too soon?
	 */
	if (__predict_false(!pmap_initialized))
		return;

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_INSERT_HEAD(&(pg->mdpage.pv_list), pted, pted_pv_list);
	pted->pted_va |= PTED_VA_MANAGED_M;
	mtx_leave(&pg->mdpage.pv_mtx);
}

void
pmap_remove_pv(struct pte_desc *pted)
{
	struct vm_page *pg = PHYS_TO_VM_PAGE(pted->pted_pte & PTE_RPGN);

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_REMOVE(pted, pted_pv_list);
	mtx_leave(&pg->mdpage.pv_mtx);
}

int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	struct pte_desc *pted;
	struct vm_page *pg;
	int error;
	int cache = PMAP_CACHE_WB;
	int need_sync = 0;

	if (pa & PMAP_NOCACHE)
		cache = PMAP_CACHE_CI;
	if (pa & PMAP_DEVICE)
		cache = PMAP_CACHE_DEV;
	pg = PHYS_TO_VM_PAGE(pa);

	pmap_lock(pm);
	pted = pmap_vp_lookup(pm, va, NULL);
	if (pted && PTED_VALID(pted)) {
		pmap_remove_pted(pm, pted);
		/* we lost our pted if it was user */
		if (pm != pmap_kernel())
			pted = pmap_vp_lookup(pm, va, NULL);
	}

	pm->pm_stats.resident_count++;

	/* Do not have pted for this, get one and put it in VP */
	if (pted == NULL) {
		pted = pool_get(&pmap_pted_pool, PR_NOWAIT | PR_ZERO);
		if (pted == NULL) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("%s: failed to allocate pted", __func__);
			error = ENOMEM;
			goto out;
		}
		if (pmap_vp_enter(pm, va, pted, flags)) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("%s: failed to allocate L2/L3", __func__);
			error = ENOMEM;
			pool_put(&pmap_pted_pool, pted);
			goto out;
		}
	}

	/*
	 * If it should be enabled _right now_, we can skip doing ref/mod
	 * emulation. Any access includes reference, modified only by write.
	 */
	if (pg != NULL &&
	    ((flags & PROT_MASK) || (pg->pg_flags & PG_PMAP_REF))) {
		atomic_setbits_int(&pg->pg_flags, PG_PMAP_REF);
		if ((prot & PROT_WRITE) && (flags & PROT_WRITE)) {
			atomic_setbits_int(&pg->pg_flags, PG_PMAP_MOD);
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
		}
	}

	pmap_fill_pte(pm, va, pa, pted, prot, flags, cache);

	if (pg != NULL) {
		pmap_enter_pv(pted, pg); /* only managed mem */
	}

	/*
	 * Insert into table, if this mapping said it needed to be mapped
	 * now.
	 */
	if (flags & (PROT_READ|PROT_WRITE|PROT_EXEC|PMAP_WIRED)) {
		pmap_pte_insert(pted);
	}

	// XXX Do a TLB Flush
	// ttlb_flush(pm, va & ~PAGE_MASK);

	if (pg != NULL && (flags & PROT_EXEC)) {
		need_sync = ((pg->pg_flags & PG_PMAP_EXE) == 0);
		atomic_setbits_int(&pg->pg_flags, PG_PMAP_EXE);
	}

	// XXX Sync Instruction Cache
	// if (need_sync && (pm == pmap_kernel() || (curproc &&
	//     curproc->p_vmspace->vm_map.pmap == pm)))
	// 	cpu_icache_sync_range(va & ~PAGE_MASK, PAGE_SIZE);

	error = 0;
out:
	pmap_unlock(pm);
	return error;
}

void
pmap_remove(pmap_t pm, vaddr_t sva, vaddr_t eva)
{
	struct pte_desc *pted;
	vaddr_t va;

	pmap_lock(pm);
	for (va = sva; va < eva; va += PAGE_SIZE) {
		pted = pmap_vp_lookup(pm, va, NULL);

		if (pted == NULL)
			continue;

		if (pted->pted_va & PTED_VA_WIRED_M) {
			pm->pm_stats.wired_count--;
			pted->pted_va &= ~PTED_VA_WIRED_M;
		}

		if (PTED_VALID(pted))
			pmap_remove_pted(pm, pted);
	}
	pmap_unlock(pm);
}

void
pmap_remove_pted(pmap_t pm, struct pte_desc *pted)
{
	pm->pm_stats.resident_count--;

	if (pted->pted_va & PTED_VA_WIRED_M) {
		pm->pm_stats.wired_count--;
		pted->pted_va &= ~PTED_VA_WIRED_M;
	}

	pmap_pte_remove(pted, pm != pmap_kernel());

	// XXX TLB Flush
	// ttlb_flush(pm, pted->pted_va & ~PAGE_MASK);

	if (pted->pted_va & PTED_VA_EXEC_M) {
		pted->pted_va &= ~PTED_VA_EXEC_M;
	}

	if (PTED_MANAGED(pted))
		pmap_remove_pv(pted);

	pted->pted_pte = 0;
	pted->pted_va = 0;

	if (pm != pmap_kernel())
		pool_put(&pmap_pted_pool, pted);
}

void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	pmap_kenter_pa_internal(va, pa, prot, prot,
	    (pa & PMAP_NOCACHE) ? PMAP_CACHE_CI : PMAP_CACHE_WB);
}

void
pmap_kenter_cache(vaddr_t va, paddr_t pa, vm_prot_t prot, int cacheable)
{
	pmap_kenter_pa_internal(va, pa, prot, prot, cacheable);
}


void
pmap_kenter_pa_internal(vaddr_t va, paddr_t pa, vm_prot_t prot, int flags, int cache)
{
	pmap_t pm = pmap_kernel();
	struct pte_desc *pted;

	pted = pmap_vp_lookup(pm, va, NULL);

	/* Do not have pted for this, get one and put it in VP */
	if (pted == NULL) {
		panic("pted not preallocated in pmap_kernel() va %lx pa %lx\n",
		    va, pa);
	}

	if (pted && PTED_VALID(pted))
		pmap_kremove_pg(va); /* pted is reused */

	pm->pm_stats.resident_count++;

	flags |= PMAP_WIRED; /* kernel mappings are always wired. */
	/* Calculate PTE */
	pmap_fill_pte(pm, va, pa, pted, prot, flags, cache);

	/*
	 * Insert into table
	 * We were told to map the page, probably called from vm_fault,
	 * so map the page!
	 */
	pmap_pte_insert(pted);

	// XXX TLB Flush?
	// ttlb_flush(pm, va & ~PAGE_MASK);
	// XXX Writeback Invalidate? Investigate further...
	// if (cache == PMAP_CACHE_CI || cache == PMAP_CACHE_DEV)
	// 	cpu_idcache_wbinv_range(va & ~PAGE_MASK, PAGE_SIZE);
}

void
pmap_kremove(vaddr_t va, vsize_t len)
{
	for (len >>= PAGE_SHIFT; len > 0; len--, va += PAGE_SIZE) {
		pmap_kremove_pg(va);
	}
}

void
pmap_kremove_pg(vaddr_t va)
{
	pmap_t pm = pmap_kernel();
	struct pte_desc *pted;
	int s;

	pmap_vp_lookup(pm, va, NULL);
	if (pted == NULL) {
		return;
	}

	if (!PTED_VALID(pted)) {
		return; /* page is not mapped */
	}

	s = splvm();

	pm->pm_stats.resident_count--;

	/* XXX Need to actually remove the pted */
}

void
pmap_set_l1(struct pmap *pm, uint64_t va, struct pmapvp1 *l1_va)
{
	uint64_t pg_entry;
	paddr_t l1_pa;
	int idx0;

	if (pmap_extract(pmap_kernel(), (vaddr_t)l1_va, &l1_pa) == 0)
		panic("unable to find vp pa mapping %p\n", l1_va);

	if (l1_pa & (Lx_TABLE_ALIGN-1))
		panic("misaligned L1 table\n");

	// XXX Massage PA to pg_entry
	pg_entry = l1_pa;

	idx0 = VP_IDX0(va);
	pm->pm_vp0->vp[idx0] = l1_va;
	pm->pm_vp0->l0[idx0] = pg_entry;
}

void
pmap_set_l2(struct pmap *pm, uint64_t va, struct pmapvp1 *vp1,
    struct pmapvp2 *l2_va)
{
	uint64_t pg_entry;
	paddr_t l2_pa;
	int idx1;

	if (pmap_extract(pmap_kernel(), (vaddr_t)l2_va, &l2_pa) == 0)
		panic("unable to find vp pa mapping %p\n", l2_va);

	if (l2_pa & (Lx_TABLE_ALIGN-1))
		panic("misaligned L2 table\n");

	// XXX Massage PA to pg_entry
	pg_entry = l2_pa;

	idx1 = VP_IDX1(va);
	vp1->vp[idx1] = l2_va;
	vp1->l1[idx1] = pg_entry;
}

void
pmap_fill_pte(pmap_t pm, vaddr_t va, paddr_t pa, struct pte_desc *pted,
    vm_prot_t prot, int flags, int cache)
{
	pted->pted_va = va;
	pted->pted_pmap = pm;

	switch (cache) {
	case PMAP_CACHE_WB:
		break;
	case PMAP_CACHE_WT:
		break;
	case PMAP_CACHE_CI:
		break;
	case PMAP_CACHE_DEV:
		break;
	default:
		panic("pmap_fill_pte:invalid cache mode");
	}
	pted->pted_va |= cache;

	pted->pted_va |= prot & (PROT_READ|PROT_WRITE|PROT_EXEC);

	if (flags & PMAP_WIRED) {
		pted->pted_va |= PTED_VA_WIRED_M;
		pm->pm_stats.wired_count++;
	}

	pted->pted_pte = pa & PTE_RPGN;
	pted->pted_pte |= flags & (PROT_READ|PROT_WRITE|PROT_EXEC);
}

void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
	struct pmap *pm = vm_map_pmap(&pr->ps_vmspace->vm_map);
	vaddr_t kva = zero_page + cpu_number() * PAGE_SIZE;
	paddr_t pa;
	vsize_t clen;
	vsize_t off;

	/*
	 * If we're caled for the current processes, we can simply
	 * flush the data cache to the point of unification and
	 * invalidate the instruction cache.
	 */
	// XXX Sync Instruction Cache
	// if (pr == curproc->p_p) {
	// 	cpu_icache_sync_range(va, len);
	// 	return;
	// }

	/*
	 * Flush and invalidate through an aliased mapping.  This
	 * assumes the instruction cache is PIPT.  That is only true
	 * for some of the hardware we run on.
	 */
	while (len > 0) {
		/* add one to always round up to the next page */
		clen = round_page(va + 1) - va;
		if (clen > len)
			clen = len;

		off = va - trunc_page(va);
		if (pmap_extract(pm, trunc_page(va), &pa)) {
			pmap_kenter_pa(kva, pa, PROT_READ|PROT_WRITE);
			// XXX Sync Instruction Cache
			// cpu_icache_sync_range(kva + off, clen);
			pmap_kremove_pg(kva);
		}

		len -= clen;
		va += clen;
	}
}

void
pmap_pte_insert(struct pte_desc *pted)
{
	/* put entry into table */
	/* need to deal with ref/change here */
	pmap_t pm = pted->pted_pmap;
	uint64_t *pl3;

	if (pmap_vp_lookup(pm, pted->pted_va, &pl3) == NULL) {
		panic("%s: have a pted, but missing a vp"
		    " for %lx va pmap %p", __func__, pted->pted_va, pm);
	}

	pmap_pte_update(pted, pl3);
}

void
pmap_pte_update(struct pte_desc *pted, uint64_t *pl3)
{
	uint64_t pte = 0, access_bits = 0, attr = 0;
	// pmap_t pm = pted->pted_pmap;

	/* see mair in locore.S */
	// XXX Attribute Bits
	// switch (pted->pted_va & pmap_cache_bits) {
	// case pmap_cache_wb:
	// 	/* inner and outer writeback */
	// 	attr |= attr_idx(pte_attr_wb);
	// 	attr |= attr_sh(sh_inner);
	// 	break;
	// case pmap_cache_wt:
	// 	 /* inner and outer writethrough */
	// 	attr |= attr_idx(pte_attr_wt);
	// 	attr |= attr_sh(sh_inner);
	// 	break;
	// case pmap_cache_ci:
	// 	attr |= attr_idx(pte_attr_ci);
	// 	attr |= attr_sh(sh_inner);
	// 	break;
	// case pmap_cache_dev:
	// 	attr |= attr_idx(pte_attr_dev);
	// 	attr |= attr_sh(sh_inner);
	// 	break;
	// default:
	// 	panic("pmap_pte_insert: invalid cache mode");
	// }

	// XXX Access Protect Bits
	// if (pm->pm_privileged)
	// 	access_bits = ap_bits_kern[pted->pted_pte & PROT_MASK];
	// else
	// 	access_bits = ap_bits_user[pted->pted_pte & PROT_MASK];

	// XXX Construct the PTE -- double-check this
	pte = (pted->pted_pte & PTE_RPGN) | attr | access_bits;
	*pl3 = pte;
}

void
pmap_pte_remove(struct pte_desc *pted, int remove_pted)
{
	/* put entry into table */
	/* need to deal with ref/change here */
	struct pmapvp1 *vp1;
	struct pmapvp2 *vp2;
	pmap_t pm = pted->pted_pmap;

	vp1 = pm->pm_vp0->vp[VP_IDX0(pted->pted_va)];
	if (vp1 == NULL) {
		panic("have a pted, but missing the l1 for %lx va pmap %p",
		    pted->pted_va, pm);
	}
	vp2 = vp1->vp[VP_IDX1(pted->pted_va)];
	if (vp2 == NULL) {
		panic("have a pted, but missing the l2 for %lx va pmap %p",
		    pted->pted_va, pm);
	}
	vp2->l2[VP_IDX2(pted->pted_va)] = 0;
	if (remove_pted)
		vp2->vp[VP_IDX2(pted->pted_va)] = NULL;

	// TLB Flush?
	// ttlb_flush(pm, pted->pted_va);
}

/*
 * activate a pmap entry
 */
void
pmap_activate(struct proc *p)
{
	// XXX Write SATP CSR to set ASID + PPN
#if 0
	pmap_t pm = p->p_vmspace->vm_map.pmap;
	int psw;

	psw = disable_interrupts();
	if (p == curproc && pm != curcpu()->ci_curpm)
		pmap_setttb(p);
	restore_interrupts(psw);
#endif
}

/*
 * deactivate a pmap entry
 */
void
pmap_deactivate(struct proc *p)
{
}

/*
 * Get the physical page address for the given pmap/virtual address.
 */
boolean_t
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pa)
{
	struct pte_desc *pted;

	pted = pmap_vp_lookup(pm, va, NULL);

	if (pted == NULL)
		return FALSE;

	if (pted->pted_pte == 0)
		return FALSE;

	if (pa != NULL)
		*pa = (pted->pted_pte & PTE_RPGN) | (va & PAGE_MASK);

	return TRUE;
}

void
pmap_page_ro(pmap_t pm, vaddr_t va, vm_prot_t prot)
{
	struct pte_desc *pted;
	uint64_t *pl3;

	/* Every VA needs a pted, even unmanaged ones. */
	pted = pmap_vp_lookup(pm, va, &pl3);
	if (!pted || !PTED_VALID(pted)) {
		return;
	}

	pted->pted_va &= ~PROT_WRITE;
	pted->pted_pte &= ~PROT_WRITE;
	if ((prot & PROT_EXEC) == 0) {
		pted->pted_va &= ~PROT_EXEC;
		pted->pted_pte &= ~PROT_EXEC;
	}
	pmap_pte_update(pted, pl3);

	// XXX TLB Flush?
	// ttlb_flush(pm, pted->pted_va & ~PAGE_MASK);

	return;
}

void
pmap_collect(pmap_t pm)
{
	// XXX Optional Function
}

void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	vaddr_t va = zero_page + cpu_number() * PAGE_SIZE;

	pmap_kenter_pa(va, pa, PROT_READ|PROT_WRITE);
	pagezero(va);
	pmap_kremove_pg(va);
}

void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t srcpa = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dstpa = VM_PAGE_TO_PHYS(dstpg);
	vaddr_t srcva = copy_src_page + cpu_number() * PAGE_SIZE;
	vaddr_t dstva = copy_dst_page + cpu_number() * PAGE_SIZE;

	pmap_kenter_pa(srcva, srcpa, PROT_READ);
	pmap_kenter_pa(dstva, dstpa, PROT_READ|PROT_WRITE);
	memcpy((void *)dstva, (void *)srcva, PAGE_SIZE);
	pmap_kremove_pg(srcva);
	pmap_kremove_pg(dstva);
}

int pmap_vp_poolcache = 0; /* force vp poolcache to allocate late */

pmap_t
pmap_create(void)
{
	pmap_t pmap;
	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK | PR_ZERO);

	mtx_init(&pmap->pm_mtx, IPL_VM);

	pmap_pinit(pmap);
	// Conditional below is stolen from arm64 pmap impl.
	// XXX Why does this happen here? Investigate further.
	if (pmap_vp_poolcache == 0) {
		pool_setlowat(&pmap_vp_pool, 20);
		pmap_vp_poolcache = 20;
	}
	return (pmap);
}

void
pmap_reference(pmap_t pm)
{
	atomic_inc_int(&pm->pm_refs);
}

void
pmap_destroy(pmap_t pm)
{
	int refs;

	refs = atomic_dec_int_nv(&pm->pm_refs);
	if (refs > 0)
		return;

	/*
	 * reference count is zero, free pmap resources and free pmap.
	 */
	pmap_release(pm);
	pmap_free_asid(pm);
	pool_put(&pmap_pmap_pool, pm);
}

void
pmap_release(pmap_t pm)
{
	struct pmapvp0 *vp0;
	struct pmapvp1 *vp1;
	struct pmapvp2 *vp2;
	struct pte_desc *pted;
	int i, j, k;

	vp0 = pm->pm_vp0;
	for (i = 0; i < VP_IDX0_CNT; i++) {
		vp1 = vp0->vp[i];
		if (vp1 == NULL)
			continue;
		vp0->vp[i] = NULL;

		for (j = 0; j < VP_IDX1_CNT; j++) {
			vp2 = vp1->vp[j];
			if (vp2 == NULL)
				continue;
			vp1->vp[j] = NULL;

			for (k = 0; k < VP_IDX2_CNT; k++) {
				pted = vp2->vp[k];
				if (pted == NULL)
					continue;
				vp2->vp[k] = NULL;

				pool_put(&pmap_pted_pool, pted);
			}

			pool_put(&pmap_vp_pool, vp2);
		}
		pool_put(&pmap_vp_pool, vp1);
	}
	pool_put(&pmap_vp_pool, vp0);
	pm->pm_vp0 = NULL;
}

/*
 * Initialize pmap setup.
 * ALL of the code which deals with avail needs rewritten as an actual
 * memory allocation.
 */
CTASSERT(sizeof(struct pmapvp0) == 2 * PAGE_SIZE);

void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	struct pte_desc *pted;
	struct pmap *pm;

	if (prot != PROT_NONE) {
		mtx_enter(&pg->mdpage.pv_mtx);
		LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list) {
			pmap_page_ro(pted->pted_pmap, pted->pted_va, prot);
		}
		mtx_leave(&pg->mdpage.pv_mtx);
		return;
	}

	mtx_enter(&pg->mdpage.pv_mtx);
	while ((pted = LIST_FIRST(&(pg->mdpage.pv_list))) != NULL) {
		pmap_reference(pted->pted_pmap);
		pm = pted->pted_pmap;
		mtx_leave(&pg->mdpage.pv_mtx);

		pmap_lock(pm);

		/*
		 * We dropped the pvlist lock before grabbing the pmap
		 * lock to avoid lock ordering problems.  This means
		 * we have to check the pvlist again since somebody
		 * else might have modified it.  All we care about is
		 * that the pvlist entry matches the pmap we just
		 * locked.  If it doesn't, unlock the pmap and try
		 * again.
		 */
		mtx_enter(&pg->mdpage.pv_mtx);
		pted = LIST_FIRST(&(pg->mdpage.pv_list));
		if (pted == NULL || pted->pted_pmap != pm) {
			mtx_leave(&pg->mdpage.pv_mtx);
			pmap_unlock(pm);
			pmap_destroy(pm);
			mtx_enter(&pg->mdpage.pv_mtx);
			continue;
		}
		mtx_leave(&pg->mdpage.pv_mtx);

		pmap_remove_pted(pm, pted);
		pmap_unlock(pm);
		pmap_destroy(pm);

		mtx_enter(&pg->mdpage.pv_mtx);
	}
	/* page is being reclaimed, sync icache next use */
	atomic_clearbits_int(&pg->pg_flags, PG_PMAP_EXE);
	mtx_leave(&pg->mdpage.pv_mtx);
}

void
pmap_protect(pmap_t pm, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	if (prot & (PROT_READ | PROT_EXEC)) {
		pmap_lock(pm);
		while (sva < eva) {
			pmap_page_ro(pm, sva, prot);
			sva += PAGE_SIZE;
		}
		pmap_unlock(pm);
		return;
	}
	pmap_remove(pm, sva, eva);
}

void
pmap_init(void)
{
	// XXX Rewrite SATP CSR to set MODE, ASID, PPN?

	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmap", NULL);
	pool_setlowat(&pmap_pmap_pool, 2);
	pool_init(&pmap_pted_pool, sizeof(struct pte_desc), 0, IPL_VM, 0,
	    "pted", NULL);
	pool_setlowat(&pmap_pted_pool, 20);
	pool_init(&pmap_vp_pool, sizeof(struct pmapvp0), PAGE_SIZE, IPL_VM, 0,
	    "vp", &pmap_vp_allocator);
}

void
pmap_postinit(void)
{
	// XXX pmap_postinit
#if 0
	extern char trampoline_vectors[];
	paddr_t pa;
	vaddr_t minaddr, maxaddr;
	u_long npteds, npages;

	memset(pmap_tramp.pm_vp.l1, 0, sizeof(struct pmapvp1));
	pmap_extract(pmap_kernel(), (vaddr_t)trampoline_vectors, &pa);
	pmap_enter(&pmap_tramp, (vaddr_t)trampoline_vectors, pa,
	    PROT_READ | PROT_EXEC, PROT_READ | PROT_EXEC | PMAP_WIRED);

	/*
	 * Reserve enough virtual address space to grow the kernel
	 * page tables.  We need a descriptor for each page as well as
	 * an extra page for level 1/2/3 page tables for management.
	 * To simplify the code, we always allocate full tables at
	 * level 3, so take that into account.
	 */
	npteds = (VM_MAX_KERNEL_ADDRESS - pmap_maxkvaddr + 1) / PAGE_SIZE;
	npteds = roundup(npteds, VP_IDX3_CNT);
	npages = howmany(npteds, PAGE_SIZE / (sizeof(struct pte_desc)));
	npages += 2 * howmany(npteds, VP_IDX3_CNT);
	npages += 2 * howmany(npteds, VP_IDX3_CNT * VP_IDX2_CNT);
	npages += 2 * howmany(npteds, VP_IDX3_CNT * VP_IDX2_CNT * VP_IDX1_CNT);

	/*
	 * Use an interrupt safe map such that we don't recurse into
	 * uvm_map() to allocate map entries.
	 */
	minaddr = vm_map_min(kernel_map);
	pmap_kvp_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    npages * PAGE_SIZE, VM_MAP_INTRSAFE, FALSE, NULL);
#endif
}

void
pmap_update(pmap_t pm)
{
	// XXX Optional Function
}

int
pmap_is_referenced(struct vm_page *pg)
{
	return ((pg->pg_flags & PG_PMAP_REF) != 0);
}

int
pmap_is_modified(struct vm_page *pg)
{
	return ((pg->pg_flags & PG_PMAP_MOD) != 0);
}

int
pmap_clear_modify(struct vm_page *pg)
{
	struct pte_desc *pted;
	uint64_t *pl3 = NULL;

	atomic_clearbits_int(&pg->pg_flags, PG_PMAP_MOD);

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list) {
		if (pmap_vp_lookup(pted->pted_pmap, pted->pted_va & ~PAGE_MASK, &pl3) == NULL)
			panic("failed to look up pte\n");
		// XXX What is this about?
		// *pl3  |= ATTR_AP(2);
		pted->pted_pte &= ~PROT_WRITE;

		// TLB Flush?
		// ttlb_flush(pted->pted_pmap, pted->pted_va & ~PAGE_MASK);
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	return 0;
}

int
pmap_clear_reference(struct vm_page *pg)
{
	struct pte_desc *pted;

	atomic_clearbits_int(&pg->pg_flags, PG_PMAP_REF);

	mtx_enter(&pg->mdpage.pv_mtx);
	LIST_FOREACH(pted, &(pg->mdpage.pv_list), pted_pv_list) {
		pted->pted_pte &= ~PROT_MASK;
		pmap_pte_insert(pted);
		// TLB Flush?
		// ttlb_flush(pted->pted_pmap, pted->pted_va & ~PAGE_MASK);
	}
	mtx_leave(&pg->mdpage.pv_mtx);

	return 0;
}

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vaddr_t dst_addr,
	vsize_t len, vaddr_t src_addr)
{
	// XXX Optional Function
}

void
pmap_unwire(pmap_t pm, vaddr_t va)
{
	struct pte_desc *pted;

	pted = pmap_vp_lookup(pm, va, NULL);
	if ((pted != NULL) && (pted->pted_va & PTED_VA_WIRED_M)) {
		pm->pm_stats.wired_count--;
		pted->pted_va &= ~PTED_VA_WIRED_M;
	}
}

void
pmap_remove_holes(struct vmspace *vm)
{
	/* NOOP */
}

void
pmap_virtual_space(vaddr_t *start, vaddr_t *end)
{
	// XXX Optional Function
}
