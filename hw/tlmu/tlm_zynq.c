/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2012 Xilinx. Inc
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@xilinx.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/config-file.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/block/flash.h"

#include <libfdt.h>
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#include "hw/arm/arm.h"

#include "tlm.h"
#include "hw/tlm_mem.h"

#define MACHINE_NAME "tlm-zynq"

#define MAX_CPUS 4

#define SMP_BOOT_ADDR 0xfffffff0
/* Meaningless, but keeps arm boot happy */
#define SMP_BOOTREG_ADDR 0xfffffffc

/* Entry point for secondary CPU */
static uint32_t zynq_smpboot[] = {
    0xe320f003, /* wfi */
    0xeafffffd, /* beq     <wfi> */
};

static void zynq_write_secondary_boot(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
    int n;

    for (n = 0; n < ARRAY_SIZE(zynq_smpboot); n++) {
        zynq_smpboot[n] = tswap32(zynq_smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", zynq_smpboot, sizeof(zynq_smpboot),
                       SMP_BOOT_ADDR);
}

static void zynq_ps7_usb_nuke_phy(void *fdt)
{
    char usb_node_path[DT_PATH_LENGTH];

    int ret = qemu_devtree_node_by_compatible(fdt, usb_node_path,
                                              "xlnx,ps7-usb-1.00.a");
    if (!ret) {
        qemu_fdt_setprop_string(fdt, usb_node_path, "phy_type", "none");
        qemu_fdt_setprop_string(fdt, usb_node_path, "dr_mode", "host");
    }
}

static char *zynq_ps7_qspi_flash_node_clone(void *fdt)
{
    char qspi_node_path[DT_PATH_LENGTH];
    char qspi_new_node_path[DT_PATH_LENGTH];
    char *qspi_clone_name = NULL;

    /* clear node paths */
    memset(qspi_node_path, 0, sizeof(qspi_node_path));
    memset(qspi_new_node_path, 0, sizeof(qspi_new_node_path));

    /* search for ps7 qspi node */
    int ret = qemu_devtree_node_by_compatible(fdt, qspi_node_path,
                                              "xlnx,zynq-qspi-1.0");
    if (ret == 0) {
        int qspi_is_dual = qemu_fdt_getprop_cell(fdt, qspi_node_path,
                                                 "is-dual", 0, false, NULL);
        /* Generate dummy name */
        snprintf(qspi_new_node_path, DT_PATH_LENGTH, "%s/ps7-qspi-dummy@0",
                 qspi_node_path);

        /* get the spi flash node to clone from (assume first child node) */
        int child_num = qemu_devtree_get_num_children(fdt, qspi_node_path, 1);
        char **child_flash = qemu_devtree_get_children(fdt, qspi_node_path, 1);
        if (child_num > 0) {
            char *compat_str = NULL;
            compat_str = qemu_fdt_getprop(fdt, child_flash[0],
                                          "compatible", NULL, false, NULL);
            /* Create the cloned node if the qspi controller is in dual spi mode
             * and the compatible string is avaliable */
            if (compat_str != NULL) {
                if (qspi_is_dual == 1) {
                    /* Clone first node, preserving only 'compatible' value */
                    qemu_fdt_add_subnode(fdt, qspi_new_node_path);
                    qemu_fdt_setprop_string(fdt, qspi_new_node_path,
                                             "compatible", compat_str);
                    qspi_clone_name = g_strdup(qspi_new_node_path);
                }
                g_free(compat_str);
            }
        }
        g_free(child_flash);
    }

    return qspi_clone_name;
}

static qemu_irq* zynq_get_irqs(FDTMachineInfo *fdti)
{
  int i;
  void *fdt = fdti->fdt;
  char intc_node_path[DT_PATH_LENGTH];
  static qemu_irq zynq_irq[16];
  FDTGenericIntcClass *intc_fdt_class;
  DeviceState *intc;
  Error *errp = NULL;
  uint32_t cells[3];

  /* search for GIC node */
  int ret = qemu_devtree_node_by_compatible(fdt, intc_node_path,
                                              "arm,cortex-a9-gic");
  if(ret) goto fail;

  intc = DEVICE(fdt_init_get_opaque(fdti, intc_node_path));
  intc_fdt_class = FDT_GENERIC_INTC_GET_CLASS(intc);
  if (!intc_fdt_class) goto fail;

  /* expose PL[0:15] interrupt lines */ 
  cells[0] = 0;
  cells[1] = 29;  /* interrupts 61:68 and 84:91 */ 
  cells[2] = 0;
  for(i = 0; i < 16; i++) {
    intc_fdt_class->get_irq(FDT_GENERIC_INTC(intc), &zynq_irq[i], 
                            cells, 3, 1, &errp);
    if(++cells[1] == 37) cells[1] = 52;
  }

  return zynq_irq;
fail:
  hw_error("Error: Unable to find interrupt controller\n");
  return NULL;
}


static struct arm_boot_info tlm_zynq_binfo = {};

static void tlm_zynq_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_kernel_base = 0, ram_kernel_size = 0;

    void *fdt;
    void *sw_fdt;
    int fdt_size, sw_fdt_size;
    const char *dtb_arg, *hw_dtb_arg;
    char node_path[DT_PATH_LENGTH];
    FDTMachineInfo *fdti;
    FDTMemoryInfo *meminfo;
    char *qspi_clone_spi_flash_node_name;
    qemu_irq* irqs; 

    /*char *qspi_clone_spi_flash_node_name; */
    int boot_cpu = qemu_opt_get_number(qemu_get_boot_opts(), "cpu", 0);
    CPUState *cs;

    dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "dtb");
    hw_dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "hw-dtb");
    if (!dtb_arg && !hw_dtb_arg) {
        goto no_dtb_arg;
    }

    /* Software dtb is always the -dtb arg */
    sw_fdt = load_device_tree(dtb_arg, &sw_fdt_size);
    if (!sw_fdt) {
        hw_error("Error: Unable to load Device Tree %s\n", dtb_arg);
        return;
    }
    /* If the user provided a -hw-dtb, use it as the hw description.  */
    if (hw_dtb_arg) {
        fdt = load_device_tree(hw_dtb_arg, &fdt_size);
        if (!fdt) {
            hw_error("Error: Unable to load Device Tree %s\n", hw_dtb_arg);
            return;
        }
    } else {
        fdt = sw_fdt;
    }

    /* FIXME: remove when QSPI DTB description get sorted */
#if 1
    qspi_clone_spi_flash_node_name = zynq_ps7_qspi_flash_node_clone(fdt);
#endif

    /* find memory node or add new one if needed */
    while (qemu_devtree_get_node_by_name(fdt, node_path, "memory")) {
        qemu_fdt_add_subnode(fdt, "/memory@0");
        qemu_fdt_setprop_cells(fdt, "/memory@0", "reg", 0, args->ram_size);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    meminfo = fdt_init_get_opaque(fdti, node_path);
    /* Assert that at least one region of memory exists */
    assert(meminfo && meminfo->nr_regions > 0);
    /* FIXME: this is a bit of a fragile policy - try and remove */
    ram_kernel_base = meminfo->last_base;
    ram_kernel_size = meminfo->last_size;
    irqs = zynq_get_irqs(fdti);
    fdt_init_destroy_fdti(fdti);

    tlm_zynq_binfo.fdt = sw_fdt;
    tlm_zynq_binfo.fdt_size = sw_fdt_size;
    tlm_zynq_binfo.ram_size = ram_kernel_size;
    tlm_zynq_binfo.kernel_filename = args->kernel_filename;
    tlm_zynq_binfo.kernel_cmdline = args->kernel_cmdline;
    tlm_zynq_binfo.initrd_filename = args->initrd_filename;
    tlm_zynq_binfo.nb_cpus = fdt_generic_num_cpus;
    tlm_zynq_binfo.write_secondary_boot = zynq_write_secondary_boot;
    tlm_zynq_binfo.smp_loader_start = SMP_BOOT_ADDR;
    tlm_zynq_binfo.smp_bootreg_addr = SMP_BOOTREG_ADDR;
    tlm_zynq_binfo.board_id = 0xd32;
    tlm_zynq_binfo.loader_start = ram_kernel_base;

    for (cs = first_cpu; cs && boot_cpu; cs = CPU_NEXT(cs)) {
        boot_cpu--;
    }
    if (!cs) {
        error_report("Specified boot CPU is out of range!");
        exit(1);
    }
    tlm_zynq_binfo.primary_cpu = ARM_CPU(cs);

#if 1
    if (qspi_clone_spi_flash_node_name != NULL) {
        /* Remove cloned DTB node */
        int offset = fdt_path_offset(fdt, qspi_clone_spi_flash_node_name);
        fdt_del_node(fdt, offset);
        g_free(qspi_clone_spi_flash_node_name);
    }
#endif

    /*
     * FIXME: Probably better implemented as a plnx-specific pre-boot dtb
     * modifier
     */
    zynq_ps7_usb_nuke_phy(fdt);


    if (!irqs || !cs->env_ptr) {
        error_report("Error initializing TLM CPU!");
        exit(1);
    }

    tlm_map(cs->env_ptr, 0x40000000ULL, 0x7FFFFFFFULL,
            tlm_sync_period_ns, irqs, 16, NULL);  

    tlm_register_rams();

    if (args->kernel_filename) {
        arm_load_kernel(&tlm_zynq_binfo);
    }
    return;

no_dtb_arg:
    hw_error("DTB must be specified for %s machine model\n", MACHINE_NAME);
    return;
}

static void tlm_zynq_init_plnx(QEMUMachineInitArgs *args)
{
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    SysBusDevice *busdev;

    /*FIXME: Describe OCM in DTB and delete this */
    /* ZYNQ OCM: */
    {
        MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
        memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 << 10);
        vmstate_register_ram_global(ocm_ram);
        memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);
    }

    /* FIXME: Descibe NAND in DTB and delete this */
    /* NAND: */
    dev = qdev_create(NULL, "arm.pl35x");
    /* FIXME: handle this somewhere central */
    object_property_add_child(container_get(qdev_get_machine(), "/unattached"),
                              "pl353", OBJECT(dev), NULL);
    qdev_prop_set_uint8(dev, "x", 3);
    {
        DriveInfo *dinfo = drive_get_next(IF_PFLASH);
        DeviceState *att_dev = nand_init(dinfo ? dinfo->bdrv : NULL,
                                         NAND_MFR_STMICRO, 0xaa);

        object_property_set_link(OBJECT(dev), OBJECT(att_dev), "dev1",
                                 &error_abort);
    }
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xe000e000);
    sysbus_mmio_map(busdev, 2, 0xe1000000);

    tlm_zynq_init(args);

    /* FIXME: Descibe SCU in DTB and delete this */
    /* ZYNQ SCU: */
    {
        DeviceState *dev = qdev_create(NULL, "a9-scu");
        SysBusDevice *busdev = SYS_BUS_DEVICE(dev);

        qdev_prop_set_uint32(dev, "num-cpu", fdt_generic_num_cpus);
        qdev_init_nofail(dev);
        sysbus_mmio_map(busdev, 0, 0xF8F00000);
    }

}

static QEMUMachine tlm_zynq_machine = {
    .name = MACHINE_NAME,
    .desc = "Zynq TLM-wrapped machine model",
    .init = tlm_zynq_init,
    .max_cpus = MAX_CPUS,
};

static QEMUMachine tlm_zynq_machine_plnx = {
    .name = MACHINE_NAME "-plnx",
    .desc = "Zynq TLM-wrapped machine model for PetaLinux",
    .init = tlm_zynq_init_plnx,
    .max_cpus = MAX_CPUS,
};

static void tlm_zynq_machine_init(void)
{
    qemu_register_machine(&tlm_zynq_machine);
    qemu_register_machine(&tlm_zynq_machine_plnx);
}

machine_init(tlm_zynq_machine_init);

