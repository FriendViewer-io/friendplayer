//#include <iostream>
//
//bool diffiehellman(){
//    CryptoPP::AutoSeededRandomPool rng;
//    CryptoPP::PrimeAndGenerator gen;
//    gen.Generate(1, rng, 1024, 160);
//    
//
//    auto p = gen.Prime();
//    auto q = gen.SubPrime();
//    auto g = gen.Generator();
//
//    CryptoPP::DH dhA;
//    dhA.AccessGroupParameters().Initialize(p, q, g);
//
//    
//    CryptoPP::DH dhB;
//    dhB.AccessGroupParameters().Initialize(p, q, g);
//
//    CryptoPP::SecByteBlock privateA(dhA.PrivateKeyLength()), 
//                           publicA(dhA.PublicKeyLength()),
//                           privateB(dhB.PrivateKeyLength()),
//                           publicB(dhB.PublicKeyLength());
//
//    
//    dhA.GenerateKeyPair(rng, privateA, publicA);
//    dhB.GenerateKeyPair(rng, privateB, publicB);
//    
//    CryptoPP::SecByteBlock sharedA(dhA.AgreedValueLength()), sharedB(dhB.AgreedValueLength());
//
//    dhA.Agree(sharedA, privateA, publicB);
//    dhB.Agree(sharedB, privateB, publicA);
//
//    CryptoPP::Integer a, b;
//    
//    a.Decode(sharedA, sharedA.SizeInBytes());
//    b.Decode(sharedB, sharedB.SizeInBytes());
//
//    std::cout << sharedA.SizeInBytes() << std::endl;
//
//    return true;
//}



// AES
    //CryptoPP::AutoSeededRandomPool rnd;

    //CryptoPP::SecByteBlock key(0x00, CryptoPP::AES::DEFAULT_KEYLENGTH);
    //rnd.GenerateBlock( key, key.size() );

    //CryptoPP::SecByteBlock iv(CryptoPP::AES::BLOCKSIZE);
    //rnd.GenerateBlock(iv, iv.size());

    //

    //CryptoPP::byte plainText1[] = "Hello! How are you - message 123";
    //CryptoPP::byte plainText2[] = "Hello! How ame you - message 134";
    //CryptoPP::byte plainText3[] = "Hello! How aqe you - message 145";
    //size_t messageLen = std::strlen((char*)plainText1);

    //

    //CryptoPP::GCM<CryptoPP::AES>::Encryption cfbEncryption;
    //cfbEncryption.SetKeyWithIV(key, key.size(), iv);
    //cfbEncryption.ProcessData(plainText1, plainText1, messageLen);
    //cfbEncryption.ProcessData(plainText2, plainText2, messageLen);
    //cfbEncryption.ProcessData(plainText3, plainText3, messageLen);

    //std::cout << plainText1 << std::endl;
    //
    //std::fill(plainText1, plainText1 + 5, 0);

    //CryptoPP::GCM<CryptoPP::AES>::Decryption cfbDecryption;
    //cfbDecryption.SetKeyWithIV(key, key.size(), iv);
    //cfbDecryption.ProcessData(plainText1, plainText1, messageLen);
    //CryptoPP::byte bunk[32];
    //cfbDecryption.ProcessData(bunk, bunk, 32);
    //cfbDecryption.ProcessData(plainText3, plainText3, messageLen);

    //std::cout << plainText1 << std::endl;
    //std::cout << plainText3 << std::endl;