#pragma once

#include "revision_symbols.hpp"
#include "rom_revision.hpp"

#include <atomic>
#include <cstdint>

// These are decomp symbol addresses, not guessed offsets. The selected ROM
// chooses one immutable address table inside the shared frontend/runtime.
// Mutable aliases preserve the existing call sites without compiling a second
// renderer, window, input stack, or UI for another retail revision.
namespace dkr::runtime::revision_addresses {

// AddressTable and the immutable kUsV77 / kUsV80 tables are generated from
// runtime-recomp/symbols/revision_addresses.toml (scripts/gen_guest_symbols.py).

// Threading contract.
//
// select() is a startup step. The shipped app calls it once, from
// RegisterGame on the main thread, before the CPU payload's sections are
// registered and before any guest, render or audio thread exists. The
// experimental race frontend and the host tests call it on their own single
// thread before starting anything that reads. Nothing switches revision while
// the game runs.
//
// current() is the one safe read: it goes through a single atomic pointer to
// an immutable table, so a reader sees either the whole old table or the whole
// new one, never a mixture. select() publishes it last, with release ordering,
// so a thread that acquires the new pointer also sees the aliases below.
// The aliases and gSelectedRevision are plain variables written field by field;
// they are only safe to read when no select() can run at the same time, which
// the startup-only rule above guarantees.
inline std::atomic<const AddressTable*> gCurrentTable{&kUsV77};
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

// The table for the selected revision. Prefer this over the aliases below in new
// code; the aliases are being migrated onto it. See the threading contract above.
inline const AddressTable& current() {
    return *gCurrentTable.load(std::memory_order_acquire);
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
    gCurrentTable.store(&table, std::memory_order_release);
    return true;
}

inline rom::Revision selected_revision() {
    return gSelectedRevision;
}

} // namespace dkr::runtime::revision_addresses
