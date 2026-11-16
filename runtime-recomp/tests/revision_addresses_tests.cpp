#include "revision_addresses.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::revision_addresses;

    assert(select(dkr::runtime::rom::Revision::UsV80));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV80);
    assert(dkr::runtime::revision_addresses::current().CurrentMenuId == 0x800DF9F0U);
    assert(dkr::runtime::revision_addresses::current().NumberOfActivePlayers == 0x800DFA3CU);
    assert(dkr::runtime::revision_addresses::current().PlayerIdMap == 0x801216D0U);
    assert(dkr::runtime::revision_addresses::current().Racers == 0x8011B464U);
    assert(dkr::runtime::revision_addresses::current().RaceStartTimer == 0x8011DAC0U);
    assert(dkr::runtime::revision_addresses::current().TextureCache == 0x801268C8U);
    assert(dkr::runtime::revision_addresses::current().HudDisplayList == 0x801272BCU);
    assert(dkr::runtime::revision_addresses::current().HudColour == 0x800E2DB4U);
    assert(dkr::runtime::revision_addresses::current().IsInTracksMenu == 0x800E0EFCU);
    assert(dkr::runtime::revision_addresses::current().GameMode == 0x80123A6CU);
    assert(dkr::runtime::revision_addresses::current().MenuStage == 0x80126980U);
    assert(dkr::runtime::revision_addresses::current().MenuDelay == 0x800DF9FCU);
    assert(dkr::runtime::revision_addresses::current().PostraceFinishState == 0x801271E8U);
    assert(dkr::runtime::revision_addresses::current().CurrentSequence == 0x80116284U);
    assert(dkr::runtime::revision_addresses::current().SequenceTable == 0x80116278U);
    assert(dkr::runtime::revision_addresses::current().SequenceLengths == 0x8011628CU);
    assert(dkr::runtime::revision_addresses::current().SequenceSoundTable == 0x8011629CU);
    assert(dkr::runtime::revision_addresses::current().WaveTexUVMaskX == 0x8012A654U);
    assert(dkr::runtime::revision_addresses::current().WaveTexUVMaskY == 0x8012A658U);

    assert(select(dkr::runtime::rom::Revision::UsV77));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV77);
    assert(dkr::runtime::revision_addresses::current().CurrentMenuId == 0x800DF470U);
    assert(dkr::runtime::revision_addresses::current().NumberOfActivePlayers == 0x800DF4BCU);
    assert(dkr::runtime::revision_addresses::current().PlayerIdMap == 0x80121150U);
    assert(dkr::runtime::revision_addresses::current().Racers == 0x8011AEE4U);
    assert(dkr::runtime::revision_addresses::current().RaceStartTimer == 0x8011D540U);
    assert(dkr::runtime::revision_addresses::current().TextureCache == 0x80126328U);
    assert(dkr::runtime::revision_addresses::current().HudDisplayList == 0x80126CFCU);
    assert(dkr::runtime::revision_addresses::current().HudColour == 0x800E2834U);
    assert(dkr::runtime::revision_addresses::current().IsInTracksMenu == 0x800E097CU);
    assert(dkr::runtime::revision_addresses::current().GameMode == 0x801234ECU);
    assert(dkr::runtime::revision_addresses::current().MenuStage == 0x801263E0U);
    assert(dkr::runtime::revision_addresses::current().MenuDelay == 0x800DF47CU);
    assert(dkr::runtime::revision_addresses::current().PostraceFinishState == 0x80126C28U);
    assert(dkr::runtime::revision_addresses::current().CurrentSequence == 0x80115D04U);
    assert(dkr::runtime::revision_addresses::current().SequenceTable == 0x80115CF8U);
    assert(dkr::runtime::revision_addresses::current().SequenceLengths == 0x80115D0CU);
    assert(dkr::runtime::revision_addresses::current().SequenceSoundTable == 0x80115D1CU);
    assert(dkr::runtime::revision_addresses::current().WaveTexUVMaskX == 0x8012A094U);
    assert(dkr::runtime::revision_addresses::current().WaveTexUVMaskY == 0x8012A098U);

    assert(!select(dkr::runtime::rom::Revision::Unsupported));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV77);
    assert(dkr::runtime::revision_addresses::current().CurrentMenuId == 0x800DF470U);

    std::puts("[test][revision-addresses] PASS");
    return 0;
}
