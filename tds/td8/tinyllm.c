/*
 * tinyllm.c - forward pass of the TD8 micro-LLM, identical to train/train.py (NumPy).
 *   x = wte[token] + wpe[pos]
 *   for each layer:  x += Wo · attention(LN1(x));   x += Wpr · GELU(Wfc · LN2(x))
 *   logits = wte · LN_f(x)
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tinyllm.h"

#define HEADER 64

int tl_load(tl_model *m, const char *path)
{
    memset(m, 0, sizeof *m);
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return -1;
    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size < HEADER) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    /* MAP_PRIVATE + PROT_READ: nobody can modify the weights through this mapping. */
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);                                   /* the mapping keeps the file alive */
    if (map == MAP_FAILED)
        return -1;

    const unsigned char *raw = map;
    int32_t h[7];
    memcpy(h, raw + 8, sizeof h);
    if (memcmp(raw, "FSOLLM01", 8) != 0 || h[0] != 1 || h[4] > TL_MAX_LAYERS || h[3] % h[5] != 0) {
        munmap(map, (size_t)st.st_size);
        errno = EINVAL;
        return -1;
    }
    m->vocab = h[1]; m->ctx = h[2]; m->d = h[3]; m->n_layer = h[4]; m->n_head = h[5]; m->d_ff = h[6];
    m->map = map;
    m->map_size = (size_t)st.st_size;

    /* The pointers point INTO the mapping: no copy (the file is little-endian float32). */
    const float *p = (const float *)(raw + HEADER);
    const int d = m->d, f = m->d_ff;
#define TAKE(dst, n) do { (dst) = p; p += (n); } while (0)
    TAKE(m->wte, (long)m->vocab * d);
    TAKE(m->wpe, (long)m->ctx * d);
    for (int l = 0; l < m->n_layer; l++) {
        tl_layer *L = &m->layer[l];
        TAKE(L->ln1_g, d); TAKE(L->ln1_b, d);
        TAKE(L->w_qkv, (long)d * 3 * d); TAKE(L->b_qkv, 3 * d);
        TAKE(L->w_o, (long)d * d); TAKE(L->b_o, d);
        TAKE(L->ln2_g, d); TAKE(L->ln2_b, d);
        TAKE(L->w_fc, (long)d * f); TAKE(L->b_fc, f);
        TAKE(L->w_pr, (long)f * d); TAKE(L->b_pr, d);
    }
    TAKE(m->lnf_g, d); TAKE(m->lnf_b, d);
#undef TAKE
    m->n_params = (long)(p - (const float *)(raw + HEADER));
    if ((size_t)HEADER + (size_t)m->n_params * sizeof(float) != m->map_size) {
        munmap(map, m->map_size);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void tl_unload(tl_model *m)
{
    if (m->map)
        munmap(m->map, m->map_size);
    m->map = NULL;
}

int tl_state_init(tl_state *s, const tl_model *m)
{
    memset(s, 0, sizeof *s);
    s->m = m;
    size_t kv = (size_t)m->n_layer * m->ctx * m->d;
    size_t n = 2 * kv + 2 * m->d + 3 * m->d + (size_t)m->n_head * m->ctx + m->d_ff + m->vocab;
    float *mem = calloc(n, sizeof(float));
    if (mem == NULL)
        return -1;
    s->k = mem;                 s->v = s->k + kv;
    s->x = s->v + kv;           s->xb = s->x + m->d;
    s->qkv = s->xb + m->d;      s->att = s->qkv + 3 * m->d;
    s->ff = s->att + (size_t)m->n_head * m->ctx;
    s->logits = s->ff + m->d_ff;
    s->bytes = n * sizeof(float);
    return 0;
}

void tl_state_free(tl_state *s)
{
    free(s->k);
    s->k = NULL;
}

/* out[j] = b[j] + sum_i x[i] * W[i][j]   (W stored row-major, n_in x n_out, like NumPy x @ W) */
static void matvec(float *out, const float *x, const float *W, const float *b, int n_in, int n_out)
{
    for (int j = 0; j < n_out; j++)
        out[j] = b ? b[j] : 0.0f;
    for (int i = 0; i < n_in; i++) {
        const float xi = x[i], *row = W + (long)i * n_out;
        for (int j = 0; j < n_out; j++)
            out[j] += xi * row[j];
    }
}

static void layernorm(float *out, const float *x, const float *g, const float *b, int n)
{
    float mu = 0, var = 0;
    for (int i = 0; i < n; i++) mu += x[i];
    mu /= n;
    for (int i = 0; i < n; i++) var += (x[i] - mu) * (x[i] - mu);
    const float rstd = 1.0f / sqrtf(var / n + 1e-5f);
    for (int i = 0; i < n; i++) out[i] = (x[i] - mu) * rstd * g[i] + b[i];
}

static float gelu(float x)
{
    return 0.5f * x * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
}

const float *tl_step(tl_state *s, int token)
{
    const tl_model *m = s->m;
    const int d = m->d, nh = m->n_head, hd = d / nh, t = s->pos;
    if (t >= m->ctx)
        return NULL;

    for (int i = 0; i < d; i++)
        s->x[i] = m->wte[(long)token * d + i] + m->wpe[(long)t * d + i];

    for (int l = 0; l < m->n_layer; l++) {
        const tl_layer *L = &m->layer[l];
        float *kc = s->k + ((size_t)l * m->ctx) * d, *vc = s->v + ((size_t)l * m->ctx) * d;

        /* ---- causal self-attention: this token looks at itself and every previous one ---- */
        layernorm(s->xb, s->x, L->ln1_g, L->ln1_b, d);
        matvec(s->qkv, s->xb, L->w_qkv, L->b_qkv, d, 3 * d);
        memcpy(kc + (size_t)t * d, s->qkv + d, d * sizeof(float));       /* store k and v */
        memcpy(vc + (size_t)t * d, s->qkv + 2 * d, d * sizeof(float));
        float y[d];
        for (int h = 0; h < nh; h++) {
            const float *q = s->qkv + h * hd;
            float *att = s->att + h * m->ctx, mx = -1e30f, sum = 0;
            for (int u = 0; u <= t; u++) {
                const float *k = kc + (size_t)u * d + h * hd;
                float dot = 0;
                for (int i = 0; i < hd; i++) dot += q[i] * k[i];
                att[u] = dot / sqrtf((float)hd);
                if (att[u] > mx) mx = att[u];
            }
            for (int u = 0; u <= t; u++) { att[u] = expf(att[u] - mx); sum += att[u]; }
            for (int i = 0; i < hd; i++) y[h * hd + i] = 0;
            for (int u = 0; u <= t; u++) {
                const float w = att[u] / sum, *v = vc + (size_t)u * d + h * hd;
                att[u] = w;
                for (int i = 0; i < hd; i++) y[h * hd + i] += w * v[i];
            }
        }
        matvec(s->xb, y, L->w_o, L->b_o, d, d);
        for (int i = 0; i < d; i++) s->x[i] += s->xb[i];

        /* ---- MLP ---- */
        layernorm(s->xb, s->x, L->ln2_g, L->ln2_b, d);
        matvec(s->ff, s->xb, L->w_fc, L->b_fc, d, m->d_ff);
        for (int i = 0; i < m->d_ff; i++) s->ff[i] = gelu(s->ff[i]);
        matvec(s->xb, s->ff, L->w_pr, L->b_pr, m->d_ff, d);
        for (int i = 0; i < d; i++) s->x[i] += s->xb[i];
    }

    layernorm(s->xb, s->x, m->lnf_g, m->lnf_b, d);
    for (int v = 0; v < m->vocab; v++) {                 /* tied embeddings: logits = wte . x */
        const float *e = m->wte + (long)v * d;
        float dot = 0;
        for (int i = 0; i < d; i++) dot += e[i] * s->xb[i];
        s->logits[v] = dot;
    }
    s->pos++;
    return s->logits;
}

int tl_generate(const tl_model *m, const char *request, char *out, size_t outsz,
                tl_trace_fn trace, void *arg)
{
    char prompt[512];
    int n = snprintf(prompt, sizeof prompt, "Q: %s\nA: ", request);
    if (n < 0 || n >= (int)sizeof prompt || n >= m->ctx)
        return -1;

    tl_state s;
    if (tl_state_init(&s, m) == -1)
        return -1;
    const float *logits = NULL;
    for (int i = 0; i < n; i++)                           /* "prefill": read the whole prompt */
        logits = tl_step(&s, (unsigned char)prompt[i]);

    size_t len = 0;
    int steps = 0;
    while (logits != NULL && len + 1 < outsz) {           /* "decode": one token at a time */
        int top[3] = { -1, -1, -1 };                      /* the 3 best candidates, sorted */
        for (int v = 0; v < m->vocab; v++)
            for (int k = 0; k < 3; k++)
                if (top[k] == -1 || logits[v] > logits[top[k]]) {
                    for (int j = 2; j > k; j--) top[j] = top[j - 1];
                    top[k] = v;
                    break;
                }
        if (trace) {
            float mx = logits[top[0]], sum = 0, prob[3];
            for (int v = 0; v < m->vocab; v++) sum += expf(logits[v] - mx);
            for (int k = 0; k < 3; k++) prob[k] = expf(logits[top[k]] - mx) / sum;
            trace(steps, top, prob, arg);
        }
        int next = top[0];                                /* greedy: always the most likely */
        steps++;
        if (next == '\n')
            break;
        out[len++] = (char)next;
        logits = tl_step(&s, next);
    }
    out[len] = '\0';
    tl_state_free(&s);
    return steps;
}
