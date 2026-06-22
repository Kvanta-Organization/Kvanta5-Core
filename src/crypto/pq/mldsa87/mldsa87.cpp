#include <crypto/pq/mldsa87/mldsa87.h>

extern "C" {
#include <crypto/pq/mldsa87/upstream/ref/api.h>
}



bool MLDSA87GenerateKeypair(std::vector<unsigned char>& pubkey, std::vector<unsigned char>& seckey)
{
    pubkey.resize(MLDSA87_PUBLIC_KEY_SIZE);
    seckey.resize(MLDSA87_SECRET_KEY_SIZE);

    static_assert(MLDSA87_PUBLIC_KEY_SIZE == pqcrystals_dilithium5_PUBLICKEYBYTES);
    static_assert(MLDSA87_SECRET_KEY_SIZE == pqcrystals_dilithium5_SECRETKEYBYTES);
    static_assert(MLDSA87_SIGNATURE_SIZE == pqcrystals_dilithium5_BYTES);

    return pqcrystals_dilithium5_ref_keypair(pubkey.data(), seckey.data()) == 0;
}


bool MLDSA87Sign(Span<const unsigned char> seckey,
                 Span<const unsigned char> msg,
                 std::vector<unsigned char>& sig_out)
{
    if (seckey.size() != MLDSA87_SECRET_KEY_SIZE) return false;

    sig_out.resize(MLDSA87_SIGNATURE_SIZE);
    size_t sig_len = sig_out.size();

    const unsigned char* ctx = nullptr;
    const size_t ctx_len = 0;

    const int ret = pqcrystals_dilithium5_ref_signature(
        sig_out.data(),
        &sig_len,
        msg.data(),
        msg.size(),
        ctx,
        ctx_len,
        seckey.data());

    if (ret != 0 || sig_len != MLDSA87_SIGNATURE_SIZE) {
        sig_out.clear();
        return false;
    }

    return true;
}

bool MLDSA87Verify(Span<const unsigned char> pubkey,
                   Span<const unsigned char> msg,
                   Span<const unsigned char> sig)
{
    if (pubkey.size() != MLDSA87_PUBLIC_KEY_SIZE) return false;
    if (sig.size() != MLDSA87_SIGNATURE_SIZE) return false;

    const unsigned char* ctx = nullptr;
    const size_t ctx_len = 0;

    return pqcrystals_dilithium5_ref_verify(
        sig.data(),
        sig.size(),
        msg.data(),
        msg.size(),
        ctx,
        ctx_len,
        pubkey.data()) == 0;
}

bool MLDSA87GenerateKeypairFromSeed(Span<const unsigned char> seed, std::vector<unsigned char>& pubkey, std::vector<unsigned char>& seckey)
{
    if (seed.size() != MLDSA87_SEED_SIZE) {
        return false;
    }

    pubkey.resize(MLDSA87_PUBLIC_KEY_SIZE);
    seckey.resize(MLDSA87_SECRET_KEY_SIZE);

    const int ret = crypto_sign_keypair_from_seed(pubkey.data(), seckey.data(), seed.data());
    if (ret != 0) {
        pubkey.clear();
        seckey.clear();
        return false;
    }

    return true;
}
