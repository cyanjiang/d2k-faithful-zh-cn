// The route search (FUN_0045eaf0 "FindPath") and its smoothing
// (FUN_0045fcc0), ported from the decompile. RE: docs/re/09-unit-movement.md
// section 9. Not A*: a Bresenham line, and around each obstacle a
// left-hand and a right-hand wall-follow, the shorter kept.
#include "sim/world.hpp"

#include <cstdlib>

namespace d2k::sim {

namespace {
using Pt = std::array<int, 2>;

int cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }
bool near(int ax, int ay, int bx, int by) { return std::abs(ax - bx) < 2 && std::abs(ay - by) < 2; }

// FUN_0045deb0: +4 (clockwise) when `cw`, else -4, in 0..31.
int rotate(int d, bool cw) { return (d + (cw ? 0x24 : 0x1c)) & 0x1f; }

// FUN_0045de30: one cell in an 8-way facing (0 N, 4 NE, ... 28 NW).
Pt step_dir(int x, int y, int d) {
    if (d == 0 || d == 4 || d == 0x1c) --y;
    if (d == 4 || d == 8 || d == 0xc) ++x;
    if (d == 0xc || d == 0x10 || d == 0x14) ++y;
    if (d == 0x14 || d == 0x18 || d == 0x1c) --x;
    return {x, y};
}

// FUN_0045e0e0 "GetBestAngle": the 8-way facing from (x1, y1) to (x2, y2).
int best_angle(int x1, int y1, int x2, int y2) {
    const int dx = x2 - x1, dy = y1 - y2; // dy: up is positive
    if (dx >= 0 && dy >= 0) return dy <= dx ? (dx != dy ? 8 : 4) : 0;
    if (dx >= 0) return -dy <= dx ? (dx != -dy ? 8 : 12) : 0x10; // dy < 0
    if (dy <= 0) return -dy <= -dx ? (-dx != -dy ? 0x18 : 0x14) : 0x10;
    return dy <= -dx ? (-dx != dy ? 0x18 : 0x1c) : 0;
}

// FUN_0045fd40 / FUN_0045fe20: the Bresenham walk (sign(0) is +1).
struct Line {
    int e1 = 0, e2 = 0, ax = 0, ay = 0, bx = 0, by = 0, err = 0;
    Line(int x0, int y0, int x1, int y1) {
        const int dx = x1 - x0, dy = y1 - y0;
        const int adx = std::abs(dx), ady = std::abs(dy);
        const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        if (ady <= adx) {
            e1 = ady * 2;
            e2 = ady * 2 - adx * 2;
            err = e1 - adx;
            ay = sy; // the extra on a diagonal step
            bx = sx; // every step
        } else {
            e1 = adx * 2;
            e2 = adx * 2 - ady * 2;
            err = e1 - ady;
            ax = sx;
            by = sy;
        }
    }
    Pt next(int x, int y) {
        if (err >= 0) {
            err += e2;
            return {x + ax + bx, y + ay + by};
        }
        err += e1;
        return {x + bx, y + by};
    }
};

// FUN_0045e890: the buffer entry closest to (gx, gy) (Chebyshev, the first
// wins); `off` = 1 + the last index before it that equals (fx, fy). True
// when it is closer than (fx, fy) itself.
bool closest(const std::vector<Pt>& buf, int n, int fx, int fy, int gx, int gy, int& dist, int& off, int& idx) {
    const int start = cheb(fx, fy, gx, gy);
    dist = start;
    idx = 0;
    int last_start = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[size_t(i)][0] == fx && buf[size_t(i)][1] == fy) last_start = i + 1;
        const int d = cheb(buf[size_t(i)][0], buf[size_t(i)][1], gx, gy);
        if (d < dist) {
            off = last_start;
            dist = d;
            idx = i;
        }
    }
    return dist != start;
}
} // namespace

int World::find_path(const Unit& u, int mode, std::vector<std::array<int, 2>>& out) const {
    // FUN_0045eaf0. Returns 1 (reached), 3 or 4 (partial / none); `out` is
    // the returned run of the buffer.
    const int sx = u.cur_x, sy = u.cur_y, gx = u.dest_x, gy = u.dest_y;
    auto pass = [&](int x, int y) { return passable(x, y, u, mode); };
    constexpr int kMax = 1000;
    std::vector<Pt> A(kMax + 4), B(kMax + 4);
    int nA = 0, sA = 0, nB = 0, sB = 0;
    int code = 4;
    auto ret = [&](const std::vector<Pt>& buf, int off, int count) {
        out.assign(buf.begin() + off, buf.begin() + off + std::max(0, count));
        return code;
    };
    auto ret_start = [&]() { // A / B [0] = start, nothing to follow
        out.clear();
        return code;
    };

    // FUN_0045e960: the obstacle exits along the line (the first free cell
    // after each blocked run; none when the goal itself is blocked).
    std::vector<Pt> exits;
    {
        Line l(sx, sy, gx, gy);
        Pt p{sx, sy};
        for (;;) {
            p = l.next(p[0], p[1]);
            if (p[0] == gx && p[1] == gy) break;
            if (pass(p[0], p[1])) continue;
            bool done = false;
            for (;;) {
                p = l.next(p[0], p[1]);
                if (p[0] == gx && p[1] == gy) {
                    if (pass(p[0], p[1])) exits.push_back(p);
                    done = true;
                    break;
                }
                if (pass(p[0], p[1])) break;
            }
            if (done) break;
            exits.push_back(p);
        }
    }
    const int nX = int(exits.size());

    // FUN_0045e1a0: one wall-follow (cw = the A side) from (px, py) towards
    // the target; 2 reached, 3 boxed in, 4 failed.
    auto follow = [&](int px, int py, int tx, int ty, int dir, std::vector<Pt>& buf, int& n, int& s, bool b_side) {
        int x = px, y = py, near_start = 0;
        for (;;) {
            Pt c{};
            int tries = 0;
            do {
                dir = rotate(dir, !b_side);
                c = step_dir(x, y, dir);
                if (++tries > 8) return 3;
            } while (!pass(c[0], c[1]) && !(c[0] == sx && c[1] == sy));
            buf[size_t(n)] = c;
            ++n;
            if (n > kMax - 1) return 4;
            if (c[0] == sx && c[1] == sy) s = n;
            if (near(c[0], c[1], tx, ty)) {
                if (!pass(tx, ty)) return 4;
                buf[size_t(n)] = {tx, ty};
                ++n;
                return 2;
            }
            if (near(c[0], c[1], px, py) && ++near_start > 8) return 4;
            for (int k = 0; k < 3; ++k) dir = rotate(dir, b_side);
            x = c[0];
            y = c[1];
        }
    };

    int k_exit = 0;           // local_6c
    int cx = sx, cy = sy;     // where the line walk restarts
    Pt last{sx, sy};        // local_20: the last cell reached
    for (int guard = 0; guard < 4096; ++guard) {
        Line l(cx, cy, gx, gy);
        Pt prev{cx, cy}, p{};
        for (;;) {
            p = l.next(prev[0], prev[1]);
            if (!pass(p[0], p[1])) break;
            A[size_t(nA++)] = p;
            B[size_t(nB++)] = p;
            last = p;
            if (nA > kMax - 1) {
                int d78, i80, d74, i7c;
                if (nB < kMax) {
                    closest(B, nB, sx, sy, gx, gy, d74, sB, i7c);
                    return ret(B, sB, nB - sB);
                }
                const bool c4 = closest(A, nA, sx, sy, gx, gy, d78, sA, i80);
                const bool c3 = closest(B, nB, sx, sy, gx, gy, d74, sB, i7c);
                if (c4) {
                    if (!c3) return ret(A, sA, nA - sA);
                    if (d78 == d74 ? (i7c - sB) <= (i80 - sA) : d74 <= d78) return ret(B, sB, nB - sB);
                    return ret(A, sA, nA - sA);
                }
                if (!c3) return ret_start();
                return ret(B, sB, nB - sB);
            }
            if (nB > kMax - 1) {
                int d78, i80;
                closest(A, nA, sx, sy, gx, gy, d78, sA, i80);
                return ret(A, sA, nA - sA);
            }
            if (p[0] == gx && p[1] == gy) {
                code = 1;
                if (nB - sB < nA - sA) return ret(B, sB, nB - sB);
                return ret(A, sA, nA - sA);
            }
            prev = p;
        }
        int d78 = 0, i80 = 0, d74 = 0, i7c = 0;
        // Pick A or B by the closest-entry results (the shared tail of the
        // partial returns).
        auto partial = [&](bool c4, bool c3) {
            if (!c4) {
                if (c3) return ret(B, sB, i7c - sB + 1);
                return ret_start();
            }
            if (!c3) return ret(A, sA, i80 - sA + 1);
            if (d78 != d74 ? d74 <= d78 : (i7c - sB) <= (i80 - sA)) return ret(B, sB, i7c - sB + 1);
            return ret(A, sA, i80 - sA + 1);
        };
        if (p[0] == gx && p[1] == gy) { // the goal itself is blocked
            const bool c4 = closest(A, nA, sx, sy, gx, gy, d78, sA, i80);
            const bool c3 = closest(B, nB, sx, sy, gx, gy, d74, sB, i7c);
            return partial(c4, c3);
        }
        Pt target{gx, gy};
        if (k_exit + 1 <= nX) target = exits[size_t(k_exit)];
        ++k_exit;
        const int dir = best_angle(prev[0], prev[1], p[0], p[1]);
        const int saved_nA = nA, saved_sA = sA, saved_nB = nB, saved_sB = sB;

        // FUN_0045e3b0: both wall-follows in lockstep.
        int resA = 4, resB = 4;
        {
            int ax = prev[0], ay = prev[1], bx = prev[0], by = prev[1], da = dir, db = dir;
            int near_a = 0, near_b = 0;
            for (;;) {
                Pt ca{}, cb{};
                int tries = 0;
                do {
                    da = rotate(da, true);
                    ca = step_dir(ax, ay, da);
                    if (++tries > 8) { resA = resB = 3; goto followed; }
                } while (!pass(ca[0], ca[1]) && !(ca[0] == sx && ca[1] == sy));
                tries = 0;
                do {
                    db = rotate(db, false);
                    cb = step_dir(bx, by, db);
                    if (++tries > 8) { resA = resB = 3; goto followed; }
                } while (!pass(cb[0], cb[1]) && !(cb[0] == sx && cb[1] == sy));
                A[size_t(nA++)] = ca;
                if (nA > kMax - 1) { resA = resB = 4; goto followed; }
                B[size_t(nB++)] = cb;
                if (nB > kMax - 1) { resA = resB = 4; goto followed; }
                if (ca[0] == sx && ca[1] == sy) sA = nA;
                if (cb[0] == sx && cb[1] == sy) sB = nB;
                if (near(ca[0], ca[1], target[0], target[1])) {
                    if (pass(target[0], target[1])) {
                        A[size_t(nA++)] = target;
                        resA = 2;
                    }
                    goto followed; // resB stays 4
                }
                if (near(cb[0], cb[1], target[0], target[1])) {
                    if (pass(target[0], target[1])) {
                        B[size_t(nB++)] = target;
                        resB = 2;
                    }
                    goto followed;
                }
                if (near(ca[0], ca[1], prev[0], prev[1]) && ++near_a > 8) goto followed;
                if (near(cb[0], cb[1], prev[0], prev[1]) && ++near_b > 8) goto followed;
                for (int r = 0; r < 3; ++r) {
                    da = rotate(da, false);
                    db = rotate(db, true);
                }
                ax = ca[0];
                ay = ca[1];
                bx = cb[0];
                by = cb[1];
            }
        }
    followed:
        if (resA == 2 || resB == 2) last = target;
        if (resA == 4 && resB == 4) {
            // A later exit already on a fresh trace: follow again to it.
            bool found = false;
            for (int m = k_exit; m < nX && !found;) {
                const Pt x = exits[size_t(m)];
                for (int i = saved_nA; i < nA; ++i) {
                    if (A[size_t(i)] == x) {
                        nA = saved_nA;
                        sA = saved_sA;
                        target = x;
                        resA = follow(prev[0], prev[1], x[0], x[1], dir, A, nA, sA, false);
                        found = true;
                        last = x;
                    }
                }
                for (int i = saved_nB; i < nB; ++i) {
                    if (B[size_t(i)] == x) {
                        nB = saved_nB;
                        sB = saved_sB;
                        target = x;
                        resB = follow(prev[0], prev[1], x[0], x[1], dir, B, nB, sB, true);
                        found = true;
                        last = x;
                    }
                }
                if (found) k_exit = m + 1;
                else ++m;
            }
            if (!found) {
                int off = 0;
                bool c4 = closest(A, nA, prev[0], prev[1], gx, gy, d78, off, i80);
                bool c3 = closest(B, nB, prev[0], prev[1], gx, gy, d74, off, i7c);
                sA = saved_sA;
                sB = saved_sB;
                if (!c4 && !c3) {
                    c4 = closest(A, nA, sx, sy, last[0], last[1], d78, sA, i80);
                    c3 = closest(B, nB, sx, sy, last[0], last[1], d74, sB, i7c);
                    return partial(c4, c3);
                }
                --k_exit;
                const bool use_b = !c4 || (c3 && (d78 == d74 ? i7c < i80 : d78 > d74));
                if (use_b) {
                    resB = 2;
                    target = B[size_t(i7c)];
                    nB = i7c + 1;
                } else {
                    resA = 2;
                    target = A[size_t(i80)];
                    nA = i80 + 1;
                }
            }
        }
        if (resA == 2 || resB == 2) {
            // Keep the shorter side (A on a tie), copied into both buffers.
            const bool use_b = resA == 2 ? (resB == 2 && nB - sB < nA - sA) : true;
            const std::vector<Pt>& src = use_b ? B : A;
            const int off = use_b ? sB : sA, len = (use_b ? nB : nA) - off;
            std::vector<Pt> run(src.begin() + off, src.begin() + off + len);
            std::copy(run.begin(), run.end(), A.begin());
            std::copy(run.begin(), run.end(), B.begin());
            nA = nB = len;
            sA = sB = 0;
        } else if (resA == 3 && resB == 3) {
            code = 3;
            closest(A, nA, sx, sy, gx, gy, d78, sA, i80);
            return ret_start();
        }
        // (Other results are logged by the exe, which then continues from
        // the target; so do we.)
        cx = target[0];
        cy = target[1];
        if (cx == gx && cy == gy) break;
    }
    code = 1;
    if (nB - sB < nA - sA) return ret(B, sB, nB - sB);
    return ret(A, sA, nA - sA);
}

void World::smooth_path(const Unit& u, std::vector<std::array<int, 2>>& p) const {
    // FUN_0045fcc0 from the unit's cell.
    const int sx = u.cur_x, sy = u.cur_y;
    auto pass0 = [&](int x, int y) { return passable(x, y, u, 0); };
    // FUN_0045f970: cut the first loop (a cell met again), i < 12; repeated
    // until nothing changes.
    for (;;) {
        bool cut = false;
        const int n = int(p.size());
        for (int i = 0; i < n && i < 12 && !cut; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (p[size_t(i)] == p[size_t(j)]) {
                    p.erase(p.begin() + i, p.begin() + j);
                    cut = true;
                    break;
                }
            }
        }
        if (!cut) break;
    }
    bool changed = false;
    int guard = 0;
    do {
        size_t n0;
        do {
            n0 = p.size();
            // FUN_0045fa10: drop a cell whose neighbours touch.
            if (p.size() >= 3) {
                bool stop = false;
                if (near(sx, sy, p[1][0], p[1][1])) {
                    p.erase(p.begin());
                    stop = p.size() < 3;
                }
                for (int k = 0, iter = 0; !stop && iter < 12; ++iter) {
                    if (near(p[size_t(k)][0], p[size_t(k)][1], p[size_t(k + 2)][0], p[size_t(k + 2)][1])) {
                        p.erase(p.begin() + k + 1);
                        if (p.size() < 3) break;
                    } else {
                        ++k;
                    }
                    if (int(p.size()) <= k + 2) break;
                }
            }
            // FUN_0045fb00: pull a corner onto the straight line when the
            // cell beside it is free (mode 0).
            changed = false;
            if (p.size() >= 2) {
                auto& p0 = p[0];
                const auto& p1 = p[1];
                if (std::abs(sx - p1[0]) == 2 && sy == p1[1] && sy != p0[1]) {
                    if (pass0(sx < p0[0] ? sx + 1 : sx - 1, sy)) {
                        p0[1] = sy;
                        changed = true;
                    }
                } else if (sx == p1[0] && std::abs(sy - p1[1]) == 2 && sx != p0[0]) {
                    if (pass0(sx, sy < p0[1] ? sy + 1 : sy - 1)) {
                        p0[0] = sx;
                        changed = true;
                    }
                }
                for (size_t k = 0; k + 2 < p.size() && k < 12; ++k) {
                    auto& a = p[k];
                    auto& b = p[k + 1];
                    const auto& c = p[k + 2];
                    if (std::abs(a[0] - c[0]) == 2 && a[1] == c[1] && a[1] != b[1]) {
                        if (pass0(a[0] < b[0] ? a[0] + 1 : a[0] - 1, a[1])) {
                            b[1] = a[1];
                            changed = true;
                        }
                    } else if (a[0] == c[0] && std::abs(a[1] - c[1]) == 2 && a[0] != b[0]) {
                        if (pass0(a[0], b[1] > a[1] ? a[1] + 1 : a[1] - 1)) {
                            b[0] = a[0];
                            changed = true;
                        }
                    }
                }
            }
        } while (n0 != p.size() && ++guard < 1000);
    } while (changed && ++guard < 1000); // the exe has no cap; 300,000 random paths ended within 16 rounds, and a path that looped would hang the exe
}

} // namespace d2k::sim
