#include "asm-generic/fixmap.h"
#include "asm/pgtable.h"
#include "stddef.h"
#include <ee/pgtable.h>
#include <ee/pfn.h>
#include <asm-generic/bug.h>
#include <stdint.h>
#include <string.h>
#include <type.h>
#include <linkage.h>
#include <mm/memblock.h>
#include <asm/memory.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/csr.h>

extern char __kimage_start[];
extern char __kimage_end[];


/* mm page_va - mm phys_pa */
unsigned long va_pa_offset;

/* kernel_image_start_va - load_pa */
unsigned long kimage_voffset;

static int mmu_enabled;

/* early pgd */
pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
pgd_t early_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
static pmd_t early_pmd[PTRS_PER_PMD] __aligned(PAGE_SIZE);

static pmd_t fixmap_pmd[PTRS_PER_PMD] __page_aligned_bss;
static pte_t fixmap_pte[PTRS_PER_PTE] __page_aligned_bss;

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *ptep;

	BUG_ON(idx <= FIX_HOLE || idx >= __end_of_fixed_addresses);

	ptep = &fixmap_pte[pte_index(addr)];

	if (pgprot_val(prot)) {
		set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
	} else {
		set_pte(ptep, __pte(0));
		local_flush_tlb_page(addr);
	}
}

static pte_t *get_pte_virt(phys_addr_t pa)
{
	if(mmu_enabled)
	{
		clear_fixmap(FIX_PTE);
		return (pte_t *)set_fixmap_offset(FIX_PTE, pa);
	}
	/* Before MMU is enabled */
	return (pte_t *)(0);
}

static phys_addr_t alloc_pte(uintptr_t va)
{
	if(mmu_enabled)
	{
		return memblock_phys_alloc_align(PAGE_SIZE, PAGE_SIZE);
	}

	/* BUG: only used when mmu_enable */
	return 0;
}

static pmd_t *get_pmd_virt(phys_addr_t pa)
{
	if(mmu_enabled)
	{
		clear_fixmap(FIX_PMD);
		return (pmd_t *)set_fixmap_offset(FIX_PMD, pa);
	}

	/* Before MMU is enabled */
	return (pmd_t *)((uintptr_t)pa);
}

static phys_addr_t alloc_pmd(uintptr_t va)
{
	if(mmu_enabled)
	{
		return memblock_phys_alloc_align(PAGE_SIZE, PAGE_SIZE);
	}

	return (phys_addr_t)early_pmd;
}

static void create_pte_mapping(pte_t *ptep,
				      uintptr_t va, phys_addr_t pa,
				      phys_addr_t sz, pgprot_t prot)
{
	uintptr_t pte_idx = pte_index(va);

	BUG_ON(sz != PAGE_SIZE);

	if (pte_none(ptep[pte_idx]))
		ptep[pte_idx] = pfn_pte(PFN_DOWN(pa), prot);
}

static void create_pmd_mapping(pmd_t *pmdp,
				      uintptr_t va, phys_addr_t pa,
				      phys_addr_t sz, pgprot_t prot)
{
	pte_t *ptep;
	phys_addr_t pte_phys;
	uintptr_t pmd_idx = pmd_index(va);

	if (sz == PMD_SIZE) {
		if (pmd_none(pmdp[pmd_idx]))
			pmdp[pmd_idx] = pfn_pmd(PFN_DOWN(pa), prot);
		return;
	}

	if (pmd_none(pmdp[pmd_idx])) {
		pte_phys = alloc_pte(va);
		pmdp[pmd_idx] = pfn_pmd(PFN_DOWN(pte_phys), PAGE_TABLE);
		ptep = get_pte_virt(pte_phys);
		memset(ptep, 0, PAGE_SIZE);
	} else {
		pte_phys = PFN_PHYS(_pmd_pfn(pmdp[pmd_idx]));
		ptep = get_pte_virt(pte_phys);
	}

	create_pte_mapping(ptep, va, pa, sz, prot);
}

void create_pgd_mapping(pgd_t *pgdp,
				uintptr_t va, phys_addr_t pa,
				phys_addr_t sz, pgprot_t prot)
{
	pmd_t *pmdp;
	phys_addr_t next_phys;
	uintptr_t pgd_idx = pgd_index(va);

	// 如果创建映射大小与一级页表相同 只创建一级映射就可以
	// 创建完成后返回
	if (sz == PGDIR_SIZE) {
		if (pgd_val(pgdp[pgd_idx]) == 0)
			pgdp[pgd_idx] = pfn_pgd(PFN_DOWN(pa), prot);
		return;
	}

	if (pgd_val(pgdp[pgd_idx]) == 0) {
		next_phys = alloc_pmd(va);
		pgdp[pgd_idx] = pfn_pgd(PFN_DOWN(next_phys), PAGE_TABLE);
		pmdp = get_pmd_virt(next_phys);
		memset(pmdp, 0, PAGE_SIZE);
	} else {
		next_phys = PFN_PHYS(_pgd_pfn(pgdp[pgd_idx]));
		pmdp = get_pmd_virt(next_phys);
	}

	create_pmd_mapping(pmdp, va, pa, sz, prot);
}

static uintptr_t best_map_size(phys_addr_t base, phys_addr_t size)
{
	/* Upgrade to PMD_SIZE mappings whenever possible */
	if ((base & (PMD_SIZE - 1)) || (size & (PMD_SIZE - 1)))
		return PAGE_SIZE;

	return PMD_SIZE;
}

void setup_vm(void)
{
	/* early map fixmap */
	create_pgd_mapping(early_pg_dir,
			FIXADDR_START, (uintptr_t)fixmap_pmd,
			PGDIR_SIZE, PAGE_TABLE);

	create_pmd_mapping(fixmap_pmd,
			FIXADDR_START, (uintptr_t)fixmap_pte,
			PMD_SIZE, PAGE_TABLE);

	/**
	 * create kernel image map in early_pd_dir
	 *
	 */
	phys_addr_t kernel_image_start = (phys_addr_t)&__kimage_start;
	phys_addr_t kernel_image_end = (phys_addr_t)&__kimage_end;
	phys_addr_t kernel_image_size = kernel_image_end - kernel_image_start;

	uintptr_t pa = kernel_image_start;
	uintptr_t va = KIMAGE_VADDR;
	uintptr_t va_end = va + kernel_image_size;

	va_pa_offset = PAGE_OFFSET - pa;
	kimage_voffset = KIMAGE_VADDR - pa;

	BUG_ON((pa % PMD_SIZE) != 0);

	for(; va < va_end; va += PMD_SIZE)
	{
		/* pmd = early_pmd */
		/* size = 1g is enough */
		create_pgd_mapping(early_pg_dir, va, pa + (va - KIMAGE_VADDR), PMD_SIZE, PAGE_KERNEL_EXEC);
	}
}

/**
 * 1. 创建正式内核页表 swapper_pg_dir
 * 2. 映射 FIXMAP
 * 3. 映射 mem
 * 4. 映射 kernel
 * 5. 页表切换
 *
 */
void setup_vm_final(void)
{
	mmu_enabled = 1;

	/* FIXMAP */
	create_pgd_mapping(swapper_pg_dir,
			FIXADDR_START, __pa_symbol(fixmap_pmd),
			PGDIR_SIZE, PAGE_TABLE);

	/* KERNEL */
	uintptr_t va, pa;
	uintptr_t kimage_start, kimage_end;

	kimage_start = (uintptr_t)&__kimage_start;
	kimage_end = (uintptr_t)&__kimage_end;

	va = KIMAGE_VADDR;
	pa = __pa_symbol(kimage_start);

	for(; va < kimage_end; va += PMD_SIZE, pa += PMD_SIZE)
	{
		create_pgd_mapping(swapper_pg_dir, va, pa, PMD_SIZE, PAGE_KERNEL_EXEC);
	}

	/* MEM */
	/* from memblock */
	u64 i, size;
	phys_addr_t start, end;
	va = PAGE_OFFSET;

	for_each_mem_range(i, &start, &end)
	{
		pa = start;
		size = best_map_size(start, end - start);
		for(pa = start; pa < end; pa += size)
		{
			va = (uintptr_t)phys_to_virt(pa);
			create_pgd_mapping(swapper_pg_dir, va, pa, size, PAGE_KERNEL);
		}
	}

	/* Clear fixmap PTE and PMD mappings */
	clear_fixmap(FIX_PTE);
	clear_fixmap(FIX_PMD);

	/* Move to swapper page table */
	csr_write(satp, PFN_DOWN(__pa_symbol(swapper_pg_dir)) | (0x08UL << 60));
	local_flush_tlb_all();

#if 0
	asm("jal .");
	asm("nop");
	uint64_t fix_va = set_fixmap_offset(FIX_EARLYCON_MEM_BASE, 0x10000000);
#endif
}
