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

using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;


#if !defined(ROLE_p0) && !defined(ROLE_p1)
#error "ROLE must be defined as ROLE_p0 or ROLE_p1"
#endif

// --- endian helpers (wire = big-endian) ---
static inline uint64_t h2be64(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}
static inline uint64_t be2h64(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}
static inline uint32_t h2be32(uint32_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(x);
#else
    return x;
#endif
}
static inline uint32_t be2h32(uint32_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(x);
#else
    return x;
#endif
}

// ----------------------- Helper coroutines -----------------------
awaitable<void> send_coroutine(tcp::socket& sock, uint32_t value) {
    co_await boost::asio::async_write(sock, boost::asio::buffer(&value, sizeof(value)), use_awaitable);
}

awaitable<void> recv_coroutine(tcp::socket& sock, uint32_t& out) {
    co_await boost::asio::async_read(sock, boost::asio::buffer(&out, sizeof(out)), use_awaitable);
}

// Blinded exchange between peers
awaitable<void> exchange_blinded(tcp::socket socket, uint32_t value_to_send) {
    uint32_t blinded = blind_value(value_to_send);
    uint32_t received;

    co_await send_coroutine(socket, blinded);
    co_await recv_coroutine(socket, received);

    std::cout << "Received blinded value from other party: " << received << std::endl;
    co_return;
}

static inline const char* user_matrix_path() {
#ifdef ROLE_p0
    return P0_USER_SHARES_FILE;
#else
    return P1_USER_SHARES_FILE;
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

// Setup connection to P2 (P0/P1 act as clients, P2 acts as server)
awaitable<tcp::socket> setup_server_connection(boost::asio::io_context& io_context, tcp::resolver& resolver) {
    tcp::socket sock(io_context);

    // Connect to P2
    auto endpoints_p2 = resolver.resolve("p2", "9002");
    co_await boost::asio::async_connect(sock, endpoints_p2, use_awaitable);

    co_return sock;
}

// Receive random value from P2
awaitable<uint32_t> recv_from_P2(tcp::socket& sock) {
    uint32_t received;
    co_await recv_coroutine(sock, received);
    co_return received;
}

// Setup peer connection between P0 and P1
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
static constexpr const char* SHARE_LOG_PATH  = "/data/client0.shares";
static constexpr const char* RESULT_LOG_PATH = "/data/client0.results";
#else
static constexpr const char* SHARE_LOG_PATH  = "/data/client1.shares";
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
    int v;
    while (iss >> v) out.push_back(v);
    random_vector rv(out.size());
    return rv;
}

awaitable<void> recv_all_shares_from_P2(tcp::socket& sock, std::vector<DuAtAllahClient>& store) {
    boost::asio::streambuf buf;
    int idx = 0;
    for (;;) {
        std::string line1;
        // skip empty lines
        do { co_await read_line(sock, buf, line1); } while (line1.empty());

        if (line1 == "OK") break;

        random_vector rv_temp = parse_int_line(line1);
        DuAtAllahClient s(rv_temp.size());
        s.X = rv_temp;

        std::string line2; co_await read_line(sock, buf, line2);
        s.Y = parse_int_line(line2);

        std::string line3; co_await read_line(sock, buf, line3);
        { std::istringstream iss(line3); iss >> s.z; }

        // separator (can be blank, read and drop)
        std::string sep; co_await read_line(sock, buf, sep);

        append_my_share_to_file(s,idx++); //Store in a file for debugging

        store.emplace_back(std::move(s));
        // print_share_debug(store.back(), store.size());
    }
    std::cout << "Total shares received from P2: " << store.size() << std::endl;
    co_return;
}

// ----------------------- Shares Read Write -----------------------
static random_vector
read_row_from_matrix_file(const std::string& path, int row_index /*0-based*/) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to open " + path);

    int rows=0, cols=0;
    if (!(f >> rows >> cols)) {
        throw std::runtime_error("Bad header in " + path);
    }
    if (row_index < 0 || row_index >= rows) {
        throw std::runtime_error("Row index out of range in " + path);
    }

    // skip rows before target
    std::string line;
    std::getline(f, line); // consume end of header line
    for (int r = 0; r < row_index; ++r) {
        std::getline(f, line);
        if (!f) throw std::runtime_error("Unexpected EOF while skipping rows in " + path);
    }

    // read the row
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
    random_vector vec(row.size());
    vec.data = row;
    return vec;
}

// --- optional: write back one row (safe rewrite via temp file+rename) ---
static void
update_row_in_matrix_file(const std::string& path, int row_index,
                          const std::vector<long long>& newrow) {
    // Read entire matrix
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

    // Update row
    M[row_index] = newrow;

    // Rewrite atomically
    const std::string tmp = path + ".tmp";
    {
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
    }
    // Replace
    std::remove(path.c_str()); // ignore result
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        throw std::runtime_error("Failed to rename " + tmp + " -> " + path);
    }
}

// --- header for exchanging two vectors ---
struct VecPairHeader {
    uint32_t magic;     // 'DXCH' = 0x44584348
    uint32_t version;   // 1
    uint32_t query_idx; // optional sanity
    uint32_t len_x;
    uint32_t len_y;
};

// async send two int64 vectors (big-endian on wire)
static awaitable<void> send_two_vecs_async(
    tcp::socket& sock,
    uint32_t query_idx,
    const random_vector &vx,
    const random_vector &vy)
{
    VecPairHeader h{
        h2be32(0x44584348u),
        h2be32(1u),
        h2be32(query_idx),
        h2be32(static_cast<uint32_t>(vx.data.size())),
        h2be32(static_cast<uint32_t>(vy.data.size()))
    };

    co_await boost::asio::async_write(sock, boost::asio::buffer(&h, sizeof(h)), use_awaitable);

    if (!vx.empty()) {
        std::vector<uint64_t> tmp(vx.data.size());
        for (size_t i = 0; i < vx.data.size(); ++i) tmp[i] = h2be64(static_cast<uint64_t>(vx[i]));
        co_await boost::asio::async_write(sock, boost::asio::buffer(tmp.data(), tmp.size()*sizeof(uint64_t)), use_awaitable);
    }
    if (!vy.empty()) {
        std::vector<uint64_t> tmp(vy.data.size());
        for (size_t i = 0; i < vy.data.size(); ++i) tmp[i] = h2be64(static_cast<uint64_t>(vy[i]));
        co_await boost::asio::async_write(sock, boost::asio::buffer(tmp.data(), tmp.size()*sizeof(uint64_t)), use_awaitable);
    }
    co_return;
}

// async recv two int64 vectors (big-endian on wire)
static awaitable<void> recv_two_vecs_async(
    tcp::socket& sock,
    uint32_t& query_idx_out,
    random_vector &vx_out,
    random_vector &vy_out)
{
    VecPairHeader h{};
    co_await boost::asio::async_read(sock, boost::asio::buffer(&h, sizeof(h)), use_awaitable);

    const uint32_t magic = be2h32(h.magic);
    const uint32_t ver   = be2h32(h.version);
    const uint32_t qidx  = be2h32(h.query_idx);
    const uint32_t lx    = be2h32(h.len_x);
    const uint32_t ly    = be2h32(h.len_y);
    if (magic != 0x44584348u || ver != 1u) throw std::runtime_error("bad exchange header");

    query_idx_out = qidx;
    vx_out.data.resize(lx);
    vy_out.data.resize(ly);

    if (lx) {
        std::vector<uint64_t> tmp(lx);
        co_await boost::asio::async_read(sock, boost::asio::buffer(tmp.data(), tmp.size()*sizeof(uint64_t)), use_awaitable);
        for (size_t i=0;i<tmp.size();++i) vx_out[i] = static_cast<long long>(be2h64(tmp[i]));
    }
    if (ly) {
        std::vector<uint64_t> tmp(ly);
        co_await boost::asio::async_read(sock, boost::asio::buffer(tmp.data(), tmp.size()*sizeof(uint64_t)), use_awaitable);
        for (size_t i=0;i<tmp.size();++i) vy_out[i] = static_cast<long long>(be2h64(tmp[i]));
    }
    co_return;
}

// ----------------------- Barriers to keep both clients in lockstep -----------------------
awaitable<void> barrier_prep(tcp::socket& peer) {
#ifdef ROLE_p0
    uint32_t code = 1; // PREP
    co_await send_coroutine(peer, code);
    uint32_t ack; co_await recv_coroutine(peer, ack);
#else
    uint32_t code; co_await recv_coroutine(peer, code);
    co_await send_coroutine(peer, code);
#endif
    co_return;
}

awaitable<void> barrier_query(tcp::socket& peer, uint32_t idx) {
#ifdef ROLE_p0
    uint32_t code = 2; // QUERY
    co_await send_coroutine(peer, code);
    co_await send_coroutine(peer, idx);
    uint32_t code2, idx2;
    co_await recv_coroutine(peer, code2);
    co_await recv_coroutine(peer, idx2);
    if (code2 != 2 || idx2 != idx) {
        std::cerr << "Barrier mismatch (sent idx=" << idx << ", got idx=" << idx2 << ")\n";
    }
#else
    uint32_t code_in, idx_in;
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

    //Temp Solution make update to item oblivious
    long long expected_len = k+1;
    for (long long i = 0; i < q; ++i) {
        std::vector<long long> v(expected_len);
        for (int j = 0; j < expected_len; ++j) {
            if (!(fin >> v[j])) {
                std::ostringstream oss;
                oss << path <<": not enough numbers on query " << i << " (expected " << expected_len << ")\n";
                throw std::runtime_error(oss.str());
                // v.resize(j);
                // break;
            }
        }
        if ((int)v.size() == expected_len) queries.emplace_back(std::move(v));
        else break;
    }
    return queries;
}

// ----------------------- MPC dot product stub -----------------------
static boost::asio::awaitable<long long> mpc_dot_product_async(const std::vector<long long>& q, const int qidx,
                                 DuAtAllahClient& s,
                                 tcp::socket& peer_sock /* use for your protocol */) {
    

    const long long user_idx = static_cast<long long>(q[0]);
    // const int item_idx = static_cast<int>(q[1]);

    std::cout<<"query is by user #"<< user_idx <<"\n";

    // 1) Load my shares for this (user,row) and (item,row) from files (based on ROLE)
    const std::string U_path = user_matrix_path();
    // const std::string V_path = item_matrix_path();

    random_vector user_share = read_row_from_matrix_file(U_path, user_idx);
    // random_vector item_share = read_row_from_matrix_file(V_path, item_idx);
    random_vector item_share(q.size()-1);

    for(int i=1 ; i<q.size() ; i++) item_share[i-1] = q[i];

    // 2) Prepare a vector derived from s to exchange with the peer.
    //    (Pick X or Y or any aux you require; here we send s.X as an example.)
    random_vector my_x_sums, my_y_sums;
    my_x_sums.data.reserve(s.X.size());
    my_y_sums.data.reserve(s.X.size());

    my_x_sums = s.X + user_share;
    my_y_sums = s.Y + item_share;
    // for (int i=0; i<s.X.size(); i++) my_x_sums = (s.X) + (user_share);
    // for (int i=0; i<s.Y.size(); i++) my_y_sums = (s.Y) + (item_share);

    uint32_t peer_qidx = qidx, qidx_for_sanity = qidx;
    random_vector peer_x_sums, peer_y_sums;

    #ifdef ROLE_p0
    co_await send_two_vecs_async(peer_sock, qidx_for_sanity, my_x_sums, my_y_sums);
    co_await recv_two_vecs_async(peer_sock, peer_qidx, peer_x_sums, peer_y_sums);
    #else
    co_await recv_two_vecs_async(peer_sock, peer_qidx, peer_x_sums, peer_y_sums);
    co_await send_two_vecs_async(peer_sock, qidx_for_sanity, my_x_sums, my_y_sums);
    #endif

    if (peer_qidx != qidx_for_sanity) throw std::runtime_error("peer query index mismatch");
    if (peer_x_sums.size() != my_x_sums.size() || peer_y_sums.size() != my_y_sums.size())
        throw std::runtime_error("peer vector length mismatch");

    random_vector addition_temp;
    addition_temp = item_share + peer_y_sums;
    long long delta = user_share.dot_product(addition_temp) - item_share.dot_product(peer_x_sums) + s.z;
    long long factor = (1-delta);
    item_share *= factor;
    user_share = user_share + item_share;
    
    append_result_share_to_file(qidx, user_share, user_idx);
    update_row_in_matrix_file(U_path, user_idx, user_share.data);

    co_return 0LL;
}

// ----------------------- Main protocol -----------------------
awaitable<void> run(boost::asio::io_context& io_context) {
    tcp::resolver resolver(io_context);

    // Step 1: connect to P2 and receive ALL shares (preprocessing)
    tcp::socket server_sock = co_await setup_server_connection(io_context, resolver);
    std::vector<DuAtAllahClient> received_shares;
    co_await recv_all_shares_from_P2(server_sock, received_shares);

    std::cout << (
#ifdef ROLE_p0
        "P0"
#else
        "P1"
#endif
    ) << " finished receiving shares from P2\n";

    // Step 2: connect to peer (P0 <-> P1)
    tcp::socket peer_sock = co_await setup_peer_connection(io_context, resolver);

    // Step 3: PREPROCESSING BARRIER — both must reach here before starting queries
    co_await barrier_prep(peer_sock);

    // Step 4: read queries (same file path inside both containers)
    const int k = received_shares.empty() ? 0 : static_cast<int>(received_shares.front().X.size());
    auto queries = read_queries_file(query_path());

    std::cout<<"Read "<<queries.size()<<" queries from the file\n";
    if (k == 0 || queries.empty()) {
        std::cerr << "No shares or no queries to process (k=" << k << ", queries=" << queries.size() << ")\n";
        co_return;
    }
    if (queries.size() > received_shares.size()) {
        std::cerr << "Warning: queries (" << queries.size() << ") > shares (" << received_shares.size()
                  << "); truncating to available shares.\n";
        queries.resize(received_shares.size());
    }

    // Step 5: process each query in strict lockstep
    for (std::size_t i = 0; i < queries.size(); ++i) {
        // per-query barrier to ensure both parties use the same query index
        std::cout<<"processing query #"<<i<<"\n";
        co_await barrier_query(peer_sock, static_cast<uint32_t>(i));

        // compute the MPC dot product share
        co_await mpc_dot_product_async(queries[i], i, received_shares[i], peer_sock);

        // debug print
        std::cout << "Processed query #" << i << "\n";
    }

    co_return;
}

int main() {
    std::cout.setf(std::ios::unitbuf); // auto-flush cout for Docker logs
    boost::asio::io_context io_context(1);
    co_spawn(io_context, run(io_context), boost::asio::detached);
    io_context.run();
    return 0;
}
