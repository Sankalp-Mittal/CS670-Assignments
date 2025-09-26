#!/usr/bin/env python3
import sys
import re

HEADER_U_RE = re.compile(r'^\s*U\s*\(\s*m=(\d+)\s*,\s*k=(\d+)\s*\)\s*$', re.IGNORECASE)
HEADER_V_RE = re.compile(r'^\s*V\s*\(\s*n=(\d+)\s*,\s*k=(\d+)\s*\)\s*$', re.IGNORECASE)
HEADER_Q_RE = re.compile(r'^\s*queries\s*\(\s*q=(\d+)\s*\)\s*$', re.IGNORECASE)

def read_next_nonempty_line(it):
    for line in it:
        s = line.strip()
        if s:
            return s
    raise EOFError("Unexpected EOF while reading.")

def parse_header(line, kind):
    if kind == "U":
        m = HEADER_U_RE.match(line)
        if not m: raise ValueError("Expected header like: U (m=..., k=...)")
        return int(m.group(1)), int(m.group(2))
    if kind == "V":
        m = HEADER_V_RE.match(line)
        if not m: raise ValueError("Expected header like: V (n=..., k=...)")
        return int(m.group(1)), int(m.group(2))
    if kind == "Q":
        m = HEADER_Q_RE.match(line)
        if not m: raise ValueError("Expected header like: queries (q=...)")
        return int(m.group(1))
    raise ValueError("bad header kind")

def read_int_line(it, expected_len=None):
    line = read_next_nonempty_line(it)
    toks = line.split()
    try:
        vals = [int(t) for t in toks]
    except ValueError:
        raise ValueError(f"Non-integer token in line: {line!r}")
    if expected_len is not None and len(vals) != expected_len:
        raise ValueError(f"Expected {expected_len} ints, got {len(vals)}: {line!r}")
    return vals

def dot(a, b):
    if len(a) != len(b): raise ValueError("dot(): length mismatch")
    s = 0
    for x, y in zip(a, b):
        s += x * y
    return s

def add_scaled(u, alpha, v):  # u += alpha * v (in place)
    if len(u) != len(v): raise ValueError("add_scaled(): length mismatch")
    for i in range(len(u)):
        u[i] += alpha * v[i]

def parse_plain_uv(path):
    with open(path, "r", encoding="utf-8") as f:
        it = iter(f)

        # U
        u_header = read_next_nonempty_line(it)
        m, k_u = parse_header(u_header, "U")
        U = [read_int_line(it, expected_len=k_u) for _ in range(m)]

        # V
        v_header = read_next_nonempty_line(it)
        n, k_v = parse_header(v_header, "V")
        if k_v != k_u:
            raise ValueError(f"k mismatch between U(k={k_u}) and V(k={k_v})")
        V = [read_int_line(it, expected_len=k_v) for _ in range(n)]

        # queries
        q_header = read_next_nonempty_line(it)
        q = parse_header(q_header, "Q")
        queries = [tuple(read_int_line(it, expected_len=2)) for _ in range(q)]

    # Normalize query indices: accept either 0-based or 1-based
    # If any index equals m or n, we assume 1-based and subtract 1 from all.
    if queries:
        max_i = max(i for i, _ in queries)
        max_j = max(j for _, j in queries)
        # Heuristic: if any index is exactly m or n, or no index is 0, assume 1-based
        assume_one_based = (max_i == m or max_j == n) or all(i >= 1 for i, _ in queries)
        if assume_one_based:
            queries = [(i-1, j-1) for (i, j) in queries]
    return U, V, queries

def parse_share_matrix(path):
    with open(path, "r", encoding="utf-8") as f:
        head = read_next_nonempty_line(iter(f))
        toks = head.split()
        if len(toks) != 2:
            raise ValueError(f"{path}: first line must be 'rows cols'")
        rows, cols = map(int, toks)
        M = []
        it = iter(f)
        for _ in range(rows):
            vals = read_int_line(it, expected_len=cols)
            M.append(vals)
    return M

def mat_shape(M):
    return (len(M), 0 if not M else len(M[0]))

def mat_add(A, B):
    if mat_shape(A) != mat_shape(B):
        raise ValueError("mat_add: shape mismatch")
    R, C = mat_shape(A)
    out = [[A[r][c] + B[r][c] for c in range(C)] for r in range(R)]
    return out

def mat_diff(A, B):  # A - B
    if mat_shape(A) != mat_shape(B):
        raise ValueError("mat_diff: shape mismatch")
    R, C = mat_shape(A)
    out = [[A[r][c] - B[r][c] for c in range(C)] for r in range(R)]
    return out

def all_zero_row(row):
    return all(x == 0 for x in row)

def main():
    if len(sys.argv) != 4:
        print("Usage: python checker.py <plain_UV.txt> <p0_U.txt> <p1_U.txt>", file=sys.stderr)
        sys.exit(1)

    plain_uv_path, p0_u_path, p1_u_path = sys.argv[1], sys.argv[2], sys.argv[3]

    # 1) Parse ground-truth U, V, queries
    U, V, queries = parse_plain_uv(plain_uv_path)
    m, k = mat_shape(U)
    n, k2 = mat_shape(V)
    if k != k2:
        raise ValueError(f"k mismatch after parsing: U has {k}, V has {k2}")

    # 2) Apply updates sequentially on a working copy of U
    U_updated = [row[:] for row in U]
    touched = set()
    for (i, j) in queries:
        i += 1
        j+= 1
        if not (0 <= i < m and 0 <= j < n):
            raise IndexError(f"Query out of range after normalization: (i={i}, j={j}), m={m}, n={n}")
        touched.add(i)
        dp = dot(U_updated[i], V[j])
        delta = 1 - dp
        add_scaled(U_updated[i], delta, V[j])  # U_i += delta * V_j

    # 3) Load shares and reconstruct by summing
    U0 = parse_share_matrix(p0_u_path)
    U1 = parse_share_matrix(p1_u_path)
    if mat_shape(U0) != (m, k) or mat_shape(U1) != (m, k):
        raise ValueError(f"share shapes don't match U (expected {m}x{k}); got {mat_shape(U0)} and {mat_shape(U1)}")

    U_sum = mat_add(U0, U1)
    print(U_sum)

    print(U_updated)

    # 4) Compare
    D = mat_diff(U_sum, U_updated)  # (p0+p1) - updated_U

    # Report
    mismatches = [(r, D[r]) for r in range(m) if not all_zero_row(D[r])]
    print(f"m={m}, k={k}, n={n}, queries={len(queries)}")
    print(f"Touched rows: {sorted(touched)}")
    if not mismatches:
        print("OK: (p0_U + p1_U) matches Updated_U exactly.")
        return

    print(f"Mismatch rows: {len(mismatches)}")
    touched_mis = [(r, d) for (r, d) in mismatches if r in touched]
    other_mis   = [(r, d) for (r, d) in mismatches if r not in touched]

    if touched_mis:
        print("\nMISMATCH (touched rows):")
        for r, d in touched_mis:
            print(f" row {r}: diff = {d}")

    if other_mis:
        print("\nMISMATCH (untouched rows):")
        for r, d in other_mis:
            print(f" row {r}: diff = {d}")

    max_abs = 0
    for r in range(m):
        for x in D[r]:
            ax = abs(x)
            if ax > max_abs: max_abs = ax
    print(f"\nMax |diff| over all entries: {max_abs}")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(2)
