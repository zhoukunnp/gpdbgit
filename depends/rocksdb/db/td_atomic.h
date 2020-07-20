/*
 * tdsql atomic 
 * kangsun@tencent.com
 */
#pragma once

#include <stdint.h>

namespace rocksdb {


#ifndef UNUSED
#define UNUSED(v) ((void)(v))
#endif

#define __COMPILER_BARRIER() asm volatile("" ::: "memory")
#define WEAK_BARRIER() __COMPILER_BARRIER()
#define MEM_BARRIER() __sync_synchronize()
#define PAUSE() asm("pause\n")


#define ATOMIC_LOAD(x) ({__COMPILER_BARRIER(); *(x);})
#define ATOMIC_STORE(x, v) ({__COMPILER_BARRIER(); *(x) = v; __sync_synchronize(); })

#define ATOMIC_FAAx(val, addv, id)                              \
({ UNUSED(id); __sync_fetch_and_add((val), (addv)); })
#define ATOMIC_AAFx(val, addv, id)                              \
({ UNUSED(id); __sync_add_and_fetch((val), (addv)); })
#define ATOMIC_FASx(val, subv, id)                              \
({ UNUSED(id); __sync_fetch_and_sub((val), (subv)); })
#define ATOMIC_SAFx(val, subv, id)                              \
({ UNUSED(id); __sync_sub_and_fetch((val), (subv)); })
#define ATOMIC_TASx(val, newv, id)                              \
({ UNUSED(id); __sync_lock_test_and_set((val), (newv)); })
#define ATOMIC_SETx(val, newv, id)                              \
({ UNUSED(id); __sync_lock_test_and_set((val), (newv)); })
#define ATOMIC_VCASx(val, cmpv, newv, id)                       \
({ UNUSED(id);                                               \
__sync_val_compare_and_swap((val), (cmpv), (newv)); })
#define ATOMIC_BCASx(val, cmpv, newv, id)                       \
({ UNUSED(id);                                               \
__sync_bool_compare_and_swap((val), (cmpv), (newv)); })

#define LA_ATOMIC_ID 0
#define ATOMIC_FAA(val, addv) ATOMIC_FAAx(val, addv, LA_ATOMIC_ID)
#define ATOMIC_AAF(val, addv) ATOMIC_AAFx(val, addv, LA_ATOMIC_ID)
#define ATOMIC_FAS(val, subv) ATOMIC_FASx(val, subv, LA_ATOMIC_ID)
#define ATOMIC_SAF(val, subv) ATOMIC_SAFx(val, subv, LA_ATOMIC_ID)
#define ATOMIC_TAS(val, newv) ATOMIC_TASx(val, newv, LA_ATOMIC_ID)
#define ATOMIC_SET(val, newv) ATOMIC_SETx(val, newv, LA_ATOMIC_ID)
#define ATOMIC_VCAS(val, cmpv, newv) ATOMIC_VCASx(val, cmpv, newv, LA_ATOMIC_ID)
#define ATOMIC_BCAS(val, cmpv, newv) ATOMIC_BCASx(val, cmpv, newv, LA_ATOMIC_ID)

}
