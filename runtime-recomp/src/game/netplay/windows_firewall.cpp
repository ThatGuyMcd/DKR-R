#include "windows_firewall.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <netfw.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <string_view>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

class ComScope final {
  public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }
    bool usable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

  private:
    HRESULT result_ = E_FAIL;
};

template <typename T> class ComPtr final {
  public:
    ~ComPtr() {
        reset();
    }
    T** put() {
        reset();
        return &value_;
    }
    T* operator->() const {
        return value_;
    }
    explicit operator bool() const {
        return value_ != nullptr;
    }

  private:
    void reset() {
        if (value_ != nullptr)
            value_->Release();
        value_ = nullptr;
    }
    T* value_ = nullptr;
};

std::wstring executable_path() {
    std::wstring path(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size())
        return {};
    path.resize(length);
    return path;
}

std::uint64_t stable_path_hash(std::wstring_view path) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (wchar_t character : path) {
        const wchar_t folded = static_cast<wchar_t>(std::towlower(character));
        hash ^= static_cast<std::uint16_t>(folded);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::wstring rule_name(std::wstring_view path, std::uint16_t port) {
    std::wostringstream output;
    output << L"DKR-R Online Multiplayer " << std::hex << stable_path_hash(path) << L"-" << std::dec << port;
    return output.str();
}

struct BlockingRule {
    std::wstring name;
};

bool conflicting_udp_block_rules(const std::wstring& application, std::vector<BlockingRule>& conflicts,
                                 std::string& error) {
    conflicts.clear();
    ComScope com;
    if (!com.usable()) {
        error = "DKR-R could not initialize Windows Firewall inspection.";
        return false;
    }
    ComPtr<INetFwPolicy2> policy;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(policy.put())))) {
        error = "DKR-R could not open Windows Firewall policy.";
        return false;
    }
    ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(rules.put()))) {
        error = "DKR-R could not inspect Windows Firewall rules.";
        return false;
    }
    ComPtr<IUnknown> enumerator_unknown;
    if (FAILED(rules->get__NewEnum(enumerator_unknown.put())) || !enumerator_unknown) {
        error = "DKR-R could not enumerate Windows Firewall rules.";
        return false;
    }
    ComPtr<IEnumVARIANT> enumerator;
    if (FAILED(enumerator_unknown->QueryInterface(IID_PPV_ARGS(enumerator.put()))) || !enumerator) {
        error = "DKR-R could not enumerate Windows Firewall rules.";
        return false;
    }

    VARIANT item;
    VariantInit(&item);
    ULONG fetched = 0;
    while (enumerator->Next(1, &item, &fetched) == S_OK) {
        if (item.vt == VT_DISPATCH && item.pdispVal != nullptr) {
            ComPtr<INetFwRule> rule;
            if (SUCCEEDED(item.pdispVal->QueryInterface(IID_PPV_ARGS(rule.put()))) && rule) {
                VARIANT_BOOL enabled = VARIANT_FALSE;
                NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_OUT;
                NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
                long protocol = 0;
                BSTR rule_application = nullptr;
                BSTR firewall_name = nullptr;
                const bool readable =
                    SUCCEEDED(rule->get_Enabled(&enabled)) && SUCCEEDED(rule->get_Direction(&direction)) &&
                    SUCCEEDED(rule->get_Action(&action)) && SUCCEEDED(rule->get_Protocol(&protocol)) &&
                    SUCCEEDED(rule->get_ApplicationName(&rule_application)) &&
                    SUCCEEDED(rule->get_Name(&firewall_name));
                const bool blocks_udp = protocol == NET_FW_IP_PROTOCOL_UDP || protocol == NET_FW_IP_PROTOCOL_ANY;
                if (readable && enabled == VARIANT_TRUE && direction == NET_FW_RULE_DIR_IN &&
                    action == NET_FW_ACTION_BLOCK && blocks_udp && rule_application != nullptr &&
                    firewall_name != nullptr && _wcsicmp(rule_application, application.c_str()) == 0) {
                    conflicts.push_back({firewall_name});
                }
                if (rule_application != nullptr)
                    SysFreeString(rule_application);
                if (firewall_name != nullptr)
                    SysFreeString(firewall_name);
            }
        }
        VariantClear(&item);
        VariantInit(&item);
        fetched = 0;
    }
    VariantClear(&item);
    error.clear();
    return true;
}

bool matching_rule_exists(const std::wstring& name, const std::wstring& application, std::uint16_t port) {
    ComScope com;
    if (!com.usable())
        return false;
    ComPtr<INetFwPolicy2> policy;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(policy.put())))) {
        return false;
    }
    ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(rules.put())))
        return false;
    BSTR requested_name = SysAllocString(name.c_str());
    if (requested_name == nullptr)
        return false;
    ComPtr<INetFwRule> rule;
    const HRESULT item_result = rules->Item(requested_name, rule.put());
    SysFreeString(requested_name);
    if (FAILED(item_result) || !rule)
        return false;

    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_OUT;
    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
    long protocol = 0;
    BSTR rule_application = nullptr;
    BSTR local_ports = nullptr;
    const bool readable = SUCCEEDED(rule->get_Enabled(&enabled)) && SUCCEEDED(rule->get_Direction(&direction)) &&
                          SUCCEEDED(rule->get_Action(&action)) && SUCCEEDED(rule->get_Protocol(&protocol)) &&
                          SUCCEEDED(rule->get_ApplicationName(&rule_application)) &&
                          SUCCEEDED(rule->get_LocalPorts(&local_ports));
    const std::wstring ports = local_ports != nullptr ? local_ports : L"";
    const std::wstring expected_port = std::to_wstring(port);
    const bool matches = readable && enabled == VARIANT_TRUE && direction == NET_FW_RULE_DIR_IN &&
                         action == NET_FW_ACTION_ALLOW && protocol == NET_FW_IP_PROTOCOL_UDP &&
                         rule_application != nullptr && _wcsicmp(rule_application, application.c_str()) == 0 &&
                         (ports == expected_port || ports == L"*");
    if (rule_application != nullptr)
        SysFreeString(rule_application);
    if (local_ports != nullptr)
        SysFreeString(local_ports);
    return matches;
}

bool run_netsh_elevated(const std::wstring& parameters, std::string_view cancelled_message,
                        std::string_view failure_message, std::string& error) {
    std::wstring windows_directory(32768U, L'\0');
    const UINT windows_length =
        GetWindowsDirectoryW(windows_directory.data(), static_cast<UINT>(windows_directory.size()));
    if (windows_length == 0U || windows_length >= windows_directory.size()) {
        error = "DKR-R could not locate the Windows Firewall utility.";
        return false;
    }
    windows_directory.resize(windows_length);
    const std::wstring netsh = windows_directory + L"\\System32\\netsh.exe";

    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    launch.lpVerb = L"runas";
    launch.lpFile = netsh.c_str();
    launch.lpParameters = parameters.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch) || launch.hProcess == nullptr) {
        const DWORD code = GetLastError();
        error = code == ERROR_CANCELLED ? std::string(cancelled_message) : std::string(failure_message);
        return false;
    }
    const DWORD wait = WaitForSingleObject(launch.hProcess, 30000U);
    DWORD exit_code = ERROR_GEN_FAILURE;
    const bool completed = wait == WAIT_OBJECT_0 && GetExitCodeProcess(launch.hProcess, &exit_code) != 0;
    CloseHandle(launch.hProcess);
    if (!completed || exit_code != 0U) {
        error = std::string(failure_message);
        return false;
    }
    error.clear();
    return true;
}

bool remove_blocking_rules_elevated(const std::vector<BlockingRule>& conflicts, const std::wstring& application,
                                    std::string& error) {
    for (const BlockingRule& conflict : conflicts) {
        const std::wstring parameters =
            L"advfirewall firewall delete rule name=\"" + conflict.name + L"\" dir=in program=\"" + application + L"\"";
        if (!run_netsh_elevated(
                parameters, "Hosting was cancelled because DKR-R's conflicting Windows Firewall block was not removed.",
                "Windows could not remove the firewall rule that blocks DKR-R's encrypted UDP lobby.", error)) {
            return false;
        }
    }
    std::vector<BlockingRule> remaining;
    if (!conflicting_udp_block_rules(application, remaining, error)) {
        return false;
    }
    if (!remaining.empty()) {
        error = "Windows Firewall still blocks inbound UDP for this DKR-R build. Hosting was not started.";
        return false;
    }
    error.clear();
    return true;
}

bool add_rule_elevated(const std::wstring& name, const std::wstring& application, std::uint16_t port,
                       std::string& error) {
    const std::wstring parameters = L"advfirewall firewall add rule name=\"" + name +
                                    L"\" dir=in action=allow program=\"" + application +
                                    L"\" enable=yes profile=any protocol=UDP localport=" + std::to_wstring(port);
    if (!run_netsh_elevated(parameters, "Hosting was cancelled because Windows Firewall permission was not granted.",
                            "Windows Firewall did not authorize this DKR-R build for hosting.", error)) {
        return false;
    }
    if (!matching_rule_exists(name, application, port)) {
        error = "Windows reported success, but the DKR-R UDP firewall rule could not be verified.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace

bool ensure_host_firewall_access(std::uint16_t port, std::string& error) {
    const std::wstring application = executable_path();
    if (application.empty()) {
        error = "DKR-R could not determine its executable path for Windows Firewall.";
        return false;
    }
    const std::wstring name = rule_name(application, port);
    std::vector<BlockingRule> conflicts;
    if (!conflicting_udp_block_rules(application, conflicts, error)) {
        return false;
    }
    if (!conflicts.empty() && !remove_blocking_rules_elevated(conflicts, application, error)) {
        return false;
    }
    if (!matching_rule_exists(name, application, port) && !add_rule_elevated(name, application, port, error)) {
        return false;
    }
    conflicts.clear();
    if (!conflicting_udp_block_rules(application, conflicts, error)) {
        return false;
    }
    if (!conflicts.empty()) {
        error = "Windows Firewall still blocks inbound UDP for this DKR-R build. Hosting was not started.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace dkr::runtime::netplay

#else

namespace dkr::runtime::netplay {

bool ensure_host_firewall_access(std::uint16_t, std::string& error) {
    error.clear();
    return true;
}

} // namespace dkr::runtime::netplay

#endif
