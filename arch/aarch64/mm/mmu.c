#include <pgtable.h>
#include <asm/pgtable_type.h>
#include <compiler_attribute.h>

static pte_t bm_pte[PTRS_PER_PTE] __section(".bss") __aligned(PAGE_SIZE);
static pmd_t bm_pmd[PTRS_PER_PMD] __section(".bss") __aligned(PAGE_SIZE);
static pud_t bm_pud[PTRS_PER_PUD] __section(".bss") __aligned(PAGE_SIZE);

void early_fixmap_init(void)
{

}


static void __create_pgd_mapping(pgd_t *pgdir, phys_addr_t phys,
                            unsigned long virt, phys_addr_t size,
                            pgprot_t port,
                            phys_addr_t (*pgtable_alloc)(int),
                            int flags
                            )
{
    unsigned long addr, end, next;
    (void)addr;
    (void)end;

    pgd_t *pgdp = pgd_offset_pgd(pgdir, virt);

    //TODO: check phys and virt has same offset in a page
    // make sure (phys ^ virt) & ~PAGE_MASK) == 0

    phys &= PAGE_MASK;
    addr = virt & PAGE_MASK;
    end = phys + size;
}