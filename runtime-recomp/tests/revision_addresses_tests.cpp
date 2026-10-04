#include "revision_addresses.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::revision_addresses;

    assert(select(dkr::runtime::rom::Revision::UsV80));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV80);
    assert(CurrentMenuId == 0x800DF9F0U);
    assert(NumberOfActivePlayers == 0x800DFA3CU);
    assert(PlayerIdMap == 0x801216D0U);
    assert(Racers == 0x8011B464U);
    assert(RaceStartTimer == 0x8011DAC0U);
    assert(TextureCache == 0x801268C8U);
    assert(HudDisplayList == 0x801272BCU);
    assert(HudColour == 0x800E2DB4U);
    assert(IsInTracksMenu == 0x800E0EFCU);
    assert(GameMode == 0x80123A6CU);
    assert(MenuStage == 0x80126980U);
    assert(MenuDelay == 0x800DF9FCU);
    assert(PostraceFinishState == 0x801271E8U);
    assert(CurrentSequence == 0x80116284U);
    assert(SequenceTable == 0x80116278U);
    assert(SequenceLengths == 0x8011628CU);
    assert(SequenceSoundTable == 0x8011629CU);
    assert(WaveTexUVMaskX == 0x8012A654U);
    assert(WaveTexUVMaskY == 0x8012A658U);

    assert(select(dkr::runtime::rom::Revision::UsV77));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV77);
    assert(CurrentMenuId == 0x800DF470U);
    assert(NumberOfActivePlayers == 0x800DF4BCU);
    assert(PlayerIdMap == 0x80121150U);
    assert(Racers == 0x8011AEE4U);
    assert(RaceStartTimer == 0x8011D540U);
    assert(TextureCache == 0x80126328U);
    assert(HudDisplayList == 0x80126CFCU);
    assert(HudColour == 0x800E2834U);
    assert(IsInTracksMenu == 0x800E097CU);
    assert(GameMode == 0x801234ECU);
    assert(MenuStage == 0x801263E0U);
    assert(MenuDelay == 0x800DF47CU);
    assert(PostraceFinishState == 0x80126C28U);
    assert(CurrentSequence == 0x80115D04U);
    assert(SequenceTable == 0x80115CF8U);
    assert(SequenceLengths == 0x80115D0CU);
    assert(SequenceSoundTable == 0x80115D1CU);
    assert(WaveTexUVMaskX == 0x8012A094U);
    assert(WaveTexUVMaskY == 0x8012A098U);

    assert(!select(dkr::runtime::rom::Revision::Unsupported));
    assert(selected_revision() == dkr::runtime::rom::Revision::UsV77);
    assert(CurrentMenuId == 0x800DF470U);

    std::puts("[test][revision-addresses] PASS");
    return 0;
}
