#ifndef DKR_CAMERA_KERNEL_GUEST_H
#define DKR_CAMERA_KERNEL_GUEST_H
#include <stdint.h>
void* dkr_camera_access(uint64_t address,unsigned width);
void dkr_camera_step(void);
void dkr_camera_fault(const char*);
#endif
