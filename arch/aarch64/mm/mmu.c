#include <pgtable.h>
#include <asm/pgtable_type.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <compiler_attribute.h>
#include <ee/pgtable.h>

uint64_t kimage_voffset;

static pte_t bm_pte[PTRS_PER_PTE] __aligned(PAGE_SIZE);
static pmd_t bm_pmd[PTRS_PER_PMD] __aligned(PAGE_SIZE);
static pud_t bm_pud[PTRS_PER_PUD] __aligned(PAGE_SIZE);

static inline pte_t *fixmap_pte(unsigned long addr)
{
     return &bm_pte[pte_index(addr)];
}

void __set_fixmap(enum fixed_addresses idx,
                  phys_addr_t phys, pgprot_t flags)
{
    unsigned long addr = fix_to_virt(idx);
    pte_t *ptep;

    ptep = fixmap_pte(addr);
    *ptep = pfn_pte(phys >> PAGE_SHIFT, flags);
}

void early_fixmap_init(pgd_t *pg_dir)
{
    pgd_t *pgdp;
    pgd_t pgd;
    pud_t *pudp;
    pmd_t *pmdp;
    unsigned long addr = FIXADDR_START;

    pgdp = pgd_offset_pgd(pg_dir, addr);
    pgd = *pgdp;

    // pgd空时创建页表
    if (pgd_none(pgd))
    {
        __pgd_populate(pgdp, __pa_symbol(bm_pud), PGD_TYPE_TABLE);
        pudp = pud_offset_pud(bm_pud, addr);
    }

    if (pud_none(*pudp))
    {
        __pud_populate(pudp, __pa_symbol(bm_pmd), PUD_TYPE_TABLE);
    }

    pmdp = pmd_offset_pmd(bm_pmd, addr);
    __pmd_populate(pmdp, __pa_symbol(bm_pte), PMD_TYPE_TABLE);

    //bm_pte 4K * 512 map to fixaddr
}

static void alloc_init_cont_pmd(pgd_t *pudp, unsigned long addr, unsigned long end,
                           phys_addr_t phys, pgprot_t prot,
                           phys_addr_t (*pgtable_alloc)(int),
                           int flags)
{

}


static void alloc_init_pud(pgd_t *pgdp, unsigned long addr, unsigned long end,
                           phys_addr_t phys, pgprot_t prot,
                           phys_addr_t (*pgtable_alloc)(int),
                           int flags)
{
    unsigned long next;
    pud_t *pudp;
    pgd_t pgd = *pgdp;

    if (pgd_none(pgd))
    {
        pgdval_t pgdval = PGD_TYPE_TABLE | PGD_TABLE_UXN;
        phys_addr_t pud_phys;
        pud_phys = pgtable_alloc(PUD_SHIFT);
        __pgd_populate(pgdp, pud_phys, pgdval);
    }

    // use fix map do temp map for pud page alloc from memblock
    pudp = pud_offset_phys(pgdp, addr);

    do
    {
        next = pud_addr_end(addr, end);
        alloc_init_cont_pmd(pudp, addr, end, phys, prot, pgtable_alloc, flags);
        phys += next - addr;
    } while (pudp++, addr = next, addr != end);


}

static void __create_pgd_mapping(pgd_t *pgdir, phys_addr_t phys,
                                 unsigned long virt, phys_addr_t size,
                                 pgprot_t prot,
                                 phys_addr_t (*pgtable_alloc)(int),
                                 int flags)
{
    unsigned long addr, end, next;
    (void)addr;
    (void)end;

    pgd_t *pgdp = pgd_offset_pgd(pgdir, virt);

    //TODO: check phys and virt has same offset in a page
    // make sure (phys ^ virt) & ~PAGE_MASK) == 0

    phys = phys & PAGE_MASK;
    addr = virt & PAGE_MASK;
    end = phys + size;

    do
    {
        next = pgd_addr_end(addr, end);
        alloc_init_pud(pgdp, addr, next, phys, prot, pgtable_alloc, flags);
        phys += next - addr;
    } while (pgdp++, addr = next, addr != end);
}