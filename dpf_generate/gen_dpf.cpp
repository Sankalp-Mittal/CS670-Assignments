#include <iostream>
#include <vector>
#include <random>
#include <cstdint>
#include <utility>
#include <string>
using namespace std;

// --------------------------- Utilities ---------------------------
static inline uint64_t rotl64(uint64_t x, unsigned r){ return (x << r) | (x >> (64 - r)); }

static inline uint64_t smix(uint64_t x){
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static constexpr uint64_t C_L  = 0xA5A5A5A5A5A5A5A5ull;
static constexpr uint64_t C_R  = 0xC3C3C3C3C3C3C3C3ull;
static constexpr uint64_t C_TL = 0xB4B4B4B4B4B4B4B4ull;
static constexpr uint64_t C_TR = 0xD2D2D2D2D2D2D2D2ull;
static constexpr uint64_t C_V  = 0xEEEEEEEEEEEEEEEEull;

static inline int bit_at(uint64_t x, int pos_from_msb, int nbits){
    int shift = (nbits - 1 - pos_from_msb);
    return int((x >> shift) & 1ULL);
}

// PRG G: from seed s -> (sL, tL, sR, tR)
struct PRGOut { uint64_t sL, sR; bool tL, tR; };
static inline PRGOut G(uint64_t s){
    uint64_t sL = smix(s ^ C_L);
    uint64_t sR = smix(s ^ C_R);
    bool tL = (smix(s ^ C_TL) & 1ULL);
    bool tR = (smix(s ^ C_TR) & 1ULL);
    return {sL, sR, tL, tR};
}

// Value extraction v(s) in Z_2^64 (wrap-around addition). In a real system use a PRF.
static inline uint64_t v_from_seed(uint64_t s){ return s; } // identity mapping to Z_2^64 so final seed-correction can program output exactly

// --------------------------- DPF Types ---------------------------
struct CW { // per level correction words: for left and right branches
    uint64_t dSL, dSR; // seed corrections (XOR)
    bool     dTL, dTR; // t-bit corrections (XOR)
};

struct DPFKey {
    uint64_t s0; // root seed for this party
    bool     t0; // root control bit for this party (party 0: 0, party 1: 1)
    vector<CW> cws; // shared across both parties
    uint64_t cwOut; // final additive correction word in Z_2^64
};

struct DPFPair { DPFKey k0, k1; int nbits; uint64_t domain_size; uint64_t alpha; uint64_t beta; };

// ------------------------ DPF Generation -------------------------
static DPFPair generateDPF(uint64_t domain_size, uint64_t alpha, uint64_t beta, std::mt19937_64 &rng){
    if (domain_size == 0) throw runtime_error("domain_size must be >= 1");
    if (alpha >= domain_size) throw runtime_error("alpha out of range");

    // nbits = ceil(log2(domain_size))
    int nbits = 0; uint64_t tmp = 1; while (tmp < domain_size) { tmp <<= 1; ++nbits; }

    // Sample independent root seeds s0^A, s0^B
    uint64_t sA = rng();
    uint64_t sB = rng();
    bool tA = 0; // as in standard construction
    bool tB = 1; // ensures tA XOR tB = 1 at root

    DPFKey kA, kB; kA.s0 = sA; kA.t0 = tA; kB.s0 = sB; kB.t0 = tB;
    kA.cws.reserve(nbits); kB.cws.reserve(nbits);

    // For computing final cwOut we must follow the alpha-path during Gen
    uint64_t sA_path = sA, sB_path = sB; bool tA_path = tA, tB_path = tB;

    for (int i = 0; i < nbits; ++i){
        int a_i = bit_at(alpha, i, nbits);

        PRGOut gA = G(sA_path);
        PRGOut gB = G(sB_path);

        // raw deltas (XOR of children across parties)
        uint64_t dSL = gA.sL ^ gB.sL;
        uint64_t dSR = gA.sR ^ gB.sR;
        bool     dTL = gA.tL ^ gB.tL;
        bool     dTR = gA.tR ^ gB.tR;

        // Make on-path branch flip t-bit across parties (so tA^tB becomes 1 on-path, 0 off-path)
        if (a_i == 0) dTL ^= 1; else dTR ^= 1;

        CW cw{dSL, dSR, dTL, dTR};
        kA.cws.push_back(cw); // identical CWs in both keys
        kB.cws.push_back(cw);

        // Advance along the *alpha* path for Gen (apply conditional corrections based on current t)
        // Party A
        uint64_t sL_A = gA.sL, sR_A = gA.sR; bool tL_A = gA.tL, tR_A = gA.tR;
        if (tA_path){ sL_A ^= cw.dSL; tL_A ^= cw.dTL; sR_A ^= cw.dSR; tR_A ^= cw.dTR; }
        if (a_i == 0){ sA_path = sL_A; tA_path = tL_A; } else { sA_path = sR_A; tA_path = tR_A; }

        // Party B
        uint64_t sL_B = gB.sL, sR_B = gB.sR; bool tL_B = gB.tL, tR_B = gB.tR;
        if (tB_path){ sL_B ^= cw.dSL; tL_B ^= cw.dTL; sR_B ^= cw.dSR; tR_B ^= cw.dTR; }
        if (a_i == 0){ sB_path = sL_B; tB_path = tL_B; } else { sB_path = sR_B; tB_path = tR_B; }
    }

    // Final output correction: ensure y0(alpha)+y1(alpha) = beta
    // Final SEED correction word (XOR into exactly one party at the leaf)
    // We program the output so that y0 + y1 = beta in Z_2^64 while off-path cancels.
    uint64_t cwOut;
    if (tA_path) {
        // Only A will apply cwOut at the leaf: set A's final seed to s* = beta + sB_path
        uint64_t s_star = beta + sB_path; // mod 2^64
        cwOut = sA_path ^ s_star;        // XOR so that (sA_path ^ cwOut) == s_star
    } else {
        // Only B will apply cwOut at the leaf: set B's final seed to s* = sA_path - beta
        uint64_t s_star = sA_path - beta; // mod 2^64
        cwOut = sB_path ^ s_star;        // XOR so that (sB_path ^ cwOut) == s_star
    }

    kA.cwOut = cwOut; kB.cwOut = cwOut;
    // wraps mod 2^64: choose signs so off-path sums cancel (y0 = +v, y1 = -v)

    kA.cwOut = cwOut; kB.cwOut = cwOut;

    DPFPair res; res.k0 = kA; res.k1 = kB; res.nbits = nbits; res.domain_size = domain_size; res.alpha = alpha; res.beta = beta;
    return res;
}

// --------------------------- Evaluation -------------------------
static uint64_t evalDPF(const DPFKey &key, uint64_t x, int nbits){
    uint64_t s = key.s0; bool t = key.t0;

    for (int i = 0; i < nbits; ++i){
        PRGOut g = G(s);
        // Apply conditional corrections if t==1, to both branches, then select
        uint64_t sL = g.sL, sR = g.sR; bool tL = g.tL, tR = g.tR;
        const CW &cw = key.cws[i];
        if (t){ sL ^= cw.dSL; tL ^= cw.dTL; sR ^= cw.dSR; tR ^= cw.dTR; }
        int b = bit_at(x, i, nbits);
        if (b == 0){ s = sL; t = tL; } else { s = sR; t = tR; }
    }

    if (t) s ^= key.cwOut;
    uint64_t y = v_from_seed(s);
    // Party 0 uses +, Party 1 uses - so that off-path sums cancel to 0 in Z_2^64
    if (key.t0) y = 0ull - y; // negate for the t0=1 key
    return y; // in Z_2^64
}

// Evaluate full domain and verify reconstruction
static bool EvalFull(const DPFKey &k0, const DPFKey &k1, uint64_t size, int nbits, uint64_t expect_alpha, uint64_t expect_beta){
    bool ok = true;
    // vector<uint64_t> v0, v1;
    for (uint64_t x = 0; x < size; ++x){
        uint64_t y0 = evalDPF(k0, x, nbits);
        // v0.push_back(y0);

        uint64_t y1 = evalDPF(k1, x, nbits);
        // v1.push_back(y1);

        uint64_t y  = y0 + y1; // Z_2^64
        uint64_t should = (x == expect_alpha) ? expect_beta : 0ull;
        if (y != should){
            cerr << "Mismatch at x=" << x << ": got " << y << ", expected " << should << "\n";
            ok = false; // keep scanning to print all mismatches
        }
    }

    // for(auto el: v0) cout << el << " ";
    // cout<<"\n";

    // for(auto el: v1) cout << el << " ";
    // cout<<"\n";

    return ok;
}

// ----------------------------- I/O ------------------------------
static void print_key(const DPFKey &k){
    cout << "{\n";
    cout << "  \"s0\": " << k.s0 << ",\n";
    cout << "  \"t0\": " << (k.t0?1:0) << ",\n";
    cout << "  \"cwOut\": " << k.cwOut << ",\n";
    cout << "  \"cws\": [\n";
    for (size_t i = 0; i < k.cws.size(); ++i){
        const CW &w = k.cws[i];
        cout << "    { \"dSL\": " << w.dSL << ", \"dTL\": " << (w.dTL?1:0)
             << ", \"dSR\": " << w.dSR << ", \"dTR\": " << (w.dTR?1:0) << " }";
        if (i + 1 != k.cws.size()) cout << ",";
        cout << "\n";
    }
    cout << "  ]\n";
    cout << "}";
}

// ----------------------------- Main -----------------------------
int main(int argc, char **argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc != 3){
        cerr << "Usage: ./gen_queries <DPF_size> <num_DPFs>\n";
        return 1;
    }

    uint64_t DPF_size = 0, num = 0;
    try{
        DPF_size = stoull(argv[1]);
        num      = stoull(argv[2]);
    } catch (...){ cerr << "Invalid arguments\n"; return 1; }

    // RNG: seed mt19937_64 from random_device
    std::random_device rd;
    std::seed_seq seed{rd(), rd(), rd(), rd(), rd(), rd()};
    std::mt19937_64 rng(seed);

    // nbits = ceil(log2(DPF_size)) once (we still re-check in generator)
    int nbits = 0; uint64_t tmp = 1; while (tmp < DPF_size) { tmp <<= 1; ++nbits; }

    for (uint64_t i = 0; i < num; ++i){
        uint64_t alpha = rng() % DPF_size;
        uint64_t beta  = ((uint64_t)rng() << 1) ^ (uint64_t)rd(); // 64-bit random target value

        auto pair = generateDPF(DPF_size, alpha, beta, rng);

        bool ok = EvalFull(pair.k0, pair.k1, DPF_size, pair.nbits, alpha, beta);
        cout << "DPF #" << i << " (size=" << DPF_size << ", alpha=" << alpha << ", beta=" << beta << ") => "
             << (ok ? "Test Passed" : "Test Failed") << "\n";

        // Print the keys (line-delimited JSON-ish for each party)
        cout << "Key0:\n"; print_key(pair.k0); cout << "\n";
        cout << "Key1:\n"; print_key(pair.k1); cout << "\n";
        cout << string(60, '-') << "\n";
    }

    return 0;
}
