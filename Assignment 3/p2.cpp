#include "common.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <random>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>

using boost::asio::ip::tcp;

// DPF generation utilities (from gen_dpf.cpp)
static inline uint64_t smix(uint64_t x){
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static constexpr uint64_t C_L = 0xA5A5A5A5A5A5A5A5ull;
static constexpr uint64_t C_R = 0xC3C3C3C3C3C3C3C3ull;
static constexpr uint64_t C_TL = 0xB4B4B4B4B4B4B4B4ull;
static constexpr uint64_t C_TR = 0xD2D2D2D2D2D2D2D2ull;

struct PRGOut { uint64_t sL, sR; bool tL, tR; };

static inline PRGOut G(uint64_t s){
    uint64_t sL = smix(s ^ C_L);
    uint64_t sR = smix(s ^ C_R);
    bool tL = (smix(s ^ C_TL) & 1ULL);
    bool tR = (smix(s ^ C_TR) & 1ULL);
    return {sL, sR, tL, tR};
}

static inline int bit_at(uint64_t x, int pos_from_msb, int nbits){
    int shift = (nbits - 1 - pos_from_msb);
    return int((x >> shift) & 1ULL);
}

struct DPFPair {
    DPFKey k0, k1;
    int nbits;
    uint64_t domain_size;
    uint64_t alpha;
    uint64_t beta;
};

static DPFPair generateDPF(uint64_t domain_size, uint64_t alpha, uint64_t beta, std::mt19937_64 &rng){
    if (domain_size == 0) throw std::runtime_error("domain_size must be >= 1");
    if (alpha >= domain_size) throw std::runtime_error("alpha out of range");

    int nbits = 0; 
    uint64_t tmp = 1; 
    while (tmp < domain_size) { tmp <<= 1; ++nbits; }

    uint64_t sA = rng();
    uint64_t sB = rng();
    bool tA = 0;
    bool tB = 1;

    DPFKey kA, kB; 
    kA.s0 = sA; kA.t0 = tA; 
    kB.s0 = sB; kB.t0 = tB;
    kA.cws.reserve(nbits); 
    kB.cws.reserve(nbits);

    uint64_t sA_path = sA, sB_path = sB; 
    bool tA_path = tA, tB_path = tB;

    for (int i = 0; i < nbits; ++i){
        int a_i = bit_at(alpha, i, nbits);
        PRGOut gA = G(sA_path);
        PRGOut gB = G(sB_path);

        uint64_t dSL = gA.sL ^ gB.sL;
        uint64_t dSR = gA.sR ^ gB.sR;
        bool dTL = gA.tL ^ gB.tL;
        bool dTR = gA.tR ^ gB.tR;

        if (a_i == 0) dTL ^= 1; 
        else dTR ^= 1;

        DPFCorrectionWord cw{dSL, dSR, dTL, dTR};
        kA.cws.push_back(cw);
        kB.cws.push_back(cw);

        // Party A
        uint64_t sL_A = gA.sL, sR_A = gA.sR; 
        bool tL_A = gA.tL, tR_A = gA.tR;
        if (tA_path){ 
            sL_A ^= cw.dSL; tL_A ^= cw.dTL; 
            sR_A ^= cw.dSR; tR_A ^= cw.dTR; 
        }
        if (a_i == 0){ sA_path = sL_A; tA_path = tL_A; } 
        else { sA_path = sR_A; tA_path = tR_A; }

        // Party B
        uint64_t sL_B = gB.sL, sR_B = gB.sR; 
        bool tL_B = gB.tL, tR_B = gB.tR;
        if (tB_path){ 
            sL_B ^= cw.dSL; tL_B ^= cw.dTL; 
            sR_B ^= cw.dSR; tR_B ^= cw.dTR; 
        }
        if (a_i == 0){ sB_path = sL_B; tB_path = tL_B; } 
        else { sB_path = sR_B; tB_path = tR_B; }
    }

    uint64_t cwOut;
    if (tA_path) {
        uint64_t s_star = beta + sB_path;
        cwOut = sA_path ^ s_star;
    } else {
        uint64_t s_star = sA_path - beta;
        cwOut = sB_path ^ s_star;
    }

    kA.cwOut = cwOut; kB.cwOut = cwOut;

    DPFPair res; 
    res.k0 = kA; res.k1 = kB; 
    res.nbits = nbits; 
    res.domain_size = domain_size; 
    res.alpha = alpha; 
    res.beta = beta;
    return res;
}

// Endian helpers
static inline long long h2be64(long long x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

static inline int h2be32(int x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(x);
#else
    return x;
#endif
}

static inline uint64_t h2be64u(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

// Original makerandom function
inline auto makerandom(int k) {
    DuAtAllahServer security(k);
    auto [client0, client1] = security.generate_client_shares();
    return std::make_pair(std::move(client0), std::move(client1));
}

inline auto makerandommul() {
    DuAtAllahMultServer dmuls;
    DuAtAllahMultClient dmulc0, dmulc1;
    dmulc0.x = dmuls.x0;
    dmulc0.y = dmuls.y0;
    dmulc0.z = dmuls.alpha;
    dmulc1.x = dmuls.x1;
    dmulc1.y = dmuls.y1;
    dmulc1.z = -dmuls.alpha;
    return std::make_pair(std::move(dmulc0), std::move(dmulc1));
}

// Append helper
inline void appendshares(std::ofstream& f0, std::ofstream& f1, 
                         const DuAtAllahClient& s0, const DuAtAllahClient& s1) {
    int k = s0.X.size();
    for(int i = 0; i < k; i++) f0 << s0.X[i] << (i == k - 1 ? "\n" : " ");
    for(int i = 0; i < k; i++) f0 << s0.Y[i] << (i == k - 1 ? "\n" : " ");
    f0 << s0.z << "\n\n";

    for(int i = 0; i < k; i++) f1 << s1.X[i] << (i == k - 1 ? "\n" : " ");
    for(int i = 0; i < k; i++) f1 << s1.Y[i] << (i == k - 1 ? "\n" : " ");
    f1 << s1.z << "\n\n";
}

// Send DPF key
void send_dpf_key(tcp::socket& sock, const DPFKey& key) {
    // Send s0
    uint64_t s0_be = h2be64u(key.s0);
    boost::asio::write(sock, boost::asio::buffer(&s0_be, sizeof(s0_be)));

    // Send t0
    uint8_t t0_byte = key.t0 ? 1 : 0;
    boost::asio::write(sock, boost::asio::buffer(&t0_byte, 1));

    // Send number of cws
    uint32_t num_cws = static_cast<uint32_t>(key.cws.size());
    uint32_t num_cws_be = h2be32(num_cws);
    boost::asio::write(sock, boost::asio::buffer(&num_cws_be, sizeof(num_cws_be)));

    // Send each CW
    for (const auto& cw : key.cws) {
        uint64_t dSL_be = h2be64u(cw.dSL);
        uint64_t dSR_be = h2be64u(cw.dSR);
        uint8_t dTL_byte = cw.dTL ? 1 : 0;
        uint8_t dTR_byte = cw.dTR ? 1 : 0;

        boost::asio::write(sock, boost::asio::buffer(&dSL_be, sizeof(dSL_be)));
        boost::asio::write(sock, boost::asio::buffer(&dSR_be, sizeof(dSR_be)));
        boost::asio::write(sock, boost::asio::buffer(&dTL_byte, 1));
        boost::asio::write(sock, boost::asio::buffer(&dTR_byte, 1));
    }

    // Send cwOut
    uint64_t cwOut_be = h2be64u(key.cwOut);
    boost::asio::write(sock, boost::asio::buffer(&cwOut_be, sizeof(cwOut_be)));
}

int main() {
    try {
        std::cout << "P2 server starting...\n";

        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9002));

        std::cout << "Listening on port 9002 for client connections...\n";

        // Accept connections from P0 and P1
        tcp::socket socket_p0(io_context);
        tcp::socket socket_p1(io_context);

        std::cout << "Waiting for P0 to connect...\n";
        acceptor.accept(socket_p0);
        std::cout << "P0 connected.\n";

        std::cout << "Waiting for P1 to connect...\n";
        acceptor.accept(socket_p1);
        std::cout << "P1 connected.\n";

        // Read parameters
        std::ifstream params_file("/data/params.txt");
        if (!params_file) {
            std::cerr << "Failed to open /data/params.txt\n";
            return 1;
        }

        int m, n, k, q;
        if (!(params_file >> m >> n >> k >> q)) {
            std::cerr << "Failed to read parameters from params.txt\n";
            return 1;
        }
        std::cout << "Parameters: m=" << m << ", n=" << n << ", k=" << k << ", q=" << q << "\n";

        // Open output files
        std::ofstream f0("/data/p0_shares/client0.txt");
        std::ofstream f1("/data/p1_shares/client1.txt");

        if (!f0 || !f1) {
            std::cerr << "Failed to open output files\n";
            return 1;
        }

        // Generate q random shares for queries
        std::cout << "Generating " << q << " query shares...\n";
        for (int i = 0; i < q; ++i) {
            auto [s0, s1] = makerandom(k);
            appendshares(f0, f1, s0, s1);

            std::ostringstream line;
            for (int j = 0; j < k; ++j) {
                line << s0.X[j];
                if (j < k-1) line << " ";
            }
            line << "\n";
            for (int j = 0; j < k; ++j) {
                line << s0.Y[j];
                if (j < k-1) line << " ";
            }
            line << "\n" << s0.z << "\n\n";

            std::string data = line.str();
            boost::asio::write(socket_p0, boost::asio::buffer(data));

            line.str(""); line.clear();
            for (int j = 0; j < k; ++j) {
                line << s1.X[j];
                if (j < k-1) line << " ";
            }
            line << "\n";
            for (int j = 0; j < k; ++j) {
                line << s1.Y[j];
                if (j < k-1) line << " ";
            }
            line << "\n" << s1.z << "\n\n";

            data = line.str();
            boost::asio::write(socket_p1, boost::asio::buffer(data));
        }

        // Send terminator
        std::string ok_msg = "OK\n";
        boost::asio::write(socket_p0, boost::asio::buffer(ok_msg));
        boost::asio::write(socket_p1, boost::asio::buffer(ok_msg));

        std::cout << "Sent all query shares. Generating multiplication triples...\n";

        // Generate multiplication triples: need 2k per query (k for dot product, k for update)
        int triples_per_query = 2 * k;

        std::ostringstream header;
        header << "TRPL " << q << " " << triples_per_query << "\n";
        std::string hdr = header.str();
        boost::asio::write(socket_p0, boost::asio::buffer(hdr));
        boost::asio::write(socket_p1, boost::asio::buffer(hdr));

        for (int i = 0; i < q; ++i) {
            for (int j = 0; j < triples_per_query; ++j) {
                auto [m0, m1] = makerandommul();

                std::ostringstream t0, t1;
                t0 << m0.x << " " << m0.y << " " << m0.z << "\n";
                t1 << m1.x << " " << m1.y << " " << m1.z << "\n";

                boost::asio::write(socket_p0, boost::asio::buffer(t0.str()));
                boost::asio::write(socket_p1, boost::asio::buffer(t1.str()));
            }
        }

        std::string tok_msg = "TOK\n";
        boost::asio::write(socket_p0, boost::asio::buffer(tok_msg));
        boost::asio::write(socket_p1, boost::asio::buffer(tok_msg));

        std::cout << "Sent all multiplication triples.\n";

        // Generate and send DPF keys for each query (Assignment 3)
        std::cout << "Generating DPF keys for " << q << " queries...\n";

        std::random_device rd;
        std::seed_seq seed{rd(), rd(), rd(), rd()};
        std::mt19937_64 rng(seed);

        // Read queries to get item indices
        std::ifstream queries_file("/data/queries.txt");
        if (!queries_file) {
            std::cerr << "Warning: Could not open queries.txt, using random item indices\n";
        }

        long long q_count, k_count;
        queries_file >> q_count >> k_count;

        for (int qidx = 0; qidx < q; ++qidx) {
            // Read query to get item index
            uint64_t user_idx, item_idx;
            if (queries_file) {
                queries_file >> user_idx >> item_idx;
                // Skip the k values
                for (int i = 0; i < k; ++i) {
                    long long dummy;
                    queries_file >> dummy;
                }
            } else {
                item_idx = rng() % n; // fallback to random
            }

            // Generate DPF with alpha=item_idx, beta=0 (user will adjust later)
            auto dpf_pair = generateDPF(n, item_idx, 0, rng);

            // Send key0 to P0, key1 to P1
            send_dpf_key(socket_p0, dpf_pair.k0);
            send_dpf_key(socket_p1, dpf_pair.k1);

            std::cout << "  Sent DPF keys for query #" << qidx << " (item=" << item_idx << ")\n";
        }

        std::cout << "All DPF keys sent. P2 server done.\n";

    } catch (std::exception& e) {
        std::cerr << "Exception in P2: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
