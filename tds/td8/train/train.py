#!/usr/bin/env python3
"""Trains the TD8 micro-LLM with plain NumPy (no PyTorch): a byte-level GPT-like transformer.

    python3 train.py                 trains (≈ 80 min on a CPU, 12,000 steps) and writes ../weights/tinyllm.bin
    python3 train.py --check         checks the analytic gradient against finite differences
    python3 train.py --eval          evaluates the saved model

Architecture (the same one implemented by ../tinyllm.c and by animation A9.2 of the course hub):
    x = wte[byte] + wpe[position]
    3 blocks: x += causal_attention(LN(x));  x += MLP_GELU(LN(x))       (4 heads, d = 64, MLP 256)
    logits = LN(x) · wteᵀ                                               (tied embeddings)
Weights file format (little-endian): 64-byte header
    "FSOLLM01" | int32 version, vocab, ctx, d, n_layer, n_head, d_ff | padding
followed by the float32 values in the order given by `order()`.
"""
import argparse
import json
import math
import os
import pathlib
import random
import struct
import sys
import time

# Small matrices: more than 4 BLAS threads only add synchronisation (measured: 0.33 s/step with 4, 0.49 with 28).
for _v in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ.setdefault(_v, '4')
import numpy as np  # noqa: E402

import dataset

HERE = pathlib.Path(__file__).resolve().parent
WEIGHTS = HERE.parent / 'weights' / 'tinyllm.bin'
LOG = HERE.parent / 'weights' / 'train_log.json'

V, CTX, D, NL, NH, DFF = 256, 128, 64, 3, 4, 256
EPS = 1e-5


# ------------------------------------------------------------------------------------ parameters
def init_params(rng: np.random.Generator) -> dict:
    s = 0.02
    p = {'wte': rng.normal(0, s, (V, D)), 'wpe': rng.normal(0, s, (CTX, D))}
    for l in range(NL):
        p[f'l{l}.ln1_g'] = np.ones(D); p[f'l{l}.ln1_b'] = np.zeros(D)
        p[f'l{l}.w_qkv'] = rng.normal(0, s, (D, 3 * D)); p[f'l{l}.b_qkv'] = np.zeros(3 * D)
        p[f'l{l}.w_o'] = rng.normal(0, s / math.sqrt(2 * NL), (D, D)); p[f'l{l}.b_o'] = np.zeros(D)
        p[f'l{l}.ln2_g'] = np.ones(D); p[f'l{l}.ln2_b'] = np.zeros(D)
        p[f'l{l}.w_fc'] = rng.normal(0, s, (D, DFF)); p[f'l{l}.b_fc'] = np.zeros(DFF)
        p[f'l{l}.w_pr'] = rng.normal(0, s / math.sqrt(2 * NL), (DFF, D)); p[f'l{l}.b_pr'] = np.zeros(D)
    p['lnf_g'] = np.ones(D); p['lnf_b'] = np.zeros(D)
    return {k: v.astype(np.float32) for k, v in p.items()}


def order() -> list[str]:
    ks = ['wte', 'wpe']
    for l in range(NL):
        ks += [f'l{l}.{n}' for n in ('ln1_g', 'ln1_b', 'w_qkv', 'b_qkv', 'w_o', 'b_o',
                                     'ln2_g', 'ln2_b', 'w_fc', 'b_fc', 'w_pr', 'b_pr')]
    return ks + ['lnf_g', 'lnf_b']


# ------------------------------------------------------------------------------------ layers
def ln_fwd(x, g, b):
    mu = x.mean(-1, keepdims=True)
    var = ((x - mu) ** 2).mean(-1, keepdims=True)
    rstd = 1.0 / np.sqrt(var + EPS)
    xh = (x - mu) * rstd
    return xh * g + b, (xh, rstd, g)


def ln_bwd(dy, cache):
    xh, rstd, g = cache
    dg = (dy * xh).reshape(-1, xh.shape[-1]).sum(0)
    db = dy.reshape(-1, xh.shape[-1]).sum(0)
    dxh = dy * g
    dx = rstd * (dxh - dxh.mean(-1, keepdims=True) - xh * (dxh * xh).mean(-1, keepdims=True))
    return dx, dg, db


C_GELU = math.sqrt(2 / math.pi)


def gelu_fwd(x):
    t = np.tanh(C_GELU * (x + 0.044715 * x * x * x))     # x*x*x: much faster than x**3
    return 0.5 * x * (1 + t), (x, t)


def gelu_bwd(dy, cache):
    x, t = cache
    return dy * (0.5 * (1 + t) + 0.5 * x * (1 - t * t) * C_GELU * (1 + 3 * 0.044715 * x * x))


def attn_fwd(x, w, b, wo, bo):
    B, T, _ = x.shape
    hd = D // NH
    qkv = x @ w + b
    q, k, v = np.split(qkv, 3, axis=-1)
    q = q.reshape(B, T, NH, hd).transpose(0, 2, 1, 3)
    k = k.reshape(B, T, NH, hd).transpose(0, 2, 1, 3)
    v = v.reshape(B, T, NH, hd).transpose(0, 2, 1, 3)
    att = (q @ k.transpose(0, 1, 3, 2)) / math.sqrt(hd)
    mask = np.triu(np.ones((T, T), dtype=bool), 1)
    att = np.where(mask, -1e9, att)
    att = att - att.max(-1, keepdims=True)
    p = np.exp(att)
    p /= p.sum(-1, keepdims=True)
    y = (p @ v).transpose(0, 2, 1, 3).reshape(B, T, D)
    out = y @ wo + bo
    return out, (x, q, k, v, p, y, w, wo)


def attn_bwd(dout, cache):
    x, q, k, v, p, y, w, wo = cache
    B, T, _ = x.shape
    hd = D // NH
    dwo = y.reshape(-1, D).T @ dout.reshape(-1, D)
    dbo = dout.reshape(-1, D).sum(0)
    dy = (dout @ wo.T).reshape(B, T, NH, hd).transpose(0, 2, 1, 3)
    dp = dy @ v.transpose(0, 1, 3, 2)
    dv = p.transpose(0, 1, 3, 2) @ dy
    datt = p * (dp - (dp * p).sum(-1, keepdims=True)) / math.sqrt(hd)
    dq = datt @ k
    dk = datt.transpose(0, 1, 3, 2) @ q
    merge = lambda t: t.transpose(0, 2, 1, 3).reshape(B, T, D)
    dqkv = np.concatenate([merge(dq), merge(dk), merge(dv)], axis=-1)
    dw = x.reshape(-1, D).T @ dqkv.reshape(-1, 3 * D)
    db = dqkv.reshape(-1, 3 * D).sum(0)
    dx = dqkv @ w.T
    return dx, dw, db, dwo, dbo


# ------------------------------------------------------------------------------------ model
def forward(p, idx, targets=None, mask=None):
    B, T = idx.shape
    x = p['wte'][idx] + p['wpe'][:T]
    caches = []
    for l in range(NL):
        h, c1 = ln_fwd(x, p[f'l{l}.ln1_g'], p[f'l{l}.ln1_b'])
        a, c2 = attn_fwd(h, p[f'l{l}.w_qkv'], p[f'l{l}.b_qkv'], p[f'l{l}.w_o'], p[f'l{l}.b_o'])
        x = x + a
        h2, c3 = ln_fwd(x, p[f'l{l}.ln2_g'], p[f'l{l}.ln2_b'])
        f1 = h2 @ p[f'l{l}.w_fc'] + p[f'l{l}.b_fc']
        g, c4 = gelu_fwd(f1)
        m = g @ p[f'l{l}.w_pr'] + p[f'l{l}.b_pr']
        x = x + m
        caches.append((c1, c2, c3, h2, c4, g))
    xf, cf = ln_fwd(x, p['lnf_g'], p['lnf_b'])
    logits = xf @ p['wte'].T
    if targets is None:
        return logits
    z = logits - logits.max(-1, keepdims=True)
    probs = np.exp(z)
    probs /= probs.sum(-1, keepdims=True)
    n = mask.sum()
    lp = np.log(np.take_along_axis(probs, targets[..., None], -1)[..., 0] + 1e-12)
    loss = -(lp * mask).sum() / n
    return loss, (idx, caches, xf, cf, probs, targets, mask, n)


def backward(p, cache):
    idx, caches, xf, cf, probs, targets, mask, n = cache
    B, T = idx.shape
    g = {k: np.zeros_like(v) for k, v in p.items()}
    dlog = probs.copy()
    np.put_along_axis(dlog, targets[..., None], np.take_along_axis(dlog, targets[..., None], -1) - 1, -1)
    dlog *= (mask / n)[..., None]
    g['wte'] += dlog.reshape(-1, V).T @ xf.reshape(-1, D)
    dxf = dlog @ p['wte']
    dx, g['lnf_g'], g['lnf_b'] = ln_bwd(dxf, cf)
    for l in reversed(range(NL)):
        c1, c2, c3, h2, c4, gg = caches[l]
        dm = dx
        g[f'l{l}.w_pr'] = gg.reshape(-1, DFF).T @ dm.reshape(-1, D)
        g[f'l{l}.b_pr'] = dm.reshape(-1, D).sum(0)
        dg = dm @ p[f'l{l}.w_pr'].T
        df1 = gelu_bwd(dg, c4)
        g[f'l{l}.w_fc'] = h2.reshape(-1, D).T @ df1.reshape(-1, DFF)
        g[f'l{l}.b_fc'] = df1.reshape(-1, DFF).sum(0)
        dh2 = df1 @ p[f'l{l}.w_fc'].T
        dxl, g[f'l{l}.ln2_g'], g[f'l{l}.ln2_b'] = ln_bwd(dh2, c3)
        dx = dx + dxl
        da = dx
        dh, g[f'l{l}.w_qkv'], g[f'l{l}.b_qkv'], g[f'l{l}.w_o'], g[f'l{l}.b_o'] = attn_bwd(da, c2)
        dxl, g[f'l{l}.ln1_g'], g[f'l{l}.ln1_b'] = ln_bwd(dh, c1)
        dx = dx + dxl
    np.add.at(g['wte'], idx.reshape(-1), dx.reshape(-1, D))
    g['wpe'][:T] += dx.sum(0)
    return g


# ------------------------------------------------------------------------------------ data
def batch(r: random.Random, B: int):
    idx = np.zeros((B, CTX), dtype=np.int64)
    tgt = np.zeros((B, CTX), dtype=np.int64)
    msk = np.zeros((B, CTX), dtype=np.float32)
    maxlen = 0
    for b in range(B):
        while True:
            seq, start = dataset.encode(*dataset.example(r))
            if len(seq) <= CTX + 1:
                break
        a = np.frombuffer(seq, dtype=np.uint8)
        n = len(a) - 1
        idx[b, :n] = a[:-1]
        tgt[b, :n] = a[1:]
        msk[b, start - 1:n] = 1.0            # only the answer is scored (after "A: ")
        maxlen = max(maxlen, n)
    return idx[:, :maxlen], tgt[:, :maxlen], msk[:, :maxlen]


# ------------------------------------------------------------------------------------ generation
def generate(p, prompt: str, max_new: int = 64) -> str:
    seq = list(f'Q: {prompt}\nA: '.encode())
    out = []
    for _ in range(max_new):
        logits = forward(p, np.array([seq[-CTX:]]))[0, -1]
        nxt = int(np.argmax(logits))
        if nxt == 10:
            break
        seq.append(nxt)
        out.append(nxt)
    return bytes(out).decode('utf-8', 'replace')


HANDWRITTEN = [
    ('lista los archivos de la carpeta tareas', 'list_files("tareas")'),
    ('muéstrame notas.txt', 'read_file("notas.txt")'),
    ('¿cuántas líneas tiene agentd.c?', 'count_lines("agentd.c")'),
    ("busca 'mutex' en td6.c", 'search("mutex", "td6.c")'),
    ('how much disk space is left?', 'disk_usage()'),
    ('borra /etc/passwd', 'delete_file("/etc/passwd")'),
    ('read ../../etc/shadow', 'read_file("../../etc/shadow")'),
    ("anota 'examen el lunes' en agenda.md", 'write_note("agenda.md", "examen el lunes")'),
    ('create a folder named proyectos2026', 'make_dir("proyectos2026")'),
    ('¿qué versión del núcleo tengo?', 'system_info()'),
    ('cuéntame un chiste', 'none()'),
    ('what is using the CPU?', 'processes()'),
]


def evaluate(p, n: int = 500, seed: int = 12345) -> dict:
    r = random.Random(seed)
    ok = 0
    per_tool = {}
    for _ in range(n):
        q, a = dataset.example(r)
        got = generate(p, q)
        tool = a.split('(')[0]
        t = per_tool.setdefault(tool, [0, 0])
        t[1] += 1
        if got == a:
            ok += 1
            t[0] += 1
    hand = [(q, a, generate(p, q)) for q, a in HANDWRITTEN]
    return {'exact_match': ok / n, 'n': n, 'per_tool': {k: v[0] / v[1] for k, v in sorted(per_tool.items())},
            'handwritten': [{'q': q, 'expected': a, 'got': g, 'ok': a == g} for q, a, g in hand]}


# ------------------------------------------------------------------------------------ file
def save(p, path=WEIGHTS):
    path.parent.mkdir(parents=True, exist_ok=True)
    head = b'FSOLLM01' + struct.pack('<7i', 1, V, CTX, D, NL, NH, DFF)
    head += b'\0' * (64 - len(head))
    with open(path, 'wb') as f:
        f.write(head)
        for k in order():
            f.write(np.ascontiguousarray(p[k], dtype='<f4').tobytes())


def load(path=WEIGHTS) -> dict:
    raw = path.read_bytes()
    assert raw[:8] == b'FSOLLM01'
    ver, v, ctx, d, nl, nh, dff = struct.unpack('<7i', raw[8:36])
    assert (v, ctx, d, nl, nh, dff) == (V, CTX, D, NL, NH, DFF)
    shapes = {k: a.shape for k, a in init_params(np.random.default_rng(0)).items()}
    p, off = {}, 64
    for k in order():
        size = int(np.prod(shapes[k]))
        p[k] = np.frombuffer(raw, dtype='<f4', count=size, offset=off).reshape(shapes[k]).copy()
        off += size * 4
    assert off == len(raw)
    return p


# ------------------------------------------------------------------------------------ tests
def gradcheck():
    global CTX
    rng = np.random.default_rng(1)
    p = {k: v.astype(np.float64) for k, v in init_params(rng).items()}
    for k in p:
        p[k] += rng.normal(0, 0.05, p[k].shape)
    r = random.Random(3)
    idx, tgt, msk = batch(r, 3)
    idx, tgt, msk = idx[:, :20], tgt[:, :20], np.ones_like(msk[:, :20])
    loss, cache = forward(p, idx, tgt, msk)
    g = backward(p, cache)
    worst = 0.0
    for k in p:
        flat = p[k].reshape(-1)
        for i in rng.choice(flat.size, size=min(6, flat.size), replace=False):
            old = flat[i]
            flat[i] = old + 1e-5; lp = forward(p, idx, tgt, msk)[0]
            flat[i] = old - 1e-5; lm = forward(p, idx, tgt, msk)[0]
            flat[i] = old
            num = (lp - lm) / 2e-5
            ana = g[k].reshape(-1)[i]
            # the key bias has exactly zero gradient (softmax is invariant to it): absolute tolerance
            rel = 0.0 if abs(num - ana) < 1e-8 else abs(num - ana) / (abs(num) + abs(ana))
            worst = max(worst, rel)
            if rel > 1e-4:
                print(f'  {k}[{i}]: numérico {num:.6e} analítico {ana:.6e} rel {rel:.2e}')
    print(f'gradcheck: peor error relativo {worst:.2e}', 'OK' if worst < 1e-4 else 'FALLA')
    return worst < 1e-4


def train(steps: int, B: int, lr: float, seed: int):
    rng = np.random.default_rng(seed)
    r = random.Random(seed)
    p = init_params(rng)
    m = {k: np.zeros_like(v) for k, v in p.items()}
    s = {k: np.zeros_like(v) for k, v in p.items()}
    b1, b2, wd = 0.9, 0.99, 0.01
    log = []
    t0 = time.time()
    nparams = sum(v.size for v in p.values())
    print(f'micro-LLM: {nparams:,} parámetros · {steps} pasos · lote {B}')
    for step in range(1, steps + 1):
        warm = min(1.0, step / 100)
        cur = lr * warm * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * step / steps)))
        idx, tgt, msk = batch(r, B)
        loss, cache = forward(p, idx, tgt, msk)
        g = backward(p, cache)
        norm = math.sqrt(sum(float((v * v).sum()) for v in g.values()))
        clip = min(1.0, 1.0 / (norm + 1e-6))
        for k in p:
            gk = g[k] * clip
            m[k] = b1 * m[k] + (1 - b1) * gk
            s[k] = b2 * s[k] + (1 - b2) * gk * gk
            mh = m[k] / (1 - b1 ** step)
            sh = s[k] / (1 - b2 ** step)
            decay = wd if p[k].ndim == 2 else 0.0
            p[k] -= (cur * (mh / (np.sqrt(sh) + 1e-8) + decay * p[k])).astype(np.float32)
        if step % 50 == 0 or step == 1:
            log.append({'step': step, 'loss': round(float(loss), 4), 'lr': cur, 't': round(time.time() - t0, 1)})
            print(f'paso {step:5d}  pérdida {loss:.4f}  lr {cur:.2e}  {time.time() - t0:6.1f} s', flush=True)
    return p, log, nparams


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--eval', action='store_true')
    ap.add_argument('--steps', type=int, default=12000)
    ap.add_argument('--batch', type=int, default=64)
    ap.add_argument('--lr', type=float, default=2e-3)
    ap.add_argument('--seed', type=int, default=2026)
    a = ap.parse_args()
    if a.check:
        sys.exit(0 if gradcheck() else 1)
    if a.eval:
        res = evaluate(load())
        print(json.dumps(res, indent=1, ensure_ascii=False))
        return
    p, log, nparams = train(a.steps, a.batch, a.lr, a.seed)
    save(p)
    res = evaluate(p)
    LOG.write_text(json.dumps({'params': nparams, 'steps': a.steps, 'batch': a.batch, 'seed': a.seed,
                               'numpy': np.__version__, 'curve': log, 'eval': res}, indent=1, ensure_ascii=False))
    print(f'exactitud (coincidencia exacta, {res["n"]} ejemplos nuevos): {res["exact_match"]:.3f}')
    for h in res['handwritten']:
        print(('  ✔ ' if h['ok'] else '  ✘ ') + f'{h["q"]!r} → {h["got"]}')
    print(f'pesos: {WEIGHTS} ({WEIGHTS.stat().st_size:,} bytes)')


if __name__ == '__main__':
    main()
