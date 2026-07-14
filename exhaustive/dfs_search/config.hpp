#pragma once

#ifndef N
#define N 6
#endif
#define VERTICES   (1 << N)
#define MAX_LENGTH VERTICES

#ifndef PREFIX_LENGTH
#define PREFIX_LENGTH 11
#endif

#ifndef SLICE_COUNT
#define SLICE_COUNT 1
#endif
#ifndef SLICE_ID
#define SLICE_ID 0
#endif
#ifndef ROOT_OFFSET
#define ROOT_OFFSET 0
#endif
#ifndef ROOT_COUNT
#define ROOT_COUNT (-1)
#endif

#ifndef KNUTH_PROBES
#define KNUTH_PROBES 0
#endif
#ifndef PROBE_ONLY
#define PROBE_ONLY 0
#endif

#define TAG_REQUEST 1
#define TAG_TASK    2
#define TAG_STOP    3
#define TAG_RESULT  4

#define TASK_INTS   (PREFIX_LENGTH + 1)
#define RESULT_DBLS 5

#define BAR_WIDTH   40
#define CKPT_PERIOD 2.0

static inline bool inSlice(unsigned long long i)
{
#if SLICE_COUNT > 1
    return (i % (unsigned long long)SLICE_COUNT) == (unsigned long long)SLICE_ID;
#else
    if (i < (unsigned long long)ROOT_OFFSET) return false;
#if ROOT_COUNT > 0
    if (i >= (unsigned long long)ROOT_OFFSET + (unsigned long long)ROOT_COUNT) return false;
#endif
    return true;
#endif
}
