/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-04-11     liYony       the first version
 */

#include <cpu.h>
#include <mmu.h>
#include <board.h>
#include <mm_aspace.h>
#include <mm_page.h>
#include <drv_uart.h>
#include <gtimer.h>
#include <psci.h>

extern size_t MMUTable[];

#ifdef RT_USING_SMART
struct mem_desc platform_mem_desc[] = {
    {KERNEL_VADDR_START, KERNEL_VADDR_START + 0x7FF00000 - 1, (rt_size_t)ARCH_MAP_FAILED, NORMAL_MEM}
};
#else
struct mem_desc platform_mem_desc[] =
{
    {0x00200000, 0x7FF00000 - 1, 0x00200000, NORMAL_MEM},
    {GIC400_DISTRIBUTOR_PPTR, GIC400_DISTRIBUTOR_PPTR + GIC400_SIZE - 1, GIC400_DISTRIBUTOR_PPTR, DEVICE_MEM},
    {GIC400_CONTROLLER_PPTR, GIC400_CONTROLLER_PPTR + GIC400_SIZE - 1, GIC400_CONTROLLER_PPTR, DEVICE_MEM},
};
#endif

const rt_uint32_t platform_mem_desc_size = sizeof(platform_mem_desc) / sizeof(platform_mem_desc[0]);

void idle_wfi(void)
{
    asm volatile("wfi");
}

rt_uint64_t rt_cpu_mpidr_table[] =
{
    [0] = 0x80000000,
    [1] = 0x80000001,
    [2] = 0x80000002,
    [3] = 0x80000003,
};

void rt_hw_board_init(void)
{
#ifdef RT_USING_SMART
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)0xfffffffff0000000, 0x10000000, MMUTable, PV_OFFSET);
#else
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)0xffffd0000000, 0x10000000, MMUTable, 0);
#endif
    rt_region_t init_page_region;
    init_page_region.start = PAGE_START;
    init_page_region.end = PAGE_END;
    rt_page_init(init_page_region);

    rt_hw_mmu_setup(&rt_kernel_space, platform_mem_desc, platform_mem_desc_size);

#ifdef RT_USING_HEAP
    /* initialize system heap */
    rt_system_heap_init((void *)HEAP_BEGIN, (void *)HEAP_END);
#endif
    /* initialize hardware interrupt */
    rt_hw_interrupt_init();

    /* initialize uart */
    rt_hw_uart_init();

    /* initialize timer for os tick */
    rt_hw_gtimer_init();

    rt_thread_idle_sethook(idle_wfi);

    rt_psci_init("smc", PSCI_VERSION(0, 2), RT_NULL);
#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)
    /* set console device */
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif
    rt_kprintf("heap: [0x%08x - 0x%08x]\n", HEAP_BEGIN, HEAP_END);

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}

void reboot(void)
{
    psci_system_reboot();
}

MSH_CMD_EXPORT(reboot, reboot system);

#ifdef RT_USING_SMP
rt_weak void rt_hw_secondary_cpu_up(void)
{
    int cpu_id = rt_hw_cpu_id();

    extern void _secondary_cpu_entry(void);
    rt_uint64_t entry = (rt_uint64_t)rt_kmem_v2p(_secondary_cpu_entry);

    if (!entry)
    {
        rt_kprintf("Failed to translate '_secondary_cpu_entry' to physical address\r\n");
        RT_ASSERT(0);
    }
    int i = 0;
    
    /* Maybe we are no in the first cpu */
    for (i = 1; i < RT_CPUS_NR; i++)
    {
        rt_psci_cpu_on(i, (uint64_t)(_secondary_cpu_entry));
    }
}

void rt_hw_secondary_cpu_bsp_start(void)
{
    int cpu_id = rt_hw_cpu_id();
    rt_hw_spin_lock(&_cpus_lock);

    rt_hw_mmu_ktbl_set((unsigned long)MMUTable);

    rt_hw_vector_init();

    arm_gic_cpu_init(0, 0);

    rt_hw_gtimer_init();

    rt_kprintf("\r\ncpu %d boot success\r\n", rt_hw_cpu_id());

    rt_system_scheduler_start();
}

void rt_hw_secondary_cpu_idle_exec(void)
{
    asm volatile ("wfe":::"memory", "cc");
}
#endif