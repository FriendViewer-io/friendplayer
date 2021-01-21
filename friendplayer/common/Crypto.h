#pragma once

#include <cryptopp\integer.h>
#include <cryptopp\osrng.h>

class Crypto {
public:
    Crypto(int gen_bit_value);
    Crypto(const std::string& p_byte, const std::string& q_byte, const std::string& g_byte);

    void SharedKeyAgreement(CryptoPP::SecByteBlock other_pub_key);
    void Encrypt(const std::string& in, std::string& out);
    void Decrypt(const std::string& in, std::string& out);

    std::string GetPublicKey() const;
    std::string P() const;
    std::string Q() const;
    std::string G() const;

private:
    CryptoPP::AutoSeededRandomPool rng;

    CryptoPP::Integer p;
    CryptoPP::Integer q;
    CryptoPP::Integer g;

    CryptoPP::SecByteBlock private_key;
    CryptoPP::SecByteBlock public_key;

    CryptoPP::SecByteBlock shared_key;
    CryptoPP::SecByteBlock password;
};