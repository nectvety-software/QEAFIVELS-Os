#ifndef VQEAF_AMS_H
#define VQEAF_AMS_H
#include <stdint.h>
/* ActivityManagerService-like app process manager for the independent kernel.
 * Maps the QEAF launch path: the Launcher asks AMS to start an activity;
 * AMS loads the app's .qen module into a fresh isolated user root and spawns
 * an EL0 process (the Zygote-fork equivalent), tracking its lifecycle. Apps
 * are addressed by package name; each gets its own address space and pid. */
#define VQEAF_AMS_MAX_APPS 8u
#define VQEAF_AMS_MAX_PROCS 8u
void vqeaf_ams_init(uint64_t kernel_root);
/* Launch the named installed app at base (page-aligned). Returns pid (>0) on
 * success or a negative error. Prints the AMS launch lifecycle marker. */
int vqeaf_ams_launch(const char *name,uint64_t base);
/* Scheduler slot of a launched pid, or -1. Used to poll process exit. */
int vqeaf_ams_process_slot(int pid);
/* Number of installed apps in the registry. */
int vqeaf_ams_app_count(void);
/* Package name of installed app index i, or 0. */
const char *vqeaf_ams_app_name(int i);
#endif