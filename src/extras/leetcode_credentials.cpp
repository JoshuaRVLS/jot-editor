#include "extras/leetcode_credentials.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#endif

namespace LeetCodeCredentials
{
#ifdef _WIN32
  namespace
  {
    constexpr wchar_t kTarget[] = L"jot.leetcode.session";

    std::string narrow(const wchar_t *value)
    {
      if (!value) return {};
      const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
      if (count <= 1) return {};
      std::string out((size_t)count, '\0');
      WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), count, nullptr, nullptr);
      out.pop_back();
      return out;
    }
  }

  Result get()
  {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kTarget, CRED_TYPE_GENERIC, 0, &credential))
    {
      const DWORD code = GetLastError();
      return {code == ERROR_NOT_FOUND, true, {}, code == ERROR_NOT_FOUND ? "" : "Windows Credential Manager could not read the session"};
    }
    std::string value((const char *)credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return {true, true, std::move(value), {}};
  }

  Result set(const std::string &value)
  {
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(kTarget);
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlob = (LPBYTE)value.data();
    credential.CredentialBlobSize = (DWORD)value.size();
    credential.UserName = const_cast<wchar_t *>(L"LeetCode session");
    return CredWriteW(&credential, 0)
               ? Result{true, true, {}, {}}
               : Result{false, true, {}, "Windows Credential Manager could not save the session"};
  }

  Result erase()
  {
    if (CredDeleteW(kTarget, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
      return {true, true, {}, {}};
    return {false, true, {}, "Windows Credential Manager could not remove the session"};
  }
#elif defined(__APPLE__)
  namespace
  {
    constexpr const char *kService = "jot.leetcode";
    constexpr const char *kAccount = "session";

    CFStringRef cf(const char *value)
    {
      return CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
    }

    std::string copy_cf_string(CFStringRef value)
    {
      if (!value) return {};
      const CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
      std::string out((size_t)max, '\0');
      if (!CFStringGetCString(value, out.data(), max, kCFStringEncodingUTF8)) return {};
      out.resize(std::char_traits<char>::length(out.c_str()));
      return out;
    }
  }

  Result get()
  {
    const std::string service = kService;
    const std::string account = kAccount;
    CFStringRef service_ref = cf(service.c_str());
    CFStringRef account_ref = cf(account.c_str());
    const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
    const void *values[] = {kSecClassGenericPassword, service_ref, account_ref, kCFBooleanTrue, kSecMatchLimitOne};
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 5,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFTypeRef data = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &data);
    CFRelease(query);
    CFRelease(service_ref);
    CFRelease(account_ref);
    if (status == errSecItemNotFound) return {true, true, {}, {}};
    if (status != errSecSuccess || !data)
      return {false, true, {}, "Apple Keychain could not read the session"};
    CFDataRef secret = (CFDataRef)data;
    std::string out((const char *)CFDataGetBytePtr(secret), (size_t)CFDataGetLength(secret));
    CFRelease(secret);
    return {true, true, std::move(out), {}};
  }

  Result set(const std::string &value)
  {
    Result removed = erase();
    if (!removed.ok) return removed;
    CFStringRef service = cf(kService);
    CFStringRef account = cf(kAccount);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)value.data(), (CFIndex)value.size());
    const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
    const void *values[] = {kSecClassGenericPassword, service, account, data};
    CFDictionaryRef item = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 4,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    const OSStatus status = SecItemAdd(item, nullptr);
    CFRelease(item);
    CFRelease(data);
    CFRelease(service);
    CFRelease(account);
    return status == errSecSuccess ? Result{true, true, {}, {}}
                                   : Result{false, true, {}, "Apple Keychain could not save the session"};
  }

  Result erase()
  {
    CFStringRef service = cf(kService);
    CFStringRef account = cf(kAccount);
    const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void *values[] = {kSecClassGenericPassword, service, account};
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
                                               &kSecTypeDictionaryKeyCallBacks,
                                               &kSecTypeDictionaryValueCallBacks);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    CFRelease(service);
    CFRelease(account);
    return status == errSecSuccess || status == errSecItemNotFound
               ? Result{true, true, {}, {}}
               : Result{false, true, {}, "Apple Keychain could not remove the session"};
  }
#else
  namespace
  {
    constexpr const char *kAttributes = "application=jot;purpose=leetcode-session";

    bool available()
    {
      return std::system("command -v secret-tool >/dev/null 2>&1") == 0
             && std::getenv("DBUS_SESSION_BUS_ADDRESS") != nullptr;
    }

    std::string quote(const std::string &value)
    {
      std::string out = "'";
      for (char c : value)
      {
        if (c == '\'') out += "'\\''";
        else out += c;
      }
      return out + "'";
    }

    struct Pipe
    {
      FILE *file = nullptr;
      int status = -1;
      ~Pipe() { if (file) status = pclose(file); }
    };
  }

  Result get()
  {
    if (!available()) return {false, false, {}, "Linux Secret Service is unavailable; no session was stored"};
    Pipe pipe;
    pipe.file = popen("secret-tool lookup application jot purpose leetcode-session 2>/dev/null", "r");
    if (!pipe.file) return {false, true, {}, "Linux Secret Service could not be queried"};
    std::array<char, 512> buffer{};
    std::string value;
    while (fgets(buffer.data(), (int)buffer.size(), pipe.file)) value += buffer.data();
    const int status = pclose(pipe.file);
    pipe.file = nullptr;
    if (status != 0) return {true, true, {}, {}};
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return {true, true, std::move(value), {}};
  }

  Result set(const std::string &value)
  {
    if (!available()) return {false, false, {}, "Linux Secret Service is unavailable; no session was stored"};
    const std::string command = "secret-tool store --label='jot LeetCode session' application jot purpose leetcode-session";
    FILE *pipe = popen(command.c_str(), "w");
    if (!pipe) return {false, true, {}, "Linux Secret Service could not save the session"};
    const std::string stored = value + "\n";
    const bool wrote = fwrite(stored.data(), 1, stored.size(), pipe) == stored.size();
    const int status = pclose(pipe);
    return wrote && status == 0 ? Result{true, true, {}, {}}
                                : Result{false, true, {}, "Linux Secret Service could not save the session"};
  }

  Result erase()
  {
    if (!available()) return {false, false, {}, "Linux Secret Service is unavailable; no session was stored"};
    const int status = std::system("secret-tool clear application jot purpose leetcode-session >/dev/null 2>&1");
    return status == 0 ? Result{true, true, {}, {}}
                       : Result{false, true, {}, "Linux Secret Service could not remove the session"};
  }
#endif
} // namespace LeetCodeCredentials
