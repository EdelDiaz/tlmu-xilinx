#ifndef TLMU_TLM_H
#define TLMU_TLM_H
#include "tlmu-qemuif.h"
#ifdef __cplusplus
extern "C"{
#endif
extern void *tlm_opaque;
extern int (*tlm_bus_access_cb)(void *o, int64_t clk, int rw,
                          uint64_t addr, void *data, int len);
extern void (*tlm_bus_access_dbg_cb)(void *o, int64_t clk,
                                int rw, uint64_t addr,
                                void *data, int len);
extern void (*tlm_get_dmi_ptr_cb)(void *o, uint64_t addr,
                                  struct tlmu_dmi *dmi);

/* From SystemC into QEMU.  */
extern int tlm_bus_access(int rw, uint64_t addr, void *data, int len);
extern void tlm_bus_access_dbg(int rw, uint64_t addr, void *data, int len);
extern int tlm_get_dmi_ptr(struct tlmu_dmi *dmi);
extern void (*tlm_sync)(void *o, uint64_t time_ns);

/* Used to map address areas as RAM. Needed by QEMU to allow code execution
   on these areas.  */
void tlm_map_ram(const char *name, uint64_t addr, uint64_t size, int rw, int);
void tlm_register_rams(void);

extern uint64_t tlm_sync_period_ns;

extern void tlm_notify_event(enum tlmu_event ev, void *d);

/* Non-zero means running.  */
extern int tlm_boot_state;

extern uint64_t tlm_image_load_base;
extern uint64_t tlm_image_load_size;

#ifdef __cplusplus
}
#endif
#endif
