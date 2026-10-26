#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "sdl3_input_protocol.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using namespace dkr::runtime::sdl3_input::protocol;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

struct Options {
    std::uint16_t port = 0U;
    std::array<std::uint8_t, kTokenBytes> token{};
    std::filesystem::path mappings;
    bool token_present = false;
    bool self_test = false;
};

struct HostDevice {
    SDL_JoystickID sdl_id = 0U;
    std::int32_t public_instance = -1;
    SDL_Gamepad* gamepad = nullptr;
    std::uint16_t vendor = 0U;
    std::uint16_t product = 0U;
    std::string persistent_key;
    std::string name;
    bool rumble = false;
    bool gyro = false;
    bool gamepad_gyro = false;
    bool raw_deck_gyro = false;
    float gyro_rate_hz = 0.0F;
    std::array<std::uint8_t, dkr::runtime::controllers::kSnapshotButtonCount> button_bound{};
    std::array<float, 3> gyro_data{};
    std::uint64_t gyro_timestamp_us = 0U;
    bool gyro_valid = false;
};

constexpr std::uint16_t kValveVendorId = 0x28DEU;
constexpr std::uint16_t kSteamDeckProductId = 0x1205U;
constexpr std::uint16_t kSteamVirtualGamepadProductId = 0x11FFU;
constexpr int kSteamDeckControllerInterface = 2;
constexpr float kSteamDeckGyroRateHz = 250.0F;
constexpr float kSteamDeckGyroScale = (2000.0F * 0.01745329251994329577F) / 32768.0F;

#pragma pack(push, 1)
struct SteamDeckHidReport {
    std::uint16_t version = 0U;
    std::uint8_t type = 0U;
    std::uint8_t length = 0U;
    std::uint32_t packet_number = 0U;
    std::uint64_t buttons = 0U;
    std::int16_t left_pad_x = 0;
    std::int16_t left_pad_y = 0;
    std::int16_t right_pad_x = 0;
    std::int16_t right_pad_y = 0;
    std::int16_t accel_x = 0;
    std::int16_t accel_y = 0;
    std::int16_t accel_z = 0;
    std::int16_t gyro_x = 0;
    std::int16_t gyro_y = 0;
    std::int16_t gyro_z = 0;
    std::int16_t gyro_quat_w = 0;
    std::int16_t gyro_quat_x = 0;
    std::int16_t gyro_quat_y = 0;
    std::int16_t gyro_quat_z = 0;
    std::uint16_t left_trigger = 0U;
    std::uint16_t right_trigger = 0U;
    std::int16_t left_stick_x = 0;
    std::int16_t left_stick_y = 0;
    std::int16_t right_stick_x = 0;
    std::int16_t right_stick_y = 0;
    std::uint16_t left_pad_pressure = 0U;
    std::uint16_t right_pad_pressure = 0U;
    // The Deck state payload occupies 56 bytes; the 64-byte Valve report
    // union carries four trailing bytes used by newer firmware revisions.
    std::uint16_t left_stick_touch_coverage = 0U;
    std::uint16_t right_stick_touch_coverage = 0U;
};
#pragma pack(pop)

static_assert(sizeof(SteamDeckHidReport) == 64U);

struct SteamDeckGyroSample {
    std::array<float, 3> data{};
    std::uint64_t timestamp_us = 0U;
    std::uint32_t packet_number = 0U;
    bool imu_active = false;
};

bool DecodeSteamDeckGyro(const void* bytes, std::size_t size, SteamDeckGyroSample& sample) {
    if (bytes == nullptr || size != sizeof(SteamDeckHidReport))
        return false;
    SteamDeckHidReport report{};
    std::memcpy(&report, bytes, sizeof(report));
    if (report.version != 1U || report.type != 9U || report.length != sizeof(SteamDeckHidReport)) {
        return false;
    }

    // Match SDL's Steam Deck HID driver exactly. SDL gamepad gyro values are
    // radians per second in the controller coordinate system.
    sample.data[0] = static_cast<float>(report.gyro_x) * kSteamDeckGyroScale;
    sample.data[1] = static_cast<float>(report.gyro_z) * kSteamDeckGyroScale;
    sample.data[2] = -static_cast<float>(report.gyro_y) * kSteamDeckGyroScale;
    sample.timestamp_us = static_cast<std::uint64_t>(report.packet_number) * 4000U;
    sample.packet_number = report.packet_number;
    sample.imu_active = report.accel_x != 0 || report.accel_y != 0 || report.accel_z != 0 || report.gyro_x != 0 ||
                        report.gyro_y != 0 || report.gyro_z != 0;
    return true;
}

bool SteamDeckDecoderSelfTest() {
    SteamDeckHidReport report{};
    report.version = 1U;
    report.type = 9U;
    report.length = sizeof(report);
    report.packet_number = 123U;
    report.accel_z = 16384;
    report.gyro_x = 16384;
    report.gyro_y = -8192;
    report.gyro_z = 4096;
    SteamDeckGyroSample sample{};
    if (!DecodeSteamDeckGyro(&report, sizeof(report), sample) || !sample.imu_active || sample.timestamp_us != 492000U) {
        return false;
    }
    constexpr float kTolerance = 0.0001F;
    return std::fabs(sample.data[0] - 1000.0F * 0.01745329251994329577F) < kTolerance &&
           std::fabs(sample.data[1] - 250.0F * 0.01745329251994329577F) < kTolerance &&
           std::fabs(sample.data[2] - 500.0F * 0.01745329251994329577F) < kTolerance;
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

HostDevice* SteamDeckGyroTarget(std::vector<HostDevice>& devices) {
    const auto direct_match = std::find_if(devices.begin(), devices.end(), [](const HostDevice& device) {
        return device.vendor == kValveVendorId &&
               (device.product == kSteamVirtualGamepadProductId || device.product == kSteamDeckProductId);
    });
    if (direct_match != devices.end())
        return &*direct_match;

    const auto named_match = std::find_if(devices.begin(), devices.end(), [](const HostDevice& device) {
        const std::string name = Lowercase(device.name);
        return name.find("steam deck") != std::string::npos || name.find("steam virtual gamepad") != std::string::npos;
    });
    if (named_match != devices.end())
        return &*named_match;

    // Some SteamOS revisions expose the builtin controls with a generic
    // evdev name. The presence of the physical Deck HID endpoint makes a
    // single connected gamepad unambiguous, but never guess among several.
    return devices.size() == 1U ? &devices.front() : nullptr;
}

class SteamDeckRawGyro {
  public:
    ~SteamDeckRawGyro() {
        Close();
    }

    void Shutdown() {
        Close();
    }

    void Pump(std::vector<HostDevice>& devices) {
        ClearAttachment(devices);
#if defined(__linux__)
        HostDevice* target = SteamDeckGyroTarget(devices);
        if (target == nullptr || target->gamepad_gyro) {
            Close();
            return;
        }
        if (!EnsureOpen())
            return;

        std::array<std::uint8_t, sizeof(SteamDeckHidReport)> bytes{};
        for (int report_index = 0; report_index < 64; ++report_index) {
            const int count = SDL_hid_read(device_, bytes.data(), bytes.size());
            if (count == 0)
                break;
            if (count < 0) {
                std::fprintf(stderr, "[input][sdl3][deck-gyro] HID read failed: %s\n", SDL_GetError());
                Close();
                return;
            }
            SteamDeckGyroSample sample{};
            if (!DecodeSteamDeckGyro(bytes.data(), static_cast<std::size_t>(count), sample) ||
                (have_packet_ && sample.packet_number == last_packet_)) {
                continue;
            }
            have_packet_ = true;
            last_packet_ = sample.packet_number;
            if (!sample.imu_active) {
                ++zero_imu_reports_;
                MaybeEnableImu();
                continue;
            }
            zero_imu_reports_ = 0U;
            sample_ = sample;
            sample_valid_ = true;
        }

        if (device_ != nullptr && sample_valid_) {
            target->raw_deck_gyro = true;
            target->gyro = true;
            target->gyro_rate_hz = kSteamDeckGyroRateHz;
            target->gyro_data = sample_.data;
            target->gyro_timestamp_us = sample_.timestamp_us;
            target->gyro_valid = true;
        }
#else
        (void)devices;
#endif
    }

  private:
    void ClearAttachment(std::vector<HostDevice>& devices) {
        for (HostDevice& device : devices) {
            if (!device.raw_deck_gyro)
                continue;
            device.raw_deck_gyro = false;
            if (!device.gamepad_gyro) {
                device.gyro = false;
                device.gyro_rate_hz = 0.0F;
                device.gyro_valid = false;
                device.gyro_data = {};
                device.gyro_timestamp_us = 0U;
            }
        }
    }

#if defined(__linux__)
    bool EnsureOpen() {
        if (device_ != nullptr)
            return true;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_probe_)
            return false;
        next_probe_ = now + std::chrono::seconds{1};

        SDL_hid_device_info* devices = SDL_hid_enumerate(kValveVendorId, kSteamDeckProductId);
        for (SDL_hid_device_info* candidate = devices; candidate != nullptr; candidate = candidate->next) {
            if (candidate->interface_number != kSteamDeckControllerInterface || candidate->path == nullptr) {
                continue;
            }
            SDL_hid_device* opened = SDL_hid_open_path(candidate->path);
            if (opened == nullptr)
                continue;
            if (SDL_hid_set_nonblocking(opened, 1) != 0) {
                SDL_hid_close(opened);
                continue;
            }
            device_ = opened;
            path_ = candidate->path;
            break;
        }
        SDL_hid_free_enumeration(devices);

        if (device_ == nullptr) {
            if (!reported_missing_) {
                std::fprintf(stderr,
                             "[input][sdl3][deck-gyro] physical Steam Deck "
                             "HID interface 2 is unavailable: %s\n",
                             SDL_GetError());
                reported_missing_ = true;
            }
            return false;
        }
        reported_missing_ = false;
        std::fprintf(stderr,
                     "[input][sdl3][deck-gyro] opened physical Steam Deck "
                     "gyro fallback at %s\n",
                     path_.c_str());
        return true;
    }

    void MaybeEnableImu() {
        if (device_ == nullptr || zero_imu_reports_ < 32U)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_enable_attempt_)
            return;
        next_enable_attempt_ = now + std::chrono::seconds{2};

        // Steam can turn off raw IMU reporting when its controller layout has
        // no gyro binding. Ask only for raw accelerometer and gyro data; do not
        // alter buttons, sticks, trackpads, lizard mode, or Steam Input.
        std::array<std::uint8_t, 65> command{};
        command[1] = 0x87U; // ID_SET_SETTINGS_VALUES
        command[2] = 0x03U; // one packed controller setting
        command[3] = 0x30U; // SETTING_IMU_MODE
        command[4] = 0x18U; // raw accelerometer | raw gyro
        const int written = SDL_hid_send_feature_report(device_, command.data(), command.size());
        std::fprintf(stderr,
                     "[input][sdl3][deck-gyro] raw IMU was disabled; "
                     "enable request %s\n",
                     written == static_cast<int>(command.size()) ? "sent" : "failed");
        zero_imu_reports_ = 0U;
    }
#endif

    void Close() {
#if defined(__linux__)
        if (device_ != nullptr) {
            SDL_hid_close(device_);
            device_ = nullptr;
        }
        path_.clear();
        sample_valid_ = false;
        have_packet_ = false;
        zero_imu_reports_ = 0U;
#endif
    }

#if defined(__linux__)
    SDL_hid_device* device_ = nullptr;
    std::string path_;
    SteamDeckGyroSample sample_{};
    std::chrono::steady_clock::time_point next_probe_{};
    std::chrono::steady_clock::time_point next_enable_attempt_{};
    std::uint32_t last_packet_ = 0U;
    std::uint32_t zero_imu_reports_ = 0U;
    bool sample_valid_ = false;
    bool have_packet_ = false;
    bool reported_missing_ = false;
#endif
};

void CloseSocket(NativeSocket& socket) {
    if (socket == kInvalidSocket)
        return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
    socket = kInvalidSocket;
}

bool SendAll(NativeSocket socket, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size > 0U) {
#if defined(_WIN32)
        const int chunk = send(socket, reinterpret_cast<const char*>(bytes), static_cast<int>(size), 0);
#else
        const ssize_t chunk = send(socket, bytes, size, MSG_NOSIGNAL);
#endif
        if (chunk <= 0)
            return false;
        bytes += static_cast<std::size_t>(chunk);
        size -= static_cast<std::size_t>(chunk);
    }
    return true;
}

bool ReceiveAll(NativeSocket socket, void* data, std::size_t size) {
    auto* bytes = static_cast<std::uint8_t*>(data);
    while (size > 0U) {
#if defined(_WIN32)
        const int chunk = recv(socket, reinterpret_cast<char*>(bytes), static_cast<int>(size), 0);
#else
        const ssize_t chunk = recv(socket, bytes, size, 0);
#endif
        if (chunk <= 0)
            return false;
        bytes += static_cast<std::size_t>(chunk);
        size -= static_cast<std::size_t>(chunk);
    }
    return true;
}

bool Readable(NativeSocket socket) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{};
#if defined(_WIN32)
    return select(0, &readable, nullptr, nullptr, &timeout) > 0;
#else
    return select(socket + 1, &readable, nullptr, nullptr, &timeout) > 0;
#endif
}

bool ParseHexToken(const std::string& text, std::array<std::uint8_t, kTokenBytes>& token) {
    if (text.size() != token.size() * 2U)
        return false;
    for (std::size_t index = 0U; index < token.size(); ++index) {
        const std::string byte = text.substr(index * 2U, 2U);
        char* end = nullptr;
        const unsigned long value = std::strtoul(byte.c_str(), &end, 16);
        if (end == nullptr || *end != '\0' || value > 0xFFU)
            return false;
        token[index] = static_cast<std::uint8_t>(value);
    }
    return true;
}

std::optional<Options> ParseOptions(int argc, char** argv) {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--self-test") {
            options.self_test = true;
        } else if (argument == "--port" && index + 1 < argc) {
            const int port = std::atoi(argv[++index]);
            if (port <= 0 || port > 65535)
                return std::nullopt;
            options.port = static_cast<std::uint16_t>(port);
        } else if (argument == "--token" && index + 1 < argc) {
            options.token_present = ParseHexToken(argv[++index], options.token);
            if (!options.token_present)
                return std::nullopt;
        } else if (argument == "--mappings" && index + 1 < argc) {
            options.mappings = std::filesystem::path(argv[++index]);
        } else {
            return std::nullopt;
        }
    }
    if (!options.self_test && (options.port == 0U || !options.token_present)) {
        return std::nullopt;
    }
    return options;
}

void ConfigureSdlHints() {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
}

std::string PersistentKey(SDL_Gamepad* gamepad) {
    std::string identity;
    if (const char* serial = SDL_GetGamepadSerial(gamepad); serial != nullptr && *serial != '\0') {
        identity = "serial:" + std::string(serial);
    } else if (const char* path = SDL_GetGamepadPath(gamepad); path != nullptr && *path != '\0') {
        identity = "path:" + std::string(path);
    } else {
        char guid_text[64]{};
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(gamepad)), guid_text, sizeof(guid_text));
        identity = "guid:" + std::string(guid_text);
    }
    identity += ":" + std::to_string(SDL_GetGamepadVendor(gamepad));
    identity += ":" + std::to_string(SDL_GetGamepadProduct(gamepad));
    identity += ":" + std::to_string(SDL_GetGamepadProductVersion(gamepad));

    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : identity) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << "sdl3-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

void CloseDevice(HostDevice& device) {
    if (device.gamepad != nullptr) {
        SDL_CloseGamepad(device.gamepad);
        device.gamepad = nullptr;
    }
}

void PopulateButtonBindings(HostDevice& device) {
    device.button_bound.fill(0U);
    int count = 0;
    SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(device.gamepad, &count);
    if (bindings != nullptr) {
        for (int index = 0; index < count; ++index) {
            const SDL_GamepadBinding* binding = bindings[index];
            if (binding != nullptr && binding->output_type == SDL_GAMEPAD_BINDTYPE_BUTTON) {
                const int button = static_cast<int>(binding->output.button);
                if (button >= 0 && button < static_cast<int>(device.button_bound.size())) {
                    device.button_bound[static_cast<std::size_t>(button)] = 1U;
                }
            }
        }
        SDL_free(bindings);
    }
    // Some platform drivers do not expose their internal mapping bindings.
    // Preserve truthful button availability in that case.
    if (std::none_of(device.button_bound.begin(), device.button_bound.end(),
                     [](std::uint8_t value) { return value != 0U; })) {
        for (std::size_t index = 0U; index < device.button_bound.size(); ++index) {
            device.button_bound[index] =
                SDL_GamepadHasButton(device.gamepad, static_cast<SDL_GamepadButton>(index)) ? 1U : 0U;
        }
    }
}

void RefreshDevices(std::vector<HostDevice>& devices, std::int32_t& next_public_instance) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::vector<SDL_JoystickID> connected;
    if (ids != nullptr && count > 0) {
        connected.assign(ids, ids + count);
    }
    SDL_free(ids);

    for (auto iterator = devices.begin(); iterator != devices.end();) {
        if (std::find(connected.begin(), connected.end(), iterator->sdl_id) == connected.end()) {
            CloseDevice(*iterator);
            iterator = devices.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (const SDL_JoystickID id : connected) {
        if (devices.size() >= kMaximumDevices)
            break;
        if (std::any_of(devices.begin(), devices.end(),
                        [id](const HostDevice& device) { return device.sdl_id == id; })) {
            continue;
        }
        SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
        if (gamepad == nullptr)
            continue;
        HostDevice device{};
        device.sdl_id = id;
        device.public_instance = next_public_instance++;
        device.gamepad = gamepad;
        device.vendor = SDL_GetGamepadVendor(gamepad);
        device.product = SDL_GetGamepadProduct(gamepad);
        const char* name = SDL_GetGamepadName(gamepad);
        device.name = name != nullptr && *name != '\0' ? name : "SDL3 gamepad";
        device.persistent_key = PersistentKey(gamepad);
        const SDL_PropertiesID properties = SDL_GetGamepadProperties(gamepad);
        device.rumble = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
        device.gamepad_gyro = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO);
        if (device.gamepad_gyro) {
            if (!SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true)) {
                device.gamepad_gyro = false;
            } else {
                device.gyro_rate_hz = SDL_GetGamepadSensorDataRate(gamepad, SDL_SENSOR_GYRO);
            }
        }
        device.gyro = device.gamepad_gyro;
        PopulateButtonBindings(device);
        devices.push_back(std::move(device));
    }
}

template <std::size_t Size> void CopyText(std::array<char, Size>& destination, const std::string& source) {
    const std::size_t length = std::min(source.size(), destination.size() - 1U);
    std::memcpy(destination.data(), source.data(), length);
    destination[length] = '\0';
}

void FillDevicePacket(DevicePacket& packet, const HostDevice& device) {
    packet = {};
    packet.instance = device.public_instance;
    packet.connected = device.gamepad != nullptr ? 1U : 0U;
    packet.mapped = 1U;
    packet.rumble = device.rumble ? 1U : 0U;
    packet.gyro = device.gyro ? 1U : 0U;
    packet.gyro_valid = device.gyro_valid ? 1U : 0U;
    packet.button_bound = device.button_bound;
    for (std::size_t index = 0U; index < packet.buttons.size(); ++index) {
        packet.buttons[index] = SDL_GetGamepadButton(device.gamepad, static_cast<SDL_GamepadButton>(index)) ? 1U : 0U;
    }
    for (std::size_t index = 0U; index < packet.axes.size(); ++index) {
        packet.axes[index] = SDL_GetGamepadAxis(device.gamepad, static_cast<SDL_GamepadAxis>(index));
    }
    packet.gyro_data = device.gyro_data;
    packet.gyro_timestamp_us = device.gyro_timestamp_us;
    packet.gyro_rate_hz = device.gyro_rate_hz;
    CopyText(packet.name, device.name);
    CopyText(packet.persistent_key, device.persistent_key);
    CopyText(packet.mapping_source,
             device.raw_deck_gyro ? "SDL3 native mapping + direct Steam Deck gyro" : "SDL3 native mapping");
}

enum class CommandResult { Continue, Shutdown, Disconnected };

CommandResult ProcessCommand(NativeSocket socket, const std::array<std::uint8_t, kTokenBytes>& token,
                             std::vector<HostDevice>& devices) {
    if (!Readable(socket))
        return CommandResult::Continue;
    PacketHeader header{};
    if (!ReceiveAll(socket, &header, sizeof(header))) {
        return CommandResult::Disconnected;
    }
    if (header.magic != kMagic || header.version != kVersion || header.token != token ||
        header.size < sizeof(PacketHeader)) {
        return CommandResult::Disconnected;
    }
    if (header.type == PacketType::Shutdown && header.size == sizeof(ShutdownPacket)) {
        return CommandResult::Shutdown;
    }
    if (header.type == PacketType::Rumble && header.size == sizeof(RumblePacket)) {
        RumblePacket packet{};
        packet.header = header;
        auto* remainder = reinterpret_cast<std::uint8_t*>(&packet) + sizeof(header);
        if (!ReceiveAll(socket, remainder, sizeof(packet) - sizeof(header))) {
            return CommandResult::Disconnected;
        }
        const auto device = std::find_if(devices.begin(), devices.end(), [&](const HostDevice& candidate) {
            return candidate.public_instance == packet.instance;
        });
        if (device != devices.end() && device->gamepad != nullptr) {
            SDL_RumbleGamepad(device->gamepad, packet.low_frequency, packet.high_frequency, packet.duration_ms);
        }
        return CommandResult::Continue;
    }
    return CommandResult::Disconnected;
}

NativeSocket Connect(std::uint16_t port) {
    NativeSocket socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == kInvalidSocket)
        return kInvalidSocket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        CloseSocket(socket_handle);
        return kInvalidSocket;
    }
    int enabled = 1;
    setsockopt(socket_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    return socket_handle;
}

int SelfTest(const std::filesystem::path& mappings) {
    ConfigureSdlHints();
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR | SDL_INIT_HAPTIC)) {
        std::fprintf(stderr, "SDL3 input self-test failed: %s\n", SDL_GetError());
        return 2;
    }
    if (!mappings.empty())
        SDL_AddGamepadMappingsFromFile(mappings.string().c_str());
    if (!SteamDeckDecoderSelfTest()) {
        std::fprintf(stderr, "SDL3 input self-test failed: Steam Deck report decoder\n");
        SDL_Quit();
        return 3;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int index = 0; ids != nullptr && index < count; ++index) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[index]);
        if (gamepad == nullptr)
            continue;
        std::fprintf(stderr, "SDL3 gamepad %d: name=%s vid=%04x pid=%04x gyro=%s\n", index,
                     SDL_GetGamepadName(gamepad) != nullptr ? SDL_GetGamepadName(gamepad) : "unknown",
                     SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad),
                     SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO) ? "yes" : "no");
        SDL_CloseGamepad(gamepad);
    }
    SDL_free(ids);
#if defined(__linux__)
    int raw_deck_interfaces = 0;
    SDL_hid_device_info* hid_devices = SDL_hid_enumerate(kValveVendorId, kSteamDeckProductId);
    for (SDL_hid_device_info* candidate = hid_devices; candidate != nullptr; candidate = candidate->next) {
        if (candidate->interface_number == kSteamDeckControllerInterface) {
            ++raw_deck_interfaces;
        }
    }
    SDL_hid_free_enumeration(hid_devices);
    std::fprintf(stderr, "SDL3 Steam Deck physical gyro interfaces=%d\n", raw_deck_interfaces);
#endif
    const int version = SDL_GetVersion();
    std::fprintf(stderr, "SDL3 input self-test passed: version=%d.%d.%d gamepads=%d\n", SDL_VERSIONNUM_MAJOR(version),
                 SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), count);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = ParseOptions(argc, argv);
    if (!options.has_value()) {
        std::fprintf(stderr, "Usage: DKR-R-InputHost --port PORT --token HEX [--mappings FILE]\n"
                             "       DKR-R-InputHost --self-test [--mappings FILE]\n");
        return 1;
    }
    if (options->self_test)
        return SelfTest(options->mappings);

#if defined(_WIN32)
    WSADATA socket_data{};
    if (WSAStartup(MAKEWORD(2, 2), &socket_data) != 0)
        return 2;
#endif
    NativeSocket socket_handle = Connect(options->port);
    if (socket_handle == kInvalidSocket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return 3;
    }

    HelloPacket hello{};
    initialise(hello, PacketType::Hello, options->token);
    ConfigureSdlHints();
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR | SDL_INIT_HAPTIC)) {
        hello.ready = 0U;
        CopyText(hello.detail, std::string("SDL3 initialization failed: ") + SDL_GetError());
        SendAll(socket_handle, &hello, sizeof(hello));
        CloseSocket(socket_handle);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 4;
    }
    if (!options->mappings.empty()) {
        SDL_AddGamepadMappingsFromFile(options->mappings.string().c_str());
    }
    const int version = SDL_GetVersion();
    hello.ready = 1U;
    hello.sdl_major = static_cast<std::uint8_t>(SDL_VERSIONNUM_MAJOR(version));
    hello.sdl_minor = static_cast<std::uint8_t>(SDL_VERSIONNUM_MINOR(version));
    hello.sdl_micro = static_cast<std::uint8_t>(SDL_VERSIONNUM_MICRO(version));
    CopyText(hello.detail, "SDL3 native gamepad and sensor subsystems ready.");
    if (!SendAll(socket_handle, &hello, sizeof(hello))) {
        SDL_Quit();
        CloseSocket(socket_handle);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 5;
    }

    std::vector<HostDevice> devices;
    SteamDeckRawGyro raw_deck_gyro;
    std::int32_t next_public_instance = 1;
    RefreshDevices(devices, next_public_instance);
    std::uint64_t sequence = 0U;
    StatePacket previous_state{};
    bool previous_state_valid = false;
    const auto input_epoch = std::chrono::steady_clock::now();
    auto last_state_change = input_epoch;
    auto last_state_send = input_epoch - std::chrono::milliseconds{100};
    std::uint64_t state_packets_sent = 0U;
    std::uint64_t changed_packets_sent = 0U;
    std::uint64_t heartbeat_packets_sent = 0U;
    std::uint64_t active_iterations = 0U;
    std::uint64_t idle_iterations = 0U;
    constexpr auto kActivePollInterval = std::chrono::milliseconds{4};
    constexpr auto kIdlePollInterval = std::chrono::milliseconds{16};
    constexpr auto kActiveWindow = std::chrono::milliseconds{250};
    constexpr auto kHeartbeatInterval = std::chrono::milliseconds{100};
    bool running = true;
    while (running) {
        SDL_Event event{};
        bool refresh = false;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED ||
                event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
                refresh = true;
            } else if (event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE && event.gsensor.sensor == SDL_SENSOR_GYRO) {
                const auto device = std::find_if(devices.begin(), devices.end(), [&](const HostDevice& candidate) {
                    return candidate.sdl_id == event.gsensor.which;
                });
                if (device != devices.end()) {
                    std::copy_n(event.gsensor.data, 3, device->gyro_data.begin());
                    device->gyro_timestamp_us = event.gsensor.sensor_timestamp / 1000U;
                    device->gyro_valid = true;
                }
            }
        }
        if (refresh)
            RefreshDevices(devices, next_public_instance);
        raw_deck_gyro.Pump(devices);
        const CommandResult command = ProcessCommand(socket_handle, options->token, devices);
        if (command != CommandResult::Continue) {
            running = false;
            continue;
        }
        StatePacket state{};
        initialise(state, PacketType::State, options->token);
        state.device_count = static_cast<std::uint32_t>(std::min(devices.size(), state.devices.size()));
        for (std::size_t index = 0U; index < state.device_count; ++index) {
            FillDevicePacket(state.devices[index], devices[index]);
        }
        const bool state_changed =
            !previous_state_valid || state.device_count != previous_state.device_count ||
            std::memcmp(state.devices.data(), previous_state.devices.data(), sizeof(state.devices)) != 0;
        const auto now = std::chrono::steady_clock::now();
        if (state_changed)
            last_state_change = now;
        const bool heartbeat_due = now - last_state_send >= kHeartbeatInterval;
        if (state_changed || heartbeat_due) {
            state.sequence = ++sequence;
            if (!SendAll(socket_handle, &state, sizeof(state))) {
                running = false;
                continue;
            }
            previous_state = state;
            previous_state_valid = true;
            last_state_send = now;
            ++state_packets_sent;
            if (state_changed) {
                ++changed_packets_sent;
            } else {
                ++heartbeat_packets_sent;
            }
        }
        if (now - last_state_change < kActiveWindow) {
            ++active_iterations;
            std::this_thread::sleep_for(kActivePollInterval);
        } else {
            ++idle_iterations;
            std::this_thread::sleep_for(kIdlePollInterval);
        }
    }

    const double input_elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - input_epoch).count();
    std::fprintf(stderr,
                 "[perf][sdl3-host] elapsed=%.2fs packets=%llu changed=%llu "
                 "heartbeats=%llu active-polls=%llu idle-polls=%llu\n",
                 input_elapsed_seconds, static_cast<unsigned long long>(state_packets_sent),
                 static_cast<unsigned long long>(changed_packets_sent),
                 static_cast<unsigned long long>(heartbeat_packets_sent),
                 static_cast<unsigned long long>(active_iterations), static_cast<unsigned long long>(idle_iterations));

    raw_deck_gyro.Shutdown();
    for (HostDevice& device : devices)
        CloseDevice(device);
    SDL_Quit();
    CloseSocket(socket_handle);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
