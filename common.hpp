#pragma once
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>
#include<vector>
#include<utility>
#include <random>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
namespace this_coro = boost::asio::this_coro;

#define UPPER_LIM 100
#define P0_USER_SHARES_FILE "/data/p0_shares/p0_U.txt"
#define P1_USER_SHARES_FILE "/data/p1_shares/p1_U.txt"
#define P0_QUERIES_SHARES_FILE "/data/p0_shares/p0_queries.txt"
#define P1_QUERIES_SHARES_FILE "/data/p1_shares/p1_queries.txt"
#define P0_MULT_SHARES_FILE "/data/p0_shares/p0_mult.txt"
#define P1_MULT_SHARES_FILE "/data/p1_shares/p1_mult.txt"

inline uint32_t random_uint32() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;
    return dis(gen);
}

// Blind by XOR mask
inline uint32_t blind_value(uint32_t v) {
    return v ^ 0xDEADBEEF;
}

struct random_vector{
    std::vector<long long> data;

    random_vector() = default;  

    explicit random_vector(size_t k) : data(k) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(-UPPER_LIM, UPPER_LIM);

        // Fill vector with random values
        for (auto &x : data) {
            x = dist(gen);
        }
    }

    long long& operator[](size_t i) {
        return data[i];
    }

    const long long& operator[](size_t i) const {
        return data[i];
    }

    // Get length
    size_t size() const {
        return data.size();
    }

    long long dot_product(random_vector& x){
        long long val = 0;
        long long k = this->data.size();
        for(int i=0;i<k;i++){
            val += data[i]*(x.data[i]);
        }
        return val;
    }

    void resize(size_t len){
        data.resize(len);
    }

    bool empty() const noexcept{
        return data.empty();
    }
};

random_vector operator+(random_vector& a, random_vector& b){
    if(a.data.size()!=b.data.size()){
        throw std::runtime_error("Vectors cannot be added size mismatch");
    }
    random_vector ans(0);
    ans.resize(a.data.size());
    for(int i=0;i<a.data.size();i++){
        ans[i] = a[i] + b[i];
    }
    return ans;
}

void operator*=(random_vector& a, long long& scale){
    random_vector ans(0);
    ans.resize(a.data.size());
    for(int i=0;i<a.data.size();i++){
        a[i] = a[i]*scale;
    }
}

struct DuAtAllahClient{
    random_vector X, Y;
    long long z;

    DuAtAllahClient(int k) : X(k), Y(k){
        z = (long long)random_uint32();
    }
};

struct DuAtAllahServer{
    random_vector X0, X1, Y0, Y1;
    long long alpha;

    DuAtAllahServer(int k) : X0(k), X1(k), Y0(k), Y1(k) {
        alpha = random_uint32();
    }

    std::pair<DuAtAllahClient,DuAtAllahClient> generate_client_shares(){
        int k = X0.size();
        DuAtAllahClient client0(k), client1(k);
        client0.X = X0;
        client0.Y = Y0;
        client0.z = X0.dot_product(Y1) + alpha;
        client1.X = X1;
        client1.Y = Y1;
        client1.z = Y0.dot_product(X1) - alpha;
        return {client0, client1};
    }
};

struct DuAtAllahMultClient{
    long long x, y, z;

    DuAtAllahMultClient(): x(0), y(0), z(0) {}
};

struct DuAtAllahMultServer{
    long long x0, x1, y1, y0;
    long long alpha;

    DuAtAllahMultServer() {
        x0 = random_uint32();
        x1 = random_uint32();
        y0 = random_uint32();
        y1 = random_uint32();
        alpha = random_uint32();
    }
};


