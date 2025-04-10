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

/* File IO-related headers */
#include <linux/fs.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

/* Sleep and timer headers */
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/pci.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adil Ahmad");
MODULE_DESCRIPTION("Page Walk Example");
MODULE_VERSION("0.01");

/* Init and Exit functions */
static int __init sample_init(void) {
    printk("[*] Sample page walk for 0xdead\n");

    unsigned long vaddr = 0xdead;

    /* 
        Each process has a pointer (mm_struct->pgd) to its own page global directory (PGD) which is a physical page frame.
        The frame contains an array of type pgd_t which is defined in <asm/page.h>.
        Each active entry in the PGD table points to a page frame containing an array of page middle directory (PMD) entries
        of type pmd_t, which in turn points to page fames containing page table entries (PTE) of type pte_t, which finally points
        to page frames containing actual user data.

        Linear addresses are broken into parts to yield offsets within the three page table levels and an offset within the
        actual page. A number of macros are provided for breaking up the linear address into its component parts: SHIFT, SIZE,
        and MASK.
            SHIFT macros specify the length in bits that are mapped by each level of the page tables.
            MASK values can be AND'd with a linear address to mask out all the upper bits, frequently used to determine if a
            linear address is aligned to a given level within the page table.

        <------------------------------------------------------------->
        +--------------+---------------+----------------+--------------+
        | global (PGD) |  middle (PMD) |   table (PTE)  |    offset    |
        +--------------+---------------+----------------+--------------+
                            PAGE_MASK                       PAGE_SIZE
        <-----------------------------------------------><------------->
                    PMD_MASK                        PMD_SIZE
        <------------------------------><------------------------------>
           PGDIR_MASK                     PGDIR_SIZE
        <--------------><---------------------------------------------->
    */
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    /* Return pointer of the PGD. mm is the mm_struct of the process, address is the logical 
       address in the virtual memory space*/
    /*
        pgd_t *pgd_offset(struct mm_struct *mm, unsigned long addr);
        Returns the pointer to the PGD entry (pgd_t) for a given virtual address.
            pgd_t is basically a wrapper around an entry in the page global directory. It typically holds:
                -a physical address pointing to the next-level page table (P4D table)
                -some flags (present, writable, user-accessible)
        and mm_struct contains a pointer to the pgd base address (mm_struct->pgd). i think
    */
    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd)) {
        printk("Error: pgd should always be mapped (something is really wrong!).\n");
        goto out;
    }
    printk("PGD is allocated. \n");

    /* Return pointer to the P4D. pgd is the pointer of PGD, address is the logical address in 
       the virtual memory space.*/
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk("No P4D allocated; page must be unmapped.");
        goto out;
    }
    printk("P4D is allocated. \n");

    /* Return pointer to the PUD. p4d is the pointer of P4D, address is the logical address in 
       the virtual memory space.*/
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        printk("No PUD allocated; page must be unmapped.");
        goto out;
    }
    printk("PUD is allocated. \n");


    /* Return pointer to the PMD. pud is the pointer of PUD, address is the logical address in 
       the virtual memory space.*/
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        printk("No PMD allocated; page must be unmapped.");
        goto out;
    }
    printk("PMD is allocated. \n");

    /* Return pointer to the PTE. pmd is the pointer of PMD, address is the logical address in 
       the virtual memory space*/
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) {
        printk("No PTE allocated; page must be unmapped.");
        goto out;
    }
    printk("PTE is allocated. \n");

    if (pte_present(*pte)) {
    	printk("Page is mapped.");
        goto out;
    }
    printk("Page is not mapped. \n");    // Get a free page for the process
    
    gfp_t gfp = GFP_KERNEL_ACCOUNT;
    /* get_zeroed_page(): allocates a physical page frame, zeroes it out, returns a kernel virtual address mapping to that page */
    /* virt_addr is a pointer to the kernel-mapped virtual address of that physical page */
    void *virt_addr = (void*) get_zeroed_page(gfp);
    if (!virt_addr) {
        printk(KERN_ERR "Failed to allocate memory using vmalloc\n");
        return -ENOMEM;
    }

    // Get the physical address of the page (converts kernel virtual address back to the physical address)
    unsigned long paddr = __pa(virt_addr);

out:
    return 0;
}

static void __exit sample_exit(void) {
    printk("[*] Goodbye world!.\n");
}

module_init(sample_init);
module_exit(sample_exit);
