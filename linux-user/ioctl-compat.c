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

#include "qemu/osdep.h"

#include "qemu.h"
#include "user/ioctl-compat.h"

const argtype *ioctl_data_type_next(const argtype *type_ptr) {
    switch (*type_ptr++) {
    case TYPE_STRUCT: {
        int elements = *type_ptr++;
        for (int i = 0; i < elements; i++) {
            type_ptr = ioctl_data_type_next(type_ptr);
        }
        break;
    }
    case TYPE_PTR:
        type_ptr = ioctl_data_type_next(type_ptr);
        break;
    case TYPE_ARRAY:
        type_ptr = ioctl_data_type_next(type_ptr + 1);
        break;
    default:
        break;
    }
    return type_ptr;
}

int ioctl_data_type_size(const argtype *type_ptr, int to_host) {
    // fprintf(stderr, "[qemu] ioctl_data_type_size [%d] type=%d, ptr=%p\n", to_host, *type_ptr, type_ptr);
    switch (*type_ptr++) {
    case TYPE_STRUCT: {
        int total_size = 0;
        int elements = *type_ptr++;
        for (int i = 0; i < elements; i++) {
            // fprintf(stderr, "[qemu] ioctl_data_type_size struct[%d]=%d\n", i, *type_ptr);
            total_size += ioctl_data_type_size(type_ptr, to_host);
            type_ptr = ioctl_data_type_next(type_ptr);
        }
        return total_size;
    }
    case TYPE_PTR:
        if (to_host) {
            return ioctl_data_type_size(type_ptr, to_host);
        } else {
            const argtype tmp_argtype[] = {
                TYPE_PTRVOID, 0
            };
            return ioctl_data_type_size(tmp_argtype, THUNK_TARGET);
        }
    case TYPE_ARRAY:
        {
            int array_length = *type_ptr++;
            return array_length * ioctl_data_type_size(type_ptr, to_host);
        }
    default:
        return thunk_type_size(type_ptr - 1, to_host);
    }
}

const argtype *ioctl_data_convert(void *dst, const void *src, const argtype *type_ptr, int to_host) {
    int src_size = ioctl_data_type_size(type_ptr, 1 - to_host);
    int dst_size = ioctl_data_type_size(type_ptr, to_host);
    // fprintf(stderr, "[qemu] ioctl_data_convert dst=%p, src=%p, type=%d, src_size=0x%x, dst_size=0x%x\n", dst, src, *type_ptr, src_size, dst_size);
    switch (*type_ptr++) {
    case TYPE_PTR: {
        if (to_host) {
            abi_long usrc = *(const abi_long *)src;
            if (usrc == 0) {
                memset(dst, 0, dst_size);
                type_ptr = ioctl_data_type_next(type_ptr);
            } else {
                void *new_src = lock_user(VERIFY_READ, usrc, src_size, 1);
                if (!new_src) {
                    fprintf(stderr, "[qemu] ioctl_data_convert lock_user failed for src=%lx, size=%d\n", (unsigned long)usrc, src_size);
                    type_ptr = ioctl_data_type_next(type_ptr);
                } else {
                    type_ptr = ioctl_data_convert(dst, new_src, type_ptr, to_host);
                    unlock_user(new_src, usrc, 0);
                }
            }
        } else {
            abi_long udst = *(abi_long *)dst;
            if (udst == 0) {
                type_ptr = ioctl_data_type_next(type_ptr);
            } else {
                dst_size = ioctl_data_type_size(type_ptr, to_host);
                void *new_dst = g_malloc(dst_size);
                if (!new_dst) {
                    // fprintf(stderr, "[qemu] ioctl_data_convert g_malloc failed for size=%d\n", dst_size);
                    type_ptr = ioctl_data_type_next(type_ptr);
                } else {
                    copy_from_user(new_dst, udst, dst_size);
                    type_ptr = ioctl_data_convert(new_dst, src, type_ptr, to_host);
                    // Here we ignore error
                    copy_to_user(udst, new_dst, dst_size);
                }
                g_free(new_dst);
            }
        }
        break;
    }
    case TYPE_STRUCT: {
        int elements = *type_ptr++;
        // fprintf(stderr, "[qemu] ioctl_data_convert struct with %d fields\n", elements);
        for (int i = 0; i < elements; i++) {
            // fprintf(stderr, "[qemu] ioctl_data_convert field=%d, type=%d\n", i, *type_ptr);
            int dst_elem_size = ioctl_data_type_size(type_ptr, to_host);
            int src_elem_size = ioctl_data_type_size(type_ptr, 1 - to_host);
            type_ptr = ioctl_data_convert(dst, src, type_ptr, to_host);
            dst = (void *)((char *)dst + dst_elem_size);
            src = (void *)((const char *)src + src_elem_size);
        }
        break;
    }
    // case TYPE_ARRAY: {
    //     int array_length = *type_ptr++;
    //     fprintf(stderr, "[qemu] ioctl_data_convert array with %d elements\n", array_length);
    //     int dst_elem_size = ioctl_data_type_size(type_ptr, to_host);
    //     int src_elem_size = ioctl_data_type_size(type_ptr, 1 - to_host);
    //     for (int i = 0; i < array_length; i++) {
    //         ioctl_data_convert(dst, src, type_ptr, to_host);
    //         dst = (void *)((char *)dst + dst_elem_size);
    //         src = (void *)((const char *)src + src_elem_size);
    //     }
    //     type_ptr = ioctl_data_type_next(type_ptr);
    //     break;
    // }
    default:
        type_ptr = thunk_convert(dst, src, type_ptr - 1, to_host);
        break;
    }
    return type_ptr;
}
