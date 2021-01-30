#include "Crypto.h"
#include <cryptopp\aes.h>
#include <cryptopp\gcm.h>
#include <cryptopp\nbtheory.h>
#include <cryptopp\dh.h>

/**
 *  @int gen_bit_value: creates a l-bit value to generate a prime p.
 *  Generates public info: p, q, and g 
 *  creates a public private key pair with diffie-hellman
 * */
Crypto::Crypto(int gen_bit_value) {
    CryptoPP::PrimeAndGenerator gen;
    gen.Generate(1, rng, gen_bit_value, 160);
    
    p = gen.Prime();
    q = gen.SubPrime();
    g = gen.Generator();
    
    CryptoPP::DH dh;
    dh.AccessGroupParameters().Initialize(p, q, g);

    private_key = CryptoPP::SecByteBlock(dh.PrivateKeyLength());
    public_key = CryptoPP::SecByteBlock(dh.PublicKeyLength());

    dh.GenerateKeyPair(rng, private_key, public_key);
}

Crypto::Crypto(const std::string& p_byte, const std::string& q_byte, const std::string& g_byte)
    : p(reinterpret_cast<const CryptoPP::byte*>(p_byte.data()), p_byte.size()),
      q(reinterpret_cast<const CryptoPP::byte*>(q_byte.data()), q_byte.size()),
      g(reinterpret_cast<const CryptoPP::byte*>(g_byte.data()), g_byte.size()) {
    CryptoPP::DH dh;
    dh.AccessGroupParameters().Initialize(p, q, g);

    private_key = CryptoPP::SecByteBlock(dh.PrivateKeyLength());
    public_key = CryptoPP::SecByteBlock(dh.PublicKeyLength());

    dh.GenerateKeyPair(rng, private_key, public_key);
}

/** 
* @return int id of the shared secret
* */
void Crypto::SharedKeyAgreement(const std::string& other_pub_key) {
    CryptoPP::DH dh;
    dh.AccessGroupParameters().Initialize(p, q, g);

    shared_key = CryptoPP::SecByteBlock(dh.AgreedValueLength());
    dh.Agree(shared_key, private_key, reinterpret_cast<const CryptoPP::byte*>(other_pub_key.data()));

    // Faster way of getting keys
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    CryptoPP::SHA256 hash;
    hash.CalculateDigest(digest, shared_key, shared_key.size());
    int L_128 = CryptoPP::SHA256::DIGESTSIZE / 2;
    CryptoPP::SecByteBlock key(digest, L_128);
    password = key;
}

/**
* Give out 32 more bytes to use for iv
* */
void Crypto::Encrypt(const std::string& in, std::string& out) {
    int L_128 = CryptoPP::SHA256::DIGESTSIZE / 2;
    
    out.resize(in.size() + L_128);
    CryptoPP::SecByteBlock iv(L_128);
    rng.GenerateBlock(iv, L_128);
    
    CryptoPP::SecByteBlock key(password, L_128);
    CryptoPP::GCM<CryptoPP::AES>::Encryption gcm_encryption;

    gcm_encryption.SetKeyWithIV(key, key.size(), iv);
    std::copy(iv.begin(), iv.end(), out.data());

    gcm_encryption.ProcessData(reinterpret_cast<CryptoPP::byte*>(out.data()) + L_128,
        reinterpret_cast<const CryptoPP::byte*>(in.data()), in.size());
}

void Crypto::Decrypt(const std::string& in, std::string& out) {
    int L_128 = CryptoPP::SHA256::DIGESTSIZE / 2;

    // Take out vector from data
    CryptoPP::SecByteBlock iv(reinterpret_cast<const CryptoPP::byte*>(in.data()), L_128);
    CryptoPP::SecByteBlock key(password, L_128);
    CryptoPP::GCM<CryptoPP::AES>::Decryption gcm_decryption;
    
    out.resize(in.size() - L_128);
    gcm_decryption.SetKeyWithIV(key, key.size(), iv);
    gcm_decryption.ProcessData(reinterpret_cast<CryptoPP::byte*>(out.data()), 
        reinterpret_cast<const CryptoPP::byte*>(in.data()) + L_128, in.size() - L_128);
}

void Crypto::DecryptInPlace(std::string& inout) {
    int L_128 = CryptoPP::SHA256::DIGESTSIZE / 2;

    // Take out vector from data
    CryptoPP::SecByteBlock iv(reinterpret_cast<const CryptoPP::byte*>(inout.data()), L_128);
    CryptoPP::SecByteBlock key(password, L_128);
    CryptoPP::GCM<CryptoPP::AES>::Decryption gcm_decryption;

    gcm_decryption.SetKeyWithIV(key, key.size(), iv);
    gcm_decryption.ProcessData(reinterpret_cast<CryptoPP::byte*>(inout.data()), 
        reinterpret_cast<const CryptoPP::byte*>(inout.data()) + L_128, inout.size() - L_128);
    inout.resize(inout.size() - L_128);
}

std::string Crypto::GetPublicKey() const {
    std::string ret;
    ret.resize(public_key.SizeInBytes());
    ret.assign(public_key.data(), public_key.data() + public_key.SizeInBytes());
    return std::move(ret);
}

std::string Crypto::P() const {
    std::string ret;
    ret.resize(p.MinEncodedSize());
    p.Encode(reinterpret_cast<CryptoPP::byte*>(ret.data()), ret.size(), CryptoPP::Integer::UNSIGNED);
    return std::move(ret);
}

std::string Crypto::Q() const {
    std::string ret;
    ret.resize(q.MinEncodedSize());
    q.Encode(reinterpret_cast<CryptoPP::byte*>(ret.data()), ret.size(), CryptoPP::Integer::UNSIGNED);
    return std::move(ret);
}   

std::string Crypto::G() const {
    std::string ret;
    ret.resize(g.MinEncodedSize());
    g.Encode(reinterpret_cast<CryptoPP::byte*>(ret.data()), ret.size(), CryptoPP::Integer::UNSIGNED);
    return std::move(ret);
}