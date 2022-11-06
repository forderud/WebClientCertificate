#include <windows.h>
#include <ncrypt.h>

#include <functional>
#include <iostream>
#include <cassert>


void OpenCNGKey(const wchar_t* providername, const wchar_t* keyname, DWORD legacyKeySpec) {
    NCRYPT_PROV_HANDLE provider = 0;
    SECURITY_STATUS status = NCryptOpenStorageProvider(&provider, providername, 0);
    if (status != ERROR_SUCCESS)
        abort();

    NCRYPT_KEY_HANDLE key = 0;
    status = NCryptOpenKey(provider, &key, keyname, legacyKeySpec, NCRYPT_SILENT_FLAG);
    if (status == NTE_BAD_KEYSET)
        abort();
    if (status != ERROR_SUCCESS)
        abort();

    std::wcout << L"  CNG key access succeeded\n";

#if 0
    DWORD size = 0;
    status = NCryptGetProperty(key, NCRYPT_NAME_PROPERTY, nullptr, 0, &size, 0);
    if (status == NTE_NOT_FOUND)
        continue;
    if (status != ERROR_SUCCESS)
        abort();
    std::vector<BYTE> cert;
    cert.resize(size);
    NCryptGetProperty(key, NCRYPT_NAME_PROPERTY, &cert[0], size, &size, 0);
#endif

    NCryptFreeObject(key);

    NCryptFreeObject(provider);
}

/** CertGetCertificateContextProperty convenience wrapper.
    Expects the query to either succeed or fail with CRYPT_E_NOT_FOUND. */
static std::vector<BYTE> CertContextProperty(const CERT_CONTEXT& cert, DWORD prop) {
    DWORD buffer_len = 0;
    if (!CertGetCertificateContextProperty(&cert, prop, nullptr, &buffer_len)) {
        DWORD err = GetLastError();
        if (err == CRYPT_E_NOT_FOUND)
            return {};

        abort();
    }
    std::vector<BYTE> buffer(buffer_len, 0);
    if (!CertGetCertificateContextProperty(&cert, prop, buffer.data(), &buffer_len)) {
        DWORD err = GetLastError();
        err;
        abort();
    }

    return buffer;
}

/** Check if a certificate will expire within the next "X" days. */
static bool CertWillExpireInDays(CERT_INFO & cert_info, int days) {
     // get current time in UTC
    FILETIME currentTime = {};
    GetSystemTimePreciseAsFileTime(&currentTime);

    // move current time "X" days forward
    static constexpr uint64_t SECOND = 10000000L; // 100-nanosecond scale
    (uint64_t&)currentTime += days*24*60*60*SECOND;

    // check if certificate will then have expired
    LONG diff = CompareFileTime(&cert_info.NotAfter, &currentTime);
    return diff < 0;
}

/** CertGetEnhancedKeyUsage convenience wrapper. Returns empty string if no EKU fields are found. */
static std::vector<std::string> CertEnhancedKeyUsage (const CERT_CONTEXT& cert, DWORD flags) {
    DWORD len = 0;
    BOOL ok = CertGetEnhancedKeyUsage(&cert, flags, nullptr, &len);
    if (!ok || (len == 0))
        return {};

    std::vector<BYTE> eku_buf;
    eku_buf.resize(len, (BYTE)0);
    ok = CertGetEnhancedKeyUsage(&cert, flags, (CERT_ENHKEY_USAGE*)eku_buf.data(), &len);
    assert(ok);

    auto* eku = (CERT_ENHKEY_USAGE*)eku_buf.data();

    std::vector<std::string> result;
    for (DWORD i = 0; i < eku->cUsageIdentifier; ++i)
        result.push_back(eku->rgpszUsageIdentifier[i]);
    
    return result;
}

class CertStore {
public:
    CertStore(const wchar_t storename[], bool perUser) {
        m_store = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, perUser ? CERT_SYSTEM_STORE_CURRENT_USER : CERT_SYSTEM_STORE_LOCAL_MACHINE, storename);
        if (m_store == NULL)
            abort();

    }
    ~CertStore() {
        CertCloseStore(m_store, 0);
    }

    /** Cetificate iterator. Returns nullptr at end. */
    const CERT_CONTEXT* Next() {
        m_cert = CertEnumCertificatesInStore(m_store, m_cert);
        return m_cert;
    }

private:
    HCERTSTORE          m_store = nullptr;
    const CERT_CONTEXT* m_cert = nullptr; ///< iterator
};


void CertAccessWin32() {
    CertStore store(L"My", true);

    for (const CERT_CONTEXT* cert = store.Next(); cert; cert = store.Next()) {
        // print certificate name
        wchar_t buffer[1024] = {};
        DWORD len = CertNameToStrW(cert->dwCertEncodingType, &cert->pCertInfo->Subject, CERT_SIMPLE_NAME_STR, buffer, (DWORD)std::size(buffer));
        assert(len > 0);
        std::wcout << L"Cert: " << buffer << L'\n';

        if (CertWillExpireInDays(*cert->pCertInfo, 31)) {
            std::wcout << L"  Cert will expire within a month.\n";
            continue;
        }

        std::vector<BYTE> thumbprint = CertContextProperty(*cert, CERT_HASH_PROP_ID);
        std::wcout << L"  Thumbprint: ";
        for (BYTE elm : thumbprint) {
            wchar_t buffer[3] = {};
            swprintf(buffer, std::size(buffer), L"%02x", elm);
            std::wcout << buffer;
        }
        std::wcout << L'\n';

        std::vector<std::string> ekus = CertEnhancedKeyUsage(*cert, 0);
        for (auto eku : ekus)
            std::cout << "  EKU: " << eku << '\n';

        std::vector<BYTE> prov_buf = CertContextProperty(*cert, CERT_KEY_PROV_INFO_PROP_ID);
        if (prov_buf.empty())
            continue;
        CRYPT_KEY_PROV_INFO* provider = (CRYPT_KEY_PROV_INFO*)prov_buf.data();
        std::wcout << L"  Provider: " << provider->pwszProvName << L'\n';
        std::wcout << L"  Container: " << provider->pwszContainerName << L'\n';

        if (provider->dwProvType != 0) {
            std::wcout << L"  Not a CNG type key\n";
            continue;
        }

        OpenCNGKey(provider->pwszProvName, provider->pwszContainerName, provider->dwKeySpec);
    }
}
