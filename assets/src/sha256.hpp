// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Streaming SHA-256 (OpenSSL EVP), private to wc98_assets. Same construction as tools/inventory's
// one-shot EVP_Digest; this variant streams whole files without loading them into memory.
#include <openssl/evp.h>
#include <cstdio>
#include <memory>
#include <string>

namespace wc98::assets {

class Sha256 {
public:
    Sha256() { EVP_DigestInit_ex(ctx_.get(), EVP_sha256(), nullptr); }
    void update(const void* d, std::size_t n) { EVP_DigestUpdate(ctx_.get(), d, n); }
    std::string hex() {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned len = 0;
        EVP_DigestFinal_ex(ctx_.get(), md, &len);
        std::string s(len * 2, '\0');
        for (unsigned i = 0; i < len; ++i) std::snprintf(&s[i * 2], 3, "%02x", md[i]);
        return s;
    }

private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx_{EVP_MD_CTX_new(), EVP_MD_CTX_free};
};

} // namespace wc98::assets
