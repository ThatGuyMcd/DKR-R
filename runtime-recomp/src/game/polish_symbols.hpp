#pragma once
#include <array>
#include <cstdint>
namespace dkr::runtime::presentation {
inline constexpr std::array<std::array<std::uint32_t,8>,2> polish_symbols{{
    {0x800E3690U,0x800E36C8U,0x8012A7E4U,0x8012A7E0U,0x8012A7F0U,0x800E1DF0U,0x8011B0B8U,0x800E30D0U},
    {0x800E3C20U,0x800E3C58U,0x8012ADA4U,0x8012ADA0U,0x8012ADB0U,0x800E2370U,0x8011B638U,0x800E3660U}
}};
}
