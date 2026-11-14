/* fast.inc - the G400's fast path (emit.c): each vertex fetched,
 * transformed, given its outcode, projected and formed for the WARPs once,
 * and its triangles or fans written straight into the DMA stream. emit.c
 * includes it twice (fe.md F3): FAST(n) names one copy's functions and
 * FAST_ATTR compiles it, for the Pentium and for the Pentium II, whose FCOMI
 * and CMOV replace FCOM, FNSTSW, SAHF and branches in the twenty or so
 * comparisons a vertex makes. The arithmetic is the same: REGCRC compares
 * the two. The data both use is emit.c's. */

/* byte_of (emit.c), here so that each copy has its own. */
static FAST_ATTR uint32_t FAST(byte_of)(float f)
{
    return f <= 0 ? 0 : f >= 255.0f ? 255 : (uint32_t)mga_ifloor((double)f + 0.5);
}

static FAST_ATTR const fast_vtx *FAST(fast_get)(GLint i)
{
    fast_vtx *f = &fv[(unsigned)i & (FAST_SLOTS - 1)];
    const float *p, *m = fm;
    volatile float x, y, z, w;          /* stored as floats, as dgl_transform's: the general path's values exactly */
    float p3;
    double iw, xw, yw, zw;
    int prof_o;
    if (f->gen == fgen && f->key == i)
        return f;
    prof_o = DGL_FINE_SWITCH(PROF_D_FXF);
    DGL_PCOUNT(fverts);
    f->key = i;
    f->gen = fgen;
    p = (const float *)(dgl_pl_v.p + (size_t)i * dgl_pl_v.stride);
    p3 = dgl_pl_v.size > 3 ? p[3] : 1.0f;
    x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * p3;
    y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * p3;
    z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * p3;
    w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] * p3;
    f->oc = (z + w < 0) | ((w - z < 0) << 1) | ((x + guard_x * w < 0) << 2) | ((guard_x * w - x < 0) << 3) |
            ((y + guard_y * w < 0) << 4) | ((guard_y * w - y < 0) << 5);
    if (dgl_pl_c.on && dgl_pl_c.type == GL_FLOAT) {
        /* project()'s products, stored as floats, then warp_form's bytes */
        const float *c = (const float *)(dgl_pl_c.p + (size_t)i * dgl_pl_c.stride);
        volatile float r = c[0] * 255.0f, g = c[1] * 255.0f, b = c[2] * 255.0f,
                       a = (dgl_pl_c.size > 3 ? c[3] : 1.0f) * 255.0f;
        f->w.bgra = FAST(byte_of)(b) | (FAST(byte_of)(g) << 8) | (FAST(byte_of)(r) << 16) | (FAST(byte_of)(a) << 24);
    } else if (dgl_pl_c.on) {
        const unsigned char *c = dgl_pl_c.p + (size_t)i * dgl_pl_c.stride;
        f->w.bgra = (uint32_t)c[2] | ((uint32_t)c[1] << 8) | ((uint32_t)c[0] << 16) |
                    ((uint32_t)(dgl_pl_c.size > 3 ? c[3] : 255) << 24);
    } else
        f->w.bgra = fbgra;
    if (f->oc) {
        DGL_FINE_BACK(prof_o);
        return f;
    }
    DGL_FINE_SWITCH(PROF_D_FPRJ);
    iw = 1.0 / w;
    xw = dgl_gl.viewport[0] + (x * iw + 1.0) * 0.5 * dgl_gl.viewport[2];
    yw = dgl_ctx.height - (dgl_gl.viewport[1] + (y * iw + 1.0) * 0.5 * dgl_gl.viewport[3]);
    zw = dgl_gl.depth_near + (z * iw + 1.0) * 0.5 * (dgl_gl.depth_far - dgl_gl.depth_near);
    f->X16 = mga_irint_nearest(xw * 16.0);
    f->Y16 = mga_irint_nearest(yw * 16.0);
    f->w.x = (float)xw + WARP_SUBPIXEL_X;
    f->w.y = (float)yw + WARP_SUBPIXEL_Y;
    f->w.z = (float)(zw < 0 ? 0 : zw > 1 ? 1 : zw);
    f->w.rhw = (float)iw;
    if (dgl_gl.fog) {                   /* project()'s factor from dgl_transform's eye distance */
        const float *mv = fmv;
        float d = -(mv[2] * p[0] + mv[6] * p[1] + mv[10] * p[2] + mv[14] * p3);
        f->w.spec = FAST(byte_of)(255.0f * fog_factor(d)) << 24;
    } else
        f->w.spec = 0xFF000000u;        /* (no fog: its factor 255) */
    if (textured == 2) {                /* the white texel of untextured fogged draws */
        f->w.u0 = f->w.v0 = 0;
    } else if (dgl_pl_t.on) {
        const float *t = (const float *)(dgl_pl_t.p + (size_t)i * dgl_pl_t.stride);
        f->w.u0 = t[0] * tex_scale_s;
        f->w.v0 = t[1] * tex_scale_t;
    } else {
        f->w.u0 = fs0;
        f->w.v0 = ft0;
    }
    if (textured == 1) {                /* wrap_dw's floor, ahead: the smallest of three is the floor of the smallest */
        f->fu = f->w.u0 > -1e9f && f->w.u0 < 1e9f ? mga_ifloor(f->w.u0) : NO_FLOOR;
        f->fv = f->w.v0 > -1e9f && f->w.v0 < 1e9f ? mga_ifloor(f->w.v0) : NO_FLOOR;
    }
    if (dual) {                         /* warp_form's map 1, from unit 1 (not map1_copy: emit_fast) */
        if (dgl_pl_t1.on) {
            const float *t = (const float *)(dgl_pl_t1.p + (size_t)i * dgl_pl_t1.stride);
            f->w.u1 = t[0] * tex_scale_s1;
            f->w.v1 = t[1] * tex_scale_t1;
        } else {
            f->w.u1 = fs1;
            f->w.v1 = ft1;
        }
        f->fu1 = f->w.u1 > -1e9f && f->w.u1 < 1e9f ? mga_ifloor(f->w.u1) : NO_FLOOR;
        f->fv1 = f->w.v1 > -1e9f && f->w.v1 < 1e9f ? mga_ifloor(f->w.v1) : NO_FLOOR;
    } else
        f->w.u1 = f->w.v1 = 0;
    DGL_FINE_BACK(prof_o);
    return f;
}

static FAST_ATTR int32_t FAST(min3i)(int32_t a, int32_t b, int32_t c)
{
    int32_t m = a < b ? a : b;
    return c < m ? c : m;
}

/* A WARP vertex into the stream, its words in order (a write-combining ring
 * takes whole lines best), with the colour given and its coordinates moved
 * by whole periods (wrap_dw's result). */
static FAST_ATTR void FAST(put8)(uint32_t *d, const mga_wvtx *w, uint32_t bgra, int32_t su, int32_t sv)
{
    float u = w->u0, v = w->v0;
    if (su)
        u = w->u0 - (float)su;
    if (sv)
        v = w->v0 - (float)sv;
    memcpy(d, w, 16);                   /* x, y, z, rhw */
    d[4] = bgra;
    d[5] = w->spec;
    memcpy(d + 6, &u, 4);
    memcpy(d + 7, &v, 4);
}

/* The same for the T2 pipes: map 1's coordinates after, moved by their own
 * whole periods. */
static FAST_ATTR void FAST(put10)(uint32_t *d, const mga_wvtx *w, uint32_t bgra, int32_t su, int32_t sv, int32_t su1, int32_t sv1)
{
    float u = w->u1, v = w->v1;
    FAST(put8)(d, w, bgra, su, sv);
    if (su1)
        u = w->u1 - (float)su1;
    if (sv1)
        v = w->v1 - (float)sv1;
    memcpy(d + 8, &u, 4);
    memcpy(d + 9, &v, 4);
}

/* warp_wrap on three vertices already in the DMA stream: f[idx] of each
 * (dw floats apart) shifted by whole periods so the smallest is in [0, 1). */
static FAST_ATTR void FAST(wrap_dw)(float *f, int dw, int idx)
{
    float mn = f[idx];
    if (f[idx + dw] < mn) mn = f[idx + dw];
    if (f[idx + 2 * dw] < mn) mn = f[idx + 2 * dw];
    if (!(mn > -1e9f && mn < 1e9f))
        return;
    mn = (float)mga_ifloor(mn);
    if (mn != 0) {
        f[idx] -= mn;
        f[idx + dw] -= mn;
        f[idx + 2 * dw] -= mn;
    }
}

static FAST_ATTR void FAST(fast_tri)(GLint ia, GLint ib, GLint ic, GLint ip)
{
    const uint32_t flat = dgl_gl.shade_model == GL_FLAT ? FAST(fast_get)(ip)->w.bgra : 0;    /* (first: its slot may be a's) */
    const fast_vtx *a = FAST(fast_get)(ia), *b = FAST(fast_get)(ib), *c = FAST(fast_get)(ic);
    mga_wvtx w[3];
    int64_t area2;
    DGL_FINE_SCOPE(PROF_D_FTRI);
    if ((a->oc | b->oc | c->oc) || !warp_now) {
        dgl_tri_general(ia, ib, ic, ip);        /* (counts itself) */
        return;
    }
    dgl_prims.tris_in++;
    area2 = (int64_t)(b->X16 - a->X16) * (c->Y16 - a->Y16) - (int64_t)(c->X16 - a->X16) * (b->Y16 - a->Y16);
    if (area2 == 0) {
        dgl_prims.zero_area++;
        return;
    }
    if (dgl_gl.cull_face) {
        int front = (dgl_gl.front_face == GL_CCW) ? area2 < 0 : area2 > 0;
        if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front) {
            dgl_prims.culled++;
            return;
        }
    }
    if (dgl_gl.fog) {
        /* fog_split's test: a large triangle over which the factor varies
         * much goes the general way, to be subdivided (area2 is in 1/256
         * square pixels, doubled). */
        uint32_t fa = a->w.spec >> 24, fb = b->w.spec >> 24, fc = c->w.spec >> 24;
        uint32_t lo = fa < fb ? fa : fb, hi = fa > fb ? fa : fb;
        if (fc < lo) lo = fc;
        if (fc > hi) hi = fc;
        if (hi - lo > FOG_SPLIT && (area2 < 0 ? -area2 : area2) >= 32 * 512) {
            dgl_prims.tris_in--;
            dgl_tri_general(ia, ib, ic, ip);
            return;
        }
    }
    {
        int dw;
        uint32_t *d;
        DGL_FINE_SWITCH(PROF_D_RING);           /* (the scope's end puts the caller's stage back) */
        d = warp_reserve(3, &dw);
        if (d) {                                /* straight into the DMA stream */
            int32_t su = 0, sv = 0, su1 = 0, sv1 = 0;
            int whole = dw == (dual ? 10 : 8);
            if (textured == 1 && whole) {
                if (!tstate.clamp_u) {
                    su = FAST(min3i)(a->fu, b->fu, c->fu);
                    whole = whole && a->fu != NO_FLOOR && b->fu != NO_FLOOR && c->fu != NO_FLOOR;
                }
                if (!tstate.clamp_v) {
                    sv = FAST(min3i)(a->fv, b->fv, c->fv);
                    whole = whole && a->fv != NO_FLOOR && b->fv != NO_FLOOR && c->fv != NO_FLOOR;
                }
            }
            if (dual && whole) {
                if (!tstate1.clamp_u) {
                    su1 = FAST(min3i)(a->fu1, b->fu1, c->fu1);
                    whole = whole && a->fu1 != NO_FLOOR && b->fu1 != NO_FLOOR && c->fu1 != NO_FLOOR;
                }
                if (!tstate1.clamp_v) {
                    sv1 = FAST(min3i)(a->fv1, b->fv1, c->fv1);
                    whole = whole && a->fv1 != NO_FLOOR && b->fv1 != NO_FLOOR && c->fv1 != NO_FLOOR;
                }
            }
            if (whole) {                        /* each word written once, in order */
                int fl = dgl_gl.shade_model == GL_FLAT;
                if (dual) {
                    FAST(put10)(d, &a->w, fl ? flat : a->w.bgra, su, sv, su1, sv1);
                    FAST(put10)(d + 10, &b->w, fl ? flat : b->w.bgra, su, sv, su1, sv1);
                    FAST(put10)(d + 20, &c->w, fl ? flat : c->w.bgra, su, sv, su1, sv1);
                } else {
                    FAST(put8)(d, &a->w, fl ? flat : a->w.bgra, su, sv);
                    FAST(put8)(d + 8, &b->w, fl ? flat : b->w.bgra, su, sv);
                    FAST(put8)(d + 16, &c->w, fl ? flat : c->w.bgra, su, sv);
                }
                warp_commit(3);
                warp_since_cpu = 1;
                return;
            }
            memcpy(d, &a->w, (size_t)dw * 4u);
            memcpy(d + dw, &b->w, (size_t)dw * 4u);
            memcpy(d + 2 * dw, &c->w, (size_t)dw * 4u);
            if (dgl_gl.shade_model == GL_FLAT)
                d[4] = d[4 + dw] = d[4 + 2 * dw] = flat;
            if (textured == 1) {
                if (!tstate.clamp_u)
                    FAST(wrap_dw)((float *)d, dw, 6);
                if (!tstate.clamp_v)
                    FAST(wrap_dw)((float *)d, dw, 7);
            }
            if (dual && dw == 10) {
                if (!tstate1.clamp_u)
                    FAST(wrap_dw)((float *)d, dw, 8);
                if (!tstate1.clamp_v)
                    FAST(wrap_dw)((float *)d, dw, 9);
            }
            warp_commit(3);
            warp_since_cpu = 1;
            return;
        }
    }
    w[0] = a->w; w[1] = b->w; w[2] = c->w;
    if (dgl_gl.shade_model == GL_FLAT)
        w[0].bgra = w[1].bgra = w[2].bgra = flat;
    if (textured)
        warp_wrap(w, 0, &tstate);
    if (dual)
        warp_wrap(w, 1, &tstate1);
    if (warp_vertices(w, 3) == 0) {
        warp_since_cpu = 1;
        return;
    }
    warp_on = warp_now = 0;                     /* the FIFO stopped draining: the CPU from now on */
    DGL_WARN("DGL-WARN the WARP stopped taking vertices: triangles are set up by the CPU");
    dgl_gl.dirty |= DGL_DIRTY_REGS;
    dgl_prims.tris_in--;
    dgl_tri_general(ia, ib, ic, ip);
}

/* A fan (GL_TRIANGLE_FAN, GL_POLYGON) or strip (GL_TRIANGLE_STRIP) as one
 * sequence of the WARPs' (warp_reserve_seq): each vertex once in the DMA
 * stream, not three times a triangle. Only when it draws the triangles
 * fast_tri would: every vertex inside the guard band, none of the triangles
 * zero in area or culled, no fog, smooth shading (or GL_POLYGON's one flat
 * colour). The texture coordinates move by the sequence's whole periods,
 * not each triangle's. 1 when sent; 0: the caller sends triangles. */
static FAST_ATTR int FAST(fast_seq)(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    /* The vertices' slots; with indices, copies (a slot may be refilled
     * within the sequence). Consecutive vertices (count <= FAST_SLOTS) never
     * share a slot. */
    static const fast_vtx *V[SEQ_MAX];
    static fast_vtx cp[SEQ_MAX];
    const int fan = mode != GL_TRIANGLE_STRIP, flat = dgl_gl.shade_model == GL_FLAT;
    int32_t su = 0, sv = 0, su1 = 0, sv1 = 0;
    uint32_t *d;
    GLsizei i;
    int dw;
    if (count < 5 || count > SEQ_MAX || dgl_gl.fog || (flat && (mode != GL_POLYGON || !(seq_kinds & 4))) ||
        !(seq_kinds & (fan ? 1 : 2)))
        return 0;

    for (i = 0; i < count; i++) {
        const fast_vtx *f = FAST(fast_get)(idx ? idx[i] : first + i);
        if (f->oc)
            return 0;
        if (idx) {
            cp[i] = *f;
            f = &cp[i];
        }
        V[i] = f;
    }
    {
    DGL_FINE_SCOPE(PROF_D_FTRI);
    for (i = 0; i + 2 < count; i++) {   /* fast_tri's triangles: none zero in area, none culled */
        const int a = fan ? 0 : (i & 1) ? i + 1 : i, b = fan ? i + 1 : (i & 1) ? i : i + 1, c = i + 2;
        const int64_t area2 = (int64_t)(V[b]->X16 - V[a]->X16) * (V[c]->Y16 - V[a]->Y16) - (int64_t)(V[c]->X16 - V[a]->X16) * (V[b]->Y16 - V[a]->Y16);
        if (area2 == 0)
            return 0;
        if (dgl_gl.cull_face) {
            int front = (dgl_gl.front_face == GL_CCW) ? area2 < 0 : area2 > 0;
            if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front)
                return 0;
        }
    }
    if (textured == 1) {
        if (!tstate.clamp_u)
            for (su = V[0]->fu, i = 0; i < count; i++) {
                if (V[i]->fu == NO_FLOOR)
                    return 0;
                if (V[i]->fu < su) su = V[i]->fu;
            }
        if (!tstate.clamp_v)
            for (sv = V[0]->fv, i = 0; i < count; i++) {
                if (V[i]->fv == NO_FLOOR)
                    return 0;
                if (V[i]->fv < sv) sv = V[i]->fv;
            }
    }
    if (dual) {
        if (!tstate1.clamp_u)
            for (su1 = V[0]->fu1, i = 0; i < count; i++) {
                if (V[i]->fu1 == NO_FLOOR)
                    return 0;
                if (V[i]->fu1 < su1) su1 = V[i]->fu1;
            }
        if (!tstate1.clamp_v)
            for (sv1 = V[0]->fv1, i = 0; i < count; i++) {
                if (V[i]->fv1 == NO_FLOOR)
                    return 0;
                if (V[i]->fv1 < sv1) sv1 = V[i]->fv1;
            }
    }
    DGL_FINE_SWITCH(PROF_D_RING);
    d = warp_reserve_seq(fan, (int)count, &dw);
    if (!d)
        return 0;
    if (dw == 10)
        for (i = 0; i < count; i++)
            FAST(put10)(d + 10 * i, &V[i]->w, flat ? V[0]->w.bgra : V[i]->w.bgra, su, sv, su1, sv1);
    else
        for (i = 0; i < count; i++)
            FAST(put8)(d + 8 * i, &V[i]->w, flat ? V[0]->w.bgra : V[i]->w.bgra, su, sv);
    warp_commit_seq((int)count);
    warp_since_cpu = 1;
    dgl_prims.tris_in += (unsigned)(count - 2);
    dgl_prims.seqs++;
    return 1;
    }
}

static FAST_ATTR int FAST(emit_fast)(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    GLsizei i;
    if (!warp_now || !dgl_gl.tex_identity || map0_unit ||
        (dual && (map1_copy || !dgl_gl.tex1_identity ||
                  (dgl_pl_t1.on && (dgl_pl_t1.type != GL_FLOAT || dgl_pl_t1.size < 2)))) ||
        (dgl_gl.offset_fill && (dgl_gl.offset_factor != 0.0f || dgl_gl.offset_units != 0.0f)) ||
        dgl_pl_v.type != GL_FLOAT || dgl_pl_v.size < 3 ||
        (dgl_pl_c.on && dgl_pl_c.type != GL_UNSIGNED_BYTE && dgl_pl_c.type != GL_FLOAT) ||
        (dgl_pl_c.on && dgl_pl_c.size < 3) ||
        (dgl_pl_t.on && (dgl_pl_t.type != GL_FLOAT || dgl_pl_t.size < 2)))
        return 0;
    switch (mode) {
    case GL_TRIANGLES: case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN:
    case GL_QUADS: case GL_QUAD_STRIP: case GL_POLYGON:
        break;
    default:
        return 0;
    }
    DGL_FINE_SCOPE(PROF_D_BATCH);
    if (++fgen == 0) {                          /* (a wrapped generation: every slot stale) */
        memset(fv, 0, sizeof fv);
        fgen = 1;
    }
    fm = dgl_mvp()->m;
    fmv = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m;
    /* The current colour and coordinates stand in for an array that is off
     * (fast_get); immediate mode's vertices carry their own (plan_imm). */
    if (!dgl_pl_c.on)
        fbgra = FAST(byte_of)(dgl_gl.cur_color[2] * 255.0f) | (FAST(byte_of)(dgl_gl.cur_color[1] * 255.0f) << 8) |
                (FAST(byte_of)(dgl_gl.cur_color[0] * 255.0f) << 16) | (FAST(byte_of)(dgl_gl.cur_color[3] * 255.0f) << 24);
    if (!dgl_pl_t.on) {
        fs0 = dgl_gl.cur_tex[0] * tex_scale_s;
        ft0 = dgl_gl.cur_tex[1] * tex_scale_t;
    }
    if (!dgl_pl_t1.on) {
        fs1 = dgl_gl.cur_tex1[0] * tex_scale_s1;
        ft1 = dgl_gl.cur_tex1[1] * tex_scale_t1;
    }
#define I(k) (idx ? idx[k] : first + (k))
    switch (mode) {
    case GL_TRIANGLES:
        for (i = 0; i + 2 < count; i += 3)
            FAST(fast_tri)(I(i), I(i + 1), I(i + 2), I(i + 2));
        break;
    case GL_TRIANGLE_STRIP:
        if (FAST(fast_seq)(mode, count, idx, first))
            break;
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) FAST(fast_tri)(I(i + 1), I(i), I(i + 2), I(i + 2));
            else       FAST(fast_tri)(I(i), I(i + 1), I(i + 2), I(i + 2));
        }
        break;
    case GL_TRIANGLE_FAN:
        if (FAST(fast_seq)(mode, count, idx, first))
            break;
        for (i = 1; i + 1 < count; i++)
            FAST(fast_tri)(I(0), I(i), I(i + 1), I(i + 1));
        break;
    case GL_QUADS:
        for (i = 0; i + 3 < count; i += 4) {
            FAST(fast_tri)(I(i), I(i + 1), I(i + 2), I(i + 3));
            FAST(fast_tri)(I(i), I(i + 2), I(i + 3), I(i + 3));
        }
        break;
    case GL_QUAD_STRIP:
        for (i = 0; i + 3 < count; i += 2) {
            FAST(fast_tri)(I(i), I(i + 1), I(i + 3), I(i + 3));
            FAST(fast_tri)(I(i), I(i + 3), I(i + 2), I(i + 3));
        }
        break;
    case GL_POLYGON:
        if (FAST(fast_seq)(mode, count, idx, first))
            break;
        for (i = 1; i + 1 < count; i++)
            FAST(fast_tri)(I(0), I(i), I(i + 1), I(0));
        break;
    }
#undef I
    return 1;
}
