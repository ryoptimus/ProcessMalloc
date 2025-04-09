/* General headers */
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>
#include <asm/pgalloc.h>

#include "memalloc-common.h"

/*
    page table hierarchy in Linux:
        +-----+
        | PGD |
        +-----+
           |
           |   +-----+
           +-->| P4D |
               +-----+
                  |
                  |   +-----+
                  +-->| PUD |
                      +-----+
                         |
                         |   +-----+
                         +-->| PMD |
                             +-----+
                                |
                                |   +-----+
                                +-->| PTE |
                                    +-----+
    pgd, pgd_t, pgdval_t    =   page global directory (PGD)
                                Linux kernel main page table handling PGD for kernel memory is still in swapper_pg_dir
                                each userspace process in the system has its own memory context
                                i.e., each process has its own pgd, found in struct mm_struct which in turn is ref'd to in each struct task_struct
                                so tasks have memory context in the form of a struct mm_struct
                                mm_struct has a struct pgt_t* pgd pointer to the corresponding page global directory
    p4d, p4d_t, p4dval_t    =   page level 4 directory
                                intro'd to handle 5-level pages after the pud was intro'd
                                only used on systems which actually have 5 levels of page tables
    pud, pud_t, pudval_t    =   page upper directory
                                intro'd after other levels to handle 4-level page tables
                                if unused, it is folded
    pmd, pmd_t, pmdval_t    =   page middle directory
                                hierarchy right above the pte, with PTRS_PER_PMD references to the pte:s
    pte, pte_t, pteval_t    =   page table entry
                                pte is an array of PTRS_PER_PTE elements of the pteval_t type
                                    each maps a single page of virtual memory to a single page of physical memory
                                pteval_t depends on architecture
                                    typical example: pteval_t is 32- or 64-bit val with upper bits being a pfn (page frame number) and lower bits being some architecture-specific bits (e.g., memory protection)
                                confusing-ish name; pte is the lowermost page table

    each level of the page table hierarchy is an ARRAY OF POINTERS
        pgd contains PTRS_PER_PGD pointers to the next level below
        p4d contains PTRS_PER_P4D pointers to pud items
        and so on

    typedef struct { pd4val_t p4d; } p4d_t;
    typedef struct { pudval_t pud; } pud_t;
*/

/*
    most memory allocation APIs use GFP ("get free pages") flags to express how memory should be allocated
    untrusted alllocations triggered from userspace should be a subject of kmem accounting and must have __GFP_ACCOUNT bit set
        use GFP_KERNEL_ACCOUNT shortcut for GPF_KERNEL allocs that should be accounted
        puts objects in an accounted cache named kmalloc-cg-x
*/

/* Project Functions */
pud_t* memalloc_pud_alloc(p4d_t* p4d, unsigned long vaddr) {
    gfp_t gfp = GFP_KERNEL_ACCOUNT;
    /* get_zeroed_page(): returns a pointer to a new page and fills the page with zeroes */
    pud_t* pud = (pud_t*) get_zeroed_page(gfp);
    if (!pud) {
        printk("Error: Failed to allocate PUD.\n");
        return NULL;
    }

#if defined(CONFIG_X86_64)
    WRITE_ONCE(*p4d, __p4d(_PAGE_TABLE | __pa(pud)));
#else 
    p4dval_t p4dval = P4D_TYPE_TABLE | P4D_TABLE_PXN;
    phys_addr_t pud_pa = __pa(pud);
    p4d_t p4dt = __p4d(__phys_to_p4d_val(pud_pa) | p4dval);
    /* write final value into the P4D entry */
	WRITE_ONCE(*p4d, p4dt);
	dsb(ishst);
	isb();
#endif

    return pud;
}

pmd_t* memalloc_pmd_alloc(pud_t* pud, unsigned long vaddr) {
    gfp_t gfp = GFP_KERNEL_ACCOUNT;
    /* get_zeroed_page(): returns a pointer to a new page and fills the page with zeroes */
    pmd_t* pmd = (pmd_t*) get_zeroed_page(gfp);
    if (!pmd) {
        printk("Error: Failed to allocate PMD.\n");
        return NULL;
    }

#if defined(CONFIG_X86_64)
    set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)));
#else 
    pudval_t pudval = PUD_TYPE_TABLE | PUD_TABLE_PXN;
    /* convert page to a physical address */
    phys_addr_t pmd_pa = __pa(pmd);
    pud_t pudt = __pud(__phys_to_pud_val(pmd_pa) | pudval);
    /* write final value into the PMD entry */
	WRITE_ONCE(*pud, pudt);
	if (pud_valid(pudt)) {
        /* flush CPU pipelines and ensure memory visibility; ensure CPU state is synced with page table changes */
		dsb(ishst);
		isb();
	}
#endif

    return pmd;
}

/* 
    memalloc_pte_alloc() allocates + installs a PTE page beneath a PMD entry as part of setting up the 
    full page table hierarchy that maps a virtual address (vaddr) to physical memory
*/
void memalloc_pte_alloc(pmd_t* pmd, unsigned long vaddr) {
    /* GFP_PGTABLE_USER flag used for allocating memory for user page tables */
    gfp_t gfp = GFP_PGTABLE_USER;
    /* ptdesc is a page table descriptor, a kernel structure that holds metadata about a page table page */
    /* pagetable_alloc() returns a pointer to this ptdesc structure */
    struct ptdesc* pte = (struct ptdesc*) pagetable_alloc(gfp, 0);
    if (!pte) {
        printk("Error: Failed to allocate PTE.\n");
        return;
    }
    /* ptdesc_page() converts the pointer to a pgtable_t pointing to a real memory page */
    pgtable_t pt = ptdesc_page(pte);
#if defined(CONFIG_X86_64)
    /* current->mm is the current process's memory map */
    /* pmd_populate() sets the PMD entry to point to the physical address of the new page */
    pmd_populate(current->mm, pmd, pt);
/* for ARM and others */
#else 
    pmdval_t pmdval = PMD_TYPE_TABLE | PMD_TABLE_PXN;
    phys_addr_t pt_pa = page_to_phys(pt);
    pmd_t pmdt = __pmd(__phys_to_pmd_val(pt_pa) | pmdval);
	WRITE_ONCE(*pmd, pmdt);
	if (pmd_valid(pmdt)) {
		dsb(ishst);
		isb();
	}
#endif
}
