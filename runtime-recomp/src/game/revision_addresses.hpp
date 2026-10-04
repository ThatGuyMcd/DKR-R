#pragma once

#include "rom_revision.hpp"

#include <cstdint>

// These are decomp symbol addresses, not guessed offsets. The selected ROM
// chooses one immutable address table inside the shared frontend/runtime.
// Mutable aliases preserve the existing call sites without compiling a second
// renderer, window, input stack, or UI for another retail revision.
namespace dkr::runtime::revision_addresses {

struct AddressTable {
    std::uint32_t EntrypointStackTop;
    std::uint32_t AspMainTextStart;
    std::uint32_t MusicPlayer;
    std::uint32_t MusicBaseVolume;
    std::uint32_t BlockMusicChange;
    std::uint32_t MusicNextSequence;
    std::uint32_t FrustumReference;
    std::uint32_t ScreenViewports;
    std::uint32_t OrthoMatrix;
    std::uint32_t IsInRace;
    std::uint32_t LevelLoadTimer;
    std::uint32_t CurrentRngSeed;
    std::uint32_t PreviousRngSeed;
    std::uint32_t LogicUpdateRate;
    std::uint32_t TracksMode;
    std::uint32_t NumberOfReadyPlayers;
    std::uint32_t NumberOfActivePlayers;
    std::uint32_t CurrentMenuId;
    std::uint32_t ActiveMagicCodes;
    std::uint32_t UnlockedMagicCodes;
    std::uint32_t TrophyRaceWorldId;
    std::uint32_t WaveSelectionMap;
    std::uint32_t WaveModel;
    std::uint32_t WaveHeightIndices;
    std::uint32_t WaveGenCount;
    std::uint32_t WaveGenList;
    std::uint32_t WaveGenObjects;
    std::uint32_t MusicTempo;
    std::uint32_t DynamicMusicChannelMask;
    std::uint32_t ObjectCount;
    std::uint32_t ObjectList;
    std::uint32_t ObjectListStart;
    std::uint32_t ObjectMapSpawnList;
    std::uint32_t ObjectMapSize;
    std::uint32_t RaceEndTimer;
    std::uint32_t RaceEndStage;
    std::uint32_t PathUpdateOff;
    std::uint32_t RaceFinishTriggered;
    std::uint32_t NumberOfFinishedRacers;
    std::uint32_t ObjectCurrentMatrix;
    std::uint32_t Racers;
    std::uint32_t RacersByPort;
    std::uint32_t NumberOfRacers;
    std::uint32_t TrackDisplayList;
    std::uint32_t SceneActiveCamera;
    std::uint32_t ShadowHeapFlip;
    std::uint32_t ShadowHeapTriangles;
    std::uint32_t ShadowHeapVertices;
    std::uint32_t ShadowHeapData;
    std::uint32_t VoidLateralX;
    std::uint32_t VoidLateralZ;
    std::uint32_t VoidCentreX;
    std::uint32_t VoidCentreZ;
    std::uint32_t RaceStartTimer;
    std::uint32_t RaceStartProgress;
    std::uint32_t Cameras;
    std::uint32_t PlayerIdMap;
    std::uint32_t ViewportLayout;
    std::uint32_t ActiveCameraId;
    std::uint32_t CurrentCameraFov;
    std::uint32_t CutsceneCameraActive;
    std::uint32_t ViewProjectionMatrix;
    std::uint32_t CurrentMapId;
    std::uint32_t CurrentLevelHeader;
    std::uint32_t SpTaskNumber;
    std::uint32_t IsPaused;
    std::uint32_t PostRaceViewport;
    std::uint32_t TextureCache;
    std::uint32_t NumberOfLoadedTextures;
    std::uint32_t MenuSelectedCharacter;
    std::uint32_t MenuCurrentCharacter;
    std::uint32_t ActivePlayersArray;
    std::uint32_t CharacterSelectStatus;
    std::uint32_t PlayersCharacterArray;
    std::uint32_t CharacterIdSlots;
    std::uint32_t PlayerSelectVehicle;
    std::uint32_t MenuButtons;
    std::uint32_t NumberOfGameplayPlayers;
    std::uint32_t CurrentHud;
    std::uint32_t PlayerHud;
    std::uint32_t HudNumPlayers;
    std::uint32_t HudDisplayList;
    std::uint32_t HudColour;
    std::uint32_t RumblePresent;
    std::uint32_t TitleDemoIndex;
    std::uint32_t TitleRevealTimer;
    std::uint32_t WaveController;
    std::uint32_t NumberOfLevelSegments;
    std::uint32_t WavePowerBase;
    std::uint32_t WaveMagnitude;
    std::uint32_t WavePowerDivisor;
    std::uint32_t IsInTracksMenu;
    std::uint32_t GameMode;
    std::uint32_t MenuStage;
    std::uint32_t MenuDelay;
    std::uint32_t PostraceFinishState;
    std::uint32_t WaveTexUVMaskX;
    std::uint32_t WaveTexUVMaskY;
    std::uint32_t CurrentSequence;
    std::uint32_t SequenceTable;       // gSequenceTable (ALSeqFile *)
    std::uint32_t SequenceLengths;     // gSeqLengthTable (u32 *)
    std::uint32_t SequenceSoundTable;  // gSeqSoundTable (MusicData *)
};

inline constexpr AddressTable kUsV77{
    0x80120AC0U,
    0x800D7600U,
    0x800DC630U,
    0x800DC638U,
    0x800DC648U,
    0x800DC65CU,
    0x800DC8ACU,
    0x800DD064U,
    0x800DD2B8U,
    0x800DD31CU,
    0x800DD394U,
    0x800DD434U,
    0x800DD438U,
    0x800DD404U,
    0x800DF4B8U,
    0x800DF480U,
    0x800DF4BCU,
    0x800DF470U,
    0x800DFD98U,
    0x800DFD9CU,
    0x800E0FE8U,
    0x800E30D4U,
    0x800E30D8U,
    0x800E3044U,
    0x800E3188U,
    0x800E3190U,
    0x800E3194U,
    0x80115D30U,
    0x80115F7CU,
    0x8011AE5CU,
    0x8011AE58U,
    0x8011AE60U,
    0x8011AE98U,
    0x8011AEA0U,
    0x8011AD4EU,
    0x8011AD50U,
    0x8011ADACU,
    0x8011ADB4U,
    0x8011ADC0U,
    0x8011AE90U,
    0x8011AEE4U,
    0x8011AEECU,
    0x8011AEF0U,
    0x8011B0A0U,
    0x8011B0B0U,
    0x8011B0C8U,
    0x8011D320U,
    0x8011D338U,
    0x8011D350U,
    0x8011D4A0U,
    0x8011D4A4U,
    0x8011D4ACU,
    0x8011D4B0U,
    0x8011D540U,
    0x8011D544U,
    0x80120AC0U,
    0x80121150U,
    0x80120CE0U,
    0x80120CE4U,
    0x80120D10U,
    0x80120D14U,
    0x80120F20U,
    0x80121164U,
    0x80121168U,
    0x801234E8U,
    0x80123515U,
    0x80123516U,
    0x80126328U,
    0x80126330U,
    0x801263B4U,
    0x801263C0U,
    0x801263D4U,
    0x801263DCU,
    0x801263E8U,
    0x801263F0U,
    0x801269C0U,
    0x801267D8U,
    0x80126D37U,
    0x80126CDCU,
    0x80126CE0U,
    0x80126D0CU,
    0x80126CFCU, // gHudDL (gHudMtx is the following word, not the display list)
    0x800E2834U,
    0x801241E5U,
    0x80126864U,
    0x8012686CU,
    0x80129FC8U,
    0x8012A0E0U,
    0x8012A720U,
    0x8012A724U,
    0x8012A728U,
    0x800E097CU,
    0x801234ECU,
    0x801263E0U,
    0x800DF47CU,
    0x80126C28U,
    0x8012A094U, // gWaveTexUVMaskX, verified v77 ELF symbol
    0x8012A098U, // gWaveTexUVMaskY
    0x80115D04U,
    0x80115CF8U,
    0x80115D0CU,
    0x80115D1CU,
};

inline constexpr AddressTable kUsV80{
    0x80121040U,
    0x800D7B60U,
    0x800DCBA0U,
    0x800DCBA8U,
    0x800DCBB8U,
    0x800DCBCCU,
    0x800DCE1CU,
    0x800DD5D4U,
    0x800DD828U,
    0x800DD88CU,
    0x800DD904U,
    0x800DD9A4U,
    0x800DD9A8U,
    0x800DD974U,
    0x800DFA38U,
    0x800DFA00U,
    0x800DFA3CU,
    0x800DF9F0U,
    0x800E0318U,
    0x800E031CU,
    0x800E1568U,
    0x800E3664U,
    0x800E3668U,
    0x800E35D4U,
    0x800E3718U,
    0x800E3720U,
    0x800E3724U,
    0x801162B0U,
    0x801164FCU,
    0x8011B3DCU,
    0x8011B3D8U,
    0x8011B3E0U,
    0x8011B418U,
    0x8011B420U,
    0x8011B2CEU,
    0x8011B2D0U,
    0x8011B32CU,
    0x8011B334U,
    0x8011B340U,
    0x8011B410U,
    0x8011B464U,
    0x8011B46CU,
    0x8011B470U,
    0x8011B620U,
    0x8011B630U,
    0x8011B648U,
    0x8011D8A0U,
    0x8011D8B8U,
    0x8011D8D0U,
    0x8011DA20U,
    0x8011DA24U,
    0x8011DA2CU,
    0x8011DA30U,
    0x8011DAC0U,
    0x8011DAC4U,
    0x80121040U,
    0x801216D0U,
    0x80121260U,
    0x80121264U,
    0x80121290U,
    0x80121294U,
    0x801214A0U,
    0x801216E4U,
    0x801216E8U,
    0x80123A68U,
    0x80123A95U,
    0x80123A96U,
    0x801268C8U,
    0x801268D0U,
    0x80126954U,
    0x80126960U,
    0x80126974U,
    0x8012697CU,
    0x80126988U,
    0x80126990U,
    0x80126F80U,
    0x80126D78U,
    0x801272F7U,
    0x8012729CU,
    0x801272A0U,
    0x801272CCU,
    0x801272BCU, // gHudDL (gHudMtx is the following word, not the display list)
    0x800E2DB4U,
    0x80124765U,
    0x80126E04U,
    0x80126E0CU,
    0x8012A588U,
    0x8012A6A0U,
    0x8012ACE0U,
    0x8012ACE4U,
    0x8012ACE8U,
    0x800E0EFCU,
    0x80123A6CU,
    0x80126980U,
    0x800DF9FCU,
    0x801271E8U,
    0x8012A654U, // gWaveTexUVMaskX, verified v80 ELF symbol
    0x8012A658U, // gWaveTexUVMaskY
    0x80116284U,
    0x80116278U,
    0x8011628CU,
    0x8011629CU,
};

inline rom::Revision gSelectedRevision = rom::Revision::UsV77;
inline std::uint32_t WaveTexUVMaskX = kUsV77.WaveTexUVMaskX;
inline std::uint32_t WaveTexUVMaskY = kUsV77.WaveTexUVMaskY;
inline std::uint32_t EntrypointStackTop = kUsV77.EntrypointStackTop;
inline std::uint32_t AspMainTextStart = kUsV77.AspMainTextStart;
inline std::uint32_t MusicPlayer = kUsV77.MusicPlayer;
inline std::uint32_t MusicBaseVolume = kUsV77.MusicBaseVolume;
inline std::uint32_t BlockMusicChange = kUsV77.BlockMusicChange;
inline std::uint32_t MusicNextSequence = kUsV77.MusicNextSequence;
inline std::uint32_t FrustumReference = kUsV77.FrustumReference;
inline std::uint32_t ScreenViewports = kUsV77.ScreenViewports;
inline std::uint32_t OrthoMatrix = kUsV77.OrthoMatrix;
inline std::uint32_t IsInRace = kUsV77.IsInRace;
inline std::uint32_t LevelLoadTimer = kUsV77.LevelLoadTimer;
inline std::uint32_t CurrentRngSeed = kUsV77.CurrentRngSeed;
inline std::uint32_t PreviousRngSeed = kUsV77.PreviousRngSeed;
inline std::uint32_t LogicUpdateRate = kUsV77.LogicUpdateRate;
inline std::uint32_t TracksMode = kUsV77.TracksMode;
inline std::uint32_t NumberOfReadyPlayers = kUsV77.NumberOfReadyPlayers;
inline std::uint32_t NumberOfActivePlayers = kUsV77.NumberOfActivePlayers;
inline std::uint32_t CurrentMenuId = kUsV77.CurrentMenuId;
inline std::uint32_t ActiveMagicCodes = kUsV77.ActiveMagicCodes;
inline std::uint32_t UnlockedMagicCodes = kUsV77.UnlockedMagicCodes;
inline std::uint32_t TrophyRaceWorldId = kUsV77.TrophyRaceWorldId;
inline std::uint32_t WaveSelectionMap = kUsV77.WaveSelectionMap;
inline std::uint32_t WaveModel = kUsV77.WaveModel;
inline std::uint32_t WaveHeightIndices = kUsV77.WaveHeightIndices;
inline std::uint32_t WaveGenCount = kUsV77.WaveGenCount;
inline std::uint32_t WaveGenList = kUsV77.WaveGenList;
inline std::uint32_t WaveGenObjects = kUsV77.WaveGenObjects;
inline std::uint32_t MusicTempo = kUsV77.MusicTempo;
inline std::uint32_t DynamicMusicChannelMask = kUsV77.DynamicMusicChannelMask;
inline std::uint32_t ObjectCount = kUsV77.ObjectCount;
inline std::uint32_t ObjectList = kUsV77.ObjectList;
inline std::uint32_t ObjectListStart = kUsV77.ObjectListStart;
inline std::uint32_t ObjectMapSpawnList = kUsV77.ObjectMapSpawnList;
inline std::uint32_t ObjectMapSize = kUsV77.ObjectMapSize;
inline std::uint32_t RaceEndTimer = kUsV77.RaceEndTimer;
inline std::uint32_t RaceEndStage = kUsV77.RaceEndStage;
inline std::uint32_t PathUpdateOff = kUsV77.PathUpdateOff;
inline std::uint32_t RaceFinishTriggered = kUsV77.RaceFinishTriggered;
inline std::uint32_t NumberOfFinishedRacers = kUsV77.NumberOfFinishedRacers;
inline std::uint32_t ObjectCurrentMatrix = kUsV77.ObjectCurrentMatrix;
inline std::uint32_t Racers = kUsV77.Racers;
inline std::uint32_t RacersByPort = kUsV77.RacersByPort;
inline std::uint32_t NumberOfRacers = kUsV77.NumberOfRacers;
inline std::uint32_t TrackDisplayList = kUsV77.TrackDisplayList;
inline std::uint32_t SceneActiveCamera = kUsV77.SceneActiveCamera;
inline std::uint32_t ShadowHeapFlip = kUsV77.ShadowHeapFlip;
inline std::uint32_t ShadowHeapTriangles = kUsV77.ShadowHeapTriangles;
inline std::uint32_t ShadowHeapVertices = kUsV77.ShadowHeapVertices;
inline std::uint32_t ShadowHeapData = kUsV77.ShadowHeapData;
inline std::uint32_t VoidLateralX = kUsV77.VoidLateralX;
inline std::uint32_t VoidLateralZ = kUsV77.VoidLateralZ;
inline std::uint32_t VoidCentreX = kUsV77.VoidCentreX;
inline std::uint32_t VoidCentreZ = kUsV77.VoidCentreZ;
inline std::uint32_t RaceStartTimer = kUsV77.RaceStartTimer;
inline std::uint32_t RaceStartProgress = kUsV77.RaceStartProgress;
inline std::uint32_t Cameras = kUsV77.Cameras;
inline std::uint32_t PlayerIdMap = kUsV77.PlayerIdMap;
inline std::uint32_t ViewportLayout = kUsV77.ViewportLayout;
inline std::uint32_t ActiveCameraId = kUsV77.ActiveCameraId;
inline std::uint32_t CurrentCameraFov = kUsV77.CurrentCameraFov;
inline std::uint32_t CutsceneCameraActive = kUsV77.CutsceneCameraActive;
inline std::uint32_t ViewProjectionMatrix = kUsV77.ViewProjectionMatrix;
inline std::uint32_t CurrentMapId = kUsV77.CurrentMapId;
inline std::uint32_t CurrentLevelHeader = kUsV77.CurrentLevelHeader;
inline std::uint32_t SpTaskNumber = kUsV77.SpTaskNumber;
inline std::uint32_t IsPaused = kUsV77.IsPaused;
inline std::uint32_t PostRaceViewport = kUsV77.PostRaceViewport;
inline std::uint32_t TextureCache = kUsV77.TextureCache;
inline std::uint32_t NumberOfLoadedTextures = kUsV77.NumberOfLoadedTextures;
inline std::uint32_t MenuSelectedCharacter = kUsV77.MenuSelectedCharacter;
inline std::uint32_t MenuCurrentCharacter = kUsV77.MenuCurrentCharacter;
inline std::uint32_t ActivePlayersArray = kUsV77.ActivePlayersArray;
inline std::uint32_t CharacterSelectStatus = kUsV77.CharacterSelectStatus;
inline std::uint32_t PlayersCharacterArray = kUsV77.PlayersCharacterArray;
inline std::uint32_t CharacterIdSlots = kUsV77.CharacterIdSlots;
inline std::uint32_t PlayerSelectVehicle = kUsV77.PlayerSelectVehicle;
inline std::uint32_t MenuButtons = kUsV77.MenuButtons;
inline std::uint32_t NumberOfGameplayPlayers = kUsV77.NumberOfGameplayPlayers;
inline std::uint32_t CurrentHud = kUsV77.CurrentHud;
inline std::uint32_t PlayerHud = kUsV77.PlayerHud;
inline std::uint32_t HudNumPlayers = kUsV77.HudNumPlayers;
inline std::uint32_t HudDisplayList = kUsV77.HudDisplayList;
inline std::uint32_t HudColour = kUsV77.HudColour;
inline std::uint32_t RumblePresent = kUsV77.RumblePresent;
inline std::uint32_t TitleDemoIndex = kUsV77.TitleDemoIndex;
inline std::uint32_t TitleRevealTimer = kUsV77.TitleRevealTimer;
inline std::uint32_t WaveController = kUsV77.WaveController;
inline std::uint32_t NumberOfLevelSegments = kUsV77.NumberOfLevelSegments;
inline std::uint32_t WavePowerBase = kUsV77.WavePowerBase;
inline std::uint32_t WaveMagnitude = kUsV77.WaveMagnitude;
inline std::uint32_t WavePowerDivisor = kUsV77.WavePowerDivisor;
inline std::uint32_t IsInTracksMenu = kUsV77.IsInTracksMenu;
inline std::uint32_t GameMode = kUsV77.GameMode;
inline std::uint32_t MenuStage = kUsV77.MenuStage;
inline std::uint32_t MenuDelay = kUsV77.MenuDelay;
inline std::uint32_t PostraceFinishState = kUsV77.PostraceFinishState;
inline std::uint32_t CurrentSequence = kUsV77.CurrentSequence;
inline std::uint32_t SequenceTable = kUsV77.SequenceTable;
inline std::uint32_t SequenceLengths = kUsV77.SequenceLengths;
inline std::uint32_t SequenceSoundTable = kUsV77.SequenceSoundTable;

inline const AddressTable& table_for(const rom::Revision revision) {
    return revision == rom::Revision::UsV80 ? kUsV80 : kUsV77;
}

inline bool select(const rom::Revision revision) {
    if (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80) {
        return false;
    }

    const AddressTable& table = table_for(revision);
    EntrypointStackTop = table.EntrypointStackTop;
    AspMainTextStart = table.AspMainTextStart;
    MusicPlayer = table.MusicPlayer;
    MusicBaseVolume = table.MusicBaseVolume;
    BlockMusicChange = table.BlockMusicChange;
    MusicNextSequence = table.MusicNextSequence;
    FrustumReference = table.FrustumReference;
    ScreenViewports = table.ScreenViewports;
    OrthoMatrix = table.OrthoMatrix;
    IsInRace = table.IsInRace;
    LevelLoadTimer = table.LevelLoadTimer;
    CurrentRngSeed = table.CurrentRngSeed;
    PreviousRngSeed = table.PreviousRngSeed;
    LogicUpdateRate = table.LogicUpdateRate;
    TracksMode = table.TracksMode;
    NumberOfReadyPlayers = table.NumberOfReadyPlayers;
    NumberOfActivePlayers = table.NumberOfActivePlayers;
    CurrentMenuId = table.CurrentMenuId;
    ActiveMagicCodes = table.ActiveMagicCodes;
    UnlockedMagicCodes = table.UnlockedMagicCodes;
    TrophyRaceWorldId = table.TrophyRaceWorldId;
    WaveSelectionMap = table.WaveSelectionMap;
    WaveModel = table.WaveModel;
    WaveHeightIndices = table.WaveHeightIndices;
    WaveGenCount = table.WaveGenCount;
    WaveGenList = table.WaveGenList;
    WaveGenObjects = table.WaveGenObjects;
    MusicTempo = table.MusicTempo;
    DynamicMusicChannelMask = table.DynamicMusicChannelMask;
    ObjectCount = table.ObjectCount;
    ObjectList = table.ObjectList;
    ObjectListStart = table.ObjectListStart;
    ObjectMapSpawnList = table.ObjectMapSpawnList;
    ObjectMapSize = table.ObjectMapSize;
    RaceEndTimer = table.RaceEndTimer;
    RaceEndStage = table.RaceEndStage;
    PathUpdateOff = table.PathUpdateOff;
    RaceFinishTriggered = table.RaceFinishTriggered;
    NumberOfFinishedRacers = table.NumberOfFinishedRacers;
    ObjectCurrentMatrix = table.ObjectCurrentMatrix;
    Racers = table.Racers;
    RacersByPort = table.RacersByPort;
    NumberOfRacers = table.NumberOfRacers;
    TrackDisplayList = table.TrackDisplayList;
    SceneActiveCamera = table.SceneActiveCamera;
    ShadowHeapFlip = table.ShadowHeapFlip;
    ShadowHeapTriangles = table.ShadowHeapTriangles;
    ShadowHeapVertices = table.ShadowHeapVertices;
    ShadowHeapData = table.ShadowHeapData;
    VoidLateralX = table.VoidLateralX;
    VoidLateralZ = table.VoidLateralZ;
    VoidCentreX = table.VoidCentreX;
    VoidCentreZ = table.VoidCentreZ;
    RaceStartTimer = table.RaceStartTimer;
    RaceStartProgress = table.RaceStartProgress;
    Cameras = table.Cameras;
    PlayerIdMap = table.PlayerIdMap;
    ViewportLayout = table.ViewportLayout;
    ActiveCameraId = table.ActiveCameraId;
    CurrentCameraFov = table.CurrentCameraFov;
    CutsceneCameraActive = table.CutsceneCameraActive;
    ViewProjectionMatrix = table.ViewProjectionMatrix;
    CurrentMapId = table.CurrentMapId;
    CurrentLevelHeader = table.CurrentLevelHeader;
    SpTaskNumber = table.SpTaskNumber;
    IsPaused = table.IsPaused;
    PostRaceViewport = table.PostRaceViewport;
    TextureCache = table.TextureCache;
    NumberOfLoadedTextures = table.NumberOfLoadedTextures;
    MenuSelectedCharacter = table.MenuSelectedCharacter;
    MenuCurrentCharacter = table.MenuCurrentCharacter;
    ActivePlayersArray = table.ActivePlayersArray;
    CharacterSelectStatus = table.CharacterSelectStatus;
    PlayersCharacterArray = table.PlayersCharacterArray;
    CharacterIdSlots = table.CharacterIdSlots;
    PlayerSelectVehicle = table.PlayerSelectVehicle;
    MenuButtons = table.MenuButtons;
    NumberOfGameplayPlayers = table.NumberOfGameplayPlayers;
    CurrentHud = table.CurrentHud;
    PlayerHud = table.PlayerHud;
    HudNumPlayers = table.HudNumPlayers;
    HudDisplayList = table.HudDisplayList;
    HudColour = table.HudColour;
    RumblePresent = table.RumblePresent;
    TitleDemoIndex = table.TitleDemoIndex;
    TitleRevealTimer = table.TitleRevealTimer;
    WaveController = table.WaveController;
    NumberOfLevelSegments = table.NumberOfLevelSegments;
    WavePowerBase = table.WavePowerBase;
    WaveMagnitude = table.WaveMagnitude;
    WavePowerDivisor = table.WavePowerDivisor;
    IsInTracksMenu = table.IsInTracksMenu;
    GameMode = table.GameMode;
    MenuStage = table.MenuStage;
    MenuDelay = table.MenuDelay;
    PostraceFinishState = table.PostraceFinishState;
    WaveTexUVMaskX = table.WaveTexUVMaskX;
    WaveTexUVMaskY = table.WaveTexUVMaskY;
    CurrentSequence = table.CurrentSequence;
    SequenceTable = table.SequenceTable;
    SequenceLengths = table.SequenceLengths;
    SequenceSoundTable = table.SequenceSoundTable;
    gSelectedRevision = revision;
    return true;
}

inline rom::Revision selected_revision() {
    return gSelectedRevision;
}

} // namespace dkr::runtime::revision_addresses
