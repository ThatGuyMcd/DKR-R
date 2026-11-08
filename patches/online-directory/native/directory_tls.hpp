#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincrypt.h>
#endif

namespace dkr::online {
// Same public GTS Root R4 fallback as the accepted Quick Join TLS repair.
inline constexpr std::string_view DirectoryFallbackRoot=R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";
inline std::string directory_ca_bundle(){
    if(const char* configured=std::getenv("SSL_CERT_FILE");configured&&*configured){
        std::error_code error;if(std::filesystem::is_regular_file(configured,error))return configured;
    }
#ifdef _WIN32
    // libdatachannel requires an explicit CA source on Windows. Export only
    // public ROOT certificates; no personal certificates/private keys are read.
    struct Store {HCERTSTORE value=CertOpenSystemStoreW(0,L"ROOT");~Store(){if(value)CertCloseStore(value,0);}} store;
    std::string bundle;
    if(store.value){
        PCCERT_CONTEXT certificate=nullptr;
        while((certificate=CertEnumCertificatesInStore(store.value,certificate))!=nullptr){
            DWORD size=0;
            if(!CryptBinaryToStringA(certificate->pbCertEncoded,certificate->cbCertEncoded,CRYPT_STRING_BASE64HEADER,nullptr,&size)||size>1024*1024)continue;
            std::string pem(size,'\0');
            if(CryptBinaryToStringA(certificate->pbCertEncoded,certificate->cbCertEncoded,CRYPT_STRING_BASE64HEADER,pem.data(),&size)){
                while(!pem.empty()&&pem.back()=='\0')pem.pop_back();bundle+=pem;bundle+='\n';
            }
        }
    }
    if(!bundle.empty())return bundle;
#else
    for(const char* path:{"/etc/ssl/certs/ca-certificates.crt","/etc/ssl/cert.pem","/etc/pki/tls/certs/ca-bundle.crt","/etc/ssl/ca-bundle.pem"}){
        std::error_code error;if(std::filesystem::is_regular_file(path,error))return path;
    }
#endif
    return std::string(DirectoryFallbackRoot);
}
}
