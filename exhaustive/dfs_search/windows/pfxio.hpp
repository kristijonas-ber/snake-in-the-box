// pfxio.hpp (Windows copy) — read/write standalone prefix batch files (.pfx).
//
// Identical to the mac/Linux ../pfxio.hpp except that count-trailing-zeros uses
// sib_ctz() (config.hpp) instead of __builtin_ctz, which MSVC lacks. Keep the two
// in sync if the format changes.
//
// A prefix is one canonical induced path of PREFIX_LENGTH vertices, always
// starting at vertex 0, plus its transitionCounter — the complete seed
// Search::dfs_from_prefix needs. Stored per prefix: PREFIX_LENGTH-1 transition
// bytes + one transitionCounter byte.
//
// Layout (all header integers little-endian):
//   header : 'P''F''X''1' | u16 N | u16 PREFIX_LENGTH | u64 record_count
//   record : (PREFIX_LENGTH-1) transition bytes | 1 transitionCounter byte
#pragma once
#include "config.hpp"
#include "prefixgen.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

#define PFX_REC_BYTES   (PREFIX_LENGTH)          // (PREFIX_LENGTH-1) transitions + 1 tc
#define PFX_HDR_BYTES   16
#define PFX_COUNT_OFF   8                         // byte offset of record_count in header

// ---- little-endian scalar helpers -----------------------------------------

static inline void pfxPutU16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}
static inline void pfxPutU64(unsigned char *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}
static inline uint16_t pfxGetU16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint64_t pfxGetU64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

// ---- record encode / decode -----------------------------------------------

// verts[0..PREFIX_LENGTH-1] (verts[0] == 0) + tc  ->  PFX_REC_BYTES bytes.
static inline void pfxEncode(const int *verts, int tc, unsigned char *rec)
{
    for (int i = 1; i < PREFIX_LENGTH; i++)
        rec[i - 1] = (unsigned char)sib_ctz((unsigned)(verts[i] ^ verts[i - 1]));
    rec[PREFIX_LENGTH - 1] = (unsigned char)tc;
}

// PFX_REC_BYTES bytes  ->  Prefix{vertices, transitionCounter}.
static inline void pfxDecode(const unsigned char *rec, Prefix &out)
{
    out.vertices[0] = 0;
    for (int i = 1; i < PREFIX_LENGTH; i++)
        out.vertices[i] = out.vertices[i - 1] ^ (1 << rec[i - 1]);
    out.transitionCounter = rec[PREFIX_LENGTH - 1];
}

// ---- writer: rolling, self-finalizing batch files -------------------------

struct PfxWriter {
    FILE       *f        = nullptr;
    uint64_t    inFile   = 0;
    uint64_t    total    = 0;
    uint64_t    batchSize = 0;
    int         fileIdx  = 0;
    std::string dir;

    void init(const std::string &outDir, uint64_t batch)
    {
        dir = outDir;
        batchSize = batch ? batch : 1;
    }

    bool openNext()
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/batch_%05d.pfx", dir.c_str(), fileIdx);
        f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "pfxio: cannot open %s for writing\n", path); return false; }
        unsigned char hdr[PFX_HDR_BYTES];
        memcpy(hdr, "PFX1", 4);
        pfxPutU16(hdr + 4, (uint16_t)N);
        pfxPutU16(hdr + 6, (uint16_t)PREFIX_LENGTH);
        pfxPutU64(hdr + PFX_COUNT_OFF, 0);           // patched on close
        fwrite(hdr, 1, PFX_HDR_BYTES, f);
        inFile = 0;
        return true;
    }

    void closeCurrent()
    {
        if (!f) return;
        unsigned char cnt[8];
        pfxPutU64(cnt, inFile);
        fseek(f, PFX_COUNT_OFF, SEEK_SET);
        fwrite(cnt, 1, 8, f);
        fclose(f);
        f = nullptr;
        fileIdx++;
    }

    bool add(const int *verts, int tc)
    {
        if (!f || inFile == batchSize)
        {
            closeCurrent();
            if (!openNext()) return false;
        }
        unsigned char rec[PFX_REC_BYTES];
        pfxEncode(verts, tc, rec);
        fwrite(rec, 1, PFX_REC_BYTES, f);
        inFile++;
        total++;
        return true;
    }

    void finish() { closeCurrent(); }
};

// ---- reader ---------------------------------------------------------------

// Append every prefix in `path` to `out`. Returns false on a missing file or a
// header that does not match this build's N / PREFIX_LENGTH.
static inline bool pfxRead(const char *path, std::vector<Prefix> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "pfxio: cannot open %s\n", path); return false; }

    unsigned char hdr[PFX_HDR_BYTES];
    if (fread(hdr, 1, PFX_HDR_BYTES, f) != PFX_HDR_BYTES || memcmp(hdr, "PFX1", 4) != 0)
    {
        fprintf(stderr, "pfxio: %s is not a .pfx file\n", path);
        fclose(f);
        return false;
    }
    uint16_t fn = pfxGetU16(hdr + 4), fpl = pfxGetU16(hdr + 6);
    if (fn != (uint16_t)N || fpl != (uint16_t)PREFIX_LENGTH)
    {
        fprintf(stderr, "pfxio: %s built for N=%u PREFIX_LENGTH=%u, this binary is N=%d PREFIX_LENGTH=%d\n",
                path, fn, fpl, N, PREFIX_LENGTH);
        fclose(f);
        return false;
    }
    uint64_t count = pfxGetU64(hdr + PFX_COUNT_OFF);

    unsigned char rec[PFX_REC_BYTES];
    uint64_t got = 0;
    while (fread(rec, 1, PFX_REC_BYTES, f) == PFX_REC_BYTES)
    {
        Prefix p;
        pfxDecode(rec, p);
        out.push_back(p);
        got++;
    }
    fclose(f);
    if (count && got != count)
        fprintf(stderr, "pfxio: warning: %s header says %llu records, read %llu\n",
                path, (unsigned long long)count, (unsigned long long)got);
    return true;
}
