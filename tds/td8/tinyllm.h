/*
 * tinyllm.h - inference of the TD8 micro-LLM (byte-level GPT, ~125k parameters).
 *
 * The weights file is mapped with mmap(2) and used IN PLACE: loading costs no copy and the
 * pages are read from disk only when the first token touches them (page faults). Several
 * processes that load the same file share the same physical pages (page cache).
 * Each request has its own state: the KV cache (keys and values of the tokens already seen),
 * so that every new token costs one pass through the network instead of re-reading the prompt.
 */
#ifndef TINYLLM_H
#define TINYLLM_H

#include <stddef.h>

#define TL_MAX_LAYERS 8

typedef struct {
    const float *ln1_g, *ln1_b, *w_qkv, *b_qkv, *w_o, *b_o;
    const float *ln2_g, *ln2_b, *w_fc, *b_fc, *w_pr, *b_pr;
} tl_layer;

typedef struct {
    int vocab, ctx, d, n_layer, n_head, d_ff;
    const float *wte, *wpe, *lnf_g, *lnf_b;
    tl_layer layer[TL_MAX_LAYERS];
    void *map;              /* the whole file, mapped read-only */
    size_t map_size;
    long n_params;
} tl_model;

typedef struct {
    const tl_model *m;
    int pos;                /* number of tokens already in the KV cache */
    float *k, *v;           /* KV cache: [n_layer][ctx][d] each */
    float *x, *xb, *qkv, *att, *ff, *logits;
    size_t bytes;           /* memory used by this state (KV cache + scratch) */
} tl_state;

/* Called for every generated token when tracing: the 3 best candidates and their probability. */
typedef void (*tl_trace_fn)(int step, const int top[3], const float prob[3], void *arg);

int  tl_load(tl_model *m, const char *path);        /* 0 on success, -1 (errno set) otherwise */
void tl_unload(tl_model *m);
int  tl_state_init(tl_state *s, const tl_model *m);
void tl_state_free(tl_state *s);

/* Feed one token (a byte) at the next position; returns the logits of the next token. */
const float *tl_step(tl_state *s, int token);

/*
 * Build the prompt "Q: <request>\nA: ", run the model and write the answer (greedy decoding,
 * stops at '\n') into out. Returns the number of generated tokens, or -1 if the request does
 * not fit in the context.
 */
int tl_generate(const tl_model *m, const char *request, char *out, size_t outsz,
                tl_trace_fn trace, void *arg);

#endif /* TINYLLM_H */
