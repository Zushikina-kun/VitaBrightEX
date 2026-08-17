#pragma once
/*
 * Declarations for taihenModuleUtils functions not in taihen.h.
 * These are provided by libtaihenModuleUtils_stub.a.
 * See: https://tai.henkaku.xyz/docs/group__module.html
 */
#include <taihen.h>
#include <psp2kern/types.h>

/**
 * Gets a loaded module's info by name or NID.
 */
int module_get_by_name_nid(SceUID pid, const char *name, uint32_t nid,
                           tai_module_info_t *info);

/**
 * Gets a virtual address from a segment + byte offset within a loaded module.
 */
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset,
                      uintptr_t *addr);

/**
 * Gets the address of an exported function by NID.
 * @param pid        Process/kernel PID (use KERNEL_PID for kernel modules)
 * @param modname    Module name string
 * @param libnid     Library NID, or TAI_ANY_LIBRARY
 * @param funcnid    Function NID
 * @param func       Output: resolved function address
 * @return 0 on success, negative on failure
 */
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid,
                           uint32_t funcnid, uintptr_t *func);

/**
 * Gets the stub address of an imported function by NID.
 */
int module_get_import_func(SceUID pid, const char *modname,
                           uint32_t target_libnid, uint32_t funcnid,
                           uintptr_t *stub);
