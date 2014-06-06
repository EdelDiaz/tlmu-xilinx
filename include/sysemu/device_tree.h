/*
 * Header with function prototypes to help device tree manipulation using
 * libfdt. It also provides functions to read entries from device tree proc
 * interface.
 *
 * Copyright 2008 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 *          Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __DEVICE_TREE_H__
#define __DEVICE_TREE_H__

#include "qemu-common.h"
#include "qapi/qmp/qerror.h"

void *create_device_tree(int *sizep);
void *load_device_tree(const char *filename_path, int *sizep);

/* property setters */

int qemu_fdt_setprop(void *fdt, const char *node_path,
                     const char *property, const void *val, int size);
int qemu_fdt_setprop_cell(void *fdt, const char *node_path,
                          const char *property, uint32_t val);
int qemu_fdt_setprop_u64(void *fdt, const char *node_path,
                         const char *property, uint64_t val);
int qemu_fdt_setprop_string(void *fdt, const char *node_path,
                            const char *property, const char *string);
int qemu_fdt_setprop_phandle(void *fdt, const char *node_path,
                             const char *property,
                             const char *target_node_path);
void *qemu_fdt_getprop(void *fdt, const char *node_path,
                             const char *property, int *lenp,
                             bool inherit, Error **errp);
uint32_t qemu_fdt_getprop_cell(void *fdt, const char *node_path,
                               const char *property, int offset,
                               bool inherit, Error **errp);
uint64_t qemu_fdt_getprop_sized_cell(void *fdt, const char *node_path,
                                     const char *property, int offset,
                                     int size, Error **errp);
char *qemu_fdt_getprop_string(void *fdt, const char*node_path,
                              const char *property, int cell,
                              bool inherit, Error **errp);
uint32_t qemu_fdt_get_phandle(void *fdt, const char *path);
uint32_t qemu_fdt_alloc_phandle(void *fdt);
int qemu_fdt_nop_node(void *fdt, const char *node_path);
int qemu_fdt_add_subnode(void *fdt, const char *name);

#define qemu_fdt_setprop_cells(fdt, node_path, property, ...)                 \
    do {                                                                      \
        uint32_t qdt_tmp[] = { __VA_ARGS__ };                                 \
        int i;                                                                \
                                                                              \
        for (i = 0; i < ARRAY_SIZE(qdt_tmp); i++) {                           \
            qdt_tmp[i] = cpu_to_be32(qdt_tmp[i]);                             \
        }                                                                     \
        qemu_fdt_setprop(fdt, node_path, property, qdt_tmp,                   \
                         sizeof(qdt_tmp));                                    \
    } while (0)

void qemu_fdt_dumpdtb(void *fdt, int size);

/**
 * qemu_fdt_setprop_sized_cells_from_array:
 * @fdt: device tree blob
 * @node_path: node to set property on
 * @property: property to set
 * @numvalues: number of values
 * @values: array of number-of-cells, value pairs
 *
 * Set the specified property on the specified node in the device tree
 * to be an array of cells. The values of the cells are specified via
 * the values list, which alternates between "number of cells used by
 * this value" and "value".
 * number-of-cells must be either 1 or 2 (other values will result in
 * an error being returned). If a value is too large to fit in the
 * number of cells specified for it, an error is returned.
 *
 * This function is useful because device tree nodes often have cell arrays
 * which are either lists of addresses or lists of address,size tuples, but
 * the number of cells used for each element vary depending on the
 * #address-cells and #size-cells properties of their parent node.
 * If you know all your cell elements are one cell wide you can use the
 * simpler qemu_fdt_setprop_cells(). If you're not setting up the
 * array programmatically, qemu_fdt_setprop_sized_cells may be more
 * convenient.
 *
 * Return value: 0 on success, <0 on error.
 */
int qemu_fdt_setprop_sized_cells_from_array(void *fdt,
                                            const char *node_path,
                                            const char *property,
                                            int numvalues,
                                            uint64_t *values);

/**
 * qemu_fdt_setprop_sized_cells:
 * @fdt: device tree blob
 * @node_path: node to set property on
 * @property: property to set
 * @...: list of number-of-cells, value pairs
 *
 * Set the specified property on the specified node in the device tree
 * to be an array of cells. The values of the cells are specified via
 * the variable arguments, which alternates between "number of cells
 * used by this value" and "value".
 *
 * This is a convenience wrapper for the function
 * qemu_fdt_setprop_sized_cells_from_array().
 *
 * Return value: 0 on success, <0 on error.
 */
#define qemu_fdt_setprop_sized_cells(fdt, node_path, property, ...)       \
    ({                                                                    \
        uint64_t qdt_tmp[] = { __VA_ARGS__ };                             \
        qemu_fdt_setprop_sized_cells_from_array(fdt, node_path,           \
                                                property,                 \
                                                ARRAY_SIZE(qdt_tmp) / 2,  \
                                                qdt_tmp);                 \
    })

typedef struct QEMUDevtreeProp {
    char *name;
    int len;
    void *value;
} QEMUDevtreeProp;

/* node queries */

char *qemu_devtree_get_node_name(void *fdt, const char *node_path);
int qemu_devtree_get_node_depth(void *fdt, const char *node_path);
int qemu_devtree_get_num_children(void *fdt, const char *node_path, int depth);
char **qemu_devtree_get_children(void *fdt, const char *node_path, int depth);
int qemu_devtree_num_props(void *fdt, const char *node_path);
QEMUDevtreeProp *qemu_devtree_get_props(void *fdt, const char *node_path);

/* node getters */

int qemu_devtree_node_by_compatible(void *fdt, char *node_path,
                        const char *compats);
int qemu_devtree_get_node_by_name(void *fdt, char *node_path,
                        const char *cmpname);
int qemu_devtree_get_node_by_phandle(void *fdt, char *node_path, int phandle);
int qemu_devtree_getparent(void *fdt, char *node_path,
                        const char *current);
int qemu_devtree_get_root_node(void *fdt, char *node_path);

/* misc */

int devtree_get_num_nodes(void *fdt);
void devtree_info_dump(void *fdt);

#define DT_PATH_LENGTH 1024

#endif /* __DEVICE_TREE_H__ */
