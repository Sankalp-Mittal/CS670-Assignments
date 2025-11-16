#include "common.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/connect.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>

using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;

#if !defined(ROLE_p0) && !defined(ROLE_p1)
#error "ROLE must be defined as ROLE_p0 or ROLE_p1"
#endif

// --- endian helpers (wire = big-endian) ---
static inline long long h2be64(long long x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

static inline long long be2h64(long long x){
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

static inline int be2h32(int x){
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

static inline uint64_t be2h64u(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

// ----------------------- Helper coroutines -----------------------
awaitable<void> send_coroutine(tcp::socket& sock, int value) {
    co_await boost::asio::async_write(sock, boost::asio::buffer(&value, sizeof(value)), use_awaitable);
}

awaitable<void> recv_coroutine(tcp::socket& sock, int& out) {
    co_await boost::asio::async_read(sock, boost::asio::buffer(&out, sizeof(out)), use_awaitable);
}

static inline const char* user_matrix_path() {
#ifdef ROLE_p0
    return P0_USER_SHARES_FILE;
#else
    return P1_USER_SHARES_FILE;
#endif
}

static inline const char* item_matrix_path() {
#ifdef ROLE_p0
    return P0_ITEM_SHARES_FILE;
#else
    return P1_ITEM_SHARES_FILE;
#endif
}

static inline const char* query_path() {
#ifdef ROLE_p0
    return P0_QUERIES_SHARES_FILE;
#else
    return P1_QUERIES_SHARES_FILE;
#endif
}

// ----------------------- Setup connections -----------------------
awaitable<tcp::socket> setup_server_connection(boost::asio::io_context& io_context, tcp::resolver& resolver) {
    tcp::socket sock(io_context);
    auto endpoints_p2 = resolver.resolve("p2", "9002");
    co_await boost::asio::async_connect(sock, endpoints_p2, use_awaitable);
    co_return sock;
}

awaitable<tcp::socket> setup_peer_connection(boost::asio::io_context& io_context, tcp::resolver& resolver) {
    tcp::socket sock(io_context);
#ifdef ROLE_p0
    auto endpoints_p1 = resolver.resolve("p1", "9001");
    co_await boost::asio::async_connect(sock, endpoints_p1, use_awaitable);
#else
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9001));
    sock = co_await acceptor.async_accept(use_awaitable);
#endif
    co_return sock;
}

// ----------------------- File persistence -----------------------
#ifdef ROLE_p0
static constexpr const char* SHARE_LOG_PATH = "/data/client0.shares";
static constexpr const char* RESULT_LOG_PATH = "/data/client0.results";
#else
static constexpr const char* SHARE_LOG_PATH = "/data/client1.shares";
static constexpr const char* RESULT_LOG_PATH = "/data/client1.results";
#endif

static inline void append_my_share_to_file(const DuAtAllahClient& s, std::size_t idx) {
    std::ofstream f(SHARE_LOG_PATH, std::ios::app);
    if (!f) { std::cerr << "Failed to open " << SHARE_LOG_PATH << " for append\n"; return; }
    f << "# query " << idx << "\n";
    for (size_t i = 0; i < s.X.size(); ++i) { if (i) f << ' '; f << s.X[i]; } f << '\n';
    for (size_t i = 0; i < s.Y.size(); ++i) { if (i) f << ' '; f << s.Y[i]; } f << '\n';
    f << s.z << "\n\n";
}

static inline void append_result_share_to_file(std::size_t idx, random_vector& share_vector, int user_idx) {
    std::ofstream f(RESULT_LOG_PATH, std::ios::app);
    if (!f) { std::cerr << "Failed to open " << RESULT_LOG_PATH << " for append\n"; return; }
    f << "query " << idx << " by user #" << user_idx << " | updated share: ";
    for (size_t i = 0; i < share_vector.size(); ++i) { if (i) f << ' '; f << share_vector[i]; }
    f << "\n";
}

// ----------------------- Share reception + storage -----------------------
static inline void rstrip_cr(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

awaitable<void> read_line(tcp::socket& sock, boost::asio::streambuf& buf, std::string& line) {
    co_await boost::asio::async_read_until(sock, buf, '\n', use_awaitable);
    std::istream is(&buf);
    std::getline(is, line);
    rstrip_cr(line);
    co_return;
}

static inline random_vector parse_int_line(const std::string& line) {
    std::vector<long long> out;
    std::istringstream iss(line);
    long long v;
    while (iss >> v) out.push_back(v);
    random_vector rv(0);
    rv.data = out;
    return rv;
}

awaitable<void> recv_all_shares_from_P2(tcp::socket& sock,
                                         std::vector<DuAtAllahClient>& store,
                                         std::vector<std::vector<DuAtAllahMultClient>>& mul_store) {
    boost::asio::streambuf buf;
    int idx = 0;

    // --------- 1) READ SHARE LINES UNTIL "OK" ----------
    for (;;) {
        std::string line1;
        do { co_await read_line(sock, buf, line1); } while (line1.empty());
        if (line1 == "OK") break;

        random_vector rv_temp = parse_int_line(line1);
        DuAtAllahClient s(rv_temp.size());
        s.X = rv_temp;

        std::string line2; co_await read_line(sock, buf, line2);
        s.Y = parse_int_line(line2);

        std::string line3; co_await read_line(sock, buf, line3);
        { std::istringstream iss(line3); iss >> s.z; }

        std::string sep; co_await read_line(sock, buf, sep);
        append_my_share_to_file(s, idx++);
        store.emplace_back(std::move(s));
    }

    std::cout << "Total shares received from P2: " << store.size() << std::endl;

    // --------- 2) READ MULTIPLICATION TRIPLES BLOCK ----------
    std::string header;
    co_await read_line(sock, buf, header);

    std::istringstream hs(header);
    std::string tag; int q = 0, k = 0;
    if (!(hs >> tag >> q >> k) || tag != "TRPL" || q <= 0 || k <= 0) {
        throw std::runtime_error("triples header malformed: " + header);
    }

    mul_store.assign(q, std::vector<DuAtAllahMultClient>(k));
    for (int i = 0; i < q; ++i) {
        for (int d = 0; d < k; ++d) {
            std::string ln;
            co_await read_line(sock, buf, ln);
            std::istringstream ls(ln);
            long long x, y, z;
            if (!(ls >> x >> y >> z)) {
                std::ostringstream oss;
                oss << "triple parse error at (" << i << "," << d << ")";
                throw std::runtime_error(oss.str());
            }
            mul_store[i][d].x = x;
            mul_store[i][d].y = y;
            mul_store[i][d].z = z;
        }
    }

    std::string tok;
    co_await read_line(sock, buf, tok);
    if (tok != "TOK") {
        throw std::runtime_error("triples terminator missing (expected TOK), got: " + tok);
    }

    std::cout << "Total multiplication triples received from P2: " << mul_store.size() << " sets of "
              << (mul_store.empty() ? 0 : mul_store[0].size()) << " each\n";
    co_return;
}

// ----------------------- Matrix file I/O -----------------------
static random_vector read_row_from_matrix_file(const std::string& path, int row_index) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to open " + path);
    int rows=0, cols=0;
    if (!(f >> rows >> cols)) {
        throw std::runtime_error("Bad header in " + path);
    }

    if (row_index < 0 || row_index >= rows) {
        throw std::runtime_error("Row index out of range in " + path);
    }

    std::string line;
    std::getline(f, line); // consume end of header line
    for (int r = 0; r < row_index; ++r) {
        std::getline(f, line);
        if (!f) throw std::runtime_error("Unexpected EOF while skipping rows in " + path);
    }

    std::getline(f, line);
    if (!f) throw std::runtime_error("Unexpected EOF reading row in " + path);
    std::istringstream iss(line);
    std::vector<long long> row;
    row.reserve(cols);
    for (int c = 0; c < cols; ++c) {
        long long v;
        if (!(iss >> v)) {
            throw std::runtime_error("Row parse error in " + path);
        }
        row.push_back(v);
    }

    random_vector vec(0);
    vec.data = row;
    return vec;
}

static void update_row_in_matrix_file(const std::string& path, int row_index,
                                      const std::vector<long long>& newrow) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to open " + path);
    int rows=0, cols=0;
    if (!(f >> rows >> cols)) throw std::runtime_error("Bad header in " + path);
    if (row_index < 0 || row_index >= rows)
        throw std::runtime_error("Row index out of range for update in " + path);

    std::vector<std::vector<long long>> M(rows, std::vector<long long>(cols));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (!(f >> M[r][c])) throw std::runtime_error("Matrix body parse error in " + path);
        }
    }
    if ((int)newrow.size() != cols)
        throw std::runtime_error("New row has wrong length in update for " + path);

    M[row_index] = newrow;

    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("Failed to open temp " + tmp);
    out << rows << " " << cols << "\n";
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (c) out << ' ';
            out << M[r][c];
        }
        out << "\n";
    }
    out.close();

    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        throw std::runtime_error("Failed to rename " + tmp + " -> " + path);
    }
}

// ----------------------- Communication helpers -----------------------
static inline awaitable<void> send_two_u64(tcp::socket& sock, long long u0, long long u1) {
    long long be[2] = { h2be64(u0), h2be64(u1) };
    co_await boost::asio::async_write(sock, boost::asio::buffer(be, sizeof(be)), use_awaitable);
    co_return;
}

static inline awaitable<void> recv_two_u64(tcp::socket& sock, long long& u0, long long& u1) {
    long long be[2];
    co_await boost::asio::async_read(sock, boost::asio::buffer(be, sizeof(be)), use_awaitable);
    u0 = be2h64(be[0]);
    u1 = be2h64(be[1]);
    co_return;
}

// ----------------------- Barriers -----------------------
awaitable<void> barrier_prep(tcp::socket& peer) {
#ifdef ROLE_p0
    int code = 1;
    co_await send_coroutine(peer, code);
    int ack; co_await recv_coroutine(peer, ack);
#else
    int code; co_await recv_coroutine(peer, code);
    co_await send_coroutine(peer, code);
#endif
    co_return;
}

awaitable<void> barrier_query(tcp::socket& peer, int idx) {
#ifdef ROLE_p0
    int code = 2;
    co_await send_coroutine(peer, code);
    co_await send_coroutine(peer, idx);
    int code2, idx2;
    co_await recv_coroutine(peer, code2);
    co_await recv_coroutine(peer, idx2);
    if (code2 != 2 || idx2 != idx) {
        std::cerr << "Barrier mismatch (sent idx=" << idx << ", got idx=" << idx2 << ")\n";
    }
#else
    int code_in, idx_in;
    co_await recv_coroutine(peer, code_in);
    co_await recv_coroutine(peer, idx_in);
    co_await send_coroutine(peer, code_in);
    co_await send_coroutine(peer, idx_in);
#endif
    co_return;
}

// ----------------------- Query file loader -----------------------
static std::vector<std::vector<long long>> read_queries_file(const std::string& path) {
    std::ifstream fin(path);
    std::vector<std::vector<long long>> queries;
    if (!fin) {
        std::cerr << "Failed to open " << path << "\n";
        return queries;
    }

    long long q = 0;
    if (!(fin >> q)) {
        std::cerr << path <<": first token must be q\n";
        return queries;
    }

    queries.reserve(static_cast<size_t>(q));
    long long k = 0;
    if (!(fin >> k)) {
        std::cerr << path <<": second token must be k\n";
        return queries;
    }

    long long expected_len = k+2; // user_idx, item_idx, k values
    for (long long i = 0; i < q; ++i) {
        std::vector<long long> v(expected_len);
        for (int j = 0; j < expected_len; ++j) {
            if (!(fin >> v[j])) {
                std::ostringstream oss;
                oss << path <<": not enough numbers on query " << i << " (expected " << expected_len << ")\n";
                throw std::runtime_error(oss.str());
            }
        }
        if ((int)v.size() == expected_len) queries.emplace_back(std::move(v));
        else break;
    }

    return queries;
}

// ----------------------- MPC multiplication -----------------------
static awaitable<long long> secure_mpc_multiplication(long long a, long long b,
                                                       DuAtAllahMultClient dmulc,
                                                       tcp::socket& peer_sock){
    long long myx = a + dmulc.x, myy = b + dmulc.y;
    long long peerx, peery;

#ifdef ROLE_p0
    co_await send_two_u64(peer_sock, myx, myy);
    co_await recv_two_u64(peer_sock, peerx, peery);
#else
    co_await recv_two_u64(peer_sock, peerx, peery);
    co_await send_two_u64(peer_sock, myx, myy);
#endif

    long long c = a*(b + peery) - dmulc.y*(peerx) + dmulc.z;
    co_return c;
}

// ----------------------- DPF Key Exchange (Assignment 3) -----------------------
// Send DPF key structure
static awaitable<void> send_dpf_key(tcp::socket& sock, const DPFKey& key) {
    // Send s0
    uint64_t s0_be = h2be64u(key.s0);
    co_await boost::asio::async_write(sock, boost::asio::buffer(&s0_be, sizeof(s0_be)), use_awaitable);

    // Send t0 as uint8_t
    uint8_t t0_byte = key.t0 ? 1 : 0;
    co_await boost::asio::async_write(sock, boost::asio::buffer(&t0_byte, 1), use_awaitable);

    // Send number of cws
    uint32_t num_cws = static_cast<uint32_t>(key.cws.size());
    uint32_t num_cws_be = h2be32(num_cws);
    co_await boost::asio::async_write(sock, boost::asio::buffer(&num_cws_be, sizeof(num_cws_be)), use_awaitable);

    // Send each correction word
    for (const auto& cw : key.cws) {
        uint64_t dSL_be = h2be64u(cw.dSL);
        uint64_t dSR_be = h2be64u(cw.dSR);
        uint8_t dTL_byte = cw.dTL ? 1 : 0;
        uint8_t dTR_byte = cw.dTR ? 1 : 0;

        co_await boost::asio::async_write(sock, boost::asio::buffer(&dSL_be, sizeof(dSL_be)), use_awaitable);
        co_await boost::asio::async_write(sock, boost::asio::buffer(&dSR_be, sizeof(dSR_be)), use_awaitable);
        co_await boost::asio::async_write(sock, boost::asio::buffer(&dTL_byte, 1), use_awaitable);
        co_await boost::asio::async_write(sock, boost::asio::buffer(&dTR_byte, 1), use_awaitable);
    }

    // Send cwOut
    uint64_t cwOut_be = h2be64u(key.cwOut);
    co_await boost::asio::async_write(sock, boost::asio::buffer(&cwOut_be, sizeof(cwOut_be)), use_awaitable);

    co_return;
}

// Receive DPF key structure
static awaitable<DPFKey> recv_dpf_key(tcp::socket& sock) {
    DPFKey key;

    // Receive s0
    uint64_t s0_be;
    co_await boost::asio::async_read(sock, boost::asio::buffer(&s0_be, sizeof(s0_be)), use_awaitable);
    key.s0 = be2h64u(s0_be);

    // Receive t0
    uint8_t t0_byte;
    co_await boost::asio::async_read(sock, boost::asio::buffer(&t0_byte, 1), use_awaitable);
    key.t0 = (t0_byte != 0);

    // Receive number of cws
    uint32_t num_cws_be;
    co_await boost::asio::async_read(sock, boost::asio::buffer(&num_cws_be, sizeof(num_cws_be)), use_awaitable);
    uint32_t num_cws = be2h32(num_cws_be);

    // Receive each correction word
    key.cws.resize(num_cws);
    for (uint32_t i = 0; i < num_cws; ++i) {
        uint64_t dSL_be, dSR_be;
        uint8_t dTL_byte, dTR_byte;

        co_await boost::asio::async_read(sock, boost::asio::buffer(&dSL_be, sizeof(dSL_be)), use_awaitable);
        co_await boost::asio::async_read(sock, boost::asio::buffer(&dSR_be, sizeof(dSR_be)), use_awaitable);
        co_await boost::asio::async_read(sock, boost::asio::buffer(&dTL_byte, 1), use_awaitable);
        co_await boost::asio::async_read(sock, boost::asio::buffer(&dTR_byte, 1), use_awaitable);

        key.cws[i].dSL = be2h64u(dSL_be);
        key.cws[i].dSR = be2h64u(dSR_be);
        key.cws[i].dTL = (dTL_byte != 0);
        key.cws[i].dTR = (dTR_byte != 0);
    }

    // Receive cwOut
    uint64_t cwOut_be;
    co_await boost::asio::async_read(sock, boost::asio::buffer(&cwOut_be, sizeof(cwOut_be)), use_awaitable);
    key.cwOut = be2h64u(cwOut_be);

    co_return key;
}

// ----------------------- DPF Evaluation (Assignment 3) -----------------------
// PRG utilities (same as gen_dpf.cpp)
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

// Evaluate DPF at single point
static uint64_t evalDPF(const DPFKey &key, uint64_t x, int nbits){
    uint64_t s = key.s0; 
    bool t = key.t0;

    for (int i = 0; i < nbits; ++i){
        PRGOut g = G(s);
        uint64_t sL = g.sL, sR = g.sR; 
        bool tL = g.tL, tR = g.tR;
        const DPFCorrectionWord &cw = key.cws[i];
        if (t){ 
            sL ^= cw.dSL; tL ^= cw.dTL; 
            sR ^= cw.dSR; tR ^= cw.dTR; 
        }
        int b = bit_at(x, i, nbits);
        if (b == 0){ s = sL; t = tL; } 
        else { s = sR; t = tR; }
    }

    if (t) s ^= key.cwOut;
    uint64_t y = s; // v_from_seed is identity
    if (key.t0) y = 0ull - y; // negate for party 1
    return y;
}

// Evaluate DPF over full domain
static std::vector<uint64_t> evalFullDPF(const DPFKey &key, uint64_t domain_size, int nbits){
    std::vector<uint64_t> result(domain_size);
    for (uint64_t x = 0; x < domain_size; ++x){
        result[x] = evalDPF(key, x, nbits);
    }
    return result;
}

// ----------------------- Assignment 1: User Profile Update -----------------------
static awaitable<void> update_user_profile_secure(const std::vector<long long>& query, 
                                                    const int qidx,
                                                    DuAtAllahClient& s, 
                                                    std::vector<DuAtAllahMultClient>& vmuls,
                                                    tcp::socket& peer_sock) {
    const long long user_idx = static_cast<long long>(query[0]);
    std::cout << "Updating user profile for user #" << user_idx << "\n";

    // Read current user share
    const std::string U_path = user_matrix_path();
    random_vector user_share = read_row_from_matrix_file(U_path, user_idx);

    // Extract item profile from query (query format: [user_idx, item_idx, v[0], v[1], ..., v[k-1]])
    random_vector item_share(query.size() - 2);
    for (size_t i = 2; i < query.size(); ++i) {
        item_share[i - 2] = query[i];
    }

    int k = user_share.size();
    if ((int)item_share.size() != k || (int)s.X.size() != k) {
        throw std::runtime_error("Dimension mismatch in user profile update");
    }

    // Step 1: Compute dot product share using MPC
    long long dot_share = 0;
    for (int i = 0; i < k; ++i) {
        long long prod_share = co_await secure_mpc_multiplication(
            user_share[i], item_share[i], vmuls[i], peer_sock);
        dot_share += prod_share;
    }

    // Step 2: Compute (1 - <ui, vj>) share
    // In additive sharing: [1] = [1]_0 + [1]_1 where one party gets 1, other gets 0
#ifdef ROLE_p0
    long long one_minus_dot_share = 1 - dot_share;
#else
    long long one_minus_dot_share = -dot_share;
#endif

    // Step 3: Compute update M = vj * (1 - <ui, vj>)
    // Need k more multiplications
    std::vector<long long> update_share(k);
    for (int i = 0; i < k; ++i) {
        update_share[i] = co_await secure_mpc_multiplication(
            item_share[i], one_minus_dot_share, vmuls[k + i], peer_sock);
    }

    // Step 4: Apply update to user profile
    std::vector<long long> new_user_share(k);
    for (int i = 0; i < k; ++i) {
        new_user_share[i] = user_share[i] + update_share[i];
    }

    // Step 5: Write back updated share
    update_row_in_matrix_file(U_path, user_idx, new_user_share);

    random_vector result(0);
    result.data = new_user_share;
    append_result_share_to_file(qidx, result, user_idx);

    std::cout << "User profile #" << user_idx << " updated successfully\n";
    co_return;
}

// ----------------------- Assignment 3: Item Profile Update with DPF -----------------------
static awaitable<void> update_item_profile_with_dpf(const std::vector<long long>& query,
                                                      const int qidx,
                                                      DuAtAllahClient& s,
                                                      std::vector<DuAtAllahMultClient>& vmuls,
                                                      tcp::socket& peer_sock,
                                                      tcp::socket& p2_sock,
                                                      int n_items) {
    const long long user_idx = static_cast<long long>(query[0]);
    const long long item_idx = static_cast<long long>(query[1]);

    std::cout << "Assignment 3: Updating item profile #" << item_idx << " (query by user #" << user_idx << ")\n";

    // Read user profile share
    const std::string U_path = user_matrix_path();
    random_vector user_share = read_row_from_matrix_file(U_path, user_idx);

    // Read item profile share
    const std::string V_path = item_matrix_path();
    random_vector item_share = read_row_from_matrix_file(V_path, item_idx);

    int k = user_share.size();
    if ((int)item_share.size() != k) {
        throw std::runtime_error("Dimension mismatch in item profile update");
    }

    // Step 1: Receive DPF key from user (via P2)
    // The user sends different keys to each server
    std::cout << "  Receiving DPF key from user...\n";
    DPFKey dpf_key = co_await recv_dpf_key(p2_sock);

    // Step 2: Compute local share of update value M = ui * (1 - <ui, vj>)
    std::cout << "  Computing update value share...\n";

    // Compute dot product share
    long long dot_share = 0;
    for (int i = 0; i < k; ++i) {
        long long prod_share = co_await secure_mpc_multiplication(
            user_share[i], item_share[i], vmuls[i], peer_sock);
        dot_share += prod_share;
    }

    // Compute (1 - <ui, vj>) share
#ifdef ROLE_p0
    long long one_minus_dot_share = 1 - dot_share;
#else
    long long one_minus_dot_share = -dot_share;
#endif

    // Compute M share = ui * (1 - <ui, vj>)
    std::vector<long long> M_share_vec(k);
    for (int i = 0; i < k; ++i) {
        M_share_vec[i] = co_await secure_mpc_multiplication(
            user_share[i], one_minus_dot_share, vmuls[k + i], peer_sock);
    }

    // Step 3: Adjust the DPF final correction word
    // Each server sends (M_b - FCW_b) to the other
    std::cout << "  Adjusting DPF correction word...\n";

    // For each dimension, we need to adjust the DPF
    // We'll process all k dimensions
    std::vector<uint64_t> adjusted_cwOut(k);

    for (int dim = 0; dim < k; ++dim) {
        uint64_t my_M = static_cast<uint64_t>(M_share_vec[dim]);
        uint64_t my_FCW = dpf_key.cwOut;

        // Compute my_M - my_FCW (in Z_2^64)
        uint64_t my_diff = my_M - my_FCW;

        // Exchange with peer
        uint64_t peer_diff;
#ifdef ROLE_p0
        uint64_t my_diff_be = h2be64u(my_diff);
        co_await boost::asio::async_write(peer_sock, boost::asio::buffer(&my_diff_be, sizeof(my_diff_be)), use_awaitable);

        uint64_t peer_diff_be;
        co_await boost::asio::async_read(peer_sock, boost::asio::buffer(&peer_diff_be, sizeof(peer_diff_be)), use_awaitable);
        peer_diff = be2h64u(peer_diff_be);
#else
        uint64_t peer_diff_be;
        co_await boost::asio::async_read(peer_sock, boost::asio::buffer(&peer_diff_be, sizeof(peer_diff_be)), use_awaitable);
        peer_diff = be2h64u(peer_diff_be);

        uint64_t my_diff_be = h2be64u(my_diff);
        co_await boost::asio::async_write(peer_sock, boost::asio::buffer(&my_diff_be, sizeof(my_diff_be)), use_awaitable);
#endif

        // Compute adjusted FCW: FCWm = (M0 - FCW0) + (M1 - FCW1)
        adjusted_cwOut[dim] = my_diff + peer_diff;
    }

    // Step 4: Evaluate DPF with adjusted correction word and apply update
    std::cout << "  Evaluating DPF and applying update...\n";

    // Calculate nbits for domain size n_items
    int nbits = 0;
    uint64_t tmp = 1;
    while (tmp < (uint64_t)n_items) { tmp <<= 1; ++nbits; }

    // For each dimension, evaluate DPF and update item profiles
    for (int dim = 0; dim < k; ++dim) {
        // Create modified key with adjusted cwOut
        DPFKey modified_key = dpf_key;
        modified_key.cwOut = adjusted_cwOut[dim];

        // Evaluate DPF over full domain
        std::vector<uint64_t> dpf_output = evalFullDPF(modified_key, n_items, nbits);

        // Convert XOR shares to additive shares
        // Insecure method: P0 negates its output
        std::vector<long long> additive_update(n_items);
        for (int i = 0; i < n_items; ++i) {
            long long val = static_cast<long long>(dpf_output[i]);
#ifdef ROLE_p0
            additive_update[i] = -val; // P0 negates
#else
            additive_update[i] = val;  // P1 keeps as is
#endif
        }

        // Update all item profiles for this dimension
        for (int i = 0; i < n_items; ++i) {
            random_vector item_i = read_row_from_matrix_file(V_path, i);
            std::vector<long long> new_item(k);
            for (int d = 0; d < k; ++d) {
                if (d == dim) {
                    new_item[d] = item_i[d] + additive_update[i];
                } else {
                    new_item[d] = item_i[d];
                }
            }
            update_row_in_matrix_file(V_path, i, new_item);
        }
    }

    std::cout << "Item profile #" << item_idx << " updated successfully\n";
    co_return;
}

// ----------------------- Main execution loop -----------------------
awaitable<void> run(boost::asio::io_context& io_context) {
    tcp::resolver resolver(io_context);

    // Step 1: Connect to P2 and receive shares
    std::cout << "Connecting to P2...\n";
    tcp::socket server_sock = co_await setup_server_connection(io_context, resolver);
    std::vector<DuAtAllahClient> received_shares;
    std::vector<std::vector<DuAtAllahMultClient>> received_mul_shares;
    co_await recv_all_shares_from_P2(server_sock, received_shares, received_mul_shares);

    std::cout << (
#ifdef ROLE_p0
    "P0"
#else
    "P1"
#endif
    ) << " finished receiving shares from P2\n";

    // Step 2: Connect to peer
    std::cout << "Setting up peer connection...\n";
    tcp::socket peer_sock = co_await setup_peer_connection(io_context, resolver);

    // Step 3: Preprocessing barrier
    co_await barrier_prep(peer_sock);
    std::cout << "Preprocessing complete, ready to process queries\n";

    // Step 4: Read queries
    auto queries = read_queries_file(query_path());
    std::cout << "Read " << queries.size() << " queries\n";

    if (queries.size() > received_shares.size()) {
        std::cerr << "Warning: queries (" << queries.size() << ") > shares (" << received_shares.size()
                  << "); truncating to available shares.\n";
        queries.resize(received_shares.size());
    }

    // Determine number of items (read from item matrix file)
    int n_items = 0;
    {
        std::ifstream f(item_matrix_path());
        if (f) {
            int rows, cols;
            f >> rows >> cols;
            n_items = rows;
        }
    }
    std::cout << "Number of items in database: " << n_items << "\n";

    // Step 5: Process queries
    for (std::size_t i = 0; i < queries.size(); ++i) {
        std::cout << "\n=== Processing query #" << i << " ===\n";
        co_await barrier_query(peer_sock, static_cast<int>(i));

        // Assignment 1: User profile update
        co_await update_user_profile_secure(queries[i], i, received_shares[i], 
                                            received_mul_shares[i], peer_sock);

        // Assignment 3: Item profile update with DPF
        if (n_items > 0) {
            co_await update_item_profile_with_dpf(queries[i], i, received_shares[i],
                                                  received_mul_shares[i], peer_sock,
                                                  server_sock, n_items);
        }

        std::cout << "Query #" << i << " completed\n";
    }

    std::cout << "\nAll queries processed successfully!\n";
    co_return;
}

int main() {
    std::cout.setf(std::ios::unitbuf); // auto-flush cout for Docker logs
    boost::asio::io_context io_context(1);
    co_spawn(io_context, run(io_context), boost::asio::detached);
    io_context.run();
    return 0;
}
