#pragma once
#include <json/json.hpp>
#include <rtc/rtc.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>
#include "netplay/secure_channel.hpp"

namespace dkr::online {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace secure = dkr::runtime::netplay::secure;
inline constexpr unsigned Protocol = 1;
inline constexpr const char* Api = "https://dkr-mod-library-api.ross-macgregorbars.workers.dev";
struct Account { bool signed_in=false; unsigned generation=0; std::string name; };
using AccountView = std::function<Account()>;
using AccountRequest = std::function<Json(std::string,std::string,Json,std::string,std::stop_token)>;
void bind_account(AccountView, AccountRequest);
void shutdown_account();
void configure(const std::filesystem::path&);
struct Room {
    std::string id,title,host,compatibility,phase;
    unsigned racers=0,max_racers=0,spectators=0,max_spectators=0;
    bool locked=false,can_join=false,can_spectate=false;
};
struct View {
    bool signed_in=false,connected=false,feature_enabled=false,enabled=true,joining=false;
    bool public_desired=false,public_listed=false,updating=false,hidden=false;
    bool running_spectators=false;
    bool count_available=false;
    std::uint64_t online=0,revision=0;
    std::uint64_t connection_generation=0;
    unsigned account_generation=0;
    std::string account_name,status="Sign in with Discord to find public lobbies.",error;
    std::vector<Room> rooms;
};
std::shared_ptr<const View> view();
void connect(bool enabled);
void visibility(bool make_public);
void hidden(bool value);
void request_join(std::string room, std::string code, bool watch, std::string friend_admission={});
void cancel_join();
struct Bootstrap { std::string code; secure::Key admission{}; bool watch=false; };
bool take_bootstrap(const void* owner, Bootstrap&);

// Native owner bridge: no socket callbacks acquire DirectSession or PartyLink.
struct Bridge {
    std::mutex mutex;
    Json state=Json::object();
    std::vector<Json> commands,responses;
};
std::shared_ptr<Bridge> bridge(const void*);
void forget(const void*);
void publish(const void*, Json state);
std::vector<Json> commands(const void*);
int hosting_mode(const void*); // 0 preparing/inactive, 1 retained legacy room, 2 directory-backed.
void respond(const void*, Json response);
secure::KeyPair client_key(const void*);
bool bypass_code_resolution(const void*, const secure::Key&);
bool begin_code_join(const void*, std::string code, std::string name, const secure::Key& admission, bool watch);
std::string compatibility();
void compatibility(std::string value);
} // namespace dkr::online
