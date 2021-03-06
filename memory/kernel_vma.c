/*
 * Copyright (c) 2018 Jie Zheng
 */
#include <memory/include/kernel_vma.h>
#include <lib/include/string.h>
#include <kernel/include/printk.h>
#include <memory/include/physical_memory.h>
#include <memory/include/paging.h>
static struct kernel_vma _kernel_vma[KERNEL_VMA_ARRAY_LENGTH];

uint32_t
search_free_kernel_virtual_address(uint32_t length)
{
    int idx = 0;
    int idx_tmp = 0;
    struct kernel_vma * vma = NULL;
    struct kernel_vma * vma_tmp = NULL;
    struct kernel_vma * vma_nearest = NULL;
    uint64_t vaddr_free_start = 0;
    uint64_t vaddr_free_end = 0;
    // Length must be page rounded
    length = length & PAGE_MASK ? (length & ~PAGE_MASK) + PAGE_SIZE : length;
    for (idx = 0; idx < KERNEL_VMA_ARRAY_LENGTH; idx++) {
        vma = &_kernel_vma[idx];
        if (!vma->present)
            continue;
        vaddr_free_start = (uint64_t)(vma->virt_addr + vma->length);
        {
            // Try to find the vma's boundary by search the next vma
            // but search the nearest vma first
            vma_nearest = NULL;
            for (idx_tmp = 0; idx_tmp < KERNEL_VMA_ARRAY_LENGTH; idx_tmp++) {
                if (idx_tmp == idx)
                    continue;
                vma_tmp = &_kernel_vma[idx_tmp];
                if (!vma_tmp->present)
                    continue;
                if (vma_tmp->virt_addr >= vaddr_free_start) {
                    if (!vma_nearest ||
                        vma_tmp->virt_addr <= vma_nearest->virt_addr)
                        vma_nearest = vma_tmp;
                }
            }
            vaddr_free_end = vma_nearest ?
                vma_nearest->virt_addr :
                USERSPACE_BOTTOM;
        }
        ASSERT(vaddr_free_end);
        if ((vaddr_free_end - vaddr_free_start) > length)
            return vaddr_free_start;
    }
    return 0;
}

struct kernel_vma *
search_kernel_vma(uint32_t virt_addr)
{
    struct kernel_vma * vma = NULL;
    struct kernel_vma * _vma = NULL;
    int idx;
    for (idx = 0; idx < KERNEL_VMA_ARRAY_LENGTH; idx++) {
        _vma = &_kernel_vma[idx];
        if (!_vma->present)
            continue;
        if ((virt_addr >= _vma->virt_addr) &&
            (virt_addr < (_vma->virt_addr + _vma->length))) {
            vma = _vma;
            break;
        }
    }
    return vma;
}

int
register_kernel_vma(struct kernel_vma * vma)
{
    int ret = OK;
    int target_index = -1;
    int idx = 0;
    struct kernel_vma * _vma;
    for (idx = 0; idx < KERNEL_VMA_ARRAY_LENGTH; idx++) {
        _vma = &_kernel_vma[idx];
        if (!_kernel_vma[idx].present) {
            target_index = (target_index == -1) ? idx : target_index; 
            continue;
        }
        /*
         * check whether the VMA to be added in the global overlaps with
         * current VMA entries
         */ 
        if ((vma->virt_addr >= _vma->virt_addr) &&
            (vma->virt_addr < (_vma->virt_addr + _vma->length)))
            return -ERR_INVALID_ARG;
    }

    if (target_index == -1)
        return -ERR_OUT_OF_RESOURCE;
    _vma = &_kernel_vma[target_index];
    strcpy_safe(_vma->name, vma->name, sizeof(_vma->name));
    _vma->present = 1;
    _vma->exact = vma->exact;
    _vma->write_permission = vma->write_permission;
    _vma->page_writethrough = vma->page_writethrough;
    _vma->page_cachedisable = vma->page_cachedisable;
    _vma->virt_addr = vma->virt_addr;
    _vma->phy_addr = vma->phy_addr;
    _vma->length = vma->length;

    if (_vma->premap) {
        uint32_t linear_address = _vma->virt_addr;
        uint32_t phy_address = 0;
        for (; linear_address < (_vma->virt_addr + _vma->length);
            linear_address += PAGE_SIZE) {
            phy_address = _vma->exact ? 
                linear_address - _vma->virt_addr + _vma->phy_addr :
                get_page();
            ASSERT(phy_address);
            kernel_map_page(linear_address,
                phy_address,
                _vma->write_permission,
                _vma->page_writethrough,
                _vma->page_cachedisable);
        }
    };
    return ret;
}
/*
 * This is to find an unoccupied virtual address, premap this.
 */
uint32_t
kernel_map_vma(
    uint8_t * vma_name,
    uint32_t exact,
    uint32_t premap,
    uint32_t phy_addr,
    uint32_t length,
    uint32_t write_permission,
    uint32_t page_writethrough,
    uint32_t page_cachedisable)
{
    uint32_t virt_addr = search_free_kernel_virtual_address(length);
    struct kernel_vma vma;
    // Find no virtual address block
    if (!virt_addr)
        return virt_addr;
    memset(&vma, 0x0, sizeof(struct kernel_vma));
    strcpy_safe(vma.name, vma_name, sizeof(vma.name));
    vma.exact = 1;
    vma.write_permission = write_permission;
    vma.page_writethrough = page_writethrough;
    vma.page_cachedisable = page_cachedisable;
    vma.premap = premap;
    vma.virt_addr = virt_addr;
    vma.phy_addr = phy_addr;
    vma.length = length;
    ASSERT(!register_kernel_vma(&vma));
    return virt_addr;
}
void dump_kernel_vma(void)
{
    int idx = 0;
    struct kernel_vma * _vma;
    LOG_INFO("Dump kernel vma:\n");
    for(idx = 0; idx < KERNEL_VMA_ARRAY_LENGTH; idx++) {
        _vma = &_kernel_vma[idx];
        if (!_vma->present)
            continue;
    LOG_INFO("   vma entry %d: name:%s virt:0x%x phy:0x%x len:0x%x "
        "permission:0x%x %s\n",
        idx,
        _vma->name,
        _vma->virt_addr,
        _vma->phy_addr,
        _vma->length,
        _vma->write_permission |
            _vma->page_writethrough << 1|
            _vma->page_cachedisable << 2,
        _vma->exact ? "(*use exact mapping)" : "");
    }
}

void
kernel_vma_init(void)
{
    struct kernel_vma _vma;
    uint32_t sys_mem_start = get_system_memory_start();
    memset(_kernel_vma, 0x0, sizeof(_kernel_vma));
    LOG_INFO("Set KERNEL_VMA_ARRAY_LENGTH = %d\n", KERNEL_VMA_ARRAY_LENGTH);
    /*
     * Setup initial layout linear VM area
     */
    strcpy_safe(_vma.name, (const uint8_t*)"Low1MB", sizeof(_vma.name));
    _vma.exact = 1;
    _vma.write_permission = PAGE_PERMISSION_READ_WRITE;
    _vma.page_writethrough = PAGE_WRITEBACK;
    _vma.page_cachedisable = PAGE_CACHE_ENABLED;
    _vma.virt_addr = 0;
    _vma.phy_addr = 0;
    _vma.length = 0x100000;
    _vma.premap = 0;
    ASSERT(register_kernel_vma(&_vma) == OK);

    strcpy_safe(_vma.name, (const uint8_t*)"KernelImage", sizeof(_vma.name));
    _vma.exact = 1;
    _vma.write_permission = PAGE_PERMISSION_READ_WRITE;
    _vma.page_writethrough = PAGE_WRITEBACK;
    _vma.page_cachedisable = PAGE_CACHE_ENABLED;
    _vma.virt_addr = 0x100000;
    _vma.phy_addr = 0x100000;
    _vma.length = sys_mem_start - 0x100000;
    _vma.premap = 0;
    ASSERT(register_kernel_vma(&_vma) == OK);

    strcpy_safe(_vma.name, (const uint8_t*)"PageInventory", sizeof(_vma.name));
    _vma.exact = 1;
    _vma.write_permission = PAGE_PERMISSION_READ_WRITE;
    _vma.page_writethrough = PAGE_WRITEBACK;
    _vma.page_cachedisable = PAGE_CACHE_ENABLED;
    _vma.virt_addr = PAGE_SPACE_BOTTOM;
    _vma.phy_addr = PAGE_SPACE_BOTTOM;
    _vma.length = PAGE_SPACE_TOP - PAGE_SPACE_BOTTOM;
    _vma.premap = 0;
    ASSERT(register_kernel_vma(&_vma) == OK);

    strcpy_safe(_vma.name, (const uint8_t*)"KernelHeap", sizeof(_vma.name));
    _vma.exact = 0;
    _vma.write_permission = PAGE_PERMISSION_READ_WRITE;
    _vma.page_writethrough = PAGE_WRITEBACK;
    _vma.page_cachedisable = PAGE_CACHE_ENABLED;
    _vma.virt_addr = KERNEL_HEAP_BOTTOM;
    _vma.phy_addr = 0;
    _vma.length = KERNEL_HEAP_TOP - KERNEL_HEAP_BOTTOM;
    _vma.premap = 0;
    ASSERT(register_kernel_vma(&_vma) == OK);

#if 0
    strcpy(_vma.name, (const uint8_t*)"KernelStack");
    _vma.exact = 0;
    _vma.write_permission = PAGE_PERMISSION_READ_WRITE;
    _vma.page_writethrough = PAGE_WRITEBACK;
    _vma.page_cachedisable = PAGE_CACHE_ENABLED;
    _vma.virt_addr = KERNEL_STACK_BOTTOM;
    _vma.phy_addr = 0;
    _vma.length = KERNEL_STACK_TOP - KERNEL_STACK_BOTTOM;
    ASSERT(register_kernel_vma(&_vma) == OK);
#endif
    dump_kernel_vma();
}



