// driver_prefixgen.cpp — standalone canonical-prefix generator.
//
// Streams every canonical prefix of PREFIX_LENGTH vertices straight to a
// rolling set of .pfx batch files. Holds only the current walk path in memory,
// so its footprint is O(2^N + PREFIX_LENGTH) no matter how many prefixes it
// emits — the point being to produce far more prefixes than fit in RAM and
// partition them across files for distributed DFS.
//
// Usage:
//   ./prefixgen_tool [out_dir] [batch_size] [from_ordinal] [to_ordinal]
// Defaults: out_dir=prefixes, batch_size=1000000, from=0, to=all.
//
// N and PREFIX_LENGTH are compile-time (config.hpp); the .pfx header records
// them so the DFS runner rejects a mismatched build.

#include "config.hpp"
#include "prefixgen.hpp"
#include "pfxio.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <climits>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKEDIR(path) _mkdir(path)
#else
#define MAKEDIR(path) mkdir((path), 0777)
#endif

static volatile sig_atomic_t g_stop = 0;
static void onSignal(int) { g_stop = 1; }

int main(int argc, char **argv)
{
    const char    *outDir = (argc > 1) ? argv[1] : "prefixes";
    unsigned long long batchSize = (argc > 2) ? strtoull(argv[2], nullptr, 10) : 1000000ULL;
    unsigned long long from = (argc > 3) ? strtoull(argv[3], nullptr, 10) : 0ULL;
    unsigned long long to   = (argc > 4) ? strtoull(argv[4], nullptr, 10) : ULLONG_MAX;

    if (batchSize == 0) batchSize = 1;
    if (to <= from) { fprintf(stderr, "prefixgen_tool: 'to' must be greater than 'from'\n"); return 1; }

    MAKEDIR(outDir);

    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    printf("Generating prefixes: N=%d, PREFIX_LENGTH=%d\n", N, PREFIX_LENGTH);
    printf("Output dir=%s, batch size=%llu, ordinal range=[%llu, %llu)\n",
           outDir, batchSize, from, to);
    fflush(stdout);

    PfxWriter w;
    w.init(outDir, batchSize);

    bool writeError = false;
    PrefixGen g;
    g.generate([&](const int *verts, int tc, unsigned long long idx) -> bool {
        if (g_stop) return false;                 // finalize current file, stop
        if (idx <  from) return true;             // below window: skip
        if (idx >= to)   return false;            // past window: stop
        if (!w.add(verts, tc)) { writeError = true; return false; }
        return true;
    });
    w.finish();

    unsigned long long walked = g.ordinal;
    printf("\nDone%s. Prefixes written: %llu, files: %d (of %llu total canonical prefixes walked).\n",
           g_stop ? " (interrupted)" : "", (unsigned long long)w.total, w.fileIdx,
           walked);
    if (writeError) { fprintf(stderr, "prefixgen_tool: aborted on a file write error.\n"); return 1; }
    return 0;
}
