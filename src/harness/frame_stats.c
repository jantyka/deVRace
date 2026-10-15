#include "frame_stats.h"
#include "include/harness/os.h"
#include "include/harness/trace.h"

#include <stdio.h>
#include <stdlib.h>

// Frames beyond this count in one interval are still counted, but not used for the percentile
#define FRAME_STATS_MAX_SAMPLES 8192

static void (*original_swap)(br_pixelmap* back_buffer);
static double interval;
static double interval_start;
static double last_swap_end;

static int frame_count;
static double frame_sum;
static double frame_min;
static double frame_max;
static double work_sum;
static double work_max;
static float frame_samples[FRAME_STATS_MAX_SAMPLES];

static int CompareFloats(const void* pA, const void* pB) {
    float a = *(const float*)pA;
    float b = *(const float*)pB;
    return (a > b) - (a < b);
}

static void ResetInterval(double pNow) {
    interval_start = pNow;
    frame_count = 0;
    frame_sum = 0;
    frame_min = 0;
    frame_max = 0;
    work_sum = 0;
    work_max = 0;
}

static void ReportInterval(double pNow) {
    int sample_count;
    double p99;
    char line[256];

    if (frame_count == 0) {
        return;
    }
    sample_count = frame_count < FRAME_STATS_MAX_SAMPLES ? frame_count : FRAME_STATS_MAX_SAMPLES;
    qsort(frame_samples, sample_count, sizeof(frame_samples[0]), CompareFloats);
    p99 = frame_samples[(sample_count * 99) / 100];

    snprintf(line, sizeof(line), "%.1fs: %d frames, %.1f fps | frame ms avg %.2f min %.2f max %.2f p99 %.2f | work ms avg %.2f max %.2f",
        pNow - interval_start,
        frame_count,
        frame_count / frame_sum,
        frame_sum * 1000 / frame_count,
        frame_min * 1000,
        frame_max * 1000,
        p99 * 1000,
        work_sum * 1000 / frame_count,
        work_max * 1000);
    LOG_INFO2("%s", line);
    // stdout is fully buffered when redirected to a file, flush so the numbers can be followed live
    fflush(stdout);
}

static void FrameStats_Swap(br_pixelmap* back_buffer) {
    double before;
    double after;
    double frame;
    double work;

    before = OS_GetMonotonicTime();
    original_swap(back_buffer);
    after = OS_GetMonotonicTime();

    if (last_swap_end == 0) {
        last_swap_end = after;
        ResetInterval(after);
        return;
    }

    frame = after - last_swap_end;
    work = before - last_swap_end;
    last_swap_end = after;

    if (frame_count < FRAME_STATS_MAX_SAMPLES) {
        frame_samples[frame_count] = (float)frame;
    }
    if (frame_count == 0 || frame < frame_min) {
        frame_min = frame;
    }
    if (frame > frame_max) {
        frame_max = frame;
    }
    if (work > work_max) {
        work_max = work;
    }
    frame_count++;
    frame_sum += frame;
    work_sum += work;

    if (after - interval_start >= interval) {
        ReportInterval(after);
        ResetInterval(after);
    }
}

void FrameStats_Install(tHarness_platform* pPlatform, int pInterval_seconds) {
    if (pPlatform->Swap == NULL || original_swap != NULL) {
        return;
    }
    original_swap = pPlatform->Swap;
    pPlatform->Swap = FrameStats_Swap;
    interval = pInterval_seconds;
    LOG_INFO2("Frame timing statistics every %d s", pInterval_seconds);
}
