#ifndef KVANTA5_CRYPTO_PQ_MLDSA87_H
#define KVANTA5_CRYPTO_PQ_MLDSA87_H

#include <span.h>

#include <cstddef>
#include <vector>

static constexpr size_t MLDSA87_PUBLIC_KEY_SIZE = 2592;
static constexpr size_t MLDSA87_SECRET_KEY_SIZE = 4896;
static constexpr size_t MLDSA87_SIGNATURE_SIZE = 4627;
static constexpr size_t MLDSA87_SEED_SIZE = 32;

bool MLDSA87GenerateKeypairFromSeed(Span<const unsigned char> seed, std::vector<unsigned char>& pubkey, std::vector<unsigned char>& seckey);
bool MLDSA87GenerateKeypair(std::vector<unsigned char>& pubkey, std::vector<unsigned char>& seckey);

bool MLDSA87Sign(
    Span<const unsigned char> seckey,
    Span<const unsigned char> msg,
    std::vector<unsigned char>& sig_out);

bool MLDSA87Verify(
    Span<const unsigned char> pubkey,
    Span<const unsigned char> msg,
    Span<const unsigned char> sig);

#endif // KVANTA5_CRYPTO_PQ_MLDSA87_H
