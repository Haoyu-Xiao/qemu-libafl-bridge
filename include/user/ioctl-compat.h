/*
 *  Generic thunking code to convert data between host and target CPU
 *
 *  Copyright (c) 2025 Haoyu Xiao
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef USER_IOCTL_COMPAT_H
#define USER_IOCTL_COMPAT_H

#include "qemu/osdep.h"
#include "user/thunk.h"

// Assume host is x86_64 Linux
#define HOST_IOC_NRBITS       8
#define HOST_IOC_TYPEBITS     8
#define HOST_IOC_SIZEBITS     14
#define HOST_IOC_DIRBITS      2

#define HOST_IOC_NRMASK       ((1 << HOST_IOC_NRBITS)-1)
#define HOST_IOC_TYPEMASK     ((1 << HOST_IOC_TYPEBITS)-1)
#define HOST_IOC_SIZEMASK     ((1 << HOST_IOC_SIZEBITS)-1)
#define HOST_IOC_DIRMASK      ((1 << HOST_IOC_DIRBITS)-1)

#define HOST_IOC_NRSHIFT      0
#define HOST_IOC_TYPESHIFT    (HOST_IOC_NRSHIFT+HOST_IOC_NRBITS)
#define HOST_IOC_SIZESHIFT    (HOST_IOC_TYPESHIFT+HOST_IOC_TYPEBITS)
#define HOST_IOC_DIRSHIFT     (HOST_IOC_SIZESHIFT+HOST_IOC_SIZEBITS)

#define HOST_IOC_NONE   0U
#define HOST_IOC_WRITE  1U
#define HOST_IOC_READ   2U

#define HOST_IOC(nr, type, size, dir) \
    (((nr)  << HOST_IOC_NRSHIFT) | \
     ((type) << HOST_IOC_TYPESHIFT) | \
     ((size) << HOST_IOC_SIZESHIFT) | \
     ((dir)  << HOST_IOC_DIRSHIFT))

#define host_ioc_dir(cmd)   (((cmd) >> HOST_IOC_DIRSHIFT) & (HOST_IOC_WRITE | HOST_IOC_READ))
#define host_ioc_size(cmd)  (((cmd) >> HOST_IOC_SIZESHIFT) & HOST_IOC_SIZEMASK)

static inline bool ioctl_cmd_may_conflict(int cmd) {
    return !(cmd & (~HOST_IOC_NRMASK));
}

// Mainly convert size and direction bits to target format
static inline int ioctl_cmd_trans(int cmd) {

    // Hack: Translate easily conflict ioctl command
    if (ioctl_cmd_may_conflict(cmd)) {
        return cmd | (0xff << HOST_IOC_TYPESHIFT);
    }

#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_SH4)  \
    || defined(TARGET_M68K) || defined(TARGET_CRIS)                     \
    || defined(TARGET_S390X) || defined(TARGET_OPENRISC)                \
    || defined(TARGET_RISCV)                                            \
    || defined(TARGET_XTENSA) || defined(TARGET_LOONGARCH64) \
    || defined(TARGET_HEXAGON)
    return cmd;
#elif defined(TARGET_PPC) || defined(TARGET_ALPHA) ||           \
    defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE) ||      \
    defined(TARGET_MIPS) || defined(TARGET_HPPA)
    int dir = (cmd >> TARGET_IOC_DIRSHIFT) & TARGET_IOC_DIRMASK;
    switch (dir) {
    case TARGET_IOC_NONE:
        dir = HOST_IOC_NONE;
        break;
    case TARGET_IOC_READ:
        dir = HOST_IOC_READ;
        break;
    case TARGET_IOC_WRITE:
        dir = HOST_IOC_WRITE;
        break;
    case TARGET_IOC_READ | TARGET_IOC_WRITE:
        dir = HOST_IOC_READ | HOST_IOC_WRITE;
        break;
    default:
        dir = HOST_IOC_NONE;
        // fprintf(stderr, "Unsupported ioctl direction: cmd=0x%04lx, dir=%d\n",
        //                 (long)cmd, dir);
        // qemu_log_mask(LOG_UNIMP, "Unsupported ioctl direction: cmd=0x%04lx\n",
                    //   (long)cmd);
        return cmd;
    }
    int new_cmd = (cmd & ((1 << TARGET_IOC_SIZESHIFT) - 1)) | \
        ((((cmd >> TARGET_IOC_SIZESHIFT) & TARGET_IOC_SIZEMASK) & HOST_IOC_SIZEMASK) << HOST_IOC_SIZESHIFT) | \
        (dir << HOST_IOC_DIRSHIFT);
    // fprintf(stderr, "ioctl cmd %x -> %x\n", cmd, new_cmd);
    return new_cmd;
#endif
}

#define EINCOMPAT 250 /* Incompatible ioctl for device */
#define TARGET_EINCOMPAT 250
#define COMPAT_IOCTL_ARG_TY_MAX 32
#define COMPAT_RDWR_DATA_MAX 512

#define COMPAT_FLAG_CONVERT 0x1
#define COMPAT_FLAG_LOG 0x2
#define COMPAT_FLAG_FLUSH 0x4

typedef struct compat_ioctl_info_t {
    uint32_t cmd; // original/translated cmd; 
    uint32_t size; // arg data size
    uint32_t flags;
    uint32_t arg_types[COMPAT_IOCTL_ARG_TY_MAX];
} compat_ioctl_info_t;

typedef struct compat_flush_info_t {
    uint32_t size; // size of data
    uint32_t offset; // offset of data to read
} compat_flush_info_t;

#define IOCTL_COMPAT_LOG_MAX_SIZE 0x200

#define IOCTL_COMPAT_IOCTL HOST_IOC(0xE0, 'i', sizeof(compat_ioctl_info_t), HOST_IOC_READ | HOST_IOC_WRITE)
#define IOCTL_COMPAT_READ HOST_IOC(0xE1, 'i', 0, HOST_IOC_NONE)
#define IOCTL_COMPAT_WRITE HOST_IOC(0xE2, 'i', 0, HOST_IOC_NONE)
#define IOCTL_COMPAT_LOG HOST_IOC(0xF0, 'i', IOCTL_COMPAT_LOG_MAX_SIZE, HOST_IOC_READ)
#define IOCTL_COMPAT_FLUSH HOST_IOC(0xF1, 'i', sizeof(compat_flush_info_t), HOST_IOC_READ)

const argtype *ioctl_data_type_next(const argtype *type_ptr);

int ioctl_data_type_size(const argtype *type_ptr, int to_host);

const argtype *ioctl_data_convert(void *dst, const void *src, const argtype *type_ptr, int to_host);

#endif