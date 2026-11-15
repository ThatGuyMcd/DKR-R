// ROM-free check that current() and the mutable aliases agree after select().
//
// The field list below must match the aliases in revision_addresses.hpp;
// scripts/tests/test_revision_addresses.py fails if it does not.
#include "revision_addresses.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace addresses = dkr::runtime::revision_addresses;
namespace rom = dkr::runtime::rom;

#define DKR_ADDRESS_FIELDS(X)                                                                                          \
    X(WaveTexUVMaskX)                                                                                                  \
    X(WaveTexUVMaskY)                                                                                                  \
    X(EntrypointStackTop)                                                                                              \
    X(AspMainTextStart)                                                                                                \
    X(MusicPlayer)                                                                                                     \
    X(MusicBaseVolume)                                                                                                 \
    X(BlockMusicChange)                                                                                                \
    X(MusicNextSequence)                                                                                               \
    X(FrustumReference)                                                                                                \
    X(ScreenViewports)                                                                                                 \
    X(OrthoMatrix)                                                                                                     \
    X(IsInRace)                                                                                                        \
    X(LevelLoadTimer)                                                                                                  \
    X(CurrentRngSeed)                                                                                                  \
    X(PreviousRngSeed)                                                                                                 \
    X(LogicUpdateRate)                                                                                                 \
    X(TracksMode)                                                                                                      \
    X(NumberOfReadyPlayers)                                                                                            \
    X(NumberOfActivePlayers)                                                                                           \
    X(CurrentMenuId)                                                                                                   \
    X(ActiveMagicCodes)                                                                                                \
    X(UnlockedMagicCodes)                                                                                              \
    X(TrophyRaceWorldId)                                                                                               \
    X(WaveSelectionMap)                                                                                                \
    X(WaveModel)                                                                                                       \
    X(WaveHeightIndices)                                                                                               \
    X(WaveGenCount)                                                                                                    \
    X(WaveGenList)                                                                                                     \
    X(WaveGenObjects)                                                                                                  \
    X(MusicTempo)                                                                                                      \
    X(DynamicMusicChannelMask)                                                                                         \
    X(ObjectCount)                                                                                                     \
    X(ObjectList)                                                                                                      \
    X(ObjectListStart)                                                                                                 \
    X(ObjectMapSpawnList)                                                                                              \
    X(ObjectMapSize)                                                                                                   \
    X(RaceEndTimer)                                                                                                    \
    X(RaceEndStage)                                                                                                    \
    X(PathUpdateOff)                                                                                                   \
    X(RaceFinishTriggered)                                                                                             \
    X(NumberOfFinishedRacers)                                                                                          \
    X(ObjectCurrentMatrix)                                                                                             \
    X(Racers)                                                                                                          \
    X(RacersByPort)                                                                                                    \
    X(NumberOfRacers)                                                                                                  \
    X(TrackDisplayList)                                                                                                \
    X(SceneActiveCamera)                                                                                               \
    X(ShadowHeapFlip)                                                                                                  \
    X(ShadowHeapTriangles)                                                                                             \
    X(ShadowHeapVertices)                                                                                              \
    X(ShadowHeapData)                                                                                                  \
    X(VoidLateralX)                                                                                                    \
    X(VoidLateralZ)                                                                                                    \
    X(VoidCentreX)                                                                                                     \
    X(VoidCentreZ)                                                                                                     \
    X(RaceStartTimer)                                                                                                  \
    X(RaceStartProgress)                                                                                               \
    X(Cameras)                                                                                                         \
    X(PlayerIdMap)                                                                                                     \
    X(ViewportLayout)                                                                                                  \
    X(ActiveCameraId)                                                                                                  \
    X(CurrentCameraFov)                                                                                                \
    X(CutsceneCameraActive)                                                                                            \
    X(ViewProjectionMatrix)                                                                                            \
    X(CurrentMapId)                                                                                                    \
    X(CurrentLevelHeader)                                                                                              \
    X(SpTaskNumber)                                                                                                    \
    X(IsPaused)                                                                                                        \
    X(PostRaceViewport)                                                                                                \
    X(TextureCache)                                                                                                    \
    X(NumberOfLoadedTextures)                                                                                          \
    X(MenuSelectedCharacter)                                                                                           \
    X(MenuCurrentCharacter)                                                                                            \
    X(ActivePlayersArray)                                                                                              \
    X(CharacterSelectStatus)                                                                                           \
    X(PlayersCharacterArray)                                                                                           \
    X(CharacterIdSlots)                                                                                                \
    X(PlayerSelectVehicle)                                                                                             \
    X(MenuButtons)                                                                                                     \
    X(NumberOfGameplayPlayers)                                                                                         \
    X(CurrentHud)                                                                                                      \
    X(PlayerHud)                                                                                                       \
    X(HudNumPlayers)                                                                                                   \
    X(HudDisplayList)                                                                                                  \
    X(HudColour)                                                                                                       \
    X(RumblePresent)                                                                                                   \
    X(TitleDemoIndex)                                                                                                  \
    X(TitleRevealTimer)                                                                                                \
    X(WaveController)                                                                                                  \
    X(NumberOfLevelSegments)                                                                                           \
    X(WavePowerBase)                                                                                                   \
    X(WaveMagnitude)                                                                                                   \
    X(WavePowerDivisor)                                                                                                \
    X(IsInTracksMenu)                                                                                                  \
    X(GameMode)                                                                                                        \
    X(MenuStage)                                                                                                       \
    X(MenuDelay)                                                                                                       \
    X(PostraceFinishState)                                                                                             \
    X(CurrentSequence)                                                                                                 \
    X(SequenceTable)                                                                                                   \
    X(SequenceLengths)                                                                                                 \
    X(SequenceSoundTable)

namespace {

int failures = 0;

void check_revision(const rom::Revision revision, const addresses::AddressTable& expected, const char* name) {
    if (!addresses::select(revision)) {
        std::fprintf(stderr, "select(%s) refused\n", name);
        ++failures;
        return;
    }
    if (&addresses::current() != &expected) {
        std::fprintf(stderr, "current() is not the %s table\n", name);
        ++failures;
    }
    if (addresses::selected_revision() != revision) {
        std::fprintf(stderr, "selected_revision() is wrong for %s\n", name);
        ++failures;
    }
    int checked = 0;
#define X(field)                                                                                                       \
    do {                                                                                                               \
        ++checked;                                                                                                     \
        if (addresses::field != addresses::current().field || addresses::current().field != expected.field) {          \
            std::fprintf(stderr, "%s: alias %s is 0x%08X, table says 0x%08X\n", name, #field,                          \
                         static_cast<unsigned>(addresses::field), static_cast<unsigned>(expected.field));              \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false);
    DKR_ADDRESS_FIELDS(X)
#undef X
    std::printf("%s: %d fields checked\n", name, checked);
}

} // namespace

int main() {
    // Both orders, so a field that select() forgets cannot hide behind the default.
    check_revision(rom::Revision::UsV80, addresses::kUsV80, "v80");
    check_revision(rom::Revision::UsV77, addresses::kUsV77, "v77");
    check_revision(rom::Revision::UsV80, addresses::kUsV80, "v80");

    // A refused revision must leave everything as it was.
    if (addresses::select(static_cast<rom::Revision>(255)) || &addresses::current() != &addresses::kUsV80) {
        std::fprintf(stderr, "a refused select() changed the current table\n");
        ++failures;
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
