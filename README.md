Examples of how to use certificates for client authentication in web and TLS socket scenarios. The examples are geared towards Windows with trusted certificate storage, but all principles also apply to other operating systems.


## Getting started instructions

Prerequisites:
* Windows computer
* [Visual Studio](https://visualstudio.microsoft.com/) - for buiding the C++ and C# sample projects
* [Git](https://git-scm.com/) for Windows - for OpenSSL that's required for the Python projects
* [Python](https://www.python.org/) - to run the test web server

### Generate certificates for testing (optional)
Test certificates are already added to the `TestCertificates` folder. These can be re-generated by running `powershell .\GenerateCertificates.ps1` from the TestCertificates subfolder. Please note that this script will leave certificate "residue" in the "Current User\Personal" certificate store.


### Install root certificate (optional)
This step will remove the "Not secure" warning when testing from a web browser.


Install the root certificate:
* Either: From an admin command prompt: `certutil –addstore –f "root" TestRootCertificate.cer`
* Or: Double-click on `TestRootCertificate.cer`, select "Install Certificate", select "Local Machine" as store location, then "Trusted Root Certificate Authorities" as certificate store.

The root certificate will now show up in the Windows "Manage computer certificates" window:

![CertMgr Root](figures/CertMgrRoot.png) 


### Install client certificate
This step will enable the web browser to use the client certificate for authentication against the server.

Install the client certificate:
* Either: From a command prompt: `certutil -user –importpfx ClientCert.pfx NoRoot,NoExport` (password "1234")
* Or: Double-click on `ClientCert.pfx`, select "Install Certificate", select "Current User" as store location, enable "Protect private key... (Non-exportable)", then install with default settings.
* Or: From the web browser "Manage certificates" menu: Import `ClientCert.pfx` into "Personal" certificate store with default settings.

The client certificate will now show up in the Windows "Manage user certificates" window:

![CertMgr Client](figures/CertMgrClient.png) 

It will also show up in the web browser certificate dialogs:

![Browser Cert Install](figures/BrowserCertInstall.png) 


### TPM storage of private key
Installed certificates will by default have their private key managed by the SW-based "Microsoft Software Key Storage Provider" when importing non-exportable. It's also possible to store the private key in the TPM chip for enhanced HW-enforced security.

This should in principle by possible with `certutil -user -csp TPM -p "" -importpfx ClientCert.pfx NoExport`. However, that doesn't seem to work as expected, and instead leads to a `NTE_INVALID_PARAMETER` error. This appears to be a known issue, and one can use the [TPMImport](https://github.com/glueckkanja-pki/TPMImport) tool as work-around. The certificate can then be imported to the TPM with `TPMImport.exe -user -v ClientCert.pfx ""`.

One can verify the actual key storage with `certutil -user -store My ClientCert`. You'll then get `Provider = Microsoft Platform Crypto Provider` if the private key is actually stored in the TPM.


## Client authentication
The `clientAuth` OID (1.3.6.1.5.5.7.3.2) EKU field in the client certificate enables it to be used for client authentication.
Double-click on `WebServer.py` to start the test web server to be used for testing of client authentication.

### Testing from web browser
Steps:
* Open https://localhost:443/ in a web browser.
* Select `ClientCert` in the certificate selection menu.

![Browser Cert Select](figures/BrowserCertSelect.png)

* Observe that the selected certificate is listed in the generated webpage.

![Browser Webpage](figures/BrowserWebpage.png)

### Programmatic HTTP communication

| Language  | Secure certificate store support | HTTP API(s)              | Sample code              | Limitations |
|-----------|----------------------------------|-----------------------|---------------------------|-------------|
| C++ Win32 | [CNG](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-portal) | [WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/iwinhttprequest-interface), [WinINet](https://learn.microsoft.com/en-us/windows/win32/wininet/portal) & [MSXML6](https://learn.microsoft.com/en-us/windows/win32/api/msxml6/) | See [WebClientCpp](WebClientCpp/) | Unable to access certificates in ["Local Computer\Personal" store with MSXML6](https://stackoverflow.com/a/38779903/3267386). |
| C++/C# UWP| [CertificateStores](https://learn.microsoft.com/en-us/uwp/api/windows.security.cryptography.certificates.certificatestores) | | See [WebClientUwp](WebClientUwp/) | Unable to access certificates in ["Local Computer\Personal" store](https://github.com/MicrosoftDocs/winrt-api/issues/2288) |
| C#/.Net   | [X509Store](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.x509certificates.x509store) | | See [WebClientNet](WebClientNet/) | None discovered |
| Java      | [Leveraging Security in the Native Platform Using Java ..](https://www.oracle.com/technical-resources/articles/javase/security.html) | | Not yet tested | TBD |
| Python    |No known support (see [#10](../../issues/10)) | | See [WebClientPy](WebClientPy/) (file-based certificate handling)| [Unable to use certificate store for mTLS](https://github.com/sethmlarson/truststore/issues/78) |

All the language samples are command-line applications that tries to authenticate against `https://localhost:443/` using the client certificate. The applications can be run without any arguments and will output the following on success:
```
Client certificate: ClientCert

<html><head><title>Client certificate authentication test</title></head>
<body>
<p>Request path: /</p>
<p>Successfully validated <b>client certificate</b>: (commonName: ClientCert), issued by (commonName: TestRootCertificate).</p>
</body></html>
```

### Programmatic TLS socket communication
Client certificates can also be used for authentication when using "raw" TLS/SSL sockets directly. However, it's then important that the underlying socket library is based on [schannel](https://learn.microsoft.com/en-us/windows/win32/secauthn/performing-authentication-using-schannel) with Winsock underneath, and _not_ on OpenSSL. Direct [Winsock](https://learn.microsoft.com/en-us/windows/win32/winsock/secure-winsock-programming) usage _might_ also work, but that remain to be investigated.

The reason for OpenSSL _not_ being supported, is that OpenSSL is unable to access private keys through the [CNG](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-portal) API. There does exist a `openssl-cng-engine` project that seeks to address this gap, but [client autentication doesn't appear to be supported yet](https://github.com/rticommunity/openssl-cng-engine/issues/46).

### Proxy settings
The above API alternatives will automatically utilize the Windows proxy settings for the currently logged in user.

Proxy settings can either be configured from "Windows Settings" -> "Proxy" or using the [`Netsh winhttp set advproxy`](https://learn.microsoft.com/en-us/windows/win32/winhttp/netsh-exe-commands#set-advproxy) command


How to inspect proxy settings:
```
> netsh winhttp show advproxy

Current WinHTTP advanced proxy settings:

{
        "Proxy":         "http://proxy.mycompany.com:8080",
        "ProxyBypass":   "mycompany.com",
        "AutoconfigUrl": "https://mycompany.com/pac.pac",
        "AutoDetect":    true
}
```
It's usually _not_ a good idea to combine `Proxy` & `ProxyBypass` settings with `AutoconfigUrl` as shown above, since the settings would undermine each other. The same settings are also found in the `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings` registry folder (use `regedit.exe` to view them).


## Code signing
The `codeSigning` OID (1.3.6.1.5.5.7.3.3) EKU field in the client certificate enables it to be used for code signing.

How to sign a binary:
* From a developer command prompt, run `signtool sign /a <FileName>.exe`
