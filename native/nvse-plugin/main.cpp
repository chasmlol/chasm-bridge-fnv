#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <deque>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>
#include <winhttp.h>
#include <memory>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "winhttp.lib")

#include "common/ITypes.h"
#include "nvse/containers.h"
#include "nvse/GameTypes.h"
#include "nvse/nvse_version.h"
#include "nvse/PluginAPI.h"
#include "nvse/GameAPI.h"
#include "nvse/GameData.h"
#include "nvse/GameExtraData.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameEffects.h"
#include "nvse/GameOSDepend.h"
#include "nvse/GameUI.h"

#include "facegen_age.h"

PluginHandle g_pluginHandle = kPluginHandle_Invalid;
NVSEInterface* g_nvse = nullptr;
NVSEMessagingInterface* g_messaging = nullptr;
NVSEScriptInterface* g_scriptInterface = nullptr;
NVSESerializationInterface* g_serialization = nullptr;
NVSEEventManagerInterface* g_eventManager = nullptr;
extern const _FormHeap_Free FormHeap_Free;

namespace
{
namespace fs = std::filesystem;

constexpr char kPluginName[] = "FNVBridgeNative";
constexpr UInt32 kPluginVersion = 1;
constexpr UInt32 kMinimumNvseVersion = MAKE_NEW_VEGAS_VERSION(6, 4, 5);
constexpr char kRequestId[] = "req_live";
constexpr char kInputCallbackRelativePath[] = "fnv_bridge/input_callback.txt";
constexpr int kTextInputWidth = 700;
constexpr int kTextInputHeight = 220;
constexpr int kTextInputMaxLength = 280;
constexpr int kControlMargin = 14;
constexpr float kDefaultSubtitleSeconds = 3.5f;
constexpr float kMaxSubtitleSeconds = 9.0f;
constexpr double kMaxAudibleDistance = 2400.0;
constexpr float kGameUnitsPerMeter = 70.0f;
constexpr float kVoiceMinDistanceMeters = 96.0f / kGameUnitsPerMeter;
constexpr float kVoiceMaxDistanceMeters = static_cast<float>(kMaxAudibleDistance / kGameUnitsPerMeter);
constexpr float kChatNpcSearchRadiusMeters = 10.0f;
constexpr float kGroupChatNearbyRadiusMeters = 10.0f;
constexpr float kGamestateNearbyRadiusMeters = 30.0f;
constexpr SHORT kChatVirtualKey = VK_RETURN;
constexpr SHORT kVoiceChatVirtualKey = VK_MENU;
constexpr SHORT kAdminChatVirtualKey = 'O';
constexpr SHORT kAdminVoiceChatVirtualKey = 'H';
constexpr char kAdminNpcKey[] = "todd";
constexpr char kAdminNpcName[] = "Todd";
constexpr char kBridgeDialoguePluginName[] = "NVBridge.esp";
constexpr char kBridgeDialogueQuestEditorId[] = "NVBridgeQuest";
constexpr char kBridgeConversationPackageEditorId[] = "NVBConversationPackage";
constexpr UInt32 kBridgeDialogueTopicLocalFormId = 0x00001000;
constexpr UInt32 kBridgeConversationPackageLocalFormId = 0x00002002;
constexpr UInt32 kFaceGenPhonemeCount = 16;
constexpr DWORD kSpeechEnvelopeWindowMs = 40;
constexpr DWORD kSpeechAnimationUpdateIntervalMs = 50;
constexpr DWORD kSpeechBindingValidationIntervalMs = 500;
constexpr DWORD kSpeechAnimationTailMs = 120;
constexpr DWORD kSpeechBindingRetryMs = 500;
constexpr DWORD kConversationReleaseDelayMs = 5000;
constexpr DWORD kConversationFaceUpdateIntervalMs = 900;
constexpr DWORD kConversationModeFaceRefreshIntervalMs = 1000;
constexpr DWORD kConversationLookRefreshIntervalMs = 1500;
constexpr DWORD kConversationPackageRefreshIntervalMs = 5000;
constexpr DWORD kTextInputEmptySubmitGraceMs = 250;
constexpr DWORD kTextInputInvisibleRecoveryMs = 1000;
constexpr DWORD kModLocalFormRetryMs = 30000;
constexpr DWORD kDirectSoundIdleReleaseDelayMs = 5000;
constexpr DWORD kVoicePlaybackSampleRate = 44100;
constexpr WORD kVoicePlaybackChannels = 1;
constexpr WORD kVoicePlaybackBitsPerSample = 16;
constexpr WORD kWaveFormatPcm = 0x0001;
constexpr WORD kWaveFormatIeeeFloat = 0x0003;
constexpr WORD kWaveFormatExtensible = 0xFFFE;
constexpr double kVoicePlaybackHeadroom = 0.84;
constexpr double kVoicePlaybackFadeMs = 3.0;
constexpr float kSpeechSilenceThreshold = 0.035f;
constexpr float kSpeechMaxWeight = 0.95f;
constexpr float kSpeechMinWeight = 0.18f;
constexpr float kConversationFaceTurnThresholdDegrees = 20.0f;
constexpr float kConversationModeReleaseDistanceMeters = 10.0f;
constexpr DWORD kVoiceCaptureSampleRate = 16000;
constexpr WORD kVoiceCaptureBitsPerSample = 16;
constexpr WORD kVoiceCaptureChannels = 1;
constexpr DWORD kVoiceCaptureBufferMs = 200;
constexpr size_t kVoiceCaptureBufferCount = 4;
constexpr DWORD kVoiceCaptureMinimumMs = 180;
constexpr DWORD kDebugConfigPollMs = 1000;
constexpr DWORD kRuntimeHeartbeatIntervalMs = 100;
constexpr DWORD kStreamedSpeechEndPaddingMs = 40;
constexpr DWORD kDefaultStreamingChunkOverlapMs = 40;

// Phase 3 single-buffer streaming: capacity (seconds) of the one DirectSound buffer
// that holds a whole NPC utterance, written incrementally as mini-chunks arrive and
// played continuously. 30s covers any line plus opener/remainder gaps; a new
// utterance (new request id) starts a fresh buffer.
constexpr DWORD kStreamingVoiceMaxSeconds = 30;
// Lead (ms of audio) to keep the write cursor ahead of the play cursor when
// recovering from an underrun (e.g. the opener->remainder gap), so freshly written
// audio is never placed behind the play cursor (which would skip it).
constexpr DWORD kStreamingVoiceLeadMs = 60;
// Grace after the last append before declaring a played-out utterance done (guards
// against stopping during a brief inter-chunk gap).
constexpr DWORD kStreamingVoiceEndGraceMs = 250;
// Caption chunking (DISPLAY ONLY): default ms of speech per character, used to time
// the reveal of each on-screen caption segment against playback (~75ms/char ≈ 13
// cps). Tunable via native_debug.cfg `caption_ms_per_char`. Never affects audio.
constexpr DWORD kDefaultCaptionMsPerChar = 75;
// Coalesce the 200ms streaming mini-chunks into lip-sync windows of this size, so the
// face animation is (re)started ~once per window instead of ~5x/sec per mini-chunk
// (which restarted the FaceGen animation constantly -> lag, jank, and crashes). ~one
// StartSpeechAnimation per window, close to the old per-sentence cadence.
constexpr DWORD kLipSyncWindowMs = 800;
constexpr DWORD kMaxStreamingChunkOverlapMs = 250;
constexpr unsigned long long kRuntimeHeartbeatHistoryMaxBytes = 8ull * 1024ull * 1024ull;
constexpr DWORD kStackBootstrapCooldownMs = 15000;
constexpr DWORD kSaveStateSyncTimeoutMs = 8000;
constexpr DWORD kSaveStateAckPollMs = 100;
constexpr DWORD kSaveStateSyncHudCooldownMs = 1500;
constexpr char kDefaultFollowPackageEditorId[] = "DefaultFollowPlayerFar";
// Movement engine: the Travel package (editor id aaChasmTravel, location = Near
// Linked Reference) in NVCompanions.esp at local form id 0x000c00. To travel, we set
// the NPC's linked ref to the destination and add this package; the engine paths
// them there — through load doors, overriding their routine. Resolved by mod-local
// form id (robust — no editor-id-at-runtime dependency), like the dialogue forms.
constexpr char kCompanionsPluginName[] = "NVCompanions.esp";
constexpr UInt32 kChasmTravelPackageLocalFormId = 0x00000c00;
constexpr char kNativeActionCommandVersion2[] = "NVBRIDGE_ACTION_V2";
constexpr char kTrustedFNVActionEngine[] = "fallout-new-vegas:xnvse";
constexpr size_t kMaxTrustedExecutionScriptBytes = 256ull * 1024ull;

struct ResponsePayload
{
    int statusCode = 0;
    bool ok = false;
    bool isFinal = true;
    std::string requestId;
    std::string npcKey;
    std::string npcName;
    std::string playerText;
    std::string audioFile;
    std::string text;
    std::string error;
    int audioChunkIndex = -1;
    std::string actionNpcKey;
    std::string actionNpcName;
    bool nonPositionalAudio = false;
    std::string gameMasterAction;
    std::string actionId;
    std::string actionBookId;
    std::string executionEngine;
    std::string executionTemplateId;
    std::string executionLanguage;
    std::string executionScript;
    std::vector<std::string> executionArguments;
    double gameMasterConfidence = 0.0;
    bool gameMasterShouldTrigger = false;
};

enum class TrustedExecutionArgumentType
{
    Ref,
    Form,
    Number,
    String
};

struct TrustedExecutionArgument
{
    TrustedExecutionArgumentType type = TrustedExecutionArgumentType::Ref;
    TESObjectREFR* ref = nullptr;
    TESForm* form = nullptr;
    double number = 0.0;
    std::string text;
};

struct SpeakerSnapshot
{
    UInt32 refId = 0;
    UInt32 baseId = 0;  // base form (TESNPC/TESCreature) FormID; detects FormID recycling
                        // when a cell unloads (a recycled ref keeps refId but changes base).
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    bool valid = false;
};

struct LocationSnapshot
{
    std::string major;
    std::string minor;
    std::string cell;
    std::string worldspace;
    std::string region;
    // True when inside a building (interior cell) — the game's own IsInterior()
    // flag. Surfaced to the scenario as `inside_or_outside`/`interior` macros so
    // the prompt reads differently in vs out (they were previously identical).
    bool interior = false;
};

struct ActiveSound
{
    IDirectSoundBuffer8* buffer = nullptr;
    IDirectSound3DBuffer* buffer3d = nullptr;
    SpeakerSnapshot speaker;
};

struct QueuedAudioChunk
{
    std::string requestId;
    fs::path wavPath;
    std::string audioFile;
    std::string speakerKey;
    std::string speakerName;
    std::string subtitleText;
    std::string publishedAtIso;
    int chunkIndex = -1;
    bool nonPositional = false;
    int captionMaxChars = -1; // display-only caption split hint; -1 = unset (whole line)
};

// Phase 3: a chunk awaiting its lip-sync trigger. Audio is written into the single
// streaming buffer ahead of playback, but `StartSpeechAnimation` is wall-clock /
// amplitude-envelope driven and must fire when the chunk's audio actually plays, so
// we hold each chunk's PCM + metadata and fire it at `startMs` into playback.
struct PendingStreamLipSync
{
    DWORD startMs = 0;     // playback offset (ms from stream start) when this plays
    DWORD durationMs = 0;
    std::vector<BYTE> pcm; // chunk PCM (for the amplitude envelope)
    WAVEFORMATEX format{};
    std::string requestId;
    std::string audioFile;
    std::string subtitleText;
    SpeakerSnapshot speaker;
};

struct ResolvedNpcTarget
{
    TESObjectREFR* ref = nullptr;
    std::string npcKey;
    std::string npcName;
    std::string voiceTypeKey;
    std::string voiceTypeName;
    double distanceSquared = DBL_MAX;
    bool underCrosshair = false;
};

struct NearbyNpcCandidate
{
    TESObjectREFR* ref = nullptr;
    std::string npcKey;
    std::string npcName;
    std::string voiceTypeKey;
    std::string voiceTypeName;
    double distanceSquared = DBL_MAX;
    bool underCrosshair = false;
};

struct FaceGenKeyframeMultiple32
{
    void* vtbl = nullptr;
    UInt32 type = 0;
    float unk0C = 0.0f;
    float* values = nullptr;
    UInt32 count = 0;
    UInt8 isUpdated = 0;
    UInt8 pad1D = 0;
    UInt16 pad1E = 0;
};
static_assert(sizeof(FaceGenKeyframeMultiple32) == 0x18);

struct FaceGenNiNodeRuntime
{
    BYTE pad000[0x0DC];
    void* spAnimationData = nullptr;
    BYTE pad0E0[0x026];
    UInt8 bAnimationUpdate = 0;
    UInt8 bRotatedLastUpdate = 0;
    UInt8 bApplyRotationToParent = 0;
    BYTE pad109[3]{};
    float fLastTime = -1.0f;
    UInt8 bUsingLoResHead = 0;
    UInt8 bIAmPlayerCharacter = 0;
    UInt8 bIAmInDialouge = 0;
    UInt8 pad113 = 0;
    void* pActor = nullptr;
};
static_assert(offsetof(FaceGenNiNodeRuntime, spAnimationData) == 0x0DC);
static_assert(offsetof(FaceGenNiNodeRuntime, bAnimationUpdate) == 0x106);
static_assert(offsetof(FaceGenNiNodeRuntime, bIAmInDialouge) == 0x112);

struct FaceAnimationBinding
{
    UInt32 speakerRefId = 0;
    FaceGenNiNodeRuntime* faceNode = nullptr;
    void* animationData = nullptr;
    FaceGenKeyframeMultiple32* phonemeKeyframe = nullptr;
    FaceGenKeyframeMultiple32* modifierKeyframe = nullptr;
};

struct VisemeCue
{
    DWORD startMs = 0;
    DWORD endMs = 0;
    UInt32 phonemeId = 0;
    float emphasis = 1.0f;
};

struct ActiveSpeechAnimation
{
    bool active = false;
    std::string requestId;
    SpeakerSnapshot speaker;
    DWORD startedTick = 0;
    DWORD durationMs = 0;
    DWORD envelopeWindowMs = kSpeechEnvelopeWindowMs;
    // Extra mouth-open multiplier on top of the global speechWeightScale. 1.0 for
    // normal speech; songs use a higher value so the singing lip movement is
    // exaggerated + readable (a sung mix is quieter/flatter than clean speech).
    float weightGain = 1.0f;
    DWORD lastBindingAttemptTick = 0;
    DWORD lastBindingValidationTick = 0;
    DWORD lastWeightsUpdateTick = 0;
    bool loggedBindingFailure = false;
    bool firstWeightsAppliedLogged = false;
    FaceAnimationBinding binding;
    std::vector<float> envelope;
    std::vector<VisemeCue> visemes;
    std::array<float, kFaceGenPhonemeCount> lastWeights{};
    std::array<float, kFaceGenPhonemeCount> originalWeights{};
    UInt32 originalPhonemeCount = 0;
    UInt8 originalPhonemeIsUpdated = 0;
    UInt8 originalFaceAnimationUpdate = 0;
    UInt8 originalFaceInDialogue = 0;
    bool originalBindingStateCaptured = false;
};

struct ConversationHoldState
{
    bool active = false;
    // True while the held NPC is in combat: we deliberately do NOT lock them into
    // the conversation package / restraint (that "freeze and face the player" is
    // what clears their combat state and makes them chat calmly). They keep
    // fighting and answer over the top.
    bool combatMode = false;
    bool scriptPackageApplied = false;
    bool conversationIssued = false;
    bool preserveFurnitureState = false;
    bool lookApplied = false;
    bool conversationModeApplied = false;
    bool originalRestrainedKnown = false;
    bool originalRestrained = false;
    bool restrainedApplied = false;
    bool noMovePackageApplied = false;
    std::string npcKey;
    std::string npcName;
    SpeakerSnapshot speaker;
    DWORD releaseTick = 0;
    DWORD lastFaceUpdateTick = 0;
    DWORD lastBodyFaceUpdateTick = 0;
    DWORD lastPackageCheckTick = 0;
    float lastAppliedFacingDegrees = FLT_MAX;
};

struct ModLocalFormCacheEntry
{
    TESForm* form = nullptr;
    UInt32 runtimeFormId = 0;
    UInt8 modIndex = 0xFF;
    DWORD nextRetryTick = 0;
    bool modMissingLogged = false;
    bool formMissingLogged = false;
};

struct VoiceCaptureBuffer
{
    WAVEHDR header{};
    std::vector<BYTE> storage;
};

struct VoiceCaptureState
{
    bool keyDownLastFrame = false;
    bool adminKeyDownLastFrame = false;
    bool active = false;
    bool transcribing = false;
    bool adminMode = false;
    std::string npcKey;
    std::string npcName;
    SpeakerSnapshot speaker;
    HWAVEIN waveIn = nullptr;
    DWORD startedTick = 0;
    DWORD subtitleRefreshTick = 0;
    std::vector<BYTE> capturedPcm;
    std::vector<VoiceCaptureBuffer> buffers;
};

struct RuntimeState
{
    bool keyDownLastFrame = false;
    bool adminKeyDownLastFrame = false;
    bool awaitingInput = false;
    bool bridgeTextInputOwned = false;
    bool awaitingReply = false;
    bool awaitingVoiceReply = false;
    bool inputMenuSeenVisible = false;
    bool inputEnterDownLastFrame = false;
    bool inputEscapeDownLastFrame = false;
    DWORD inputEmptyEnterCancelTick = 0;
    DWORD inputStartedTick = 0;
    DWORD staleTextInputCloseRetryTick = 0;
    bool gameWindowFocusedLastFrame = false;
    DWORD ignoreHotkeysUntilTick = 0;
    bool loadedIntoGame = false;
    std::string pendingNpcKey;
    std::string pendingNpcName;
    SpeakerSnapshot pendingSpeaker;
    IDirectSound8* directSound = nullptr;
    IDirectSoundBuffer* primaryBuffer = nullptr;
    IDirectSound3DListener* listener3d = nullptr;
    DWORD directSoundIdleSinceTick = 0;
    DWORD replyStartedTick = 0;
    DWORD lastBridgeActivityTick = 0;
    bool sawBridgeActivity = false;
    std::string activeRequestId;
    int lastAudioChunkIndex = -1;
    bool subtitleShownForReply = false;
    bool dialogSubtitleActive = false;
    DWORD dialogSubtitleHideTick = 0;
    std::string replySubtitleText;
    std::string lastNpcKey;
    std::string lastNpcName;
    SpeakerSnapshot lastNpcSpeaker;
    std::unordered_map<std::string, SpeakerSnapshot> npcSpeakersByKey;
    DWORD activeSpeechUntilTick = 0;
    bool streamedAudioSeenForReply = false;
    std::string traceRequestId;
    ULONGLONG traceStartedTick = 0;
    ActiveSpeechAnimation speechAnimation;
    std::vector<ActiveSound> activeSounds;
    std::deque<QueuedAudioChunk> pendingAudioChunks;
    // --- Phase 3 single-buffer streaming voice (gated on DebugConfig.singleBufferStreaming) ---
    IDirectSoundBuffer8* streamBuffer = nullptr;
    IDirectSound3DBuffer* streamBuffer3d = nullptr;
    bool streamActive = false;       // a streaming buffer exists for the current utterance
    bool streamStarted = false;      // Play() has been called
    DWORD streamCapacityBytes = 0;   // buffer size
    DWORD streamWriteCursor = 0;     // next byte offset to write real audio at
    DWORD streamPlayStartTick = 0;   // tick when Play() began (lip-sync scheduling)
    DWORD streamLastAppendTick = 0;  // tick of the last chunk write (end grace)
    DWORD streamCumulativeMs = 0;    // total audio ms appended (next chunk's startMs)
    WAVEFORMATEX streamFormat{};     // buffer format (from the first chunk)
    std::string streamRequestId;     // utterance this buffer serves
    SpeakerSnapshot streamSpeaker;
    bool streamNonPositional = false;          // 2D (player-centered) vs 3D positional
    bool streamSpeakerOrphaned = false;        // speaker unloaded/recycled mid-utterance:
                                               // freeze audio at last pos, never rebind
    std::deque<PendingStreamLipSync> streamLipSyncQueue;
    // Caption timeline (display only): one segment per synthesized SENTENCE. chasm
    // streams LLM->TTS at sentence granularity, and each sentence's audio begins with a
    // chunk carrying that sentence's caption text (later chunks of the same sentence
    // carry EMPTY text). Each segment is anchored to the audio-buffer ms where its
    // sentence starts, so UpdateStreamingVoice swaps the caption in sync with playback
    // across EVERY sentence of the reply (not just the first).
    int captionMaxChars = -1;                  // -1 unset (display hint, forwarded by backend)
    std::vector<std::string> captionSegments;  // one caption per sentence, in play order
    std::vector<DWORD> captionSegmentStartMs;  // audio-buffer ms where each segment begins
    int captionCurrentIndex = -1;              // segment currently on screen
    std::string captionSourceText;             // accumulated full line so far (lip-sync visemes)
    DWORD captionLastShowTick = 0;             // last (re)show of the current caption (refresh timer)
    // Phase 3 lip-sync coalescing: accumulate mini-chunks into ~kLipSyncWindowMs
    // windows so the face animation isn't restarted per 200ms chunk.
    std::vector<BYTE> lipSyncAccumPcm;
    DWORD lipSyncAccumMs = 0;
    DWORD lipSyncAccumStartMs = 0;
    SpeakerSnapshot lipSyncAccumSpeaker;
    std::unordered_set<std::string> movementActionRequestIds;
    ConversationHoldState conversationHold;
    std::string lastPlaybackDiagnostics;
    DWORD lastDebugConfigPollTick = 0;
    DWORD lastRuntimeHeartbeatTick = 0;
    ULONGLONG runtimeHeartbeatFrame = 0;
    bool voiceBootstrapSubtitleActive = false;
    DWORD voiceBootstrapStatusPollTick = 0;
    DWORD voiceBootstrapSubtitleRefreshTick = 0;
    std::string voiceBootstrapMessage;
    bool saveStateSyncPending = false;
    std::string saveStateSyncEventId;
    std::string saveStateSyncType;
    std::string pendingLoadSavePath;
    DWORD saveStateSyncLastPollTick = 0;
    DWORD saveStateSyncHudMessageTick = 0;
    DWORD saveStateSyncStartedTick = 0;
    DWORD stackBootstrapAttemptTick = 0;
    bool stackBootstrapAttempted = false;
    VoiceCaptureState voiceCapture;

    // --- Music: play-a-song (guitar) performance (task/music) ------------------
    // A generated song is delivered out-of-band via the control/songs queue (which
    // is polled unconditionally, unlike the turn-scoped reply) and played
    // positionally from the NPC by ProcessSongDeliveries(). The guitar idle is
    // re-asserted for the song's duration and stopped at the end.
    DWORD songScanTick = 0;          // last control/songs scan (throttle)
    bool songActive = false;         // a performance is live (guitar out and/or song playing)
    DWORD songUntilTick = 0;         // GetTickCount() when the performance ends
    DWORD songReissueTick = 0;       // last guitar-idle re-assert
    SpeakerSnapshot songSpeaker;     // the performing NPC (to re-assert/stop the idle)
    // The guitar is DEFERRED until this turn's spoken reply (TTS) finishes, so the
    // NPC doesn't whip out a guitar mid-sentence while still accepting the request.
    bool pendingGuitar = false;      // a performance is waiting for TTS to end
    SpeakerSnapshot pendingGuitarSpeaker;
    // Which idle the current/pending performance uses: false = guitar
    // (SpecialIdleNVGuitar), true = rap/vocal (SpecialIdleSinging). Set from the
    // action that started it and read by RunGuitarIdle when (re)asserting the idle.
    bool performIsRap = false;
};

enum class BridgeTransport
{
    File,
    Http,
};

struct DebugConfig
{
    // Dialogue-turn transport. File (default) writes/reads the NVBridge request/
    // reply/chunk files exactly as before. Http POSTs the turn to chasm's
    // /api/game/v1/turn and consumes the streaming NDJSON response on a worker
    // thread. Only the dialogue turn moves to HTTP; save-sync + the durable
    // control/actions queues stay on files regardless of this flag.
    BridgeTransport transport = BridgeTransport::File;
    std::string httpHost = "127.0.0.1";
    int httpPort = 7341;
    std::string httpTurnPath = "/api/game/v1/turn";
    bool runtimeHeartbeatEnabled = true;
    bool speechAnimationEnabled = true;
    bool speechWritePhonemeValues = true;
    bool speechWriteFaceFlags = true;
    bool speechClearBindingOnStop = true;
    bool subtitlesEnabled = true;
    bool listenerUpdatesEnabled = true;
    bool directSound3dEnabled = true;
    bool directSoundSoftwareBufferEnabled = true;
    bool drainQueuedChunksAfterFinal = true;
    // Phase 3: play streamed TTS through ONE continuous DirectSound buffer per
    // utterance (seamless, no per-chunk buffer churn) instead of the per-chunk
    // static-buffer queue. Toggle off to fall back to the static path for A/B.
    bool singleBufferStreaming = true;
    bool requestTracingEnabled = false;
    bool conversationModeEnabled = true;
    bool autoStartStack = true;
    float speechWeightScale = 1.0f;
    float conversationModeReleaseDistanceMeters = kConversationModeReleaseDistanceMeters;
    DWORD speechAnimationUpdateIntervalMs = kSpeechAnimationUpdateIntervalMs;
    DWORD speechBindingValidationIntervalMs = kSpeechBindingValidationIntervalMs;
    DWORD conversationModeFaceRefreshIntervalMs = kConversationModeFaceRefreshIntervalMs;
    DWORD conversationLookRefreshIntervalMs = kConversationLookRefreshIntervalMs;
    DWORD runtimeHeartbeatIntervalMs = kRuntimeHeartbeatIntervalMs;
    DWORD streamingChunkOverlapMs = kDefaultStreamingChunkOverlapMs;
    DWORD captionMsPerChar = kDefaultCaptionMsPerChar;
    DWORD stackBootstrapCooldownMs = kStackBootstrapCooldownMs;
    std::string stackLauncherPath;
    std::string bridgeRootPath;
    // --- Player persona capture (docs/persona.md). Uploads use http_host /
    // http_port regardless of `transport` (chasm's HTTP server runs even when
    // dialogue turns ride the file bridge). ---
    bool personaEnabled = true;              // master switch for the whole feature
    std::string personaHttpPath = "/api/game/v1/persona";
};

// The four configurable in-game input bindings, as Win32 virtual keys. Chasm
// writes them to <bridge>\control\hotkeys.cfg (decimal VK codes; see
// LoadHotkeysConfigIfNeeded for the wire format); a missing/invalid file
// leaves the plugin's original hardcoded defaults in place. Note the
// TextEditMenu submit watcher stays hardwired to VK_RETURN — the game menu
// itself always submits on Enter; only the "open chat / talk" keys rebind.
struct HotkeyBindings
{
    SHORT chatVk = kChatVirtualKey;             // enter text (NPCs)
    SHORT voiceVk = kVoiceChatVirtualKey;       // push to talk (NPCs)
    SHORT adminChatVk = kAdminChatVirtualKey;   // enter text (Todd)
    SHORT adminVoiceVk = kAdminVoiceChatVirtualKey; // push to talk (Todd)
};

RuntimeState g_state;
Script* g_openTextInputScript = nullptr;
Script* g_closeTextInputScript = nullptr;
Script* g_startCombatScript = nullptr;
Script* g_setPlayerTeammateScript = nullptr;
Script* g_startConversationScript = nullptr;
Script* g_startLookScript = nullptr;
Script* g_stopLookScript = nullptr;
Script* g_evaluatePackageScript = nullptr;
Script* g_isCurrentPackageScript = nullptr;
Script* g_addScriptPackageScript = nullptr;
Script* g_removeScriptPackageScript = nullptr;
Script* g_getRestrainedScript = nullptr;
Script* g_setRestrainedScript = nullptr;
Script* g_clearRestrainedScript = nullptr;
Script* g_setAngleScript = nullptr;
Script* g_faceObjectScript = nullptr;
bool g_faceObjectScriptAttempted = false;
Script* g_applyNoMovePackageScript = nullptr;
bool g_applyNoMovePackageScriptAttempted = false;
Script* g_runBatchScript = nullptr;
Script* g_consoleCommandScript = nullptr;
// Scheduler clock: GameDaysPassed via a compiled GECK helper. GameHour (0x38) is a
// correct direct read, but the day counter is NOT at 0x26 in FalloutNV, so we let
// the script compiler resolve the `GameDaysPassed` global BY NAME instead of
// guessing its runtime form id. Compiled once, cached.
Script* g_gameDaysPassedScript = nullptr;
bool g_gameDaysPassedScriptAttempted = false;
// Music (task/music): performance idle re-assert + stop, lazily compiled. Guitar =
// SpecialIdleNVGuitar (play-a-song); Sing = SpecialIdleSinging (rap/vocal).
Script* g_playGuitarIdleScript = nullptr;
Script* g_playSingIdleScript = nullptr;
Script* g_stopGuitarIdleScript = nullptr;
bool g_guitarIdleScriptsAttempted = false;
std::unordered_map<std::string, Script*> g_trustedExecutionScripts;
std::unordered_map<std::string, ModLocalFormCacheEntry> g_modLocalFormCache;
DebugConfig g_debugConfig;
fs::file_time_type g_debugConfigWriteTime{};
bool g_debugConfigLoaded = false;
HotkeyBindings g_hotkeys;
fs::file_time_type g_hotkeysWriteTime{};
bool g_hotkeysLoaded = false;
DWORD g_hotkeysLastPollTick = 0;

constexpr UInt32 kInterfaceManagerSingletonAddress = 0x011D8A80;
constexpr UInt32 kPlayerSingletonAddress = 0x011DEA3C;
constexpr UInt32 kOSGlobalsAddress = 0x011DEA0C;
constexpr UInt32 kDataHandlerSingletonAddress = 0x011C3F2C;
constexpr UInt32 kFormsMapAddress = 0x011C54C0;
constexpr UInt32 kQueueUIMessageAddress = 0x007052F0;
constexpr UInt32 kMenuVisibilityArrayAddress = 0x011F308F;
constexpr UInt32 kTileMenuArrayAddress = 0x011F3508;
constexpr UInt32 kCreateFormInstanceAddress = 0x00465110;
constexpr UInt32 kTempMenuByTypeAddress = 0x00707990;
constexpr UInt32 kHudUpdateVisibilityStateAddress = 0x00771700;
constexpr UInt32 kTraitNameToIdAddress = 0x00A01860;

using QueueUiMessageFn = bool (*)(const char*, UInt32, const char*, const char*, float, bool);
QueueUiMessageFn g_queueUiMessage = reinterpret_cast<QueueUiMessageFn>(kQueueUIMessageAddress);
using CreateFormInstanceFn = TESForm* (*)(UInt8);
CreateFormInstanceFn g_createFormInstance = reinterpret_cast<CreateFormInstanceFn>(kCreateFormInstanceAddress);
using TempMenuByTypeFn = Menu * (*)(UInt32);
TempMenuByTypeFn g_tempMenuByType = reinterpret_cast<TempMenuByTypeFn>(kTempMenuByTypeAddress);
using HudUpdateVisibilityStateFn = void (*)(signed int);
HudUpdateVisibilityStateFn g_hudUpdateVisibilityState = reinterpret_cast<HudUpdateVisibilityStateFn>(kHudUpdateVisibilityStateAddress);
using TraitNameToIdFn = UInt32 (*)(const char*);
TraitNameToIdFn g_traitNameToId = reinterpret_cast<TraitNameToIdFn>(kTraitNameToIdAddress);

// --- Companions (docs/companions-architecture.md) ---------------------------
// Template pool shipped in our own esp; slots claimed at runtime and configured
// (name/face/follow) natively. Frozen local formid map — never renumber.
constexpr char kCompanionsEspName[] = "NVCompanions.esp";
constexpr UInt32 kCompanionPoolSize = 64;
constexpr UInt32 kCompanionFemaleSlotStart = 32; // slots 32..63 are female templates
constexpr UInt32 kCompanionBaseLocalId = 0x000800; // + slot -> NPC_ template
constexpr UInt32 kCompanionRefLocalId = 0x000900;  // + slot -> persistent ACHR
constexpr UInt32 kRaceSexMenuType = 1036;          // 0x40C, chargen menu
constexpr SHORT kCompanionHotkeyVirtualKey = VK_F7; // manual trigger: process queue / face design / summon
constexpr char kCompanionCommandVersion[] = "CHASM_COMPANION_V1";
constexpr char kCompanionAckVersion[] = "CHASM_COMPANION_ACK_V1";
constexpr char kCompanionRegistryVersion[] = "CHASM_COMPANION_REGISTRY_V1";
constexpr DWORD kCompanionPollIntervalMs = 500;
constexpr DWORD kCompanionMenuOpenTimeoutMs = 8000;
// Engine addresses (runtime 1.4.0.525; cross-verified between xNVSE and JIP
// sources — see docs/companions-architecture.md. The Steam exe is DRM-packed
// on disk, so CompanionsLogEngineProbes() logs the live bytes at first init
// for ground-truth diagnostics.)
constexpr UInt32 kTESNPCCopyAppearanceAddress = 0x00603790;   // thiscall(dest, TESNPC* src)
constexpr UInt32 kActorBaseDataFlagSetterAddress = 0x0047DD50; // thiscall(&baseData, mask, value, 1)
constexpr UInt32 kCharacterRebuild3DAddress = 0x008D3FA0;     // thiscall(Character*) — regen incl. facegen head
constexpr UInt32 kPlayerRaceChangeAddress = 0x0060B240;       // thiscall(playerBaseNPC, TESRace*, 0)
constexpr UInt32 kLoadFaceGenHeadEGTFilesAddress = 0x011D5AE0; // cached ini bool: runtime facegen tinting

struct CompanionAppearance
{
    bool valid = false;
    float fggs[50]{};
    float fgga[30]{};
    float fgts[50]{};
    std::string raceRef;                 // "Mod.esm:LOCALHEX" strings — load-order safe
    std::string hairRef;
    std::string eyesRef;
    std::vector<std::string> headPartRefs;
    UInt32 hairColor = 0;
    float hairLength = 1.0f;
    float height = 1.0f;
    float weight = 1.0f;
    bool female = false;
};

struct CompanionSlot
{
    bool claimed = false;
    std::string npcKey;        // stable chat identity (slug of name at create)
    std::string name;          // display name (UTF-8; engine gets ASCII projection)
    std::string characterName; // chasm character card name
    std::string voice;         // informational (chasm owns voice mapping)
    bool female = false;
    bool faceDesigned = false;
    bool waiting = false;      // M3 wait-state
    std::string status = "unclaimed"; // unclaimed|claimed|spawned|dismissed
    CompanionAppearance appearance;
};

// Chargen round-trip session (one at a time).
struct CompanionFaceSession
{
    // 0 idle, 1 awaiting focus/conditions, 2 awaiting menu open, 3 menu open, 4 unused
    int phase = 0;
    UInt32 slot = 0;
    std::string requestId;
    std::string op;
    bool spawnAfter = false;
    DWORD menuOpenDeadlineTick = 0;
    // Alt-tabbing auto-pauses FNV into the Start menu; we close blocking menus
    // before opening chargen (paced retries, bounded).
    DWORD nextMenuCloseTick = 0;
    int menuCloseAttempts = 0;
    CompanionAppearance playerSnapshot;
};

struct CompanionsRuntime
{
    bool registryLoaded = false;
    UInt32 rev = 0;
    CompanionSlot slots[kCompanionPoolSize];
    CompanionFaceSession face;
    UInt32 slotBaseFormIds[kCompanionPoolSize]{}; // runtime formids, cached once resolved
    DWORD nextPollTick = 0;
    bool engineProbesLogged = false;
    bool hotkeyDownLastFrame = false;
};
CompanionsRuntime g_companions;
Script* g_companionMoveToTargetScript = nullptr;
Script* g_companionMoveToHoldScript = nullptr;
Script* g_companionShowRaceMenuScript = nullptr;

fs::path BridgeDir();
fs::path VoiceBootstrapStatusPath();
fs::path SaveStateControlDir();
fs::path SaveStateEventsDir();
fs::path SaveStateAcksDir();
fs::path NativeActionCommandDir();
fs::path SaveStateEventPath(const std::string& eventId);
fs::path SaveStateAckPath(const std::string& eventId);
void EnsureBridgeDirectories();
void MaybeRequestBridgeStackStartup(const char* reason, bool force = false);
std::string GenerateSaveStateEventId();
bool DispatchSaveStateEvent(const std::string& eventType, const std::string& savePath, bool waitForAck);
void PollSaveStateSyncAck();
std::string Trim(std::string value);
std::string ToLowerAscii(std::string value);
void ShowGeneralSubtitle(const std::string& speaker, const std::string& text, float seconds);
void ShowHudMessage(const std::string& message);
void ClearDialogSubtitle();
bool ShowDialogSubtitle(const std::string& speaker, const std::string& text, float seconds);
std::string ToUiAscii(std::string_view value);
void InterruptBridgeReplyAndPlayback(const char* reason);
void StopGuitarPerformance(const char* reason);
bool SetActorRestrainedState(TESObjectREFR* actorRef, bool restrained);
void CancelHttpTurn();
bool HasPendingChunkFiles();
void ClearOutboxArtifacts(const char* reason);
bool HasQueuedOrPlayingReply();
void ClearIdleOutboxArtifacts(const char* reason);
PlayerCharacter* GetPlayer();
TESObjectREFR* ResolveSpeakerRef(const SpeakerSnapshot& speaker);
void RememberNpcTarget(const std::string& npcKey, const std::string& npcName, const SpeakerSnapshot& speaker);
std::optional<SpeakerSnapshot> ResolveSpeakerSnapshotForNpc(const std::string& npcKey, const std::string& npcName);
SpeakerSnapshot CaptureSpeakerSnapshot(TESObjectREFR* ref);
LocationSnapshot CapturePlayerLocation();
bool WriteRequest(const std::string& npcKey, const std::string& npcName, const std::string& text, const LocationSnapshot& location, const std::string& metadataJson = "", bool clearSpeechSidecar = true, const std::vector<BYTE>* httpVoiceWav = nullptr, bool nonPositionalHint = false);
bool WriteVoiceRequest(const std::string& npcKey, const std::string& npcName, const SpeakerSnapshot& speaker, const std::vector<BYTE>& wavBytes, const LocationSnapshot& location, bool adminMode = false);
DataHandler* GetDataHandler();
std::string FormIdHex(UInt32 formId);
std::optional<UInt32> ParseTrustedFormId(const std::string& rawValue);
std::string DescribeScriptResult(const NVSEArrayVarInterface::Element& result);
std::vector<std::string> BuildRunBatchScriptPathCandidates(const fs::path& scriptPath);
bool ExecuteConsoleCommand(TESObjectREFR* callingRef, const std::string& command);
bool IsActorUsingPackage(TESObjectREFR* actorRef, TESForm* packageForm, bool* outKnown = nullptr);
bool IsActorUsingBridgeConversationPackage(TESObjectREFR* actorRef, bool* outKnown = nullptr);
bool EvaluateActorPackage(TESObjectREFR* actorRef);
bool AddActorScriptPackage(TESObjectREFR* actorRef, TESForm* packageForm, const char* packageEditorId, const char* traceStage);
bool RemoveActorScriptPackage(TESObjectREFR* actorRef, const char* traceStage);
bool SetActorPlayerTeammate(TESObjectREFR* actorRef, bool enabled = true, const char* traceStage = "game_master_follow_teammate");
TESForm* ResolveDefaultFollowPackage();
double DistanceSquared3D(const TESObjectREFR* left, const TESObjectREFR* right);
UInt32 MakeWorldCellKey(SInt32 x, SInt32 y);
bool ShouldPreserveActorConversationAnimation(TESObjectREFR* speakerRef);
void StopSpeechAnimation();
void UpdateSpeechAnimation();
// Phase 3 single-buffer streaming voice (defined later; declared here for the
// reply-teardown sites that run before the definitions).
void StopStreamingVoice(const char* reason);
void UpdateStreamingVoice();
void DrainChunksToStreamingVoice();
void ShutdownDirectSound();
void UpdateConversationHold();
void ReleaseConversationHold(const char* reason);
void UpdateVoiceBootstrapStatus();
void UpdateVoiceCaptureHotkey();
void PollVoiceCaptureBuffers();
void PollNativeActionCommands();
void AbortVoiceCapture(const char* reason, bool releaseHold = true);
void FinishVoiceCaptureAndSubmit();
bool GameWindowHasFocus();
void ResetTextInputKeyWatcher();
void ClearTextInputKeyWatcher();
void PrimeHotkeyEdgeStateFromKeyboard();
void LoadDebugConfigIfNeeded(bool force = false);
void LoadHotkeysConfigIfNeeded(bool force = false);
void WriteRuntimeHeartbeatIfNeeded(bool force = false);
// Scheduler: read a vanilla TESGlobal float (GameDaysPassed 0x26, GameHour 0x38)
// so the runtime heartbeat + turn metadata can report the in-game clock to chasm.
bool ReadGlobalFloat(UInt32 formId, float& out);
bool ReadGameDaysPassed(float& out);
std::string FormatClockFloat(float value);
// Scheduler travel: the named map locations within `radiusMeters` of the player,
// closest first (comma-separated), so chasm can offer them as travel destinations.
std::string BuildNearbyLocationsMacro(PlayerCharacter* player, size_t maxCount);
// Movement engine: dump all map markers (name + world pos + form id) for chasm.
void WriteLocationsManifestIfNeeded(PlayerCharacter* player, bool force = false);

// Movement engine Phase 2: per-NPC travel status the plugin reports back to chasm
// via the heartbeat, so chasm knows whether a traveller is currently loaded (being
// walked by a real package) or unloaded (needs the off-screen sim), and can
// re-anchor its simulation on the NPC's ACTUAL position — no backward-teleport
// when the player walks away mid-journey.
struct TravelerStatus
{
    bool loaded = false;
    // Reached the target — used for journeys to a MOVING target (player / another
    // NPC) whose position chasm can't measure itself, so the plugin signals arrival.
    bool arrived = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    ULONGLONG updatedMs = 0;
    // Last time we (re)issued the walk package; throttled so we don't reset the
    // actor's path every tick (which would stutter the animation).
    ULONGLONG lastWalkIssueMs = 0;
    // Whether the NPC is currently inside a building (interior cell). The chasm
    // movement engine reads this: while inside, it has the plugin step them out the
    // front door; once outside, it simulates the walk (offscreen) or hands off to
    // the live package (loaded).
    bool interior = false;
    // The id of the journey this status is FOR. Chasm scopes `arrived` to it so a
    // brand-new journey never reads a leftover `arrived=true` from the last trip
    // (which would mark it done instantly). Reset clears `arrived` on a new id.
    std::string journeyId;
    // The display name of the building the NPC is currently INSIDE ("" when outside).
    // Chasm compares it to the destination to tell "inside the saloon" = arrived from
    // "inside his own shop" = still needs to leave.
    std::string building;
};
std::map<std::string, TravelerStatus> g_travelers;

// Movement engine: building entrances discovered from teleport doors. Buildings
// (the saloon, a shop) aren't map markers — they're interior CELLS reached through
// a load door. We map each interior cell NAME ("Prospector Saloon") to its EXTERIOR
// door's world position + form id, so a named building resolves to a walkable spot.
// Accumulated over the session (keyed by lowercased name) since a door is only
// enumerable while its exterior cell is loaded; once seen it stays known.
struct BuildingEntrance
{
    std::string name;
    // The EXTERIOR front door (outside the building — the walk-to target).
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    UInt32 formId = 0;
    // The linked INTERIOR door (just inside — where an NPC steps through to when
    // travelling to the INSIDE of a building).
    float ix = 0.0f;
    float iy = 0.0f;
    float iz = 0.0f;
    UInt32 interiorFormId = 0;
};
std::map<std::string, BuildingEntrance> g_buildingEntrances;
void UpdatePersonaCapture();
void ResetPersonaStateForSession();
void TraceRequestEvent(const std::string& requestId, const std::string& stage,
    const std::vector<std::pair<std::string, std::string>>& stringFields = {},
    const std::vector<std::pair<std::string, double>>& numberFields = {},
    const std::vector<std::pair<std::string, bool>>& boolFields = {});
bool IsLiveNearbyActorRef(const TESObjectREFR* anchorRef, TESObjectREFR* ref);
void CollectNearbyMappedNpcAround(const TESObjectREFR* anchorRef, TESObjectREFR* ref, double maxDistanceSquared, bool underCrosshair, std::vector<NearbyNpcCandidate>& candidates);
std::pair<std::string, std::string> ResolveRefVoiceTypeMetadata(TESObjectREFR* ref);
// Companions (implementations live in the companions section before HandleNvseMessage).
void PollCompanionCommands(bool force = false);
void UpdateCompanionFaceDesignSession();
void UpdateCompanionHotkey();
void CompanionsOnDeferredInit();
void CompanionsOnSessionReady(const char* reason);
std::optional<std::pair<std::string, std::string>> ResolveCompanionNpcForRef(TESObjectREFR* ref);
TESForm* ResolveModLocalForm(const char* modName, UInt32 localFormId);
// Game event log (extraction + aggregation; see the GAME EVENT LOG section).
fs::path GameEventsDir();
void RegisterGameEventHandlers();
void ResetGameEventRuntime(const char* reason);
void QueueGameEvent(const char* type, const std::string& summary,
    const std::vector<std::pair<std::string, std::string>>& actors = {},
    const std::string& locationOverride = std::string());
void FlushGameEvents(bool force);
void UpdateGameEventLog();
void NoteConversationRequestForEventLog(const std::string& npcKey, const std::string& npcName);

TESForm* LookupFormByIdRuntime(UInt32 refId)
{
    auto* formsMap = *reinterpret_cast<NiTPointerMap<TESForm>**>(kFormsMapAddress);
    return formsMap ? formsMap->Lookup(refId) : nullptr;
}

void LogLine(const char* fmt, ...)
{
    char buffer[2048]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");

    if (!g_nvse)
    {
        return;
    }

    EnsureBridgeDirectories();
    std::ofstream out(BridgeDir() / "native_plugin.log", std::ios::binary | std::ios::app);
    out << buffer << "\r\n";
}

fs::path RuntimeDir()
{
    return fs::path(g_nvse->GetRuntimeDirectory());
}

fs::path DataDir()
{
    return RuntimeDir() / "Data";
}

// Resolve a Win32 environment variable to a filesystem path (wide, so non-ASCII
// user names survive). Returns an empty path if the variable is unset/empty.
fs::path EnvPath(const wchar_t* name)
{
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
    {
        return {};
    }

    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed)
    {
        return {};
    }

    value.resize(written);
    if (value.empty())
    {
        return {};
    }

    return fs::path(value);
}

// Base dir for the chasm <-> plugin rendezvous, matching chasm's resolution EXACTLY:
//   1. CHASM_BRIDGE_ROOT (if set & non-empty) is the FULL bridge path -> used as-is.
//   2. otherwise <base>\chasm\bridge, where <base> is %LOCALAPPDATA%, else %APPDATA%,
//      else the system temp dir.
// This path lives OUTSIDE Mod Organizer 2's virtual filesystem, so the plugin's
// writes land on real disk where the separate chasm process actually reads them.
fs::path DefaultBridgeDir()
{
    const fs::path override = EnvPath(L"CHASM_BRIDGE_ROOT");
    if (!override.empty())
    {
        return override;
    }

    fs::path base = EnvPath(L"LOCALAPPDATA");
    if (base.empty())
    {
        base = EnvPath(L"APPDATA");
    }
    if (base.empty())
    {
        std::error_code ec;
        base = fs::temp_directory_path(ec);
    }

    return base / "chasm" / "bridge";
}

fs::path ResolveConfiguredRuntimePath(const std::string& rawPath)
{
    if (rawPath.empty())
    {
        return {};
    }

    fs::path resolved(rawPath);
    if (resolved.is_relative())
    {
        resolved = RuntimeDir() / resolved;
    }

    return resolved.lexically_normal();
}

fs::path BridgeDir()
{
    if (!g_debugConfig.bridgeRootPath.empty())
    {
        const fs::path configured = ResolveConfiguredRuntimePath(g_debugConfig.bridgeRootPath);
        if (!configured.empty())
        {
            return configured;
        }
    }

    return DefaultBridgeDir();
}

fs::path BridgeStorageRootDir()
{
    const fs::path bridgeDir = BridgeDir();
    const fs::path parent = bridgeDir.parent_path();
    if (!parent.empty())
    {
        return parent;
    }

    return DataDir();
}

fs::path SaveStateControlDir()
{
    return BridgeDir() / "control";
}

fs::path SaveStateEventsDir()
{
    return SaveStateControlDir() / "events";
}

fs::path SaveStateAcksDir()
{
    return SaveStateControlDir() / "acks";
}

fs::path NativeActionCommandDir()
{
    return SaveStateControlDir() / "actions";
}

fs::path GameEventsDir()
{
    return SaveStateControlDir() / "gameevents";
}

std::string SafeEventStem(std::string value)
{
    for (char& ch : value)
    {
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
        {
            ch = '_';
        }
    }
    return value;
}

fs::path SaveStateEventPath(const std::string& eventId)
{
    return SaveStateEventsDir() / (SafeEventStem(eventId) + ".txt");
}

fs::path SaveStateAckPath(const std::string& eventId)
{
    return SaveStateAcksDir() / (SafeEventStem(eventId) + ".txt");
}

fs::path InboxPath()
{
    return BridgeDir() / "inbox" / "req_live.txt";
}

fs::path SttInboxAudioPath()
{
    return BridgeDir() / "inbox" / "req_live.stt.wav";
}

fs::path UiSubmitPath()
{
    // The in-game text-input channel. This file is produced by the embedded GECK
    // callback script (EnsureInputCallbackScript) via the engine's WriteStringToFile,
    // which can ONLY target a path relative to the game root -> Data/NVBridge. It is
    // therefore pinned here rather than under BridgeDir(): chasm never reads or writes
    // ui_submit.txt (it is purely the player-typed-text -> plugin path), so it does not
    // belong to the chasm rendezvous layout. Keeping it game-relative keeps the writer
    // (GECK script) and reader (this plugin) in agreement even though BridgeDir() now
    // lives at %LOCALAPPDATA%\chasm\bridge.
    return DataDir() / "NVBridge" / "ui_submit.txt";
}

fs::path OutboxPath()
{
    return BridgeDir() / "outbox" / "req_live.txt";
}

// A plain folder OUTSIDE MO2's virtual filesystem (usvfs). Files under the game
// Data / mods / overwrite tree are virtualized, so every file open from INSIDE the
// game costs ~tens of ms (the streamed-chunk in-game lag — the helper, which runs
// outside MO2, writes each file in ~2ms but the plugin reads it in ~80ms). The
// streamed chunk commands + audio live here for native-speed reads; the helper
// writes the identical path (%LOCALAPPDATA%\NVBridgeStream).
fs::path StreamStorageDir()
{
    char buf[MAX_PATH] = { 0 };
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
        return fs::path(buf) / "NVBridgeStream";
    }
    return DefaultBridgeDir() / "stream";
}

fs::path OutboxChunkDir()
{
    return StreamStorageDir() / "chunks";
}

fs::path UserFunctionDir()
{
    return DataDir() / "NVSE" / "user_defined_functions" / "fnv_bridge";
}

fs::path ScriptRunnerDir()
{
    return DataDir() / "NVSE" / "Plugins" / "scripts";
}

fs::path InputCallbackScriptPath()
{
    return UserFunctionDir() / "input_callback.txt";
}

fs::path AudioDir()
{
    // Native-speed folder outside usvfs (see StreamStorageDir). Was under the game's
    // Sound/Voice tree (virtualized), which made per-chunk WAV reads ~tens of ms.
    return StreamStorageDir() / "audio";
}

fs::path DiagnosticsPath()
{
    return BridgeDir() / "native_state.txt";
}

fs::path ScriptRunnerTracePath()
{
    return BridgeDir() / "script_runner_trace.txt";
}

fs::path DebugConfigPath()
{
    return DefaultBridgeDir() / "native_debug.cfg";
}

fs::path HotkeysConfigPath()
{
    // Written by chasm (ui::hotkeys save + bridge startup), read-only here.
    // Lives with the other chasm<->plugin control files under BridgeDir().
    return BridgeDir() / "control" / "hotkeys.cfg";
}

fs::path RuntimeHeartbeatPath()
{
    return BridgeDir() / "runtime_heartbeat.json";
}

fs::path RuntimeHeartbeatHistoryPath()
{
    return BridgeDir() / "runtime_heartbeat_history.jsonl";
}

fs::path DefaultStackLauncherPath()
{
    // The stack is managed externally by chasm (it watches the heartbeat PID and
    // brings the LLM/TTS/STT processes up and down). This packaged path remains
    // only as a legacy fallback for the plugin's optional autostart_stack hook.
    return RuntimeDir() / "Tools" / "FNVBridge" / "start_stack_if_needed.ps1";
}

fs::path ResolveStackLauncherPath(const std::string& configured)
{
    fs::path launcher = configured.empty() ? DefaultStackLauncherPath() : fs::path(configured);
    if (launcher.empty())
    {
        return {};
    }

    if (launcher.is_relative())
    {
        launcher = RuntimeDir() / launcher;
    }

    return launcher.lexically_normal();
}

fs::path GetPowerShellPath()
{
    wchar_t systemDir[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return fs::path("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
    }

    return fs::path(systemDir) / "WindowsPowerShell" / "v1.0" / "powershell.exe";
}

fs::path GetCmdPath()
{
    wchar_t systemDir[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return fs::path("C:/Windows/System32/cmd.exe");
    }

    return fs::path(systemDir) / "cmd.exe";
}

bool LaunchDetachedProcess(const fs::path& application, const std::wstring& commandLine, const fs::path& workingDir)
{
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};
    std::wstring mutableCommandLine = commandLine;

    BOOL ok = CreateProcessW(
        application.wstring().c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS | CREATE_NO_WINDOW,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.wstring().c_str(),
        &startupInfo,
        &processInfo);

    if (!ok)
    {
        LogLine("Bridge stack launch failed for %S (error %lu).", application.wstring().c_str(), GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool LaunchStackLauncherPath(const fs::path& launcherPath)
{
    std::error_code ec;
    if (launcherPath.empty() || !fs::exists(launcherPath, ec))
    {
        LogLine("Bridge stack launcher not found: %s", launcherPath.string().c_str());
        return false;
    }

    const std::string extension = ToLowerAscii(launcherPath.extension().string());
    const fs::path workingDir = launcherPath.parent_path().empty() ? RuntimeDir() : launcherPath.parent_path();

    if (extension == ".ps1")
    {
        const fs::path powershell = GetPowerShellPath();
        const std::wstring launcher = launcherPath.wstring();
        std::wstring commandLine = L"\"" + powershell.wstring() + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" + launcher + L"\"";
        return LaunchDetachedProcess(powershell, commandLine, workingDir);
    }

    if (extension == ".bat" || extension == ".cmd")
    {
        const fs::path cmd = GetCmdPath();
        const std::wstring launcher = launcherPath.wstring();
        std::wstring commandLine = L"\"" + cmd.wstring() + L"\" /c \"" + launcher + L"\"";
        return LaunchDetachedProcess(cmd, commandLine, workingDir);
    }

    const std::wstring executable = launcherPath.wstring();
    std::wstring commandLine = L"\"" + executable + L"\"";
    return LaunchDetachedProcess(launcherPath, commandLine, workingDir);
}

void MaybeRequestBridgeStackStartup(const char* reason, bool force)
{
    LoadDebugConfigIfNeeded(false);
    if (!g_debugConfig.autoStartStack)
    {
        return;
    }

    const DWORD now = GetTickCount();
    if (!force
        && g_state.stackBootstrapAttempted
        && g_state.stackBootstrapAttemptTick
        && (now - g_state.stackBootstrapAttemptTick) < g_debugConfig.stackBootstrapCooldownMs)
    {
        return;
    }

    const fs::path launcherPath = ResolveStackLauncherPath(g_debugConfig.stackLauncherPath);
    g_state.stackBootstrapAttempted = true;
    g_state.stackBootstrapAttemptTick = now;

    if (LaunchStackLauncherPath(launcherPath))
    {
        LogLine("Requested bridge stack startup (%s) via %s.", reason ? reason : "unknown", launcherPath.string().c_str());
    }
}

fs::path VoiceBootstrapStatusPath()
{
    return BridgeDir() / "voice_bootstrap_status.json";
}

fs::path TraceDir()
{
    return BridgeDir() / "traces";
}

fs::path RequestTracePath(std::string_view requestId)
{
    std::string safe(requestId);
    for (char& ch : safe)
    {
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
        {
            ch = '_';
        }
    }
    return TraceDir() / (safe + ".jsonl");
}

std::string NowIsoUtc()
{
    SYSTEMTIME systemTime{};
    GetSystemTime(&systemTime);
    char buffer[40]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        static_cast<unsigned>(systemTime.wYear),
        static_cast<unsigned>(systemTime.wMonth),
        static_cast<unsigned>(systemTime.wDay),
        static_cast<unsigned>(systemTime.wHour),
        static_cast<unsigned>(systemTime.wMinute),
        static_cast<unsigned>(systemTime.wSecond),
        static_cast<unsigned>(systemTime.wMilliseconds));
    return buffer;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                char hex[8]{};
                std::snprintf(hex, sizeof(hex), "\\u%04X", static_cast<unsigned>(ch));
                out << hex;
            }
            else
            {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

void EnsureTraceContext(const std::string& requestId)
{
    if (requestId.empty())
    {
        return;
    }

    if (g_state.traceRequestId != requestId)
    {
        g_state.traceRequestId = requestId;
        g_state.traceStartedTick = GetTickCount64();
        EnsureBridgeDirectories();
        std::error_code ec;
        fs::remove(RequestTracePath(requestId), ec);
    }
}

double TraceElapsedMs(const std::string& requestId)
{
    if (requestId.empty())
    {
        return 0.0;
    }

    EnsureTraceContext(requestId);
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG delta = (now >= g_state.traceStartedTick) ? (now - g_state.traceStartedTick) : 0;
    return static_cast<double>(delta);
}

void TraceRequestEvent(const std::string& requestId, const std::string& stage,
    const std::vector<std::pair<std::string, std::string>>& stringFields,
    const std::vector<std::pair<std::string, double>>& numberFields,
    const std::vector<std::pair<std::string, bool>>& boolFields)
{
    if (!g_debugConfig.requestTracingEnabled)
    {
        return;
    }

    if (requestId.empty() || stage.empty())
    {
        return;
    }

    EnsureTraceContext(requestId);
    EnsureBridgeDirectories();
    std::ofstream out(RequestTracePath(requestId), std::ios::binary | std::ios::app);
    if (!out)
    {
        return;
    }

    out << "{";
    out << "\"request_id\":" << JsonEscape(requestId);
    out << ",\"stage\":" << JsonEscape(stage);
    out << ",\"at\":" << JsonEscape(NowIsoUtc());
    out << ",\"elapsed_ms\":" << std::fixed << std::setprecision(3) << TraceElapsedMs(requestId);
    out.unsetf(std::ios::floatfield);
    out << std::setprecision(6);

    for (const auto& [name, value] : stringFields)
    {
        out << ",\"" << name << "\":" << JsonEscape(value);
    }
    for (const auto& [name, value] : numberFields)
    {
        out << ",\"" << name << "\":" << std::fixed << std::setprecision(3) << value;
        out.unsetf(std::ios::floatfield);
        out << std::setprecision(6);
    }
    for (const auto& [name, value] : boolFields)
    {
        out << ",\"" << name << "\":" << (value ? "true" : "false");
    }

    out << "}\n";
}

std::string GenerateRequestId()
{
    static UInt32 sequence = 0;
    std::ostringstream out;
    out << "req_" << static_cast<unsigned long long>(GetTickCount64()) << "_" << ++sequence;
    return out.str();
}

std::string GenerateSaveStateEventId()
{
    static UInt32 sequence = 0;
    std::ostringstream out;
    out << "saveevt_" << static_cast<unsigned long long>(GetTickCount64()) << "_" << ++sequence;
    return out.str();
}

bool DispatchSaveStateEvent(const std::string& eventType, const std::string& savePath, bool waitForAck)
{
    if (eventType.empty())
    {
        return false;
    }

    EnsureBridgeDirectories();
    const std::string eventId = GenerateSaveStateEventId();
    const fs::path eventPath = SaveStateEventPath(eventId);
    const fs::path ackPath = SaveStateAckPath(eventId);
    const std::string saveName = savePath.empty() ? "" : fs::path(savePath).filename().string();

    std::error_code ec;
    fs::remove(eventPath, ec);
    fs::remove(ackPath, ec);

    std::ofstream out(eventPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        LogLine("Failed to open save-state event path for %s.", eventType.c_str());
        return false;
    }

    out << eventId << "\n";
    out << eventType << "\n";
    out << savePath << "\n";
    out << saveName << "\n";
    out << NowIsoUtc() << "\n";
    out.close();
    if (!out)
    {
        LogLine("Failed while writing save-state event %s.", eventType.c_str());
        return false;
    }

    if (waitForAck)
    {
        g_state.saveStateSyncPending = true;
        g_state.saveStateSyncEventId = eventId;
        g_state.saveStateSyncType = eventType;
        g_state.saveStateSyncLastPollTick = 0;
        g_state.saveStateSyncHudMessageTick = 0;
        g_state.saveStateSyncStartedTick = GetTickCount();
    }

    LogLine("Queued save-state event %s id=%s for %s.", eventType.c_str(), eventId.c_str(), savePath.c_str());
    return true;
}

void PollSaveStateSyncAck()
{
    if (!g_state.saveStateSyncPending || g_state.saveStateSyncEventId.empty())
    {
        return;
    }

    const DWORD now = GetTickCount();
    if (g_state.saveStateSyncLastPollTick && (now - g_state.saveStateSyncLastPollTick) < kSaveStateAckPollMs)
    {
        return;
    }

    g_state.saveStateSyncLastPollTick = now;
    const fs::path ackPath = SaveStateAckPath(g_state.saveStateSyncEventId);
    if (!fs::exists(ackPath))
    {
        if (g_state.saveStateSyncStartedTick
            && (now - g_state.saveStateSyncStartedTick) >= kSaveStateSyncTimeoutMs)
        {
            LogLine("Save-state sync %s id=%s timed out after %lu ms; clearing pending state.",
                g_state.saveStateSyncType.c_str(),
                g_state.saveStateSyncEventId.c_str(),
                static_cast<unsigned long>(now - g_state.saveStateSyncStartedTick));
            g_state.saveStateSyncPending = false;
            g_state.saveStateSyncEventId.clear();
            g_state.saveStateSyncType.clear();
            g_state.saveStateSyncLastPollTick = 0;
            g_state.saveStateSyncHudMessageTick = 0;
            g_state.saveStateSyncStartedTick = 0;
        }
        return;
    }

    std::ifstream in(ackPath, std::ios::binary);
    if (!in)
    {
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }

    const std::string status = lines.size() > 1 ? ToLowerAscii(Trim(lines[1])) : "";
    const std::string message = lines.size() > 2 ? Trim(lines[2]) : "";
    const bool ok = status == "ok";

    std::error_code ec;
    fs::remove(ackPath, ec);

    LogLine("Save-state sync %s completed with status=%s message=%s",
        g_state.saveStateSyncType.c_str(),
        status.c_str(),
        message.c_str());

    g_state.saveStateSyncPending = false;
    g_state.saveStateSyncEventId.clear();
    g_state.saveStateSyncType.clear();
    g_state.saveStateSyncLastPollTick = 0;
    g_state.saveStateSyncHudMessageTick = 0;
    g_state.saveStateSyncStartedTick = 0;
    g_state.saveStateSyncStartedTick = 0;

    if (!ok)
    {
        ShowHudMessage(message.empty() ? "Bridge save sync failed." : message);
    }
}

void EnsureBridgeDirectories()
{
    fs::create_directories(BridgeDir() / "inbox");
    fs::create_directories(BridgeDir() / "outbox");
    fs::create_directories(OutboxChunkDir());
    fs::create_directories(BridgeDir() / "processed");
    fs::create_directories(SaveStateEventsDir());
    fs::create_directories(SaveStateAcksDir());
    fs::create_directories(NativeActionCommandDir());
    fs::create_directories(GameEventsDir());
    fs::create_directories(TraceDir());
    fs::create_directories(AudioDir());
    fs::create_directories(UserFunctionDir());
    fs::create_directories(ScriptRunnerDir());
    fs::create_directories(BridgeDir() / "control" / "companions" / "acks");
    fs::create_directories(BridgeDir() / "companions");
}

void WriteDiagnostics(const std::string& body)
{
    EnsureBridgeDirectories();
    std::ofstream out(DiagnosticsPath(), std::ios::binary | std::ios::trunc);
    out << body;
    if (!body.empty() && body.back() != '\n')
    {
        out << "\n";
    }
    if (!g_state.traceRequestId.empty())
    {
        out << "trace_request_id=" << g_state.traceRequestId << "\n";
        out << "trace_file=" << RequestTracePath(g_state.traceRequestId).string() << "\n";
    }
}

void EnsureInputCallbackScript()
{
    EnsureBridgeDirectories();

    constexpr char kCallbackScript[] =
        "string_var sTextInput\n"
        "\n"
        "begin Function {sTextInput}\n"
        "    WriteStringToFile \"Data/NVBridge/ui_submit.txt\" 0 \"%z\" sTextInput\n"
        "    sv_Destruct sTextInput\n"
        "end\n";

    const fs::path path = InputCallbackScriptPath();
    bool shouldWrite = true;

    if (fs::exists(path))
    {
        std::ifstream in(path, std::ios::binary);
        const std::string current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        shouldWrite = current != kCallbackScript;
    }

    if (!shouldWrite)
    {
        return;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kCallbackScript;
}

void WriteTextFileIfChanged(const fs::path& path, const std::string& body)
{
    bool shouldWrite = true;
    if (fs::exists(path))
    {
        std::ifstream in(path, std::ios::binary);
        const std::string current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        shouldWrite = current != body;
    }

    if (!shouldWrite)
    {
        return;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << body;
}

void EnsureDialoguePlaybackScripts()
{
    EnsureBridgeDirectories();

    const std::string femaleGoodspringsScript =
        "ref rTarget\n"
        "let rTarget := \"00000014\"\n"
        "GetSelf.SayTo rTarget vcg02_greeting 1 1\n";

    const std::string maleOldGoodspringsScript =
        "ref rTarget\n"
        "let rTarget := \"00000014\"\n"
        "GetSelf.SayTo rTarget vfreeformg_vfreeformgoodsp 1 1\n";

    WriteTextFileIfChanged(ScriptRunnerDir() / "sayto_female_goodsprings.txt", femaleGoodspringsScript);
    WriteTextFileIfChanged(ScriptRunnerDir() / "sayto_maleold_goodsprings.txt", maleOldGoodspringsScript);
}

std::string DescribeScriptResult(const NVSEArrayVarInterface::Element& result)
{
    switch (result.GetType())
    {
    case NVSEArrayVarInterface::Element::kType_Numeric:
    {
        std::ostringstream out;
        out << result.GetNumber();
        return out.str();
    }
    case NVSEArrayVarInterface::Element::kType_Form:
        return FormIdHex(result.GetFormID());
    case NVSEArrayVarInterface::Element::kType_String:
        return result.GetString() ? result.GetString() : "";
    case NVSEArrayVarInterface::Element::kType_Array:
    {
        std::ostringstream out;
        out << result.GetArrayID();
        return out.str();
    }
    default:
        return "invalid";
    }
}

std::vector<std::string> BuildRunBatchScriptPathCandidates(const fs::path& scriptPath)
{
    std::vector<std::string> candidates;
    auto appendCandidate = [&candidates](const fs::path& candidatePath) {
        if (candidatePath.empty())
        {
            return;
        }

        fs::path preferredPath = candidatePath;
        const std::string preferred = preferredPath.make_preferred().string();
        if (!preferred.empty() && std::find(candidates.begin(), candidates.end(), preferred) == candidates.end())
        {
            candidates.push_back(preferred);
        }

        std::string forward = preferred;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        if (!forward.empty() && std::find(candidates.begin(), candidates.end(), forward) == candidates.end())
        {
            candidates.push_back(forward);
        }
    };

    appendCandidate(scriptPath.filename());

    std::error_code ec;
    const fs::path absoluteScriptPath = fs::absolute(scriptPath, ec);
    if (ec)
    {
        return candidates;
    }

    const fs::path runtimeDir = RuntimeDir();
    if (!runtimeDir.empty())
    {
        const fs::path relativeToRuntime = absoluteScriptPath.lexically_relative(runtimeDir);
        const bool escapesRuntime = !relativeToRuntime.empty()
            && relativeToRuntime.begin() != relativeToRuntime.end()
            && (*relativeToRuntime.begin() == ".." || *relativeToRuntime.begin() == ".");
        if (!relativeToRuntime.empty() && !escapesRuntime)
        {
            appendCandidate(relativeToRuntime);
        }
    }

    const fs::path dataDir = DataDir();
    if (!dataDir.empty())
    {
        const fs::path relativeToData = absoluteScriptPath.lexically_relative(dataDir);
        const bool escapesData = !relativeToData.empty()
            && relativeToData.begin() != relativeToData.end()
            && (*relativeToData.begin() == ".." || *relativeToData.begin() == ".");
        if (!relativeToData.empty() && !escapesData)
        {
            appendCandidate(relativeToData);
        }
    }

    return candidates;
}

bool EnsureOpenTextInputScript()
{
    if (g_openTextInputScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot open in-game text input.");
        return false;
    }

    EnsureInputCallbackScript();

    constexpr char kLauncherScript[] = R"(
string_var sTitle
ref rCallback

Begin Function { sTitle }
    let rCallback := GetUDFFromFile "fnv_bridge/input_callback.txt"
    if rCallback
        ShowTextInputMenu rCallback 700 220 "%z" sTitle
        SetTextInputExtendedProps 0 0 1 280 2
    endif
End
)";

    g_openTextInputScript = g_scriptInterface->CompileScript(kLauncherScript);
    if (!g_openTextInputScript)
    {
        LogLine("Failed to compile TextEditMenu launcher script.");
        return false;
    }

    return true;
}

bool EnsureCloseTextInputScript()
{
    if (g_closeTextInputScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot compile TextEditMenu close helper.");
        return false;
    }

    constexpr char kCloseScript[] = R"(
Begin Function {}
    CloseActiveMenu
End
)";

    g_closeTextInputScript = g_scriptInterface->CompileScript(kCloseScript);
    if (!g_closeTextInputScript)
    {
        LogLine("Failed to compile TextEditMenu close helper script.");
        return false;
    }

    return true;
}

bool EnsureStartCombatScript()
{
    if (g_startCombatScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot trigger NPC combat.");
        return false;
    }

    constexpr char kStartCombatScript[] = R"(
ref rActor
ref rTarget

Begin Function { rActor, rTarget }
    if rActor && rTarget
        rActor.StartCombat rTarget
    endif
End
)";

    g_startCombatScript = g_scriptInterface->CompileScript(kStartCombatScript);
    if (!g_startCombatScript)
    {
        LogLine("Failed to compile StartCombat helper script.");
        return false;
    }

    return true;
}

bool EnsurePlayerTeammateScript()
{
    if (g_setPlayerTeammateScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot set NPC teammate state.");
        return false;
    }

    constexpr char kSetPlayerTeammateScript[] = R"(
ref rActor
int iTeammate
float fIssued

Begin Function { rActor, iTeammate }
    let fIssued := 0
    if rActor
        rActor.SetPlayerTeammate iTeammate
        if iTeammate
            rActor.SetRestrained 0
            rActor.EvaluatePackage
        endif
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

    g_setPlayerTeammateScript = g_scriptInterface->CompileScript(kSetPlayerTeammateScript);
    if (!g_setPlayerTeammateScript)
    {
        LogLine("Failed to compile SetPlayerTeammate helper script.");
        return false;
    }

    return true;
}

bool EnsureConversationHoldScripts()
{
    if (g_startConversationScript && g_startLookScript && g_stopLookScript && g_evaluatePackageScript && g_isCurrentPackageScript && g_addScriptPackageScript && g_removeScriptPackageScript && g_getRestrainedScript && g_setRestrainedScript && g_clearRestrainedScript && g_setAngleScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot compile conversation hold helpers.");
        return false;
    }

    if (!g_getRestrainedScript)
    {
        constexpr char kGetRestrainedScript[] = R"(
ref rActor
float fRestrained

Begin Function { rActor }
    let fRestrained := 0
    if rActor
        let fRestrained := rActor.GetRestrained
    endif
    SetFunctionValue fRestrained
End
)";

        g_getRestrainedScript = g_scriptInterface->CompileScript(kGetRestrainedScript);
        if (!g_getRestrainedScript)
        {
            LogLine("Failed to compile GetRestrained helper script.");
            return false;
        }
    }

    if (!g_startConversationScript)
    {
        constexpr char kStartConversationScript[] = R"(
ref rActor
ref rTarget
ref rTopic
ref rSpeakerLoc
ref rTargetLoc
float fIssued

Begin Function { rActor, rTarget, rTopic, rSpeakerLoc, rTargetLoc }
    let fIssued := 0
    if rActor && rTarget && rTopic
        rActor.StartConversation rTarget rTopic rSpeakerLoc rTargetLoc 1 1
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

        g_startConversationScript = g_scriptInterface->CompileScript(kStartConversationScript);
        if (!g_startConversationScript)
        {
            LogLine("Failed to compile StartConversation helper script.");
            return false;
        }
    }

    if (!g_evaluatePackageScript)
    {
        constexpr char kEvaluatePackageScript[] = R"(
ref rActor
float fIssued

Begin Function { rActor }
    let fIssued := 0
    if rActor
        rActor.EvaluatePackage
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

        g_evaluatePackageScript = g_scriptInterface->CompileScript(kEvaluatePackageScript);
        if (!g_evaluatePackageScript)
        {
            LogLine("Failed to compile EvaluatePackage helper script.");
            return false;
        }
    }

    if (!g_addScriptPackageScript)
    {
        constexpr char kAddScriptPackageScript[] = R"(
ref rActor
ref rPackage
float fIssued

Begin Function { rActor, rPackage }
    let fIssued := 0
    if rActor && rPackage
        rActor.AddScriptPackage rPackage
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

        g_addScriptPackageScript = g_scriptInterface->CompileScript(kAddScriptPackageScript);
        if (!g_addScriptPackageScript)
        {
            LogLine("Failed to compile AddScriptPackage helper script.");
            return false;
        }
    }

    if (!g_removeScriptPackageScript)
    {
        constexpr char kRemoveScriptPackageScript[] = R"(
ref rActor
float fIssued

Begin Function { rActor }
    let fIssued := 0
    if rActor
        rActor.RemoveScriptPackage
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

        g_removeScriptPackageScript = g_scriptInterface->CompileScript(kRemoveScriptPackageScript);
        if (!g_removeScriptPackageScript)
        {
            LogLine("Failed to compile RemoveScriptPackage helper script.");
            return false;
        }
    }

    if (!g_isCurrentPackageScript)
    {
        constexpr char kIsCurrentPackageScript[] = R"(
ref rActor
ref rPackage
float fMatch

Begin Function { rActor, rPackage }
    let fMatch := 0
    if rActor && rPackage
        if rActor.GetCurrentPackage == rPackage
            let fMatch := 1
        endif
    endif
    SetFunctionValue fMatch
End
)";

        g_isCurrentPackageScript = g_scriptInterface->CompileScript(kIsCurrentPackageScript);
        if (!g_isCurrentPackageScript)
        {
            LogLine("Failed to compile GetCurrentPackage helper script.");
            return false;
        }
    }

    if (!g_startLookScript)
    {
        constexpr char kStartLookScript[] = R"(
ref rActor
ref rTarget

Begin Function { rActor, rTarget }
    if rActor && rTarget
        rActor.Look rTarget 1
    endif
End
)";

        g_startLookScript = g_scriptInterface->CompileScript(kStartLookScript);
        if (!g_startLookScript)
        {
            LogLine("Failed to compile Look helper script.");
            return false;
        }
    }

    if (!g_stopLookScript)
    {
        constexpr char kStopLookScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.StopLook
    endif
End
)";

        g_stopLookScript = g_scriptInterface->CompileScript(kStopLookScript);
        if (!g_stopLookScript)
        {
            LogLine("Failed to compile StopLook helper script.");
            return false;
        }
    }

    if (!g_setRestrainedScript)
    {
        constexpr char kSetRestrainedScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.SetRestrained 1
    endif
End
)";

        g_setRestrainedScript = g_scriptInterface->CompileScript(kSetRestrainedScript);
        if (!g_setRestrainedScript)
        {
            LogLine("Failed to compile SetRestrained helper script.");
            return false;
        }
    }

    if (!g_clearRestrainedScript)
    {
        constexpr char kClearRestrainedScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.SetRestrained 0
        rActor.EvaluatePackage
    endif
End
)";

        g_clearRestrainedScript = g_scriptInterface->CompileScript(kClearRestrainedScript);
        if (!g_clearRestrainedScript)
        {
            LogLine("Failed to compile clear-Restrained helper script.");
            return false;
        }
    }

    if (!g_setAngleScript)
    {
        constexpr char kSetAngleScript[] = R"(
ref rActor
float fAngle

Begin Function { rActor, fAngle }
    if rActor
        rActor.SetAngle Z fAngle
    endif
End
)";

        g_setAngleScript = g_scriptInterface->CompileScript(kSetAngleScript);
        if (!g_setAngleScript)
        {
            LogLine("Failed to compile SetAngle helper script.");
            return false;
        }
    }

    if (!g_faceObjectScript && !g_faceObjectScriptAttempted)
    {
        g_faceObjectScriptAttempted = true;
        constexpr char kFaceObjectScript[] = R"(
ref rActor
ref rTarget

Begin Function { rActor, rTarget }
    if rActor && rTarget
        rActor.FaceObject rTarget
    endif
End
)";

        g_faceObjectScript = g_scriptInterface->CompileScript(kFaceObjectScript);
        if (!g_faceObjectScript)
        {
            LogLine("Failed to compile FaceObject helper script; body facing will rely on head look only.");
        }
    }

    if (!g_applyNoMovePackageScript && !g_applyNoMovePackageScriptAttempted)
    {
        g_applyNoMovePackageScriptAttempted = true;
        constexpr char kApplyNoMovePackageScript[] = R"(
ref rActor
float fIssued

Begin Function { rActor }
    let fIssued := 0
    if rActor
        rActor.AddScriptPackage DefaultSandboxNoMoveCurrentLocation200
        rActor.EvaluatePackage
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";

        g_applyNoMovePackageScript = g_scriptInterface->CompileScript(kApplyNoMovePackageScript);
        if (!g_applyNoMovePackageScript)
        {
            LogLine("Failed to compile no-move package helper script; conversation mode will use restrained fallback.");
        }
    }

    return true;
}

bool EnsureConsoleCommandHelper()
{
    if (g_consoleCommandScript)
    {
        return true;
    }

    if (!g_scriptInterface)
    {
        LogLine("Script interface unavailable; cannot compile Console helper.");
        return false;
    }

    constexpr char kConsoleCommandHelper[] = R"(
string_var sCommand

Begin Function { sCommand }
    if eval sCommand != ""
        Console sCommand
    endif
End
)";

    g_consoleCommandScript = g_scriptInterface->CompileScript(kConsoleCommandHelper);
    if (!g_consoleCommandScript)
    {
        LogLine("Failed to compile Console helper script.");
        return false;
    }

    return true;
}

std::string Trim(std::string value)
{
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::string ReplaceAll(std::string value, char from, char to)
{
    std::replace(value.begin(), value.end(), from, to);
    return value;
}

std::string SanitizeLine(std::string_view value)
{
    std::string text(value);
    text = ReplaceAll(text, '\r', ' ');
    text = ReplaceAll(text, '\n', ' ');
    text = ReplaceAll(text, '\t', ' ');
    return Trim(text);
}

template <typename T>
void WriteLittleEndian(std::ostream& out, T value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::vector<BYTE> BuildWaveBytesFromPcm(const std::vector<BYTE>& pcmBytes)
{
    const DWORD sampleRate = kVoiceCaptureSampleRate;
    const WORD channels = kVoiceCaptureChannels;
    const WORD bitsPerSample = kVoiceCaptureBitsPerSample;
    const WORD blockAlign = static_cast<WORD>(channels * bitsPerSample / 8);
    const DWORD byteRate = sampleRate * blockAlign;
    const DWORD dataSize = static_cast<DWORD>(pcmBytes.size());
    const DWORD riffSize = 36u + dataSize;

    std::ostringstream out(std::ios::binary);
    out.write("RIFF", 4);
    WriteLittleEndian(out, riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    WriteLittleEndian<DWORD>(out, 16u);
    WriteLittleEndian<WORD>(out, 1u);
    WriteLittleEndian(out, channels);
    WriteLittleEndian(out, static_cast<DWORD>(sampleRate));
    WriteLittleEndian(out, byteRate);
    WriteLittleEndian(out, blockAlign);
    WriteLittleEndian(out, bitsPerSample);
    out.write("data", 4);
    WriteLittleEndian(out, dataSize);
    if (!pcmBytes.empty())
    {
        out.write(reinterpret_cast<const char*>(pcmBytes.data()), static_cast<std::streamsize>(pcmBytes.size()));
    }

    const std::string bytes = out.str();
    return std::vector<BYTE>(bytes.begin(), bytes.end());
}

std::string ToUpperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> SplitCommaList(const std::string& value)
{
    std::vector<std::string> items;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        item = Trim(item);
        if (!item.empty())
        {
            items.push_back(item);
        }
    }
    return items;
}

std::map<std::string, std::string> ParseKeyValueLines(const std::vector<std::string>& lines, size_t startIndex)
{
    std::map<std::string, std::string> fields;
    for (size_t index = startIndex; index < lines.size(); ++index)
    {
        const std::string& line = lines[index];
        const size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }
        const std::string key = ToLowerAscii(Trim(line.substr(0, separator)));
        const std::string value = Trim(line.substr(separator + 1));
        if (!key.empty())
        {
            fields[key] = value;
        }
    }
    return fields;
}

std::string GetField(const std::map<std::string, std::string>& fields, const char* key)
{
    const auto it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
}

int Base64Value(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z')
    {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0' + 52;
    }
    if (ch == '+')
    {
        return 62;
    }
    if (ch == '/')
    {
        return 63;
    }
    return -1;
}

std::optional<std::string> DecodeBase64String(const std::string& input, size_t maxBytes)
{
    std::string output;
    output.reserve(input.size() * 3 / 4);

    int value = 0;
    int valueBits = -8;
    for (unsigned char ch : input)
    {
        if (std::isspace(ch))
        {
            continue;
        }
        if (ch == '=')
        {
            break;
        }

        const int decoded = Base64Value(ch);
        if (decoded < 0)
        {
            return std::nullopt;
        }

        value = (value << 6) | decoded;
        valueBits += 6;
        if (valueBits >= 0)
        {
            output.push_back(static_cast<char>((value >> valueBits) & 0xFF));
            if (output.size() > maxBytes)
            {
                return std::nullopt;
            }
            valueBits -= 8;
        }
    }

    return output;
}

std::string EncodeBase64(const unsigned char* data, size_t length)
{
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= length)
    {
        const unsigned int triple = (static_cast<unsigned int>(data[i]) << 16)
            | (static_cast<unsigned int>(data[i + 1]) << 8)
            | static_cast<unsigned int>(data[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
        i += 3;
    }

    const size_t remaining = length - i;
    if (remaining == 1)
    {
        const unsigned int triple = static_cast<unsigned int>(data[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (remaining == 2)
    {
        const unsigned int triple = (static_cast<unsigned int>(data[i]) << 16)
            | (static_cast<unsigned int>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

// --- Minimal JSON value extraction for the flat NDJSON turn events. -----------
// chasm's /api/game/v1/turn streams one self-contained JSON object per line. We do
// not need a general DOM: each event is a flat object whose values we read by key.
// These helpers scan a single object literal (which may contain nested objects /
// strings) and pull out a top-level field by name. They tolerate whitespace and
// escaped characters inside strings, and skip over nested braces/brackets when
// locating the matching top-level key. This is deliberately small + allocation-
// light; it is NOT a validating parser.

// Decode a JSON string literal body (the text between the surrounding quotes, with
// the surrounding quotes already removed) into raw bytes, resolving \" \\ \/ \n \r
// \t \b \f and \uXXXX (BMP; surrogate pairs combined to UTF-8).
std::string JsonDecodeStringBody(const std::string& body)
{
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i)
    {
        const char ch = body[i];
        if (ch != '\\')
        {
            out.push_back(ch);
            continue;
        }
        if (i + 1 >= body.size())
        {
            break;
        }
        const char esc = body[++i];
        switch (esc)
        {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u':
        {
            if (i + 4 >= body.size())
            {
                i = body.size();
                break;
            }
            auto readHex4 = [&](size_t at, unsigned int& outValue) -> bool {
                unsigned int value = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    const char hexCh = body[at + k];
                    value <<= 4;
                    if (hexCh >= '0' && hexCh <= '9') value |= static_cast<unsigned int>(hexCh - '0');
                    else if (hexCh >= 'a' && hexCh <= 'f') value |= static_cast<unsigned int>(hexCh - 'a' + 10);
                    else if (hexCh >= 'A' && hexCh <= 'F') value |= static_cast<unsigned int>(hexCh - 'A' + 10);
                    else return false;
                }
                outValue = value;
                return true;
            };
            unsigned int code = 0;
            if (!readHex4(i + 1, code))
            {
                break;
            }
            i += 4;
            if (code >= 0xD800 && code <= 0xDBFF && i + 6 < body.size() && body[i + 1] == '\\' && body[i + 2] == 'u')
            {
                unsigned int low = 0;
                if (readHex4(i + 3, low) && low >= 0xDC00 && low <= 0xDFFF)
                {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
            }
            if (code < 0x80)
            {
                out.push_back(static_cast<char>(code));
            }
            else if (code < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else if (code < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
        }
        default:
            out.push_back(esc);
            break;
        }
    }
    return out;
}

// Find the position just after the colon following "key" at the top level of the
// object in [begin,end). Returns std::string::npos if the key is not present at the
// top level. Nested objects/arrays/strings are skipped so a key inside an inner
// object is not matched.
size_t JsonFindTopLevelValue(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < json.size(); ++i)
    {
        const char ch = json[i];
        if (inString)
        {
            if (ch == '\\')
            {
                ++i; // skip escaped char
            }
            else if (ch == '"')
            {
                inString = false;
            }
            continue;
        }
        if (ch == '"')
        {
            // Only consider keys at object depth 1 (immediate members of the root object).
            if (depth == 1 && json.compare(i, needle.size(), needle) == 0)
            {
                size_t j = i + needle.size();
                while (j < json.size() && (json[j] == ' ' || json[j] == '\t')) ++j;
                if (j < json.size() && json[j] == ':')
                {
                    ++j;
                    while (j < json.size() && (json[j] == ' ' || json[j] == '\t')) ++j;
                    return j;
                }
            }
            inString = true;
            continue;
        }
        if (ch == '{' || ch == '[')
        {
            ++depth;
        }
        else if (ch == '}' || ch == ']')
        {
            --depth;
        }
    }
    return std::string::npos;
}

// Read a top-level string field. Returns false if missing or not a string.
bool JsonGetString(const std::string& json, const std::string& key, std::string& out)
{
    const size_t pos = JsonFindTopLevelValue(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"')
    {
        return false;
    }
    size_t i = pos + 1;
    const size_t bodyStart = i;
    for (; i < json.size(); ++i)
    {
        if (json[i] == '\\')
        {
            ++i;
            continue;
        }
        if (json[i] == '"')
        {
            break;
        }
    }
    if (i > json.size())
    {
        return false;
    }
    out = JsonDecodeStringBody(json.substr(bodyStart, i - bodyStart));
    return true;
}

std::string JsonGetStringOr(const std::string& json, const std::string& key, const std::string& fallback = "")
{
    std::string value;
    return JsonGetString(json, key, value) ? value : fallback;
}

// Read a top-level numeric field (returns false if missing / non-numeric).
bool JsonGetNumber(const std::string& json, const std::string& key, double& out)
{
    const size_t pos = JsonFindTopLevelValue(json, key);
    if (pos == std::string::npos || pos >= json.size())
    {
        return false;
    }
    size_t end = pos;
    while (end < json.size())
    {
        const char ch = json[end];
        if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.' || ch == 'e' || ch == 'E')
        {
            ++end;
        }
        else
        {
            break;
        }
    }
    if (end == pos)
    {
        return false;
    }
    out = std::atof(json.substr(pos, end - pos).c_str());
    return true;
}

// Read a top-level boolean field (true/false). Returns fallback if missing.
bool JsonGetBool(const std::string& json, const std::string& key, bool fallback)
{
    const size_t pos = JsonFindTopLevelValue(json, key);
    if (pos == std::string::npos || pos >= json.size())
    {
        return fallback;
    }
    if (json.compare(pos, 4, "true") == 0)
    {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0)
    {
        return false;
    }
    return fallback;
}

// Extract a nested object literal (including its braces) for a top-level key, so it
// can be re-scanned with the same helpers. Returns false if the key's value is not
// an object.
bool JsonGetObject(const std::string& json, const std::string& key, std::string& out)
{
    const size_t pos = JsonFindTopLevelValue(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '{')
    {
        return false;
    }
    int depth = 0;
    bool inString = false;
    for (size_t i = pos; i < json.size(); ++i)
    {
        const char ch = json[i];
        if (inString)
        {
            if (ch == '\\') { ++i; }
            else if (ch == '"') { inString = false; }
            continue;
        }
        if (ch == '"') { inString = true; continue; }
        if (ch == '{') { ++depth; }
        else if (ch == '}')
        {
            --depth;
            if (depth == 0)
            {
                out = json.substr(pos, i - pos + 1);
                return true;
            }
        }
    }
    return false;
}

std::string HashString64Hex(const std::string& value)
{
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char ch : value)
    {
        hash ^= static_cast<unsigned long long>(ch);
        hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

bool ParseConfigBool(std::string value, bool fallback)
{
    value = ToUpperAscii(Trim(std::move(value)));
    if (value == "1" || value == "TRUE" || value == "YES" || value == "ON")
    {
        return true;
    }
    if (value == "0" || value == "FALSE" || value == "NO" || value == "OFF")
    {
        return false;
    }
    return fallback;
}

bool IsIntegerToken(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    size_t index = 0;
    if (value[index] == '-' || value[index] == '+')
    {
        ++index;
    }

    if (index >= value.size())
    {
        return false;
    }

    for (; index < value.size(); ++index)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[index])))
        {
            return false;
        }
    }

    return true;
}

void ApplyResponseMetadata(ResponsePayload& payload, const std::string& rawKey, const std::string& value)
{
    const std::string key = ToLowerAscii(Trim(rawKey));
    if (key == "action_npc_key")
    {
        payload.actionNpcKey = Trim(value);
        return;
    }
    if (key == "action_npc_name")
    {
        payload.actionNpcName = Trim(value);
        return;
    }
    if (key == "admin_voice" || key == "non_positional_audio")
    {
        payload.nonPositionalAudio = ParseConfigBool(value, payload.nonPositionalAudio);
    }
}

bool IsNonPositionalChunkMetadata(const std::string& rawKey, const std::string& value, bool fallback)
{
    const std::string key = ToLowerAscii(Trim(rawKey));
    if (key == "admin_voice" || key == "non_positional_audio")
    {
        return ParseConfigBool(value, fallback);
    }
    return fallback;
}

void WriteDefaultDebugConfigIfMissing()
{
    std::error_code ec;
    if (fs::exists(DebugConfigPath(), ec))
    {
        return;
    }

    EnsureBridgeDirectories();
    std::ofstream out(DebugConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return;
    }

    out
        << "# FNV bridge native debug config\r\n"
        << "runtime_heartbeat=1\r\n"
        << "speech_animation=1\r\n"
        << "speech_write_phoneme_values=1\r\n"
        << "speech_write_face_flags=1\r\n"
        << "speech_clear_binding_on_stop=1\r\n"
        << "subtitles=1\r\n"
        << "listener_updates=1\r\n"
        << "directsound_3d=1\r\n"
        << "directsound_software_buffer=1\r\n"
        << "drain_queued_chunks_after_final=1\r\n"
        << "single_buffer_streaming=1\r\n"
        << "request_tracing=0\r\n"
        << "speech_weight_scale=1.00\r\n"
        << "speech_animation_update_interval_ms=50\r\n"
        << "speech_binding_validation_interval_ms=500\r\n"
        << "conversation_mode=1\r\n"
        << "conversation_mode_release_distance_m=10.00\r\n"
        << "conversation_face_player_interval_ms=1000\r\n"
        << "conversation_look_refresh_interval_ms=1500\r\n"
        << "runtime_heartbeat_interval_ms=100\r\n"
        << "streaming_chunk_overlap_ms=40\r\n"
        << "caption_ms_per_char=75\r\n"
        << "autostart_stack=1\r\n"
        << "stack_bootstrap_cooldown_ms=15000\r\n"
        << "stack_launcher_path=" << DefaultStackLauncherPath().string() << "\r\n"
        << "# bridge_root_path: optional override for the chasm <-> plugin rendezvous dir.\r\n"
        << "# Default (when unset) is %LOCALAPPDATA%\\chasm\\bridge, shared with chasm\r\n"
        << "# automatically and outside Mod Organizer 2's virtual filesystem. You normally\r\n"
        << "# do NOT need to set this. To override, set this path or the CHASM_BRIDGE_ROOT\r\n"
        << "# environment variable (CHASM_BRIDGE_ROOT, if set, wins and is the full path).\r\n"
        << "# bridge_root_path=\r\n"
        << "# Dialogue-turn transport: file (default, NVBridge files) or http (chasm /api/game/v1/turn).\r\n"
        << "transport=file\r\n"
        << "http_host=127.0.0.1\r\n"
        << "http_port=7341\r\n"
        << "http_turn_path=/api/game/v1/turn\r\n"
        << "# Player persona capture (docs/persona.md): EVERY game save (manual, quicksave,\r\n"
        << "# autosave) snaps the player's stats + appearance data (sex, race, hair, eyes,\r\n"
        << "# facial hair, outfit - no screenshot, nothing rendered) and POSTs it to chasm\r\n"
        << "# (http_host:http_port, used even when transport=file).\r\n"
        << "persona=1\r\n"
        << "persona_http_path=/api/game/v1/persona\r\n";
}

void LoadDebugConfigIfNeeded(bool force)
{
    const DWORD now = GetTickCount();
    if (!force && g_state.lastDebugConfigPollTick && (now - g_state.lastDebugConfigPollTick) < kDebugConfigPollMs)
    {
        return;
    }
    g_state.lastDebugConfigPollTick = now;

    WriteDefaultDebugConfigIfMissing();

    std::error_code ec;
    const bool exists = fs::exists(DebugConfigPath(), ec);
    if (!exists)
    {
        return;
    }

    const auto writeTime = fs::last_write_time(DebugConfigPath(), ec);
    if (!force && g_debugConfigLoaded && !ec && writeTime == g_debugConfigWriteTime)
    {
        return;
    }

    DebugConfig config{};
    std::ifstream in(DebugConfigPath(), std::ios::binary);
    if (!in)
    {
        return;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }

        const std::string key = ToLowerAscii(Trim(line.substr(0, equals)));
        const std::string value = Trim(line.substr(equals + 1));
        if (key == "runtime_heartbeat")
        {
            config.runtimeHeartbeatEnabled = ParseConfigBool(value, config.runtimeHeartbeatEnabled);
        }
        else if (key == "speech_animation")
        {
            config.speechAnimationEnabled = ParseConfigBool(value, config.speechAnimationEnabled);
        }
        else if (key == "speech_write_phoneme_values")
        {
            config.speechWritePhonemeValues = ParseConfigBool(value, config.speechWritePhonemeValues);
        }
        else if (key == "speech_write_face_flags")
        {
            config.speechWriteFaceFlags = ParseConfigBool(value, config.speechWriteFaceFlags);
        }
        else if (key == "speech_clear_binding_on_stop")
        {
            config.speechClearBindingOnStop = ParseConfigBool(value, config.speechClearBindingOnStop);
        }
        else if (key == "subtitles")
        {
            config.subtitlesEnabled = ParseConfigBool(value, config.subtitlesEnabled);
        }
        else if (key == "listener_updates")
        {
            config.listenerUpdatesEnabled = ParseConfigBool(value, config.listenerUpdatesEnabled);
        }
        else if (key == "directsound_3d")
        {
            config.directSound3dEnabled = ParseConfigBool(value, config.directSound3dEnabled);
        }
        else if (key == "directsound_software_buffer")
        {
            config.directSoundSoftwareBufferEnabled = ParseConfigBool(value, config.directSoundSoftwareBufferEnabled);
        }
        else if (key == "drain_queued_chunks_after_final")
        {
            config.drainQueuedChunksAfterFinal = ParseConfigBool(value, config.drainQueuedChunksAfterFinal);
        }
        else if (key == "single_buffer_streaming")
        {
            config.singleBufferStreaming = ParseConfigBool(value, config.singleBufferStreaming);
        }
        else if (key == "request_tracing")
        {
            config.requestTracingEnabled = ParseConfigBool(value, config.requestTracingEnabled);
        }
        else if (key == "conversation_mode")
        {
            config.conversationModeEnabled = ParseConfigBool(value, config.conversationModeEnabled);
        }
        else if (key == "speech_weight_scale")
        {
            const float scale = static_cast<float>(std::atof(value.c_str()));
            if (scale >= 0.10f && scale <= 3.00f)
            {
                config.speechWeightScale = scale;
            }
        }
        else if (key == "conversation_mode_release_distance_m")
        {
            const float distanceMeters = static_cast<float>(std::atof(value.c_str()));
            if (distanceMeters >= 1.0f && distanceMeters <= 100.0f)
            {
                config.conversationModeReleaseDistanceMeters = distanceMeters;
            }
        }
        else if (key == "speech_animation_update_interval_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 16 && interval <= 250)
            {
                config.speechAnimationUpdateIntervalMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "speech_binding_validation_interval_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 100 && interval <= 2000)
            {
                config.speechBindingValidationIntervalMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "conversation_face_player_interval_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 50 && interval <= 5000)
            {
                config.conversationModeFaceRefreshIntervalMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "conversation_look_refresh_interval_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 250 && interval <= 30000)
            {
                config.conversationLookRefreshIntervalMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "runtime_heartbeat_interval_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 10 && interval <= 5000)
            {
                config.runtimeHeartbeatIntervalMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "streaming_chunk_overlap_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 0 && interval <= static_cast<int>(kMaxStreamingChunkOverlapMs))
            {
                config.streamingChunkOverlapMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "caption_ms_per_char")
        {
            const int v = std::atoi(value.c_str());
            if (v >= 10 && v <= 500)
            {
                config.captionMsPerChar = static_cast<DWORD>(v);
            }
        }
        else if (key == "autostart_stack")
        {
            config.autoStartStack = ParseConfigBool(value, config.autoStartStack);
        }
        else if (key == "stack_bootstrap_cooldown_ms")
        {
            const int interval = std::atoi(value.c_str());
            if (interval >= 1000 && interval <= 600000)
            {
                config.stackBootstrapCooldownMs = static_cast<DWORD>(interval);
            }
        }
        else if (key == "stack_launcher_path")
        {
            config.stackLauncherPath = value;
        }
        else if (key == "bridge_root_path")
        {
            config.bridgeRootPath = value;
        }
        else if (key == "transport")
        {
            const std::string mode = ToLowerAscii(value);
            if (mode == "http")
            {
                config.transport = BridgeTransport::Http;
            }
            else if (mode == "file")
            {
                config.transport = BridgeTransport::File;
            }
            // Any other value leaves the default (File) in place.
        }
        else if (key == "http_host")
        {
            if (!value.empty())
            {
                config.httpHost = value;
            }
        }
        else if (key == "http_port")
        {
            const int port = std::atoi(value.c_str());
            if (port > 0 && port <= 65535)
            {
                config.httpPort = port;
            }
        }
        else if (key == "http_turn_path")
        {
            if (!value.empty() && value[0] == '/')
            {
                config.httpTurnPath = value;
            }
        }
        else if (key == "persona")
        {
            config.personaEnabled = ParseConfigBool(value, config.personaEnabled);
        }
        // The screenshot feature is fully retired: persona_screenshot,
        // persona_capture_on_autosave, persona_debounce_ms and every
        // persona_camera_* / persona_portrait_* / persona_face_* /
        // persona_max_image_width / persona_jpeg_quality key is ignored if
        // present in an old cfg (captures are data-only, on every save).
        else if (key == "persona_http_path")
        {
            if (!value.empty() && value[0] == '/')
            {
                config.personaHttpPath = value;
            }
        }
    }

    if (config.stackLauncherPath.empty())
    {
        config.stackLauncherPath = DefaultStackLauncherPath().string();
    }

    g_debugConfig = config;
    g_debugConfigLoaded = true;
    if (!ec)
    {
        g_debugConfigWriteTime = writeTime;
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "debug_config_loaded",
            {},
            {
                { "speech_weight_scale", static_cast<double>(g_debugConfig.speechWeightScale) },
                { "conversation_mode_release_distance_m", static_cast<double>(g_debugConfig.conversationModeReleaseDistanceMeters) },
                { "speech_animation_update_interval_ms", static_cast<double>(g_debugConfig.speechAnimationUpdateIntervalMs) },
                { "speech_binding_validation_interval_ms", static_cast<double>(g_debugConfig.speechBindingValidationIntervalMs) },
                { "conversation_face_player_interval_ms", static_cast<double>(g_debugConfig.conversationModeFaceRefreshIntervalMs) },
                { "conversation_look_refresh_interval_ms", static_cast<double>(g_debugConfig.conversationLookRefreshIntervalMs) },
                { "runtime_heartbeat_interval_ms", static_cast<double>(g_debugConfig.runtimeHeartbeatIntervalMs) },
                { "streaming_chunk_overlap_ms", static_cast<double>(g_debugConfig.streamingChunkOverlapMs) },
                { "stack_bootstrap_cooldown_ms", static_cast<double>(g_debugConfig.stackBootstrapCooldownMs) },
            },
            {
                { "runtime_heartbeat", g_debugConfig.runtimeHeartbeatEnabled },
                { "speech_animation", g_debugConfig.speechAnimationEnabled },
                { "speech_write_phoneme_values", g_debugConfig.speechWritePhonemeValues },
                { "speech_write_face_flags", g_debugConfig.speechWriteFaceFlags },
                { "speech_clear_binding_on_stop", g_debugConfig.speechClearBindingOnStop },
                { "subtitles", g_debugConfig.subtitlesEnabled },
                { "listener_updates", g_debugConfig.listenerUpdatesEnabled },
                { "directsound_3d", g_debugConfig.directSound3dEnabled },
                { "directsound_software_buffer", g_debugConfig.directSoundSoftwareBufferEnabled },
                { "drain_queued_chunks_after_final", g_debugConfig.drainQueuedChunksAfterFinal },
                { "single_buffer_streaming", g_debugConfig.singleBufferStreaming },
                { "request_tracing", g_debugConfig.requestTracingEnabled },
                { "conversation_mode", g_debugConfig.conversationModeEnabled },
                { "autostart_stack", g_debugConfig.autoStartStack },
                { "has_bridge_root_override", !g_debugConfig.bridgeRootPath.empty() },
            });
    }
}

// Reload <bridge>\control\hotkeys.cfg when its mtime changes (1s poll, same
// pattern as LoadDebugConfigIfNeeded), so rebinding in the chasm UI takes
// effect in a running game without a restart.
//
// WIRE FORMAT (written by chasm — chasm-core/src/hotkeys.rs is the writer and
// must stay in sync): `key=value` lines, `#` comments, CRLF. Values are
// DECIMAL Win32 virtual-key codes. Keys:
//   chat_vk        enter text (NPCs)      default VK_RETURN (13)
//   voice_vk       push to talk (NPCs)    default VK_MENU   (18)
//   admin_chat_vk  enter text (Todd)      default 'O'       (79)
//   admin_voice_vk push to talk (Todd)    default 'H'       (72)
// Out-of-range (not 1..254) or VK_ESCAPE values are ignored (Escape is the
// cancel key); a missing file resets to the built-in defaults.
void LoadHotkeysConfigIfNeeded(bool force)
{
    const DWORD now = GetTickCount();
    if (!force && g_hotkeysLastPollTick && (now - g_hotkeysLastPollTick) < kDebugConfigPollMs)
    {
        return;
    }
    g_hotkeysLastPollTick = now;

    std::error_code ec;
    if (!fs::exists(HotkeysConfigPath(), ec))
    {
        if (g_hotkeysLoaded)
        {
            g_hotkeys = HotkeyBindings{};
            g_hotkeysLoaded = false;
            LogLine("Hotkeys config removed; reverting to default bindings.");
        }
        return;
    }

    const auto writeTime = fs::last_write_time(HotkeysConfigPath(), ec);
    if (!force && g_hotkeysLoaded && !ec && writeTime == g_hotkeysWriteTime)
    {
        return;
    }

    std::ifstream in(HotkeysConfigPath(), std::ios::binary);
    if (!in)
    {
        return;
    }

    HotkeyBindings bindings{};
    auto applyKey = [](SHORT& slot, const std::string& value)
    {
        const int vk = std::atoi(value.c_str());
        if (vk >= 1 && vk <= 254 && vk != VK_ESCAPE)
        {
            slot = static_cast<SHORT>(vk);
        }
    };

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        const std::string key = ToLowerAscii(Trim(line.substr(0, equals)));
        const std::string value = Trim(line.substr(equals + 1));
        if (key == "chat_vk")
        {
            applyKey(bindings.chatVk, value);
        }
        else if (key == "voice_vk")
        {
            applyKey(bindings.voiceVk, value);
        }
        else if (key == "admin_chat_vk")
        {
            applyKey(bindings.adminChatVk, value);
        }
        else if (key == "admin_voice_vk")
        {
            applyKey(bindings.adminVoiceVk, value);
        }
        // "version" and unknown keys: ignored (forward compatibility).
    }

    const bool changed = !g_hotkeysLoaded
        || bindings.chatVk != g_hotkeys.chatVk
        || bindings.voiceVk != g_hotkeys.voiceVk
        || bindings.adminChatVk != g_hotkeys.adminChatVk
        || bindings.adminVoiceVk != g_hotkeys.adminVoiceVk;
    g_hotkeys = bindings;
    g_hotkeysLoaded = true;
    if (!ec)
    {
        g_hotkeysWriteTime = writeTime;
    }
    if (changed)
    {
        LogLine("Hotkeys loaded: chat_vk=%d voice_vk=%d admin_chat_vk=%d admin_voice_vk=%d.",
            static_cast<int>(g_hotkeys.chatVk), static_cast<int>(g_hotkeys.voiceVk),
            static_cast<int>(g_hotkeys.adminChatVk), static_cast<int>(g_hotkeys.adminVoiceVk));
        // Re-prime edge detection so a key held across the swap doesn't fire.
        PrimeHotkeyEdgeStateFromKeyboard();
    }
}

void WriteRuntimeHeartbeatIfNeeded(bool force)
{
    LoadDebugConfigIfNeeded(false);
    if (!g_debugConfig.runtimeHeartbeatEnabled)
    {
        return;
    }

    const DWORD now = GetTickCount();
    if (!force && g_state.lastRuntimeHeartbeatTick
        && (now - g_state.lastRuntimeHeartbeatTick) < g_debugConfig.runtimeHeartbeatIntervalMs)
    {
        return;
    }
    g_state.lastRuntimeHeartbeatTick = now;
    ++g_state.runtimeHeartbeatFrame;

    EnsureBridgeDirectories();
    std::ofstream out(RuntimeHeartbeatPath(), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return;
    }

    const DWORD speechRemainingMs = g_state.activeSpeechUntilTick && now < g_state.activeSpeechUntilTick
        ? (g_state.activeSpeechUntilTick - now)
        : 0;

    const PlayerCharacter* player = GetPlayer();
    // Movement engine: refresh the map-marker manifest chasm uses to plan journeys
    // (self-throttled to every few seconds; rides the heartbeat's cadence).
    WriteLocationsManifestIfNeeded(GetPlayer());
    SpeakerSnapshot lastNpcSnapshot = g_state.lastNpcSpeaker;
    bool lastNpcResolved = false;
    if (TESObjectREFR* resolvedLastNpc = ResolveSpeakerRef(g_state.lastNpcSpeaker))
    {
        lastNpcSnapshot = CaptureSpeakerSnapshot(resolvedLastNpc);
        lastNpcResolved = true;
    }

    double playerToLastNpcDistanceMeters = -1.0;
    if (player && lastNpcSnapshot.valid)
    {
        const double dx = static_cast<double>(player->posX) - static_cast<double>(lastNpcSnapshot.posX);
        const double dy = static_cast<double>(player->posY) - static_cast<double>(lastNpcSnapshot.posY);
        const double dz = static_cast<double>(player->posZ) - static_cast<double>(lastNpcSnapshot.posZ);
        playerToLastNpcDistanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz) / kGameUnitsPerMeter;
    }

    std::ostringstream payload;
    payload << "{\n";
    payload << "  \"pid\": " << static_cast<unsigned long>(GetCurrentProcessId()) << ",\n";
    payload << "  \"updated_at\": " << JsonEscape(NowIsoUtc()) << ",\n";
    payload << "  \"frame\": " << g_state.runtimeHeartbeatFrame << ",\n";
    payload << "  \"trace_request_id\": " << JsonEscape(g_state.traceRequestId) << ",\n";
    payload << "  \"active_request_id\": " << JsonEscape(g_state.activeRequestId) << ",\n";
    payload << "  \"awaiting_input\": " << (g_state.awaitingInput ? "true" : "false") << ",\n";
    payload << "  \"bridge_text_input_owned\": " << (g_state.bridgeTextInputOwned ? "true" : "false") << ",\n";
    payload << "  \"awaiting_reply\": " << (g_state.awaitingReply ? "true" : "false") << ",\n";
    payload << "  \"game_window_has_focus\": " << (GameWindowHasFocus() ? "true" : "false") << ",\n";
    payload << "  \"pending_audio_chunks\": " << g_state.pendingAudioChunks.size() << ",\n";
    payload << "  \"active_sounds\": " << g_state.activeSounds.size() << ",\n";
    payload << "  \"streamed_audio_seen_for_reply\": " << (g_state.streamedAudioSeenForReply ? "true" : "false") << ",\n";
    payload << "  \"last_audio_chunk_index\": " << g_state.lastAudioChunkIndex << ",\n";
    payload << "  \"speech_remaining_ms\": " << speechRemainingMs << ",\n";
    payload << "  \"player\": {\n";
    payload << "    \"present\": " << (player ? "true" : "false") << ",\n";
    payload << "    \"ref_id\": " << (player ? player->refID : 0) << ",\n";
    payload << "    \"pos_x\": " << (player ? player->posX : 0.0f) << ",\n";
    payload << "    \"pos_y\": " << (player ? player->posY : 0.0f) << ",\n";
    payload << "    \"pos_z\": " << (player ? player->posZ : 0.0f) << "\n";
    payload << "  },\n";
    payload << "  \"last_npc\": {\n";
    payload << "    \"npc_key\": " << JsonEscape(g_state.lastNpcKey) << ",\n";
    payload << "    \"npc_name\": " << JsonEscape(g_state.lastNpcName) << ",\n";
    payload << "    \"ref_id\": " << lastNpcSnapshot.refId << ",\n";
    payload << "    \"snapshot_valid\": " << (lastNpcSnapshot.valid ? "true" : "false") << ",\n";
    payload << "    \"resolved\": " << (lastNpcResolved ? "true" : "false") << ",\n";
    payload << "    \"pos_x\": " << lastNpcSnapshot.posX << ",\n";
    payload << "    \"pos_y\": " << lastNpcSnapshot.posY << ",\n";
    payload << "    \"pos_z\": " << lastNpcSnapshot.posZ << ",\n";
    payload << "    \"distance_to_player_m\": " << playerToLastNpcDistanceMeters << "\n";
    payload << "  },\n";
    // Movement engine Phase 2: live status of every NPC chasm is currently walking
    // somewhere — is it loaded (a real walk package is moving it) and where is it
    // right now. chasm re-anchors its off-screen sim on this actual position and
    // detects arrival from it. Entries older than 60s are pruned (journey ended).
    {
        const ULONGLONG travelersNowMs = GetTickCount64();
        for (auto it = g_travelers.begin(); it != g_travelers.end();)
        {
            if (travelersNowMs - it->second.updatedMs > 60000)
            {
                it = g_travelers.erase(it);
            }
            else
            {
                ++it;
            }
        }
        payload << "  \"travelers\": {\n";
        size_t travelerIdx = 0;
        for (const auto& kv : g_travelers)
        {
            payload << "    " << JsonEscape(kv.first) << ": {"
                << "\"loaded\": " << (kv.second.loaded ? "true" : "false")
                << ", \"arrived\": " << (kv.second.arrived ? "true" : "false")
                << ", \"interior\": " << (kv.second.interior ? "true" : "false")
                << ", \"journey_id\": " << JsonEscape(kv.second.journeyId)
                << ", \"building\": " << JsonEscape(kv.second.building)
                << ", \"pos_x\": " << std::fixed << std::setprecision(2) << kv.second.x
                << ", \"pos_y\": " << kv.second.y
                << ", \"pos_z\": " << kv.second.z << "}"
                << (++travelerIdx < g_travelers.size() ? "," : "") << "\n";
        }
        payload << "  },\n";
    }
    payload << "  \"speech_animation\": {\n";
    payload << "    \"active\": " << (g_state.speechAnimation.active ? "true" : "false") << ",\n";
    payload << "    \"request_id\": " << JsonEscape(g_state.speechAnimation.requestId) << ",\n";
    payload << "    \"speaker_ref_id\": " << g_state.speechAnimation.speaker.refId << ",\n";
    payload << "    \"duration_ms\": " << g_state.speechAnimation.durationMs << ",\n";
    payload << "    \"binding_resolved\": " << (g_state.speechAnimation.binding.phonemeKeyframe ? "true" : "false") << "\n";
    payload << "  },\n";
    payload << "  \"conversation_hold\": {\n";
    payload << "    \"active\": " << (g_state.conversationHold.active ? "true" : "false") << ",\n";
    payload << "    \"speaker_ref_id\": " << g_state.conversationHold.speaker.refId << ",\n";
    payload << "    \"release_tick\": " << g_state.conversationHold.releaseTick << ",\n";
    payload << "    \"script_package_applied\": " << (g_state.conversationHold.scriptPackageApplied ? "true" : "false") << "\n";
    payload << "  },\n";
    payload << "  \"voice_capture\": {\n";
    payload << "    \"active\": " << (g_state.voiceCapture.active ? "true" : "false") << ",\n";
    payload << "    \"transcribing\": " << (g_state.voiceCapture.transcribing ? "true" : "false") << ",\n";
    payload << "    \"admin_mode\": " << (g_state.voiceCapture.adminMode ? "true" : "false") << ",\n";
    payload << "    \"key_down_last_frame\": " << (g_state.voiceCapture.keyDownLastFrame ? "true" : "false") << ",\n";
    payload << "    \"admin_key_down_last_frame\": " << (g_state.voiceCapture.adminKeyDownLastFrame ? "true" : "false") << ",\n";
    payload << "    \"started_tick\": " << g_state.voiceCapture.startedTick << ",\n";
    payload << "    \"subtitle_refresh_tick\": " << g_state.voiceCapture.subtitleRefreshTick << ",\n";
    payload << "    \"captured_pcm_bytes\": " << g_state.voiceCapture.capturedPcm.size() << ",\n";
    payload << "    \"npc_key\": " << JsonEscape(g_state.voiceCapture.npcKey) << ",\n";
    payload << "    \"npc_name\": " << JsonEscape(g_state.voiceCapture.npcName) << ",\n";
    payload << "    \"speaker_ref_id\": " << g_state.voiceCapture.speaker.refId << "\n";
    payload << "  },\n";
    payload << "  \"direct_sound\": {\n";
    payload << "    \"device\": " << (g_state.directSound ? "true" : "false") << ",\n";
    payload << "    \"primary_buffer\": " << (g_state.primaryBuffer ? "true" : "false") << ",\n";
    payload << "    \"listener\": " << (g_state.listener3d ? "true" : "false") << "\n";
    payload << "  },\n";
    payload << "  \"debug_config\": {\n";
    payload << "    \"runtime_heartbeat\": " << (g_debugConfig.runtimeHeartbeatEnabled ? "true" : "false") << ",\n";
    payload << "    \"speech_animation\": " << (g_debugConfig.speechAnimationEnabled ? "true" : "false") << ",\n";
    payload << "    \"speech_write_phoneme_values\": " << (g_debugConfig.speechWritePhonemeValues ? "true" : "false") << ",\n";
    payload << "    \"speech_write_face_flags\": " << (g_debugConfig.speechWriteFaceFlags ? "true" : "false") << ",\n";
    payload << "    \"speech_clear_binding_on_stop\": " << (g_debugConfig.speechClearBindingOnStop ? "true" : "false") << ",\n";
    payload << "    \"subtitles\": " << (g_debugConfig.subtitlesEnabled ? "true" : "false") << ",\n";
    payload << "    \"listener_updates\": " << (g_debugConfig.listenerUpdatesEnabled ? "true" : "false") << ",\n";
    payload << "    \"directsound_3d\": " << (g_debugConfig.directSound3dEnabled ? "true" : "false") << ",\n";
    payload << "    \"directsound_software_buffer\": " << (g_debugConfig.directSoundSoftwareBufferEnabled ? "true" : "false") << ",\n";
    payload << "    \"drain_queued_chunks_after_final\": " << (g_debugConfig.drainQueuedChunksAfterFinal ? "true" : "false") << ",\n";
    payload << "    \"speech_weight_scale\": " << std::fixed << std::setprecision(2) << g_debugConfig.speechWeightScale << ",\n";
    payload << "    \"runtime_heartbeat_interval_ms\": " << g_debugConfig.runtimeHeartbeatIntervalMs << ",\n";
    payload << "    \"streaming_chunk_overlap_ms\": " << g_debugConfig.streamingChunkOverlapMs << ",\n";
    payload << "    \"caption_ms_per_char\": " << g_debugConfig.captionMsPerChar << ",\n";
    payload << "    \"bridge_root_path\": " << JsonEscape(g_debugConfig.bridgeRootPath) << "\n";
    payload << "  },\n";
    // Scheduler in-game clock: chasm's scheduler tick reads this block every few
    // seconds to advance its notion of the current in-game day+hour and fire
    // time-triggered tasks even while the player is idle (not in a dialogue turn).
    // clock_valid is false at the main menu / before a save loads.
    {
        float gdp = 0.0f;
        float ghr = 0.0f;
        const bool clockValid = player && ReadGlobalFloat(0x38, ghr) && ReadGameDaysPassed(gdp);
        payload << "  \"game\": {\n";
        payload << "    \"loaded\": " << ((player && g_state.loadedIntoGame) ? "true" : "false") << ",\n";
        payload << "    \"clock_valid\": " << (clockValid ? "true" : "false") << ",\n";
        payload << "    \"days_passed\": " << JsonEscape(clockValid ? FormatClockFloat(gdp) : std::string("0")) << ",\n";
        payload << "    \"hour\": " << JsonEscape(clockValid ? FormatClockFloat(ghr) : std::string("0")) << "\n";
        payload << "  },\n";
    }
    payload << "  \"last_playback_diagnostics\": " << JsonEscape(g_state.lastPlaybackDiagnostics) << "\n";
    payload << "}\n";

    const std::string snapshot = payload.str();
    out << snapshot;

    std::error_code ec;
    if (fs::exists(RuntimeHeartbeatHistoryPath(), ec) && fs::file_size(RuntimeHeartbeatHistoryPath(), ec) > kRuntimeHeartbeatHistoryMaxBytes)
    {
        fs::remove(RuntimeHeartbeatHistoryPath(), ec);
    }

    std::ofstream history(RuntimeHeartbeatHistoryPath(), std::ios::binary | std::ios::app);
    if (history)
    {
        std::string compact = snapshot;
        compact.erase(std::remove(compact.begin(), compact.end(), '\r'), compact.end());
        std::replace(compact.begin(), compact.end(), '\n', ' ');
        history << compact << "\n";
    }
}

fs::path ReplaceExtension(const fs::path& path, std::string_view extension)
{
    fs::path result = path;
    result.replace_extension(extension);
    return result;
}

std::string GetFormNameSafe(TESForm* form);
std::string GetStringValueSafe(String& value);

std::string FormIdHex(UInt32 formId)
{
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << formId;
    return out.str();
}

std::string ModLocalFormCacheKey(const char* modName, UInt32 localFormId)
{
    std::ostringstream out;
    out << (modName ? modName : "")
        << ':'
        << FormIdHex(localFormId);
    return out.str();
}

TESForm* ResolveModLocalForm(const char* modName, UInt32 localFormId)
{
    if (!modName || !*modName)
    {
        return nullptr;
    }

    const DWORD now = GetTickCount();
    ModLocalFormCacheEntry& cache = g_modLocalFormCache[ModLocalFormCacheKey(modName, localFormId)];
    if (cache.form)
    {
        return cache.form;
    }
    if (cache.nextRetryTick && now < cache.nextRetryTick)
    {
        return nullptr;
    }

    DataHandler* dataHandler = GetDataHandler();
    if (!dataHandler)
    {
        if (!cache.formMissingLogged)
        {
            LogLine("DataHandler unavailable while resolving %s:%s.", modName, FormIdHex(localFormId).c_str());
            cache.formMissingLogged = true;
        }
        cache.nextRetryTick = now + kModLocalFormRetryMs;
        return nullptr;
    }

    UInt8 modIndex = 0xFF;
    for (UInt32 i = 0; i < dataHandler->modList.loadedModCount; ++i)
    {
        ModInfo* modInfo = dataHandler->modList.loadedMods[i];
        if (modInfo && _stricmp(modInfo->name, modName) == 0)
        {
            modIndex = modInfo->modIndex;
            break;
        }
    }

    if (modIndex == 0xFF)
    {
        if (!cache.modMissingLogged)
        {
            LogLine("Mod %s is not loaded; cannot resolve local form %s.", modName, FormIdHex(localFormId).c_str());
            cache.modMissingLogged = true;
        }
        cache.nextRetryTick = now + kModLocalFormRetryMs;
        return nullptr;
    }

    const UInt32 runtimeFormId = (static_cast<UInt32>(modIndex) << 24) | (localFormId & 0x00FFFFFF);
    TESForm* form = LookupFormByIdRuntime(runtimeFormId);
    if (!form)
    {
        if (!cache.formMissingLogged)
        {
            LogLine("Failed to resolve runtime form %08X for %s:%s.", runtimeFormId, modName, FormIdHex(localFormId).c_str());
            cache.formMissingLogged = true;
        }
        cache.modIndex = modIndex;
        cache.runtimeFormId = runtimeFormId;
        cache.nextRetryTick = now + kModLocalFormRetryMs;
        return nullptr;
    }

    cache.form = form;
    cache.modIndex = modIndex;
    cache.runtimeFormId = runtimeFormId;
    cache.nextRetryTick = 0;
    cache.modMissingLogged = false;
    cache.formMissingLogged = false;
    return form;
}

std::string Slugify(std::string_view value)
{
    std::string result;
    bool lastUnderscore = false;
    for (unsigned char ch : std::string(value))
    {
        if (std::isalnum(ch))
        {
            result.push_back(static_cast<char>(std::tolower(ch)));
            lastUnderscore = false;
            continue;
        }

        if (!lastUnderscore)
        {
            result.push_back('_');
            lastUnderscore = true;
        }
    }

    while (!result.empty() && result.front() == '_')
    {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_')
    {
        result.pop_back();
    }
    return result;
}

bool StartsWithInsensitive(std::string_view value, std::string_view prefix)
{
    if (value.size() < prefix.size())
    {
        return false;
    }

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
        {
            return false;
        }
    }

    return true;
}

std::string RemoveDuplicateSuffix(std::string value)
{
    const std::string marker = "DUPLICATE";
    const size_t pos = value.find(marker);
    if (pos != std::string::npos)
    {
        value.erase(pos);
    }
    while (!value.empty() && std::isdigit(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

std::string HumanizeIdentifier(std::string value)
{
    value = RemoveDuplicateSuffix(value);
    value = ReplaceAll(value, '_', ' ');

    std::string out;
    out.reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (i > 0)
        {
            const unsigned char prev = static_cast<unsigned char>(value[i - 1]);
            if (std::isupper(ch) && (std::islower(prev) || std::isdigit(prev)))
            {
                out.push_back(' ');
            }
        }
        out.push_back(static_cast<char>(ch));
    }

    out = Trim(out);
    if (StartsWithInsensitive(out, "GS ") || StartsWithInsensitive(out, "GS"))
    {
        out.erase(0, 2);
        out = Trim(out);
    }
    if (out.size() >= 8 && StartsWithInsensitive(out, "Interior"))
    {
        out.erase(0, 8);
        out = Trim(out);
    }
    if (out.size() >= 8 && out.size() >= 8 && StartsWithInsensitive(out.substr(out.size() - 8), "Interior"))
    {
        out.erase(out.size() - 8);
        out = Trim(out);
    }

    return out;
}

std::string InferMajorLocationFromCellIdentifier(const std::string& rawCell)
{
    const std::string slug = Slugify(rawCell);
    if (StartsWithInsensitive(rawCell, "GS")
        || slug.find("goodsprings") != std::string::npos
        || slug.find("prospector_saloon") != std::string::npos
        || slug.find("prospectorsaloon") != std::string::npos
        || slug.find("doc_mitchell") != std::string::npos
        || slug.find("docmitchell") != std::string::npos
        || slug.find("general_store") != std::string::npos
        || slug.find("generalstore") != std::string::npos
        || slug.find("schoolhouse") != std::string::npos
        || slug.find("goodsprings_source") != std::string::npos
        || slug.find("cemetery") != std::string::npos)
    {
        return "Goodsprings";
    }
    return "";
}

std::string InferMinorLocationFromCellIdentifier(const std::string& rawCell)
{
    const std::string slug = Slugify(rawCell);
    if (slug.find("prospector_saloon") != std::string::npos || slug.find("prospectorsaloon") != std::string::npos)
    {
        return "Prospector Saloon";
    }
    if (slug.find("doc_mitchell") != std::string::npos || slug.find("docmitchell") != std::string::npos)
    {
        return "Doc Mitchell's House";
    }
    if (slug.find("general_store") != std::string::npos || slug.find("generalstore") != std::string::npos)
    {
        return "Chet's General Store";
    }
    if (slug.find("schoolhouse") != std::string::npos)
    {
        return "Goodsprings Schoolhouse";
    }

    const std::string humanized = HumanizeIdentifier(rawCell);
    if (!humanized.empty() && Slugify(humanized) != "wilderness")
    {
        return humanized;
    }
    return "";
}

UInt32 MakeWorldCellKey(SInt32 x, SInt32 y)
{
    return (static_cast<UInt32>(x) << 16) + ((static_cast<UInt32>(y) << 16) >> 16);
}

std::optional<std::pair<SInt32, SInt32>> GetWorldCellCoordinates(const TESObjectCELL* cell)
{
    if (!cell || !cell->cellData)
    {
        return std::nullopt;
    }

    return std::make_pair(
        static_cast<SInt32>(cell->cellData->x),
        static_cast<SInt32>(cell->cellData->y));
}

std::string GetLoadDoorDestinationName(TESObjectREFR* ref)
{
    if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESObjectDOOR)
    {
        return "";
    }

    const std::string sourceDoorName = GetFormNameSafe(ref->baseForm);
    if (!sourceDoorName.empty())
    {
        return sourceDoorName;
    }

    BSExtraData* extra = ref->extraDataList.GetByType(kExtraData_Teleport);
    ExtraTeleport* teleport = extra ? reinterpret_cast<ExtraTeleport*>(extra) : nullptr;
    if (!teleport || !teleport->data || !teleport->data->linkedDoor)
    {
        return "";
    }

    TESObjectREFR* linkedDoor = teleport->data->linkedDoor;
    const std::string linkedDoorName = GetFormNameSafe(linkedDoor->baseForm);
    if (!linkedDoorName.empty())
    {
        return linkedDoorName;
    }

    if (linkedDoor->parentCell)
    {
        const std::string cellName = GetFormNameSafe(linkedDoor->parentCell);
        if (!cellName.empty())
        {
            return cellName;
        }
    }

    return GetFormNameSafe(linkedDoor);
}

std::string GetNaturalMinorLocationName(TESObjectREFR* ref)
{
    const std::string doorDestination = GetLoadDoorDestinationName(ref);
    if (!doorDestination.empty())
    {
        return doorDestination;
    }

    return GetFormNameSafe(ref);
}

HWND GetGameWindow()
{
    auto globals = reinterpret_cast<OSGlobals**>(kOSGlobalsAddress);
    if (globals && *globals)
    {
        return (*globals)->window;
    }
    return nullptr;
}

bool GameWindowHasFocus()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd)
    {
        return false;
    }

    HWND foreground = GetForegroundWindow();
    if (!foreground)
    {
        return false;
    }

    if (foreground == hwnd)
    {
        return true;
    }

    DWORD gamePid = 0;
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(hwnd, &gamePid);
    GetWindowThreadProcessId(foreground, &foregroundPid);
    return gamePid != 0 && gamePid == foregroundPid;
}

TESObjectREFR* GetCrosshairRef()
{
    auto* ui = *reinterpret_cast<InterfaceManager**>(kInterfaceManagerSingletonAddress);
    return ui ? ui->crosshairRef : nullptr;
}

PlayerCharacter* GetPlayer()
{
    return *reinterpret_cast<PlayerCharacter**>(kPlayerSingletonAddress);
}

DataHandler* GetDataHandler()
{
    return *reinterpret_cast<DataHandler**>(kDataHandlerSingletonAddress);
}

bool IsMenuVisible(UInt32 menuType)
{
    if (menuType < kMenuType_Min || menuType > kMenuType_Max)
    {
        return false;
    }

    auto* menuVisibility = reinterpret_cast<UInt8*>(kMenuVisibilityArrayAddress);
    return menuVisibility[menuType] != 0;
}

bool IsMenuAllocated(UInt32 menuType)
{
    auto* menuArray = reinterpret_cast<NiTArray<TileMenu*>*>(kTileMenuArrayAddress);
    if (!menuArray || menuType < kMenuType_Min || menuType > kMenuType_Max)
    {
        return false;
    }

    return menuArray->Get(menuType - kMenuType_Min) != nullptr;
}

bool IsTextInputMenuActive()
{
    return IsMenuVisible(kMenuType_TextEdit) || IsMenuAllocated(kMenuType_TextEdit);
}

std::optional<std::pair<std::string, std::string>> ResolveMappedNpcImpl(TESObjectREFR* ref, bool logUnmapped)
{
    if (!ref)
    {
        return std::nullopt;
    }

    // Companions claim their identity before any static mapping: a ref whose
    // base is a claimed NVCompanions.esp template resolves to the companion's
    // stable npc_key/name so chasm binds it to the authored character card.
    if (auto companion = ResolveCompanionNpcForRef(ref); companion.has_value())
    {
        return companion;
    }

    const auto makeMatch = [](std::string_view key, std::string_view display) {
        return std::make_pair(std::string(key), std::string(display));
    };
    const auto makeRefScopedMatch = [ref](std::string_view baseKey, std::string_view fallbackDisplay) {
        std::ostringstream key;
        key << std::string(baseKey);
        if (ref)
        {
            key << "__ref_" << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << ref->refID;
        }

        std::string display = std::string(fallbackDisplay);
        if (ref)
        {
            if (char* refName = ref->GetName())
            {
                const std::string trimmed = Trim(refName);
                if (!trimmed.empty())
                {
                    display = trimmed;
                }
            }
            else if (ref->baseForm && ref->baseForm != ref)
            {
                if (char* baseName = ref->baseForm->GetName())
                {
                    const std::string trimmed = Trim(baseName);
                    if (!trimmed.empty())
                    {
                        display = trimmed;
                    }
                }
            }
        }

        return std::make_pair(key.str(), display);
    };
    const UInt32 refLocalFormId = ref
        ? (ref->refID & 0x00FFFFFF)
        : 0;
    const UInt32 baseLocalFormId = (ref && ref->baseForm)
        ? (ref->baseForm->refID & 0x00FFFFFF)
        : 0;
    TESNPC* baseNpc = (ref && ref->baseForm && ref->baseForm->typeID == kFormType_TESNPC)
        ? static_cast<TESNPC*>(ref->baseForm)
        : nullptr;
    const UInt32 templateLocalFormId = (baseNpc && baseNpc->copyFrom)
        ? (baseNpc->copyFrom->refID & 0x00FFFFFF)
        : 0;
    const UInt32 cellLocalFormId = (ref && ref->parentCell)
        ? (ref->parentCell->refID & 0x00FFFFFF)
        : 0;
    const std::string cellNameSlug = Slugify(GetFormNameSafe(ref ? ref->parentCell : nullptr));
    const auto resolveVoiceTypeSlug = [baseNpc]() {
        if (!baseNpc)
        {
            return std::string();
        }

        BGSVoiceType* voiceType = baseNpc->baseData.GetVoiceType();
        if (!voiceType)
        {
            voiceType = baseNpc->baseData.voiceType;
        }

        return Slugify(GetFormNameSafe(voiceType));
    };
    const std::string voiceTypeSlug = resolveVoiceTypeSlug();
    const auto makePowderGangerVariantMatch = [&makeRefScopedMatch, &voiceTypeSlug]() {
        std::string baseKey = "powder_ganger";
        if (!voiceTypeSlug.empty())
        {
            baseKey += "_";
            baseKey += voiceTypeSlug;
        }
        return makeRefScopedMatch(baseKey, "Powder Ganger");
    };
    const auto matchesAnyLocalFormId = [refLocalFormId, baseLocalFormId, templateLocalFormId](std::initializer_list<UInt32> formIds) {
        for (const UInt32 formId : formIds)
        {
            if (formId == 0)
            {
                continue;
            }

            if (refLocalFormId == formId || baseLocalFormId == formId || templateLocalFormId == formId)
            {
                return true;
            }
        }

        return false;
    };
    const auto endsWith = [](const std::string& value, const std::string& suffix) {
        return value.size() >= suffix.size()
            && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const auto isLikelyEditorRefName = [&endsWith](const std::string& value) {
        const std::string trimmed = Trim(value);
        if (trimmed.empty())
        {
            return false;
        }

        if (trimmed.find_first_of(" \t\r\n") != std::string::npos)
        {
            return false;
        }

        const std::string slug = Slugify(trimmed);
        if (slug.empty())
        {
            return false;
        }

        return endsWith(slug, "ref")
            || endsWith(slug, "marker")
            || endsWith(slug, "trigger")
            || endsWith(slug, "template");
    };

    std::vector<std::string> candidates;
    if (char* name = ref->GetName())
    {
        const std::string trimmed = Trim(name);
        if (!isLikelyEditorRefName(trimmed))
        {
            candidates.push_back(Slugify(trimmed));
        }
    }
    if (ref->baseForm && ref->baseForm != ref)
    {
        if (char* name = ref->baseForm->GetName())
        {
            candidates.push_back(Slugify(name));
        }
    }
    if (baseNpc && baseNpc->copyFrom)
    {
        if (char* templateName = baseNpc->copyFrom->GetName())
        {
            candidates.push_back(Slugify(templateName));
        }
    }
    const auto resolveVisibleName = [ref, baseNpc, &isLikelyEditorRefName]() -> std::string {
        // Prefer the real display name (GetTheName) over GetName(): in FNV,
        // GetName() leaks the editor ID (e.g. "NobarkNoonan") for any NPC that
        // isn't in the hardcoded roster above, which then fails to match its
        // display-named character card. GetName() stays as a fallback so a
        // truly-nameless ref keeps its previous behaviour.
        if (ref)
        {
            if (const char* display = ref->GetTheName())
            {
                const std::string trimmed = Trim(display);
                if (!trimmed.empty() && !isLikelyEditorRefName(trimmed))
                {
                    return trimmed;
                }
            }
            if (char* refName = ref->GetName())
            {
                const std::string trimmed = Trim(refName);
                if (!trimmed.empty() && !isLikelyEditorRefName(trimmed))
                {
                    return trimmed;
                }
            }
        }

        if (ref && ref->baseForm && ref->baseForm != ref)
        {
            if (const char* display = ref->baseForm->GetTheName())
            {
                const std::string trimmed = Trim(display);
                if (!trimmed.empty())
                {
                    return trimmed;
                }
            }
            if (char* baseName = ref->baseForm->GetName())
            {
                const std::string trimmed = Trim(baseName);
                if (!trimmed.empty())
                {
                    return trimmed;
                }
            }
        }

        if (baseNpc && baseNpc->copyFrom)
        {
            if (const char* display = baseNpc->copyFrom->GetTheName())
            {
                const std::string trimmed = Trim(display);
                if (!trimmed.empty())
                {
                    return trimmed;
                }
            }
            if (char* templateName = baseNpc->copyFrom->GetName())
            {
                const std::string trimmed = Trim(templateName);
                if (!trimmed.empty())
                {
                    return trimmed;
                }
            }
        }

        return std::string();
    };
    const std::string visibleName = resolveVisibleName();
    const std::string visibleNameSlug = Slugify(visibleName);

    const auto containsAny = [&candidates](std::initializer_list<std::string_view> needles) {
        for (const auto& candidate : candidates)
        {
            for (const auto needle : needles)
            {
                if (candidate.find(needle) != std::string::npos)
                {
                    return true;
                }
            }
        }
        return false;
    };

    const auto isFemaleNpc = [ref]() -> bool {
        if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESNPC)
        {
            return false;
        }

        auto* npc = static_cast<TESNPC*>(ref->baseForm);
        return npc && npc->baseData.IsFemale();
    };
    const auto hasPowderGangerFaction = [baseNpc]() -> bool {
        if (!baseNpc)
        {
            return false;
        }

        for (tList<TESActorBaseData::FactionListData>::Iterator iter = baseNpc->baseData.factionList.Begin(); !iter.End(); ++iter)
        {
            TESActorBaseData::FactionListData* data = iter.Get();
            if (!data || !data->faction)
            {
                continue;
            }

            const std::string factionName = Slugify(GetFormNameSafe(data->faction));
            if (factionName.find("powder_ganger") != std::string::npos
                || factionName.find("powdergang") != std::string::npos
                || factionName.find("convict") != std::string::npos)
            {
                return true;
            }
        }

        return false;
    };
    const auto isAnonymousPrisonNpc = [ref, cellLocalFormId, cellNameSlug, voiceTypeSlug]() -> bool {
        if (!ref || !ref->parentCell || !ref->baseForm || ref->baseForm->typeID != kFormType_TESNPC)
        {
            return false;
        }

        const bool hasVisibleName = (ref->GetName() && *ref->GetName())
            || (ref->baseForm->GetName() && *ref->baseForm->GetName());
        if (hasVisibleName)
        {
            return false;
        }

        if (cellLocalFormId == 0x0008D0D5)
        {
            return true;
        }

        const bool prisonCellByName = cellNameSlug.find("ncrprison") != std::string::npos
            || cellNameSlug.find("visitors_center") != std::string::npos
            || cellNameSlug.find("ncr_correctional_facility") != std::string::npos
            || cellNameSlug.find("correctional_facility") != std::string::npos;
        if (!prisonCellByName)
        {
            return false;
        }

        return voiceTypeSlug.find("powder") != std::string::npos
            || voiceTypeSlug.find("convict") != std::string::npos
            || voiceTypeSlug.find("raider") != std::string::npos
            || voiceTypeSlug.empty();
    };

    switch (baseLocalFormId)
    {
    case 0x0008D371: // Powder Ganger (melee)
    case 0x0008D372: // Powder Ganger (guns)
    case 0x0008F115: // Powder Ganger NCR CF (guns)
    case 0x00090B87: // Powder Ganger NCR CF (melee)
    case 0x0015F310: // Powder Ganger (Goodsprings)
    case 0x000A5AD5: // Powder Ganger bodyguard 01
    case 0x000A5AD8: // Powder Ganger bodyguard 02
    case 0x000E3696: // Powder Ganger bodyguard 03
    case 0x00000963: // Anonymous NCRCF Powder Ganger variant
    case 0x00001097: // Anonymous NCRCF Powder Ganger variant
        return makePowderGangerVariantMatch();
    default:
        break;
    }
    switch (templateLocalFormId)
    {
    case 0x0008D371: // Powder Ganger (melee)
    case 0x0008D372: // Powder Ganger (guns)
    case 0x0008F115: // Powder Ganger NCR CF (guns)
    case 0x00090B87: // Powder Ganger NCR CF (melee)
    case 0x0015F310: // Powder Ganger (Goodsprings)
    case 0x000A5AD5: // Powder Ganger bodyguard 01
    case 0x000A5AD8: // Powder Ganger bodyguard 02
    case 0x000E3696: // Powder Ganger bodyguard 03
    case 0x00000963: // Anonymous NCRCF Powder Ganger variant
    case 0x00001097: // Anonymous NCRCF Powder Ganger variant
        return makePowderGangerVariantMatch();
    default:
        break;
    }

    // Named NCRCF actors must resolve by exact ID, otherwise nearby generic convicts
    // can be misclassified as Eddie or other prison uniques via loose name heuristics.
    if (matchesAnyLocalFormId({ 0x000D7036, 0x0008D0E9 })) return makeMatch("eddie", "Eddie");
    if (matchesAnyLocalFormId({ 0x000D6F51, 0x0008F13A })) return makeMatch("dawes", "Dawes");
    if (matchesAnyLocalFormId({ 0x000D71B7, 0x000D71B6 })) return makeMatch("hannigan", "Hannigan");
    if (matchesAnyLocalFormId({ 0x000CEF3C, 0x000CEF3B })) return makeMatch("carter", "Carter");
    if (matchesAnyLocalFormId({ 0x0008D501, 0x0008D0E7 })) return makeMatch("meyers", "Meyers");
    if (matchesAnyLocalFormId({ 0x000D7037, 0x0008D0EB })) return makeMatch("scrambler", "Scrambler");
    if (matchesAnyLocalFormId({ 0x000E32A3, 0x000E32A2 })) return makeMatch("chavez", "Chavez");

    if (containsAny({ "easy_pete", "easypete" })) return makeMatch("easy_pete", "Easy Pete");
    if (containsAny({ "sunny_smiles", "sunnysmiles" })) return makeMatch("sunny_smiles", "Sunny Smiles");
    if (containsAny({ "doc_mitchell", "docmitchell", "mitchell" })) return makeMatch("doc_mitchell", "Doc Mitchell");
    if (containsAny({ "trudy" })) return makeMatch("trudy", "Trudy");
    if (containsAny({ "chet" })) return makeMatch("chet", "Chet");
    if (containsAny({ "victor" })) return makeMatch("victor", "Victor");
    if (containsAny({ "ringo" })) return makeMatch("ringo", "Ringo");
    if (containsAny({ "cheyenne" })) return makeMatch("cheyenne", "Cheyenne");
    if (containsAny({ "goodsprings_settler", "goodspringssettler" })) return makeRefScopedMatch(isFemaleNpc() ? "goodsprings_settler_female" : "goodsprings_settler_male", "Goodsprings Settler");
    if (containsAny({ "powder_ganger", "powderganger", "powder_gangers", "powdergangers", "powder_ganger_bodyguard", "powdergangerbodyguard", "escaped_convict", "escapedconvict", "convict" })) return makePowderGangerVariantMatch();
    if (hasPowderGangerFaction()) return makePowderGangerVariantMatch();
    if (isAnonymousPrisonNpc()) return makePowderGangerVariantMatch();
    if (baseNpc && !visibleNameSlug.empty()) return makeMatch(visibleNameSlug, visibleName);

    if (logUnmapped && !candidates.empty())
    {
        LogLine("Unmapped target: ref=%08X base=%08X name=%s",
            ref->refID,
            ref->baseForm ? ref->baseForm->refID : 0,
            candidates.front().c_str());
    }

    return std::nullopt;
}

void ConsiderNearestMappedNpc(PlayerCharacter* player, TESObjectREFR* ref, double maxDistanceSquared, bool underCrosshair, ResolvedNpcTarget& best)
{
    if (!player || !ref || ref == player)
    {
        return;
    }

    if (!ref->baseForm)
    {
        return;
    }

    const UInt8 baseType = ref->baseForm->typeID;
    if (baseType != kFormType_TESNPC && baseType != kFormType_TESCreature)
    {
        return;
    }

    if (!IsLiveNearbyActorRef(player, ref))
    {
        return;
    }

    const auto resolved = ResolveMappedNpcImpl(ref, false);
    if (!resolved.has_value())
    {
        return;
    }

    const double distanceSquared = DistanceSquared3D(player, ref);
    if (distanceSquared > maxDistanceSquared)
    {
        return;
    }

    if (best.ref)
    {
        if (underCrosshair && !best.underCrosshair)
        {
            // Keep the actor under the crosshair as the direct-talk target.
        }
        else if (!underCrosshair && best.underCrosshair)
        {
            return;
        }
        else if (distanceSquared >= best.distanceSquared)
        {
            return;
        }
    }

    best.ref = ref;
    best.npcKey = resolved->first;
    best.npcName = resolved->second;
    const auto voiceType = ResolveRefVoiceTypeMetadata(ref);
    best.voiceTypeKey = voiceType.first;
    best.voiceTypeName = voiceType.second;
    best.distanceSquared = distanceSquared;
    best.underCrosshair = underCrosshair;
}

bool IsLiveNearbyActorRef(const TESObjectREFR* anchorRef, TESObjectREFR* ref)
{
    if (!anchorRef || !ref || ref == anchorRef)
    {
        return false;
    }

    if (ref->IsDeleted())
    {
        return false;
    }

    if (!ref->GetInSameCellOrWorld(const_cast<TESObjectREFR*>(anchorRef)))
    {
        return false;
    }

    // The cell object list can retain actor references that are not actually loaded into
    // the active scene. Nearby chat should only consider actors that currently have live 3D.
    if (!ref->GetNiNode())
    {
        return false;
    }

    const UInt8 baseType = ref->baseForm ? ref->baseForm->typeID : 0;
    if (baseType == kFormType_TESNPC || baseType == kFormType_TESCreature)
    {
        const auto* mobile = static_cast<const MobileObject*>(ref);
        if (!mobile->baseProcess)
        {
            return false;
        }
    }

    return true;
}

void CollectNearbyMappedNpcAround(const TESObjectREFR* anchorRef, TESObjectREFR* ref, double maxDistanceSquared, bool underCrosshair, std::vector<NearbyNpcCandidate>& candidates)
{
    if (!anchorRef || !ref || ref == anchorRef)
    {
        return;
    }

    if (!ref->baseForm)
    {
        return;
    }

    const UInt8 baseType = ref->baseForm->typeID;
    if (baseType != kFormType_TESNPC && baseType != kFormType_TESCreature)
    {
        return;
    }

    if (!IsLiveNearbyActorRef(anchorRef, ref))
    {
        return;
    }

    const double distanceSquared = DistanceSquared3D(anchorRef, ref);
    if (distanceSquared > maxDistanceSquared)
    {
        return;
    }

    const auto resolved = ResolveMappedNpcImpl(ref, false);
    if (!resolved.has_value())
    {
        TESNPC* baseNpc = (ref->baseForm && ref->baseForm->typeID == kFormType_TESNPC)
            ? static_cast<TESNPC*>(ref->baseForm)
            : nullptr;
        const UInt32 templateRefId = (baseNpc && baseNpc->copyFrom) ? baseNpc->copyFrom->refID : 0;
        BGSVoiceType* voiceType = baseNpc ? baseNpc->baseData.GetVoiceType() : nullptr;
        if (!voiceType && baseNpc)
        {
            voiceType = baseNpc->baseData.voiceType;
        }
        const char* refName = ref->GetName();
        const char* baseName = (ref->baseForm && ref->baseForm != ref) ? ref->baseForm->GetName() : nullptr;
        LogLine("Nearby unmapped actor: ref=%08X base=%08X template=%08X cell=%08X dist=%.2fm voice=%s name=%s base_name=%s",
            ref->refID,
            ref->baseForm ? ref->baseForm->refID : 0,
            templateRefId,
            ref->parentCell ? ref->parentCell->refID : 0,
            std::sqrt(distanceSquared) / kGameUnitsPerMeter,
            voiceType ? GetFormNameSafe(voiceType).c_str() : "",
            refName ? refName : "",
            baseName ? baseName : "");
        return;
    }

    auto existing = std::find_if(candidates.begin(), candidates.end(), [&](const NearbyNpcCandidate& candidate) {
        return candidate.ref == ref || (candidate.ref && ref && candidate.ref->refID == ref->refID);
        });
    if (existing != candidates.end())
    {
        existing->underCrosshair = existing->underCrosshair || underCrosshair;
        if (distanceSquared < existing->distanceSquared)
        {
            existing->distanceSquared = distanceSquared;
            existing->npcKey = resolved->first;
            existing->npcName = resolved->second;
            const auto voiceType = ResolveRefVoiceTypeMetadata(ref);
            existing->voiceTypeKey = voiceType.first;
            existing->voiceTypeName = voiceType.second;
        }
        return;
    }

    const auto voiceType = ResolveRefVoiceTypeMetadata(ref);
    candidates.push_back({
        ref,
        resolved->first,
        resolved->second,
        voiceType.first,
        voiceType.second,
        distanceSquared,
        underCrosshair,
        });
}

std::vector<NearbyNpcCandidate> FindNearbyMappedNpcsAround(const TESObjectREFR* anchorRef, float maxDistanceMeters)
{
    std::vector<NearbyNpcCandidate> candidates;
    if (!anchorRef)
    {
        return candidates;
    }

    const TESObjectCELL* anchorCell = anchorRef->parentCell;
    if (!anchorCell)
    {
        return candidates;
    }

    const double maxDistanceSquared = std::pow(static_cast<double>(maxDistanceMeters * kGameUnitsPerMeter), 2.0);
    TESObjectREFR* crosshairTarget = GetCrosshairRef();
    if (crosshairTarget)
    {
        CollectNearbyMappedNpcAround(anchorRef, crosshairTarget, maxDistanceSquared, true, candidates);
    }

    auto scanCell = [&](TESObjectCELL* cell) {
        if (!cell)
        {
            return;
        }

        for (auto iter = cell->objectList.Begin(); !iter.End(); ++iter)
        {
            TESObjectREFR* ref = *iter;
            CollectNearbyMappedNpcAround(anchorRef, ref, maxDistanceSquared, ref == crosshairTarget, candidates);
        }
    };

    scanCell(const_cast<TESObjectCELL*>(anchorCell));

    TESWorldSpace* worldSpace = anchorCell->worldSpace;
    const auto anchorCellCoordinates = GetWorldCellCoordinates(anchorCell);
    if (worldSpace && worldSpace->cellMap && anchorCellCoordinates.has_value())
    {
        const SInt32 centerX = anchorCellCoordinates->first;
        const SInt32 centerY = anchorCellCoordinates->second;
        for (SInt32 y = centerY - 1; y <= centerY + 1; ++y)
        {
            for (SInt32 x = centerX - 1; x <= centerX + 1; ++x)
            {
                if (x == centerX && y == centerY)
                {
                    continue;
                }
                scanCell(worldSpace->cellMap->Lookup(MakeWorldCellKey(x, y)));
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const NearbyNpcCandidate& left, const NearbyNpcCandidate& right) {
        if (left.underCrosshair != right.underCrosshair)
        {
            return left.underCrosshair && !right.underCrosshair;
        }
        if (left.distanceSquared != right.distanceSquared)
        {
            return left.distanceSquared < right.distanceSquared;
        }
        return _stricmp(left.npcName.c_str(), right.npcName.c_str()) < 0;
        });

    return candidates;
}

std::vector<NearbyNpcCandidate> FindNearbyMappedNpcsForGroupChat(PlayerCharacter* player, float maxDistanceMeters)
{
    return FindNearbyMappedNpcsAround(player, maxDistanceMeters);
}

void MergeNearbyNpcCandidates(std::vector<NearbyNpcCandidate>& target, const std::vector<NearbyNpcCandidate>& source)
{
    for (const NearbyNpcCandidate& candidate : source)
    {
        auto existing = std::find_if(target.begin(), target.end(), [&](const NearbyNpcCandidate& current) {
            return current.ref == candidate.ref || (current.ref && candidate.ref && current.ref->refID == candidate.ref->refID);
            });

        if (existing == target.end())
        {
            target.push_back(candidate);
            continue;
        }

        existing->underCrosshair = existing->underCrosshair || candidate.underCrosshair;
        if (candidate.distanceSquared < existing->distanceSquared)
        {
            existing->distanceSquared = candidate.distanceSquared;
            existing->npcKey = candidate.npcKey;
            existing->npcName = candidate.npcName;
        }
    }
}

// ---------------------------------------------------------------------------
// Gamestate macros
// ---------------------------------------------------------------------------
// Per-turn player state sent to chasm as `metadata.macros` — a flat
// string->string map of full display values (e.g. "Goodsprings", never an
// internal id). chasm records the table each turn and resolves `{{key}}`
// placeholders against it, so the key names below are the contract with the
// backend (see mod-source/docs/gamestate-macros.md). Every field degrades
// gracefully: when a read fails the key is omitted entirely (never null), and
// the returned JSON is always a well-formed single-line object.

std::string GetDisplayNameSafe(TESForm* form)
{
    if (!form)
    {
        return "";
    }

    const char* name = form->GetTheName();
    if (!name || !*name)
    {
        return "";
    }

    return SanitizeLine(name);
}

int ReadPlayerActorValueInt(PlayerCharacter* player, UInt32 avCode)
{
    // ActorValueOwner::Fn_02 is the game's GetActorValue (current, integer).
    return static_cast<int>(player->avOwner.Fn_02(avCode));
}

void AppendJsonMacro(std::ostringstream& out, bool& first, const char* key, const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    if (!first)
    {
        out << ",";
    }
    first = false;
    out << JsonEscape(key) << ":" << JsonEscape(value);
}

std::string JoinWithOverflow(const std::vector<std::string>& items, size_t maxItems, const char* separator = ", ")
{
    if (items.empty())
    {
        return "";
    }

    std::ostringstream out;
    const size_t shown = (std::min)(items.size(), maxItems);
    for (size_t index = 0; index < shown; ++index)
    {
        if (index > 0)
        {
            out << separator;
        }
        out << items[index];
    }
    if (items.size() > shown)
    {
        out << separator << "+" << (items.size() - shown) << " more";
    }
    return out.str();
}

// Scheduler clock source. Reads a vanilla TESGlobal float by its (hard-coded,
// FalloutNV.esm) form id. GameHour (0x38) is the 0..24 float wall clock. Returns
// false (and leaves `out` untouched) at the main menu / before a save loads, when
// the global is absent. (GameDaysPassed does NOT read cleanly by form id here —
// see ReadGameDaysPassed, which resolves it by name via the script compiler.)
bool ReadGlobalFloat(UInt32 formId, float& out)
{
    TESForm* form = LookupFormByID(formId);
    if (!form || form->typeID != kFormType_TESGlobal)
    {
        return false;
    }
    out = static_cast<TESGlobal*>(form)->data;
    return true;
}

// GameDaysPassed — the monotonic in-game day counter (increases ~1.0/day). Its
// runtime form id is not the commonly-cited 0x26 in FalloutNV, so rather than
// guess we compile a trivial GECK function once (the compiler resolves the global
// BY NAME) and call it. Cheap: a cached script call. Returns false before the
// script interface / player exist, or if the call fails.
bool ReadGameDaysPassed(float& out)
{
    // Cache for ~1s: the heartbeat asks 10x/second but the day counter barely
    // moves, so cap the actual script call to once per second.
    static float s_cachedDays = 0.0f;
    static DWORD s_cachedTick = 0;
    const DWORD nowTick = GetTickCount();
    if (s_cachedTick != 0 && (nowTick - s_cachedTick) < 1000)
    {
        out = s_cachedDays;
        return true;
    }

    if (!g_scriptInterface)
    {
        return false;
    }
    if (!g_gameDaysPassedScript && !g_gameDaysPassedScriptAttempted)
    {
        g_gameDaysPassedScriptAttempted = true;
        constexpr char kScript[] = R"(
float fDays

Begin Function { }
    let fDays := GameDaysPassed
    SetFunctionValue fDays
End
)";
        g_gameDaysPassedScript = g_scriptInterface->CompileScript(kScript);
        if (!g_gameDaysPassedScript)
        {
            LogLine("scheduler: failed to compile GameDaysPassed helper.");
        }
    }
    if (!g_gameDaysPassedScript)
    {
        return false;
    }
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_gameDaysPassedScript, player, nullptr, &result, 0);
    if (!callOk || result.GetType() != NVSEArrayVarInterface::Element::kType_Numeric)
    {
        return false;
    }
    out = static_cast<float>(result.GetNumber());
    s_cachedDays = out;
    s_cachedTick = nowTick;
    return true;
}

// "13.5217"-style plain decimal for a clock global, for the turn macro table and
// heartbeat (chasm parses these back to a day+hour). Fixed 4dp keeps ~0.4s hour
// resolution while staying stable regardless of the caller's ostream flags.
std::string FormatClockFloat(float value)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(value));
    return buffer;
}

std::string BuildTimeOfDayMacro()
{
    TESForm* gameHourForm = LookupFormByID(0x38); // vanilla GameHour global (FalloutNV.esm)
    if (!gameHourForm || gameHourForm->typeID != kFormType_TESGlobal)
    {
        return "";
    }

    const float gameHour = static_cast<TESGlobal*>(gameHourForm)->data;
    if (!(gameHour >= 0.0f) || gameHour > 24.0f)
    {
        return "";
    }

    int hour = static_cast<int>(gameHour);
    int minute = static_cast<int>((gameHour - static_cast<float>(hour)) * 60.0f + 0.5f);
    if (minute >= 60)
    {
        minute = 0;
        ++hour;
    }
    hour %= 24;

    const char* meridiem = (hour < 12) ? "AM" : "PM";
    int hour12 = hour % 12;
    if (hour12 == 0)
    {
        hour12 = 12;
    }

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d:%02d%s", hour12, minute, meridiem);
    return buffer;
}

struct ActorValueLabel
{
    UInt32 code;
    const char* label;
};

std::string BuildSpecialMacro(PlayerCharacter* player)
{
    static constexpr ActorValueLabel kSpecialValues[] = {
        { eActorVal_Strength, "STR" },
        { eActorVal_Perception, "PER" },
        { eActorVal_Endurance, "END" },
        { eActorVal_Charisma, "CHA" },
        { eActorVal_Intelligence, "INT" },
        { eActorVal_Agility, "AGI" },
        { eActorVal_Luck, "LCK" },
    };

    std::ostringstream out;
    bool first = true;
    for (const ActorValueLabel& entry : kSpecialValues)
    {
        if (!first)
        {
            out << ", ";
        }
        first = false;
        out << entry.label << " " << ReadPlayerActorValueInt(player, entry.code);
    }
    return out.str();
}

std::string BuildSkillsMacro(PlayerCharacter* player)
{
    // Big Guns (33) is omitted: the actor value exists in data but FNV hides it.
    static constexpr ActorValueLabel kSkillValues[] = {
        { eActorVal_Barter, "Barter" },
        { eActorVal_EnergyWeapons, "Energy Weapons" },
        { eActorVal_Explosives, "Explosives" },
        { eActorVal_Guns, "Guns" },
        { eActorVal_Lockpick, "Lockpick" },
        { eActorVal_Medicine, "Medicine" },
        { eActorVal_MeleeWeapons, "Melee Weapons" },
        { eActorVal_Repair, "Repair" },
        { eActorVal_Science, "Science" },
        { eActorVal_Sneak, "Sneak" },
        { eActorVal_Speech, "Speech" },
        { eActorVal_Survival, "Survival" },
        { eActorVal_Unarmed, "Unarmed" },
    };

    std::ostringstream out;
    bool first = true;
    for (const ActorValueLabel& entry : kSkillValues)
    {
        if (!first)
        {
            out << ", ";
        }
        first = false;
        out << entry.label << " " << ReadPlayerActorValueInt(player, entry.code);
    }
    return out.str();
}

std::string BuildConditionMacro(PlayerCharacter* player)
{
    static constexpr ActorValueLabel kLimbValues[] = {
        { eActorVal_Head, "Head" },
        { eActorVal_Torso, "Torso" },
        { eActorVal_LeftArm, "Left Arm" },
        { eActorVal_RightArm, "Right Arm" },
        { eActorVal_LeftLeg, "Left Leg" },
        { eActorVal_RightLeg, "Right Leg" },
    };
    constexpr size_t kLimbCount = sizeof(kLimbValues) / sizeof(kLimbValues[0]);

    std::vector<std::string> crippled;
    for (const ActorValueLabel& entry : kLimbValues)
    {
        if (ReadPlayerActorValueInt(player, entry.code) <= 0)
        {
            crippled.push_back(entry.label);
        }
    }

    if (crippled.empty())
    {
        return "All limbs OK";
    }
    if (crippled.size() == kLimbCount)
    {
        return "All limbs crippled";
    }

    std::ostringstream out;
    for (size_t index = 0; index < crippled.size(); ++index)
    {
        if (index > 0)
        {
            out << ", ";
        }
        out << crippled[index];
    }
    out << " crippled; rest OK";
    return out.str();
}

std::string BuildEffectsMacro(PlayerCharacter* player)
{
    EffectNode* effectList = player->magicTarget.GetEffectList();
    if (!effectList)
    {
        return "";
    }

    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (auto iter = effectList->Begin(); !iter.End(); ++iter)
    {
        ActiveEffect* effect = iter.Get();
        if (!effect || effect->bTerminated || !effect->magicItem)
        {
            continue;
        }

        std::string name = GetStringValueSafe(effect->magicItem->name);
        if (name.empty())
        {
            continue;
        }
        if (!seen.insert(ToLowerAscii(name)).second)
        {
            continue;
        }
        names.push_back(std::move(name));
    }

    return JoinWithOverflow(names, 10);
}

std::string BuildPerksMacro(PlayerCharacter* player)
{
    DataHandler* dataHandler = GetDataHandler();
    if (!dataHandler)
    {
        return "";
    }

    std::vector<std::string> perks;
    for (auto iter = dataHandler->perkList.Begin(); !iter.End(); ++iter)
    {
        BGSPerk* perk = iter.Get();
        if (!perk || perk->data.isHidden)
        {
            continue;
        }

        // GetPerkRank returns 0 when the perk is not applied (0xFF = "-1" error).
        const UInt8 rank = player->GetPerkRank(perk, false);
        if (rank == 0 || rank == 0xFF)
        {
            continue;
        }

        std::string name = GetDisplayNameSafe(perk);
        if (name.empty())
        {
            continue;
        }
        if (rank > 1)
        {
            name += " " + std::to_string(static_cast<int>(rank));
        }
        perks.push_back(std::move(name));
    }

    return JoinWithOverflow(perks, 24);
}

std::string BuildEquippedApparelMacro(PlayerCharacter* player)
{
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    EquippedItemsList equipped = player->GetEquippedItems();
    for (TESForm* item : equipped)
    {
        if (!item || item->typeID != kFormType_TESObjectARMO)
        {
            continue;
        }

        std::string name = GetDisplayNameSafe(item);
        if (name.empty())
        {
            continue;
        }
        if (!seen.insert(ToLowerAscii(name)).second)
        {
            continue;
        }
        names.push_back(std::move(name));
    }

    return JoinWithOverflow(names, 8);
}

std::string BuildInventoryMacro(PlayerCharacter* player)
{
    InventoryItemsMap invItems(0x40);
    if (!player->GetInventoryItems(invItems))
    {
        return "";
    }

    struct InventoryLine
    {
        std::string name;
        SInt32 count = 0;
    };

    // Curated: aid, ammo, weapons, and apparel only, each capped, so the value
    // stays prompt-sized. Everything else (misc, books, keys, caps...) is
    // counted into the trailing "+N more".
    std::vector<InventoryLine> aid;
    std::vector<InventoryLine> ammo;
    std::vector<InventoryLine> weapons;
    std::vector<InventoryLine> apparel;
    size_t omitted = 0;

    for (auto iter = invItems.Begin(); !iter.End(); ++iter)
    {
        TESForm* item = iter.Key();
        const SInt32 count = iter.Get().count;
        if (!item || count <= 0)
        {
            continue;
        }

        std::vector<InventoryLine>* bucket = nullptr;
        switch (item->typeID)
        {
        case kFormType_AlchemyItem:
            bucket = &aid;
            break;
        case kFormType_TESAmmo:
            bucket = &ammo;
            break;
        case kFormType_TESObjectWEAP:
            bucket = &weapons;
            break;
        case kFormType_TESObjectARMO:
            bucket = &apparel;
            break;
        default:
            ++omitted;
            continue;
        }

        std::string name = GetDisplayNameSafe(item);
        if (name.empty())
        {
            ++omitted;
            continue;
        }
        bucket->push_back({ std::move(name), count });
    }

    const auto byCountDescThenName = [](const InventoryLine& left, const InventoryLine& right) {
        if (left.count != right.count)
        {
            return left.count > right.count;
        }
        return _stricmp(left.name.c_str(), right.name.c_str()) < 0;
    };

    std::vector<std::string> lines;
    const auto appendBucket = [&](std::vector<InventoryLine>& bucket, size_t cap) {
        std::sort(bucket.begin(), bucket.end(), byCountDescThenName);
        for (size_t index = 0; index < bucket.size(); ++index)
        {
            if (index >= cap)
            {
                ++omitted;
                continue;
            }
            std::ostringstream line;
            line << bucket[index].name;
            if (bucket[index].count > 1)
            {
                line << " x" << bucket[index].count;
            }
            lines.push_back(line.str());
        }
    };

    appendBucket(aid, 10);
    appendBucket(ammo, 6);
    appendBucket(weapons, 6);
    appendBucket(apparel, 6);

    if (lines.empty())
    {
        return "";
    }

    std::ostringstream out;
    for (size_t index = 0; index < lines.size(); ++index)
    {
        if (index > 0)
        {
            out << ", ";
        }
        out << lines[index];
    }
    if (omitted > 0)
    {
        out << ", +" << omitted << " more";
    }
    return out.str();
}

void BuildQuestMacros(PlayerCharacter* player, std::string& outQuests, std::string& outMiscQuests)
{
    struct QuestLine
    {
        TESQuest* quest = nullptr;
        std::string text;
    };

    std::vector<QuestLine> quests;
    std::vector<std::string> miscQuests;
    std::unordered_set<TESQuest*> seenQuests;
    std::unordered_set<std::string> seenMisc;

    for (auto iter = player->questObjectiveList.Begin(); !iter.End(); ++iter)
    {
        BGSQuestObjective* objective = iter.Get();
        if (!objective || !objective->quest)
        {
            continue;
        }

        // Pip-Boy semantics: an objective is live when flagged displayed and
        // not completed.
        if ((objective->status & BGSQuestObjective::eQObjStatus_displayed) == 0 ||
            (objective->status & BGSQuestObjective::eQObjStatus_completed) != 0)
        {
            continue;
        }

        TESQuest* quest = objective->quest;
        const std::string questName = GetDisplayNameSafe(quest);
        const std::string objectiveText = GetStringValueSafe(objective->displayText);

        // Objectives from quests without a display name land in the Pip-Boy's
        // "Misc" bucket; mirror that split here.
        if (questName.empty())
        {
            if (!objectiveText.empty() && seenMisc.insert(ToLowerAscii(objectiveText)).second)
            {
                miscQuests.push_back(objectiveText);
            }
            continue;
        }

        // The list is newest-first, so the first live objective seen for a
        // quest is its current one.
        if (!seenQuests.insert(quest).second)
        {
            continue;
        }

        QuestLine line;
        line.quest = quest;
        line.text = questName;
        if (!objectiveText.empty())
        {
            line.text += " - " + objectiveText;
        }
        quests.push_back(std::move(line));
    }

    // Put the tracked quest first so a capped list never drops it.
    if (player->quest)
    {
        std::stable_partition(quests.begin(), quests.end(), [player](const QuestLine& line) {
            return line.quest == player->quest;
        });
    }

    std::vector<std::string> questTexts;
    questTexts.reserve(quests.size());
    for (QuestLine& line : quests)
    {
        questTexts.push_back(std::move(line.text));
    }

    outQuests = JoinWithOverflow(questTexts, 8, "; ");
    outMiscQuests = JoinWithOverflow(miscQuests, 6, "; ");
}

std::string BuildPlayerMacros(PlayerCharacter* player, const LocationSnapshot* location = nullptr)
{
    if (!player)
    {
        return "";
    }

    LocationSnapshot capturedLocation;
    if (!location)
    {
        capturedLocation = CapturePlayerLocation();
        location = &capturedLocation;
    }

    std::ostringstream out;
    out << "{";
    bool first = true;

    AppendJsonMacro(out, first, "player_name", GetDisplayNameSafe(player));

    const int level = static_cast<int>(player->avOwner.Fn_0A());
    if (level > 0)
    {
        AppendJsonMacro(out, first, "level", std::to_string(level));
    }

    // Copied from the snapshot the turn already captured, so the macro table is
    // self-contained even though `location` is also sent at the top level.
    AppendJsonMacro(out, first, "major_location", location->major);
    AppendJsonMacro(out, first, "minor_location", location->minor);
    // Inside vs outside a building, so the scenario reads differently in/out
    // (previously identical). `inside_or_outside` is prose for the template;
    // `interior` is the raw 1/0 for anyone who wants a boolean.
    AppendJsonMacro(out, first, "inside_or_outside", location->interior ? "inside" : "outside");
    AppendJsonMacro(out, first, "interior", location->interior ? "1" : "0");
    AppendJsonMacro(out, first, "time_of_day", BuildTimeOfDayMacro());

    // Scheduler clock (numeric): the raw GameDaysPassed / GameHour globals, so
    // chasm can total-order in-game time and fire time-triggered scheduled tasks.
    // The runtime heartbeat carries the same pair for firing while the player is
    // idle (not talking); these macros make it visible on the Gamestate page too.
    float gameDaysPassed = 0.0f;
    if (ReadGameDaysPassed(gameDaysPassed))
    {
        AppendJsonMacro(out, first, "game_days_passed", FormatClockFloat(gameDaysPassed));
    }
    float gameHour = 0.0f;
    if (ReadGlobalFloat(0x38, gameHour))
    {
        AppendJsonMacro(out, first, "game_hour", FormatClockFloat(gameHour));
    }

    // Scheduler travel: named map locations within ~1.2km, closest first. chasm
    // offers these as the travel action's destination candidates so the model
    // names a REAL place ("Prospector Saloon") the plugin can resolve to a marker.
    const std::string nearbyLocations = BuildNearbyLocationsMacro(player, 10);
    if (!nearbyLocations.empty())
    {
        AppendJsonMacro(out, first, "nearby_locations", nearbyLocations);
    }

    const int currentHealth = ReadPlayerActorValueInt(player, eActorVal_Health);
    // ActorValueOwner::Fn_08 is GetPermanentActorValue: the Pip-Boy's max HP.
    const int maxHealth = static_cast<int>(player->avOwner.Fn_08(eActorVal_Health) + 0.5f);
    if (maxHealth > 0)
    {
        AppendJsonMacro(out, first, "health", std::to_string(currentHealth) + "/" + std::to_string(maxHealth));
        int percent = static_cast<int>((100.0 * currentHealth / maxHealth) + 0.5);
        percent = (std::clamp)(percent, 0, 100);
        AppendJsonMacro(out, first, "health_percent", std::to_string(percent) + "%");
    }

    const int rads = ReadPlayerActorValueInt(player, eActorVal_RadLevel);
    if (rads >= 0)
    {
        AppendJsonMacro(out, first, "radiation", std::to_string(rads) + " rads");
    }

    AppendJsonMacro(out, first, "condition", BuildConditionMacro(player));
    AppendJsonMacro(out, first, "effects", BuildEffectsMacro(player));
    AppendJsonMacro(out, first, "special", BuildSpecialMacro(player));
    AppendJsonMacro(out, first, "skills", BuildSkillsMacro(player));
    AppendJsonMacro(out, first, "perks", BuildPerksMacro(player));
    AppendJsonMacro(out, first, "equipped_weapon", GetDisplayNameSafe(player->GetEquippedWeapon()));
    AppendJsonMacro(out, first, "equipped_apparel", BuildEquippedApparelMacro(player));
    if (player->baseForm && player->baseForm->typeID == kFormType_TESNPC)
    {
        auto* npc = static_cast<TESNPC*>(player->baseForm);
        AppendJsonMacro(out, first, "sex", npc->baseData.IsFemale() ? "female" : "male");
        if (npc->race.race)
        {
            AppendJsonMacro(out, first, "race", GetDisplayNameSafe(npc->race.race));
        }
        if (npc->hair)
        {
            AppendJsonMacro(out, first, "hair_style", GetDisplayNameSafe(npc->hair));
        }
        if (npc->eyes)
        {
            AppendJsonMacro(out, first, "eye_color", GetDisplayNameSafe(npc->eyes));
        }
        char hairHex[8]{};
        std::snprintf(hairHex, sizeof(hairHex), "#%02X%02X%02X",
            static_cast<unsigned>(npc->hairColor & 0xFF),
            static_cast<unsigned>((npc->hairColor >> 8) & 0xFF),
            static_cast<unsigned>((npc->hairColor >> 16) & 0xFF));
        AppendJsonMacro(out, first, "hair_color", hairHex);
    }
    AppendJsonMacro(out, first, "inventory", BuildInventoryMacro(player));

    std::string quests;
    std::string miscQuests;
    BuildQuestMacros(player, quests, miscQuests);
    AppendJsonMacro(out, first, "quests", quests);
    AppendJsonMacro(out, first, "misc_quests", miscQuests);

    out << "}";
    return out.str();
}

// --- Event-driven combat tracking (authoritative) --------------------------
// The engine fires NVSE `onstartcombat` the instant an actor enters combat and
// `oncombatend` when it truly ends. We record the (actor -> opponent) edge on
// start and erase it on end, so the relationship is held for the WHOLE fight --
// unaffected by the conversation package briefly clearing the IsInCombat flag or
// by GetCombatTarget going null mid-conversation. This is the primary signal;
// the polled/recency signals below are backups.
struct CombatEdge
{
    UInt32 targetRefId = 0;
    std::string targetName;
    DWORD startTick = 0;   // GetTickCount() when onstartcombat last (re)fired
};
std::unordered_map<UInt32, CombatEdge> g_combatEdges;  // actor refID -> current opponent
// NO artificial stickiness: an edge lives exactly from onstartcombat to
// oncombatend. Mid-fight robustness comes from the signals themselves -- the
// player's engine-sticky combat byte (0xDF0), the combat-controller targets, and
// onstartcombat re-firing throughout a real fight -- so when the engine says the
// fight is over, we read peaceful immediately.

// NVSE native event handler signature is void(TESObjectREFR* thisObj, void* params).
// For game events thisObj is null and params = { source, object } (verified against
// EventManager.cpp): source[0] is the actor entering combat, object[1] its target.
void CombatAlertOnStartCombat(TESObjectREFR*, void* parameters)
{
    auto** args = static_cast<void**>(parameters);
    if (!args)
    {
        return;
    }
    auto* actor = static_cast<TESObjectREFR*>(args[0]);
    auto* target = static_cast<TESForm*>(args[1]);
    if (!actor)
    {
        return;
    }
    CombatEdge& edge = g_combatEdges[actor->refID];
    edge.targetRefId = target ? target->refID : 0;
    edge.targetName = target ? GetDisplayNameSafe(target) : std::string();
    edge.startTick = GetTickCount();
    LogLine("Combat event: onstartcombat '%s' -> '%s'.",
        GetDisplayNameSafe(actor).c_str(),
        edge.targetName.empty() ? "?" : edge.targetName.c_str());
}

void CombatAlertOnCombatEnd(TESObjectREFR*, void* parameters)
{
    auto** args = static_cast<void**>(parameters);
    if (!args)
    {
        return;
    }
    auto* actor = static_cast<TESObjectREFR*>(args[0]);
    if (!actor)
    {
        return;
    }
    if (g_combatEdges.erase(actor->refID) > 0)
    {
        LogLine("Combat event: oncombatend '%s'.", GetDisplayNameSafe(actor).c_str());
    }
}

// Registers the combat event handlers once the event manager is available.
void RegisterCombatEventHandlers()
{
    static bool registered = false;
    if (registered || !g_eventManager)
    {
        return;
    }
    registered = true;
    const bool a = g_eventManager->SetNativeEventHandler("onstartcombat", CombatAlertOnStartCombat);
    const bool b = g_eventManager->SetNativeEventHandler("oncombatend", CombatAlertOnCombatEnd);
    LogLine("Combat event handlers registered (onstartcombat=%d oncombatend=%d).", a ? 1 : 0, b ? 1 : 0);
}

// Looks up an authoritative combat edge for `refId` (onstartcombat fired, no
// oncombatend yet). Fills the opponent when present.
bool HasCombatEdge(UInt32 refId, UInt32* outTargetRefId, std::string* outTargetName)
{
    auto it = g_combatEdges.find(refId);
    if (it == g_combatEdges.end())
    {
        return false;
    }
    if (outTargetRefId)
    {
        *outTargetRefId = it->second.targetRefId;
    }
    if (outTargetName)
    {
        *outTargetName = it->second.targetName;
    }
    return true;
}

// Wipes ALL combat tracking. Called from ResetRuntimeState on every load / new
// game / exit-to-menu: a save load resets the engine's combat WITHOUT firing
// `oncombatend`, so without this an actor that was fighting when you saved would
// stay "in combat" forever (stale edge) after the load. Genuine ongoing combat
// re-populates from the live IsInCombat() signal and the next `onstartcombat`.
void ClearCombatTracking(const char* reason)
{
    if (!g_combatEdges.empty())
    {
        LogLine("Combat tracking cleared (%s): %zu edge(s).",
            reason ? reason : "?", g_combatEdges.size());
    }
    g_combatEdges.clear();
}

// Per-turn combat awareness for the responding/mapped NPC. Detection uses ONLY
// the two documented Actor virtuals (IsInCombat / GetCombatTarget) from the
// xNVSE SDK -- no raw offset poking -- so it stays robust across game builds.
struct SpeakerCombatInfo
{
    bool inCombat = false;
    std::vector<std::string> combatWith;  // display names, deduped, primary first
};

// Detect whether `speakerRef` (the NPC being spoken to) is in combat and collect
// the display name(s) of who it is fighting. Returns { false, {} } for a
// non-actor / unloaded / peaceful ref, so callers that omit the fields leave
// non-combat turns byte-identical.
//
// IMPORTANT: talking to an NPC drops it into a conversation package, which often
// clears its OWN IsInCombat flag the instant the turn request is built — so
// relying on the speaker's flag alone misses the common "player attacks a
// friendly, then talks to them mid-fight" case. We therefore ALSO treat the
// speaker as in combat when the PLAYER is fighting and locked onto this speaker
// (or the speaker onto the player). The player's combat flag is stable through
// the conversation, so this is the reliable signal for a violent scene.
SpeakerCombatInfo DetectSpeakerCombat(TESObjectREFR* speakerRef, PlayerCharacter* player, const std::vector<NearbyNpcCandidate>& nearbyCandidates)
{
    SpeakerCombatInfo info;
    if (!speakerRef)
    {
        return info;
    }
    auto* speaker = static_cast<Actor*>(speakerRef);
    if (!speaker || !speaker->baseProcess)  // baseProcess gates "is a live, loaded actor"
    {
        return info;
    }

    // PlayerCharacter derives from Actor, so the same combat virtuals apply.
    auto* playerActor = player ? static_cast<Actor*>(player) : nullptr;
    Actor* speakerTarget = speaker->GetCombatTarget();
    Actor* playerTarget = playerActor ? playerActor->GetCombatTarget() : nullptr;
    const bool speakerInCombat = speaker->IsInCombat();
    const bool playerFlagInCombat = playerActor && playerActor->IsInCombat();
    // THE PLAYER'S OWN combat state. The player has dedicated combat tracking: a
    // byte at PlayerCharacter+0xDF0 the engine keeps set for the WHOLE encounter
    // (it drives the combat-music timeout), far stickier through a fight's lulls
    // than any per-actor flag. Within the PlayerCharacter object -> safe to read.
    const UInt8 playerCombatByte = player ? *(reinterpret_cast<const UInt8*>(player) + 0xDF0) : 0;
    const bool playerInCombat = playerFlagInCombat || playerCombatByte != 0;

    // AUTHORITATIVE, NON-FLICKERING: the combat CONTROLLER's target pointer. The
    // IsInCombat() flag byte toggles moment-to-moment during a real firefight
    // (target switch, LOS break) -- the log shows it dropping to 0 mid-fight --
    // but GetCombatTarget() stays set for the whole engagement. This is the same
    // data JIP LN NVSE's GetCombatTargets reads. Treat a live combat target as
    // authoritative: the speaker fighting ANYONE, or the player fighting the
    // speaker, is combat regardless of either "in combat" flag.
    const bool speakerHasTarget = speakerTarget != nullptr;       // speaker is fighting someone
    const bool playerTargetsSpeaker = playerTarget == speaker;    // player is fighting the speaker

    // LIVE mutual check (flag-gated, kept as one more corroborating signal).
    const bool fightingThePlayerNow =
        playerInCombat && (playerTarget == speaker || speakerTarget == playerActor);

    // AUTHORITATIVE: an onstartcombat edge for this speaker (or for the player
    // against this speaker) that hasn't been cleared by oncombatend. Held for the
    // whole fight regardless of the conversation package clearing the flag.
    std::string speakerEdgeTarget;
    const bool speakerHasEdge = HasCombatEdge(speakerRef->refID, nullptr, &speakerEdgeTarget);
    UInt32 playerEdgeTargetRef = 0;
    const bool playerHasEdge = playerActor && HasCombatEdge(playerActor->refID, &playerEdgeTargetRef, nullptr);
    const bool playerEdgeVsSpeaker = playerHasEdge && playerEdgeTargetRef == speakerRef->refID;

    // THE PLAYER'S ANGLE: is the PLAYER in combat, and is the speaker who with?
    // The player's combat state (0xDF0 byte) is engine-sticky through lulls; the
    // speaker counts as the opponent when either side targets the other or holds a
    // live onstartcombat edge. This makes "I'm in a fight with this NPC" hold even
    // when I look away to talk.
    const bool speakerIsPlayerOpponent = playerTargetsSpeaker || speakerTarget == playerActor
        || speakerHasEdge || playerEdgeVsSpeaker;
    const bool playerFightingSpeaker = playerInCombat && speakerIsPlayerOpponent;

    const bool inCombat = playerFightingSpeaker || speakerHasTarget || playerTargetsSpeaker
        || speakerInCombat || fightingThePlayerNow || speakerHasEdge || playerEdgeVsSpeaker;

    // Diagnostic: every candidate signal, so a single reproduction shows exactly
    // which one(s) fire (or none) for a given NPC. `player[...]` = the player's own
    // combat state; `target[...]` = the combat-controller target read.
    LogLine("Combat detect '%s': player[flag=%d byte=%d vsSelf=%d] target[self=%s playerVsSelf=%d] now[self=%d] edge[self=%d(%s) playerVsSelf=%d] -> %s",
        GetDisplayNameSafe(speakerRef).c_str(),
        playerFlagInCombat, playerCombatByte, playerFightingSpeaker,
        speakerHasTarget ? GetDisplayNameSafe(speakerTarget).c_str() : "-", playerTargetsSpeaker,
        speakerInCombat,
        speakerHasEdge, speakerEdgeTarget.empty() ? "-" : speakerEdgeTarget.c_str(), playerEdgeVsSpeaker,
        inCombat ? "IN COMBAT" : "peaceful");

    if (!inCombat)
    {
        return info;
    }
    info.inCombat = true;

    std::vector<std::string>& names = info.combatWith;
    const auto addName = [&names](const std::string& name) {
        if (name.empty())
        {
            return;
        }
        for (const auto& existing : names)
        {
            if (_stricmp(existing.c_str(), name.c_str()) == 0)
            {
                return;  // dedupe (case-insensitive)
            }
        }
        names.push_back(name);
    };

    // Live target: who the speaker is fighting this instant (may be null once the
    // conversation package has cleared it — the edge/player reads recover it).
    if (speakerTarget)
    {
        addName(GetDisplayNameSafe(speakerTarget));
    }
    // Authoritative opponent from the speaker's live onstartcombat edge.
    addName(speakerEdgeTarget);
    // The player, when the fight is between the player and this NPC. (addName
    // dedupes, so this is harmless if the speaker's own target already resolved
    // to the player's name.)
    if (player && (playerFightingSpeaker || playerTargetsSpeaker || speakerTarget == playerActor
        || playerEdgeVsSpeaker))
    {
        addName(GetDisplayNameSafe(player));
    }
    // Any nearby mapped actor in combat AND targeting the speaker -- the other
    // attackers in a multi-actor fight.
    for (const auto& candidate : nearbyCandidates)
    {
        if (!candidate.ref)
        {
            continue;
        }
        auto* other = static_cast<Actor*>(candidate.ref);
        if (!other || other == speaker || !other->baseProcess)
        {
            continue;
        }
        if (other->IsInCombat() && other->GetCombatTarget() == speaker)
        {
            addName(GetDisplayNameSafe(other));
        }
    }
    return info;
}

// Serialize combat info into JSON fields for the request metadata object. Empty
// string when NOT in combat, so the fields are omitted and non-combat requests
// are byte-identical to before this feature. Format (no braces, no outer comma):
//   "in_combat":true,"combat_with":["Raider","Powder Ganger"]
std::string BuildCombatMetadataFields(const SpeakerCombatInfo& info)
{
    if (!info.inCombat)
    {
        return {};
    }
    std::ostringstream out;
    out << "\"in_combat\":true,\"combat_with\":[";
    for (size_t index = 0; index < info.combatWith.size(); ++index)
    {
        if (index > 0)
        {
            out << ",";
        }
        out << JsonEscape(info.combatWith[index]);
    }
    out << "]";
    return out.str();
}

std::string BuildTextRequestMetadata(PlayerCharacter* player, const SpeakerSnapshot* preferredSpeaker = nullptr, const LocationSnapshot* location = nullptr)
{
    std::vector<NearbyNpcCandidate> nearbyCandidates = FindNearbyMappedNpcsForGroupChat(player, kGamestateNearbyRadiusMeters);
    std::string preferredVoiceTypeKey;
    std::string preferredVoiceTypeName;
    TESObjectREFR* speakerRef = (preferredSpeaker && preferredSpeaker->valid) ? ResolveSpeakerRef(*preferredSpeaker) : nullptr;
    // FALLBACK: a save load runs ResetRuntimeState, which clears pendingSpeaker --
    // but an ongoing conversation continues via lastNpcSpeaker (re-captured on
    // every NPC reply). Without this, every turn after a mid-conversation load
    // SILENTLY skipped combat detection (no speaker to inspect) even while the
    // NPC was actively fighting the player.
    if (!speakerRef && g_state.lastNpcSpeaker.valid)
    {
        speakerRef = ResolveSpeakerRef(g_state.lastNpcSpeaker);
        if (speakerRef)
        {
            LogLine("Combat detect: pendingSpeaker unresolved -> using lastNpcSpeaker '%s'.",
                GetDisplayNameSafe(speakerRef).c_str());
        }
    }
    if (!speakerRef)
    {
        // Make the skip VISIBLE -- a silent skip here cost hours of debugging.
        LogLine("Combat detect skipped: no speaker ref resolvable for this request.");
    }
    if (speakerRef)
    {
        const auto preferredVoiceType = ResolveRefVoiceTypeMetadata(speakerRef);
        preferredVoiceTypeKey = preferredVoiceType.first;
        preferredVoiceTypeName = preferredVoiceType.second;
    }

    // Combat awareness for the responding NPC. Built BEFORE the early returns
    // below so an in-combat NPC with no nearby mapped NPCs still reports it.
    const SpeakerCombatInfo combatInfo = DetectSpeakerCombat(speakerRef, player, nearbyCandidates);
    const std::string combatFields = BuildCombatMetadataFields(combatInfo);
    if (combatInfo.inCombat)
    {
        std::string joinedCombatants;
        for (size_t index = 0; index < combatInfo.combatWith.size(); ++index)
        {
            if (index > 0)
            {
                joinedCombatants += ", ";
            }
            joinedCombatants += combatInfo.combatWith[index];
        }
        LogLine("Combat detection: speaker '%s' IS IN COMBAT with [%s] -> sent to prompt.",
            speakerRef ? GetDisplayNameSafe(speakerRef).c_str() : "<unknown>",
            joinedCombatants.empty() ? "<unresolved>" : joinedCombatants.c_str());
    }
    // (The peaceful case is already logged per-signal inside DetectSpeakerCombat.)

    std::sort(nearbyCandidates.begin(), nearbyCandidates.end(), [](const NearbyNpcCandidate& left, const NearbyNpcCandidate& right) {
        if (left.underCrosshair != right.underCrosshair)
        {
            return left.underCrosshair && !right.underCrosshair;
        }
        if (left.distanceSquared != right.distanceSquared)
        {
            return left.distanceSquared < right.distanceSquared;
        }
        return _stricmp(left.npcName.c_str(), right.npcName.c_str()) < 0;
        });

    const std::string macrosJson = BuildPlayerMacros(player, location);

    if (nearbyCandidates.empty())
    {
        std::string inner = combatFields;  // "" unless the speaker is in combat
        if (!macrosJson.empty())
        {
            if (!inner.empty())
            {
                inner += ",";
            }
            inner += "\"macros\":" + macrosJson;
        }
        if (inner.empty())
        {
            return {};
        }
        return "{" + inner + "}";
    }

    auto focusIt = std::find_if(nearbyCandidates.begin(), nearbyCandidates.end(), [](const NearbyNpcCandidate& candidate) {
        return candidate.underCrosshair;
        });

    std::ostringstream out;
    out << "{";
    if (!combatFields.empty())
    {
        out << combatFields << ",";  // "in_combat":true,"combat_with":[...] then rest follows
    }
    if (!preferredVoiceTypeKey.empty())
    {
        out << "\"voice_type_key\":" << JsonEscape(preferredVoiceTypeKey);
        if (!preferredVoiceTypeName.empty())
        {
            out << ",\"voice_type_name\":" << JsonEscape(preferredVoiceTypeName);
        }
        out << ",";
    }
    out << "\"targeting\":{";
    if (focusIt != nearbyCandidates.end())
    {
        out << "\"focus_npc_key\":" << JsonEscape(focusIt->npcKey);
        out << ",\"focus_npc_name\":" << JsonEscape(focusIt->npcName);
        if (!focusIt->voiceTypeKey.empty())
        {
            out << ",\"focus_voice_type_key\":" << JsonEscape(focusIt->voiceTypeKey);
            if (!focusIt->voiceTypeName.empty())
            {
                out << ",\"focus_voice_type_name\":" << JsonEscape(focusIt->voiceTypeName);
            }
        }
        out << ",";
    }

    out << "\"nearby_npcs\":[";
    for (size_t index = 0; index < nearbyCandidates.size(); ++index)
    {
        if (index > 0)
        {
            out << ",";
        }

        const NearbyNpcCandidate& candidate = nearbyCandidates[index];
        const double distanceMeters = std::sqrt(candidate.distanceSquared) / kGameUnitsPerMeter;
        out << "{";
        out << "\"npc_key\":" << JsonEscape(candidate.npcKey);
        out << ",\"npc_name\":" << JsonEscape(candidate.npcName);
        out << ",\"ref_id\":" << (candidate.ref ? candidate.ref->refID : 0);
        out << ",\"pos_x\":" << std::fixed << std::setprecision(2) << (candidate.ref ? candidate.ref->posX : 0.0f);
        out << ",\"pos_y\":" << std::fixed << std::setprecision(2) << (candidate.ref ? candidate.ref->posY : 0.0f);
        out << ",\"pos_z\":" << std::fixed << std::setprecision(2) << (candidate.ref ? candidate.ref->posZ : 0.0f);
        if (!candidate.voiceTypeKey.empty())
        {
            out << ",\"voice_type_key\":" << JsonEscape(candidate.voiceTypeKey);
            if (!candidate.voiceTypeName.empty())
            {
                out << ",\"voice_type_name\":" << JsonEscape(candidate.voiceTypeName);
            }
        }
        out << ",\"distance_m\":" << std::fixed << std::setprecision(2) << distanceMeters;
        out << ",\"under_crosshair\":" << (candidate.underCrosshair ? "true" : "false");
        out << "}";
    }
    out << "]}";
    if (!macrosJson.empty())
    {
        out << ",\"macros\":" << macrosJson;
    }
    out << "}";
    return out.str();
}

std::string BuildAdminVoiceRequestMetadata(PlayerCharacter* player)
{
    std::string base = Trim(BuildTextRequestMetadata(player, nullptr));
    if (base.empty() || base == "{}")
    {
        base = "{";
    }
    else if (base.back() == '}')
    {
        base.pop_back();
        base += ",";
    }
    else
    {
        base = "{";
    }

    base += "\"admin\":true";
    base += ",\"adminMode\":true";
    base += ",\"voice_request\":true";
    base += ",\"input_mode\":\"admin\"";
    base += ",\"targetName\":";
    base += JsonEscape(kAdminNpcName);
    base += ",\"source\":\"fallout-new-vegas-native-admin-voice\"";
    base += "}";
    return base;
}

std::optional<ResolvedNpcTarget> FindFocusedMappedNpcForChat(PlayerCharacter* player)
{
    if (!player || !player->parentCell)
    {
        return std::nullopt;
    }

    TESObjectREFR* crosshairTarget = GetCrosshairRef();
    if (!crosshairTarget)
    {
        return std::nullopt;
    }

    const double maxDistanceSquared = std::pow(static_cast<double>(kChatNpcSearchRadiusMeters * kGameUnitsPerMeter), 2.0);
    ResolvedNpcTarget best{};
    ConsiderNearestMappedNpc(player, crosshairTarget, maxDistanceSquared, true, best);
    if (!best.ref)
    {
        return std::nullopt;
    }

    return best;
}

float SubtitleDuration(std::string_view text)
{
    const float seconds = 2.0f + static_cast<float>(text.size()) / 24.0f;
    return (std::min)((std::max)(seconds, kDefaultSubtitleSeconds), kMaxSubtitleSeconds);
}

std::string ActionNpcKey(const ResponsePayload& response)
{
    return response.actionNpcKey.empty() ? response.npcKey : response.actionNpcKey;
}

std::string ActionNpcName(const ResponsePayload& response)
{
    return response.actionNpcName.empty() ? response.npcName : response.actionNpcName;
}

SpeakerSnapshot ResolveActionSpeaker(const ResponsePayload& response)
{
    const std::string actionNpcKey = ActionNpcKey(response);
    const std::string actionNpcName = ActionNpcName(response);
    const bool hasExplicitActionTarget = !response.actionNpcKey.empty() || !response.actionNpcName.empty();

    if (!hasExplicitActionTarget && g_state.pendingSpeaker.refId)
    {
        return g_state.pendingSpeaker;
    }

    if (!actionNpcKey.empty() && actionNpcKey == g_state.lastNpcKey && g_state.lastNpcSpeaker.refId)
    {
        return g_state.lastNpcSpeaker;
    }

    if (const auto resolvedSpeaker = ResolveSpeakerSnapshotForNpc(actionNpcKey, actionNpcName); resolvedSpeaker.has_value())
    {
        return *resolvedSpeaker;
    }

    if (!hasExplicitActionTarget && g_state.pendingSpeaker.refId)
    {
        return g_state.pendingSpeaker;
    }

    return {};
}

Script* CompileTrustedExecutionScript(const ResponsePayload& response)
{
    if (!g_scriptInterface || response.executionScript.empty())
    {
        return nullptr;
    }

    const std::string baseKey = !response.executionTemplateId.empty()
        ? response.executionTemplateId
        : (!response.actionId.empty() ? response.actionId : response.gameMasterAction);
    const std::string cacheKey = baseKey + ":" + HashString64Hex(response.executionScript);
    const auto cached = g_trustedExecutionScripts.find(cacheKey);
    if (cached != g_trustedExecutionScripts.end())
    {
        return cached->second;
    }

    Script* script = g_scriptInterface->CompileScript(response.executionScript.c_str());
    if (!script)
    {
        LogLine("Failed to compile trusted Action Book script: action_id=%s template_id=%s.",
            response.actionId.c_str(),
            response.executionTemplateId.c_str());
        return nullptr;
    }

    g_trustedExecutionScripts[cacheKey] = script;
    LogLine("Compiled trusted Action Book script: action_id=%s template_id=%s hash=%s.",
        response.actionId.c_str(),
        response.executionTemplateId.c_str(),
        cacheKey.c_str());
    return script;
}

TESObjectREFR* ResolveTrustedExecutionRefArgument(const std::string& rawName, TESObjectREFR* actorRef, PlayerCharacter* player)
{
    const std::string name = ToLowerAscii(Trim(rawName));
    if (name == "actor" || name == "source" || name == "speaker" || name == "subject" || name == "npc" || name == "npc_ref")
    {
        return actorRef;
    }
    if (name == "player" || name == "target" || name == "target_actor" || name == "player_ref")
    {
        return player;
    }
    return nullptr;
}

TESObjectREFR* ResolveTrustedExecutionRefIdArgument(const std::string& rawValue)
{
    const auto formId = ParseTrustedFormId(rawValue);
    if (!formId.has_value())
    {
        return nullptr;
    }
    TESForm* form = LookupFormByID(*formId);
    if (!form)
    {
        return nullptr;
    }
    return DYNAMIC_CAST(form, TESForm, TESObjectREFR);
}

std::optional<UInt32> ParseTrustedFormId(const std::string& rawValue)
{
    std::string value = Trim(rawValue);
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    {
        value = value.substr(2);
    }
    if (value.empty() || value.size() > 8)
    {
        return std::nullopt;
    }
    for (const char ch : value)
    {
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
        {
            return std::nullopt;
        }
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 16);
    if (!end || *end != '\0')
    {
        return std::nullopt;
    }
    return static_cast<UInt32>(parsed);
}

std::optional<double> ParseTrustedNumber(const std::string& rawValue)
{
    const std::string value = Trim(rawValue);
    if (value.empty())
    {
        return std::nullopt;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0')
    {
        return std::nullopt;
    }
    return parsed;
}

std::optional<TrustedExecutionArgument> ResolveTrustedExecutionArgument(const std::string& rawName, TESObjectREFR* actorRef, PlayerCharacter* player)
{
    const std::string raw = Trim(rawName);
    const size_t separator = raw.find(':');
    if (separator != std::string::npos)
    {
        const std::string type = ToLowerAscii(Trim(raw.substr(0, separator)));
        const std::string value = Trim(raw.substr(separator + 1));
        if (type == "ref" || type == "reference")
        {
            TESObjectREFR* ref = ResolveTrustedExecutionRefArgument(value, actorRef, player);
            if (!ref)
            {
                ref = ResolveTrustedExecutionRefIdArgument(value);
            }
            if (!ref)
            {
                return std::nullopt;
            }
            TrustedExecutionArgument argument{};
            argument.type = TrustedExecutionArgumentType::Ref;
            argument.ref = ref;
            return argument;
        }
        if (type == "refid" || type == "reference_id" || type == "referenceid")
        {
            TESObjectREFR* ref = ResolveTrustedExecutionRefIdArgument(value);
            if (!ref)
            {
                return std::nullopt;
            }
            TrustedExecutionArgument argument{};
            argument.type = TrustedExecutionArgumentType::Ref;
            argument.ref = ref;
            return argument;
        }
        if (type == "form" || type == "formid" || type == "form_id")
        {
            const auto formId = ParseTrustedFormId(value);
            TESForm* form = formId.has_value() ? LookupFormByID(*formId) : nullptr;
            if (!form)
            {
                return std::nullopt;
            }
            TrustedExecutionArgument argument{};
            argument.type = TrustedExecutionArgumentType::Form;
            argument.form = form;
            return argument;
        }
        if (type == "number" || type == "float" || type == "int" || type == "integer")
        {
            const auto number = ParseTrustedNumber(value);
            if (!number.has_value())
            {
                return std::nullopt;
            }
            TrustedExecutionArgument argument{};
            argument.type = TrustedExecutionArgumentType::Number;
            argument.number = *number;
            return argument;
        }
        if (type == "string" || type == "str")
        {
            TrustedExecutionArgument argument{};
            argument.type = TrustedExecutionArgumentType::String;
            argument.text = value;
            return argument;
        }
    }

    TESObjectREFR* ref = ResolveTrustedExecutionRefArgument(raw, actorRef, player);
    if (!ref)
    {
        return std::nullopt;
    }
    TrustedExecutionArgument argument{};
    argument.type = TrustedExecutionArgumentType::Ref;
    argument.ref = ref;
    return argument;
}

using TrustedCallValue = std::variant<TESObjectREFR*, TESForm*, double, const char*>;

TrustedCallValue GetTrustedCallValue(const TrustedExecutionArgument& arg)
{
    switch (arg.type)
    {
    case TrustedExecutionArgumentType::Ref:
        return arg.ref;
    case TrustedExecutionArgumentType::Form:
        return arg.form;
    case TrustedExecutionArgumentType::Number:
        return arg.number;
    case TrustedExecutionArgumentType::String:
        return arg.text.c_str();
    default:
        return static_cast<TESObjectREFR*>(nullptr);
    }
}

bool CallTrustedExecutionScript(Script* script, TESObjectREFR* callingRef, const std::vector<TrustedExecutionArgument>& args, NVSEArrayVarInterface::Element& result)
{
    if (!script || !g_scriptInterface || !callingRef)
    {
        return false;
    }

    std::vector<TrustedCallValue> values;
    values.reserve(args.size());
    for (const TrustedExecutionArgument& arg : args)
    {
        values.push_back(GetTrustedCallValue(arg));
    }

    switch (values.size())
    {
    case 0:
        return g_scriptInterface->CallFunction(script, callingRef, nullptr, &result, 0);
    case 1:
        return std::visit([&](auto arg0) {
            return g_scriptInterface->CallFunction(script, callingRef, nullptr, &result, 1, arg0);
        }, values[0]);
    case 2:
        return std::visit([&](auto arg0, auto arg1) {
            return g_scriptInterface->CallFunction(script, callingRef, nullptr, &result, 2, arg0, arg1);
        }, values[0], values[1]);
    case 3:
        return std::visit([&](auto arg0, auto arg1, auto arg2) {
            return g_scriptInterface->CallFunction(script, callingRef, nullptr, &result, 3, arg0, arg1, arg2);
        }, values[0], values[1], values[2]);
    case 4:
        return std::visit([&](auto arg0, auto arg1, auto arg2, auto arg3) {
            return g_scriptInterface->CallFunction(script, callingRef, nullptr, &result, 4, arg0, arg1, arg2, arg3);
        }, values[0], values[1], values[2], values[3]);
    default:
        LogLine("Trusted Action Book script has too many arguments: %zu.", args.size());
        return false;
    }
}

bool TriggerTrustedActionBinding(const ResponsePayload& response)
{
    if (response.executionScript.empty())
    {
        return false;
    }
    if (ToLowerAscii(Trim(response.executionEngine)) != kTrustedFNVActionEngine)
    {
        LogLine("Ignoring trusted Action Book execution for unsupported engine: %s.", response.executionEngine.c_str());
        return false;
    }
    if (!response.executionLanguage.empty()
        && response.executionLanguage != "geck/xnvse"
        && response.executionLanguage != "xnvse"
        && response.executionLanguage != "geck")
    {
        LogLine("Ignoring trusted Action Book execution for unsupported language: %s.", response.executionLanguage.c_str());
        return false;
    }

    Script* script = CompileTrustedExecutionScript(response);
    if (!script)
    {
        return false;
    }

    const std::string actionNpcKey = ActionNpcKey(response);
    const std::string actionNpcName = ActionNpcName(response);

    // Music (task/music): DEFER the performance idle until this turn's spoken reply
    // (TTS) has finished. Otherwise the NPC yanks out a guitar / strikes a pose while
    // still speaking the "give me a minute" line. We stash the actor + which idle
    // (guitar for play-a-song, singing for rap) and let UpdateActiveSong() fire it
    // once the reply audio ends; the generated song is likewise held until speech ends.
    const bool isSongGuitar = response.actionId == "npc.play_song_guitar"
        || response.executionTemplateId.rfind("npc.play_song_guitar", 0) == 0;
    const bool isSongRap = response.actionId == "npc.play_song_rap"
        || response.executionTemplateId.rfind("npc.play_song_rap", 0) == 0;
    if (isSongGuitar || isSongRap)
    {
        const SpeakerSnapshot snap = ResolveActionSpeaker(response);
        if (snap.valid)
        {
            g_state.pendingGuitar = true;
            g_state.pendingGuitarSpeaker = snap;
            g_state.performIsRap = isSongRap;
            // Root the performer in place RIGHT NOW (the game_master action just
            // dropped the conversation hold, so without this they'd wander off
            // during the acceptance line before the performance even starts).
            if (TESObjectREFR* ref = ResolveSpeakerRef(snap))
            {
                SetActorRestrainedState(ref, true);
            }
            LogLine("Music: %s deferred until turn speech ends (npc=%s).",
                isSongRap ? "rap" : "guitar", actionNpcName.c_str());
            return true; // handled — do NOT play the idle now
        }
    }

    TESObjectREFR* actorRef = ResolveSpeakerRef(ResolveActionSpeaker(response));
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        LogLine("Could not resolve player for trusted Action Book action: npc=%s action_id=%s template_id=%s.",
            actionNpcName.c_str(),
            response.actionId.c_str(),
            response.executionTemplateId.c_str());
        return false;
    }

    std::vector<TrustedExecutionArgument> trustedArgs;
    for (const std::string& argName : response.executionArguments)
    {
        const auto resolvedArg = ResolveTrustedExecutionArgument(argName, actorRef, player);
        if (!resolvedArg.has_value())
        {
            LogLine("Could not resolve trusted Action Book argument '%s' for action_id=%s template_id=%s.",
                argName.c_str(),
                response.actionId.c_str(),
                response.executionTemplateId.c_str());
            return false;
        }
        trustedArgs.push_back(*resolvedArg);
    }

    TESObjectREFR* callingRef = actorRef ? actorRef : static_cast<TESObjectREFR*>(player);
    NVSEArrayVarInterface::Element result;
    const bool callOk = CallTrustedExecutionScript(script, callingRef, trustedArgs, result);
    const bool issued = callOk
        && (result.GetType() != NVSEArrayVarInterface::Element::kType_Numeric || result.GetNumber() != 0.0);

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "trusted_action_book_execution",
            {
                { "npc_key", actionNpcKey },
                { "npc_name", actionNpcName },
                { "action_id", response.actionId },
                { "action_book_id", response.actionBookId },
                { "template_id", response.executionTemplateId },
                { "engine", response.executionEngine },
                { "language", response.executionLanguage },
            },
            {
                { "speaker_ref_id", actorRef ? static_cast<double>(actorRef->refID) : 0.0 },
                { "arg_count", static_cast<double>(trustedArgs.size()) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", callOk },
                { "issued", issued },
            });
    }

    if (!issued)
    {
        LogLine("Trusted Action Book execution did not issue: action_id=%s template_id=%s call_ok=%d result_type=%d.",
            response.actionId.c_str(),
            response.executionTemplateId.c_str(),
            callOk ? 1 : 0,
            callOk ? static_cast<int>(result.GetType()) : 0);
        return false;
    }

    if (actorRef)
    {
        RememberNpcTarget(actionNpcKey, actionNpcName, CaptureSpeakerSnapshot(actorRef));
    }
    if (!response.requestId.empty())
    {
        g_state.movementActionRequestIds.insert(response.requestId);
    }

    const std::string label = !response.actionId.empty() ? response.actionId : response.executionTemplateId;
    ShowHudMessage((actionNpcName.empty() ? std::string("NPC") : actionNpcName) + " executed " + (label.empty() ? std::string("Action Book action") : label) + ".");
    LogLine("Triggered trusted Action Book execution for %s: action_id=%s template_id=%s.",
        actionNpcName.c_str(),
        response.actionId.c_str(),
        response.executionTemplateId.c_str());
    return true;
}

bool TriggerNpcAttack(const ResponsePayload& response)
{
    if (ToUpperAscii(Trim(response.gameMasterAction)) != "ATTACK")
    {
        return false;
    }

    if (!EnsureStartCombatScript())
    {
        return false;
    }

    const std::string actionNpcName = ActionNpcName(response);
    TESObjectREFR* actorRef = ResolveSpeakerRef(ResolveActionSpeaker(response));
    PlayerCharacter* player = GetPlayer();
    if (!actorRef || !player)
    {
        LogLine("Could not resolve actor/player for ATTACK action: npc=%s action=%s", actionNpcName.c_str(), response.gameMasterAction.c_str());
        return false;
    }

    // Already fighting? Then ATTACK is a no-op. Without this guard the combat
    // prompt made the model dramatically pick `attack` every in-combat turn, and
    // the fresh StartCombat call re-kindled/extended the fight each time -- a
    // feedback loop (combat -> attack action -> more combat) that also re-aggroed
    // NPCs who were disengaging. Attacking someone you're already attacking means
    // nothing; the action stays meaningful for STARTING a fight from peace.
    {
        auto* actor = static_cast<Actor*>(actorRef);
        const bool alreadyFighting = (actor && actor->baseProcess
            && (actor->IsInCombat() || actor->GetCombatTarget() != nullptr))
            || HasCombatEdge(actorRef->refID, nullptr, nullptr);
        if (alreadyFighting)
        {
            LogLine("ATTACK action skipped for %s: already in combat (no re-StartCombat).", actionNpcName.c_str());
            return true;  // treated as handled -- the NPC keeps fighting as-is
        }
    }

    if (!g_scriptInterface->CallFunctionAlt(g_startCombatScript, actorRef, 2, actorRef, player))
    {
        LogLine("CallFunctionAlt failed for ATTACK action on %s.", actionNpcName.c_str());
        return false;
    }

    LogLine("Triggered StartCombat for %s via gamemaster ATTACK.", actionNpcName.c_str());
    return true;
}

bool TriggerNpcFollow(const ResponsePayload& response)
{
    if (ToUpperAscii(Trim(response.gameMasterAction)) != "FOLLOW")
    {
        return false;
    }

    const std::string actionNpcKey = ActionNpcKey(response);
    const std::string actionNpcName = ActionNpcName(response);
    TESObjectREFR* actorRef = ResolveSpeakerRef(ResolveActionSpeaker(response));
    PlayerCharacter* player = GetPlayer();
    TESForm* followPackage = ResolveDefaultFollowPackage();
    if (!actorRef || !player || !followPackage)
    {
        LogLine("Could not resolve actor/player/follow package for FOLLOW action: npc=%s action=%s", actionNpcName.c_str(), response.gameMasterAction.c_str());
        return false;
    }

    const bool teammateIssued = SetActorPlayerTeammate(actorRef);
    const bool packageIssued = AddActorScriptPackage(actorRef, followPackage, kDefaultFollowPackageEditorId, "game_master_follow_package");
    if (!packageIssued)
    {
        LogLine("Failed to apply follow package for %s.", actionNpcName.c_str());
        return false;
    }

    const SpeakerSnapshot snapshot = CaptureSpeakerSnapshot(actorRef);
    RememberNpcTarget(actionNpcKey, actionNpcName, snapshot);
    if (!response.requestId.empty())
    {
        g_state.movementActionRequestIds.insert(response.requestId);
    }

    if (!g_state.traceRequestId.empty())
    {
            TraceRequestEvent(g_state.traceRequestId, "game_master_follow_triggered",
            {
                { "npc_key", actionNpcKey },
                { "npc_name", actionNpcName },
                { "package_editor_id", kDefaultFollowPackageEditorId },
            },
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "player_ref_id", static_cast<double>(player->refID) },
                { "package_ref_id", static_cast<double>(followPackage->refID) },
            },
            {
                { "teammate_issued", teammateIssued },
                { "package_issued", packageIssued },
            });
    }

    ShowHudMessage((actionNpcName.empty() ? std::string("NPC") : actionNpcName) + " is following.");
    LogLine("Triggered follow package %s for %s via gamemaster FOLLOW.", kDefaultFollowPackageEditorId, actionNpcName.c_str());
    return true;
}

bool TriggerNpcStopFollow(const ResponsePayload& response)
{
    if (ToUpperAscii(Trim(response.gameMasterAction)) != "STOP_FOLLOW")
    {
        return false;
    }

    const std::string actionNpcKey = ActionNpcKey(response);
    const std::string actionNpcName = ActionNpcName(response);
    TESObjectREFR* actorRef = ResolveSpeakerRef(ResolveActionSpeaker(response));
    PlayerCharacter* player = GetPlayer();
    if (!actorRef)
    {
        LogLine("Could not resolve actor for STOP_FOLLOW action: npc=%s action=%s", actionNpcName.c_str(), response.gameMasterAction.c_str());
        return false;
    }

    TESForm* followPackage = ResolveDefaultFollowPackage();
    bool packageCurrentKnown = false;
    const bool packageCurrent = followPackage ? IsActorUsingPackage(actorRef, followPackage, &packageCurrentKnown) : false;
    const bool packageRemoved = followPackage && packageCurrentKnown && packageCurrent
        ? RemoveActorScriptPackage(actorRef, "game_master_stop_follow_package")
        : false;
    const bool teammateCleared = SetActorPlayerTeammate(actorRef, false, "game_master_stop_follow_teammate");

    const SpeakerSnapshot snapshot = CaptureSpeakerSnapshot(actorRef);
    RememberNpcTarget(actionNpcKey, actionNpcName, snapshot);
    if (!response.requestId.empty())
    {
        g_state.movementActionRequestIds.insert(response.requestId);
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "game_master_stop_follow_triggered",
            {
                { "npc_key", actionNpcKey },
                { "npc_name", actionNpcName },
                { "package_editor_id", kDefaultFollowPackageEditorId },
            },
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "player_ref_id", player ? static_cast<double>(player->refID) : 0.0 },
                { "package_ref_id", followPackage ? static_cast<double>(followPackage->refID) : 0.0 },
            },
            {
                { "package_current_known", packageCurrentKnown },
                { "package_current", packageCurrent },
                { "package_removed", packageRemoved },
                { "teammate_cleared", teammateCleared },
            });
    }

    if (!packageRemoved && !teammateCleared)
    {
        LogLine("STOP_FOLLOW action for %s resolved actor but did not remove package or clear teammate state.", actionNpcName.c_str());
        return false;
    }

    ShowHudMessage((actionNpcName.empty() ? std::string("NPC") : actionNpcName) + " stopped following.");
    LogLine("Triggered stop follow for %s via gamemaster STOP_FOLLOW (package_removed=%d teammate_cleared=%d).", actionNpcName.c_str(), packageRemoved ? 1 : 0, teammateCleared ? 1 : 0);
    return true;
}

bool TriggerGameMasterAction(const ResponsePayload& response, std::string* outTriggeredAction = nullptr)
{
    if (!response.isFinal || !response.ok || !response.gameMasterShouldTrigger)
    {
        return false;
    }

    const std::string action = ToUpperAscii(Trim(response.gameMasterAction));
    if (action.empty() || action == "NONE")
    {
        return false;
    }

    // Only DROP the conversation hold for actions that actually move the NPC
    // (follow, attack, go sit/wait/sandbox, spawn, etc.). Non-locomotion actions —
    // gestures and the song/rap performances — should KEEP the NPC held in the
    // conversation instead of returning them to a wandering state. This is the
    // general "the moment they do an action they wander off" fix; performances also
    // SetRestrained on top for a hard hold.
    const bool keepsHold = response.actionId.rfind("npc.gesture_", 0) == 0
        || response.actionId.rfind("npc.play_song", 0) == 0;
    if (!keepsHold)
    {
        ReleaseConversationHold("game_master_action");
    }

    if (!response.executionScript.empty())
    {
        const bool triggered = TriggerTrustedActionBinding(response);
        if (triggered)
        {
            if (outTriggeredAction)
            {
                *outTriggeredAction = response.actionId.empty() ? action : response.actionId;
            }
            return true;
        }
        if (action == "ACTION_BOOK")
        {
            return false;
        }
        LogLine("Trusted Action Book execution failed for %s; trying legacy native action %s.",
            response.actionId.c_str(),
            action.c_str());
    }

    if (action == "ATTACK")
    {
        const bool triggered = TriggerNpcAttack(response);
        if (triggered && outTriggeredAction)
        {
            *outTriggeredAction = action;
        }
        return triggered;
    }

    if (action == "FOLLOW")
    {
        const bool triggered = TriggerNpcFollow(response);
        if (triggered && outTriggeredAction)
        {
            *outTriggeredAction = action;
        }
        return triggered;
    }

    if (action == "STOP_FOLLOW")
    {
        const bool triggered = TriggerNpcStopFollow(response);
        if (triggered && outTriggeredAction)
        {
            *outTriggeredAction = action;
        }
        return triggered;
    }

    if (action == "ACTION_BOOK")
    {
        LogLine("Action Book command %s did not include executable trusted metadata.", response.actionId.c_str());
        return false;
    }

    LogLine("No native handler is implemented for gamemaster action %s on %s.", action.c_str(), response.npcName.c_str());
    return false;
}

std::string BuildSubtitleMessage(const std::string& speaker, const std::string& text)
{
    const std::string cleanText = Trim(text);
    if (cleanText.empty())
    {
        return "";
    }
    return cleanText;
}

void ShowGeneralSubtitle(const std::string& speaker, const std::string& text, float seconds)
{
    const std::string message = BuildSubtitleMessage(speaker, text);
    if (message.empty())
    {
        return;
    }

    LogLine("Subtitle: %s", message.c_str());
}

TileMenu* GetTileMenuByTypeLocal(UInt32 menuType)
{
    auto* menuArray = reinterpret_cast<NiTArray<TileMenu*>*>(kTileMenuArrayAddress);
    if (!menuArray || menuType < kMenuType_Min || menuType > kMenuType_Max)
    {
        return nullptr;
    }

    return menuArray->Get(menuType - kMenuType_Min);
}

Menu* GetMenuByTypeLocal(UInt32 menuType)
{
    TileMenu* tileMenu = GetTileMenuByTypeLocal(menuType);
    return tileMenu ? tileMenu->menu : nullptr;
}

Tile::Value* GetTileValueByIdLocal(Tile* tile, UInt32 valueId)
{
    if (!tile)
    {
        return nullptr;
    }

    UInt32 left = 0;
    UInt32 right = tile->values.size;
    while (left < right)
    {
        const UInt32 mid = left + ((right - left) / 2);
        Tile::Value* value = tile->values[mid];
        if (!value)
        {
            return nullptr;
        }

        if (value->id == valueId)
        {
            return value;
        }

        if (value->id < valueId)
        {
            left = mid + 1;
        }
        else
        {
            right = mid;
        }
    }

    return nullptr;
}

Tile::Value* GetTileValueByNameLocal(Tile* tile, const char* valueName)
{
    if (!tile || !valueName || !g_traitNameToId)
    {
        return nullptr;
    }

    return GetTileValueByIdLocal(tile, g_traitNameToId(valueName));
}

Tile* GetChildTileLocal(Tile* parentTile, const char* childName)
{
    if (!parentTile || !childName || !*childName)
    {
        return nullptr;
    }

    int childIndex = 0;
    char* colon = std::strchr(const_cast<char*>(childName), ':');
    if (colon)
    {
        if (colon == childName)
        {
            return nullptr;
        }
        *colon = 0;
        childIndex = std::atoi(colon + 1);
    }

    Tile* result = nullptr;
    const bool wildcard = *childName == '*';
    for (tList<Tile::ChildNode>::Iterator iter = parentTile->childList.Begin(); !iter.End(); ++iter)
    {
        if (*iter && iter->child && (wildcard || _stricmp(iter->child->name.m_data, childName) == 0) && !childIndex--)
        {
            result = iter->child;
            break;
        }
    }

    if (colon)
    {
        *colon = ':';
    }
    return result;
}

Tile::Value* GetTileComponentValueLocal(Tile* rootTile, const char* componentPath)
{
    if (!rootTile || !componentPath || !*componentPath)
    {
        return nullptr;
    }

    std::string mutablePath(componentPath);
    Tile* currentTile = rootTile;
    const char* remaining = mutablePath.c_str();
    char* slash = nullptr;
    while ((slash = std::strpbrk(const_cast<char*>(remaining), "/\\")) != nullptr)
    {
        *slash = 0;
        currentTile = GetChildTileLocal(currentTile, remaining);
        if (!currentTile)
        {
            return nullptr;
        }
        remaining = slash + 1;
    }

    return *remaining ? GetTileValueByNameLocal(currentTile, remaining) : nullptr;
}

bool SetMenuTileString(Menu* menu, const char* componentPath, const std::string& value)
{
    if (!menu || !menu->tile || !componentPath)
    {
        return false;
    }

    Tile::Value* tileValue = GetTileComponentValueLocal(menu->tile, componentPath);
    if (!tileValue || !tileValue->parent)
    {
        return false;
    }

    CALL_MEMBER_FN(tileValue->parent, SetStringValue)(tileValue->id, value.c_str(), true);
    return true;
}

bool SetMenuTileFloat(Menu* menu, const char* componentPath, float value)
{
    if (!menu || !menu->tile || !componentPath)
    {
        return false;
    }

    Tile::Value* tileValue = GetTileComponentValueLocal(menu->tile, componentPath);
    if (!tileValue || !tileValue->parent)
    {
        return false;
    }

    CALL_MEMBER_FN(tileValue->parent, SetFloatValue)(tileValue->id, value, true);
    return true;
}

bool SetTileTraitString(Tile* tile, const char* traitName, const std::string& value)
{
    if (!tile || !traitName || !*traitName)
    {
        return false;
    }

    Tile::Value* tileValue = GetTileValueByNameLocal(tile, traitName);
    if (!tileValue || !tileValue->parent)
    {
        return false;
    }

    CALL_MEMBER_FN(tileValue->parent, SetStringValue)(tileValue->id, value.c_str(), true);
    return true;
}

bool SetTileTraitFloat(Tile* tile, const char* traitName, float value)
{
    if (!tile || !traitName || !*traitName)
    {
        return false;
    }

    Tile::Value* tileValue = GetTileValueByNameLocal(tile, traitName);
    if (!tileValue || !tileValue->parent)
    {
        return false;
    }

    CALL_MEMBER_FN(tileValue->parent, SetFloatValue)(tileValue->id, value, true);
    return true;
}

std::string DescribeTilePath(Tile* tile)
{
    std::vector<std::string> parts;
    while (tile)
    {
        parts.push_back(tile->name.m_data ? tile->name.m_data : "<unnamed>");
        tile = tile->parent;
    }

    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
    {
        if (!path.empty())
        {
            path += "/";
        }
        path += *it;
    }
    return path;
}

int ScoreSubtitleTileCandidate(Tile* tile)
{
    if (!tile || !tile->name.m_data)
    {
        return 0;
    }

    std::string lowerName = ToLowerAscii(tile->name.m_data);
    int score = 0;
    if (lowerName.find("subtitle") != std::string::npos)
    {
        score += 100;
    }
    if (lowerName.find("text") != std::string::npos)
    {
        score += 50;
    }
    if (lowerName.find("message") != std::string::npos)
    {
        score += 25;
    }
    if (lowerName.find("activator") != std::string::npos)
    {
        score -= 100;
    }
    return score;
}

Tile* FindBestSubtitleTextTile(Tile* root, int* bestScore = nullptr)
{
    if (!root)
    {
        if (bestScore)
        {
            *bestScore = 0;
        }
        return nullptr;
    }

    Tile* bestTile = nullptr;
    int localBestScore = 0;
    if (GetTileValueByNameLocal(root, "string"))
    {
        localBestScore = 1 + ScoreSubtitleTileCandidate(root);
        bestTile = root;
    }

    for (tList<Tile::ChildNode>::Iterator iter = root->childList.Begin(); !iter.End(); ++iter)
    {
        if (!*iter || !iter->child)
        {
            continue;
        }

        int childScore = 0;
        Tile* childBest = FindBestSubtitleTextTile(iter->child, &childScore);
        if (childBest && childScore > localBestScore)
        {
            localBestScore = childScore;
            bestTile = childBest;
        }
    }

    if (bestScore)
    {
        *bestScore = localBestScore;
    }
    return bestTile;
}

void ClearDescendantSubtitleStrings(Tile* root)
{
    if (!root)
    {
        return;
    }

    SetTileTraitString(root, "string", "");
    SetTileTraitFloat(root, "visible", 0.0f);
    SetTileTraitFloat(root, "alpha", 0.0f);
    for (tList<Tile::ChildNode>::Iterator iter = root->childList.Begin(); !iter.End(); ++iter)
    {
        if (*iter && iter->child)
        {
            ClearDescendantSubtitleStrings(iter->child);
        }
    }
}

void MakeTileChainVisible(Tile* tile)
{
    while (tile)
    {
        SetTileTraitFloat(tile, "visible", 1.0f);
        SetTileTraitFloat(tile, "alpha", 255.0f);
        tile = tile->parent;
    }
}

bool SetAnyMenuTileString(Menu* menu, std::initializer_list<const char*> componentPaths, const std::string& value, const char** matchedPath = nullptr)
{
    if (matchedPath)
    {
        *matchedPath = nullptr;
    }
    for (const char* componentPath : componentPaths)
    {
        if (!componentPath || !*componentPath)
        {
            continue;
        }

        if (SetMenuTileString(menu, componentPath, value))
        {
            if (matchedPath)
            {
                *matchedPath = componentPath;
            }
            return true;
        }
    }

    return false;
}

bool SetAnyMenuTileFloat(Menu* menu, std::initializer_list<const char*> componentPaths, float value, const char** matchedPath = nullptr)
{
    if (matchedPath)
    {
        *matchedPath = nullptr;
    }
    for (const char* componentPath : componentPaths)
    {
        if (!componentPath || !*componentPath)
        {
            continue;
        }

        if (SetMenuTileFloat(menu, componentPath, value))
        {
            if (matchedPath)
            {
                *matchedPath = componentPath;
            }
            return true;
        }
    }

    return false;
}

void ClearDialogSubtitle()
{
    if (Menu* hudMenu = GetMenuByTypeLocal(kMenuType_HUDMain))
    {
        if (Tile* subtitlesRoot = GetChildTileLocal(hudMenu->tile, "Subtitles"))
        {
            ClearDescendantSubtitleStrings(subtitlesRoot);
        }
        SetAnyMenuTileString(hudMenu,
            {
                "Info/justify_center_text/string",
                "Info/justify_center_hotrect/justify_center_text/string",
            },
            "");
        SetAnyMenuTileFloat(hudMenu,
            {
                "Info/justify_center_text/visible",
                "Info/justify_center_hotrect/justify_center_text/visible",
                "Info/justify_center_hotrect/visible",
            },
            0.0f);
        SetAnyMenuTileFloat(hudMenu,
            {
                "Info/justify_center_hotrect/alpha",
            },
            0.0f);
    }
    g_state.dialogSubtitleActive = false;
    g_state.dialogSubtitleHideTick = 0;
}

void ClearOutboxArtifacts(const char* reason)
{
    std::error_code ec;
    bool removedAny = false;

    if (fs::remove(OutboxPath(), ec))
    {
        removedAny = true;
    }
    ec.clear();

    if (fs::exists(OutboxChunkDir(), ec))
    {
        for (const auto& entry : fs::directory_iterator(OutboxChunkDir(), ec))
        {
            std::error_code removeEc;
            if (entry.is_regular_file(removeEc) && fs::remove(entry.path(), removeEc))
            {
                removedAny = true;
            }
        }
    }

    if (removedAny)
    {
        LogLine("Cleared bridge outbox artifacts (%s).", reason ? reason : "cleanup");
    }
}

bool HasQueuedOrPlayingReply()
{
    return g_state.awaitingReply
        || !g_state.pendingAudioChunks.empty()
        || !g_state.activeSounds.empty()
        || g_state.streamActive // Phase 3: the single streaming buffer counts as playing
        || (g_state.activeSpeechUntilTick && GetTickCount() < g_state.activeSpeechUntilTick)
        || HasPendingChunkFiles();
}

void ClearIdleOutboxArtifacts(const char* reason)
{
    if (g_state.awaitingReply
        || !g_state.pendingAudioChunks.empty()
        || !g_state.activeSounds.empty()
        || (g_state.activeSpeechUntilTick && GetTickCount() < g_state.activeSpeechUntilTick))
    {
        return;
    }

    std::error_code ec;
    if (fs::exists(OutboxPath(), ec) || HasPendingChunkFiles())
    {
        ClearOutboxArtifacts(reason ? reason : "idle_stale_response");
    }
}

void InterruptBridgeReplyAndPlayback(const char* reason)
{
    // Any interrupt (push-to-talk, a new turn, teardown) also ends a guitar
    // performance — otherwise the NPC keeps strumming while you talk over them.
    // Runs before the reply-state gate below because a guitar-out-awaiting-song
    // phase has no reply audio and would otherwise skip cleanup.
    StopGuitarPerformance(reason ? reason : "reply_interrupted");

    // Match the gate (HasQueuedOrPlayingReply) exactly. The old guard omitted
    // `streamActive`, so when a streaming reply had finished generating
    // (awaitingReply already false) but was still audibly playing out the stream
    // buffer, callers (e.g. STT push-to-talk) saw "playing" via the gate yet this
    // function early-returned and never called StopStreamingVoice — the line kept
    // playing until the next reply's first chunk replaced it (i.e. after release).
    const bool hadReplyState = HasQueuedOrPlayingReply();
    if (!hadReplyState)
    {
        return;
    }

    ClearOutboxArtifacts(reason ? reason : "reply_interrupted");
    // Abandon any in-flight HTTP turn so the worker's remaining output is discarded.
    CancelHttpTurn();

    StopSpeechAnimation();
    ClearDialogSubtitle();

    for (auto& sound : g_state.activeSounds)
    {
        if (sound.buffer)
        {
            sound.buffer->Stop();
            sound.buffer->Release();
            sound.buffer = nullptr;
        }
        if (sound.buffer3d)
        {
            sound.buffer3d->Release();
            sound.buffer3d = nullptr;
        }
    }
    g_state.activeSounds.clear();
    ShutdownDirectSound();

    ReleaseConversationHold(reason ? reason : "reply_interrupted");
    g_state.awaitingReply = false;
    g_state.awaitingVoiceReply = false;
    g_state.replyStartedTick = 0;
    g_state.lastBridgeActivityTick = 0;
    g_state.sawBridgeActivity = false;
    g_state.activeRequestId.clear();
    g_state.lastAudioChunkIndex = -1;
    g_state.subtitleShownForReply = false;
    g_state.activeSpeechUntilTick = 0;
    g_state.replySubtitleText.clear();
    g_state.streamedAudioSeenForReply = false;
    g_state.pendingAudioChunks.clear();
    StopStreamingVoice("reply_state_reset");
    WriteRuntimeHeartbeatIfNeeded(true);
    LogLine("Interrupted bridge reply playback (%s).", reason ? reason : "reply_interrupted");
}

bool ShowDialogSubtitle(const std::string& speaker, const std::string& text, float seconds)
{
    ShowGeneralSubtitle(speaker, text, seconds);

    const std::string message = BuildSubtitleMessage(speaker, text);
    if (message.empty())
    {
        return false;
    }

    ClearDialogSubtitle();

    Menu* hudMenu = GetMenuByTypeLocal(kMenuType_HUDMain);
    if (!hudMenu)
    {
        LogLine("HUDMainMenu unavailable while showing general subtitle.");
        return false;
    }

    if (Tile* subtitlesRoot = GetChildTileLocal(hudMenu->tile, "Subtitles"))
    {
        int subtitleScore = 0;
        Tile* subtitleTile = FindBestSubtitleTextTile(subtitlesRoot, &subtitleScore);
        if (subtitleTile && SetTileTraitString(subtitleTile, "string", message))
        {
            MakeTileChainVisible(subtitleTile);
            g_state.dialogSubtitleActive = true;
            const DWORD durationMs = static_cast<DWORD>((std::max)(0.1f, seconds) * 1000.0f);
            g_state.dialogSubtitleHideTick = GetTickCount() + durationMs;
            LogLine("Displayed general subtitle via HUD Subtitles tile %s for %.2fs.",
                DescribeTilePath(subtitleTile).c_str(),
                seconds);
            return true;
        }
    }

    const char* matchedTextPath = nullptr;
    const bool textSet = SetAnyMenuTileString(hudMenu,
        {
            "Info/justify_center_text/string",
            "Info/justify_center_hotrect/justify_center_text/string",
        },
        message,
        &matchedTextPath);
    const bool visibleSet = SetAnyMenuTileFloat(hudMenu,
        {
            "Info/justify_center_text/visible",
            "Info/justify_center_hotrect/justify_center_text/visible",
            "Info/justify_center_hotrect/visible",
        },
        1.0f);
    const bool alphaSet = SetAnyMenuTileFloat(hudMenu,
        {
            "Info/justify_center_hotrect/alpha",
        },
        255.0f);
    if (!textSet)
    {
        LogLine("Failed to set HUD centered subtitle text on any known path.");
        return false;
    }

    g_state.dialogSubtitleActive = true;
    const DWORD durationMs = static_cast<DWORD>((std::max)(0.1f, seconds) * 1000.0f);
    g_state.dialogSubtitleHideTick = GetTickCount() + durationMs;
    LogLine("Displayed HUD centered subtitle via path %s for %.2fs.",
        matchedTextPath ? matchedTextPath : "<unknown>",
        seconds);
    if (!visibleSet && !alphaSet)
    {
        LogLine("HUD centered subtitle text set, but no known visible/alpha trait was found.");
    }
    return true;
}

void ShowHudMessage(const std::string& message)
{
    if (message.empty())
    {
        return;
    }

    const std::string uiMessage = ToUiAscii(message);
    if (g_queueUiMessage && !uiMessage.empty())
    {
        g_queueUiMessage(uiMessage.c_str(), 0, nullptr, nullptr, 2.5f, false);
    }
    LogLine("HUD: %s", message.c_str());
}

void ShowRecognizedPlayerSubtitleIfNeeded(const ResponsePayload& response)
{
    // Player-origin text is retained in diagnostics/responses, but the in-game HUD
    // should only subtitle NPC/Todd replies.
    (void)response;
}

std::string ExtractJsonStringField(const std::string& text, const char* fieldName)
{
    if (!fieldName || !*fieldName)
    {
        return "";
    }

    const std::string needle = std::string("\"") + fieldName + "\"";
    const size_t keyPos = text.find(needle);
    if (keyPos == std::string::npos)
    {
        return "";
    }

    size_t colonPos = text.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos)
    {
        return "";
    }

    size_t valueStart = text.find('"', colonPos + 1);
    if (valueStart == std::string::npos)
    {
        return "";
    }

    std::string result;
    bool escaped = false;
    for (size_t index = valueStart + 1; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (escaped)
        {
            switch (ch)
            {
            case '\\':
            case '"':
            case '/':
                result.push_back(ch);
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(ch);
                break;
            }
            escaped = false;
            continue;
        }

        if (ch == '\\')
        {
            escaped = true;
            continue;
        }

        if (ch == '"')
        {
            return result;
        }

        result.push_back(ch);
    }

    return "";
}

void UpdateVoiceBootstrapStatus()
{
    const DWORD now = GetTickCount();
    if (g_state.voiceBootstrapStatusPollTick && (now - g_state.voiceBootstrapStatusPollTick) < 300)
    {
        if (g_state.voiceBootstrapSubtitleActive
            && (!g_state.voiceBootstrapSubtitleRefreshTick || now >= g_state.voiceBootstrapSubtitleRefreshTick))
        {
            const std::string message = g_state.voiceBootstrapMessage.empty()
                ? "Cloning voices..."
                : g_state.voiceBootstrapMessage;
            ShowDialogSubtitle("", message, 1.2f);
            g_state.voiceBootstrapSubtitleRefreshTick = now + 700;
        }
        return;
    }

    g_state.voiceBootstrapStatusPollTick = now;

    std::ifstream in(VoiceBootstrapStatusPath(), std::ios::binary);
    if (!in)
    {
        if (g_state.voiceBootstrapSubtitleActive)
        {
            g_state.voiceBootstrapSubtitleActive = false;
            g_state.voiceBootstrapMessage.clear();
            g_state.voiceBootstrapSubtitleRefreshTick = 0;
            ClearDialogSubtitle();
        }
        return;
    }

    const std::string payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string status = ToLowerAscii(Trim(ExtractJsonStringField(payload, "status")));
    if (status != "running")
    {
        if (g_state.voiceBootstrapSubtitleActive)
        {
            g_state.voiceBootstrapSubtitleActive = false;
            g_state.voiceBootstrapMessage.clear();
            g_state.voiceBootstrapSubtitleRefreshTick = 0;
            ClearDialogSubtitle();
        }
        return;
    }

    g_state.voiceBootstrapSubtitleActive = true;
    g_state.voiceBootstrapMessage = Trim(ExtractJsonStringField(payload, "message"));
    if (g_state.voiceBootstrapMessage.empty())
    {
        g_state.voiceBootstrapMessage = "Cloning voices...";
    }

    if (!g_state.voiceBootstrapSubtitleRefreshTick || now >= g_state.voiceBootstrapSubtitleRefreshTick)
    {
        ShowDialogSubtitle("", g_state.voiceBootstrapMessage, 1.2f);
        g_state.voiceBootstrapSubtitleRefreshTick = now + 700;
    }
}

SpeakerSnapshot CaptureSpeakerSnapshot(TESObjectREFR* ref)
{
    SpeakerSnapshot snapshot{};
    if (!ref)
    {
        return snapshot;
    }

    snapshot.refId = ref->refID;
    snapshot.baseId = (ref->baseForm ? ref->baseForm->refID : 0);
    snapshot.posX = ref->posX;
    snapshot.posY = ref->posY;
    snapshot.posZ = ref->posZ;
    snapshot.valid = true;
    return snapshot;
}

void RememberNpcTarget(const std::string& npcKey, const std::string& npcName, const SpeakerSnapshot& speaker)
{
    g_state.lastNpcKey = npcKey;
    g_state.lastNpcName = npcName;
    g_state.lastNpcSpeaker = speaker;
    if (!npcKey.empty() && speaker.valid)
    {
        g_state.npcSpeakersByKey[npcKey] = speaker;
    }
}

std::optional<SpeakerSnapshot> ResolveSpeakerSnapshotForNpc(const std::string& npcKey, const std::string& npcName)
{
    const auto matchesNpc = [&](const std::string& candidateKey, const std::string& candidateName) -> bool {
        if (!npcKey.empty() && !candidateKey.empty() && _stricmp(candidateKey.c_str(), npcKey.c_str()) == 0)
        {
            return true;
        }
        if (!npcName.empty() && !candidateName.empty() && _stricmp(candidateName.c_str(), npcName.c_str()) == 0)
        {
            return true;
        }
        return false;
    };

    if (matchesNpc(g_state.pendingNpcKey, g_state.pendingNpcName) && g_state.pendingSpeaker.valid)
    {
        return g_state.pendingSpeaker;
    }

    if (matchesNpc(g_state.lastNpcKey, g_state.lastNpcName) && g_state.lastNpcSpeaker.valid)
    {
        if (TESObjectREFR* lastRef = ResolveSpeakerRef(g_state.lastNpcSpeaker))
        {
            const SpeakerSnapshot liveSnapshot = CaptureSpeakerSnapshot(lastRef);
            if (liveSnapshot.valid)
            {
                return liveSnapshot;
            }
        }
        return g_state.lastNpcSpeaker;
    }

    if (!npcKey.empty())
    {
        auto remembered = g_state.npcSpeakersByKey.find(npcKey);
        if (remembered != g_state.npcSpeakersByKey.end())
        {
            if (TESObjectREFR* rememberedRef = ResolveSpeakerRef(remembered->second))
            {
                const SpeakerSnapshot liveSnapshot = CaptureSpeakerSnapshot(rememberedRef);
                if (liveSnapshot.valid)
                {
                    g_state.npcSpeakersByKey[npcKey] = liveSnapshot;
                    return liveSnapshot;
                }
            }
        }
    }

    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return std::nullopt;
    }

    std::vector<NearbyNpcCandidate> candidates = FindNearbyMappedNpcsAround(player, kGamestateNearbyRadiusMeters);
    if (g_state.pendingSpeaker.valid)
    {
        if (TESObjectREFR* pendingRef = ResolveSpeakerRef(g_state.pendingSpeaker))
        {
            MergeNearbyNpcCandidates(candidates, FindNearbyMappedNpcsAround(pendingRef, kGamestateNearbyRadiusMeters));
        }
    }

    auto match = std::find_if(candidates.begin(), candidates.end(), [&](const NearbyNpcCandidate& candidate) {
        return matchesNpc(candidate.npcKey, candidate.npcName);
        });
    if (match == candidates.end() || !match->ref)
    {
        return std::nullopt;
    }

    const SpeakerSnapshot liveSnapshot = CaptureSpeakerSnapshot(match->ref);
    if (!liveSnapshot.valid)
    {
        return std::nullopt;
    }

    RememberNpcTarget(match->npcKey, match->npcName, liveSnapshot);
    return liveSnapshot;
}

std::string GetFormNameSafe(TESForm* form)
{
    if (!form)
    {
        return "";
    }

    char* name = form->GetName();
    if (!name)
    {
        return "";
    }

    return SanitizeLine(name);
}

std::string GetStringValueSafe(String& value)
{
    const char* text = value.m_data;
    return text ? SanitizeLine(text) : "";
}

// The display name of a building (interior cell) — the game's own cell name, the
// same "Prospector Saloon" it shows the player. Prefer the cell's fullName; fall
// back to humanizing the editor id (e.g. "GSProspectorSaloonInterior" ->
// "Prospector Saloon"). No hardcoded name tables. Used for BOTH the scenario's
// `minor_location` AND the travel manifest's building entrances, so the name the
// model is told and the name travel resolves are identical by construction.
std::string InteriorBuildingName(TESObjectCELL* cell)
{
    if (!cell)
    {
        return "";
    }
    const std::string display = GetStringValueSafe(cell->fullName.name);
    if (!display.empty())
    {
        return display;
    }
    return HumanizeIdentifier(GetFormNameSafe(cell));
}

double DistanceSquared3D(const TESObjectREFR* left, const TESObjectREFR* right)
{
    if (!left || !right)
    {
        return 0.0;
    }

    const double dx = static_cast<double>(left->posX) - static_cast<double>(right->posX);
    const double dy = static_cast<double>(left->posY) - static_cast<double>(right->posY);
    const double dz = static_cast<double>(left->posZ) - static_cast<double>(right->posZ);
    return (dx * dx) + (dy * dy) + (dz * dz);
}

LONG ComputeDistanceAttenuatedVolume(const TESObjectREFR* listener, const SpeakerSnapshot& speaker)
{
    if (!listener || !speaker.valid)
    {
        return DSBVOLUME_MAX;
    }

    const double dx = static_cast<double>(listener->posX) - static_cast<double>(speaker.posX);
    const double dy = static_cast<double>(listener->posY) - static_cast<double>(speaker.posY);
    const double dz = static_cast<double>(listener->posZ) - static_cast<double>(speaker.posZ);
    const double distanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz) / kGameUnitsPerMeter;

    if (distanceMeters <= kVoiceMinDistanceMeters)
    {
        return DSBVOLUME_MAX;
    }

    if (distanceMeters >= kVoiceMaxDistanceMeters)
    {
        return DSBVOLUME_MIN;
    }

    const double t = (distanceMeters - kVoiceMinDistanceMeters) / (kVoiceMaxDistanceMeters - kVoiceMinDistanceMeters);
    const double amplitude = std::clamp(1.0 - t, 0.0, 1.0);
    if (amplitude <= 0.00001)
    {
        return DSBVOLUME_MIN;
    }

    const double db = 2000.0 * std::log10(amplitude);
    return static_cast<LONG>(std::clamp(db, static_cast<double>(DSBVOLUME_MIN), static_cast<double>(DSBVOLUME_MAX)));
}

float RadiansToDegrees(float radians)
{
    return radians * (180.0f / 3.14159265358979323846f);
}

float NormalizeDegrees360(float degrees)
{
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f)
    {
        normalized += 360.0f;
    }
    return normalized;
}

float NormalizeSignedDegrees(float degrees)
{
    float normalized = NormalizeDegrees360(degrees);
    if (normalized > 180.0f)
    {
        normalized -= 360.0f;
    }
    return normalized;
}

float FacingDegreesTowardTarget(const TESObjectREFR* actorRef, const TESObjectREFR* targetRef)
{
    if (!actorRef || !targetRef)
    {
        return FLT_MAX;
    }

    const float dx = targetRef->posX - actorRef->posX;
    const float dy = targetRef->posY - actorRef->posY;
    if (std::fabs(dx) < 1.0f && std::fabs(dy) < 1.0f)
    {
        return FLT_MAX;
    }

    return NormalizeDegrees360(RadiansToDegrees(std::atan2(-dx, -dy)));
}

bool IsConversationHardHoldActive(DWORD now)
{
    return g_state.awaitingInput
        || g_state.awaitingReply
        || !g_state.pendingAudioChunks.empty()
        || !g_state.activeSounds.empty()
        || (g_state.activeSpeechUntilTick && now < g_state.activeSpeechUntilTick);
}

bool TryGetActorRestrained(TESObjectREFR* actorRef, bool& isRestrained)
{
    if (!actorRef || !EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    if (!g_scriptInterface->CallFunction(g_getRestrainedScript, actorRef, nullptr, &result, 1, actorRef))
    {
        LogLine("CallFunction failed for GetRestrained helper on %08X.", actorRef->refID);
        return false;
    }

    isRestrained = result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric && result.GetNumber() != 0.0;
    return true;
}

bool SetActorRestrainedState(TESObjectREFR* actorRef, bool restrained)
{
    if (!actorRef || !EnsureConversationHoldScripts())
    {
        return false;
    }

    Script* script = restrained ? g_setRestrainedScript : g_clearRestrainedScript;
    if (!g_scriptInterface->CallFunctionAlt(script, actorRef, 1, actorRef))
    {
        LogLine("CallFunctionAlt failed for %sRestrained helper on %08X.",
            restrained ? "Set" : "Clear",
            actorRef->refID);
        return false;
    }

    return true;
}

bool SetActorLookAtPlayer(TESObjectREFR* actorRef, PlayerCharacter* player, bool enabled)
{
    if (!actorRef || !player || !EnsureConversationHoldScripts())
    {
        return false;
    }

    Script* script = enabled ? g_startLookScript : g_stopLookScript;
    const UInt8 numArgs = enabled ? 2 : 1;
    const bool ok = enabled
        ? g_scriptInterface->CallFunctionAlt(script, actorRef, numArgs, actorRef, player)
        : g_scriptInterface->CallFunctionAlt(script, actorRef, numArgs, actorRef);
    if (!ok)
    {
        LogLine("CallFunctionAlt failed for %sLook helper on %08X.",
            enabled ? "" : "Stop",
            actorRef->refID);
        if (!g_state.traceRequestId.empty())
        {
            TraceRequestEvent(g_state.traceRequestId, "conversation_hold_look",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                },
                {
                    { "enabled", enabled },
                    { "call_ok", false },
                });
        }
        return false;
    }

    if (g_state.conversationHold.active)
    {
        g_state.conversationHold.lookApplied = enabled;
        if (!enabled)
        {
            g_state.conversationHold.lastAppliedFacingDegrees = FLT_MAX;
            g_state.conversationHold.lastFaceUpdateTick = 0;
            g_state.conversationHold.lastBodyFaceUpdateTick = 0;
        }
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_look",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
            },
            {
                { "enabled", enabled },
                { "call_ok", true },
            });
    }

    return true;
}

bool SetActorFacingPlayerIfNeeded(TESObjectREFR* actorRef, PlayerCharacter* player, DWORD now, bool force, bool* outIssued = nullptr)
{
    if (outIssued)
    {
        *outIssued = false;
    }
    if (!actorRef || !player || !EnsureConversationHoldScripts())
    {
        return false;
    }

    auto& hold = g_state.conversationHold;
    if (!force && hold.lastBodyFaceUpdateTick && now - hold.lastBodyFaceUpdateTick < g_debugConfig.conversationModeFaceRefreshIntervalMs)
    {
        return true;
    }

    const float targetDegrees = FacingDegreesTowardTarget(actorRef, player);
    if (targetDegrees == FLT_MAX)
    {
        hold.lastBodyFaceUpdateTick = now;
        return true;
    }

    const bool hasPriorFacing = hold.lastAppliedFacingDegrees != FLT_MAX;
    const float delta = hasPriorFacing
        ? std::fabs(NormalizeSignedDegrees(targetDegrees - hold.lastAppliedFacingDegrees))
        : FLT_MAX;
    if (!force && hasPriorFacing && delta < kConversationFaceTurnThresholdDegrees)
    {
        hold.lastBodyFaceUpdateTick = now;
        return true;
    }

    if (!g_faceObjectScript)
    {
        hold.lastBodyFaceUpdateTick = now;
        return true;
    }

    if (!g_scriptInterface->CallFunctionAlt(g_faceObjectScript, actorRef, 2, actorRef, player))
    {
        hold.lastBodyFaceUpdateTick = now;
        LogLine("CallFunctionAlt failed for FaceObject helper on %08X.", actorRef->refID);
        if (!g_state.traceRequestId.empty())
        {
            TraceRequestEvent(g_state.traceRequestId, "conversation_hold_face_player",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                    { "target_degrees", static_cast<double>(targetDegrees) },
                },
                {
                    { "call_ok", false },
                    { "issued", false },
                    { "used_face_object", true },
                });
        }
        return false;
    }

    hold.lastAppliedFacingDegrees = targetDegrees;
    hold.lastBodyFaceUpdateTick = now;
    if (outIssued)
    {
        *outIssued = true;
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_face_player",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "target_degrees", static_cast<double>(targetDegrees) },
                { "delta_degrees", hasPriorFacing ? static_cast<double>(delta) : 360.0 },
            },
            {
                { "call_ok", true },
                { "issued", true },
                { "used_face_object", true },
            });
    }

    return true;
}

bool RefreshActorLookAtPlayerIfNeeded(TESObjectREFR* actorRef, PlayerCharacter* player, DWORD now, bool force, bool* outIssued = nullptr)
{
    if (outIssued)
    {
        *outIssued = false;
    }
    if (!actorRef || !player)
    {
        return false;
    }

    auto& hold = g_state.conversationHold;
    bool shouldIssue = force || !hold.lookApplied || !hold.lastFaceUpdateTick;
    if (!shouldIssue && now - hold.lastFaceUpdateTick >= g_debugConfig.conversationLookRefreshIntervalMs)
    {
        shouldIssue = true;
    }
    if (!shouldIssue)
    {
        return true;
    }

    if (!SetActorLookAtPlayer(actorRef, player, true))
    {
        return false;
    }

    hold.lookApplied = true;
    hold.lastFaceUpdateTick = now;
    if (outIssued)
    {
        *outIssued = true;
    }
    return true;
}

bool IsConversationModeDistanceExceeded(TESObjectREFR* actorRef, PlayerCharacter* player)
{
    if (!actorRef || !player)
    {
        return false;
    }

    const double maxDistanceUnits = static_cast<double>(g_debugConfig.conversationModeReleaseDistanceMeters) * kGameUnitsPerMeter;
    return DistanceSquared3D(actorRef, player) > (maxDistanceUnits * maxDistanceUnits);
}

bool SetActorNoMovePackageState(TESObjectREFR* actorRef, bool enabled, bool* outIssued = nullptr)
{
    if (outIssued)
    {
        *outIssued = false;
    }
    if (!actorRef || !EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    bool callOk = false;
    bool issued = false;
    if (enabled)
    {
        callOk = g_applyNoMovePackageScript
            && g_scriptInterface->CallFunction(g_applyNoMovePackageScript, actorRef, nullptr, &result, 1, actorRef);
    }
    else
    {
        callOk = g_scriptInterface->CallFunction(g_removeScriptPackageScript, actorRef, nullptr, &result, 1, actorRef);
    }

    issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!enabled && issued)
    {
        EvaluateActorPackage(actorRef);
    }
    if (outIssued)
    {
        *outIssued = issued;
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_no_move_package",
            {
                { "package_editor_id", "DefaultSandboxNoMoveCurrentLocation200" },
            },
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "enabled", enabled },
                { "call_ok", callOk },
                { "issued", issued },
            });
    }

    return issued;
}

bool ApplyConversationModeIfNeeded(TESObjectREFR* actorRef, PlayerCharacter* player, DWORD now, bool force, bool* outRestrainedIssued = nullptr, bool* outFaceIssued = nullptr, bool* outLookIssued = nullptr)
{
    if (outRestrainedIssued)
    {
        *outRestrainedIssued = false;
    }
    if (outFaceIssued)
    {
        *outFaceIssued = false;
    }
    if (outLookIssued)
    {
        *outLookIssued = false;
    }
    if (!g_debugConfig.conversationModeEnabled || !actorRef || !player)
    {
        return false;
    }

    auto& hold = g_state.conversationHold;
    if (!hold.active)
    {
        return false;
    }

    if (hold.preserveFurnitureState)
    {
        if (hold.noMovePackageApplied)
        {
            SetActorNoMovePackageState(actorRef, false);
            hold.noMovePackageApplied = false;
        }
        if (hold.restrainedApplied)
        {
            SetActorRestrainedState(actorRef, false);
            hold.restrainedApplied = false;
        }
        hold.conversationModeApplied = true;
        return RefreshActorLookAtPlayerIfNeeded(actorRef, player, now, force, outLookIssued);
    }

    if (!hold.conversationModeApplied)
    {
        bool originalRestrained = false;
        hold.originalRestrainedKnown = TryGetActorRestrained(actorRef, originalRestrained);
        hold.originalRestrained = hold.originalRestrainedKnown ? originalRestrained : false;

        bool noMoveIssued = false;
        hold.noMovePackageApplied = SetActorNoMovePackageState(actorRef, true, &noMoveIssued);
        if (hold.noMovePackageApplied)
        {
            hold.conversationModeApplied = true;
            hold.restrainedApplied = false;
            if (outRestrainedIssued)
            {
                *outRestrainedIssued = noMoveIssued;
            }
        }
        else if (SetActorRestrainedState(actorRef, true))
        {
            hold.conversationModeApplied = true;
            hold.restrainedApplied = !hold.originalRestrainedKnown || !hold.originalRestrained;
            if (outRestrainedIssued)
            {
                *outRestrainedIssued = true;
            }
        }
        else
        {
            return false;
        }
    }

    const bool faceOk = SetActorFacingPlayerIfNeeded(actorRef, player, now, force, outFaceIssued);
    const bool lookOk = RefreshActorLookAtPlayerIfNeeded(actorRef, player, now, force, outLookIssued);
    return faceOk && lookOk;
}

bool StartActorConversationWithPlayer(TESObjectREFR* actorRef, PlayerCharacter* player)
{
    if (!actorRef || !player || !EnsureConversationHoldScripts())
    {
        return false;
    }

    TESForm* topicForm = ResolveModLocalForm(kBridgeDialoguePluginName, kBridgeDialogueTopicLocalFormId);
    if (!topicForm)
    {
        LogLine("StartConversation hold could not resolve topic %08X from %s.",
            kBridgeDialogueTopicLocalFormId,
            kBridgeDialoguePluginName);
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_startConversationScript, actorRef, nullptr, &result, 5,
        actorRef, player, topicForm, actorRef, player);
    if (!callOk)
    {
        LogLine("CallFunction failed for StartConversation helper on %08X.", actorRef->refID);
        if (!g_state.traceRequestId.empty())
        {
            TraceRequestEvent(g_state.traceRequestId, "conversation_hold_start_conversation",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                    { "topic_form_id", static_cast<double>(topicForm->refID) },
                },
                {
                    { "call_ok", false },
                    { "issued", false },
                });
        }
        return false;
    }

    const bool issued = result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric && result.GetNumber() != 0.0;
    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_start_conversation",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "topic_form_id", static_cast<double>(topicForm->refID) },
                { "result_type", static_cast<double>(result.GetType()) },
                { "result_number", result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", true },
                { "issued", issued },
            });
    }

    if (!issued)
    {
        LogLine("StartConversation helper returned not-issued for %08X topic %08X.",
            actorRef->refID,
            topicForm->refID);
    }

    return issued;
}

bool EvaluateActorPackage(TESObjectREFR* actorRef)
{
    if (!actorRef || !EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_evaluatePackageScript, actorRef, nullptr, &result, 1, actorRef);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!callOk)
    {
        LogLine("CallFunction failed for EvaluatePackage helper on %08X.", actorRef->refID);
    }
    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_evaluate_package",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", callOk },
                { "issued", issued },
            });
    }
    return issued;
}

bool AddActorScriptPackage(TESObjectREFR* actorRef, TESForm* packageForm, const char* packageEditorId, const char* traceStage)
{
    if (!actorRef || !packageForm)
    {
        return false;
    }

    if (!EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_addScriptPackageScript, actorRef, nullptr, &result, 2, actorRef, packageForm);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;

    if (issued)
    {
        EvaluateActorPackage(actorRef);
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, traceStage ? traceStage : "script_package_added",
            {
                { "package_editor_id", packageEditorId ? packageEditorId : "" },
            },
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "package_ref_id", static_cast<double>(packageForm->refID) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", callOk },
                { "issued", issued },
            });
    }

    if (!callOk)
    {
        LogLine("CallFunction failed for AddScriptPackage helper on %08X package %s.", actorRef->refID, packageEditorId ? packageEditorId : "<unknown>");
    }

    return issued;
}

bool RemoveActorScriptPackage(TESObjectREFR* actorRef, const char* traceStage)
{
    if (!actorRef || !EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_removeScriptPackageScript, actorRef, nullptr, &result, 1, actorRef);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, traceStage ? traceStage : "script_package_removed",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", callOk },
                { "issued", issued },
            });
    }

    if (!callOk)
    {
        LogLine("CallFunction failed for RemoveScriptPackage helper on %08X.", actorRef->refID);
    }

    return issued;
}

bool SetActorPlayerTeammate(TESObjectREFR* actorRef, bool enabled, const char* traceStage)
{
    if (!actorRef || !EnsurePlayerTeammateScript())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_setPlayerTeammateScript, actorRef, nullptr, &result, 2, actorRef, enabled ? 1 : 0);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, traceStage ? traceStage : "game_master_follow_teammate",
            {},
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "call_ok", callOk },
                { "issued", issued },
                { "teammate_enabled", enabled },
            });
    }

    if (!callOk)
    {
        LogLine("CallFunction failed for SetPlayerTeammate helper on %08X enabled=%d.", actorRef->refID, enabled ? 1 : 0);
    }

    return issued;
}

TESForm* ResolveDefaultFollowPackage()
{
    TESForm* form = GetFormByID(kDefaultFollowPackageEditorId);
    if (!form)
    {
        LogLine("Could not resolve follow package editor id %s.", kDefaultFollowPackageEditorId);
        return nullptr;
    }

    TESPackage* package = DYNAMIC_CAST(form, TESForm, TESPackage);
    if (!package)
    {
        LogLine("Resolved %s to %08X, but it is not a TESPackage.", kDefaultFollowPackageEditorId, form->refID);
        return nullptr;
    }

    return form;
}

bool SetActorConversationPackageState(TESObjectREFR* actorRef, bool enabled, bool* outIssued = nullptr)
{
    if (!actorRef)
    {
        if (outIssued)
        {
            *outIssued = false;
        }
        return false;
    }

    if (!EnsureConversationHoldScripts())
    {
        if (outIssued)
        {
            *outIssued = false;
        }
        return false;
    }

    TESForm* packageForm = enabled ? ResolveModLocalForm(kBridgeDialoguePluginName, kBridgeConversationPackageLocalFormId) : nullptr;
    NVSEArrayVarInterface::Element result;
    bool callOk = false;
    bool issued = false;

    if (enabled)
    {
        callOk = packageForm
            && g_scriptInterface->CallFunction(g_addScriptPackageScript, actorRef, nullptr, &result, 2, actorRef, packageForm);
    }
    else
    {
        callOk = g_scriptInterface->CallFunction(g_removeScriptPackageScript, actorRef, nullptr, &result, 1, actorRef);
    }

    issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (issued)
    {
        EvaluateActorPackage(actorRef);
    }
    if (outIssued)
    {
        *outIssued = issued;
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_script_package",
            {
                { "package_editor_id", kBridgeConversationPackageEditorId },
            },
            {
                { "speaker_ref_id", static_cast<double>(actorRef->refID) },
                { "package_local_form_id_hint", static_cast<double>(kBridgeConversationPackageLocalFormId) },
                { "package_ref_id", packageForm ? static_cast<double>(packageForm->refID) : 0.0 },
                { "result_type", callOk ? static_cast<double>(result.GetType()) : 0.0 },
                { "result_number", callOk && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric ? result.GetNumber() : 0.0 },
            },
            {
                { "enabled", enabled },
                { "call_ok", callOk },
                { "issued", issued },
            });
    }

    return issued;
}

bool IsActorUsingPackage(TESObjectREFR* actorRef, TESForm* packageForm, bool* outKnown)
{
    if (outKnown)
    {
        *outKnown = false;
    }

    if (!actorRef || !packageForm || !EnsureConversationHoldScripts())
    {
        return false;
    }

    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_isCurrentPackageScript, actorRef, nullptr, &result, 2, actorRef, packageForm);
    if (!callOk || result.GetType() != NVSEArrayVarInterface::Element::kType_Numeric)
    {
        return false;
    }

    if (outKnown)
    {
        *outKnown = true;
    }
    return result.GetNumber() != 0.0;
}

bool IsActorUsingBridgeConversationPackage(TESObjectREFR* actorRef, bool* outKnown)
{
    TESForm* packageForm = ResolveModLocalForm(kBridgeDialoguePluginName, kBridgeConversationPackageLocalFormId);
    return IsActorUsingPackage(actorRef, packageForm, outKnown);
}

bool ShouldPreserveActorConversationAnimation(TESObjectREFR* speakerRef)
{
    auto* actor = static_cast<Actor*>(speakerRef);
    if (!actor || !actor->baseProcess)
    {
        return false;
    }

    const int sitSleepState = actor->baseProcess->GetSitSleepState();
    return sitSleepState == HighProcess::kSitSleepState_LoadSitIdle
        || sitSleepState == HighProcess::kSitSleepState_WantToSit
        || sitSleepState == HighProcess::kSitSleepState_WaitingForSitAnim
        || sitSleepState == HighProcess::kSitSleepState_Sitting
        || sitSleepState == HighProcess::kSitSleepState_LoadingSleepIdle
        || sitSleepState == HighProcess::kSitSleepState_WantToSleep
        || sitSleepState == HighProcess::kSitSleepState_WaitingForSleepAnim
        || sitSleepState == HighProcess::kSitSleepState_Sleeping;
}

void ReleaseConversationHold(const char* reason)
{
    auto hold = std::move(g_state.conversationHold);
    g_state.conversationHold = {};

    if (!hold.active)
    {
        return;
    }

    TESObjectREFR* actorRef = ResolveSpeakerRef(hold.speaker);
    PlayerCharacter* player = GetPlayer();
    if (actorRef && player)
    {
        SetActorLookAtPlayer(actorRef, player, false);
    }

    if (actorRef && hold.scriptPackageApplied)
    {
        SetActorConversationPackageState(actorRef, false);
    }

    if (actorRef && hold.noMovePackageApplied)
    {
        SetActorNoMovePackageState(actorRef, false);
    }

    if (actorRef && hold.conversationModeApplied)
    {
        if (hold.restrainedApplied)
        {
            SetActorRestrainedState(actorRef, false);
        }
        else if (hold.originalRestrained)
        {
            EvaluateActorPackage(actorRef);
        }
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_released",
            {
                { "reason", reason ? reason : "" },
                { "npc_key", hold.npcKey },
                { "npc_name", hold.npcName },
            },
            {
                { "speaker_ref_id", static_cast<double>(hold.speaker.refId) },
            },
            {
                { "script_package_applied", hold.scriptPackageApplied },
                { "conversation_mode_applied", hold.conversationModeApplied },
                { "no_move_package_applied", hold.noMovePackageApplied },
                { "restrained_applied", hold.restrainedApplied },
                { "original_restrained_known", hold.originalRestrainedKnown },
                { "original_restrained", hold.originalRestrained },
            });
    }
}

void EngageConversationHold(const std::string& npcKey, const std::string& npcName, const SpeakerSnapshot& speaker)
{
    if (!speaker.refId)
    {
        return;
    }

    auto& hold = g_state.conversationHold;
    if (hold.active && hold.speaker.refId != speaker.refId)
    {
        ReleaseConversationHold("speaker_changed");
    }

    ConversationHoldState& updatedHold = g_state.conversationHold;
    const bool wasActive = updatedHold.active;
    updatedHold.active = true;
    updatedHold.npcKey = npcKey;
    updatedHold.npcName = npcName;
    updatedHold.speaker = speaker;
    updatedHold.releaseTick = 0;
    updatedHold.conversationIssued = wasActive ? updatedHold.conversationIssued : false;
    updatedHold.lookApplied = wasActive ? updatedHold.lookApplied : false;
    updatedHold.conversationModeApplied = wasActive ? updatedHold.conversationModeApplied : false;
    updatedHold.originalRestrainedKnown = wasActive ? updatedHold.originalRestrainedKnown : false;
    updatedHold.originalRestrained = wasActive ? updatedHold.originalRestrained : false;
    updatedHold.restrainedApplied = wasActive ? updatedHold.restrainedApplied : false;
    updatedHold.noMovePackageApplied = wasActive ? updatedHold.noMovePackageApplied : false;
    updatedHold.lastAppliedFacingDegrees = wasActive ? updatedHold.lastAppliedFacingDegrees : FLT_MAX;
    updatedHold.lastBodyFaceUpdateTick = wasActive ? updatedHold.lastBodyFaceUpdateTick : 0;
    updatedHold.lastPackageCheckTick = wasActive ? updatedHold.lastPackageCheckTick : 0;

    TESObjectREFR* actorRef = ResolveSpeakerRef(speaker);
    PlayerCharacter* player = GetPlayer();
    if (!actorRef || !player)
    {
        return;
    }

    // COMBAT OVERRIDE: if the NPC is already fighting when addressed, do NOT lock
    // them into the conversation package / restraint. That freeze-and-face is
    // exactly what clears their combat state and makes them answer calmly. Leave
    // them fighting -- the reply rides on top, and their IsInCombat stays true so
    // prompt assembly gets the combat scenario. hold.active stays true so the rest
    // of the pipeline (trace, release) is symmetric; we just apply nothing here.
    {
        auto* speakerActor = static_cast<Actor*>(actorRef);
        updatedHold.combatMode = speakerActor && speakerActor->baseProcess && speakerActor->IsInCombat();
        if (updatedHold.combatMode)
        {
            LogLine("Conversation hold: '%s' is IN COMBAT -> not locking (stays fighting).",
                GetDisplayNameSafe(actorRef).c_str());
            return;
        }
    }

    updatedHold.preserveFurnitureState = wasActive
        ? (updatedHold.preserveFurnitureState || ShouldPreserveActorConversationAnimation(actorRef))
        : ShouldPreserveActorConversationAnimation(actorRef);

    updatedHold.scriptPackageApplied = wasActive ? updatedHold.scriptPackageApplied : false;
    const bool useScriptPackage = !g_debugConfig.conversationModeEnabled && !updatedHold.preserveFurnitureState;
    if (!wasActive && useScriptPackage)
    {
        bool packageIssued = false;
        updatedHold.scriptPackageApplied = SetActorConversationPackageState(actorRef, true, &packageIssued);
        if (!g_state.traceRequestId.empty())
        {
            TraceRequestEvent(g_state.traceRequestId, "conversation_hold_package_applied",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(speaker.refId) },
                },
                {
                    { "issued", packageIssued },
                    { "active_after_call", updatedHold.scriptPackageApplied },
                });
        }
        updatedHold.conversationIssued = updatedHold.scriptPackageApplied
            ? StartActorConversationWithPlayer(actorRef, player)
            : false;
    }
    else if (!wasActive)
    {
        updatedHold.conversationIssued = false;
    }
    const DWORD now = GetTickCount();
    bool restrainedIssued = false;
    bool faceIssued = false;
    bool lookIssued = false;
    if (g_debugConfig.conversationModeEnabled)
    {
        ApplyConversationModeIfNeeded(actorRef, player, now, !wasActive, &restrainedIssued, &faceIssued, &lookIssued);
    }
    else
    {
        RefreshActorLookAtPlayerIfNeeded(actorRef, player, now, !wasActive, &lookIssued);
    }

    if (!g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_engaged",
            {
                { "npc_key", npcKey },
                { "npc_name", npcName },
            },
            {
                { "speaker_ref_id", static_cast<double>(speaker.refId) },
            },
            {
                { "script_package_applied", updatedHold.scriptPackageApplied },
                { "conversation_issued", updatedHold.conversationIssued },
                { "preserve_furniture_state", updatedHold.preserveFurnitureState },
                { "conversation_mode_enabled", g_debugConfig.conversationModeEnabled },
                { "conversation_mode_applied", updatedHold.conversationModeApplied },
                { "no_move_package_applied", updatedHold.noMovePackageApplied },
                { "restrained_issued", restrainedIssued },
                { "face_issued", faceIssued },
                { "look_issued", lookIssued },
            });
    }
}

void UpdateConversationHold()
{
    auto& hold = g_state.conversationHold;
    if (!hold.active)
    {
        return;
    }

    const DWORD now = GetTickCount();
    const bool hardHold = IsConversationHardHoldActive(now);
    if (hardHold)
    {
        hold.releaseTick = now + kConversationReleaseDelayMs;
    }
    else if (!hold.releaseTick)
    {
        hold.releaseTick = now + kConversationReleaseDelayMs;
    }

    TESObjectREFR* actorRef = ResolveSpeakerRef(hold.speaker);
    PlayerCharacter* player = GetPlayer();
    if (!actorRef || !player)
    {
        ReleaseConversationHold("speaker_unresolved");
        return;
    }

    // COMBAT OVERRIDE: a fighting NPC must not stay locked into the conversation.
    // If combat broke out after the hold engaged (peaceful talk -> fight), undo the
    // locks so they can move/fight, and stop refreshing them. When combat ends we
    // fall through and re-establish the hold normally.
    {
        auto* speakerActor = static_cast<Actor*>(actorRef);
        const bool speakerInCombat = speakerActor && speakerActor->baseProcess && speakerActor->IsInCombat();
        if (speakerInCombat)
        {
            if (!hold.combatMode)
            {
                hold.combatMode = true;
                LogLine("Conversation hold: '%s' entered combat -> releasing lock, letting them fight.",
                    GetDisplayNameSafe(actorRef).c_str());
            }
            // Undo whatever lock was applied before combat started.
            if (hold.lookApplied)
            {
                SetActorLookAtPlayer(actorRef, player, false);
                hold.lookApplied = false;
            }
            if (hold.restrainedApplied)
            {
                SetActorRestrainedState(actorRef, false);
                hold.restrainedApplied = false;
            }
            if (hold.noMovePackageApplied)
            {
                SetActorNoMovePackageState(actorRef, false);
                hold.noMovePackageApplied = false;
            }
            if (hold.scriptPackageApplied)
            {
                SetActorConversationPackageState(actorRef, false);
                hold.scriptPackageApplied = false;
                EvaluateActorPackage(actorRef);
            }
            hold.conversationModeApplied = false;
            return;  // hands off while fighting
        }
        if (hold.combatMode)
        {
            hold.combatMode = false;  // combat ended -> resume the normal hold below
        }
    }

    if (g_debugConfig.conversationModeEnabled && IsConversationModeDistanceExceeded(actorRef, player))
    {
        ReleaseConversationHold("player_out_of_range");
        return;
    }

    const SpeakerSnapshot liveSpeaker = CaptureSpeakerSnapshot(actorRef);
    if (liveSpeaker.valid)
    {
        hold.speaker = liveSpeaker;
    }
    if (!hold.preserveFurnitureState && ShouldPreserveActorConversationAnimation(actorRef))
    {
        hold.preserveFurnitureState = true;
    }

    bool packageIssued = false;
    bool packageCurrentKnown = false;
    bool packageCurrent = false;
    bool packageChecked = false;
    if (!g_debugConfig.conversationModeEnabled && !hold.preserveFurnitureState && (!hold.lastPackageCheckTick || now - hold.lastPackageCheckTick >= kConversationPackageRefreshIntervalMs))
    {
        packageChecked = true;
        hold.lastPackageCheckTick = now;
        packageCurrent = IsActorUsingBridgeConversationPackage(actorRef, &packageCurrentKnown);
        if (!hold.scriptPackageApplied || (packageCurrentKnown && !packageCurrent))
        {
            hold.scriptPackageApplied = SetActorConversationPackageState(actorRef, true, &packageIssued);
        }
    }

    bool restrainedIssued = false;
    bool faceIssued = false;
    bool lookIssued = false;
    bool modeRefreshed = false;
    if (g_debugConfig.conversationModeEnabled)
    {
        modeRefreshed = ApplyConversationModeIfNeeded(actorRef, player, now, false, &restrainedIssued, &faceIssued, &lookIssued);
    }
    else if (!hold.lastFaceUpdateTick || now - hold.lastFaceUpdateTick >= kConversationFaceUpdateIntervalMs)
    {
        RefreshActorLookAtPlayerIfNeeded(actorRef, player, now, false, &lookIssued);
    }

    if ((packageChecked || restrainedIssued || faceIssued || lookIssued) && !g_state.traceRequestId.empty())
    {
        TraceRequestEvent(g_state.traceRequestId, "conversation_hold_refreshed",
            {
                { "npc_key", hold.npcKey },
                { "npc_name", hold.npcName },
            },
            {
                { "speaker_ref_id", static_cast<double>(hold.speaker.refId) },
            },
            {
                { "conversation_issued", hold.conversationIssued },
                { "script_package_refresh_issued", packageIssued },
                { "script_package_applied", hold.scriptPackageApplied },
                { "script_package_current_known", packageCurrentKnown },
                { "script_package_current", packageCurrent },
                { "script_package_checked", packageChecked },
                { "preserve_furniture_state", hold.preserveFurnitureState },
                { "conversation_mode_enabled", g_debugConfig.conversationModeEnabled },
                { "conversation_mode_refreshed", modeRefreshed },
                { "conversation_mode_applied", hold.conversationModeApplied },
                { "no_move_package_applied", hold.noMovePackageApplied },
                { "restrained_issued", restrainedIssued },
                { "face_issued", faceIssued },
                { "look_issued", lookIssued },
            });
    }

    if (!g_debugConfig.conversationModeEnabled && !hardHold && hold.releaseTick && now >= hold.releaseTick)
    {
        ReleaseConversationHold("idle_timeout");
    }
}

bool IsMapMarkerRef(TESObjectREFR* ref)
{
    return ref && ref->baseForm && ref->baseForm->refID == 0x10;
}

ExtraMapMarker* GetMapMarkerExtra(TESObjectREFR* ref)
{
    if (!IsMapMarkerRef(ref))
    {
        return nullptr;
    }

    BSExtraData* extra = ref->extraDataList.GetByType(kExtraData_MapMarker);
    return extra ? reinterpret_cast<ExtraMapMarker*>(extra) : nullptr;
}

std::string GetMapMarkerDisplayName(TESObjectREFR* ref)
{
    ExtraMapMarker* marker = GetMapMarkerExtra(ref);
    if (marker && marker->data)
    {
        const std::string markerName = GetStringValueSafe(marker->data->fullName.name);
        if (!markerName.empty())
        {
            return markerName;
        }
    }

    return GetFormNameSafe(ref);
}

bool IsLandmarkBaseType(UInt8 typeId)
{
    switch (typeId)
    {
    case kFormType_TESObjectACTI:
    case kFormType_BGSTalkingActivator:
    case kFormType_BGSTerminal:
    case kFormType_TESObjectCONT:
    case kFormType_TESObjectDOOR:
    case kFormType_TESObjectSTAT:
    case kFormType_BGSMovableStatic:
    case kFormType_TESFurniture:
    case kFormType_TESObjectTREE:
    case kFormType_TESFlora:
        return true;
    default:
        return false;
    }
}

bool IsCandidateLandmarkRef(TESObjectREFR* ref)
{
    if (!ref || ref == GetPlayer() || !ref->baseForm)
    {
        return false;
    }

    if (IsMapMarkerRef(ref))
    {
        return false;
    }

    if (!IsLandmarkBaseType(ref->baseForm->typeID))
    {
        return false;
    }

    const std::string name = GetFormNameSafe(ref);
    if (name.empty())
    {
        return false;
    }

    const std::string slug = Slugify(name);
    if (slug.empty() || slug == "door" || slug == "container" || slug == "activator")
    {
        return false;
    }

    static const char* kRejectedLandmarkTerms[] = {
        "chair", "stool", "bench", "table", "desk", "bed", "booth", "crate",
        "barrel", "sack", "campfire", "bottle", "cup", "mug", "plate", "fork",
        "spoon", "knife", "lantern", "rock", "rubble", "trash", "poster",
        "signpost", "easypete", "easy_pete", "sunny", "trudy", "chet", "ringo",
        "victor", "docmitchell", "doc_mitchell", "goodsprings settler"
    };

    for (const char* rejected : kRejectedLandmarkTerms)
    {
        if (slug.find(rejected) != std::string::npos)
        {
            return false;
        }
    }

    return true;
}

std::string FindNearestWorldMapLocation(PlayerCharacter* player)
{
    if (!player || !player->parentCell || !player->parentCell->worldSpace)
    {
        return "";
    }

    TESWorldSpace* currentWorld = player->parentCell->worldSpace;
    if (!currentWorld || !currentWorld->cellMap)
    {
        return "";
    }

    const auto parentCellCoordinates = GetWorldCellCoordinates(player->parentCell);
    if (!parentCellCoordinates.has_value())
    {
        return "";
    }

    const SInt32 centerX = parentCellCoordinates->first;
    const SInt32 centerY = parentCellCoordinates->second;
    constexpr SInt32 kSearchDepth = 8;
    double bestDistance = DBL_MAX;
    std::string bestName;

    for (SInt32 y = centerY - kSearchDepth; y <= centerY + kSearchDepth; ++y)
    {
        for (SInt32 x = centerX - kSearchDepth; x <= centerX + kSearchDepth; ++x)
        {
            TESObjectCELL* cell = currentWorld->cellMap->Lookup(MakeWorldCellKey(x, y));
            if (!cell)
            {
                continue;
            }

            for (auto iter = cell->objectList.Begin(); !iter.End(); ++iter)
            {
                TESObjectREFR* ref = *iter;
                if (!ref || !IsMapMarkerRef(ref))
                {
                    continue;
                }

                const std::string name = GetMapMarkerDisplayName(ref);
                if (name.empty())
                {
                    continue;
                }

                const double distance = DistanceSquared3D(player, ref);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestName = name;
                }
            }
        }
    }

    return bestName;
}

// Scheduler travel: lowercase + trim a marker name/query and drop a leading "the "
// so "the Prospector Saloon" and "prospector saloon" compare equal.
std::string NormalizeMarkerQuery(std::string value)
{
    value = ToLowerAscii(Trim(value));
    if (value.rfind("the ", 0) == 0)
    {
        value = value.substr(4);
    }
    return value;
}

// Resolve a natural-language destination ("prospector saloon") to a map-marker
// reference in the player's CURRENT worldspace. Every map marker is a persistent
// ref living in the worldspace's permanent cell, so that is searched first (no
// distance limit); a wide cell sweep is a fallback. Matches by display name:
// exact first, then substring either way. Returns nullptr when nothing matches
// (the caller then falls back to the player). Runs only on a fired travel task
// (rare), so the sweep's cost is a non-issue.
TESObjectREFR* FindMapMarkerByName(const std::string& query)
{
    const std::string q = NormalizeMarkerQuery(query);
    if (q.empty())
    {
        return nullptr;
    }
    PlayerCharacter* player = GetPlayer();
    if (!player || !player->parentCell || !player->parentCell->worldSpace)
    {
        return nullptr;
    }
    TESWorldSpace* world = player->parentCell->worldSpace;

    TESObjectREFR* exact = nullptr;
    TESObjectREFR* partial = nullptr;
    auto consider = [&](TESObjectREFR* ref) {
        if (exact || !ref || !IsMapMarkerRef(ref))
        {
            return;
        }
        const std::string name = NormalizeMarkerQuery(GetMapMarkerDisplayName(ref));
        if (name.empty())
        {
            return;
        }
        if (name == q)
        {
            exact = ref;
        }
        else if (!partial && (name.find(q) != std::string::npos || q.find(name) != std::string::npos))
        {
            partial = ref;
        }
    };

    // 1) The worldspace permanent cell holds every map marker, regardless of distance.
    if (world->cell)
    {
        for (auto iter = world->cell->objectList.Begin(); !iter.End() && !exact; ++iter)
        {
            consider(*iter);
        }
    }
    // 2) Fallback: a wide cell sweep around the player.
    if (!exact && !partial && world->cellMap)
    {
        const auto coords = GetWorldCellCoordinates(player->parentCell);
        if (coords.has_value())
        {
            constexpr SInt32 kDepth = 40;
            for (SInt32 y = coords->second - kDepth; y <= coords->second + kDepth && !exact; ++y)
            {
                for (SInt32 x = coords->first - kDepth; x <= coords->first + kDepth && !exact; ++x)
                {
                    TESObjectCELL* cell = world->cellMap->Lookup(MakeWorldCellKey(x, y));
                    if (!cell)
                    {
                        continue;
                    }
                    for (auto iter = cell->objectList.Begin(); !iter.End() && !exact; ++iter)
                    {
                        consider(*iter);
                    }
                }
            }
        }
    }

    TESObjectREFR* result = exact ? exact : partial;
    if (result)
    {
        LogLine("scheduler: resolved destination '%s' -> marker %08X (%s).",
            query.c_str(), result->refID, GetMapMarkerDisplayName(result).c_str());
    }
    else
    {
        LogLine("scheduler: destination '%s' matched no map marker.", query.c_str());
    }
    return result;
}

// Scheduler travel: gather the map locations within `radiusMeters` of the player,
// closest first, as a comma-separated list of display names (up to `maxCount`).
// Reuses the worldspace permanent cell (holds every marker). chasm injects this as
// the travel action's destination candidates so the model picks a REAL place name.
std::string BuildNearbyLocationsMacro(PlayerCharacter* player, size_t maxCount)
{
    // Only meaningful outdoors: distances are measured from the player's exterior
    // position (interior cells use unrelated local coordinates).
    if (!player || !player->parentCell || !player->parentCell->worldSpace)
    {
        return "";
    }
    const double px = player->posX;
    const double py = player->posY;
    const double pz = player->posZ;

    std::vector<std::pair<double, std::string>> found; // (distanceSquared, name)
    const auto addUnique = [&](double distSq, const std::string& name) {
        if (name.empty())
        {
            return;
        }
        const std::string key = ToLowerAscii(name);
        for (auto& fp : found)
        {
            if (ToLowerAscii(fp.second) == key)
            {
                if (distSq < fp.first)
                {
                    fp.first = distSq; // keep the nearest instance of a shared name
                }
                return;
            }
        }
        found.emplace_back(distSq, name);
    };

    // Map markers (the worldspace permanent cell holds every one).
    if (TESWorldSpace* world = player->parentCell->worldSpace; world && world->cell)
    {
        for (auto iter = world->cell->objectList.Begin(); !iter.End(); ++iter)
        {
            TESObjectREFR* ref = *iter;
            if (!ref || !IsMapMarkerRef(ref))
            {
                continue;
            }
            addUnique(DistanceSquared3D(player, ref), SanitizeLine(GetMapMarkerDisplayName(ref)));
        }
    }
    // Building entrances discovered so far (front doors — the same set travel resolves).
    for (const auto& kv : g_buildingEntrances)
    {
        const BuildingEntrance& be = kv.second;
        const double dx = px - be.x;
        const double dy = py - be.y;
        const double dz = pz - be.z;
        addUnique(dx * dx + dy * dy + dz * dz, be.name);
    }

    std::sort(found.begin(), found.end(),
        [](const std::pair<double, std::string>& a, const std::pair<double, std::string>& b) {
            return a.first < b.first;
        });

    // The nearest `maxCount`, each with its distance in metres so the model can pick
    // sensibly: "Prospector Saloon (45m), Goodsprings (120m), ...".
    std::ostringstream out;
    size_t shown = 0;
    for (const auto& fp : found)
    {
        if (shown >= maxCount)
        {
            break;
        }
        if (shown++)
        {
            out << ", ";
        }
        const long meters = std::lround(std::sqrt(fp.first) / static_cast<double>(kGameUnitsPerMeter));
        out << fp.second << " (" << meters << "m)";
    }
    return out.str();
}

// Movement engine: write EVERY map marker in the current worldspace (display name
// + world position + runtime form id) to `<bridge>/locations.json`, so chasm can
// measure the distance to any destination and drive an on-time journey without a
// round-trip. Throttled to at most once every few seconds (markers don't move); a
// no-op when there's no worldspace (main menu / an interior with no markers), which
// simply keeps the last-written manifest.
void WriteLocationsManifestIfNeeded(PlayerCharacter* player, bool force)
{
    static ULONGLONG lastWriteMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    if (!force && lastWriteMs != 0 && (nowMs - lastWriteMs) < 5000)
    {
        return;
    }
    if (!player || !player->parentCell || !player->parentCell->worldSpace)
    {
        return;
    }
    TESWorldSpace* world = player->parentCell->worldSpace;
    if (!world->cell)
    {
        return;
    }

    // Discover building entrances from teleport doors: for a door whose linked door
    // is in an INTERIOR, map the interior cell's name ("Prospector Saloon") to the
    // exterior front door (walk target) + the interior door (inside). Persistent load
    // doors live in the worldspace's PERMANENT cell — in memory regardless of what is
    // loaded — so all buildings are known WITHOUT having visited them.
    const auto tryAddDoor = [](TESObjectREFR* door) {
        if (!door)
        {
            return;
        }
        BSExtraData* extra = door->extraDataList.GetByType(kExtraData_Teleport);
        ExtraTeleport* tele = extra ? reinterpret_cast<ExtraTeleport*>(extra) : nullptr;
        if (!tele || !tele->data || !tele->data->linkedDoor)
        {
            return;
        }
        TESObjectREFR* linked = tele->data->linkedDoor;
        if (!linked->parentCell || !linked->parentCell->IsInterior())
        {
            return; // only doors that lead INTO a building
        }
        // Name the building from its cell display name — the SAME source the
        // scenario's minor_location uses — so model, scenario and travel agree.
        const std::string name = InteriorBuildingName(linked->parentCell);
        if (name.empty())
        {
            return; // no natural name (wilderness / unnamed)
        }
        BuildingEntrance& be = g_buildingEntrances[ToLowerAscii(name)];
        be.name = name;
        be.x = door->posX;        // exterior front door (outside — the walk target)
        be.y = door->posY;
        be.z = door->posZ;
        be.formId = door->refID;
        be.ix = linked->posX;     // interior door (inside)
        be.iy = linked->posY;
        be.iz = linked->posZ;
        be.interiorFormId = linked->refID;
    };
    // The worldspace permanent cell holds every persistent ref (map markers AND load
    // doors) in memory regardless of what is loaded — the single source, no visiting.
    for (auto it = world->cell->objectList.Begin(); !it.End(); ++it)
    {
        tryAddDoor(*it);
    }

    std::ostringstream out;
    out << "{\"version\":1,\"markers\":[";
    size_t count = 0;
    const auto emitEntry = [&](const std::string& name, float x, float y, float z, UInt32 formId) {
        if (count++)
        {
            out << ",";
        }
        out << "{\"name\":" << JsonEscape(name)
            << ",\"x\":" << std::fixed << std::setprecision(2) << x
            << ",\"y\":" << y
            << ",\"z\":" << z
            << ",\"form_id\":" << std::dec << formId << "}";
    };
    for (auto iter = world->cell->objectList.Begin(); !iter.End(); ++iter)
    {
        TESObjectREFR* ref = *iter;
        if (!ref || !IsMapMarkerRef(ref))
        {
            continue;
        }
        const std::string name = GetMapMarkerDisplayName(ref);
        if (name.empty())
        {
            continue;
        }
        emitEntry(name, ref->posX, ref->posY, ref->posZ, ref->refID);
    }
    // Building entrances ride the same list — the exterior front door (walk target)
    // plus the interior door (`inside_*`) for travelling to the INSIDE of a building.
    for (const auto& kv : g_buildingEntrances)
    {
        const BuildingEntrance& be = kv.second;
        if (count++)
        {
            out << ",";
        }
        out << "{\"name\":" << JsonEscape(be.name)
            << ",\"x\":" << std::fixed << std::setprecision(2) << be.x
            << ",\"y\":" << be.y
            << ",\"z\":" << be.z
            << ",\"form_id\":" << std::dec << be.formId
            << ",\"inside_x\":" << std::fixed << std::setprecision(2) << be.ix
            << ",\"inside_y\":" << be.iy
            << ",\"inside_z\":" << be.iz
            << ",\"inside_form_id\":" << std::dec << be.interiorFormId << "}";
    }
    out << "]}";

    lastWriteMs = nowMs;
    const fs::path path = BridgeDir() / "locations.json";
    const fs::path tmp = BridgeDir() / "locations.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            return;
        }
        const std::string body = out.str();
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
    {
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
    }
    LogLine("movement: wrote locations manifest (%zu markers).", count);
}

std::string FindNearestLocalMapLocation(PlayerCharacter* player)
{
    if (!player || !player->parentCell)
    {
        return "";
    }

    TESObjectCELL* cell = player->parentCell;
    if (cell->worldSpace == nullptr)
    {
        const std::string inferred = InferMinorLocationFromCellIdentifier(GetFormNameSafe(cell));
        if (!inferred.empty())
        {
            return inferred;
        }
    }

    double bestDistance = DBL_MAX;
    std::string bestName;

    for (auto iter = cell->objectList.Begin(); !iter.End(); ++iter)
    {
        TESObjectREFR* ref = *iter;
        if (!IsCandidateLandmarkRef(ref))
        {
            continue;
        }

        const std::string name = GetNaturalMinorLocationName(ref);
        if (name.empty())
        {
            continue;
        }
        const double distance = DistanceSquared3D(player, ref);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestName = name;
        }
    }

    if (!bestName.empty())
    {
        const std::string bestSlug = Slugify(bestName);
        if (bestSlug == "easypetechairref" || bestSlug == "easypetechair" || bestSlug == "easypeteboothref")
        {
            bestName.clear();
        }
    }

    if (!bestName.empty())
    {
        const std::string inferred = InferMinorLocationFromCellIdentifier(bestName);
        return inferred.empty() ? bestName : inferred;
    }

    const std::string cellName = GetFormNameSafe(cell);
    return InferMinorLocationFromCellIdentifier(cellName);
}

LocationSnapshot CapturePlayerLocation()
{
    LocationSnapshot snapshot{};
    PlayerCharacter* player = GetPlayer();
    if (!player || !player->parentCell)
    {
        return snapshot;
    }

    TESObjectCELL* cell = player->parentCell;
    snapshot.cell = GetFormNameSafe(cell);
    snapshot.interior = cell->IsInterior();
    if (cell->worldSpace)
    {
        snapshot.worldspace = GetFormNameSafe(cell->worldSpace);
    }
    snapshot.major = FindNearestWorldMapLocation(player);
    if (snapshot.major.empty() && snapshot.interior)
    {
        snapshot.major = InferMajorLocationFromCellIdentifier(snapshot.cell);
    }
    // Inside a building, the specific place IS the building — name it from the
    // game's cell display name (robust, non-hardcoded). Outside, keep the nearest
    // landmark. `inside_or_outside` (below) then disambiguates the two.
    if (snapshot.interior)
    {
        snapshot.minor = InteriorBuildingName(cell);
    }
    else
    {
        snapshot.minor = FindNearestLocalMapLocation(player);
    }
    if (snapshot.minor.empty())
    {
        snapshot.minor = InferMinorLocationFromCellIdentifier(snapshot.cell);
    }

    return snapshot;
}

std::string EscapeForDiag(const std::string& value)
{
    std::string out = value;
    out = ReplaceAll(out, '\r', ' ');
    out = ReplaceAll(out, '\n', ' ');
    return out;
}

std::string ToUiAscii(std::string_view value)
{
    std::string out;
    out.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch < 0x80)
        {
            out.push_back(static_cast<char>(ch));
            continue;
        }

        if (i + 2 < value.size() && ch == 0xE2)
        {
            const unsigned char b1 = static_cast<unsigned char>(value[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(value[i + 2]);
            if (b1 == 0x80 && (b2 == 0x93 || b2 == 0x94))
            {
                out.push_back('-');
                i += 2;
                continue;
            }
            if (b1 == 0x80 && (b2 == 0x98 || b2 == 0x99))
            {
                out.push_back('\'');
                i += 2;
                continue;
            }
            if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D))
            {
                out.push_back('"');
                i += 2;
                continue;
            }
            if (b1 == 0x80 && b2 == 0xA6)
            {
                out += "...";
                i += 2;
                continue;
            }
        }

        if (i + 1 < value.size() && ch == 0xC2 && static_cast<unsigned char>(value[i + 1]) == 0xA0)
        {
            out.push_back(' ');
            i += 1;
            continue;
        }

        out.push_back('?');
    }

    return out;
}

// =====================================================================================
// HTTP dialogue-turn transport (opt-in via DebugConfig.transport = http).
//
// Design / threading contract:
//   * A WinHTTP POST to chasm's /api/game/v1/turn blocks while the LLM/TTS stream the
//     NDJSON response, so it MUST NOT run on the game thread. A single persistent
//     worker thread (g_httpWorkerThread) owns all blocking HTTP work.
//   * The worker NEVER touches game state, DirectSound, the filesystem reply/chunk
//     files, or any g_state field directly other than through the dedicated, mutex-
//     guarded inbox below. It only:
//       - parses each NDJSON line,
//       - base64-decodes audio and stages the WAV to a temp file under AudioDir()
//         (filesystem write to a private staging dir is thread-safe here; the main
//         thread only ever reads these files by the path the worker hands it),
//       - pushes QueuedAudioChunk / pending reply / pending action entries into
//         g_httpInbox under g_httpMutex.
//   * OnMainGameLoop drains g_httpInbox each frame (DrainHttpInbox) and feeds the
//     SAME playback / reply / action code paths the file transport uses. Only the
//     SOURCE of the queue entries changes.
//   * A monotonically increasing generation id (g_httpActiveGeneration) tags the in-
//     flight turn. WriteRequest bumps it before dispatch; any worker output whose
//     generation does not match the current one is discarded (covers interrupts /
//     a new turn started before the previous finished).
// =====================================================================================

struct HttpTurnRequest
{
    unsigned long long generation = 0;
    std::string requestId;
    std::string npcKey;
    std::string npcName;
    std::string url;          // full https/http URL to POST to
    std::string body;         // JSON request body
    bool nonPositionalHint = false; // admin/Todd => player-centered audio fallback
};

// A reply event captured from the stream, mirroring ResponsePayload's terminal fields.
struct HttpPendingReply
{
    bool ok = false;
    std::string requestId;
    std::string npcKey;
    std::string npcName;
    std::string text;
    std::string error;
    std::string playerText;
    std::string audioFile;     // staged temp WAV filename (under AudioDir()), if any
    bool nonPositionalAudio = false;
    std::string gameMasterAction;
    double gameMasterConfidence = 0.0;
    bool gameMasterShouldTrigger = false;
    std::string actionNpcKey;
    std::string actionNpcName;
};

// An action event captured from the stream (fired by the client; queued:false).
struct HttpPendingAction
{
    std::string requestId;
    std::string npcKey;
    std::string npcName;
    std::string action;
    double confidence = 0.0;
    bool shouldTrigger = true;
    std::string actionId;
    std::string actionNpcKey;
    std::string actionNpcName;
};

struct HttpInbox
{
    std::deque<QueuedAudioChunk> audioChunks;
    std::deque<HttpPendingAction> actions;
    std::optional<HttpPendingReply> reply;     // terminal reply (set once per turn)
    std::vector<std::string> partialCaptions;   // speech.delta text (recognized/streamed)
    std::string recognizedPlayerText;           // transcript surfaced by the stream
    bool sawActivity = false;                    // any event arrived (drives activity ticks)
    bool finished = false;                       // turn.completed / turn.error / transport end
};

std::mutex g_httpMutex;
HttpInbox g_httpInbox;                                  // guarded by g_httpMutex
unsigned long long g_httpActiveGeneration = 0;          // guarded by g_httpMutex

// Worker plumbing.
std::thread g_httpWorkerThread;
std::mutex g_httpJobMutex;
std::condition_variable g_httpJobCv;
std::optional<HttpTurnRequest> g_httpPendingJob;        // guarded by g_httpJobMutex
std::atomic<bool> g_httpWorkerStop{ false };
std::atomic<bool> g_httpWorkerStarted{ false };
std::atomic<int> g_httpChunkSequence{ 0 };              // unique temp WAV filenames

std::wstring Utf8ToWide(const std::string& input)
{
    if (input.empty())
    {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0)
    {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), out.data(), needed);
    return out;
}

// Stage a decoded WAV blob to a private temp file under AudioDir() and return its
// filename (relative to AudioDir(), matching how QueuedAudioChunk.wavPath is built).
// Returns empty on failure. Runs on the WORKER thread.
std::string StageHttpAudioWav(const std::string& requestId, int chunkIndex, const std::string& wavBytes)
{
    std::error_code ec;
    fs::create_directories(AudioDir(), ec);
    const int seq = g_httpChunkSequence.fetch_add(1);
    std::ostringstream name;
    name << "http_" << requestId << "_" << chunkIndex << "_" << seq << ".wav";
    const std::string filename = name.str();
    const fs::path path = AudioDir() / filename;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        LogLine("HTTP transport failed to open staging WAV: %s", path.string().c_str());
        return std::string();
    }
    out.write(wavBytes.data(), static_cast<std::streamsize>(wavBytes.size()));
    out.flush();
    if (!out.good())
    {
        LogLine("HTTP transport failed while writing staging WAV: %s", path.string().c_str());
        std::error_code rmEc;
        fs::remove(path, rmEc);
        return std::string();
    }
    return filename;
}

// Parse ONE NDJSON event line and push results into g_httpInbox (under g_httpMutex).
// `generation` is the turn this worker is serving; if it no longer matches the active
// generation the results are dropped. Runs on the WORKER thread.
void HandleHttpTurnEvent(const std::string& line, unsigned long long generation, const HttpTurnRequest& job)
{
    std::string type;
    if (!JsonGetString(line, "type", type))
    {
        return;
    }

    if (type == "speech.delta")
    {
        std::string text;
        JsonGetString(line, "text", text);
        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            return;
        }
        g_httpInbox.sawActivity = true;
        if (!text.empty())
        {
            g_httpInbox.partialCaptions.push_back(text);
        }
        return;
    }

    if (type == "audio.chunk")
    {
        double indexValue = 0.0;
        const int chunkIndex = JsonGetNumber(line, "index", indexValue) ? static_cast<int>(indexValue) : -1;
        std::string base64Wav;
        std::string audioObj;
        if (JsonGetObject(line, "audio", audioObj))
        {
            JsonGetString(audioObj, "data", base64Wav);
        }
        if (base64Wav.empty())
        {
            // Tolerate a flat "audio":"<base64>" shape as well.
            JsonGetString(line, "audio", base64Wav);
        }
        if (base64Wav.empty())
        {
            return;
        }

        const auto decoded = DecodeBase64String(base64Wav, 32ull * 1024ull * 1024ull);
        if (!decoded.has_value() || decoded->empty())
        {
            LogLine("HTTP transport dropped an audio.chunk with undecodable/oversized data.");
            return;
        }

        std::string caption = JsonGetStringOr(line, "text");
        std::string speakerKey = JsonGetStringOr(line, "npcKey", job.npcKey);
        std::string speakerName = JsonGetStringOr(line, "npcName", job.npcName);
        double captionMaxValue = 0.0;
        const int captionMaxChars = JsonGetNumber(line, "captionMaxChars", captionMaxValue)
            ? static_cast<int>(captionMaxValue)
            : -1;
        bool nonPositional = ToLowerAscii(speakerKey) == "todd" || job.nonPositionalHint;
        std::string metaObj;
        if (JsonGetObject(line, "metadata", metaObj))
        {
            nonPositional = JsonGetBool(metaObj, "non_positional_audio", nonPositional);
            nonPositional = JsonGetBool(metaObj, "admin_voice", nonPositional);
        }

        const std::string filename = StageHttpAudioWav(job.requestId, chunkIndex, *decoded);
        if (filename.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            // Stale (turn was superseded). Remove the staged file we just wrote.
            std::error_code rmEc;
            fs::remove(AudioDir() / filename, rmEc);
            return;
        }
        g_httpInbox.sawActivity = true;
        g_httpInbox.audioChunks.push_back(QueuedAudioChunk{
            job.requestId,
            AudioDir() / filename,
            filename,
            speakerKey,
            speakerName,
            caption,
            "",
            chunkIndex,
            nonPositional,
            captionMaxChars,
        });
        return;
    }

    if (type == "action")
    {
        HttpPendingAction action;
        action.requestId = job.requestId;
        action.npcKey = job.npcKey;
        action.npcName = job.npcName;
        action.action = ToUpperAscii(JsonGetStringOr(line, "action"));
        double confidence = 0.0;
        action.confidence = JsonGetNumber(line, "confidence", confidence) ? confidence : 0.0;
        action.shouldTrigger = JsonGetBool(line, "shouldTrigger", true);
        action.actionId = JsonGetStringOr(line, "actionId");
        std::string actorObj;
        if (JsonGetObject(line, "actor", actorObj))
        {
            action.actionNpcKey = JsonGetStringOr(actorObj, "npcKey");
            action.actionNpcName = JsonGetStringOr(actorObj, "npcName");
        }
        if (action.action.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            return;
        }
        g_httpInbox.sawActivity = true;
        g_httpInbox.actions.push_back(std::move(action));
        return;
    }

    if (type == "reply")
    {
        HttpPendingReply reply;
        double status = 0.0;
        const bool hasStatus = JsonGetNumber(line, "status", status);
        reply.ok = hasStatus ? (static_cast<int>(status) == 1 || static_cast<int>(status) == 2) : true;
        reply.requestId = JsonGetStringOr(line, "requestId", job.requestId);
        reply.npcKey = JsonGetStringOr(line, "npcKey", job.npcKey);
        reply.npcName = JsonGetStringOr(line, "npcName", job.npcName);
        reply.text = JsonGetStringOr(line, "text");
        reply.error = JsonGetStringOr(line, "error");
        reply.playerText = JsonGetStringOr(line, "playerText");
        reply.nonPositionalAudio = ToLowerAscii(reply.npcKey) == "todd" || job.nonPositionalHint;
        std::string gmObj;
        if (JsonGetObject(line, "gameMaster", gmObj))
        {
            reply.gameMasterAction = ToUpperAscii(JsonGetStringOr(gmObj, "action"));
            double conf = 0.0;
            reply.gameMasterConfidence = JsonGetNumber(gmObj, "confidence", conf) ? conf : 0.0;
            reply.gameMasterShouldTrigger = JsonGetBool(gmObj, "shouldTrigger", false);
            reply.actionNpcKey = JsonGetStringOr(gmObj, "npcKey");
            reply.actionNpcName = JsonGetStringOr(gmObj, "npcName");
        }
        if (!reply.error.empty() && !hasStatus)
        {
            reply.ok = false;
        }
        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            return;
        }
        g_httpInbox.sawActivity = true;
        if (!reply.playerText.empty())
        {
            g_httpInbox.recognizedPlayerText = reply.playerText;
        }
        g_httpInbox.reply = std::move(reply);
        return;
    }

    if (type == "turn.completed")
    {
        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            return;
        }
        g_httpInbox.sawActivity = true;
        g_httpInbox.finished = true;
        return;
    }

    if (type == "turn.error")
    {
        const std::string error = JsonGetStringOr(line, "error", "turn error");
        std::lock_guard<std::mutex> lock(g_httpMutex);
        if (generation != g_httpActiveGeneration)
        {
            return;
        }
        g_httpInbox.sawActivity = true;
        if (!g_httpInbox.reply.has_value())
        {
            HttpPendingReply reply;
            reply.ok = false;
            reply.requestId = JsonGetStringOr(line, "requestId", job.requestId);
            reply.npcKey = job.npcKey;
            reply.npcName = job.npcName;
            reply.error = error;
            g_httpInbox.reply = std::move(reply);
        }
        g_httpInbox.finished = true;
        return;
    }
}

// Record a transport-level failure (unreachable host, dropped connection) as a
// status-0 reply so the main thread surfaces it gracefully. Runs on the WORKER thread.
void RecordHttpTransportFailure(unsigned long long generation, const HttpTurnRequest& job, const std::string& error)
{
    std::lock_guard<std::mutex> lock(g_httpMutex);
    if (generation != g_httpActiveGeneration)
    {
        return;
    }
    if (!g_httpInbox.reply.has_value())
    {
        HttpPendingReply reply;
        reply.ok = false;
        reply.requestId = job.requestId;
        reply.npcKey = job.npcKey;
        reply.npcName = job.npcName;
        reply.error = error;
        g_httpInbox.reply = std::move(reply);
    }
    g_httpInbox.finished = true;
}

// Perform the blocking POST + stream the NDJSON body line-by-line. Runs on the WORKER
// thread. Never throws into the worker loop; all failures route through
// RecordHttpTransportFailure.
void RunHttpTurn(const HttpTurnRequest& job)
{
    const unsigned long long generation = job.generation;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[2048] = { 0 };
    components.lpszHostName = hostName;
    components.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
    components.lpszUrlPath = urlPath;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));

    const std::wstring wideUrl = Utf8ToWide(job.url);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components))
    {
        RecordHttpTransportFailure(generation, job, "Bridge URL parse failed.");
        return;
    }

    HINTERNET session = WinHttpOpen(L"FNVBridgeNative/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        RecordHttpTransportFailure(generation, job, "Bridge HTTP session open failed.");
        return;
    }

    // Bounded timeouts: the connect/send must be quick (chasm is local). Receive is
    // generous because TTS can take a while to stream, but still bounded so a hung
    // backend cannot wedge the worker forever.
    WinHttpSetTimeouts(session, 5000 /*resolve*/, 5000 /*connect*/, 10000 /*send*/, 120000 /*receive*/);

    HINTERNET connection = WinHttpConnect(session, components.lpszHostName, components.nPort, 0);
    if (!connection)
    {
        WinHttpCloseHandle(session);
        RecordHttpTransportFailure(generation, job, "Bridge connect failed (is chasm running?).");
        return;
    }

    const DWORD secureFlag = (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", components.lpszUrlPath,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secureFlag);
    if (!request)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        RecordHttpTransportFailure(generation, job, "Bridge request open failed.");
        return;
    }

    const wchar_t* headers = L"Content-Type: application/json\r\nAccept: application/x-ndjson\r\n";
    BOOL sent = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
        const_cast<char*>(job.body.data()), static_cast<DWORD>(job.body.size()),
        static_cast<DWORD>(job.body.size()), 0);
    if (!sent)
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        RecordHttpTransportFailure(generation, job, "Bridge send failed (is chasm running?).");
        return;
    }

    if (!WinHttpReceiveResponse(request, nullptr))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        RecordHttpTransportFailure(generation, job, "Bridge response receive failed.");
        return;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 0 && statusCode >= 400)
    {
        std::ostringstream err;
        err << "Bridge HTTP " << statusCode << ".";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        RecordHttpTransportFailure(generation, job, err.str());
        return;
    }

    // Stream the body, splitting on newlines as data arrives. Each complete line is a
    // standalone NDJSON event.
    std::string pending;
    bool transportError = false;
    for (;;)
    {
        if (g_httpWorkerStop.load())
        {
            transportError = true;
            break;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            transportError = true;
            break;
        }
        if (available == 0)
        {
            break; // end of response
        }
        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
        {
            transportError = true;
            break;
        }
        if (read == 0)
        {
            break;
        }
        pending.append(buffer.data(), read);

        size_t newlinePos;
        while ((newlinePos = pending.find('\n')) != std::string::npos)
        {
            std::string lineText = pending.substr(0, newlinePos);
            pending.erase(0, newlinePos + 1);
            if (!lineText.empty() && lineText.back() == '\r')
            {
                lineText.pop_back();
            }
            // Skip blank lines / whitespace-only keepalives.
            const std::string trimmed = Trim(lineText);
            if (trimmed.empty())
            {
                continue;
            }
            HandleHttpTurnEvent(trimmed, generation, job);
        }

        // If the turn was already marked finished by a terminal event, stop reading.
        {
            std::lock_guard<std::mutex> lock(g_httpMutex);
            if (generation == g_httpActiveGeneration && g_httpInbox.finished)
            {
                break;
            }
            if (generation != g_httpActiveGeneration)
            {
                break; // superseded
            }
        }
    }

    // Flush any trailing buffered line (response not newline-terminated).
    {
        const std::string trimmed = Trim(pending);
        if (!trimmed.empty())
        {
            HandleHttpTurnEvent(trimmed, generation, job);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (transportError)
    {
        RecordHttpTransportFailure(generation, job, "Bridge connection dropped.");
        return;
    }

    // Ensure the turn is terminated even if the server closed without a terminal event.
    std::lock_guard<std::mutex> lock(g_httpMutex);
    if (generation == g_httpActiveGeneration && !g_httpInbox.finished)
    {
        if (!g_httpInbox.reply.has_value())
        {
            HttpPendingReply reply;
            reply.ok = false;
            reply.requestId = job.requestId;
            reply.npcKey = job.npcKey;
            reply.npcName = job.npcName;
            reply.error = "Bridge closed without a reply.";
            g_httpInbox.reply = std::move(reply);
        }
        g_httpInbox.finished = true;
    }
}

void HttpWorkerMain()
{
    for (;;)
    {
        HttpTurnRequest job;
        {
            std::unique_lock<std::mutex> lock(g_httpJobMutex);
            g_httpJobCv.wait(lock, [] { return g_httpWorkerStop.load() || g_httpPendingJob.has_value(); });
            if (g_httpWorkerStop.load() && !g_httpPendingJob.has_value())
            {
                return;
            }
            job = std::move(*g_httpPendingJob);
            g_httpPendingJob.reset();
        }
        RunHttpTurn(job);
    }
}

void EnsureHttpWorkerStarted()
{
    if (g_httpWorkerStarted.load())
    {
        return;
    }
    g_httpWorkerStop.store(false);
    g_httpWorkerThread = std::thread(HttpWorkerMain);
    g_httpWorkerStarted.store(true);
}

void ShutdownHttpWorker()
{
    if (!g_httpWorkerStarted.load())
    {
        return;
    }
    g_httpWorkerStop.store(true);
    g_httpJobCv.notify_all();
    if (g_httpWorkerThread.joinable())
    {
        g_httpWorkerThread.join();
    }
    g_httpWorkerStarted.store(false);
}

// Build the JSON request body for a turn. `audioBase64` is empty for typed text and a
// base64 WAV for push-to-talk voice input.
std::string BuildHttpTurnBody(const std::string& requestId, const std::string& npcKey, const std::string& npcName,
    const std::string& playerText, const LocationSnapshot& location, const std::string& metadataJson,
    const std::string& audioBase64)
{
    std::ostringstream body;
    body << "{";
    body << "\"request_id\":" << JsonEscape(requestId);
    body << ",\"npc_key\":" << JsonEscape(npcKey);
    body << ",\"npc_name\":" << JsonEscape(npcName);
    body << ",\"player_text\":" << JsonEscape(playerText);
    body << ",\"want_tts\":true";
    body << ",\"location\":{";
    body << "\"cell\":" << JsonEscape(location.cell);
    body << ",\"worldspace\":" << JsonEscape(location.worldspace);
    body << ",\"region\":" << JsonEscape(location.region);
    body << ",\"major\":" << JsonEscape(location.major);
    body << ",\"minor\":" << JsonEscape(location.minor);
    body << "}";
    // metadataJson is already a JSON object literal (or empty). Embed it verbatim.
    const std::string trimmedMeta = Trim(metadataJson);
    if (!trimmedMeta.empty() && trimmedMeta.front() == '{')
    {
        body << ",\"metadata\":" << trimmedMeta;
    }
    else
    {
        body << ",\"metadata\":{}";
    }
    if (!audioBase64.empty())
    {
        body << ",\"audio_base64\":" << JsonEscape(audioBase64);
    }
    body << "}";
    return body.str();
}

// Reset the inbox and dispatch a turn to the worker. Bumps the active generation so any
// in-flight worker output for a prior turn is discarded. Runs on the GAME thread.
void DispatchHttpTurn(const std::string& requestId, const std::string& npcKey, const std::string& npcName,
    const std::string& body, bool nonPositionalHint)
{
    EnsureHttpWorkerStarted();

    unsigned long long generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_httpMutex);
        generation = ++g_httpActiveGeneration;
        g_httpInbox = HttpInbox{}; // clear any leftover state from a prior turn
    }

    HttpTurnRequest job;
    job.generation = generation;
    job.requestId = requestId;
    job.npcKey = npcKey;
    job.npcName = npcName;
    job.nonPositionalHint = nonPositionalHint;
    std::ostringstream url;
    url << "http://" << g_debugConfig.httpHost << ":" << g_debugConfig.httpPort << g_debugConfig.httpTurnPath;
    job.url = url.str();
    job.body = body;

    {
        std::lock_guard<std::mutex> lock(g_httpJobMutex);
        g_httpPendingJob = std::move(job);
    }
    g_httpJobCv.notify_one();

    LogLine("HTTP turn dispatched for %s (%s) to %s.", npcName.c_str(), requestId.c_str(), url.str().c_str());
}

// Cancel any in-flight HTTP turn: bump the generation (so worker output is discarded)
// and clear the inbox. The worker will notice the generation change and stop reading.
// Runs on the GAME thread (called from reply teardown / interrupts / resets).
void CancelHttpTurn()
{
    std::lock_guard<std::mutex> lock(g_httpMutex);
    ++g_httpActiveGeneration;
    g_httpInbox = HttpInbox{};
}

bool WriteRequest(const std::string& npcKey, const std::string& npcName, const std::string& text, const LocationSnapshot& location, const std::string& metadataJson, bool clearSpeechSidecar, const std::vector<BYTE>* httpVoiceWav, bool nonPositionalHint)
{
    EnsureBridgeDirectories();
    // Event log: one conversation marker per NPC per window (the transcript
    // itself already lives in chasm — this is just the "we talked" beat).
    NoteConversationRequestForEventLog(npcKey, npcName);
    StopSpeechAnimation();
    ClearDialogSubtitle();
    g_state.activeRequestId = GenerateRequestId();
    EnsureTraceContext(g_state.activeRequestId);
    g_state.lastAudioChunkIndex = -1;
    g_state.subtitleShownForReply = false;
    g_state.activeSpeechUntilTick = 0;
    g_state.replySubtitleText.clear();
    g_state.streamedAudioSeenForReply = false;
    g_state.pendingAudioChunks.clear();
    StopStreamingVoice("reply_state_reset");
    g_state.replyStartedTick = GetTickCount();
    g_state.lastBridgeActivityTick = g_state.replyStartedTick;
    g_state.sawBridgeActivity = false;

    // HTTP transport: POST the turn to chasm and let the worker stream the NDJSON
    // response into g_httpInbox, which OnMainGameLoop drains. We deliberately do NOT
    // write any NVBridge request/reply/chunk files in this mode (the per-frame file
    // polls are also skipped while transport == http).
    if (g_debugConfig.transport == BridgeTransport::Http)
    {
        std::error_code httpEc;
        // Clear stale staged WAVs from prior HTTP turns so AudioDir() does not grow.
        if (fs::exists(AudioDir(), httpEc))
        {
            for (const auto& entry : fs::directory_iterator(AudioDir(), httpEc))
            {
                if (entry.is_regular_file() && entry.path().filename().string().rfind("http_", 0) == 0)
                {
                    fs::remove(entry.path(), httpEc);
                }
            }
        }

        std::string audioBase64;
        if (httpVoiceWav && !httpVoiceWav->empty())
        {
            audioBase64 = EncodeBase64(httpVoiceWav->data(), httpVoiceWav->size());
        }

        const std::string body = BuildHttpTurnBody(g_state.activeRequestId, npcKey, npcName, text, location, metadataJson, audioBase64);
        DispatchHttpTurn(g_state.activeRequestId, npcKey, npcName, body, nonPositionalHint);

        TraceRequestEvent(g_state.activeRequestId, "http_turn_dispatched",
            {
                { "npc_key", npcKey },
                { "npc_name", npcName },
                { "location_major", location.major },
                { "location_minor", location.minor },
                { "location_cell", location.cell },
                { "has_targeting_metadata", metadataJson.find("\"targeting\"") == std::string::npos ? "0" : "1" },
                { "has_macros_metadata", metadataJson.find("\"macros\"") == std::string::npos ? "0" : "1" },
                { "has_voice_audio", audioBase64.empty() ? "0" : "1" },
            },
            {
                { "player_text_length", static_cast<double>(text.size()) },
            });
        WriteRuntimeHeartbeatIfNeeded(true);
        return true;
    }

    std::error_code ec;
    fs::remove(InboxPath(), ec);
    if (clearSpeechSidecar)
    {
        fs::remove(SttInboxAudioPath(), ec);
    }
    fs::remove(OutboxPath(), ec);
    if (fs::exists(OutboxChunkDir()))
    {
        for (const auto& entry : fs::directory_iterator(OutboxChunkDir(), ec))
        {
            fs::remove(entry.path(), ec);
        }
    }

    std::ofstream out(InboxPath(), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        LogLine("Failed to open inbox path for request.");
        return false;
    }

    out << g_state.activeRequestId << "\r\n";
    out << SanitizeLine(npcKey) << "\r\n";
    out << SanitizeLine(npcName) << "\r\n";
    out << "1\r\n";
    out << SanitizeLine(text) << "\r\n";
    out << SanitizeLine(location.cell) << "\r\n";
    out << SanitizeLine(location.worldspace) << "\r\n";
    out << SanitizeLine(location.region) << "\r\n";
    out << SanitizeLine(location.major) << "\r\n";
    out << SanitizeLine(location.minor) << "\r\n";
    out << SanitizeLine(metadataJson) << "\r\n";
    out.flush();

    if (!out.good())
    {
        LogLine("Failed while writing request file.");
        return false;
    }

    TraceRequestEvent(g_state.activeRequestId, "request_file_written",
        {
            { "npc_key", npcKey },
            { "npc_name", npcName },
            { "location_major", location.major },
            { "location_minor", location.minor },
            { "location_cell", location.cell },
            { "has_targeting_metadata", metadataJson.empty() ? "0" : "1" },
        },
        {
            { "player_text_length", static_cast<double>(text.size()) },
        });
    WriteRuntimeHeartbeatIfNeeded(true);

    return true;
}

bool WriteVoiceRequest(const std::string& npcKey, const std::string& npcName, const SpeakerSnapshot& speaker, const std::vector<BYTE>& wavBytes, const LocationSnapshot& location, bool adminMode)
{
    if (wavBytes.empty())
    {
        LogLine("Voice request write skipped because WAV payload was empty.");
        return false;
    }

    const std::string metadataJson = adminMode
        ? BuildAdminVoiceRequestMetadata(GetPlayer())
        : BuildTextRequestMetadata(GetPlayer(), &speaker, &location);

    // HTTP transport: send the captured WAV inline as audio_base64 instead of writing
    // the .stt.wav sidecar; the server transcribes it (player_text stays empty).
    if (g_debugConfig.transport == BridgeTransport::Http)
    {
        if (!WriteRequest(npcKey, npcName, "", location, metadataJson, false, &wavBytes, adminMode))
        {
            return false;
        }

        TraceRequestEvent(g_state.activeRequestId, "voice_request_http_dispatched",
            {
                { "npc_key", npcKey },
                { "npc_name", npcName },
                { "location_major", location.major },
                { "location_minor", location.minor },
                { "voice_target", adminMode ? "admin_todd" : "live_chat" },
            },
            {
                { "audio_size_bytes", static_cast<double>(wavBytes.size()) },
                { "speaker_ref_id", static_cast<double>(speaker.refId) },
            });
        return true;
    }

    std::ofstream audioOut(SttInboxAudioPath(), std::ios::binary | std::ios::trunc);
    if (!audioOut)
    {
        LogLine("Failed to open STT audio sidecar path for request.");
        return false;
    }

    audioOut.write(reinterpret_cast<const char*>(wavBytes.data()), static_cast<std::streamsize>(wavBytes.size()));
    audioOut.flush();
    if (!audioOut.good())
    {
        LogLine("Failed while writing STT audio sidecar.");
        return false;
    }

    if (!WriteRequest(npcKey, npcName, "", location, metadataJson, false))
    {
        std::error_code ec;
        fs::remove(SttInboxAudioPath(), ec);
        return false;
    }

    TraceRequestEvent(g_state.activeRequestId, "voice_request_audio_written",
        {
            { "npc_key", npcKey },
            { "npc_name", npcName },
            { "location_major", location.major },
            { "location_minor", location.minor },
            { "voice_target", adminMode ? "admin_todd" : "live_chat" },
        },
        {
            { "audio_size_bytes", static_cast<double>(wavBytes.size()) },
            { "speaker_ref_id", static_cast<double>(speaker.refId) },
        });

    return true;
}

std::optional<ResponsePayload> ReadResponse()
{
    const fs::path path = OutboxPath();
    if (!fs::exists(path))
    {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogLine("Outbox exists but could not be opened.");
        return std::nullopt;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }

    if (lines.size() < 7)
    {
        return std::nullopt;
    }

    ResponsePayload payload{};
    const std::string statusToken = Trim(lines[0]);
    payload.statusCode = statusToken.empty() ? 0 : std::atoi(statusToken.c_str());
    payload.isFinal = payload.statusCode != 2;
    payload.ok = payload.statusCode == 1 || payload.statusCode == 2;
    payload.requestId = Trim(lines[1]);
    payload.npcKey = Trim(lines[2]);
    payload.npcName = Trim(lines[3]);
    payload.audioFile = Trim(lines[4]);
    payload.text = Trim(lines[5]);
    payload.error = Trim(lines[6]);
    payload.playerText = lines.size() > 8 ? Trim(lines[8]) : "";

    size_t responseIndex = 9;
    bool chunkIndexSeen = false;
    while (responseIndex < lines.size())
    {
        const std::string token = Trim(lines[responseIndex]);
        const size_t equals = token.find('=');
        if (!chunkIndexSeen && IsIntegerToken(token))
        {
            payload.audioChunkIndex = std::atoi(token.c_str());
            chunkIndexSeen = true;
            ++responseIndex;
            continue;
        }
        if (equals != std::string::npos)
        {
            ApplyResponseMetadata(payload, token.substr(0, equals), token.substr(equals + 1));
            ++responseIndex;
            continue;
        }
        break;
    }

    payload.gameMasterAction = responseIndex < lines.size() ? Trim(lines[responseIndex]) : "";
    payload.gameMasterConfidence = responseIndex + 1 < lines.size() ? std::atof(Trim(lines[responseIndex + 1]).c_str()) : 0.0;
    payload.gameMasterShouldTrigger = responseIndex + 2 < lines.size() && Trim(lines[responseIndex + 2]) == "1";

    if (!g_state.activeRequestId.empty() && payload.requestId != g_state.activeRequestId)
    {
        ClearOutboxArtifacts("stale_response");
        LogLine("Ignoring stale outbox response for request %s while awaiting %s.", payload.requestId.c_str(), g_state.activeRequestId.c_str());
        return std::nullopt;
    }

    return payload;
}

std::optional<ResponsePayload> ReadNativeActionCommand(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogLine("Native action command exists but could not be opened: %s", path.string().c_str());
        return std::nullopt;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }

    if (!lines.empty() && Trim(lines[0]) == kNativeActionCommandVersion2)
    {
        const auto fields = ParseKeyValueLines(lines, 1);
        ResponsePayload payload{};
        payload.statusCode = 1;
        payload.ok = true;
        payload.isFinal = true;
        payload.requestId = Trim(GetField(fields, "request_id"));
        if (payload.requestId.empty())
        {
            payload.requestId = path.stem().string();
        }
        payload.npcKey = Trim(GetField(fields, "npc_key"));
        payload.npcName = Trim(GetField(fields, "npc_name"));
        payload.actionNpcKey = payload.npcKey;
        payload.actionNpcName = payload.npcName;
        payload.gameMasterAction = ToUpperAscii(Trim(GetField(fields, "action")));
        if (payload.gameMasterAction.empty())
        {
            payload.gameMasterAction = "ACTION_BOOK";
        }
        payload.actionId = Trim(GetField(fields, "action_id"));
        payload.actionBookId = Trim(GetField(fields, "action_book_id"));
        payload.executionEngine = ToLowerAscii(Trim(GetField(fields, "engine")));
        payload.executionTemplateId = Trim(GetField(fields, "template_id"));
        payload.executionLanguage = ToLowerAscii(Trim(GetField(fields, "language")));
        payload.executionArguments = SplitCommaList(GetField(fields, "arguments"));
        payload.gameMasterConfidence = std::atof(Trim(GetField(fields, "confidence")).c_str());
        if (payload.gameMasterConfidence <= 0.0)
        {
            payload.gameMasterConfidence = 1.0;
        }
        payload.gameMasterShouldTrigger = true;
        payload.text = Trim(GetField(fields, "reason"));

        if (const auto decodedPlayerText = DecodeBase64String(GetField(fields, "player_text"), 16ull * 1024ull); decodedPlayerText.has_value())
        {
            payload.playerText = Trim(*decodedPlayerText);
        }
        if (const auto decodedScript = DecodeBase64String(GetField(fields, "script_base64"), kMaxTrustedExecutionScriptBytes); decodedScript.has_value())
        {
            payload.executionScript = *decodedScript;
        }
        else if (!GetField(fields, "script_base64").empty())
        {
            LogLine("Ignoring invalid or oversized trusted execution script in %s.", path.filename().string().c_str());
        }

        if (payload.npcKey.empty() && payload.npcName.empty())
        {
            LogLine("Ignoring native action command %s without an NPC identity.", path.filename().string().c_str());
            return std::nullopt;
        }

        return payload;
    }

    if (lines.size() < 4)
    {
        LogLine("Ignoring malformed native action command %s with %zu line(s).", path.filename().string().c_str(), lines.size());
        return std::nullopt;
    }

    ResponsePayload payload{};
    payload.statusCode = 1;
    payload.ok = true;
    payload.isFinal = true;
    payload.requestId = Trim(lines[0]);
    if (payload.requestId.empty())
    {
        payload.requestId = path.stem().string();
    }
    payload.npcKey = Trim(lines[1]);
    payload.npcName = Trim(lines[2]);
    payload.actionNpcKey = payload.npcKey;
    payload.actionNpcName = payload.npcName;
    payload.gameMasterAction = ToUpperAscii(Trim(lines[3]));
    payload.gameMasterConfidence = lines.size() > 4 ? std::atof(Trim(lines[4]).c_str()) : 1.0;
    payload.gameMasterShouldTrigger = true;
    payload.text = lines.size() > 5 ? Trim(lines[5]) : "";
    payload.playerText = lines.size() > 6 ? Trim(lines[6]) : "";

    if (payload.npcKey.empty() && payload.npcName.empty())
    {
        LogLine("Ignoring native action command %s without an NPC identity.", path.filename().string().c_str());
        return std::nullopt;
    }

    if (payload.gameMasterAction != "ATTACK" && payload.gameMasterAction != "FOLLOW" && payload.gameMasterAction != "STOP_FOLLOW")
    {
        LogLine("Ignoring unsupported native action command %s for %s.", payload.gameMasterAction.c_str(), payload.npcName.c_str());
        return std::nullopt;
    }

    return payload;
}

void PollNativeActionCommands()
{
    const fs::path directory = NativeActionCommandDir();
    if (!fs::exists(directory))
    {
        return;
    }

    std::error_code iterEc;
    for (const auto& entry : fs::directory_iterator(directory, iterEc))
    {
        if (iterEc)
        {
            LogLine("Failed while iterating native action command directory: %s", iterEc.message().c_str());
            break;
        }

        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc))
        {
            continue;
        }

        const std::string extension = ToLowerAscii(entry.path().extension().string());
        if (extension != ".txt")
        {
            continue;
        }

        const auto command = ReadNativeActionCommand(entry.path());
        if (command.has_value())
        {
            EnsureTraceContext(command->requestId);
            std::string triggeredAction;
            const bool triggered = TriggerGameMasterAction(*command, &triggeredAction);
            TraceRequestEvent(command->requestId, "native_action_command_processed",
                {
                    { "file", entry.path().filename().string() },
                    { "npc_key", command->npcKey },
                    { "npc_name", command->npcName },
                    { "game_master_action", command->gameMasterAction },
                    { "action_id", command->actionId },
                    { "action_book_id", command->actionBookId },
                    { "template_id", command->executionTemplateId },
                    { "engine", command->executionEngine },
                    { "triggered_action", triggeredAction },
                },
                {
                    { "game_master_confidence", command->gameMasterConfidence },
                },
                {
                    { "triggered", triggered },
                });
            LogLine("Native action command %s for %s %s.", command->gameMasterAction.c_str(), command->npcName.c_str(), triggered ? "triggered" : "did not trigger");
        }

        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (removeEc)
        {
            LogLine("Failed to remove native action command %s: %s", entry.path().string().c_str(), removeEc.message().c_str());
        }
    }
}

std::optional<std::string> ReadSubmittedInput()
{
    const fs::path path = UiSubmitPath();
    if (!fs::exists(path))
    {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogLine("Submit file exists but could not be opened.");
        return std::nullopt;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
    {
        text.pop_back();
    }

    return Trim(text);
}

struct WavData
{
    WAVEFORMATEX format{};
    std::vector<BYTE> pcmData;
};

bool ReadUInt32LE(const std::vector<BYTE>& data, size_t offset, UInt32& value)
{
    if (offset + 4 > data.size())
    {
        return false;
    }

    value = static_cast<UInt32>(data[offset]) |
        (static_cast<UInt32>(data[offset + 1]) << 8) |
        (static_cast<UInt32>(data[offset + 2]) << 16) |
        (static_cast<UInt32>(data[offset + 3]) << 24);
    return true;
}

bool ReadUInt16LE(const std::vector<BYTE>& data, size_t offset, UInt16& value)
{
    if (offset + 2 > data.size())
    {
        return false;
    }

    value = static_cast<UInt16>(data[offset]) |
        (static_cast<UInt16>(data[offset + 1]) << 8);
    return true;
}

UInt16 BytesPerAudioSample(UInt16 bitsPerSample)
{
    if (bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32 || bitsPerSample == 64)
    {
        return static_cast<UInt16>(bitsPerSample / 8);
    }

    return 0;
}

WORD ResolveWaveFormatTag(const std::vector<BYTE>& bytes, size_t chunkData, UInt32 chunkSize, const WAVEFORMATEX& format)
{
    if (format.wFormatTag != kWaveFormatExtensible || chunkSize < 40)
    {
        return format.wFormatTag;
    }

    UInt32 subFormat = 0;
    if (!ReadUInt32LE(bytes, chunkData + 24, subFormat))
    {
        return format.wFormatTag;
    }

    const WORD subFormatTag = static_cast<WORD>(subFormat & 0xFFFF);
    if (subFormatTag == kWaveFormatPcm || subFormatTag == kWaveFormatIeeeFloat)
    {
        return subFormatTag;
    }

    return format.wFormatTag;
}

double DecodePcmSample(const BYTE* sample, UInt16 bitsPerSample)
{
    switch (bitsPerSample)
    {
    case 8:
        return (static_cast<int>(*sample) - 128) / 128.0;
    case 16:
    {
        const int value = static_cast<int>(sample[0]) | (static_cast<int>(sample[1]) << 8);
        const short signedValue = static_cast<short>(value);
        return static_cast<double>(signedValue) / 32768.0;
    }
    case 24:
    {
        UInt32 rawValue = static_cast<UInt32>(sample[0]) |
            (static_cast<UInt32>(sample[1]) << 8) |
            (static_cast<UInt32>(sample[2]) << 16);
        if (rawValue & 0x00800000)
        {
            rawValue |= 0xFF000000;
        }
        const SInt32 value = static_cast<SInt32>(rawValue);
        return static_cast<double>(value) / 8388608.0;
    }
    case 32:
    {
        const UInt32 rawValue = static_cast<UInt32>(sample[0]) |
            (static_cast<UInt32>(sample[1]) << 8) |
            (static_cast<UInt32>(sample[2]) << 16) |
            (static_cast<UInt32>(sample[3]) << 24);
        const SInt32 value = static_cast<SInt32>(rawValue);
        return static_cast<double>(value) / 2147483648.0;
    }
    default:
        return 0.0;
    }
}

double DecodeFloatSample(const BYTE* sample, UInt16 bitsPerSample)
{
    if (bitsPerSample == 32)
    {
        float value = 0.0f;
        std::memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? std::clamp(static_cast<double>(value), -1.0, 1.0) : 0.0;
    }

    if (bitsPerSample == 64)
    {
        double value = 0.0;
        std::memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.0, 1.0) : 0.0;
    }

    return 0.0;
}

std::vector<float> DecodeMonoSamples(const WAVEFORMATEX& format, WORD resolvedFormatTag, const std::vector<BYTE>& pcmData)
{
    std::vector<float> monoSamples;
    if (pcmData.empty() || !format.nChannels || !format.nSamplesPerSec)
    {
        return monoSamples;
    }

    const UInt16 bytesPerSample = BytesPerAudioSample(format.wBitsPerSample);
    if (!bytesPerSample)
    {
        return monoSamples;
    }

    const UInt16 fallbackBlockAlign = static_cast<UInt16>(format.nChannels * bytesPerSample);
    const UInt16 blockAlign = format.nBlockAlign >= fallbackBlockAlign ? format.nBlockAlign : fallbackBlockAlign;
    if (!blockAlign)
    {
        return monoSamples;
    }

    const size_t frameCount = pcmData.size() / blockAlign;
    monoSamples.reserve(frameCount);
    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        const BYTE* frame = pcmData.data() + (frameIndex * blockAlign);
        double sum = 0.0;
        for (UInt16 channel = 0; channel < format.nChannels; ++channel)
        {
            const size_t sampleOffset = static_cast<size_t>(channel) * bytesPerSample;
            if (sampleOffset + bytesPerSample > blockAlign)
            {
                continue;
            }

            const BYTE* sample = frame + sampleOffset;
            if (resolvedFormatTag == kWaveFormatPcm)
            {
                sum += DecodePcmSample(sample, format.wBitsPerSample);
            }
            else if (resolvedFormatTag == kWaveFormatIeeeFloat)
            {
                sum += DecodeFloatSample(sample, format.wBitsPerSample);
            }
        }

        monoSamples.push_back(static_cast<float>(std::clamp(sum / static_cast<double>(format.nChannels), -1.0, 1.0)));
    }

    return monoSamples;
}

std::vector<float> ResampleMonoSamples(const std::vector<float>& samples, DWORD sourceRate, DWORD targetRate)
{
    if (samples.empty() || !sourceRate || !targetRate || sourceRate == targetRate || samples.size() < 2)
    {
        return samples;
    }

    const double targetCountExact = static_cast<double>(samples.size()) * static_cast<double>(targetRate) / static_cast<double>(sourceRate);
    const size_t targetCount = (std::max<size_t>)(1, static_cast<size_t>(targetCountExact + 0.5));
    std::vector<float> output;
    output.reserve(targetCount);

    const double step = static_cast<double>(sourceRate) / static_cast<double>(targetRate);
    for (size_t index = 0; index < targetCount; ++index)
    {
        const double sourcePosition = static_cast<double>(index) * step;
        const size_t left = (std::min<size_t>)(static_cast<size_t>(sourcePosition), samples.size() - 1);
        const size_t right = (std::min<size_t>)(left + 1, samples.size() - 1);
        const double fraction = sourcePosition - static_cast<double>(left);
        const double value = (static_cast<double>(samples[left]) * (1.0 - fraction)) + (static_cast<double>(samples[right]) * fraction);
        output.push_back(static_cast<float>(std::clamp(value, -1.0, 1.0)));
    }

    return output;
}

void ConditionPlaybackSamples(std::vector<float>& samples, DWORD sampleRate)
{
    (void)sampleRate;
    if (samples.empty())
    {
        return;
    }

    // Per-chunk edge fades removed: these chunks are consecutive slices of ONE
    // continuous streaming buffer, so fading each chunk's first/last few ms ate the
    // soft onsets of words landing at chunk seams (and the line's very first word) —
    // most audible on PocketTTS, whose onsets ramp up gently from near-zero. Click
    // protection at the true start/end of a line is handled upstream by the TTS
    // server's lead-in / trailing silence pads. Only the loudness clamp remains.
    for (float& sample : samples)
    {
        sample = static_cast<float>(std::clamp(static_cast<double>(sample) * kVoicePlaybackHeadroom, -0.98, 0.98));
    }
}

std::vector<BYTE> EncodeInt16MonoSamples(const std::vector<float>& samples)
{
    std::vector<BYTE> pcm;
    pcm.resize(samples.size() * sizeof(short));
    for (size_t index = 0; index < samples.size(); ++index)
    {
        const double sample = std::clamp(static_cast<double>(samples[index]), -0.98, 0.98);
        const short value = static_cast<short>(std::lround(sample * 32767.0));
        pcm[index * 2] = static_cast<BYTE>(value & 0xFF);
        pcm[(index * 2) + 1] = static_cast<BYTE>((value >> 8) & 0xFF);
    }
    return pcm;
}

std::optional<WavData> PrepareWavForPlayback(const WAVEFORMATEX& sourceFormat, WORD resolvedFormatTag, const std::vector<BYTE>& sourcePcmData, const fs::path& path)
{
    if (resolvedFormatTag != kWaveFormatPcm && resolvedFormatTag != kWaveFormatIeeeFloat)
    {
        LogLine("Unsupported WAV format %u for game playback: %s", static_cast<unsigned>(resolvedFormatTag), path.string().c_str());
        return std::nullopt;
    }

    std::vector<float> monoSamples = DecodeMonoSamples(sourceFormat, resolvedFormatTag, sourcePcmData);
    if (monoSamples.empty())
    {
        LogLine("Could not decode WAV samples for game playback: %s", path.string().c_str());
        return std::nullopt;
    }

    monoSamples = ResampleMonoSamples(monoSamples, sourceFormat.nSamplesPerSec, kVoicePlaybackSampleRate);
    ConditionPlaybackSamples(monoSamples, kVoicePlaybackSampleRate);

    WavData data{};
    data.format.wFormatTag = WAVE_FORMAT_PCM;
    data.format.nChannels = kVoicePlaybackChannels;
    data.format.nSamplesPerSec = kVoicePlaybackSampleRate;
    data.format.wBitsPerSample = kVoicePlaybackBitsPerSample;
    data.format.nBlockAlign = static_cast<WORD>((data.format.nChannels * data.format.wBitsPerSample) / 8);
    data.format.nAvgBytesPerSec = data.format.nSamplesPerSec * data.format.nBlockAlign;
    data.format.cbSize = 0;
    data.pcmData = EncodeInt16MonoSamples(monoSamples);
    return data;
}

std::optional<WavData> LoadWavFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        LogLine("Could not open WAV file: %s", path.string().c_str());
        return std::nullopt;
    }

    // Block read (single read() call) rather than std::istreambuf_iterator: the
    // iterator extracts byte-by-byte (plus vector regrowth) and cost ~20 ms per
    // ~130 KB streamed chunk INSIDE the game process — a per-chunk frame hitch on
    // every 1.5 s slice. tellg()+read() is ~1 ms.
    const std::streamoff fileSize = in.tellg();
    if (fileSize <= 0)
    {
        LogLine("WAV file empty or unreadable: %s", path.string().c_str());
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::vector<BYTE> bytes(static_cast<size_t>(fileSize));
    in.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    bytes.resize(static_cast<size_t>(in.gcount()));
    if (bytes.size() < 44)
    {
        LogLine("WAV file too small: %s", path.string().c_str());
        return std::nullopt;
    }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        LogLine("Invalid WAV header: %s", path.string().c_str());
        return std::nullopt;
    }

    size_t offset = 12;
    std::optional<WAVEFORMATEX> format;
    WORD resolvedFormatTag = kWaveFormatPcm;
    std::vector<BYTE> pcmData;

    while (offset + 8 <= bytes.size())
    {
        const char* chunkId = reinterpret_cast<const char*>(bytes.data() + offset);
        UInt32 chunkSize = 0;
        if (!ReadUInt32LE(bytes, offset + 4, chunkSize))
        {
            break;
        }

        const size_t chunkData = offset + 8;
        if (chunkData + chunkSize > bytes.size())
        {
            break;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            WAVEFORMATEX fmt{};
            ReadUInt16LE(bytes, chunkData + 0, fmt.wFormatTag);
            ReadUInt16LE(bytes, chunkData + 2, fmt.nChannels);
            ReadUInt32LE(bytes, chunkData + 4, fmt.nSamplesPerSec);
            ReadUInt32LE(bytes, chunkData + 8, fmt.nAvgBytesPerSec);
            ReadUInt16LE(bytes, chunkData + 12, fmt.nBlockAlign);
            ReadUInt16LE(bytes, chunkData + 14, fmt.wBitsPerSample);
            fmt.cbSize = 0;
            if (chunkSize >= 18)
            {
                ReadUInt16LE(bytes, chunkData + 16, fmt.cbSize);
            }
            resolvedFormatTag = ResolveWaveFormatTag(bytes, chunkData, chunkSize, fmt);
            format = fmt;
        }
        else if (std::memcmp(chunkId, "data", 4) == 0)
        {
            pcmData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunkData),
                bytes.begin() + static_cast<std::ptrdiff_t>(chunkData + chunkSize));
        }

        offset = chunkData + chunkSize + (chunkSize % 2);
    }

    if (!format.has_value() || pcmData.empty())
    {
        LogLine("Missing WAV fmt/data chunk: %s", path.string().c_str());
        return std::nullopt;
    }

    return PrepareWavForPlayback(format.value(), resolvedFormatTag, pcmData, path);
}

DWORD GetWavDurationMs(const WavData& wavData)
{
    if (!wavData.format.nAvgBytesPerSec)
    {
        return 0;
    }

    const double seconds = static_cast<double>(wavData.pcmData.size()) / static_cast<double>(wavData.format.nAvgBytesPerSec);
    if (seconds <= 0.0)
    {
        return 0;
    }

    return static_cast<DWORD>((seconds * 1000.0) + 0.5);
}

bool EnsureDirectSound()
{
    if (g_state.directSound)
    {
        if (g_debugConfig.directSound3dEnabled && !g_state.listener3d)
        {
            ShutdownDirectSound();
        }
        else
        {
            return true;
        }
    }

    if (g_state.directSound)
    {
        return true;
    }

    HWND hwnd = GetGameWindow();
    if (!hwnd)
    {
        LogLine("Cannot initialize DirectSound without a game window.");
        return false;
    }

    IDirectSound8* directSound = nullptr;
    HRESULT hr = DirectSoundCreate8(nullptr, &directSound, nullptr);
    if (FAILED(hr) || !directSound)
    {
        LogLine("DirectSoundCreate8 failed: 0x%08X", hr);
        return false;
    }

    hr = directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    if (FAILED(hr))
    {
        LogLine("SetCooperativeLevel failed: 0x%08X", hr);
        directSound->Release();
        return false;
    }

    DSBUFFERDESC primaryDesc{};
    primaryDesc.dwSize = sizeof(primaryDesc);
    primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if (g_debugConfig.directSound3dEnabled)
    {
        primaryDesc.dwFlags |= DSBCAPS_CTRL3D;
    }

    IDirectSoundBuffer* primaryBuffer = nullptr;
    hr = directSound->CreateSoundBuffer(&primaryDesc, &primaryBuffer, nullptr);
    if (FAILED(hr) || !primaryBuffer)
    {
        LogLine("Primary CreateSoundBuffer failed: 0x%08X", hr);
        directSound->Release();
        return false;
    }

    WAVEFORMATEX primaryFormat{};
    primaryFormat.wFormatTag = WAVE_FORMAT_PCM;
    primaryFormat.nChannels = 2;
    primaryFormat.nSamplesPerSec = kVoicePlaybackSampleRate;
    primaryFormat.wBitsPerSample = kVoicePlaybackBitsPerSample;
    primaryFormat.nBlockAlign = static_cast<WORD>((primaryFormat.nChannels * primaryFormat.wBitsPerSample) / 8);
    primaryFormat.nAvgBytesPerSec = primaryFormat.nSamplesPerSec * primaryFormat.nBlockAlign;
    hr = primaryBuffer->SetFormat(&primaryFormat);
    if (FAILED(hr))
    {
        LogLine("Primary SetFormat failed, continuing with device default: 0x%08X", hr);
    }

    IDirectSound3DListener* listener3d = nullptr;
    if (g_debugConfig.directSound3dEnabled)
    {
        hr = primaryBuffer->QueryInterface(IID_IDirectSound3DListener, reinterpret_cast<void**>(&listener3d));
        if (FAILED(hr) || !listener3d)
        {
            LogLine("Primary QueryInterface(IDirectSound3DListener) failed: 0x%08X", hr);
            primaryBuffer->Release();
            directSound->Release();
            return false;
        }
    }

    g_state.directSound = directSound;
    g_state.primaryBuffer = primaryBuffer;
    g_state.listener3d = listener3d;
    return true;
}

void UpdateListener3d(const PlayerCharacter* player)
{
    if (!g_debugConfig.listenerUpdatesEnabled || !g_state.listener3d || !player)
    {
        return;
    }

    const float listenerX = player->posX / kGameUnitsPerMeter;
    const float listenerY = player->posY / kGameUnitsPerMeter;
    const float listenerZ = player->posZ / kGameUnitsPerMeter;
    const float forwardX = -std::sin(player->rotZ);
    const float forwardY = -std::cos(player->rotZ);
    g_state.listener3d->SetPosition(listenerX, listenerY, listenerZ, DS3D_IMMEDIATE);
    g_state.listener3d->SetOrientation(forwardX, forwardY, 0.0f, 0.0f, 0.0f, 1.0f, DS3D_IMMEDIATE);
}

void UpdateActiveSoundPositions()
{
    if (g_state.activeSounds.empty())
    {
        return;
    }

    const PlayerCharacter* player = GetPlayer();
    UpdateListener3d(player);

    if (!g_debugConfig.listenerUpdatesEnabled)
    {
        return;
    }

    for (auto& sound : g_state.activeSounds)
    {
        if (!sound.buffer || !sound.speaker.valid || !sound.speaker.refId)
        {
            continue;
        }

        TESObjectREFR* speakerRef = ResolveSpeakerRef(sound.speaker);
        if (!speakerRef || !speakerRef->GetNiNode())
        {
            // Speaker gone or unloaded (left the cell): leave the sound at its last
            // position rather than snapping it to a stale/editor position or a recycled
            // ref. ResolveSpeakerRef already refuses a FormID recycled to another form.
            continue;
        }

        const SpeakerSnapshot liveSpeaker = CaptureSpeakerSnapshot(speakerRef);
        if (!liveSpeaker.valid)
        {
            continue;
        }

        if (!sound.buffer3d)
        {
            continue;
        }

        const float emitterX = liveSpeaker.posX / kGameUnitsPerMeter;
        const float emitterY = liveSpeaker.posY / kGameUnitsPerMeter;
        const float emitterZ = liveSpeaker.posZ / kGameUnitsPerMeter;
        sound.buffer3d->SetPosition(emitterX, emitterY, emitterZ, DS3D_IMMEDIATE);
        sound.buffer->SetVolume(ComputeDistanceAttenuatedVolume(player, liveSpeaker));
        sound.speaker = liveSpeaker;
    }
}

void CleanupFinishedSounds()
{
    auto it = g_state.activeSounds.begin();
    while (it != g_state.activeSounds.end())
    {
        DWORD status = 0;
        if (!it->buffer || FAILED(it->buffer->GetStatus(&status)) || !(status & DSBSTATUS_PLAYING))
        {
            if (it->buffer)
            {
                it->buffer->Release();
                it->buffer = nullptr;
            }
            if (it->buffer3d)
            {
                it->buffer3d->Release();
                it->buffer3d = nullptr;
            }
            it = g_state.activeSounds.erase(it);
            continue;
        }
        ++it;
    }

    if (g_state.activeSounds.empty() && (g_state.listener3d || g_state.primaryBuffer || g_state.directSound))
    {
        const DWORD now = GetTickCount();
        const bool replyStillBusy = g_state.awaitingReply
            || !g_state.pendingAudioChunks.empty()
            || g_state.streamActive // Phase 3: the single streaming buffer lives outside
                                    // activeSounds; never release the device under it.
            || (g_state.activeSpeechUntilTick && now < g_state.activeSpeechUntilTick);
        if (replyStillBusy)
        {
            g_state.directSoundIdleSinceTick = 0;
            return;
        }
        if (!g_state.directSoundIdleSinceTick)
        {
            g_state.directSoundIdleSinceTick = now;
            return;
        }
        if (now - g_state.directSoundIdleSinceTick < kDirectSoundIdleReleaseDelayMs)
        {
            return;
        }

        g_state.streamedAudioSeenForReply = false;
        ShutdownDirectSound();
    }
}

bool PlayVoiceWav(const fs::path& wavPath, const SpeakerSnapshot& speaker, const WavData* preloadedWavData = nullptr, bool force2d = false)
{
    if (!EnsureDirectSound())
    {
        return false;
    }

    std::optional<WavData> loadedWavData;
    const WavData* wavData = preloadedWavData;
    if (!wavData)
    {
        loadedWavData = LoadWavFile(wavPath);
        if (!loadedWavData.has_value())
        {
            return false;
        }
        wavData = &*loadedWavData;
    }
    if (!wavData)
    {
        return false;
    }

    const bool use3d = g_debugConfig.directSound3dEnabled && !force2d;
    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    if (use3d)
    {
        desc.dwFlags |= DSBCAPS_CTRL3D;
    }
    if (g_debugConfig.directSoundSoftwareBufferEnabled)
    {
        desc.dwFlags |= DSBCAPS_LOCSOFTWARE;
    }
    desc.dwBufferBytes = static_cast<DWORD>(wavData->pcmData.size());
    desc.lpwfxFormat = const_cast<WAVEFORMATEX*>(&wavData->format);

    IDirectSoundBuffer* rawBuffer = nullptr;
    HRESULT hr = g_state.directSound->CreateSoundBuffer(&desc, &rawBuffer, nullptr);
    if (FAILED(hr) || !rawBuffer)
    {
        LogLine("CreateSoundBuffer failed: 0x%08X", hr);
        return false;
    }

    IDirectSoundBuffer8* buffer = nullptr;
    hr = rawBuffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&buffer));
    rawBuffer->Release();
    if (FAILED(hr) || !buffer)
    {
        LogLine("QueryInterface(IDirectSoundBuffer8) failed: 0x%08X", hr);
        return false;
    }

    void* lockPtr1 = nullptr;
    void* lockPtr2 = nullptr;
    DWORD lockSize1 = 0;
    DWORD lockSize2 = 0;
    hr = buffer->Lock(0, desc.dwBufferBytes, &lockPtr1, &lockSize1, &lockPtr2, &lockSize2, 0);
    if (FAILED(hr))
    {
        LogLine("DirectSound buffer lock failed: 0x%08X", hr);
        buffer->Release();
        return false;
    }

    std::memcpy(lockPtr1, wavData->pcmData.data(), lockSize1);
    if (lockPtr2 && lockSize2)
    {
        std::memcpy(lockPtr2, wavData->pcmData.data() + lockSize1, lockSize2);
    }
    buffer->Unlock(lockPtr1, lockSize1, lockPtr2, lockSize2);

    const PlayerCharacter* player = GetPlayer();
    if (use3d)
    {
        UpdateListener3d(player);
    }

    IDirectSound3DBuffer* buffer3d = nullptr;
    if (use3d)
    {
        hr = buffer->QueryInterface(IID_IDirectSound3DBuffer, reinterpret_cast<void**>(&buffer3d));
        if (FAILED(hr) || !buffer3d)
        {
            LogLine("QueryInterface(IDirectSound3DBuffer) failed: 0x%08X", hr);
            buffer->Release();
            return false;
        }
    }

    buffer->SetVolume(force2d ? DSBVOLUME_MAX : ComputeDistanceAttenuatedVolume(player, speaker));

    if (buffer3d && speaker.valid)
    {
        const float emitterX = speaker.posX / kGameUnitsPerMeter;
        const float emitterY = speaker.posY / kGameUnitsPerMeter;
        const float emitterZ = speaker.posZ / kGameUnitsPerMeter;
        buffer3d->SetPosition(emitterX, emitterY, emitterZ, DS3D_IMMEDIATE);
    }
    else if (buffer3d)
    {
        buffer3d->SetPosition(0.0f, 1.5f, 0.0f, DS3D_IMMEDIATE);
    }

    if (buffer3d)
    {
        buffer3d->SetMinDistance(kVoiceMinDistanceMeters, DS3D_IMMEDIATE);
        buffer3d->SetMaxDistance(kVoiceMaxDistanceMeters, DS3D_IMMEDIATE);
        buffer3d->SetMode(DS3DMODE_NORMAL, DS3D_IMMEDIATE);
    }
    buffer->SetCurrentPosition(0);

    hr = buffer->Play(0, 0, 0);
    if (FAILED(hr))
    {
        LogLine("DirectSound play failed: 0x%08X", hr);
        if (buffer3d)
        {
            buffer3d->Release();
        }
        buffer->Release();
        return false;
    }

    g_state.directSoundIdleSinceTick = 0;
    g_state.activeSounds.push_back(ActiveSound{ buffer, buffer3d, speaker });
    return true;
}

void ShutdownDirectSound()
{
    // Release the single streaming buffer first so it never dangles past its device
    // (e.g. if the device is torn down for any reason while a stream is live).
    if (g_state.streamActive || g_state.streamBuffer)
    {
        StopStreamingVoice("directsound_shutdown");
    }
    g_state.directSoundIdleSinceTick = 0;
    if (!g_state.traceRequestId.empty() && (g_state.listener3d || g_state.primaryBuffer || g_state.directSound))
    {
        TraceRequestEvent(g_state.traceRequestId, "directsound_shutdown",
            {},
            {},
            {
                { "had_listener", g_state.listener3d != nullptr },
                { "had_primary_buffer", g_state.primaryBuffer != nullptr },
                { "had_device", g_state.directSound != nullptr },
            });
    }
    if (g_state.listener3d)
    {
        g_state.listener3d->Release();
        g_state.listener3d = nullptr;
    }
    if (g_state.primaryBuffer)
    {
        g_state.primaryBuffer->Release();
        g_state.primaryBuffer = nullptr;
    }
    if (g_state.directSound)
    {
        g_state.directSound->Release();
        g_state.directSound = nullptr;
    }
    WriteRuntimeHeartbeatIfNeeded(true);
}

TESObjectREFR* ResolveSpeakerRef(const SpeakerSnapshot& speaker)
{
    if (!speaker.refId)
    {
        return nullptr;
    }

    TESForm* form = LookupFormByIdRuntime(speaker.refId);
    if (!form)
    {
        return nullptr;
    }

    // A FormID freed on cell unload can be RECYCLED by the engine for a different form.
    // Two guards so in-flight speech (audio/lip-sync/captions) never rebinds to the
    // wrong actor via a stale identity:
    //   1) The form must still be a placed reference (Character/Creature/REFR share the
    //      TESObjectREFR layout). If it recycled to a non-ref form, casting and reading
    //      posX/baseForm would be garbage — refuse it.
    //   2) The live ref's base form must still match the one captured for this speaker.
    //      A recycled ref keeps the FormID but points at a different base — refuse it.
    const UInt8 typeId = form->typeID;
    if (typeId != kFormType_TESObjectREFR && typeId != kFormType_Character
        && typeId != kFormType_Creature)
    {
        return nullptr;
    }

    auto* ref = static_cast<TESObjectREFR*>(form);
    if (speaker.baseId && (!ref->baseForm || ref->baseForm->refID != speaker.baseId))
    {
        return nullptr;
    }

    return ref;
}

bool HasPageAccess(DWORD protect, bool requireWrite)
{
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0)
    {
        return false;
    }

    protect &= 0xFF;

    if (requireWrite)
    {
        return protect == PAGE_READWRITE
            || protect == PAGE_WRITECOPY
            || protect == PAGE_EXECUTE_READWRITE
            || protect == PAGE_EXECUTE_WRITECOPY;
    }

    return protect == PAGE_READONLY
        || protect == PAGE_READWRITE
        || protect == PAGE_WRITECOPY
        || protect == PAGE_EXECUTE_READ
        || protect == PAGE_EXECUTE_READWRITE
        || protect == PAGE_EXECUTE_WRITECOPY;
}

bool IsAccessibleRange(const void* ptr, size_t size, bool requireWrite)
{
    if (!ptr || !size)
    {
        return false;
    }

    auto* cursor = static_cast<const BYTE*>(ptr);
    const auto* end = cursor + size;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cursor, &mbi, sizeof(mbi)))
        {
            return false;
        }

        if (mbi.State != MEM_COMMIT || !HasPageAccess(mbi.Protect, requireWrite))
        {
            return false;
        }

        const auto* regionEnd = static_cast<const BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cursor)
        {
            return false;
        }

        cursor = (std::min)(regionEnd, end);
    }

    return true;
}

template <typename T>
bool TryReadMemory(const void* ptr, T& out)
{
    if (!IsAccessibleRange(ptr, sizeof(T), false))
    {
        return false;
    }

    std::memcpy(&out, ptr, sizeof(T));
    return true;
}

bool IsPlausibleKeyframeValues(const float* values, UInt32 count)
{
    if (!values || !count || count > 32 || !IsAccessibleRange(values, sizeof(float) * count, true))
    {
        return false;
    }

    for (UInt32 i = 0; i < count; ++i)
    {
        const float value = values[i];
        if (!std::isfinite(value) || value < -0.25f || value > 1.25f)
        {
            return false;
        }
    }

    return true;
}

bool DiscoverFaceAnimationBinding(const SpeakerSnapshot& speaker, FaceAnimationBinding& binding)
{
    binding = {};

    auto* speakerRef = ResolveSpeakerRef(speaker);
    auto* actor = static_cast<Actor*>(speakerRef);
    if (!actor || !actor->baseProcess)
    {
        return false;
    }

    if (actor->baseProcess->processLevel > BaseProcess::kProcessLevel_MiddleHigh)
    {
        return false;
    }

    auto* process = static_cast<MiddleHighProcess*>(actor->baseProcess);
    auto* faceNode = reinterpret_cast<FaceGenNiNodeRuntime*>(process->unk248 ? process->unk248 : process->unk24C);
    void* animationData = process->unk178 ? static_cast<void*>(process->unk178) : (faceNode ? static_cast<void*>(faceNode->spAnimationData) : nullptr);
    if (!faceNode || !animationData)
    {
        return false;
    }

    FaceGenKeyframeMultiple32* phonemeKeyframe = nullptr;
    FaceGenKeyframeMultiple32* modifierKeyframe = nullptr;

    for (size_t offset = 0; offset + sizeof(FaceGenKeyframeMultiple32) <= 0x180; offset += sizeof(UInt32))
    {
        auto* candidatePtr = reinterpret_cast<FaceGenKeyframeMultiple32*>(static_cast<BYTE*>(animationData) + offset);
        FaceGenKeyframeMultiple32 candidate{};
        if (!TryReadMemory(candidatePtr, candidate))
        {
            continue;
        }

        if (!candidate.values || candidate.count < 8 || candidate.count > 24)
        {
            continue;
        }

        if (!IsPlausibleKeyframeValues(candidate.values, candidate.count))
        {
            continue;
        }

        if (!phonemeKeyframe && candidate.count == kFaceGenPhonemeCount)
        {
            phonemeKeyframe = candidatePtr;
        }
        else if (!modifierKeyframe && candidate.count == 17)
        {
            modifierKeyframe = candidatePtr;
        }
    }

    if (!phonemeKeyframe || !IsAccessibleRange(phonemeKeyframe, sizeof(FaceGenKeyframeMultiple32), true))
    {
        return false;
    }

    binding.speakerRefId = speaker.refId;
    binding.faceNode = faceNode;
    binding.animationData = animationData;
    binding.phonemeKeyframe = phonemeKeyframe;
    binding.modifierKeyframe = modifierKeyframe;
    return true;
}

bool CanSafelyWriteFaceAnimationBinding(const FaceAnimationBinding& binding)
{
    if (!binding.faceNode || !binding.phonemeKeyframe)
    {
        return false;
    }

    if (!IsAccessibleRange(binding.faceNode, sizeof(FaceGenNiNodeRuntime), true)
        || !IsAccessibleRange(binding.phonemeKeyframe, sizeof(FaceGenKeyframeMultiple32), true))
    {
        return false;
    }

    FaceGenKeyframeMultiple32 keyframe{};
    if (!TryReadMemory(binding.phonemeKeyframe, keyframe))
    {
        return false;
    }

    const UInt32 count = (std::min)(keyframe.count, kFaceGenPhonemeCount);
    if (count && !IsAccessibleRange(keyframe.values, sizeof(float) * count, true))
    {
        return false;
    }

    return true;
}

bool IsCurrentFaceAnimationBinding(const SpeakerSnapshot& speaker, const FaceAnimationBinding& binding)
{
    if (!binding.faceNode || !binding.phonemeKeyframe || !speaker.refId || binding.speakerRefId != speaker.refId)
    {
        return false;
    }

    FaceAnimationBinding current{};
    if (!DiscoverFaceAnimationBinding(speaker, current))
    {
        return false;
    }

    return current.faceNode == binding.faceNode
        && current.animationData == binding.animationData
        && current.phonemeKeyframe == binding.phonemeKeyframe
        && current.modifierKeyframe == binding.modifierKeyframe;
}

void CaptureOriginalFaceAnimationBindingState(ActiveSpeechAnimation& animation)
{
    if (animation.originalBindingStateCaptured || !CanSafelyWriteFaceAnimationBinding(animation.binding))
    {
        return;
    }

    auto* phonemeKeyframe = animation.binding.phonemeKeyframe;
    const UInt32 count = (std::min)(phonemeKeyframe->count, kFaceGenPhonemeCount);
    if (!count || !IsAccessibleRange(phonemeKeyframe->values, sizeof(float) * count, true))
    {
        return;
    }

    animation.originalWeights.fill(0.0f);
    for (UInt32 i = 0; i < count; ++i)
    {
        animation.originalWeights[i] = phonemeKeyframe->values[i];
    }
    animation.originalPhonemeCount = count;
    animation.originalPhonemeIsUpdated = phonemeKeyframe->isUpdated;
    animation.originalFaceAnimationUpdate = animation.binding.faceNode->bAnimationUpdate;
    animation.originalFaceInDialogue = animation.binding.faceNode->bIAmInDialouge;
    animation.originalBindingStateCaptured = true;
}

void ClearFaceAnimationBinding(FaceAnimationBinding& binding)
{
    if (!CanSafelyWriteFaceAnimationBinding(binding))
    {
        binding = {};
        return;
    }

    auto* phonemeKeyframe = binding.phonemeKeyframe;
    const UInt32 count = (std::min)(phonemeKeyframe->count, kFaceGenPhonemeCount);
    if (g_debugConfig.speechWritePhonemeValues
        && count
        && IsAccessibleRange(phonemeKeyframe->values, sizeof(float) * count, true))
    {
        for (UInt32 i = 0; i < count; ++i)
        {
            phonemeKeyframe->values[i] = 0.0f;
        }
    }

    if (g_debugConfig.speechWriteFaceFlags)
    {
        binding.faceNode->bAnimationUpdate = 1;
        binding.faceNode->bIAmInDialouge = 0;
    }
    binding = {};
}

void RestoreFaceAnimationBinding(FaceAnimationBinding& binding, const ActiveSpeechAnimation& animation)
{
    if (!animation.originalBindingStateCaptured || !CanSafelyWriteFaceAnimationBinding(binding))
    {
        binding = {};
        return;
    }

    auto* phonemeKeyframe = binding.phonemeKeyframe;
    const UInt32 count = (std::min)(phonemeKeyframe->count, animation.originalPhonemeCount);
    if (count && IsAccessibleRange(phonemeKeyframe->values, sizeof(float) * count, true))
    {
        for (UInt32 i = 0; i < count; ++i)
        {
            const float prev = animation.lastWeights[i] < 0.0f ? 0.0f : animation.lastWeights[i];
            if (prev > 0.001f)
            {
                phonemeKeyframe->values[i] = animation.originalWeights[i];
            }
        }
    }

    binding.faceNode->bAnimationUpdate = animation.originalFaceAnimationUpdate;
    binding.faceNode->bIAmInDialouge = animation.originalFaceInDialogue;
    binding = {};
}

void ApplySpeechWeights(ActiveSpeechAnimation& animation, const std::array<float, kFaceGenPhonemeCount>& weights)
{
    auto& binding = animation.binding;
    if (!binding.faceNode || !binding.phonemeKeyframe)
    {
        return;
    }

    auto* phonemeKeyframe = binding.phonemeKeyframe;
    const UInt32 count = (std::min)(phonemeKeyframe->count, kFaceGenPhonemeCount);
    if (!count || !IsAccessibleRange(phonemeKeyframe->values, sizeof(float) * count, true))
    {
        return;
    }

    if (g_debugConfig.speechWritePhonemeValues)
    {
        for (UInt32 i = 0; i < count; ++i)
        {
            const float prev = animation.lastWeights[i] < 0.0f ? 0.0f : animation.lastWeights[i];
            const float next = weights[i];
            if (prev <= 0.001f && next <= 0.001f)
            {
                continue;
            }

            float target = animation.originalWeights[i];
            if (next > 0.001f)
            {
                target = (std::clamp)((std::max)(target, next), 0.0f, 1.0f);
            }
            phonemeKeyframe->values[i] = target;
        }
    }

    if (g_debugConfig.speechWriteFaceFlags)
    {
        binding.faceNode->bAnimationUpdate = 1;
        binding.faceNode->bIAmInDialouge = 1;
    }
}

std::vector<float> BuildSpeechEnvelope(const WavData& wavData, DWORD windowMs)
{
    std::vector<float> envelope;
    if (wavData.pcmData.empty() || !wavData.format.nChannels || !wavData.format.nSamplesPerSec || !wavData.format.nBlockAlign)
    {
        return envelope;
    }

    const UInt16 bitsPerSample = wavData.format.wBitsPerSample;
    if (bitsPerSample != 8 && bitsPerSample != 16)
    {
        return envelope;
    }

    const size_t totalFrames = wavData.pcmData.size() / wavData.format.nBlockAlign;
    if (!totalFrames)
    {
        return envelope;
    }

    const size_t framesPerWindow = (std::max<size_t>)(1, static_cast<size_t>((static_cast<unsigned long long>(wavData.format.nSamplesPerSec) * windowMs) / 1000ULL));
    envelope.reserve((totalFrames + framesPerWindow - 1) / framesPerWindow);

    float peak = 0.0f;
    for (size_t frameStart = 0; frameStart < totalFrames; frameStart += framesPerWindow)
    {
        const size_t frameEnd = (std::min)(frameStart + framesPerWindow, totalFrames);
        double windowTotal = 0.0;
        size_t windowSamples = 0;

        for (size_t frameIndex = frameStart; frameIndex < frameEnd; ++frameIndex)
        {
            const BYTE* framePtr = wavData.pcmData.data() + (frameIndex * wavData.format.nBlockAlign);
            for (UInt16 channel = 0; channel < wavData.format.nChannels; ++channel)
            {
                double sample = 0.0;
                if (bitsPerSample == 16)
                {
                    const short pcmSample = *reinterpret_cast<const short*>(framePtr + (channel * sizeof(short)));
                    sample = std::abs(static_cast<double>(pcmSample) / 32768.0);
                }
                else
                {
                    const int pcmSample = static_cast<int>(framePtr[channel]) - 128;
                    sample = std::abs(static_cast<double>(pcmSample) / 128.0);
                }

                windowTotal += sample;
                ++windowSamples;
            }
        }

        const float amplitude = windowSamples ? static_cast<float>(windowTotal / static_cast<double>(windowSamples)) : 0.0f;
        peak = (std::max)(peak, amplitude);
        envelope.push_back(amplitude);
    }

    if (peak > 0.0f)
    {
        for (float& amplitude : envelope)
        {
            const float normalised = amplitude / peak;
            amplitude = std::pow((std::clamp)(normalised, 0.0f, 1.0f), 0.65f);
        }
    }

    return envelope;
}

bool IsVowelPhoneme(UInt32 phonemeId)
{
    switch (phonemeId)
    {
    case 0:
    case 1:
    case 5:
    case 6:
    case 8:
    case 11:
    case 12:
        return true;
    default:
        return false;
    }
}

std::vector<UInt32> BuildPhonemeSequenceFromText(const std::string& text)
{
    std::string normalised;
    normalised.reserve(text.size());
    for (unsigned char ch : text)
    {
        if (std::isalpha(ch))
        {
            normalised.push_back(static_cast<char>(std::tolower(ch)));
        }
        else if (std::isspace(ch))
        {
            normalised.push_back(' ');
        }
    }

    std::vector<UInt32> sequence;
    sequence.reserve(normalised.size());
    for (size_t i = 0; i < normalised.size();)
    {
        const char ch = normalised[i];
        const char next = (i + 1 < normalised.size()) ? normalised[i + 1] : '\0';
        if (ch == ' ')
        {
            ++i;
            continue;
        }

        if (ch == 't' && next == 'h')
        {
            sequence.push_back(14);
            i += 2;
            continue;
        }

        if ((ch == 's' && next == 'h') || (ch == 'c' && next == 'h') || (ch == 'j' && next == 'h'))
        {
            sequence.push_back(3);
            i += 2;
            continue;
        }

        if (ch == 'o' && next == 'o')
        {
            sequence.push_back(12);
            i += 2;
            continue;
        }

        if (ch == 'b' || ch == 'm' || ch == 'p')
        {
            sequence.push_back(2);
        }
        else if (ch == 'f' || ch == 'v')
        {
            sequence.push_back(7);
        }
        else if (ch == 'c' || ch == 'k' || ch == 'g' || ch == 'q')
        {
            sequence.push_back(9);
        }
        else if (ch == 'n')
        {
            sequence.push_back(10);
        }
        else if (ch == 'r')
        {
            sequence.push_back(13);
        }
        else if (ch == 'w')
        {
            sequence.push_back(15);
        }
        else if (ch == 'a')
        {
            sequence.push_back(0);
        }
        else if (ch == 'e')
        {
            sequence.push_back(5);
        }
        else if (ch == 'i' || ch == 'y')
        {
            sequence.push_back(8);
        }
        else if (ch == 'o')
        {
            sequence.push_back(11);
        }
        else if (ch == 'u')
        {
            sequence.push_back(12);
        }
        else if (ch == 'd' || ch == 't' || ch == 'l' || ch == 's' || ch == 'x' || ch == 'z')
        {
            sequence.push_back(4);
        }
        else if (ch == 'j' || ch == 'h')
        {
            sequence.push_back(3);
        }

        ++i;
    }

    if (sequence.empty())
    {
        sequence = { 0, 11, 5 };
    }

    return sequence;
}

std::vector<VisemeCue> BuildVisemeTimeline(const std::string& text, DWORD durationMs)
{
    std::vector<VisemeCue> visemes;
    if (!durationMs)
    {
        return visemes;
    }

    auto sequence = BuildPhonemeSequenceFromText(text);
    if (sequence.empty())
    {
        return visemes;
    }

    const size_t maxCueCount = (std::max<size_t>)(1, static_cast<size_t>(durationMs / 70));
    if (sequence.size() > maxCueCount)
    {
        std::vector<UInt32> reduced;
        reduced.reserve(maxCueCount);
        for (size_t index = 0; index < maxCueCount; ++index)
        {
            const size_t sourceIndex = (index * sequence.size()) / maxCueCount;
            reduced.push_back(sequence[sourceIndex]);
        }
        sequence = std::move(reduced);
    }

    visemes.reserve(sequence.size());
    for (size_t index = 0; index < sequence.size(); ++index)
    {
        const DWORD startMs = static_cast<DWORD>((static_cast<unsigned long long>(durationMs) * index) / sequence.size());
        const DWORD endMs = static_cast<DWORD>((static_cast<unsigned long long>(durationMs) * (index + 1)) / sequence.size());
        const UInt32 phonemeId = sequence[index];
        visemes.push_back(VisemeCue{
            startMs,
            endMs > startMs ? endMs : (startMs + 1),
            phonemeId,
            IsVowelPhoneme(phonemeId) ? 1.0f : 0.82f,
        });
    }

    return visemes;
}

float SampleSpeechEnvelope(const ActiveSpeechAnimation& animation, DWORD elapsedMs)
{
    if (animation.envelope.empty())
    {
        return 0.0f;
    }

    const size_t index = (std::min<size_t>)(animation.envelope.size() - 1, elapsedMs / animation.envelopeWindowMs);
    return animation.envelope[index];
}

std::array<float, kFaceGenPhonemeCount> BuildSpeechWeights(const ActiveSpeechAnimation& animation, DWORD elapsedMs)
{
    std::array<float, kFaceGenPhonemeCount> weights{};
    if (animation.visemes.empty())
    {
        return weights;
    }

    const float amplitude = SampleSpeechEnvelope(animation, elapsedMs);
    if (amplitude < kSpeechSilenceThreshold)
    {
        return weights;
    }

    size_t cueIndex = animation.visemes.size() - 1;
    for (size_t index = 0; index < animation.visemes.size(); ++index)
    {
        if (elapsedMs < animation.visemes[index].endMs)
        {
            cueIndex = index;
            break;
        }
    }

    const VisemeCue& cue = animation.visemes[cueIndex];
    const float baseWeight = (std::clamp)(kSpeechMinWeight + (amplitude * (kSpeechMaxWeight - kSpeechMinWeight)), 0.0f, kSpeechMaxWeight);
    const float scaledBaseWeight = (std::clamp)(baseWeight * g_debugConfig.speechWeightScale * animation.weightGain, 0.0f, 1.0f);
    weights[cue.phonemeId] = scaledBaseWeight * cue.emphasis;

    const DWORD cueSpan = cue.endMs > cue.startMs ? (cue.endMs - cue.startMs) : 1;
    if (cueIndex + 1 < animation.visemes.size())
    {
        const float phase = static_cast<float>((elapsedMs > cue.startMs) ? (elapsedMs - cue.startMs) : 0) / static_cast<float>(cueSpan);
        float blend = (std::clamp)((phase - 0.62f) / 0.38f, 0.0f, 1.0f);
        blend = blend * blend * (3.0f - (2.0f * blend));
        weights[cue.phonemeId] *= (1.0f - (blend * 0.35f));
        const VisemeCue& nextCue = animation.visemes[cueIndex + 1];
        weights[nextCue.phonemeId] = (std::max)(weights[nextCue.phonemeId], scaledBaseWeight * 0.68f * blend * nextCue.emphasis);
    }

    return weights;
}

void AbandonSpeechAnimation(const char* reason)
{
    auto animation = std::move(g_state.speechAnimation);
    if (animation.active && !animation.requestId.empty())
    {
        TraceRequestEvent(animation.requestId, "speech_animation_abandoned",
            {
                { "reason", reason ? reason : "" },
            },
            {
                { "speaker_ref_id", static_cast<double>(animation.speaker.refId) },
            });
    }

    g_state.speechAnimation = {};
    g_state.speechAnimation.lastWeights.fill(0.0f);

    if (reason && *reason)
    {
        LogLine("Abandoned speech animation: reason=%s", reason);
    }
}

void StopSpeechAnimation()
{
    if (g_state.speechAnimation.active && !g_state.speechAnimation.requestId.empty())
    {
        TraceRequestEvent(g_state.speechAnimation.requestId, "speech_animation_stopped",
            {},
            {
                { "duration_ms", static_cast<double>(g_state.speechAnimation.durationMs) },
            });
    }

    const bool hasBinding = g_state.speechAnimation.binding.faceNode || g_state.speechAnimation.binding.phonemeKeyframe;
    const bool canClearBinding = hasBinding
        && CanSafelyWriteFaceAnimationBinding(g_state.speechAnimation.binding)
        && IsCurrentFaceAnimationBinding(g_state.speechAnimation.speaker, g_state.speechAnimation.binding);
    if (canClearBinding && g_debugConfig.speechClearBindingOnStop)
    {
        if (g_state.speechAnimation.originalBindingStateCaptured)
        {
            RestoreFaceAnimationBinding(g_state.speechAnimation.binding, g_state.speechAnimation);
        }
        else
        {
            ClearFaceAnimationBinding(g_state.speechAnimation.binding);
        }
    }
    else if (canClearBinding)
    {
        LogLine("Skipped speech binding clear on stop by config for speaker %08X.",
            g_state.speechAnimation.speaker.refId);
    }
    else if (hasBinding)
    {
        LogLine("Skipped clearing stale speech animation binding for speaker %08X.",
            g_state.speechAnimation.speaker.refId);
    }

    g_state.speechAnimation = {};
    g_state.speechAnimation.lastWeights.fill(0.0f);
    WriteRuntimeHeartbeatIfNeeded(true);
}

void StartSpeechAnimation(const WavData& wavData, const QueuedAudioChunk& chunk, const SpeakerSnapshot& speaker, DWORD durationMs, float weightGain = 1.0f)
{
    if (!g_debugConfig.speechAnimationEnabled)
    {
        StopSpeechAnimation();
        if (!chunk.requestId.empty())
        {
            TraceRequestEvent(chunk.requestId, "speech_animation_disabled_by_config",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(speaker.refId) },
                });
        }
        return;
    }

    ActiveSpeechAnimation previous = std::move(g_state.speechAnimation);
    const bool canReuseBinding = previous.active
        && previous.speaker.refId == speaker.refId
        && previous.binding.phonemeKeyframe
        && CanSafelyWriteFaceAnimationBinding(previous.binding);
    if (!canReuseBinding)
    {
        g_state.speechAnimation = std::move(previous);
        StopSpeechAnimation();
    }
    else
    {
        g_state.speechAnimation = {};
    }

    ActiveSpeechAnimation animation{};
    animation.active = durationMs > 0 && speaker.refId != 0;
    animation.requestId = chunk.requestId;
    animation.speaker = speaker;
    animation.startedTick = GetTickCount();
    animation.durationMs = durationMs;
    animation.envelopeWindowMs = kSpeechEnvelopeWindowMs;
    animation.weightGain = weightGain;
    animation.envelope = BuildSpeechEnvelope(wavData, animation.envelopeWindowMs);
    animation.visemes = BuildVisemeTimeline(chunk.subtitleText.empty() ? chunk.audioFile : chunk.subtitleText, durationMs);
    if (canReuseBinding)
    {
        animation.binding = previous.binding;
        animation.originalWeights = previous.originalWeights;
        animation.originalPhonemeCount = previous.originalPhonemeCount;
        animation.originalPhonemeIsUpdated = previous.originalPhonemeIsUpdated;
        animation.originalFaceAnimationUpdate = previous.originalFaceAnimationUpdate;
        animation.originalFaceInDialogue = previous.originalFaceInDialogue;
        animation.originalBindingStateCaptured = previous.originalBindingStateCaptured;
        animation.lastWeights = previous.lastWeights;
        animation.lastBindingValidationTick = previous.lastBindingValidationTick;
    }
    else
    {
        animation.lastWeights.fill(-1.0f);
    }
    g_state.speechAnimation = std::move(animation);

    if (g_state.speechAnimation.active)
    {
        TraceRequestEvent(g_state.speechAnimation.requestId, "speech_animation_started",
            {
                { "audio_file", chunk.audioFile },
            },
            {
                { "duration_ms", static_cast<double>(durationMs) },
                { "envelope_samples", static_cast<double>(g_state.speechAnimation.envelope.size()) },
                { "viseme_count", static_cast<double>(g_state.speechAnimation.visemes.size()) },
                { "speaker_ref_id", static_cast<double>(speaker.refId) },
            });
        if (g_debugConfig.requestTracingEnabled)
        {
            LogLine("Started native speech animation: speaker=%08X duration=%lu envelope=%zu visemes=%zu",
                speaker.refId,
                static_cast<unsigned long>(durationMs),
                g_state.speechAnimation.envelope.size(),
                g_state.speechAnimation.visemes.size());
        }
    }
}

void UpdateSpeechAnimation()
{
    auto& animation = g_state.speechAnimation;
    if (!animation.active)
    {
        return;
    }

    const DWORD now = GetTickCount();
    const DWORD elapsedMs = now - animation.startedTick;
    if (elapsedMs > animation.durationMs + kSpeechAnimationTailMs)
    {
        StopSpeechAnimation();
        return;
    }

    if (!animation.binding.phonemeKeyframe)
    {
        if (!animation.lastBindingAttemptTick || (now - animation.lastBindingAttemptTick) >= kSpeechBindingRetryMs)
        {
            animation.lastBindingAttemptTick = now;
            if (!DiscoverFaceAnimationBinding(animation.speaker, animation.binding))
            {
                if (!animation.loggedBindingFailure)
                {
                    TraceRequestEvent(animation.requestId, "speech_face_binding_missing",
                        {},
                        {
                            { "speaker_ref_id", static_cast<double>(animation.speaker.refId) },
                        });
                    LogLine("Could not resolve face animation binding for speaker %08X.", animation.speaker.refId);
                    animation.loggedBindingFailure = true;
                }
                return;
            }

            TraceRequestEvent(animation.requestId, "speech_face_binding_resolved",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(animation.speaker.refId) },
                    { "phoneme_count", static_cast<double>(animation.binding.phonemeKeyframe ? animation.binding.phonemeKeyframe->count : 0) },
                    { "modifier_count", static_cast<double>(animation.binding.modifierKeyframe ? animation.binding.modifierKeyframe->count : 0) },
                });
            LogLine("Resolved face animation binding: speaker=%08X animation=%p phonemeCount=%u modifierCount=%u",
                animation.speaker.refId,
                animation.binding.animationData,
                animation.binding.phonemeKeyframe ? animation.binding.phonemeKeyframe->count : 0,
                animation.binding.modifierKeyframe ? animation.binding.modifierKeyframe->count : 0);
            animation.lastBindingValidationTick = now;
            CaptureOriginalFaceAnimationBindingState(animation);
        }
        else
        {
            return;
        }
    }
    else if (!animation.lastBindingValidationTick
        || (now - animation.lastBindingValidationTick) >= g_debugConfig.speechBindingValidationIntervalMs)
    {
        animation.lastBindingValidationTick = now;
        if (!CanSafelyWriteFaceAnimationBinding(animation.binding)
            || !IsCurrentFaceAnimationBinding(animation.speaker, animation.binding))
        {
            TraceRequestEvent(animation.requestId, "speech_face_binding_invalidated",
                {},
                {
                    { "speaker_ref_id", static_cast<double>(animation.speaker.refId) },
                });
            LogLine("Speech face binding invalidated for speaker %08X; stopping animation.", animation.speaker.refId);
            AbandonSpeechAnimation("binding_invalidated");
            return;
        }
    }

    if (animation.lastWeightsUpdateTick
        && (now - animation.lastWeightsUpdateTick) < g_debugConfig.speechAnimationUpdateIntervalMs)
    {
        return;
    }
    animation.lastWeightsUpdateTick = now;

    auto weights = BuildSpeechWeights(animation, elapsedMs);
    if (!animation.firstWeightsAppliedLogged)
    {
        for (float value : weights)
        {
            if (value > 0.001f)
            {
                animation.firstWeightsAppliedLogged = true;
                TraceRequestEvent(animation.requestId, "speech_first_weights_applied",
                    {},
                    {
                        { "speaker_ref_id", static_cast<double>(animation.speaker.refId) },
                        { "elapsed_since_animation_start_ms", static_cast<double>(elapsedMs) },
                    });
                break;
            }
        }
    }
    if (weights != animation.lastWeights)
    {
        ApplySpeechWeights(animation, weights);
        animation.lastWeights = weights;
    }
}

// ===========================================================================
// Phase 3: single-buffer streaming voice
//
// Instead of one static DirectSound buffer per mini-chunk (the static path below),
// hold ONE non-looping buffer per utterance, write each chunk's PCM into it as it
// arrives, and play it continuously. Audio is buffered ahead of the play cursor
// (seamless); lip-sync is fired per chunk on the playback schedule so the mouth
// stays in sync. Gated on DebugConfig.singleBufferStreaming (off => static path).
// ===========================================================================

// Release the current streaming buffer + lip-sync queue and reset stream state.
void StopStreamingVoice(const char* reason)
{
    const bool wasActive = g_state.streamActive;
    if (g_state.streamBuffer3d)
    {
        g_state.streamBuffer3d->Release();
        g_state.streamBuffer3d = nullptr;
    }
    if (g_state.streamBuffer)
    {
        g_state.streamBuffer->Stop();
        g_state.streamBuffer->Release();
        g_state.streamBuffer = nullptr;
    }
    if (wasActive && reason && !g_state.streamRequestId.empty())
    {
        TraceRequestEvent(g_state.streamRequestId, "stream_voice_stopped",
            { { "reason", reason } },
            { { "written_bytes", static_cast<double>(g_state.streamWriteCursor) } });
    }
    g_state.streamActive = false;
    g_state.streamStarted = false;
    g_state.streamCapacityBytes = 0;
    g_state.streamWriteCursor = 0;
    g_state.streamPlayStartTick = 0;
    g_state.streamLastAppendTick = 0;
    g_state.streamCumulativeMs = 0;
    g_state.streamRequestId.clear();
    g_state.streamSpeaker = {};
    g_state.streamNonPositional = false;
    g_state.streamSpeakerOrphaned = false;
    g_state.streamFormat = {};
    g_state.streamLipSyncQueue.clear();
    g_state.captionMaxChars = -1;
    g_state.captionSegments.clear();
    g_state.captionSegmentStartMs.clear();
    g_state.captionCurrentIndex = -1;
    g_state.captionSourceText.clear();
    g_state.captionLastShowTick = 0;
    g_state.lipSyncAccumPcm.clear();
    g_state.lipSyncAccumMs = 0;
    g_state.lipSyncAccumStartMs = 0;
}

// Apply 3D position + distance-attenuated volume to the streaming buffer.
void ApplyStreamingVoiceSpatial(const SpeakerSnapshot& speaker, bool nonPositional)
{
    if (!g_state.streamBuffer)
    {
        return;
    }
    const PlayerCharacter* player = GetPlayer();
    if (g_state.streamBuffer3d && !nonPositional)
    {
        UpdateListener3d(player);
        if (speaker.valid)
        {
            g_state.streamBuffer3d->SetPosition(speaker.posX / kGameUnitsPerMeter,
                speaker.posY / kGameUnitsPerMeter, speaker.posZ / kGameUnitsPerMeter, DS3D_IMMEDIATE);
        }
        g_state.streamBuffer3d->SetMinDistance(kVoiceMinDistanceMeters, DS3D_IMMEDIATE);
        g_state.streamBuffer3d->SetMaxDistance(kVoiceMaxDistanceMeters, DS3D_IMMEDIATE);
        g_state.streamBuffer3d->SetMode(DS3DMODE_NORMAL, DS3D_IMMEDIATE);
    }
    g_state.streamBuffer->SetVolume(nonPositional ? DSBVOLUME_MAX
        : ComputeDistanceAttenuatedVolume(player, speaker));
}

// Re-resolve the streaming speaker's CURRENT world position each frame so 3D audio
// FOLLOWS them as they move (previously the buffer held the position captured when the
// chunk was appended, so the voice froze in place when the NPC walked away).
//
// Identity-safe: ResolveSpeakerRef refuses a FormID that was recycled to a different
// form/actor when the speaker's cell unloads. If the speaker is not currently loaded
// (no 3D) or can't be resolved, we FREEZE the audio at the last known position (orphan)
// and never rebind to a wrong ref — this is the "speaker left the cell mid-speech"
// case that used to cross-wire the voice onto another character.
void RefreshStreamingSpeakerPosition()
{
    if (g_state.streamNonPositional || !g_state.streamSpeaker.valid)
    {
        return;
    }

    TESObjectREFR* ref = ResolveSpeakerRef(g_state.streamSpeaker);
    const bool loaded = ref && ref->GetNiNode();
    if (loaded)
    {
        // Same actor, currently loaded: track its live position.
        g_state.streamSpeaker.posX = ref->posX;
        g_state.streamSpeaker.posY = ref->posY;
        g_state.streamSpeaker.posZ = ref->posZ;
        if (g_state.streamSpeakerOrphaned)
        {
            g_state.streamSpeakerOrphaned = false;
            LogLine("stream speaker %08X reloaded mid-speech; audio tracking resumed.",
                g_state.streamSpeaker.refId);
        }
        return;
    }

    // Unloaded or recycled: keep the frozen position, log once per orphan event.
    if (!g_state.streamSpeakerOrphaned)
    {
        g_state.streamSpeakerOrphaned = true;
        TraceRequestEvent(g_state.streamRequestId, "stream_speaker_unloaded",
            {},
            { { "speaker_ref_id", static_cast<double>(g_state.streamSpeaker.refId) } });
        LogLine("stream speaker %08X unloaded mid-speech; freezing audio at last position "
            "(no rebind).", g_state.streamSpeaker.refId);
    }
}

// Create the streaming buffer for a new utterance, sized to hold the whole line.
bool StartStreamingVoice(const WavData& first, const SpeakerSnapshot& speaker,
    const std::string& requestId, bool nonPositional)
{
    if (!EnsureDirectSound())
    {
        return false;
    }
    StopStreamingVoice("new_utterance");

    const WAVEFORMATEX& fmt = first.format;
    if (!fmt.nSamplesPerSec || !fmt.nBlockAlign)
    {
        return false;
    }
    DWORD byteRate = fmt.nAvgBytesPerSec ? fmt.nAvgBytesPerSec
        : fmt.nSamplesPerSec * fmt.nBlockAlign;
    DWORD capacity = byteRate * kStreamingVoiceMaxSeconds;
    capacity -= capacity % fmt.nBlockAlign; // frame-align
    if (!capacity)
    {
        return false;
    }

    const bool use3d = g_debugConfig.directSound3dEnabled && !nonPositional;
    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    if (use3d)
    {
        desc.dwFlags |= DSBCAPS_CTRL3D;
    }
    if (g_debugConfig.directSoundSoftwareBufferEnabled)
    {
        desc.dwFlags |= DSBCAPS_LOCSOFTWARE;
    }
    desc.dwBufferBytes = capacity;
    desc.lpwfxFormat = const_cast<WAVEFORMATEX*>(&fmt);

    IDirectSoundBuffer* raw = nullptr;
    if (FAILED(g_state.directSound->CreateSoundBuffer(&desc, &raw, nullptr)) || !raw)
    {
        LogLine("Stream CreateSoundBuffer failed");
        return false;
    }
    IDirectSoundBuffer8* buffer = nullptr;
    HRESULT hr = raw->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&buffer));
    raw->Release();
    if (FAILED(hr) || !buffer)
    {
        LogLine("Stream QueryInterface(IDirectSoundBuffer8) failed: 0x%08X", hr);
        return false;
    }

    // Zero-fill so any underrun region plays silence rather than stale data.
    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD s1 = 0;
    DWORD s2 = 0;
    if (SUCCEEDED(buffer->Lock(0, capacity, &p1, &s1, &p2, &s2, 0)))
    {
        if (p1)
        {
            std::memset(p1, 0, s1);
        }
        if (p2)
        {
            std::memset(p2, 0, s2);
        }
        buffer->Unlock(p1, s1, p2, s2);
    }

    IDirectSound3DBuffer* buffer3d = nullptr;
    if (use3d && FAILED(buffer->QueryInterface(IID_IDirectSound3DBuffer, reinterpret_cast<void**>(&buffer3d))))
    {
        buffer3d = nullptr;
    }

    g_state.streamBuffer = buffer;
    g_state.streamBuffer3d = buffer3d;
    g_state.streamActive = true;
    g_state.streamStarted = false;
    g_state.streamCapacityBytes = capacity;
    g_state.streamWriteCursor = 0;
    g_state.streamCumulativeMs = 0;
    g_state.streamFormat = fmt;
    g_state.streamRequestId = requestId;
    g_state.streamSpeaker = speaker;
    g_state.streamNonPositional = nonPositional;
    g_state.streamLipSyncQueue.clear();
    TraceRequestEvent(requestId, "stream_voice_started",
        {},
        {
            { "capacity_bytes", static_cast<double>(capacity) },
            { "sample_rate", static_cast<double>(fmt.nSamplesPerSec) },
        });
    return true;
}

// Append one caption segment (= one synthesized sentence) anchored at `startMs`, the
// audio-buffer offset where this sentence's audio begins. UpdateStreamingVoice reveals
// each segment when the play cursor reaches its startMs, so captions swap 1:1 with the
// TTS audio through every sentence of the reply. DISPLAY ONLY — never touches audio.
//
// This replaces the old build-once-from-the-first-chunk approach, which only ever knew
// the FIRST sentence's text (later sentences arrive on later chunks) and so froze the
// captions on the opening line for the rest of the turn.
void AppendCaptionSegment(const std::string& text, DWORD startMs)
{
    if (text.empty())
    {
        return;
    }
    // Segments arrive in playback order; startMs is monotonic within an utterance.
    // Ignore anything that would go backwards (defensive against a re-processed chunk).
    if (!g_state.captionSegmentStartMs.empty() && startMs < g_state.captionSegmentStartMs.back())
    {
        return;
    }
    g_state.captionSegments.push_back(text);
    g_state.captionSegmentStartMs.push_back(startMs);
    // Accumulate the full spoken line so far for lip-sync viseme slicing (space-joined).
    if (!g_state.captionSourceText.empty())
    {
        g_state.captionSourceText.push_back(' ');
    }
    g_state.captionSourceText += text;
    TraceRequestEvent(g_state.streamRequestId, "caption_segment_added",
        {},
        {
            { "segment_index", static_cast<double>(g_state.captionSegments.size() - 1) },
            { "start_ms", static_cast<double>(startMs) },
            { "chars", static_cast<double>(text.size()) },
        });
}

// Write one chunk's PCM into the streaming buffer (creating it on the first chunk of
// a new utterance) and queue its lip-sync for when that audio plays.
bool AppendStreamingChunk(const QueuedAudioChunk& chunk, const WavData& wav, const SpeakerSnapshot& speaker)
{
    // A single 3D buffer can sit at only ONE position, so it must serve ONE speaker.
    // In a group reply both NPCs stream under the SAME requestId, so the request-id
    // guard alone never restarts — every chunk pours into the first speaker's buffer
    // and (because UpdateStreamingVoice re-spatializes the whole buffer each frame)
    // the second NPC's words play from the first NPC's location. Also finalize +
    // restart when the resolved SPEAKER changes mid-request so each NPC gets its own
    // correctly-positioned buffer. By the time the next speaker's first chunk lands,
    // the previous speaker's audio has normally drained (the LLM-generation gap
    // between turns), so this doesn't clip the prior line in practice.
    const bool requestChanged = g_state.streamRequestId != chunk.requestId;
    const bool speakerChanged = !chunk.nonPositional
        && g_state.streamSpeaker.valid && speaker.valid
        && g_state.streamSpeaker.refId != speaker.refId;
    if (g_state.streamActive && (requestChanged || speakerChanged))
    {
        StopStreamingVoice(requestChanged ? "request_changed" : "speaker_changed");
    }
    if (!g_state.streamActive && !StartStreamingVoice(wav, speaker, chunk.requestId, chunk.nonPositional))
    {
        return false;
    }
    IDirectSoundBuffer8* buffer = g_state.streamBuffer;
    if (!buffer || wav.pcmData.empty())
    {
        return false;
    }
    const DWORD blockAlign = g_state.streamFormat.nBlockAlign ? g_state.streamFormat.nBlockAlign : 2;

    // Underrun recovery (e.g. opener->remainder gap): never write behind the play
    // cursor, or that audio is skipped. Keep a small lead ahead of it.
    DWORD playPos = 0;
    DWORD writePos = 0;
    if (g_state.streamStarted && SUCCEEDED(buffer->GetCurrentPosition(&playPos, &writePos)))
    {
        DWORD lead = g_state.streamFormat.nAvgBytesPerSec
            ? g_state.streamFormat.nAvgBytesPerSec * kStreamingVoiceLeadMs / 1000 : 0;
        lead -= lead % blockAlign;
        if (g_state.streamWriteCursor < playPos + lead)
        {
            g_state.streamWriteCursor = playPos + lead;
            g_state.streamWriteCursor -= g_state.streamWriteCursor % blockAlign;
        }
    }

    DWORD bytes = static_cast<DWORD>(wav.pcmData.size());
    if (g_state.streamWriteCursor + bytes > g_state.streamCapacityBytes)
    {
        // Utterance overran the buffer; clip the tail (rare for NPC lines).
        if (g_state.streamWriteCursor >= g_state.streamCapacityBytes)
        {
            return false;
        }
        bytes = g_state.streamCapacityBytes - g_state.streamWriteCursor;
        bytes -= bytes % blockAlign;
        if (!bytes)
        {
            return false;
        }
    }

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD s1 = 0;
    DWORD s2 = 0;
    if (FAILED(buffer->Lock(g_state.streamWriteCursor, bytes, &p1, &s1, &p2, &s2, 0)))
    {
        return false;
    }
    if (p1 && s1)
    {
        std::memcpy(p1, wav.pcmData.data(), s1);
    }
    if (p2 && s2)
    {
        std::memcpy(p2, wav.pcmData.data() + s1, s2);
    }
    buffer->Unlock(p1, s1, p2, s2);

    const DWORD chunkStartMs = g_state.streamCumulativeMs;
    const DWORD durationMs = GetWavDurationMs(wav);
    g_state.streamWriteCursor += bytes;
    g_state.streamCumulativeMs += durationMs;
    g_state.streamLastAppendTick = GetTickCount();
    g_state.streamSpeaker = speaker;

    // Start playback once the first chunk is buffered.
    if (!g_state.streamStarted)
    {
        ApplyStreamingVoiceSpatial(speaker, chunk.nonPositional);
        buffer->SetCurrentPosition(0);
        if (SUCCEEDED(buffer->Play(0, 0, 0)))
        {
            g_state.streamStarted = true;
            g_state.streamPlayStartTick = GetTickCount();
            g_state.directSoundIdleSinceTick = 0;
        }
        // Conversation hold + remembered target (mirrors StartQueuedAudioPlayback).
        if (!chunk.nonPositional)
        {
            const std::string holdKey = chunk.speakerKey.empty() ? g_state.lastNpcKey : chunk.speakerKey;
            const std::string holdName = chunk.speakerName.empty() ? g_state.lastNpcName : chunk.speakerName;
            const bool skipHold = !chunk.requestId.empty()
                && g_state.movementActionRequestIds.find(chunk.requestId) != g_state.movementActionRequestIds.end();
            RememberNpcTarget(holdKey, holdName, speaker);
            if (!skipHold
                && (!g_state.conversationHold.active || g_state.conversationHold.speaker.refId != speaker.refId))
            {
                EngageConversationHold(holdKey, holdName, speaker);
            }
        }
    }

    // Queue this chunk's lip-sync to fire when its audio reaches the speakers.
    PendingStreamLipSync ls;
    ls.startMs = chunkStartMs;
    ls.durationMs = durationMs;
    ls.pcm = wav.pcmData;
    ls.format = wav.format;
    ls.requestId = chunk.requestId;
    ls.audioFile = chunk.audioFile;
    ls.subtitleText = chunk.subtitleText;
    ls.speaker = speaker;
    g_state.streamLipSyncQueue.push_back(std::move(ls));
    g_state.streamedAudioSeenForReply = true;

    // Captions follow the TTS audio 1:1. Each synthesized sentence's audio begins with a
    // chunk carrying that sentence's caption text (later chunks of the same sentence carry
    // EMPTY text), so a non-empty subtitle marks a new sentence starting HERE in the
    // buffer. Anchor a caption segment to this chunk's start so UpdateStreamingVoice swaps
    // to it when playback reaches it — for every sentence, not just the first.
    if (!chunk.subtitleText.empty())
    {
        g_state.captionMaxChars = chunk.captionMaxChars;
        AppendCaptionSegment(chunk.subtitleText, chunkStartMs);
    }

    TraceRequestEvent(chunk.requestId, "stream_chunk_appended",
        { { "audio_file", chunk.audioFile } },
        {
            { "chunk_index", static_cast<double>(chunk.chunkIndex) },
            { "write_cursor", static_cast<double>(g_state.streamWriteCursor) },
            { "start_ms", static_cast<double>(chunkStartMs) },
            { "duration_ms", static_cast<double>(durationMs) },
        });
    return true;
}

// Pop all pending chunk files into the streaming buffer (the single-buffer path's
// replacement for the static-buffer scheduler).
void DrainChunksToStreamingVoice()
{
    while (!g_state.pendingAudioChunks.empty())
    {
        // Resolve who this chunk belongs to BEFORE committing to it (positional
        // chunks resolve to the named NPC; non-positional are player-centered 2D).
        const QueuedAudioChunk& front = g_state.pendingAudioChunks.front();
        SpeakerSnapshot speaker = g_state.pendingSpeaker;
        if (front.nonPositional)
        {
            speaker = CaptureSpeakerSnapshot(GetPlayer());
        }
        else if (const auto resolved = ResolveSpeakerSnapshotForNpc(front.speakerKey, front.speakerName); resolved.has_value())
        {
            speaker = *resolved;
        }

        // Serialize speakers in a group reply: if this chunk belongs to a DIFFERENT
        // positional speaker than the one currently streaming AND that speaker's audio
        // hasn't finished playing, leave the chunk queued and retry next frame — else
        // starting the new buffer would cut the first NPC off mid-sentence. The whole
        // multi-NPC reply shares one requestId and `awaitingReply` stays true across
        // it, so the stream won't self-stop between speakers; gate on the play cursor.
        if (g_state.streamActive && g_state.streamStarted && !front.nonPositional
            && speaker.valid && g_state.streamSpeaker.valid
            && speaker.refId != g_state.streamSpeaker.refId)
        {
            DWORD playPos = 0;
            DWORD writePos = 0;
            if (g_state.streamBuffer
                && SUCCEEDED(g_state.streamBuffer->GetCurrentPosition(&playPos, &writePos))
                && playPos < g_state.streamWriteCursor)
            {
                static DWORD s_lastWaitTraceTick = 0;
                const DWORD waitNow = GetTickCount();
                if (waitNow - s_lastWaitTraceTick > 1000)
                {
                    s_lastWaitTraceTick = waitNow;
                    TraceRequestEvent(front.requestId, "stream_chunk_waiting_other_speaker",
                        {},
                        {
                            { "chunk_index", static_cast<double>(front.chunkIndex) },
                            { "chunk_speaker_ref", static_cast<double>(speaker.refId) },
                            { "stream_speaker_ref", static_cast<double>(g_state.streamSpeaker.refId) },
                        });
                }
                break;
            }
        }

        const QueuedAudioChunk chunk = std::move(g_state.pendingAudioChunks.front());
        g_state.pendingAudioChunks.pop_front();
        if (!fs::exists(chunk.wavPath))
        {
            TraceRequestEvent(chunk.requestId, "stream_chunk_wav_missing",
                { { "wav", chunk.wavPath.filename().string() } },
                { { "chunk_index", static_cast<double>(chunk.chunkIndex) } });
            continue;
        }
        auto wav = LoadWavFile(chunk.wavPath);
        if (!wav.has_value())
        {
            TraceRequestEvent(chunk.requestId, "stream_chunk_wav_unreadable",
                { { "wav", chunk.wavPath.filename().string() } },
                { { "chunk_index", static_cast<double>(chunk.chunkIndex) } });
            continue;
        }
        TraceRequestEvent(chunk.requestId, "stream_chunk_appended",
            { { "wav", chunk.wavPath.filename().string() } },
            {
                { "chunk_index", static_cast<double>(chunk.chunkIndex) },
                { "speaker_ref_id", static_cast<double>(speaker.refId) },
            });
        AppendStreamingChunk(chunk, *wav, speaker);
        // The PCM is now copied into the streaming buffer, so the WAV file is no longer
        // needed — delete it (now a cheap native delete) to keep the audio dir from
        // growing unbounded (this folder is outside MO2's overwrite, so nothing else
        // prunes it).
        std::error_code removeEc;
        fs::remove(chunk.wavPath, removeEc);
    }
}

// Per-frame: fire scheduled lip-sync + subtitle, keep the buffer spatialized, and
// stop once the utterance has fully played out.
void UpdateStreamingVoice()
{
    if (!g_state.streamActive)
    {
        return;
    }
    const DWORD now = GetTickCount();

    if (g_state.streamStarted)
    {
        // Keep the voice spatialized EVERY frame so the 3D direction + distance volume
        // track BOTH the player (listener) and the speaker as they move/turn. The single
        // streaming buffer isn't in activeSounds, so UpdateActiveSoundPositions doesn't
        // cover it. RefreshStreamingSpeakerPosition re-reads the speaker's live position
        // (following them when they walk away) or freezes on unload without rebinding.
        RefreshStreamingSpeakerPosition();
        ApplyStreamingVoiceSpatial(g_state.streamSpeaker, g_state.streamNonPositional);
        const DWORD elapsed = now - g_state.streamPlayStartTick;
        // Coalesce due mini-chunks into a lip-sync window, then drive ONE face
        // animation per window — NOT one per 200ms chunk (that restarted the FaceGen
        // animation ~5x/sec, causing lag, jank and crashes). The envelope comes from
        // the window's real PCM; visemes use the window's text slice (rough alignment
        // via the same char/time mapping as the captions).
        while (!g_state.streamLipSyncQueue.empty()
            && g_state.streamLipSyncQueue.front().startMs <= elapsed)
        {
            PendingStreamLipSync ls = std::move(g_state.streamLipSyncQueue.front());
            g_state.streamLipSyncQueue.pop_front();
            if (g_state.lipSyncAccumPcm.empty())
            {
                g_state.lipSyncAccumStartMs = ls.startMs;
                g_state.lipSyncAccumSpeaker = ls.speaker;
            }
            g_state.lipSyncAccumPcm.insert(g_state.lipSyncAccumPcm.end(), ls.pcm.begin(), ls.pcm.end());
            g_state.lipSyncAccumMs += ls.durationMs;
        }
        const bool flushLipSync = g_state.lipSyncAccumMs >= kLipSyncWindowMs
            || (g_state.streamLipSyncQueue.empty() && g_state.lipSyncAccumMs > 0);
        if (flushLipSync && !g_state.lipSyncAccumPcm.empty())
        {
            // Text slice for this window's visemes (rough word alignment by char fraction).
            std::string winText;
            const size_t totalChars = g_state.captionSourceText.size();
            const DWORD totalMs = g_state.streamCumulativeMs;
            if (totalChars > 0 && totalMs > 0)
            {
                unsigned long long a =
                    static_cast<unsigned long long>(g_state.lipSyncAccumStartMs) * totalChars / totalMs;
                unsigned long long b =
                    static_cast<unsigned long long>(g_state.lipSyncAccumStartMs + g_state.lipSyncAccumMs) * totalChars / totalMs;
                if (a > totalChars) a = totalChars;
                if (b > totalChars) b = totalChars;
                if (b > a) winText = g_state.captionSourceText.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
            }
            ApplyStreamingVoiceSpatial(g_state.lipSyncAccumSpeaker, false);
            WavData wav;
            wav.format = g_state.streamFormat;
            wav.pcmData = std::move(g_state.lipSyncAccumPcm);
            QueuedAudioChunk chunk;
            chunk.requestId = g_state.streamRequestId;
            chunk.subtitleText = winText;
            StartSpeechAnimation(wav, chunk, g_state.lipSyncAccumSpeaker, g_state.lipSyncAccumMs);
            g_state.lipSyncAccumPcm.clear();
            g_state.lipSyncAccumMs = 0;
        }
    }

    // Caption scheduler (DISPLAY ONLY): reveal each sentence's caption when the ACTUAL
    // play cursor reaches that sentence's audio (its recorded start ms in the buffer).
    // Anchored directly to playback, so captions swap 1:1 with the audio through EVERY
    // sentence of the reply and never drift. Audio is never gated or reshaped by this.
    if (g_state.streamStarted && g_debugConfig.subtitlesEnabled && !g_state.captionSegments.empty())
    {
        const DWORD byteRate = g_state.streamFormat.nAvgBytesPerSec;
        DWORD playPos = 0;
        DWORD writePos = 0;
        const bool havePlay = g_state.streamBuffer && byteRate
            && SUCCEEDED(g_state.streamBuffer->GetCurrentPosition(&playPos, &writePos));
        const unsigned long long playMs =
            havePlay ? static_cast<unsigned long long>(playPos) * 1000ULL / byteRate : 0ULL;

        // Highest segment whose audio has started, MONOTONIC (never step back, so it
        // can't flash between two). Segment 0 is anchored at ms 0 and shows immediately.
        int target = (g_state.captionCurrentIndex < 0) ? 0 : g_state.captionCurrentIndex;
        for (size_t s = 0; s < g_state.captionSegmentStartMs.size(); ++s)
        {
            if (playMs >= static_cast<unsigned long long>(g_state.captionSegmentStartMs[s]))
            {
                if (static_cast<int>(s) > target)
                {
                    target = static_cast<int>(s);
                }
            }
            else
            {
                break;
            }
        }

        // Show on advance, and refresh ~1x/sec so the subtitle never expires
        // mid-segment (that looked like the caption "ending early").
        const bool advanced = target != g_state.captionCurrentIndex;
        if (advanced || (now - g_state.captionLastShowTick) >= 1000)
        {
            const bool swapped = target != g_state.captionCurrentIndex;
            g_state.captionCurrentIndex = target;
            const std::string ascii = ToUiAscii(g_state.captionSegments[static_cast<size_t>(target)]);
            if (!ascii.empty() && ShowDialogSubtitle("", ascii, 2.0f))
            {
                g_state.subtitleShownForReply = true;
                g_state.replySubtitleText = g_state.captionSegments[static_cast<size_t>(target)];
                g_state.captionLastShowTick = now;
                if (swapped)
                {
                    TraceRequestEvent(g_state.streamRequestId, "caption_segment_shown",
                        {},
                        {
                            { "segment_index", static_cast<double>(target) },
                            { "play_ms", static_cast<double>(playMs) },
                            { "start_ms", static_cast<double>(g_state.captionSegmentStartMs[static_cast<size_t>(target)]) },
                        });
                }
            }
        }
    }

    // End: the reply is finalized, all written audio has played, and a brief grace
    // has elapsed with no new appends. While awaiting the reply, keep the buffer
    // alive (the opener's remainder and later speakers may still append).
    if (g_state.streamStarted && g_state.streamLipSyncQueue.empty() && !g_state.awaitingReply)
    {
        DWORD playPos = 0;
        DWORD writePos = 0;
        if (g_state.streamBuffer
            && SUCCEEDED(g_state.streamBuffer->GetCurrentPosition(&playPos, &writePos))
            && playPos >= g_state.streamWriteCursor
            && now - g_state.streamLastAppendTick > kStreamingVoiceEndGraceMs)
        {
            StopStreamingVoice("played_out");
        }
    }
}

bool StartQueuedAudioPlayback(const QueuedAudioChunk& chunk, const SpeakerSnapshot& speaker)
{
    g_state.lastPlaybackDiagnostics.clear();

    auto wavData = LoadWavFile(chunk.wavPath);
    if (!wavData.has_value())
    {
        LogLine("Queued chunk WAV could not be loaded: %s", chunk.wavPath.string().c_str());
        return false;
    }

    const bool force2d = chunk.nonPositional;
    if (!PlayVoiceWav(chunk.wavPath, speaker, &*wavData, force2d))
    {
        LogLine("DirectSound playback failed for queued chunk %s.", chunk.wavPath.string().c_str());
        return false;
    }

    const DWORD durationMs = GetWavDurationMs(*wavData);
    const std::string holdNpcKey = chunk.speakerKey.empty() ? g_state.lastNpcKey : chunk.speakerKey;
    const std::string holdNpcName = chunk.speakerName.empty() ? g_state.lastNpcName : chunk.speakerName;
    const bool skipConversationHold = !chunk.requestId.empty()
        && g_state.movementActionRequestIds.find(chunk.requestId) != g_state.movementActionRequestIds.end();
    if (!chunk.nonPositional)
    {
        RememberNpcTarget(holdNpcKey, holdNpcName, speaker);
        if (skipConversationHold)
        {
            TraceRequestEvent(chunk.requestId, "conversation_hold_skipped_for_movement_action",
                {
                    { "speaker_key", holdNpcKey },
                    { "speaker_name", holdNpcName },
                },
                {
                    { "speaker_ref_id", static_cast<double>(speaker.refId) },
                },
                {});
        }
        else if (!g_state.conversationHold.active || g_state.conversationHold.speaker.refId != speaker.refId)
        {
            EngageConversationHold(holdNpcKey, holdNpcName, speaker);
        }
        else
        {
            g_state.conversationHold.npcKey = holdNpcKey;
            g_state.conversationHold.npcName = holdNpcName;
            g_state.conversationHold.speaker = speaker;
            g_state.conversationHold.releaseTick = 0;
        }
    }
    else if (!chunk.requestId.empty())
    {
        TraceRequestEvent(chunk.requestId, "conversation_hold_skipped_for_non_positional_audio",
            {
                { "speaker_key", holdNpcKey },
                { "speaker_name", holdNpcName },
            },
            {
                { "speaker_ref_id", static_cast<double>(speaker.refId) },
            },
            {});
    }
    g_state.activeSpeechUntilTick = GetTickCount() + (durationMs ? durationMs + kStreamedSpeechEndPaddingMs : 900);
    const bool used3d = g_debugConfig.directSound3dEnabled && !chunk.nonPositional;
    TraceRequestEvent(chunk.requestId, "audio_playback_started",
        {
            { "audio_file", chunk.audioFile },
            { "speaker_key", chunk.speakerKey },
            { "speaker_name", chunk.speakerName },
            { "published_at", chunk.publishedAtIso },
        },
        {
            { "chunk_index", static_cast<double>(chunk.chunkIndex) },
            { "duration_ms", static_cast<double>(durationMs) },
            { "speaker_ref_id", static_cast<double>(speaker.refId) },
            { "subtitle_length", static_cast<double>(chunk.subtitleText.size()) },
            { "streaming_chunk_overlap_ms", static_cast<double>(g_debugConfig.streamingChunkOverlapMs) },
        },
        {
            { "directsound_3d", used3d },
            { "directsound_software_buffer", g_debugConfig.directSoundSoftwareBufferEnabled },
            { "non_positional_audio", chunk.nonPositional },
        });
    if (!chunk.nonPositional)
    {
        StartSpeechAnimation(*wavData, chunk, speaker, durationMs);
    }

    const std::string previousReplySubtitle = g_state.replySubtitleText;
    std::string subtitleSource = chunk.subtitleText;
    const bool reusedReplySubtitle = subtitleSource.empty() && !g_state.replySubtitleText.empty();
    if (!subtitleSource.empty())
    {
        g_state.replySubtitleText = subtitleSource;
    }
    else
    {
        subtitleSource = g_state.replySubtitleText;
    }

    const std::string subtitleText = ToUiAscii(subtitleSource);
    const bool subtitleChanged = subtitleSource != previousReplySubtitle;
    const float subtitleSeconds = SubtitleDuration(subtitleText);
    if (g_debugConfig.subtitlesEnabled
        && !subtitleText.empty()
        && (!g_state.subtitleShownForReply || subtitleChanged)
        && ShowDialogSubtitle("", subtitleText, subtitleSeconds))
    {
        g_state.subtitleShownForReply = true;
        if (reusedReplySubtitle)
        {
            LogLine("Reused previous reply subtitle for continuation chunk %d.", chunk.chunkIndex);
        }
    }

    std::ostringstream diag;
    diag << "playback_mode=" << (chunk.nonPositional ? "directsound_2d_non_positional" : (g_debugConfig.directSound3dEnabled ? "directsound_3d_stream" : "directsound_2d_stream")) << "\n";
    diag << "playback_software_buffer=" << (g_debugConfig.directSoundSoftwareBufferEnabled ? 1 : 0) << "\n";
    diag << "playback_non_positional_audio=" << (chunk.nonPositional ? 1 : 0) << "\n";
    diag << "playback_chunk_index=" << chunk.chunkIndex << "\n";
    diag << "playback_audio_file=" << EscapeForDiag(chunk.audioFile) << "\n";
    diag << "playback_duration_ms=" << durationMs << "\n";
    diag << "playback_result=started\n";
    g_state.lastPlaybackDiagnostics = diag.str();

    return true;
}

bool ExecuteConsoleCommand(TESObjectREFR* callingRef, const std::string& command)
{
    if (!callingRef || !EnsureConsoleCommandHelper())
    {
        return false;
    }

    if (!g_scriptInterface->CallFunctionAlt(g_consoleCommandScript, callingRef, 1, command.c_str()))
    {
        LogLine("CallFunctionAlt failed for Console helper: %s", command.c_str());
        return false;
    }

    return true;
}

std::pair<std::string, std::string> ResolveRefVoiceTypeMetadata(TESObjectREFR* ref)
{
    if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESNPC)
    {
        return std::make_pair(std::string(), std::string());
    }

    auto* actorBase = static_cast<TESActorBase*>(ref->baseForm);
    BGSVoiceType* voiceType = actorBase->baseData.GetVoiceType();
    if (!voiceType)
    {
        voiceType = actorBase->baseData.voiceType;
    }
    if (!voiceType)
    {
        return std::make_pair(std::string(), std::string());
    }

    const std::string voiceTypeName = GetFormNameSafe(voiceType);
    return std::make_pair(Slugify(voiceTypeName), voiceTypeName);
}

void ConsumeAudioChunks()
{
    std::error_code ec;
    if (!fs::exists(OutboxChunkDir(), ec))
    {
        return;
    }

    std::vector<fs::path> chunkPaths;
    for (const auto& entry : fs::directory_iterator(OutboxChunkDir(), ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
        {
            chunkPaths.push_back(entry.path());
        }
    }

    std::sort(chunkPaths.begin(), chunkPaths.end());

    for (const auto& chunkPath : chunkPaths)
    {
        std::ifstream in(chunkPath, std::ios::binary);
        if (!in)
        {
            continue;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(line);
        }

        if (lines.size() < 5)
        {
            fs::remove(chunkPath, ec);
            continue;
        }

        const std::string requestId = Trim(lines[0]);
        const int chunkIndex = std::atoi(Trim(lines[1]).c_str());
        if (requestId != g_state.activeRequestId || chunkIndex <= g_state.lastAudioChunkIndex)
        {
            TraceRequestEvent(requestId, "audio_chunk_dropped_stale",
                {
                    { "file", chunkPath.filename().string() },
                    { "active_request_id", g_state.activeRequestId },
                },
                {
                    { "chunk_index", static_cast<double>(chunkIndex) },
                    { "last_audio_chunk_index", static_cast<double>(g_state.lastAudioChunkIndex) },
                });
            fs::remove(chunkPath, ec);
            continue;
        }

        g_state.lastBridgeActivityTick = GetTickCount();
        g_state.sawBridgeActivity = true;

        const std::string speakerKey = lines.size() > 2 ? Trim(lines[2]) : g_state.pendingNpcKey;
        const std::string speakerName = lines.size() > 3 ? Trim(lines[3]) : g_state.pendingNpcName;
        const std::string audioFile = Trim(lines[4]);
        const std::string subtitleText = lines.size() > 5 ? Trim(lines[5]) : "";
        const std::string publishedAtIso = lines.size() > 6 ? Trim(lines[6]) : "";
        bool nonPositionalAudio = ToLowerAscii(speakerKey) == "todd";
        int captionMaxChars = -1; // display-only caption split hint from the backend
        for (size_t metadataIndex = 7; metadataIndex < lines.size(); ++metadataIndex)
        {
            const std::string token = Trim(lines[metadataIndex]);
            const size_t equals = token.find('=');
            if (equals == std::string::npos)
            {
                continue;
            }
            const std::string mkey = Trim(token.substr(0, equals));
            const std::string mval = Trim(token.substr(equals + 1));
            nonPositionalAudio = IsNonPositionalChunkMetadata(mkey, mval, nonPositionalAudio);
            if (mkey == "caption_max_chars")
            {
                captionMaxChars = std::atoi(mval.c_str());
            }
        }

        g_state.lastAudioChunkIndex = chunkIndex;
        if (!audioFile.empty())
        {
            TraceRequestEvent(requestId, "audio_chunk_meta_seen",
                {
                    { "audio_file", audioFile },
                    { "speaker_key", speakerKey },
                    { "speaker_name", speakerName },
                    { "published_at", publishedAtIso },
                },
                {
                    { "chunk_index", static_cast<double>(chunkIndex) },
                    { "subtitle_length", static_cast<double>(subtitleText.size()) },
                },
                {
                    { "non_positional_audio", nonPositionalAudio },
                });
            g_state.pendingAudioChunks.push_back(QueuedAudioChunk{
                requestId,
                AudioDir() / audioFile,
                audioFile,
                speakerKey,
                speakerName,
                subtitleText,
                publishedAtIso,
                chunkIndex,
                nonPositionalAudio,
                captionMaxChars,
            });
            g_state.streamedAudioSeenForReply = true;
        }
        fs::remove(chunkPath, ec);
    }
}

void PlayQueuedAudioChunk()
{
    // Phase 3: feed the single continuous streaming buffer instead of the static
    // per-chunk scheduler. UpdateStreamingVoice (each frame) drives lip-sync + end.
    if (g_debugConfig.singleBufferStreaming)
    {
        DrainChunksToStreamingVoice();
        return;
    }

    const DWORD now = GetTickCount();
    if (g_state.activeSpeechUntilTick && now < g_state.activeSpeechUntilTick)
    {
        if (g_state.pendingAudioChunks.empty())
        {
            return;
        }

        const DWORD remainingMs = g_state.activeSpeechUntilTick - now;
        if (remainingMs > g_debugConfig.streamingChunkOverlapMs)
        {
            return;
        }
    }

    g_state.activeSpeechUntilTick = 0;
    while (!g_state.pendingAudioChunks.empty())
    {
        const QueuedAudioChunk chunk = std::move(g_state.pendingAudioChunks.front());
        g_state.pendingAudioChunks.pop_front();

        if (!fs::exists(chunk.wavPath))
        {
            LogLine("Queued chunk WAV missing: %s", chunk.wavPath.string().c_str());
            TraceRequestEvent(chunk.requestId, "audio_chunk_missing",
                {
                    { "audio_file", chunk.audioFile },
                },
                {
                    { "chunk_index", static_cast<double>(chunk.chunkIndex) },
                });
            continue;
        }

        SpeakerSnapshot resolvedSpeaker = g_state.pendingSpeaker;
        if (chunk.nonPositional)
        {
            resolvedSpeaker = CaptureSpeakerSnapshot(GetPlayer());
        }
        else if (const auto chunkSpeaker = ResolveSpeakerSnapshotForNpc(chunk.speakerKey, chunk.speakerName); chunkSpeaker.has_value())
        {
            resolvedSpeaker = *chunkSpeaker;
        }

        if (StartQueuedAudioPlayback(chunk, resolvedSpeaker))
        {
            return;
        }
    }
}

void ResetRuntimeState()
{
    LoadDebugConfigIfNeeded(true);
    LoadHotkeysConfigIfNeeded(true);
    CancelHttpTurn();
    ResetPersonaStateForSession();
    AbortVoiceCapture("runtime_reset_voice", false);
    ReleaseConversationHold("runtime_reset");
    ClearCombatTracking("runtime_reset");
    StopSpeechAnimation();
    StopGuitarPerformance("runtime_reset");
    ClearDialogSubtitle();
    g_state.awaitingInput = false;
    g_state.bridgeTextInputOwned = false;
    g_state.awaitingReply = false;
    g_state.awaitingVoiceReply = false;
    g_state.inputMenuSeenVisible = false;
    g_state.inputStartedTick = 0;
    g_state.staleTextInputCloseRetryTick = 0;
    g_state.gameWindowFocusedLastFrame = false;
    g_state.ignoreHotkeysUntilTick = 0;
    ClearTextInputKeyWatcher();
    g_state.pendingNpcKey.clear();
    g_state.pendingNpcName.clear();
    g_state.pendingSpeaker = {};
    g_state.replyStartedTick = 0;
    g_state.lastBridgeActivityTick = 0;
    g_state.sawBridgeActivity = false;
    g_state.activeRequestId.clear();
    g_state.lastAudioChunkIndex = -1;
    g_state.subtitleShownForReply = false;
    g_state.activeSpeechUntilTick = 0;
    g_state.replySubtitleText.clear();
    g_state.streamedAudioSeenForReply = false;
    g_state.pendingAudioChunks.clear();
    StopStreamingVoice("reply_state_reset");
    g_state.movementActionRequestIds.clear();
    g_state.lastNpcKey.clear();
    g_state.lastNpcName.clear();
    g_state.lastNpcSpeaker = {};
    g_state.npcSpeakersByKey.clear();
    g_state.keyDownLastFrame = false;
    g_state.voiceCapture.keyDownLastFrame = false;
    g_state.voiceCapture.adminKeyDownLastFrame = false;
    g_state.voiceCapture.adminMode = false;
    g_state.lastPlaybackDiagnostics.clear();
    g_state.lastRuntimeHeartbeatTick = 0;
    g_state.runtimeHeartbeatFrame = 0;
    g_state.saveStateSyncPending = false;
    g_state.saveStateSyncEventId.clear();
    g_state.saveStateSyncType.clear();
    g_state.saveStateSyncLastPollTick = 0;
    g_state.saveStateSyncHudMessageTick = 0;

    std::error_code ec;
    fs::remove(UiSubmitPath(), ec);
    fs::remove(ScriptRunnerTracePath(), ec);

    CleanupFinishedSounds();
    for (auto& sound : g_state.activeSounds)
    {
        if (sound.buffer3d)
        {
            sound.buffer3d->Release();
            sound.buffer3d = nullptr;
        }
        if (sound.buffer)
        {
            sound.buffer->Stop();
            sound.buffer->Release();
            sound.buffer = nullptr;
        }
    }
    g_state.activeSounds.clear();
    ShutdownDirectSound();
    WriteRuntimeHeartbeatIfNeeded(true);

}

// Process a terminal/streaming ResponsePayload through the shared reply pipeline
// (diagnostics, action triggering, final-audio fallback, subtitles, state teardown).
// Used by both the file transport (ConsumeReply) and the HTTP transport
// (DrainHttpInbox), so the two paths behave identically once a payload exists.
void HandleReplyPayload(const ResponsePayload* responsePtr)
{
    const auto response = responsePtr;
    std::ostringstream diag;
    diag << "response_ok=" << (response->ok ? 1 : 0) << "\n";
    diag << "response_player_text=" << EscapeForDiag(response->playerText) << "\n";
    diag << "response_text=" << EscapeForDiag(response->text) << "\n";
    diag << "response_error=" << EscapeForDiag(response->error) << "\n";
    diag << "response_status=" << response->statusCode << "\n";
    diag << "game_master_action=" << EscapeForDiag(response->gameMasterAction) << "\n";
    diag << "game_master_confidence=" << response->gameMasterConfidence << "\n";
    diag << "game_master_should_trigger=" << (response->gameMasterShouldTrigger ? 1 : 0) << "\n";
    diag << "action_npc_key=" << EscapeForDiag(ActionNpcKey(*response)) << "\n";
    diag << "action_npc_name=" << EscapeForDiag(ActionNpcName(*response)) << "\n";
    diag << "non_positional_audio=" << (response->nonPositionalAudio ? 1 : 0) << "\n";
    g_state.lastBridgeActivityTick = GetTickCount();
    g_state.sawBridgeActivity = true;

    if (!response->isFinal)
    {
        ShowRecognizedPlayerSubtitleIfNeeded(*response);
        TraceRequestEvent(response->requestId.empty() ? g_state.activeRequestId : response->requestId, "response_partial_seen",
            {
                { "npc_key", response->npcKey },
            },
            {
                { "text_length", static_cast<double>(response->text.size()) },
                { "audio_chunk_index", static_cast<double>(response->audioChunkIndex) },
            });
        diag << "partial_shown=0\n";
        WriteDiagnostics(diag.str());
        return;
    }

    if (!response->ok)
    {
        ClearOutboxArtifacts("response_failed");
        g_state.awaitingReply = false;
        g_state.replyStartedTick = 0;
        g_state.lastBridgeActivityTick = 0;
        g_state.sawBridgeActivity = false;
        g_state.activeRequestId.clear();
        g_state.awaitingVoiceReply = false;
        TraceRequestEvent(response->requestId, "response_failed",
            {
                { "error", response->error },
            });
        const std::string errorText = response->error.empty() ? "Bridge error." : ("Bridge error: " + ToUiAscii(response->error));
        ShowHudMessage(errorText);
        diag << "played_audio=0\n";
        WriteDiagnostics(diag.str());
        return;
    }

    const std::string speaker = ToUiAscii(response->npcName.empty() ? g_state.pendingNpcName : response->npcName);
    const std::string line = ToUiAscii(response->text);
    ShowRecognizedPlayerSubtitleIfNeeded(*response);
    g_state.awaitingVoiceReply = false;
    if (g_debugConfig.drainQueuedChunksAfterFinal)
    {
        ConsumeAudioChunks();
    }
    TraceRequestEvent(response->requestId, "response_final_seen",
        {
            { "npc_key", response->npcKey },
            { "npc_name", response->npcName },
            { "audio_file", response->audioFile },
            { "action_npc_key", ActionNpcKey(*response) },
            { "action_npc_name", ActionNpcName(*response) },
        },
        {
            { "text_length", static_cast<double>(response->text.size()) },
            { "audio_chunk_index", static_cast<double>(response->audioChunkIndex) },
        },
        {
            { "response_ok", response->ok },
            { "non_positional_audio", response->nonPositionalAudio },
        });
    if (!g_state.pendingAudioChunks.empty())
    {
        TraceRequestEvent(response->requestId, "audio_queue_pending_after_final",
            {},
            {
                { "pending_chunks", static_cast<double>(g_state.pendingAudioChunks.size()) },
            },
            {
                { "drain_after_final_enabled", g_debugConfig.drainQueuedChunksAfterFinal },
            });
    }
    const bool suppressedGameMasterAction = false;
    std::string triggeredGameMasterAction;
    const bool triggeredGameMaster = TriggerGameMasterAction(*response, &triggeredGameMasterAction);
    const bool triggeredCombat = triggeredGameMaster && triggeredGameMasterAction == "ATTACK";
    const bool triggeredFollow = triggeredGameMaster && triggeredGameMasterAction == "FOLLOW";
    const bool triggeredStopFollow = triggeredGameMaster && triggeredGameMasterAction == "STOP_FOLLOW";
    if (triggeredCombat)
    {
        InterruptBridgeReplyAndPlayback("game_master_attack");
    }
    bool playedAudio = false;
    const bool alreadyStreamingAudio = g_state.streamedAudioSeenForReply
        || !g_state.pendingAudioChunks.empty()
        || HasPendingChunkFiles()
        || (g_state.activeSpeechUntilTick && GetTickCount() < g_state.activeSpeechUntilTick);
    if (!triggeredCombat && !response->audioFile.empty())
    {
        if (!alreadyStreamingAudio)
        {
            const QueuedAudioChunk finalChunk{
                response->requestId,
                AudioDir() / response->audioFile,
                response->audioFile,
                response->npcKey,
                speaker,
                line,
                "",
                response->audioChunkIndex,
                response->nonPositionalAudio,
            };
            SpeakerSnapshot finalSpeaker = g_state.pendingSpeaker;
            if (response->nonPositionalAudio)
            {
                finalSpeaker = CaptureSpeakerSnapshot(GetPlayer());
            }
            else if (const auto resolvedFinalSpeaker = ResolveSpeakerSnapshotForNpc(response->npcKey, speaker); resolvedFinalSpeaker.has_value())
            {
                finalSpeaker = *resolvedFinalSpeaker;
            }
            playedAudio = StartQueuedAudioPlayback(finalChunk, finalSpeaker);
            if (!playedAudio)
            {
                LogLine("Reply playback failed for %s.", response->audioFile.c_str());
            }
        }
        else
        {
            playedAudio = true;
            TraceRequestEvent(response->requestId, "final_audio_already_streaming",
                {
                    { "audio_file", response->audioFile },
                },
                {
                    { "audio_chunk_index", static_cast<double>(response->audioChunkIndex) },
                });
        }
        if (playedAudio && g_debugConfig.subtitlesEnabled && !g_state.subtitleShownForReply)
        {
            if (ShowDialogSubtitle("", line, SubtitleDuration(line)))
            {
                g_state.subtitleShownForReply = true;
            }
        }
    }

    ClearOutboxArtifacts("response_final");
    g_state.awaitingReply = false;
    g_state.replyStartedTick = 0;
    g_state.lastBridgeActivityTick = 0;
    g_state.sawBridgeActivity = false;
    g_state.activeRequestId.clear();

    diag << "game_master_action_suppressed=" << (suppressedGameMasterAction ? 1 : 0) << "\n";
    diag << "triggered_combat=" << (triggeredCombat ? 1 : 0) << "\n";
    diag << "triggered_follow=" << (triggeredFollow ? 1 : 0) << "\n";
    diag << "triggered_stop_follow=" << (triggeredStopFollow ? 1 : 0) << "\n";
    diag << "played_audio=" << (playedAudio ? 1 : 0) << "\n";
    if (!g_state.lastPlaybackDiagnostics.empty())
    {
        diag << g_state.lastPlaybackDiagnostics;
    }
    WriteDiagnostics(diag.str());
    WriteRuntimeHeartbeatIfNeeded(true);
}

void ConsumeReply()
{
    const auto response = ReadResponse();
    if (!response.has_value())
    {
        return;
    }
    HandleReplyPayload(&response.value());
}

// HTTP transport consumer (runs on the GAME thread each frame while awaitingReply).
// Pulls everything the worker has staged in g_httpInbox and feeds it through the SAME
// playback / reply pipelines the file transport uses. Audio chunks become normal
// QueuedAudioChunk entries (already staged to temp WAVs by the worker); the terminal
// reply is converted to a ResponsePayload and handed to HandleReplyPayload (which is
// what fires actions, plays any final audio, shows subtitles, and tears down state).
void DrainHttpInbox()
{
    std::deque<QueuedAudioChunk> audioChunks;
    std::deque<HttpPendingAction> actions;
    std::optional<HttpPendingReply> reply;
    bool sawActivity = false;

    {
        std::lock_guard<std::mutex> lock(g_httpMutex);
        audioChunks.swap(g_httpInbox.audioChunks);
        actions.swap(g_httpInbox.actions);
        sawActivity = g_httpInbox.sawActivity;
        g_httpInbox.sawActivity = false;
        // The reply is consumed once: take it but leave finished as-is.
        if (g_httpInbox.reply.has_value())
        {
            reply = std::move(g_httpInbox.reply);
            g_httpInbox.reply.reset();
        }
    }

    if (sawActivity)
    {
        g_state.lastBridgeActivityTick = GetTickCount();
        g_state.sawBridgeActivity = true;
    }

    // Stage streamed audio chunks into the shared playback queue (mirror ConsumeAudioChunks).
    for (auto& chunk : audioChunks)
    {
        if (chunk.requestId != g_state.activeRequestId || chunk.chunkIndex <= g_state.lastAudioChunkIndex)
        {
            std::error_code rmEc;
            fs::remove(chunk.wavPath, rmEc);
            continue;
        }
        g_state.lastAudioChunkIndex = chunk.chunkIndex;
        g_state.streamedAudioSeenForReply = true;
        g_state.pendingAudioChunks.push_back(std::move(chunk));
    }

    // Standalone action events are advisory: the terminal reply's gameMaster field is
    // authoritative and HandleReplyPayload fires it (matching the file outbox), so we
    // only trace these to avoid double-triggering.
    for (const auto& action : actions)
    {
        TraceRequestEvent(action.requestId, "http_action_event_seen",
            {
                { "action", action.action },
                { "npc_key", action.actionNpcKey.empty() ? action.npcKey : action.actionNpcKey },
            },
            {
                { "confidence", action.confidence },
            },
            {
                { "should_trigger", action.shouldTrigger },
            });
    }

    if (!reply.has_value())
    {
        return;
    }

    // Convert the captured reply into a ResponsePayload and run the shared pipeline.
    ResponsePayload payload{};
    payload.ok = reply->ok;
    payload.statusCode = reply->ok ? 1 : 0;
    payload.isFinal = true;
    payload.requestId = reply->requestId.empty() ? g_state.activeRequestId : reply->requestId;
    payload.npcKey = reply->npcKey;
    payload.npcName = reply->npcName;
    payload.text = reply->text;
    payload.error = reply->error;
    payload.playerText = reply->playerText;
    // The HTTP path streams audio via chunks; there is no separate final audio file.
    payload.audioFile = reply->audioFile;
    payload.audioChunkIndex = g_state.lastAudioChunkIndex;
    payload.nonPositionalAudio = reply->nonPositionalAudio;
    payload.gameMasterAction = reply->gameMasterAction;
    payload.gameMasterConfidence = reply->gameMasterConfidence;
    payload.gameMasterShouldTrigger = reply->gameMasterShouldTrigger;
    payload.actionNpcKey = reply->actionNpcKey;
    payload.actionNpcName = reply->actionNpcName;

    HandleReplyPayload(&payload);
}

bool HasPendingChunkFiles()
{
    std::error_code ec;
    if (!fs::exists(OutboxChunkDir(), ec))
    {
        return false;
    }

    for (const auto& entry : fs::directory_iterator(OutboxChunkDir(), ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
        {
            return true;
        }
    }

    return false;
}

void RecoverStaleReplyState()
{
    if (!g_state.awaitingReply)
    {
        return;
    }

    const bool hasDiskActivity = fs::exists(InboxPath()) || fs::exists(OutboxPath()) || HasPendingChunkFiles();
    const bool hasAudioActivity = !g_state.pendingAudioChunks.empty()
        || !g_state.activeSounds.empty()
        || (g_state.activeSpeechUntilTick && GetTickCount() < g_state.activeSpeechUntilTick);
    if (hasDiskActivity || hasAudioActivity)
    {
        return;
    }

    const DWORD now = GetTickCount();
    const DWORD lastActivityTick = g_state.lastBridgeActivityTick ? g_state.lastBridgeActivityTick : g_state.replyStartedTick;
    const DWORD timeoutMs = g_state.sawBridgeActivity ? 60000 : 45000;
    if (!lastActivityTick || now - lastActivityTick < timeoutMs)
    {
        return;
    }

    LogLine("Recovering stale bridge reply state for request %s after %lu ms without new bridge activity.", g_state.activeRequestId.c_str(), static_cast<unsigned long>(timeoutMs));
    ShowHudMessage("Recovered a stale bridge reply state.");
    CancelHttpTurn();
    ReleaseConversationHold("stale_reply_recovered");
    g_state.awaitingReply = false;
    g_state.awaitingVoiceReply = false;
    g_state.replyStartedTick = 0;
    g_state.lastBridgeActivityTick = 0;
    g_state.sawBridgeActivity = false;
    g_state.activeRequestId.clear();
    g_state.lastAudioChunkIndex = -1;
    g_state.subtitleShownForReply = false;
    g_state.activeSpeechUntilTick = 0;
    g_state.replySubtitleText.clear();
    g_state.streamedAudioSeenForReply = false;
    StopSpeechAnimation();
    ClearDialogSubtitle();
    g_state.pendingAudioChunks.clear();
    StopStreamingVoice("reply_state_reset");
    g_state.pendingNpcKey.clear();
    g_state.pendingNpcName.clear();
    g_state.pendingSpeaker = {};
}

void ForceCloseTextInputMenu(const char* reason);

bool OpenInGameTextInput(const std::string& npcName)
{
    if (!EnsureOpenTextInputScript())
    {
        return false;
    }

    std::error_code ec;
    fs::remove(UiSubmitPath(), ec);
    if (g_state.bridgeTextInputOwned || IsTextInputMenuActive())
    {
        ForceCloseTextInputMenu("opening new bridge text input");
        if (IsTextInputMenuActive())
        {
            LogLine("Refusing to open a new bridge TextEditMenu while the previous TextEditMenu is still visible.");
            g_state.bridgeTextInputOwned = true;
            g_state.staleTextInputCloseRetryTick = GetTickCount() + 250;
            return false;
        }
        g_state.bridgeTextInputOwned = false;
    }

    if (!g_scriptInterface->CallFunctionAlt(g_openTextInputScript, GetPlayer(), 1, npcName.c_str()))
    {
        LogLine("CallFunctionAlt failed while opening TextEditMenu.");
        return false;
    }

    g_state.bridgeTextInputOwned = true;
    return true;
}

void ForceCloseTextInputMenu(const char* reason)
{
    if (!IsTextInputMenuActive())
    {
        g_state.bridgeTextInputOwned = false;
        return;
    }

    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        LogLine("TextEditMenu stayed visible after %s, but PlayerRef was unavailable.", reason ? reason : "input");
        return;
    }

    if (EnsureCloseTextInputScript() && g_scriptInterface->CallFunctionAlt(g_closeTextInputScript, player, 0))
    {
        LogLine("Requested TextEditMenu close after %s.", reason ? reason : "input");
        if (!IsTextInputMenuActive())
        {
            g_state.bridgeTextInputOwned = false;
            return;
        }
    }

    LogLine("CloseActiveMenu helper unavailable or failed after %s; trying CloseAllMenus fallback.", reason ? reason : "input");
    if (ExecuteConsoleCommand(player, "CloseAllMenus"))
    {
        LogLine("Requested TextEditMenu close with CloseAllMenus fallback after %s.", reason ? reason : "input");
        g_state.bridgeTextInputOwned = IsTextInputMenuActive();
        return;
    }

    LogLine("TextEditMenu stayed visible after %s; all close helpers failed.", reason ? reason : "input");
    g_state.bridgeTextInputOwned = IsTextInputMenuActive();
}

void ResetTextInputKeyWatcher()
{
    // Hardwired VK_RETURN (not g_hotkeys.chatVk): this watches the TextEditMenu's
    // own submit key, which is always Enter regardless of the rebindable
    // open-chat hotkey.
    g_state.inputEnterDownLastFrame = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    g_state.inputEscapeDownLastFrame = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    g_state.inputEmptyEnterCancelTick = 0;
}

void ClearTextInputKeyWatcher()
{
    g_state.inputEnterDownLastFrame = false;
    g_state.inputEscapeDownLastFrame = false;
    g_state.inputEmptyEnterCancelTick = 0;
}

void PrimeHotkeyEdgeStateFromKeyboard()
{
    g_state.keyDownLastFrame = (GetAsyncKeyState(g_hotkeys.chatVk) & 0x8000) != 0;
    g_state.adminKeyDownLastFrame = (GetAsyncKeyState(g_hotkeys.adminChatVk) & 0x8000) != 0;
    g_state.voiceCapture.keyDownLastFrame = (GetAsyncKeyState(g_hotkeys.voiceVk) & 0x8000) != 0;
    g_state.voiceCapture.adminKeyDownLastFrame = (GetAsyncKeyState(g_hotkeys.adminVoiceVk) & 0x8000) != 0;
    ResetTextInputKeyWatcher();
}

void SuppressBridgeHotkeysAfterTextInputClose(DWORD milliseconds = 350)
{
    PrimeHotkeyEdgeStateFromKeyboard();
    g_state.ignoreHotkeysUntilTick = GetTickCount() + milliseconds;
}

bool RecoverStaleTextInputMenu(const char* reason)
{
    if (g_state.awaitingInput)
    {
        g_state.staleTextInputCloseRetryTick = 0;
        return false;
    }

    if (!g_state.bridgeTextInputOwned && !IsTextInputMenuActive())
    {
        g_state.staleTextInputCloseRetryTick = 0;
        return false;
    }

    if (!IsTextInputMenuActive())
    {
        g_state.bridgeTextInputOwned = false;
        g_state.staleTextInputCloseRetryTick = 0;
        return false;
    }

    const DWORD now = GetTickCount();
    if (!g_state.staleTextInputCloseRetryTick || now >= g_state.staleTextInputCloseRetryTick)
    {
        ForceCloseTextInputMenu(reason ? reason : "stale text input");
        g_state.bridgeTextInputOwned = IsTextInputMenuActive();
        g_state.staleTextInputCloseRetryTick = now + 500;
    }

    PrimeHotkeyEdgeStateFromKeyboard();
    g_state.ignoreHotkeysUntilTick = now + 250;
    return true;
}

void CancelAwaitingTextInput(const char* reason, const char* diagnosticKey)
{
    std::error_code ec;
    fs::remove(UiSubmitPath(), ec);
    g_state.awaitingInput = false;
    ForceCloseTextInputMenu(reason ? reason : "input cancelled");
    g_state.bridgeTextInputOwned = IsTextInputMenuActive();
    g_state.inputMenuSeenVisible = false;
    ClearTextInputKeyWatcher();
    g_state.inputStartedTick = 0;
    g_state.staleTextInputCloseRetryTick = g_state.bridgeTextInputOwned ? GetTickCount() + 250 : 0;
    g_state.pendingNpcKey.clear();
    g_state.pendingNpcName.clear();
    g_state.pendingSpeaker = {};
    fs::remove(UiSubmitPath(), ec);
    SuppressBridgeHotkeysAfterTextInputClose();
    ReleaseConversationHold(reason);
    LogLine("Cancelled text input state after %s.", reason ? reason : "input");
    WriteDiagnostics(std::string(diagnosticKey ? diagnosticKey : "input_cancelled") + "=1\n");
}

bool ConsumeTextInputMenuCloseHotkeys(bool menuVisible)
{
    // Hardwired VK_RETURN: the TextEditMenu always submits on Enter (see
    // ResetTextInputKeyWatcher).
    const bool enterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool enterPressed = enterDown && !g_state.inputEnterDownLastFrame;
    const bool escapePressed = escapeDown && !g_state.inputEscapeDownLastFrame;
    g_state.inputEnterDownLastFrame = enterDown;
    g_state.inputEscapeDownLastFrame = escapeDown;

    if (!menuVisible)
    {
        g_state.inputEmptyEnterCancelTick = 0;
        if (escapePressed || (escapeDown && !g_state.inputStartedTick))
        {
            std::error_code ec;
            fs::remove(UiSubmitPath(), ec);
            CancelAwaitingTextInput("input_escape_hidden", "input_cancelled");
            return true;
        }
        return false;
    }

    if (escapePressed)
    {
        std::error_code ec;
        fs::remove(UiSubmitPath(), ec);
        CancelAwaitingTextInput("input_escape", "input_cancelled");
        ForceCloseTextInputMenu("input escape");
        return true;
    }

    std::error_code ec;
    const bool submitFileExists = fs::exists(UiSubmitPath(), ec);
    if (enterPressed && !submitFileExists)
    {
        g_state.inputEmptyEnterCancelTick = GetTickCount() + kTextInputEmptySubmitGraceMs;
    }

    if (!g_state.inputEmptyEnterCancelTick)
    {
        return false;
    }

    ec.clear();
    if (fs::exists(UiSubmitPath(), ec))
    {
        g_state.inputEmptyEnterCancelTick = 0;
        return false;
    }

    if (GetTickCount() < g_state.inputEmptyEnterCancelTick)
    {
        return false;
    }

    CancelAwaitingTextInput("input_empty", "input_empty");
    ForceCloseTextInputMenu("empty input");
    return true;
}

void ConsumeSubmittedInput()
{
    if (!g_state.awaitingInput)
    {
        return;
    }

    const bool menuVisible = IsTextInputMenuActive();
    if (menuVisible)
    {
        g_state.inputMenuSeenVisible = true;
    }

    if (ConsumeTextInputMenuCloseHotkeys(menuVisible))
    {
        return;
    }

    const auto submitted = ReadSubmittedInput();
    if (!submitted.has_value())
    {
        if (!menuVisible)
        {
            const DWORD now = GetTickCount();
            const bool invisibleTooLong = g_state.inputStartedTick
                && (now - g_state.inputStartedTick) >= kTextInputInvisibleRecoveryMs;
            if (g_state.inputMenuSeenVisible || invisibleTooLong)
            {
                CancelAwaitingTextInput("input_cancelled", "input_cancelled");
            }
        }
        return;
    }

    std::error_code ec;
    fs::remove(UiSubmitPath(), ec);

    g_state.awaitingInput = false;
    g_state.inputMenuSeenVisible = false;
    ClearTextInputKeyWatcher();
    g_state.inputStartedTick = 0;
    g_state.staleTextInputCloseRetryTick = 0;

    if (menuVisible)
    {
        ForceCloseTextInputMenu("submitted input");
        g_state.bridgeTextInputOwned = IsTextInputMenuActive();
        ec.clear();
        fs::remove(UiSubmitPath(), ec);
    }
    else
    {
        g_state.bridgeTextInputOwned = false;
    }

    if (submitted->empty())
    {
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        SuppressBridgeHotkeysAfterTextInputClose();
        ReleaseConversationHold("input_empty");
        WriteDiagnostics("input_empty=1\n");
        return;
    }

    ClearIdleOutboxArtifacts("text_submit_idle_stale_response");
    if (HasQueuedOrPlayingReply())
    {
        InterruptBridgeReplyAndPlayback("text_submit_interrupt");
    }

    const LocationSnapshot location = CapturePlayerLocation();
    PlayerCharacter* player = GetPlayer();
    const std::string requestMetadata = BuildTextRequestMetadata(player, &g_state.pendingSpeaker, &location);
    LogLine("Text request metadata: %s", requestMetadata.empty() ? "<empty>" : requestMetadata.c_str());

    if (!WriteRequest(g_state.pendingNpcKey, g_state.pendingNpcName, submitted.value(), location, requestMetadata))
    {
        ShowHudMessage("Bridge request write failed.");
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        ReleaseConversationHold("request_write_failed");
        return;
    }

    g_state.awaitingReply = true;
    g_state.awaitingVoiceReply = false;
    RememberNpcTarget(g_state.pendingNpcKey, g_state.pendingNpcName, g_state.pendingSpeaker);

    std::ostringstream diag;
    diag << "request=1\n";
    diag << "npc_key=" << g_state.pendingNpcKey << "\n";
    diag << "npc_name=" << EscapeForDiag(g_state.pendingNpcName) << "\n";
    diag << "player_text=" << EscapeForDiag(submitted.value()) << "\n";
    diag << "location_major=" << EscapeForDiag(location.major) << "\n";
    diag << "location_minor=" << EscapeForDiag(location.minor) << "\n";
    diag << "location_cell=" << EscapeForDiag(location.cell) << "\n";
    diag << "location_worldspace=" << EscapeForDiag(location.worldspace) << "\n";
    diag << "location_region=" << EscapeForDiag(location.region) << "\n";
    diag << "request_metadata=" << EscapeForDiag(requestMetadata) << "\n";
    WriteDiagnostics(diag.str());

}

void CloseVoiceCaptureHandle()
{
    auto& capture = g_state.voiceCapture;
    if (capture.waveIn)
    {
        waveInStop(capture.waveIn);
        waveInReset(capture.waveIn);
        for (auto& buffer : capture.buffers)
        {
            if (buffer.header.dwFlags & WHDR_PREPARED)
            {
                waveInUnprepareHeader(capture.waveIn, &buffer.header, sizeof(WAVEHDR));
            }
        }
        waveInClose(capture.waveIn);
        capture.waveIn = nullptr;
    }
    capture.buffers.clear();
}

bool StartVoiceCaptureWithResolvedTarget(const ResolvedNpcTarget& target)
{
    if (!target.ref || target.npcKey.empty() || target.npcName.empty())
    {
        return false;
    }

    ClearIdleOutboxArtifacts("voice_capture_idle_stale_response");
    if (HasQueuedOrPlayingReply())
    {
        InterruptBridgeReplyAndPlayback("voice_capture_interrupt");
    }

    AbortVoiceCapture("start_new_voice_capture", false);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kVoiceCaptureChannels;
    format.nSamplesPerSec = kVoiceCaptureSampleRate;
    format.wBitsPerSample = kVoiceCaptureBitsPerSample;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEIN waveIn = nullptr;
    const MMRESULT openResult = waveInOpen(&waveIn, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR || !waveIn)
    {
        ShowHudMessage("Microphone unavailable.");
        LogLine("Failed to open microphone input: %u.", static_cast<unsigned>(openResult));
        return false;
    }

    auto& capture = g_state.voiceCapture;
    capture.active = false;
    capture.transcribing = false;
    capture.adminMode = false;
    capture.npcKey = target.npcKey;
    capture.npcName = target.npcName;
    capture.speaker = CaptureSpeakerSnapshot(target.ref);
    capture.waveIn = waveIn;
    capture.startedTick = GetTickCount();
    capture.subtitleRefreshTick = 0;
    capture.capturedPcm.clear();
    capture.buffers.clear();

    const DWORD bytesPerBuffer = (kVoiceCaptureSampleRate * (kVoiceCaptureBitsPerSample / 8) * kVoiceCaptureChannels * kVoiceCaptureBufferMs) / 1000;
    bool bufferFailure = false;
    capture.buffers.resize(kVoiceCaptureBufferCount);
    for (size_t i = 0; i < kVoiceCaptureBufferCount; ++i)
    {
        VoiceCaptureBuffer& buffer = capture.buffers[i];
        buffer.storage.resize(bytesPerBuffer);
        buffer.header = {};
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.storage.data());
        buffer.header.dwBufferLength = bytesPerBuffer;
        const MMRESULT prepareResult = waveInPrepareHeader(capture.waveIn, &buffer.header, sizeof(WAVEHDR));
        const MMRESULT addResult = prepareResult == MMSYSERR_NOERROR
            ? waveInAddBuffer(capture.waveIn, &buffer.header, sizeof(WAVEHDR))
            : prepareResult;
        if (prepareResult != MMSYSERR_NOERROR || addResult != MMSYSERR_NOERROR)
        {
            LogLine("Failed to prepare microphone buffer %u: prepare=%u add=%u.", static_cast<unsigned>(i), static_cast<unsigned>(prepareResult), static_cast<unsigned>(addResult));
            bufferFailure = true;
            break;
        }
    }

    if (bufferFailure || waveInStart(capture.waveIn) != MMSYSERR_NOERROR)
    {
        CloseVoiceCaptureHandle();
        capture.npcKey.clear();
        capture.npcName.clear();
        capture.speaker = {};
        ShowHudMessage("Failed to start microphone capture.");
        return false;
    }

    capture.active = true;
    RememberNpcTarget(target.npcKey, target.npcName, capture.speaker);
    EngageConversationHold(target.npcKey, target.npcName, capture.speaker);
    capture.subtitleRefreshTick = 0;
    ShowHudMessage("Listening...");
    return true;
}

bool StartAmbientVoiceCapture()
{
    ClearIdleOutboxArtifacts("voice_capture_idle_stale_response");
    if (HasQueuedOrPlayingReply())
    {
        InterruptBridgeReplyAndPlayback("voice_capture_interrupt");
    }

    AbortVoiceCapture("start_new_voice_capture", false);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kVoiceCaptureChannels;
    format.nSamplesPerSec = kVoiceCaptureSampleRate;
    format.wBitsPerSample = kVoiceCaptureBitsPerSample;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEIN waveIn = nullptr;
    const MMRESULT openResult = waveInOpen(&waveIn, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR || !waveIn)
    {
        ShowHudMessage("Microphone unavailable.");
        LogLine("Failed to open microphone input: %u.", static_cast<unsigned>(openResult));
        return false;
    }

    auto& capture = g_state.voiceCapture;
    capture.active = false;
    capture.transcribing = false;
    capture.adminMode = false;
    capture.npcKey.clear();
    capture.npcName.clear();
    capture.speaker = {};
    capture.waveIn = waveIn;
    capture.startedTick = GetTickCount();
    capture.subtitleRefreshTick = 0;
    capture.capturedPcm.clear();
    capture.buffers.clear();

    const DWORD bytesPerBuffer = (kVoiceCaptureSampleRate * (kVoiceCaptureBitsPerSample / 8) * kVoiceCaptureChannels * kVoiceCaptureBufferMs) / 1000;
    bool bufferFailure = false;
    capture.buffers.resize(kVoiceCaptureBufferCount);
    for (size_t i = 0; i < kVoiceCaptureBufferCount; ++i)
    {
        VoiceCaptureBuffer& buffer = capture.buffers[i];
        buffer.storage.resize(bytesPerBuffer);
        buffer.header = {};
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.storage.data());
        buffer.header.dwBufferLength = bytesPerBuffer;
        const MMRESULT prepareResult = waveInPrepareHeader(capture.waveIn, &buffer.header, sizeof(WAVEHDR));
        const MMRESULT addResult = prepareResult == MMSYSERR_NOERROR
            ? waveInAddBuffer(capture.waveIn, &buffer.header, sizeof(WAVEHDR))
            : prepareResult;
        if (prepareResult != MMSYSERR_NOERROR || addResult != MMSYSERR_NOERROR)
        {
            LogLine("Failed to prepare microphone buffer %u: prepare=%u add=%u.", static_cast<unsigned>(i), static_cast<unsigned>(prepareResult), static_cast<unsigned>(addResult));
            bufferFailure = true;
            break;
        }
    }

    if (bufferFailure || waveInStart(capture.waveIn) != MMSYSERR_NOERROR)
    {
        CloseVoiceCaptureHandle();
        capture.npcKey.clear();
        capture.npcName.clear();
        capture.speaker = {};
        ShowHudMessage("Failed to start microphone capture.");
        return false;
    }

    capture.active = true;
    capture.subtitleRefreshTick = 0;
    ShowHudMessage("Listening...");
    return true;
}

bool StartAdminVoiceCapture()
{
    ClearIdleOutboxArtifacts("admin_voice_capture_idle_stale_response");
    if (HasQueuedOrPlayingReply())
    {
        InterruptBridgeReplyAndPlayback("admin_voice_capture_interrupt");
    }

    AbortVoiceCapture("start_new_admin_voice_capture", false);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kVoiceCaptureChannels;
    format.nSamplesPerSec = kVoiceCaptureSampleRate;
    format.wBitsPerSample = kVoiceCaptureBitsPerSample;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEIN waveIn = nullptr;
    const MMRESULT openResult = waveInOpen(&waveIn, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR || !waveIn)
    {
        ShowHudMessage("Microphone unavailable.");
        LogLine("Failed to open microphone input for Todd voice: %u.", static_cast<unsigned>(openResult));
        return false;
    }

    auto& capture = g_state.voiceCapture;
    capture.active = false;
    capture.transcribing = false;
    capture.adminMode = true;
    capture.npcKey = kAdminNpcKey;
    capture.npcName = kAdminNpcName;
    capture.speaker = {};
    capture.waveIn = waveIn;
    capture.startedTick = GetTickCount();
    capture.subtitleRefreshTick = 0;
    capture.capturedPcm.clear();
    capture.buffers.clear();

    const DWORD bytesPerBuffer = (kVoiceCaptureSampleRate * (kVoiceCaptureBitsPerSample / 8) * kVoiceCaptureChannels * kVoiceCaptureBufferMs) / 1000;
    bool bufferFailure = false;
    capture.buffers.resize(kVoiceCaptureBufferCount);
    for (size_t i = 0; i < kVoiceCaptureBufferCount; ++i)
    {
        VoiceCaptureBuffer& buffer = capture.buffers[i];
        buffer.storage.resize(bytesPerBuffer);
        buffer.header = {};
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.storage.data());
        buffer.header.dwBufferLength = bytesPerBuffer;
        const MMRESULT prepareResult = waveInPrepareHeader(capture.waveIn, &buffer.header, sizeof(WAVEHDR));
        const MMRESULT addResult = prepareResult == MMSYSERR_NOERROR
            ? waveInAddBuffer(capture.waveIn, &buffer.header, sizeof(WAVEHDR))
            : prepareResult;
        if (prepareResult != MMSYSERR_NOERROR || addResult != MMSYSERR_NOERROR)
        {
            LogLine("Failed to prepare Todd microphone buffer %u: prepare=%u add=%u.", static_cast<unsigned>(i), static_cast<unsigned>(prepareResult), static_cast<unsigned>(addResult));
            bufferFailure = true;
            break;
        }
    }

    if (bufferFailure || waveInStart(capture.waveIn) != MMSYSERR_NOERROR)
    {
        CloseVoiceCaptureHandle();
        capture.adminMode = false;
        capture.npcKey.clear();
        capture.npcName.clear();
        capture.speaker = {};
        ShowHudMessage("Failed to start Todd voice capture.");
        return false;
    }

    capture.active = true;
    capture.subtitleRefreshTick = 0;
    ShowHudMessage("Listening to Todd...");
    return true;
}

void PollVoiceCaptureBuffers()
{
    auto& capture = g_state.voiceCapture;
    if (!capture.active || !capture.waveIn)
    {
        return;
    }

    for (auto& buffer : capture.buffers)
    {
        if ((buffer.header.dwFlags & WHDR_DONE) == 0)
        {
            continue;
        }

        if (buffer.header.dwBytesRecorded > 0)
        {
            const BYTE* begin = reinterpret_cast<const BYTE*>(buffer.header.lpData);
            capture.capturedPcm.insert(capture.capturedPcm.end(), begin, begin + buffer.header.dwBytesRecorded);
        }

        buffer.header.dwBytesRecorded = 0;
        buffer.header.dwFlags &= ~WHDR_DONE;
        if (capture.active && capture.waveIn)
        {
            const MMRESULT addResult = waveInAddBuffer(capture.waveIn, &buffer.header, sizeof(WAVEHDR));
            if (addResult != MMSYSERR_NOERROR)
            {
                LogLine("Failed to recycle microphone buffer: %u.", static_cast<unsigned>(addResult));
                AbortVoiceCapture("voice_capture_buffer_recycle_failed");
                return;
            }
        }
    }
}

void AbortVoiceCapture(const char* reason, bool releaseHold)
{
    auto& capture = g_state.voiceCapture;
    if (!capture.active && !capture.waveIn)
    {
        capture.transcribing = false;
        capture.subtitleRefreshTick = 0;
        capture.adminMode = false;
        capture.npcKey.clear();
        capture.npcName.clear();
        capture.speaker = {};
        capture.capturedPcm.clear();
        return;
    }

    capture.active = false;
    capture.transcribing = false;
    capture.subtitleRefreshTick = 0;
    capture.adminMode = false;
    CloseVoiceCaptureHandle();
    capture.capturedPcm.clear();
    capture.npcKey.clear();
    capture.npcName.clear();
    capture.speaker = {};
    if (releaseHold)
    {
        ReleaseConversationHold(reason ? reason : "voice_capture_cancelled");
    }
    ClearDialogSubtitle();
}

void FinishVoiceCaptureAndSubmit()
{
    auto& capture = g_state.voiceCapture;
    if (!capture.waveIn)
    {
        AbortVoiceCapture("voice_capture_finish_without_device");
        return;
    }

    capture.active = false;
    capture.transcribing = true;
    capture.subtitleRefreshTick = 0;
    ShowHudMessage("Transcribing...");

    waveInStop(capture.waveIn);
    PollVoiceCaptureBuffers();
    waveInReset(capture.waveIn);
    for (auto& buffer : capture.buffers)
    {
        if (buffer.header.dwBytesRecorded > 0)
        {
            const BYTE* begin = reinterpret_cast<const BYTE*>(buffer.header.lpData);
            capture.capturedPcm.insert(capture.capturedPcm.end(), begin, begin + buffer.header.dwBytesRecorded);
            buffer.header.dwBytesRecorded = 0;
        }
    }
    CloseVoiceCaptureHandle();

    const DWORD blockAlign = (kVoiceCaptureBitsPerSample / 8) * kVoiceCaptureChannels;
    const DWORD audioMs = blockAlign
        ? static_cast<DWORD>((static_cast<unsigned long long>(capture.capturedPcm.size()) * 1000ull) / (static_cast<unsigned long long>(kVoiceCaptureSampleRate) * blockAlign))
        : 0;

    if (capture.capturedPcm.empty() || audioMs < kVoiceCaptureMinimumMs)
    {
        ShowHudMessage("Didn't catch that.");
        AbortVoiceCapture("voice_capture_too_short");
        return;
    }

    const std::vector<BYTE> wavBytes = BuildWaveBytesFromPcm(capture.capturedPcm);
    const LocationSnapshot location = CapturePlayerLocation();
    const bool adminMode = capture.adminMode;
    g_state.pendingNpcKey = capture.npcKey;
    g_state.pendingNpcName = capture.npcName;
    g_state.pendingSpeaker = capture.speaker;

    if (!WriteVoiceRequest(capture.npcKey, capture.npcName, capture.speaker, wavBytes, location, adminMode))
    {
        ShowHudMessage("Voice request write failed.");
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        AbortVoiceCapture("voice_request_write_failed");
        return;
    }

    g_state.awaitingReply = true;
    g_state.awaitingVoiceReply = true;
    RememberNpcTarget(capture.npcKey, capture.npcName, capture.speaker);

    std::ostringstream diag;
    diag << "voice_request=1\n";
    diag << "voice_target=" << (adminMode ? "admin_todd" : "live_chat") << "\n";
    diag << "npc_key=" << capture.npcKey << "\n";
    diag << "npc_name=" << EscapeForDiag(capture.npcName) << "\n";
    diag << "audio_ms=" << audioMs << "\n";
    diag << "audio_bytes=" << wavBytes.size() << "\n";
    diag << "location_major=" << EscapeForDiag(location.major) << "\n";
    diag << "location_minor=" << EscapeForDiag(location.minor) << "\n";
    WriteDiagnostics(diag.str());

    capture.transcribing = false;
    capture.subtitleRefreshTick = 0;
    capture.adminMode = false;
    capture.capturedPcm.clear();
    capture.npcKey.clear();
    capture.npcName.clear();
    capture.speaker = {};
}

void StartChatWithResolvedTarget(const ResolvedNpcTarget& target)
{
    if (!target.ref || target.npcKey.empty() || target.npcName.empty())
    {
        return;
    }

    g_state.awaitingInput = true;
    g_state.inputMenuSeenVisible = false;
    g_state.inputStartedTick = GetTickCount();
    g_state.staleTextInputCloseRetryTick = 0;
    ResetTextInputKeyWatcher();
    g_state.pendingNpcKey = target.npcKey;
    g_state.pendingNpcName = target.npcName;
    g_state.pendingSpeaker = CaptureSpeakerSnapshot(target.ref);
    RememberNpcTarget(target.npcKey, target.npcName, g_state.pendingSpeaker);

    if (!OpenInGameTextInput("Speak to " + target.npcName))
    {
        g_state.awaitingInput = false;
        g_state.inputStartedTick = 0;
        ClearTextInputKeyWatcher();
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        ShowHudMessage("Failed to open in-game chat.");
        return;
    }

    EngageConversationHold(target.npcKey, target.npcName, g_state.pendingSpeaker);

    std::ostringstream diag;
    diag << "input_open=1\n";
    diag << "npc_key=" << target.npcKey << "\n";
    diag << "npc_name=" << EscapeForDiag(target.npcName) << "\n";
    diag << "distance_m=" << (std::sqrt(target.distanceSquared) / kGameUnitsPerMeter) << "\n";
    WriteDiagnostics(diag.str());
}

void StartAmbientChatInput()
{
    g_state.awaitingInput = true;
    g_state.inputMenuSeenVisible = false;
    g_state.inputStartedTick = GetTickCount();
    g_state.staleTextInputCloseRetryTick = 0;
    ResetTextInputKeyWatcher();
    g_state.pendingNpcKey.clear();
    g_state.pendingNpcName.clear();
    g_state.pendingSpeaker = {};

    if (!OpenInGameTextInput("Speak"))
    {
        g_state.awaitingInput = false;
        g_state.inputStartedTick = 0;
        ClearTextInputKeyWatcher();
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        ShowHudMessage("Failed to open in-game chat.");
        return;
    }

    WriteDiagnostics("input_open=1\nchat_target=ambient_group\n");
}

void StartAdminChatInput()
{
    g_state.awaitingInput = true;
    g_state.inputMenuSeenVisible = false;
    g_state.inputStartedTick = GetTickCount();
    g_state.staleTextInputCloseRetryTick = 0;
    ResetTextInputKeyWatcher();
    g_state.pendingNpcKey = kAdminNpcKey;
    g_state.pendingNpcName = kAdminNpcName;
    g_state.pendingSpeaker = {};

    if (!OpenInGameTextInput(kAdminNpcName))
    {
        g_state.awaitingInput = false;
        g_state.inputStartedTick = 0;
        ClearTextInputKeyWatcher();
        g_state.pendingNpcKey.clear();
        g_state.pendingNpcName.clear();
        g_state.pendingSpeaker = {};
        ShowHudMessage("Failed to open Todd chat.");
        return;
    }

    std::ostringstream diag;
    diag << "input_open=1\n";
    diag << "chat_target=admin_todd\n";
    diag << "npc_key=" << kAdminNpcKey << "\n";
    diag << "npc_name=" << kAdminNpcName << "\n";
    WriteDiagnostics(diag.str());
}

void UpdateVoiceCaptureHotkey()
{
    auto& capture = g_state.voiceCapture;
    if (!GameWindowHasFocus())
    {
        if (capture.active)
        {
            AbortVoiceCapture("voice_capture_focus_lost");
        }
        PrimeHotkeyEdgeStateFromKeyboard();
        return;
    }

    if (g_state.ignoreHotkeysUntilTick && GetTickCount() < g_state.ignoreHotkeysUntilTick)
    {
        PrimeHotkeyEdgeStateFromKeyboard();
        return;
    }
    g_state.ignoreHotkeysUntilTick = 0;

    const bool keyDown = (GetAsyncKeyState(g_hotkeys.voiceVk) & 0x8000) != 0;
    const bool adminKeyDown = (GetAsyncKeyState(g_hotkeys.adminVoiceVk) & 0x8000) != 0;
    const bool pressedNow = keyDown && !capture.keyDownLastFrame;
    const bool adminPressedNow = adminKeyDown && !capture.adminKeyDownLastFrame;
    const bool releasedNow = !keyDown && capture.keyDownLastFrame;
    const bool adminReleasedNow = !adminKeyDown && capture.adminKeyDownLastFrame;
    capture.keyDownLastFrame = keyDown;
    capture.adminKeyDownLastFrame = adminKeyDown;

    if (capture.active)
    {
        const bool submitNow = capture.adminMode ? adminReleasedNow : releasedNow;
        if (submitNow)
        {
            FinishVoiceCaptureAndSubmit();
        }
        return;
    }

    if (!pressedNow && !adminPressedNow)
    {
        return;
    }

    if (g_state.saveStateSyncPending)
    {
        if (!g_state.saveStateSyncHudMessageTick || (GetTickCount() - g_state.saveStateSyncHudMessageTick) >= kSaveStateSyncHudCooldownMs)
        {
            ShowHudMessage("Bridge syncing save state. Wait a moment.");
            g_state.saveStateSyncHudMessageTick = GetTickCount();
        }
        return;
    }

    RecoverStaleReplyState();

    if (g_state.awaitingInput || capture.transcribing)
    {
        return;
    }

    if (IsTextInputMenuActive())
    {
        return;
    }

    if (adminPressedNow)
    {
        StartAdminVoiceCapture();
        return;
    }

    PlayerCharacter* player = GetPlayer();
    const auto target = FindFocusedMappedNpcForChat(player);
    if (target.has_value())
    {
        StartVoiceCaptureWithResolvedTarget(*target);
        return;
    }

    if (FindNearbyMappedNpcsForGroupChat(player, kGroupChatNearbyRadiusMeters).empty())
    {
        ShowHudMessage("No mapped NPC within 10 meters.");
        return;
    }

    StartAmbientVoiceCapture();
}

// =====================================================================================
// Player persona capture (frozen contract: docs/persona.md).
//
// SAVE-DRIVEN, UNCONDITIONAL: EVERY save captures — manual, quicksave, and
// autosave alike, with no debounce and no idle gating. The NVSE SaveGame
// message (the same detection point the save-sync machinery uses) classifies the
// save by file name — quicksave*.fos -> "quicksave", autosave*.fos -> "autosave",
// anything else -> "save" — and marks a capture pending; the main-loop driver
// takes it on the next frame (saves in a burst coalesce while an upload is in
// flight — one upload runs end-to-end at a time, latest stats win). Saves always
// regenerate server-side, even with identical data.
//
// DATA-ONLY: the capture is a pure game-data snapshot — stats (the same display
// strings the gamestate macros send) plus the player's appearance records (sex,
// race, hair style/color/length, eye color, facial hair head parts, worn
// apparel). Nothing is rendered, screenshotted, or encoded; the old offscreen
// portrait renderer is fully retired (it depended on the engine's lazily built
// 3rd-person head and produced garbage faces in first-person sessions). A
// detached background thread POSTs the JSON to chasm's /api/game/v1/persona
// with bounded timeouts (fire-and-forget; a dead backend costs one log line).
// =====================================================================================

struct PersonaCaptureRuntime
{
    bool capturePending = false;     // a save fired; capture on the next frame
    std::string pendingTrigger;      // "save" | "quicksave" | "autosave"
};

PersonaCaptureRuntime g_persona;
// One upload in flight at a time, end to end (the detached sender thread only
// touches this atomic and its own moved-in data — never game state).
std::atomic<bool> g_personaSendInFlight{ false };

// Classifies a save path for the persona trigger vocabulary (docs/persona.md):
// quicksave*.fos -> "quicksave", autosave*.fos -> "autosave", else "save".
std::string ClassifyPersonaSaveTrigger(const std::string& savePath)
{
    std::string stem;
    try
    {
        stem = ToLowerAscii(fs::path(savePath).filename().string());
    }
    catch (...)
    {
        stem.clear();
    }
    if (stem.rfind("quicksave", 0) == 0)
    {
        return "quicksave";
    }
    if (stem.rfind("autosave", 0) == 0)
    {
        return "autosave";
    }
    return "save";
}

// NVSE SaveGame hook (the same detection point the save-sync machinery uses):
// classify the save and mark a capture pending — EVERY save, no debounce, no
// autosave knob. The main-loop driver takes it on the next frame (a burst of
// saves while an upload is in flight coalesces; latest trigger wins).
void RequestPersonaCaptureForSave(const std::string& savePath)
{
    if (!g_debugConfig.personaEnabled)
    {
        return;
    }
    const std::string trigger = ClassifyPersonaSaveTrigger(savePath);
    g_persona.capturePending = true;
    g_persona.pendingTrigger = trigger;
    LogLine("Persona: capture pending (trigger=%s).", trigger.c_str());
}

// Reads `count` FaceGen coefficients into `out`. Layout verified against the
// live process (2026-07-03): TESNPC::FaceGenData::values IS the float array
// (begin pointer, despite the SDK's float** declaration), and useOffset /
// maxOffset are the matching end/capacity pointers (begin + count*4) — a
// begin/end/capacity triple. SEH-guarded and plausibility-checked (FaceGen
// coefficients are small); returns false on fault or implausible data — the
// caller then omits the age.
bool ReadFaceGenCoeffsGuarded(const TESNPC::FaceGenData& fg, UInt32 count, float* out)
{
    __try
    {
        if (fg.count != count || fg.size != 1 || !fg.values)
        {
            return false;
        }
        if (fg.useOffset != reinterpret_cast<UInt32>(fg.values) + count * sizeof(float))
        {
            return false; // end pointer disagrees with the begin pointer
        }
        const float* src = reinterpret_cast<const float*>(fg.values);
        for (UInt32 i = 0; i < count; ++i)
        {
            const float value = src[i];
            if (!(value > -32.0f && value < 32.0f)) // NaN fails too
            {
                return false;
            }
        }
        for (UInt32 i = 0; i < count; ++i)
        {
            out[i] = src[i];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Estimates the player's age in years from the FaceGen coefficients — the
// chargen Age slider bakes into these; the linear age/gender controls come
// from the game's own facegen\si.ctl (see facegen_age.h). Age and gender are
// each computed in the symmetric-shape and symmetric-texture subspaces and
// averaged. Returns a negative value when the coefficients cannot be read or
// fail the GENDER SELF-CHECK: the same coefficients must predict the
// character's actual sex — a wrong prediction means the read was garbage, so
// no age is reported rather than a wrong one.
float EstimateFaceGenAgeYears(TESNPC* npc)
{
    float gs[facegen_age::kCoefficientCount];
    float ts[facegen_age::kCoefficientCount];
    // faceGenData[0] = geometry symmetric (50), [1] = geometry asymmetric
    // (30), [2] = texture symmetric (50) — counts per the xNVSE SDK notes.
    if (!ReadFaceGenCoeffsGuarded(npc->faceGenData[0], facegen_age::kCoefficientCount, gs) ||
        !ReadFaceGenCoeffsGuarded(npc->faceGenData[2], facegen_age::kCoefficientCount, ts))
    {
        LogLine("Persona: FaceGen coefficients unavailable; age omitted.");
        return -1.0f;
    }
    float ageShape = facegen_age::kAgeGsOffset;
    float ageTexture = facegen_age::kAgeTsOffset;
    float genderShape = facegen_age::kGenderGsOffset;
    float genderTexture = facegen_age::kGenderTsOffset;
    for (int i = 0; i < facegen_age::kCoefficientCount; ++i)
    {
        ageShape += gs[i] * facegen_age::kAgeGs[i];
        ageTexture += ts[i] * facegen_age::kAgeTs[i];
        genderShape += gs[i] * facegen_age::kGenderGs[i];
        genderTexture += ts[i] * facegen_age::kGenderTs[i];
    }
    const float age = (ageShape + ageTexture) * 0.5f;
    const float gender = (genderShape + genderTexture) * 0.5f; // -1 male .. +1 female
    const bool female = npc->baseData.IsFemale();
    if (gender < -4.0f || gender > 4.0f ||
        (female && gender < -0.5f) || (!female && gender > 0.5f))
    {
        LogLine("Persona: FaceGen gender self-check failed (%.2f for %s); age omitted.",
            gender, female ? "female" : "male");
        return -1.0f;
    }
    LogLine("Persona: FaceGen age estimate %.1f years (shape %.1f, texture %.1f; gender %.2f).",
        age, ageShape, ageTexture, gender);
    return age;
}

// The capture's stats snapshot as JSON object FIELDS (no braces) so the sender
// thread can splice extra fields in. Keys are the frozen vocabulary of
// docs/persona.md; every value is the same display string the gamestate macros
// send, and any unreadable field is omitted (the backend tolerates absence).
std::string BuildPersonaStatsJsonFields(PlayerCharacter* player, const std::string& trigger)
{
    std::ostringstream out;
    bool first = true;
    AppendJsonMacro(out, first, "captured_at", NowIsoUtc());
    AppendJsonMacro(out, first, "trigger", trigger);
    AppendJsonMacro(out, first, "player_name", GetDisplayNameSafe(player));
    const int level = static_cast<int>(player->avOwner.Fn_0A());
    if (level > 0)
    {
        AppendJsonMacro(out, first, "level", std::to_string(level));
    }
    AppendJsonMacro(out, first, "special", BuildSpecialMacro(player));
    AppendJsonMacro(out, first, "skills", BuildSkillsMacro(player));
    AppendJsonMacro(out, first, "perks", BuildPerksMacro(player));
    AppendJsonMacro(out, first, "equipped_weapon", GetDisplayNameSafe(player->GetEquippedWeapon()));
    AppendJsonMacro(out, first, "equipped_apparel", BuildEquippedApparelMacro(player));
    if (player->baseForm && player->baseForm->typeID == kFormType_TESNPC)
    {
        auto* npc = static_cast<TESNPC*>(player->baseForm);
        AppendJsonMacro(out, first, "sex", npc->baseData.IsFemale() ? "female" : "male");
        if (npc->race.race)
        {
            AppendJsonMacro(out, first, "race", GetDisplayNameSafe(npc->race.race));
        }
        // The chargen Age slider, recovered from the FaceGen coefficients
        // (clamped to a plausible adult range; omitted when unreadable).
        const float ageYears = EstimateFaceGenAgeYears(npc);
        if (ageYears > 0.0f)
        {
            const int clamped = (std::clamp)(static_cast<int>(ageYears + 0.5f), 18, 70);
            AppendJsonMacro(out, first, "age_years", std::to_string(clamped));
        }
        if (npc->hair)
        {
            AppendJsonMacro(out, first, "hair_style", GetDisplayNameSafe(npc->hair));
            // GECK hair-length slider, 0..1 (scales the chosen style's mesh).
            if (npc->hairLength >= 0.0f && npc->hairLength <= 1.0f)
            {
                char length[16]{};
                std::snprintf(length, sizeof(length), "%.2f", npc->hairLength);
                AppendJsonMacro(out, first, "hair_length", length);
            }
        }
        if (npc->eyes)
        {
            AppendJsonMacro(out, first, "eye_color", GetDisplayNameSafe(npc->eyes));
        }
        // Facial hair: beards/mustaches are BGSHeadPart forms on the NPC (the
        // list the barber menu edits). Join the named ones; empty when none
        // (AppendJsonMacro drops empty values, so the key is simply absent).
        std::string facialHair;
        for (auto iter = npc->headPart.Begin(); !iter.End(); ++iter)
        {
            BGSHeadPart* part = iter.Get();
            if (!part)
            {
                continue;
            }
            const std::string partName = GetDisplayNameSafe(part);
            if (partName.empty())
            {
                continue;
            }
            if (!facialHair.empty())
            {
                facialHair += ", ";
            }
            facialHair += partName;
        }
        AppendJsonMacro(out, first, "facial_hair", facialHair);
        char hairHex[8]{};
        std::snprintf(hairHex, sizeof(hairHex), "#%02X%02X%02X",
            static_cast<unsigned>(npc->hairColor & 0xFF),
            static_cast<unsigned>((npc->hairColor >> 8) & 0xFF),
            static_cast<unsigned>((npc->hairColor >> 16) & 0xFF));
        AppendJsonMacro(out, first, "hair_color", hairHex);
    }
    const LocationSnapshot location = CapturePlayerLocation();
    std::string locationLabel = location.minor;
    if (!location.major.empty())
    {
        if (!locationLabel.empty())
        {
            locationLabel += ", ";
        }
        locationLabel += location.major;
    }
    AppendJsonMacro(out, first, "location", locationLabel);
    return out.str();
}

// --- HTTP upload (sender thread only) -------------------------------------------------

// One bounded POST; success = HTTP 2xx. Mirrors RunHttpTurn's WinHTTP usage minus
// the streaming (the persona response is one small JSON object).
bool PostPersonaCapture(const std::string& url, const std::string& body, std::string& outSummary)
{
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[2048] = { 0 };
    components.lpszHostName = hostName;
    components.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
    components.lpszUrlPath = urlPath;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));

    const std::wstring wideUrl = Utf8ToWide(url);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components))
    {
        outSummary = "URL parse failed";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"FNVBridgeNative/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        outSummary = "session open failed";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 20000);

    bool ok = false;
    std::ostringstream summary;
    HINTERNET connection = WinHttpConnect(session, components.lpszHostName, components.nPort, 0);
    HINTERNET request = nullptr;
    if (!connection)
    {
        summary << "connect failed (is chasm running?)";
    }
    else
    {
        const DWORD secureFlag = (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection, L"POST", components.lpszUrlPath,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secureFlag);
        if (!request)
        {
            summary << "request open failed";
        }
        else if (!WinHttpSendRequest(request, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
                     const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                     static_cast<DWORD>(body.size()), 0))
        {
            summary << "send failed (is chasm running?)";
        }
        else if (!WinHttpReceiveResponse(request, nullptr))
        {
            summary << "response receive failed";
        }
        else
        {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

            std::string responseBody;
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0 && responseBody.size() < 2048)
            {
                std::string buffer(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0)
                {
                    break;
                }
                responseBody.append(buffer.data(), read);
            }

            ok = statusCode >= 200 && statusCode < 300;
            summary << "HTTP " << statusCode;
            if (!responseBody.empty())
            {
                summary << " " << Trim(responseBody).substr(0, 200);
            }
        }
    }

    if (request)
    {
        WinHttpCloseHandle(request);
    }
    if (connection)
    {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    outSummary = summary.str();
    return ok;
}

// Fire-and-forget upload on a detached thread. The thread only touches its own
// moved-in data, LogLine (already called from the HTTP turn worker thread
// today), and the persona atomic — never game state. Detached on purpose: a
// hung POST can never wedge game exit; process teardown reaps it.
void StartPersonaUpload(std::string statsJsonFields)
{
    if (g_personaSendInFlight.exchange(true))
    {
        return; // one upload at a time; the driver waits on this before a new sequence
    }

    std::ostringstream url;
    url << "http://" << g_debugConfig.httpHost << ":" << g_debugConfig.httpPort
        << g_debugConfig.personaHttpPath;

    std::thread([statsJsonFields = std::move(statsJsonFields), url = url.str()]() mutable {
        std::string summary;
        const bool ok = PostPersonaCapture(url, "{" + statsJsonFields + "}", summary);
        LogLine("Persona upload %s (%s).", ok ? "succeeded" : "failed", summary.c_str());
        g_personaSendInFlight.store(false);
    }).detach();
}

// --- The capture (game thread) ---------------------------------------------------------

// Takes the pending save-triggered capture in ONE frame: snapshot the stats +
// appearance data and hand off the upload. Pure data — nothing is rendered.
void TakePersonaCapture(PlayerCharacter* player)
{
    LogLine("Persona: captured data snapshot (trigger=%s).", g_persona.pendingTrigger.c_str());
    StartPersonaUpload(BuildPersonaStatsJsonFields(player, g_persona.pendingTrigger));
}

// Session reset (load / new game / exit to menu): drop all transient state.
// A pending capture is dropped too — the freshly loaded state supersedes
// the save that queued it.
void ResetPersonaStateForSession()
{
    g_persona.capturePending = false;
    g_persona.pendingTrigger.clear();
}

// Per-frame driver, called from OnMainGameLoop once loadedIntoGame + player are
// established. A pending save-capture fires on the very next frame — no gating,
// no debounce; the only wait is for an already-in-flight upload to finish (one
// capture runs end-to-end at a time; a save burst coalesces into the pending
// one). Never blocks: the POST lives on the sender thread.
void UpdatePersonaCapture()
{
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }

    if (!g_debugConfig.personaEnabled)
    {
        g_persona.capturePending = false;
        return;
    }

    // Nothing to do until a save marks a capture pending.
    if (!g_persona.capturePending)
    {
        return;
    }

    if (g_personaSendInFlight.load())
    {
        return; // one capture end-to-end at a time
    }

    g_persona.capturePending = false;
    TakePersonaCapture(player);
}

// ===========================================================================
// Music: play-a-song (guitar) delivery + performance  (task/music)
// ===========================================================================
//
// A generated song arrives out-of-band as a line-based file in
// `<bridge>/control/songs/<id>.txt` (written by chasm's music module AFTER the
// turn completes — the normal reply path is turn-scoped and won't play unsolicited
// audio). This queue is polled UNCONDITIONALLY, like the durable action queue.
// Format:
//     NVBRIDGE_SONG_V1
//     <songId>
//     <npcKey>
//     <npcName>
//     <absolute wav path>
//     <durationMs>
//     <title>
// We resolve the NPC, play the WAV positionally from them (reusing PlayVoiceWav —
// one static DirectSound buffer for the whole song), and run the guitar idle for
// the song's duration, stopping it at the end. Clearly separated from the
// turn/speech audio path so it doesn't disturb the in-flight audio work.

fs::path SongDeliveryDir()
{
    return SaveStateControlDir() / "songs";
}

// Lazily compile the guitar-idle helper scripts (mirrors the SetRestrained etc.
// helpers). `PlayIdle SpecialIdleNVGuitar` is the vanilla seated guitar-strum idle
// (the Lonesome Drifter's performance); EvaluatePackage ends it by re-running AI.
void EnsureGuitarIdleScripts()
{
    if (g_guitarIdleScriptsAttempted)
    {
        return;
    }
    g_guitarIdleScriptsAttempted = true;
    if (!g_scriptInterface)
    {
        return;
    }
    constexpr char kPlayGuitarIdleScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.PlayIdle SpecialIdleNVGuitar
    endif
End
)";
    // Rap/vocal performance: Bruce Isaac's standing lounge-singer idle. No
    // instrument baked in, PlayIdle-compatible on any humanoid — reads as an MC.
    constexpr char kPlaySingIdleScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.PlayIdle SpecialIdleSinging
    endif
End
)";
    // Stopping a forced special-idle: PlayIdle/EvaluatePackage alone can't break a
    // looping performance idle. The vanilla game stops exactly these idles by playing
    // a DIFFERENT idle over them (VMSLonesomeDrifterSCRIPT breaks the guitar with
    // `PlayIdle LooseDefensiveIdleA`). So we override with a brief neutral idle, then
    // ResetAI + EvaluatePackage returns the actor to their normal AI standing idle.
    constexpr char kStopGuitarIdleScript[] = R"(
ref rActor

Begin Function { rActor }
    if rActor
        rActor.PlayIdle LooseDefensiveIdleA
        rActor.ResetAI
        rActor.EvaluatePackage
    endif
End
)";
    g_playGuitarIdleScript = g_scriptInterface->CompileScript(kPlayGuitarIdleScript);
    g_playSingIdleScript = g_scriptInterface->CompileScript(kPlaySingIdleScript);
    g_stopGuitarIdleScript = g_scriptInterface->CompileScript(kStopGuitarIdleScript);
    if (!g_playGuitarIdleScript || !g_playSingIdleScript || !g_stopGuitarIdleScript)
    {
        LogLine("Music: failed to compile performance-idle helper script(s); animation will rely on the action-book kick only.");
    }
}

// Runs the play- or stop-performance-idle helper on `actorRef`. On play it picks the
// guitar or the singing (rap) idle from g_state.performIsRap; stop is shared.
// Best-effort.
void RunGuitarIdle(TESObjectREFR* actorRef, bool play)
{
    EnsureGuitarIdleScripts();
    Script* script;
    if (!play)
    {
        script = g_stopGuitarIdleScript;
    }
    else
    {
        script = g_state.performIsRap ? g_playSingIdleScript : g_playGuitarIdleScript;
    }
    if (!script || !actorRef || !g_scriptInterface)
    {
        return;
    }
    NVSEArrayVarInterface::Element result;
    g_scriptInterface->CallFunction(script, actorRef, nullptr, &result, 1, actorRef);
    // Root the performer in place while performing (SetRestrained), like the vanilla
    // Lonesome Drifter, and release them when the performance stops. This is what
    // keeps them from wandering off mid-song (a triggered action drops the normal
    // conversation hold, so the idle alone isn't enough to hold them still).
    SetActorRestrainedState(actorRef, play);
}

// Tears down any active/ pending guitar performance: stops the idle on the
// performer(s) and clears ALL performance state, so a stale `songActive` can never
// keep re-asserting the guitar (the "guitar stays locked" bug). Called on song end,
// on save load / reset (via ResetRuntimeState), and on interrupt. Idempotent.
void StopGuitarPerformance(const char* reason)
{
    const bool wasActive = g_state.songActive || g_state.pendingGuitar;
    if (TESObjectREFR* ref = ResolveSpeakerRef(g_state.songSpeaker))
    {
        RunGuitarIdle(ref, false);
    }
    if (TESObjectREFR* ref = ResolveSpeakerRef(g_state.pendingGuitarSpeaker))
    {
        RunGuitarIdle(ref, false);
    }
    g_state.songActive = false;
    g_state.pendingGuitar = false;
    g_state.songUntilTick = 0;
    g_state.songReissueTick = 0;
    g_state.songSpeaker = {};
    g_state.pendingGuitarSpeaker = {};
    g_state.performIsRap = false;
    if (wasActive)
    {
        LogLine("Music: performance stopped (%s).", reason ? reason : "cleanup");
    }
}

// Whether this turn's spoken reply (TTS) is still being generated or played out.
// The guitar + song wait for this to go false, so the NPC finishes accepting the
// request ("give me a minute to tune up") before the performance begins.
bool IsTurnSpeechActive()
{
    return g_state.awaitingReply
        || g_state.streamActive
        || (g_state.activeSpeechUntilTick && GetTickCount() < g_state.activeSpeechUntilTick);
}

// Plays one delivered song: resolve the NPC, play the WAV positionally, kick the
// guitar idle, and arm the performance timer. Returns true when the delivery was
// consumed (played OR unrecoverable — either way the file is removed by the caller).
bool PlaySongDelivery(const std::vector<std::string>& lines)
{
    if (lines.size() < 6 || Trim(lines[0]) != "NVBRIDGE_SONG_V1")
    {
        return true; // malformed → consume (don't retry a bad file forever)
    }
    const std::string npcKey = Trim(lines[2]);
    const std::string npcName = Trim(lines[3]);
    const std::string wavPath = Trim(lines[4]);
    long durationRaw = lines.size() > 5 ? std::atol(Trim(lines[5]).c_str()) : 0;
    if (durationRaw < 0)
    {
        durationRaw = 0;
    }
    const DWORD durationMs = static_cast<DWORD>(durationRaw);

    if (wavPath.empty())
    {
        LogLine("Music: song delivery has no wav path; dropping.");
        return true;
    }
    const fs::path wav = fs::path(wavPath);
    std::error_code ec;
    if (!fs::exists(wav, ec))
    {
        LogLine("Music: song wav missing on disk (%s); dropping.", wavPath.c_str());
        return true;
    }

    // Hold the song until this turn's spoken reply (TTS) has finished, so the NPC
    // isn't singing over their own acceptance line. Retried on the next scan.
    if (IsTurnSpeechActive())
    {
        return false; // not consumed — wait for speech to end
    }

    // Resolve the singer. If they aren't loaded/nearby, leave the delivery to retry
    // (the player may be mid-walk to the NPC) — the caller ages it out.
    const auto snapshot = ResolveSpeakerSnapshotForNpc(npcKey, npcName);
    if (!snapshot.has_value() || !snapshot->valid)
    {
        return false; // not consumed — retry next scan
    }
    TESObjectREFR* actorRef = ResolveSpeakerRef(*snapshot);
    if (!actorRef)
    {
        return false;
    }

    // Load the WAV once so we can both play it and drive the mouth from its envelope.
    const auto wavData = LoadWavFile(wav);
    if (!wavData.has_value())
    {
        LogLine("Music: could not load song wav (%s); dropping.", wavPath.c_str());
        return true;
    }
    if (!PlayVoiceWav(wav, *snapshot, &*wavData))
    {
        LogLine("Music: PlayVoiceWav failed for %s.", wavPath.c_str());
        return true; // consumed — don't spin on a bad buffer
    }

    const DWORD dur = durationMs > 0 ? durationMs : 60000; // fallback ~60s if unknown

    // Lip-sync: drive the NPC's mouth from the song's amplitude envelope for its
    // duration — the same envelope->viseme machinery TTS uses. Not phoneme-accurate,
    // but the jaw moves with the singing so it reads as a genuine performance.
    QueuedAudioChunk songChunk;
    songChunk.requestId = Trim(lines[1]); // songId
    songChunk.audioFile = wav.filename().string();
    songChunk.speakerKey = npcKey;
    songChunk.speakerName = npcName;
    songChunk.chunkIndex = 0;
    songChunk.nonPositional = false;
    // Exaggerate the mouth movement for singing (2.2x): a sung/mixed track is
    // quieter and flatter than clean speech, so at normal gain the lips barely move.
    StartSpeechAnimation(*wavData, songChunk, *snapshot, dur, 2.2f);

    // Guitar is already out (the deferred idle fired when speech ended). Re-assert it
    // synced to the song and arm the performance timer to hold + stop it at the end.
    RunGuitarIdle(actorRef, true);
    g_state.songActive = true;
    g_state.songSpeaker = *snapshot;
    g_state.songReissueTick = GetTickCount();
    g_state.songUntilTick = GetTickCount() + dur + 500; // small tail
    g_state.pendingGuitar = false;                       // the song owns the performance now
    LogLine("Music: performing song for '%s' (~%u ms) from %s.", npcName.c_str(), dur, wavPath.c_str());
    return true;
}

// Scans the control/songs queue (throttled) and plays any ready deliveries.
void ProcessSongDeliveries()
{
    const DWORD now = GetTickCount();
    if (g_state.songScanTick != 0 && (now - g_state.songScanTick) < 500)
    {
        return; // throttle: song delivery is not latency-critical
    }
    g_state.songScanTick = now;

    const fs::path dir = SongDeliveryDir();
    std::error_code ec;
    if (!fs::exists(dir, ec))
    {
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc))
        {
            continue;
        }
        const fs::path path = entry.path();
        if (path.extension() != ".txt")
        {
            continue; // skip .tmp files still being written
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            continue;
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(line);
        }
        in.close();

        const bool consumed = PlaySongDelivery(lines);
        if (consumed)
        {
            fs::remove(path, fileEc);
        }
        else
        {
            // NPC not resolvable yet: age the delivery out after ~2 minutes so a
            // song for an NPC the player never reaches doesn't linger forever.
            const auto writeTime = fs::last_write_time(path, fileEc);
            if (!fileEc)
            {
                const auto age = decltype(writeTime)::clock::now() - writeTime;
                if (age > std::chrono::minutes(2))
                {
                    LogLine("Music: song delivery %s expired (NPC never resolved); dropping.",
                        path.filename().string().c_str());
                    fs::remove(path, fileEc);
                }
            }
        }
        // Only start one song per scan (avoid stacking overlapping performances).
        if (consumed && g_state.songActive)
        {
            break;
        }
    }
}

// Keeps the guitar idle asserted through the song and stops it at the end. The
// special idle can be broken by AI re-evaluation, so we re-issue it periodically.
void UpdateActiveSong()
{
    const DWORD now = GetTickCount();

    // Deferred guitar: fire the performance idle once this turn's spoken reply (TTS)
    // has finished — the NPC pulls out the guitar AFTER accepting the request, then
    // strums while the song generates. The song (arriving later) reuses this state.
    if (g_state.pendingGuitar && !IsTurnSpeechActive())
    {
        g_state.pendingGuitar = false;
        if (TESObjectREFR* ref = ResolveSpeakerRef(g_state.pendingGuitarSpeaker))
        {
            RunGuitarIdle(ref, true);
            g_state.songActive = true;
            g_state.songSpeaker = g_state.pendingGuitarSpeaker;
            g_state.songReissueTick = now;
            // Safety cap: if the song never arrives (generation failed / player left),
            // stop strumming after 90s instead of locking the idle. PlaySongDelivery
            // replaces this with the real song end when the audio arrives.
            g_state.songUntilTick = now + 90000;
            LogLine("Music: turn speech ended - guitar out, awaiting song.");
        }
    }

    if (!g_state.songActive)
    {
        return;
    }
    TESObjectREFR* actorRef = ResolveSpeakerRef(g_state.songSpeaker);

    // Performer no longer resolvable (unloaded / cell change / gone) — stop cleanly
    // so we never keep re-asserting on a stale reference.
    if (!actorRef)
    {
        StopGuitarPerformance("performer_unresolved");
        return;
    }

    if (now >= g_state.songUntilTick)
    {
        StopGuitarPerformance("song_ended");
        return;
    }

    // Re-assert the guitar idle every ~4s so it survives AI package re-evaluation.
    if ((now - g_state.songReissueTick) >= 4000)
    {
        g_state.songReissueTick = now;
        RunGuitarIdle(actorRef, true);
    }
}

// ===========================================================================
// GAME EVENT LOG — extract notable gameplay events, aggregate away the noise,
// and drop JSONL batches into control/gameevents/ for the bridge to relay to
// chasm's save-aware event store (POST /event-log/events).
//
// Sources:
//   * xNVSE EventManager native handlers (ondeath, onstartcombat, oncombatend,
//     onhit, onadd, onactorequip, onactorunequip, ondrop, onfire) — registered
//     at DeferredInit.
//   * A 1 Hz slow poll in the main loop for everything the engine has no event
//     for: cell/worldspace travel, game-day change, level-ups, karma class,
//     teammate roster, quest objectives, and the vanilla DialogMenu.
//   * WriteRequest marks chasm conversations (one marker per NPC per window).
//
// Noise rules: hits/kills collapse into ONE combat-encounter event; item
// pickups and drops each aggregate into a short window (notables called out,
// junk counted); consumables (food/meds via onactorequip on an AlchemyItem)
// dedup per item; out-of-combat shooting collapses a firing burst into one
// event (and is dropped entirely if a real fight starts); travel only fires on
// NAMED place changes; every poll-derived event needs a primed baseline
// (nothing fires on load); nothing is emitted per frame.
// ===========================================================================

constexpr UInt32 kGameTimeGlobalsAddress = 0x011DE7B8;
constexpr DWORD kEventFlushIntervalMs = 2500;
constexpr size_t kEventFlushBatchSize = 20;
constexpr size_t kEventPendingHardCap = 200;
constexpr DWORD kEventSlowPollMs = 1000;
constexpr DWORD kCombatQuietCloseMs = 4000;
constexpr DWORD kLootWindowQuietMs = 8000;
constexpr DWORD kEquipDedupMs = 10 * 60 * 1000;
constexpr DWORD kConversationMarkerDedupMs = 3 * 60 * 1000;
constexpr DWORD kConsumableDedupMs = 12 * 1000;   // collapse rapid re-use (combat healing)
constexpr DWORD kDropWindowQuietMs = 6000;        // aggregate a dropping spree
constexpr DWORD kShootWindowQuietMs = 3500;       // out-of-combat firing burst
constexpr double kEventNearbyDistance = 70.0 * kGameUnitsPerMeter; // 70 m
constexpr UInt32 kFormFlagQuestItem = 0x400;
constexpr UInt32 kActorValueKarma = 23;
// FNV ALCH (AlchemyItem) ENIT flags in the low byte at +0xBC.
constexpr UInt8 kAlchFlagFood = 0x02;
constexpr UInt8 kAlchFlagMedicine = 0x04;

// JIP-documented calendar globals (year/month/day/hour as TESGlobal floats).
struct GameTimeGlobalsLayout
{
    TESGlobal* year;
    TESGlobal* month;
    TESGlobal* day;
    TESGlobal* hour;
    TESGlobal* daysPassed;
    TESGlobal* timeScale;
};

struct GameEventLogState
{
    std::mutex pendingMutex;             // pending is touched from the HTTP worker too
    std::vector<std::string> pending;    // serialized JSON lines awaiting flush
    UInt32 eventSerial = 0;
    UInt32 batchSerial = 0;
    DWORD lastFlushTick = 0;
    bool handlersRegistered = false;
    std::unordered_set<std::string> loggedHookFirstFire;

    // Slow-poll baselines. Nothing emits until primed (set on the first poll
    // after a load), so loading a save never replays state as fresh events.
    bool primed = false;
    std::string lastCellName;
    std::string lastWorldspaceName;
    bool lastCellWasInterior = false;
    int lastGameDay = -1;
    int lastGameMonth = -1;
    int lastGameYear = -1;
    UInt16 lastPlayerLevel = 0;
    int lastKarmaClass = 99;
    std::unordered_map<UInt32, std::string> knownTeammates;
    std::map<unsigned long long, UInt32> questObjectiveStatus;
    DWORD lastSlowPollTick = 0;

    // Combat encounter aggregation.
    bool combatActive = false;
    DWORD combatStartTick = 0;
    DWORD combatLastActivityTick = 0;
    std::string combatPlace;
    std::map<UInt32, std::string> combatParticipants; // refId -> display name
    std::set<UInt32> combatTeammates;                 // participants who were allies
    std::vector<std::string> combatKills;
    int combatHits = 0;
    bool combatPlayerInvolved = false;

    // Loot window aggregation.
    DWORD lootFirstTick = 0;
    DWORD lootLastTick = 0;
    std::map<std::string, int> lootCounts;            // display name -> count
    std::vector<std::string> lootNotables;

    // Equip / unequip / consumable dedup.
    std::unordered_map<UInt32, DWORD> recentEquips;      // item formId -> tick
    std::unordered_map<UInt32, DWORD> recentUnequips;    // item formId -> tick
    std::unordered_map<UInt32, DWORD> recentConsumables; // aid formId -> tick

    // Drop window aggregation.
    DWORD dropFirstTick = 0;
    DWORD dropLastTick = 0;
    std::map<std::string, int> dropCounts;              // display name -> count

    // Out-of-combat shooting burst.
    DWORD shootFirstTick = 0;
    DWORD shootLastTick = 0;
    int shootShots = 0;
    std::string shootWeapon;

    // Conversation markers.
    std::string lastConversationNpcKey;
    DWORD lastConversationTick = 0;
    bool dialogMenuWasOpen = false;
    DWORD dialogMenuOpenTick = 0;
    std::string dialogMenuPartner;
};

GameEventLogState g_eventLog;

GameTimeGlobalsLayout* GetGameTimeGlobals()
{
    return reinterpret_cast<GameTimeGlobalsLayout*>(kGameTimeGlobalsAddress);
}

std::string CurrentGameTimeString()
{
    const GameTimeGlobalsLayout* globals = GetGameTimeGlobals();
    if (!globals || !globals->year || !globals->month || !globals->day || !globals->hour)
    {
        return "";
    }
    static const char* kMonthNames[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const int year = static_cast<int>(globals->year->data);
    const int month = static_cast<int>(globals->month->data);
    const int day = static_cast<int>(globals->day->data);
    const float hourFloat = globals->hour->data;
    const int hour = static_cast<int>(hourFloat);
    const int minute = static_cast<int>((hourFloat - static_cast<float>(hour)) * 60.0f);
    const char* monthName = (month >= 1 && month <= 12) ? kMonthNames[month - 1] : "?";
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d, %d %s %d", hour, minute, day, monthName, year);
    return buffer;
}

// The player's current place, resolved to READABLE major/minor names — the
// world/local map markers (already display strings) and cell/worldspace DISPLAY
// names ("Doc Mitchell's House", "Mojave Wasteland") — never an editor id.
struct EventPlace
{
    std::string major; // broad region: "Goodsprings", "Mojave Wasteland"
    std::string minor; // specific spot:  "Prospector Saloon", "Goodsprings Cemetery"
};

EventPlace ResolveEventPlace()
{
    EventPlace place;
    PlayerCharacter* player = GetPlayer();
    if (!player || !player->parentCell)
    {
        return place;
    }
    TESObjectCELL* cell = player->parentCell;
    const bool interior = cell->worldSpace == nullptr;

    // Major: nearest world-map marker, else the worldspace / inferred region.
    place.major = FindNearestWorldMapLocation(player);
    if (place.major.empty())
    {
        if (interior)
        {
            place.major = InferMajorLocationFromCellIdentifier(GetFormNameSafe(cell));
        }
        else if (cell->worldSpace)
        {
            place.major = GetDisplayNameSafe(cell->worldSpace);
        }
    }

    // Minor: the specific spot. Interiors use the cell's own display name;
    // exteriors use the nearest named landmark (empty in open wilderness).
    if (interior)
    {
        place.minor = GetDisplayNameSafe(cell);
        if (place.minor.empty())
        {
            place.minor = FindNearestLocalMapLocation(player);
        }
    }
    else
    {
        place.minor = FindNearestLocalMapLocation(player);
    }
    if (!place.minor.empty() && place.minor == place.major)
    {
        place.minor.clear();
    }
    return place;
}

// Combined one-line place for an event row: "Prospector Saloon, Goodsprings".
std::string ComposeEventPlace(const EventPlace& place)
{
    if (!place.minor.empty() && !place.major.empty() && place.minor != place.major)
    {
        return place.minor + ", " + place.major;
    }
    if (!place.minor.empty())
    {
        return place.minor;
    }
    return place.major;
}

// Elapsed in-game days since the save began (GameDaysPassed global). 1-based so
// the first day reads "Day 1". -1 when the global is unavailable.
int CurrentGameDay()
{
    const GameTimeGlobalsLayout* globals = GetGameTimeGlobals();
    if (!globals || !globals->daysPassed)
    {
        return -1;
    }
    return static_cast<int>(globals->daysPassed->data) + 1;
}

void LogHookFirstFire(const char* hook)
{
    if (g_eventLog.loggedHookFirstFire.insert(hook).second)
    {
        LogLine("event-log: hook %s fired for the first time this session.", hook);
    }
}

// Queue one event as a serialized JSON line. Thread-safe (WriteRequest can run
// off the main thread); everything else in the event log is main-thread only.
void QueueGameEvent(const char* type, const std::string& summary,
    const std::vector<std::pair<std::string, std::string>>& actors,
    const std::string& locationOverride)
{
    if (!type || summary.empty())
    {
        return;
    }
    const EventPlace place = ResolveEventPlace();
    const std::string location = locationOverride.empty() ? ComposeEventPlace(place) : locationOverride;
    const std::string gameTime = CurrentGameTimeString();
    const int gameDay = CurrentGameDay();

    std::ostringstream json;
    json << "{\"id\":\"e" << static_cast<unsigned long>(GetTickCount()) << "-" << ++g_eventLog.eventSerial << "\"";
    json << ",\"type\":" << JsonEscape(type);
    json << ",\"summary\":" << JsonEscape(summary);
    json << ",\"realTime\":" << JsonEscape(NowIsoUtc());
    if (!gameTime.empty())
    {
        json << ",\"gameTime\":" << JsonEscape(gameTime);
    }
    if (gameDay >= 0)
    {
        json << ",\"gameDay\":" << gameDay;
    }
    if (!location.empty())
    {
        json << ",\"location\":" << JsonEscape(location);
    }
    if (!actors.empty())
    {
        json << ",\"actors\":[";
        bool first = true;
        for (const auto& actor : actors)
        {
            if (!first)
            {
                json << ",";
            }
            first = false;
            json << "{\"name\":" << JsonEscape(actor.first);
            if (!actor.second.empty())
            {
                json << ",\"id\":" << JsonEscape(actor.second);
            }
            json << "}";
        }
        json << "]";
    }
    // Structured major/minor place, for the future Gamemaster (the row shows the
    // combined `location` above; these keep the parts machine-readable).
    if (!place.major.empty() || !place.minor.empty())
    {
        json << ",\"data\":{";
        bool first = true;
        if (!place.major.empty())
        {
            json << "\"locationMajor\":" << JsonEscape(place.major);
            first = false;
        }
        if (!place.minor.empty())
        {
            if (!first)
            {
                json << ",";
            }
            json << "\"locationMinor\":" << JsonEscape(place.minor);
        }
        json << "}";
    }
    json << "}";

    {
        std::lock_guard<std::mutex> lock(g_eventLog.pendingMutex);
        if (g_eventLog.pending.size() >= kEventPendingHardCap)
        {
            g_eventLog.pending.erase(g_eventLog.pending.begin());
        }
        g_eventLog.pending.push_back(json.str());
    }
    LogLine("event-log: [%s] %s", type, summary.c_str());
}

void FlushGameEvents(bool force)
{
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_eventLog.pendingMutex);
        if (g_eventLog.pending.empty())
        {
            return;
        }
        const DWORD now = GetTickCount();
        if (!force
            && g_eventLog.pending.size() < kEventFlushBatchSize
            && g_eventLog.lastFlushTick
            && (now - g_eventLog.lastFlushTick) < kEventFlushIntervalMs)
        {
            return;
        }
        lines.swap(g_eventLog.pending);
        g_eventLog.lastFlushTick = now;
    }

    EnsureBridgeDirectories();
    char name[80]{};
    std::snprintf(name, sizeof(name), "evt_%lu_%u.txt",
        static_cast<unsigned long>(GetTickCount()), ++g_eventLog.batchSerial);
    // Write behind a "__" prefix (the bridge skips those), then rename into
    // place so a half-written batch is never picked up.
    const fs::path finalPath = GameEventsDir() / name;
    const fs::path tempPath = GameEventsDir() / (std::string("__") + name);
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            LogLine("event-log: failed to open batch file %s; dropping %zu event(s).",
                tempPath.string().c_str(), lines.size());
            return;
        }
        for (const std::string& line : lines)
        {
            out << line << "\n";
        }
    }
    std::error_code ec;
    fs::rename(tempPath, finalPath, ec);
    if (ec)
    {
        LogLine("event-log: failed to publish batch %s (%s).", name, ec.message().c_str());
        fs::remove(tempPath, ec);
        return;
    }
    LogLine("event-log: flushed %zu event(s) to %s.", lines.size(), name);
}

bool IsPlayerRef(const TESObjectREFR* ref)
{
    return ref && ref == static_cast<const TESObjectREFR*>(GetPlayer());
}

bool IsNearPlayerForEvents(TESObjectREFR* ref)
{
    PlayerCharacter* player = GetPlayer();
    if (!player || !ref)
    {
        return false;
    }
    if (ref == player)
    {
        return true;
    }
    return DistanceSquared3D(player, ref) <= (kEventNearbyDistance * kEventNearbyDistance);
}

// Resolve a form/reference to its VISIBLE name ("Boone", "Ghost Town
// Gunfight") rather than its editor id ("CraigBooneREF", "VMS01"). GetTheName
// (display) is tried first; the editor id is only a last resort so a named
// form never logs as an internal id.
std::string EventDisplayName(TESForm* form)
{
    const std::string display = GetDisplayNameSafe(form);
    if (!display.empty())
    {
        return display;
    }
    return GetFormNameSafe(form);
}

std::string EventActorName(TESObjectREFR* ref)
{
    if (!ref)
    {
        return "";
    }
    if (IsPlayerRef(ref))
    {
        return "You";
    }
    return EventDisplayName(ref);
}

bool IsKnownTeammateRefId(UInt32 refId)
{
    return g_eventLog.knownTeammates.find(refId) != g_eventLog.knownTeammates.end();
}

void TouchCombatEncounter(TESObjectREFR* actorRef)
{
    const DWORD now = GetTickCount();
    if (!g_eventLog.combatActive)
    {
        g_eventLog.combatActive = true;
        g_eventLog.combatStartTick = now;
        g_eventLog.combatPlace = ComposeEventPlace(ResolveEventPlace());
        g_eventLog.combatParticipants.clear();
        g_eventLog.combatTeammates.clear();
        g_eventLog.combatKills.clear();
        g_eventLog.combatHits = 0;
        g_eventLog.combatPlayerInvolved = false;
        // Any in-flight "shooting" burst was the opening of this fight, not
        // idle plinking — discard it so it never double-logs alongside combat.
        g_eventLog.shootFirstTick = 0;
        g_eventLog.shootLastTick = 0;
        g_eventLog.shootShots = 0;
        g_eventLog.shootWeapon.clear();
        LogHookFirstFire("combat_encounter_open");
    }
    g_eventLog.combatLastActivityTick = now;
    if (!actorRef)
    {
        return;
    }
    if (IsPlayerRef(actorRef))
    {
        g_eventLog.combatPlayerInvolved = true;
        return;
    }
    const std::string name = EventActorName(actorRef);
    if (name.empty())
    {
        return;
    }
    g_eventLog.combatParticipants[actorRef->refID] = name;
    if (IsKnownTeammateRefId(actorRef->refID))
    {
        g_eventLog.combatTeammates.insert(actorRef->refID);
        g_eventLog.combatPlayerInvolved = true;
    }
}

void EmitCombatEncounterEvent()
{
    if (!g_eventLog.combatActive)
    {
        return;
    }
    g_eventLog.combatActive = false;

    // Ignore encounters the player never touched (distant NPC squabbles).
    if (!g_eventLog.combatPlayerInvolved && g_eventLog.combatKills.empty())
    {
        return;
    }

    // Group foes by display name ("Powder Ganger x3"), allies listed by name.
    std::map<std::string, int> foeCounts;
    std::vector<std::string> allies;
    for (const auto& entry : g_eventLog.combatParticipants)
    {
        if (g_eventLog.combatTeammates.count(entry.first))
        {
            allies.push_back(entry.second);
        }
        else
        {
            foeCounts[entry.second] += 1;
        }
    }

    std::string foes;
    int foeNames = 0;
    for (const auto& entry : foeCounts)
    {
        if (foeNames == 3)
        {
            foes += " and others";
            break;
        }
        if (foeNames)
        {
            foes += ", ";
        }
        foes += entry.first;
        if (entry.second > 1)
        {
            char suffix[16]{};
            std::snprintf(suffix, sizeof(suffix), " x%d", entry.second);
            foes += suffix;
        }
        ++foeNames;
    }

    std::string summary = foes.empty() ? "A fight broke out" : ("Fought " + foes);
    if (!allies.empty())
    {
        summary += " alongside ";
        for (size_t i = 0; i < allies.size() && i < 3; ++i)
        {
            if (i)
            {
                summary += ", ";
            }
            summary += allies[i];
        }
    }
    if (!g_eventLog.combatKills.empty())
    {
        summary += " — ";
        for (size_t i = 0; i < g_eventLog.combatKills.size(); ++i)
        {
            if (i == 4)
            {
                summary += "; …";
                break;
            }
            if (i)
            {
                summary += "; ";
            }
            summary += g_eventLog.combatKills[i];
        }
    }
    else
    {
        summary += " — no one died";
    }

    std::vector<std::pair<std::string, std::string>> actors;
    for (const auto& entry : g_eventLog.combatParticipants)
    {
        if (actors.size() >= 8)
        {
            break;
        }
        actors.emplace_back(entry.second, FormIdHex(entry.first));
    }
    QueueGameEvent("combat", summary, actors, g_eventLog.combatPlace);
}

// --- xNVSE EventManager native handlers (params = void*[2] {source, object},
// per EventManager's classic-event dispatch) --------------------------------

void OnDeathEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    LogHookFirstFire("ondeath");
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* dying = static_cast<TESObjectREFR*>(params[0]);
    auto* killerForm = static_cast<TESForm*>(params[1]);
    if (!dying)
    {
        return;
    }
    auto* killerRef = reinterpret_cast<TESObjectREFR*>(killerForm);
    const bool killerIsPlayer = killerForm && killerRef == static_cast<TESObjectREFR*>(GetPlayer());
    const bool victimIsPlayer = IsPlayerRef(dying);
    const bool victimIsTeammate = IsKnownTeammateRefId(dying->refID);

    // Off-screen deaths with no player stake are noise.
    if (!victimIsPlayer && !victimIsTeammate && !killerIsPlayer && !IsNearPlayerForEvents(dying))
    {
        return;
    }

    const std::string victimName = EventActorName(dying);
    std::string killerName = killerForm ? EventDisplayName(killerForm) : "";
    if (killerIsPlayer)
    {
        killerName = "You";
    }

    std::string kill;
    if (killerIsPlayer)
    {
        kill = "You killed " + victimName;
        TESObjectWEAP* weapon = GetPlayer() ? GetPlayer()->GetEquippedWeapon() : nullptr;
        const std::string weaponName = weapon ? GetDisplayNameSafe(weapon) : "";
        if (!weaponName.empty())
        {
            kill += " with " + weaponName;
        }
    }
    else if (!killerName.empty() && killerName != victimName)
    {
        kill = killerName + " killed " + victimName;
    }
    else
    {
        kill = victimName + " died";
    }

    // High-stakes deaths surface immediately; everything else folds into the
    // running combat encounter (or a lone death event outside combat).
    if (victimIsPlayer)
    {
        QueueGameEvent("death", killerName.empty() ? "You died" : ("You were killed by " + killerName));
        return;
    }
    if (victimIsTeammate)
    {
        QueueGameEvent("death", "Companion " + victimName + " was killed"
            + (killerName.empty() ? "" : (" by " + killerName)));
        TouchCombatEncounter(dying);
        return;
    }
    if (g_eventLog.combatActive)
    {
        TouchCombatEncounter(dying);
        if (g_eventLog.combatKills.size() < 12)
        {
            g_eventLog.combatKills.push_back(kill);
        }
        return;
    }
    if (killerIsPlayer)
    {
        QueueGameEvent("death", kill,
            { { victimName, FormIdHex(dying->refID) } });
    }
}

void OnStartCombatEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    LogHookFirstFire("onstartcombat");
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* actor = static_cast<TESObjectREFR*>(params[0]);
    auto* target = reinterpret_cast<TESObjectREFR*>(static_cast<TESForm*>(params[1]));
    const bool relevant = IsPlayerRef(actor) || IsPlayerRef(target)
        || IsNearPlayerForEvents(actor);
    if (!relevant)
    {
        return;
    }
    TouchCombatEncounter(actor);
    if (target)
    {
        TouchCombatEncounter(target);
    }
}

void OnCombatEndEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    LogHookFirstFire("oncombatend");
    if (!g_eventLog.combatActive || !parameters)
    {
        return;
    }
    // Just mark activity; the slow poll closes the encounter once the player
    // is out of combat and things have been quiet.
    g_eventLog.combatLastActivityTick = GetTickCount();
}

void OnHitEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    LogHookFirstFire("onhit");
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* victim = static_cast<TESObjectREFR*>(params[0]);
    auto* attacker = reinterpret_cast<TESObjectREFR*>(static_cast<TESForm*>(params[1]));
    const bool relevant = IsPlayerRef(victim) || IsPlayerRef(attacker)
        || IsNearPlayerForEvents(victim);
    if (!relevant)
    {
        return;
    }
    // Hits NEVER emit events — they only feed the encounter aggregate.
    TouchCombatEncounter(victim);
    if (attacker)
    {
        TouchCombatEncounter(attacker);
    }
    g_eventLog.combatHits += 1;
}

bool IsNotableInventoryForm(TESForm* form)
{
    if (!form)
    {
        return false;
    }
    if (form->flags & kFormFlagQuestItem)
    {
        return true;
    }
    return form->typeID == kFormType_TESObjectWEAP
        || form->typeID == kFormType_TESObjectARMO
        || form->typeID == kFormType_TESObjectBOOK;
}

void RecordPlayerItemPickup(TESForm* itemForm)
{
    const std::string name = GetDisplayNameSafe(itemForm);
    if (name.empty())
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_eventLog.lootFirstTick)
    {
        g_eventLog.lootFirstTick = now;
    }
    g_eventLog.lootLastTick = now;
    g_eventLog.lootCounts[name] += 1;
    if (IsNotableInventoryForm(itemForm)
        && std::find(g_eventLog.lootNotables.begin(), g_eventLog.lootNotables.end(), name)
            == g_eventLog.lootNotables.end())
    {
        g_eventLog.lootNotables.push_back(name);
    }
}

void EmitLootWindowEvent()
{
    if (!g_eventLog.lootFirstTick)
    {
        return;
    }
    int total = 0;
    for (const auto& entry : g_eventLog.lootCounts)
    {
        total += entry.second;
    }
    std::string summary;
    if (total == 1)
    {
        summary = "Picked up " + g_eventLog.lootCounts.begin()->first;
    }
    else
    {
        // Call out notables first, then pad with the most common names.
        std::vector<std::string> highlights = g_eventLog.lootNotables;
        for (const auto& entry : g_eventLog.lootCounts)
        {
            if (highlights.size() >= 3)
            {
                break;
            }
            if (std::find(highlights.begin(), highlights.end(), entry.first) == highlights.end())
            {
                highlights.push_back(entry.first);
            }
        }
        char head[48]{};
        std::snprintf(head, sizeof(head), "Picked up %d items", total);
        summary = head;
        if (!highlights.empty())
        {
            summary += " incl. ";
            for (size_t i = 0; i < highlights.size() && i < 3; ++i)
            {
                if (i)
                {
                    summary += ", ";
                }
                summary += highlights[i];
                const auto countIt = g_eventLog.lootCounts.find(highlights[i]);
                if (countIt != g_eventLog.lootCounts.end() && countIt->second > 1)
                {
                    char suffix[16]{};
                    std::snprintf(suffix, sizeof(suffix), " x%d", countIt->second);
                    summary += suffix;
                }
            }
        }
    }
    g_eventLog.lootFirstTick = 0;
    g_eventLog.lootLastTick = 0;
    g_eventLog.lootCounts.clear();
    g_eventLog.lootNotables.clear();
    QueueGameEvent("item", summary);
}

void OnAddEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* first = static_cast<TESObjectREFR*>(params[0]);
    auto* second = static_cast<TESForm*>(params[1]);
    if (g_eventLog.loggedHookFirstFire.insert("onadd").second)
    {
        // Defensive: log the raw param orientation once, so the first play
        // session confirms which side is the item and which the container.
        LogLine("event-log: hook onadd fired for the first time this session (p0 type=%u, p1 type=%u).",
            first ? static_cast<unsigned>(first->typeID) : 0,
            second ? static_cast<unsigned>(second->typeID) : 0);
    }
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    // Classic OnAdd marks the ITEM ref with the container as the object, but
    // accept the swapped orientation too.
    if (second == static_cast<TESForm*>(player) && first && first->baseForm)
    {
        RecordPlayerItemPickup(first->baseForm);
    }
    else if (first == static_cast<TESObjectREFR*>(player) && second)
    {
        RecordPlayerItemPickup(second);
    }
}

void OnActorEquipEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* first = static_cast<TESObjectREFR*>(params[0]);
    auto* second = static_cast<TESForm*>(params[1]);
    if (g_eventLog.loggedHookFirstFire.insert("onactorequip").second)
    {
        LogLine("event-log: hook onactorequip fired for the first time this session (p0 type=%u, p1 type=%u).",
            first ? static_cast<unsigned>(first->typeID) : 0,
            second ? static_cast<unsigned>(second->typeID) : 0);
    }
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    TESForm* item = nullptr;
    if (first == static_cast<TESObjectREFR*>(player) && second)
    {
        item = second;
    }
    else if (second == static_cast<TESForm*>(player) && first && first->baseForm)
    {
        item = first->baseForm;
    }
    if (!item)
    {
        return;
    }
    const DWORD now = GetTickCount();
    const std::string name = GetDisplayNameSafe(item);

    // Consumables: FNV "uses" an aid item by equipping it briefly. Classify by
    // the ALCH flags into eating / medicine / other so the summary reads right.
    if (item->typeID == kFormType_AlchemyItem)
    {
        const auto recent = g_eventLog.recentConsumables.find(item->refID);
        if (recent != g_eventLog.recentConsumables.end() && (now - recent->second) < kConsumableDedupMs)
        {
            return;
        }
        g_eventLog.recentConsumables[item->refID] = now;
        if (name.empty())
        {
            return;
        }
        const UInt8 alchFlags = reinterpret_cast<AlchemyItem*>(item)->alchFlags;
        if (g_eventLog.loggedHookFirstFire.insert("consumable").second)
        {
            LogLine("event-log: first consumable use (%s, alchFlags=0x%02X).", name.c_str(), static_cast<unsigned>(alchFlags));
        }
        const char* verb =
            (alchFlags & kAlchFlagFood) ? "Ate "
            : (alchFlags & kAlchFlagMedicine) ? "Used "
            : "Consumed ";
        QueueGameEvent("item", verb + name);
        return;
    }

    if (item->typeID != kFormType_TESObjectWEAP && item->typeID != kFormType_TESObjectARMO)
    {
        return;
    }
    const auto recent = g_eventLog.recentEquips.find(item->refID);
    if (recent != g_eventLog.recentEquips.end() && (now - recent->second) < kEquipDedupMs)
    {
        return;
    }
    g_eventLog.recentEquips[item->refID] = now;
    if (!name.empty())
    {
        QueueGameEvent("item", "Equipped " + name);
    }
}

void OnActorUnequipEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* first = static_cast<TESObjectREFR*>(params[0]);
    auto* second = static_cast<TESForm*>(params[1]);
    if (g_eventLog.loggedHookFirstFire.insert("onactorunequip").second)
    {
        LogLine("event-log: hook onactorunequip fired for the first time this session (p0 type=%u, p1 type=%u).",
            first ? static_cast<unsigned>(first->typeID) : 0,
            second ? static_cast<unsigned>(second->typeID) : 0);
    }
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    TESForm* item = nullptr;
    if (first == static_cast<TESObjectREFR*>(player) && second)
    {
        item = second;
    }
    else if (second == static_cast<TESForm*>(player) && first && first->baseForm)
    {
        item = first->baseForm;
    }
    // Only weapons and worn gear — unequipping ammo/tokens/etc. is noise.
    if (!item || (item->typeID != kFormType_TESObjectWEAP && item->typeID != kFormType_TESObjectARMO))
    {
        return;
    }
    const DWORD now = GetTickCount();
    const auto recent = g_eventLog.recentUnequips.find(item->refID);
    if (recent != g_eventLog.recentUnequips.end() && (now - recent->second) < kEquipDedupMs)
    {
        return;
    }
    g_eventLog.recentUnequips[item->refID] = now;
    const std::string name = GetDisplayNameSafe(item);
    if (!name.empty())
    {
        QueueGameEvent("item", "Unequipped " + name);
    }
}

// A dropped item feeds a short aggregation window, like loot (dropping a stack
// or clearing junk shouldn't spam one event per item).
void RecordPlayerItemDrop(TESForm* itemForm)
{
    const std::string name = GetDisplayNameSafe(itemForm);
    if (name.empty())
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_eventLog.dropFirstTick)
    {
        g_eventLog.dropFirstTick = now;
    }
    g_eventLog.dropLastTick = now;
    g_eventLog.dropCounts[name] += 1;
}

void EmitDropWindowEvent()
{
    if (!g_eventLog.dropFirstTick)
    {
        return;
    }
    int total = 0;
    for (const auto& entry : g_eventLog.dropCounts)
    {
        total += entry.second;
    }
    std::string summary;
    if (total == 1)
    {
        summary = "Dropped " + g_eventLog.dropCounts.begin()->first;
    }
    else
    {
        char head[48]{};
        std::snprintf(head, sizeof(head), "Dropped %d items", total);
        summary = head;
        int shown = 0;
        for (const auto& entry : g_eventLog.dropCounts)
        {
            if (shown == 3)
            {
                break;
            }
            summary += (shown == 0) ? " incl. " : ", ";
            summary += entry.first;
            if (entry.second > 1)
            {
                char suffix[16]{};
                std::snprintf(suffix, sizeof(suffix), " x%d", entry.second);
                summary += suffix;
            }
            ++shown;
        }
    }
    g_eventLog.dropFirstTick = 0;
    g_eventLog.dropLastTick = 0;
    g_eventLog.dropCounts.clear();
    QueueGameEvent("item", summary);
}

// Out-of-combat shooting: an isolated shot or burst (hunting, target practice,
// a warning shot) becomes ONE event when the burst goes quiet. If real combat
// starts mid-burst, EmitCombatEncounterEvent supersedes it and this is dropped.
void EmitShootingEvent()
{
    if (!g_eventLog.shootFirstTick)
    {
        return;
    }
    const int shots = g_eventLog.shootShots;
    const std::string weapon = g_eventLog.shootWeapon;
    g_eventLog.shootFirstTick = 0;
    g_eventLog.shootLastTick = 0;
    g_eventLog.shootShots = 0;
    g_eventLog.shootWeapon.clear();

    std::string summary;
    if (shots <= 1)
    {
        summary = weapon.empty() ? "Took a shot" : ("Fired a shot from your " + weapon);
    }
    else
    {
        char head[96]{};
        std::snprintf(head, sizeof(head), "Fired %d rounds", shots);
        summary = head;
        if (!weapon.empty())
        {
            summary += " from your " + weapon;
        }
    }
    QueueGameEvent("shooting", summary);
}

void OnDropEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* first = static_cast<TESObjectREFR*>(params[0]);
    auto* second = static_cast<TESForm*>(params[1]);
    if (g_eventLog.loggedHookFirstFire.insert("ondrop").second)
    {
        LogLine("event-log: hook ondrop fired for the first time this session (p0 type=%u, p1 type=%u).",
            first ? static_cast<unsigned>(first->typeID) : 0,
            second ? static_cast<unsigned>(second->typeID) : 0);
    }
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    // Classic OnDrop marks the dropping actor with the item base as the object;
    // accept the swapped orientation too.
    if (first == static_cast<TESObjectREFR*>(player) && second)
    {
        RecordPlayerItemDrop(second);
    }
    else if (second == static_cast<TESForm*>(player) && first && first->baseForm)
    {
        RecordPlayerItemDrop(first->baseForm);
    }
}

void OnFireEventHandler(TESObjectREFR* /*thisObj*/, void* parameters)
{
    if (!parameters)
    {
        return;
    }
    void** params = static_cast<void**>(parameters);
    auto* first = static_cast<TESObjectREFR*>(params[0]);
    auto* second = static_cast<TESForm*>(params[1]);
    if (g_eventLog.loggedHookFirstFire.insert("onfire").second)
    {
        LogLine("event-log: hook onfire fired for the first time this session (p0 type=%u, p1 type=%u).",
            first ? static_cast<unsigned>(first->typeID) : 0,
            second ? static_cast<unsigned>(second->typeID) : 0);
    }
    // Only the PLAYER firing, and only while NOT in a combat encounter (in
    // combat the shots are part of the fight and would be pure noise).
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    const bool playerFired = (first == static_cast<TESObjectREFR*>(player))
        || (second == static_cast<TESForm*>(player));
    if (!playerFired || g_eventLog.combatActive || player->unk104)
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_eventLog.shootFirstTick)
    {
        g_eventLog.shootFirstTick = now;
        g_eventLog.shootShots = 0;
        TESObjectWEAP* weapon = player->GetEquippedWeapon();
        g_eventLog.shootWeapon = weapon ? GetDisplayNameSafe(weapon) : "";
    }
    g_eventLog.shootLastTick = now;
    g_eventLog.shootShots += 1;
}

void RegisterGameEventHandlers()
{
    if (g_eventLog.handlersRegistered)
    {
        return;
    }
    if (!g_eventManager)
    {
        LogLine("event-log: xNVSE EventManager interface unavailable; hook-based events disabled (poll-based events still run).");
        return;
    }
    struct HookSpec
    {
        const char* name;
        NVSEEventManagerInterface::NativeEventHandler handler;
    };
    const HookSpec hooks[] = {
        { "ondeath", OnDeathEventHandler },
        { "onstartcombat", OnStartCombatEventHandler },
        { "oncombatend", OnCombatEndEventHandler },
        { "onhit", OnHitEventHandler },
        { "onadd", OnAddEventHandler },
        { "onactorequip", OnActorEquipEventHandler },
        { "onactorunequip", OnActorUnequipEventHandler },
        { "ondrop", OnDropEventHandler },
        { "onfire", OnFireEventHandler },
    };
    for (const HookSpec& hook : hooks)
    {
        const bool ok = g_eventManager->SetNativeEventHandler(hook.name, hook.handler);
        LogLine("event-log: register %s handler -> %s.", hook.name, ok ? "ok" : "FAILED");
    }
    g_eventLog.handlersRegistered = true;
}

void ResetGameEventRuntime(const char* reason)
{
    {
        std::lock_guard<std::mutex> lock(g_eventLog.pendingMutex);
        g_eventLog.pending.clear();
    }
    g_eventLog.primed = false;
    g_eventLog.lastSlowPollTick = 0;
    g_eventLog.combatActive = false;
    g_eventLog.combatParticipants.clear();
    g_eventLog.combatTeammates.clear();
    g_eventLog.combatKills.clear();
    g_eventLog.lootFirstTick = 0;
    g_eventLog.lootLastTick = 0;
    g_eventLog.lootCounts.clear();
    g_eventLog.lootNotables.clear();
    g_eventLog.recentEquips.clear();
    g_eventLog.recentUnequips.clear();
    g_eventLog.recentConsumables.clear();
    g_eventLog.dropFirstTick = 0;
    g_eventLog.dropLastTick = 0;
    g_eventLog.dropCounts.clear();
    g_eventLog.shootFirstTick = 0;
    g_eventLog.shootLastTick = 0;
    g_eventLog.shootShots = 0;
    g_eventLog.shootWeapon.clear();
    g_eventLog.knownTeammates.clear();
    g_eventLog.questObjectiveStatus.clear();
    g_eventLog.lastConversationNpcKey.clear();
    g_eventLog.lastConversationTick = 0;
    g_eventLog.dialogMenuWasOpen = false;
    g_eventLog.dialogMenuPartner.clear();
    LogLine("event-log: runtime reset (%s).", reason ? reason : "");
}

void NoteConversationRequestForEventLog(const std::string& npcKey, const std::string& npcName)
{
    if (npcKey.empty() || npcKey == kAdminNpcKey || npcName.empty())
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (npcKey == g_eventLog.lastConversationNpcKey
        && g_eventLog.lastConversationTick
        && (now - g_eventLog.lastConversationTick) < kConversationMarkerDedupMs)
    {
        g_eventLog.lastConversationTick = now;
        return;
    }
    g_eventLog.lastConversationNpcKey = npcKey;
    g_eventLog.lastConversationTick = now;
    QueueGameEvent("conversation", "Talked with " + npcName, { { npcName, "" } });
}

int KarmaClassOf(float karma)
{
    if (karma <= -750.0f) return -2;
    if (karma <= -250.0f) return -1;
    if (karma < 250.0f) return 0;
    if (karma < 750.0f) return 1;
    return 2;
}

const char* KarmaClassName(int karmaClass)
{
    switch (karmaClass)
    {
    case -2: return "very evil";
    case -1: return "evil";
    case 0: return "neutral";
    case 1: return "good";
    default: return "very good";
    }
}

// The 1 Hz slow poll: travel, calendar, level, karma, teammates, quest
// objectives, vanilla dialogue, plus encounter/loot window closure + flushing.
void UpdateGameEventLog()
{
    PlayerCharacter* player = GetPlayer();
    if (!player)
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (g_eventLog.lastSlowPollTick && (now - g_eventLog.lastSlowPollTick) < kEventSlowPollMs)
    {
        return;
    }
    g_eventLog.lastSlowPollTick = now;

    const bool priming = !g_eventLog.primed;

    // --- travel (named-place changes only; no marker scans here) ------------
    TESObjectCELL* cell = player->parentCell;
    if (cell)
    {
        // Display names ("Doc Mitchell's House", "Mojave Wasteland"), never the
        // editor id. Unnamed exterior wilderness cells have an EMPTY display
        // name, which the !cellName.empty() guards below skip naturally — so no
        // "Wilderness" substring hack is needed.
        const std::string cellName = GetDisplayNameSafe(cell);
        const bool interior = cell->worldSpace == nullptr;
        const std::string worldspaceName = interior ? "" : GetDisplayNameSafe(cell->worldSpace);
        if (!priming)
        {
            if (interior && !g_eventLog.lastCellWasInterior && !cellName.empty())
            {
                QueueGameEvent("location", "Entered " + cellName, {}, cellName);
            }
            else if (!interior && g_eventLog.lastCellWasInterior)
            {
                const std::string outside = !worldspaceName.empty() ? worldspaceName : std::string("the open wasteland");
                QueueGameEvent("location", "Stepped out into " + outside, {}, worldspaceName);
            }
            else if (interior && !cellName.empty() && cellName != g_eventLog.lastCellName)
            {
                QueueGameEvent("location", "Entered " + cellName, {}, cellName);
            }
            else if (!interior && !worldspaceName.empty() && worldspaceName != g_eventLog.lastWorldspaceName
                && !g_eventLog.lastWorldspaceName.empty())
            {
                QueueGameEvent("location", "Traveled to " + worldspaceName, {}, worldspaceName);
            }
            else if (!interior && !cellName.empty() && cellName != g_eventLog.lastCellName
                && cellName != worldspaceName)
            {
                QueueGameEvent("location", "Arrived at " + cellName, {}, cellName);
            }
        }
        g_eventLog.lastCellName = cellName;
        g_eventLog.lastWorldspaceName = worldspaceName;
        g_eventLog.lastCellWasInterior = interior;
    }

    // --- calendar ------------------------------------------------------------
    const GameTimeGlobalsLayout* timeGlobals = GetGameTimeGlobals();
    if (timeGlobals && timeGlobals->day && timeGlobals->month && timeGlobals->year)
    {
        const int day = static_cast<int>(timeGlobals->day->data);
        const int month = static_cast<int>(timeGlobals->month->data);
        const int year = static_cast<int>(timeGlobals->year->data);
        if (!priming
            && (day != g_eventLog.lastGameDay || month != g_eventLog.lastGameMonth || year != g_eventLog.lastGameYear)
            && g_eventLog.lastGameDay > 0)
        {
            const std::string stamp = CurrentGameTimeString();
            const size_t comma = stamp.find(", ");
            const std::string date = comma == std::string::npos ? stamp : stamp.substr(comma + 2);
            QueueGameEvent("day", "A new day — " + date);
        }
        g_eventLog.lastGameDay = day;
        g_eventLog.lastGameMonth = month;
        g_eventLog.lastGameYear = year;
    }

    // --- level + karma ---------------------------------------------------------
    const UInt16 level = player->avOwner.Fn_0A();
    if (!priming && level > g_eventLog.lastPlayerLevel && g_eventLog.lastPlayerLevel > 0)
    {
        char summary[48]{};
        std::snprintf(summary, sizeof(summary), "Reached level %u", static_cast<unsigned>(level));
        QueueGameEvent("level", summary);
    }
    g_eventLog.lastPlayerLevel = level;

    const float karma = player->avOwner.Fn_03(kActorValueKarma);
    const int karmaClass = KarmaClassOf(karma);
    if (!priming && karmaClass != g_eventLog.lastKarmaClass && g_eventLog.lastKarmaClass != 99)
    {
        QueueGameEvent("karma", std::string("Karma shifted — now regarded as ") + KarmaClassName(karmaClass));
    }
    g_eventLog.lastKarmaClass = karmaClass;

    // --- teammates -------------------------------------------------------------
    {
        std::unordered_map<UInt32, std::string> current;
        for (auto it = player->teammates.Begin(); !it.End(); ++it)
        {
            Actor* teammate = it.Get();
            if (!teammate)
            {
                continue;
            }
            const std::string name = EventDisplayName(teammate);
            if (!name.empty())
            {
                current[teammate->refID] = name;
            }
        }
        if (!priming)
        {
            for (const auto& entry : current)
            {
                if (!g_eventLog.knownTeammates.count(entry.first))
                {
                    QueueGameEvent("companion", entry.second + " joined you as a companion",
                        { { entry.second, FormIdHex(entry.first) } });
                }
            }
            for (const auto& entry : g_eventLog.knownTeammates)
            {
                if (!current.count(entry.first))
                {
                    QueueGameEvent("companion", entry.second + " parted ways with you",
                        { { entry.second, FormIdHex(entry.first) } });
                }
            }
        }
        g_eventLog.knownTeammates.swap(current);
    }

    // --- quest objectives --------------------------------------------------------
    {
        for (auto it = player->questObjectiveList.Begin(); !it.End(); ++it)
        {
            BGSQuestObjective* objective = it.Get();
            if (!objective || !objective->quest)
            {
                continue;
            }
            const unsigned long long key =
                (static_cast<unsigned long long>(objective->quest->refID) << 32)
                | static_cast<unsigned long long>(objective->objectiveId);
            const UInt32 status = objective->status;
            const auto known = g_eventLog.questObjectiveStatus.find(key);
            const bool isNew = known == g_eventLog.questObjectiveStatus.end();
            const UInt32 previous = isNew ? 0 : known->second;
            g_eventLog.questObjectiveStatus[key] = status;
            if (priming || !(status & BGSQuestObjective::eQObjStatus_displayed))
            {
                continue;
            }
            const std::string questName = EventDisplayName(objective->quest);
            const char* text = objective->displayText.CStr();
            const std::string objectiveText = text ? text : "";
            if (questName.empty() && objectiveText.empty())
            {
                continue;
            }
            const std::string label = questName.empty()
                ? objectiveText
                : (objectiveText.empty() ? questName : (questName + ": " + objectiveText));
            if (isNew || !(previous & BGSQuestObjective::eQObjStatus_displayed))
            {
                QueueGameEvent("quest", "New objective — " + label);
            }
            else if ((status & BGSQuestObjective::eQObjStatus_completed)
                && !(previous & BGSQuestObjective::eQObjStatus_completed))
            {
                QueueGameEvent("quest", "Objective completed — " + label);
            }
        }
    }

    // --- vanilla dialogue (DialogMenu open/close) -------------------------------
    {
        const bool dialogOpen = IsMenuVisible(kMenuType_Dialog);
        if (dialogOpen && !g_eventLog.dialogMenuWasOpen)
        {
            g_eventLog.dialogMenuOpenTick = now;
            g_eventLog.dialogMenuPartner.clear();
            InterfaceManager* ui = *reinterpret_cast<InterfaceManager**>(kInterfaceManagerSingletonAddress);
            if (ui && ui->crosshairRef)
            {
                g_eventLog.dialogMenuPartner = EventActorName(ui->crosshairRef);
            }
            LogHookFirstFire("dialog_menu");
        }
        else if (!dialogOpen && g_eventLog.dialogMenuWasOpen)
        {
            const std::string partner = g_eventLog.dialogMenuPartner.empty()
                ? std::string("a local")
                : g_eventLog.dialogMenuPartner;
            QueueGameEvent("conversation", "Spoke with " + partner,
                g_eventLog.dialogMenuPartner.empty()
                    ? std::vector<std::pair<std::string, std::string>>{}
                    : std::vector<std::pair<std::string, std::string>>{ { partner, "" } });
        }
        g_eventLog.dialogMenuWasOpen = dialogOpen;
    }

    if (priming)
    {
        g_eventLog.primed = true;
        LogLine("event-log: baselines primed (cell=%s, level=%u, teammates=%zu).",
            g_eventLog.lastCellName.c_str(),
            static_cast<unsigned>(g_eventLog.lastPlayerLevel),
            g_eventLog.knownTeammates.size());
    }

    // --- aggregate window closure + flush ----------------------------------------
    if (g_eventLog.combatActive
        && !player->unk104
        && g_eventLog.combatLastActivityTick
        && (now - g_eventLog.combatLastActivityTick) >= kCombatQuietCloseMs)
    {
        EmitCombatEncounterEvent();
    }
    if (g_eventLog.lootFirstTick
        && g_eventLog.lootLastTick
        && (now - g_eventLog.lootLastTick) >= kLootWindowQuietMs)
    {
        EmitLootWindowEvent();
    }
    if (g_eventLog.dropFirstTick
        && g_eventLog.dropLastTick
        && (now - g_eventLog.dropLastTick) >= kDropWindowQuietMs)
    {
        EmitDropWindowEvent();
    }
    if (g_eventLog.shootFirstTick
        && g_eventLog.shootLastTick
        && (now - g_eventLog.shootLastTick) >= kShootWindowQuietMs)
    {
        EmitShootingEvent();
    }
    FlushGameEvents(false);
}

void OnMainGameLoop()
{
    const bool gameWindowFocused = GameWindowHasFocus();
    const DWORD now = GetTickCount();
    if (!gameWindowFocused)
    {
        if (g_state.gameWindowFocusedLastFrame)
        {
            LogLine("Game window lost focus; clearing bridge hotkey state.");
        }
        g_state.gameWindowFocusedLastFrame = false;
        g_state.ignoreHotkeysUntilTick = now + 500;
        if (g_state.awaitingInput)
        {
            CancelAwaitingTextInput("input_focus_lost", "input_cancelled");
        }
        else if (g_state.bridgeTextInputOwned || IsTextInputMenuActive())
        {
            std::error_code ec;
            fs::remove(UiSubmitPath(), ec);
            ForceCloseTextInputMenu("focus lost stale text input");
            g_state.bridgeTextInputOwned = IsTextInputMenuActive();
            g_state.staleTextInputCloseRetryTick = now + 250;
        }
        PrimeHotkeyEdgeStateFromKeyboard();
        if (g_state.voiceCapture.active)
        {
            AbortVoiceCapture("voice_capture_focus_lost", false);
        }
        return;
    }

    if (!g_state.gameWindowFocusedLastFrame)
    {
        g_state.gameWindowFocusedLastFrame = true;
        g_state.ignoreHotkeysUntilTick = now + 500;
        PrimeHotkeyEdgeStateFromKeyboard();
        if ((g_state.bridgeTextInputOwned || IsTextInputMenuActive()) && !g_state.awaitingInput)
        {
            ForceCloseTextInputMenu("focus regained stale text input");
            g_state.bridgeTextInputOwned = IsTextInputMenuActive();
            g_state.staleTextInputCloseRetryTick = now + 250;
        }
        LogLine("Game window regained focus; bridge hotkeys suppressed briefly.");
    }

    if (g_state.dialogSubtitleActive && g_state.dialogSubtitleHideTick && GetTickCount() >= g_state.dialogSubtitleHideTick)
    {
        ClearDialogSubtitle();
    }

    if (g_state.voiceCapture.active)
    {
        const DWORD now = GetTickCount();
        if (!g_state.voiceCapture.subtitleRefreshTick || now >= g_state.voiceCapture.subtitleRefreshTick)
        {
            g_state.voiceCapture.subtitleRefreshTick = now + 700;
        }
    }

    UpdateActiveSoundPositions();
    CleanupFinishedSounds();
    PollSaveStateSyncAck();

    if (!g_state.loadedIntoGame || !GetPlayer())
    {
        return;
    }

    UpdateVoiceBootstrapStatus();
    LoadDebugConfigIfNeeded(false);
    LoadHotkeysConfigIfNeeded(false);
    PollNativeActionCommands();
    PollCompanionCommands();
    UpdateCompanionFaceDesignSession();
    UpdateCompanionHotkey();
    UpdatePersonaCapture();
    UpdateGameEventLog();
    // --- perf instrumentation (temporary): time the lip-sync + streaming phases to
    // locate the in-game speech lag, logged via "frame_perf_slow" trace events. ---
    static double s_perfAnimMs = 0.0;
    LARGE_INTEGER s_perfFreq;
    QueryPerformanceFrequency(&s_perfFreq);
    LARGE_INTEGER _pa0;
    QueryPerformanceCounter(&_pa0);
    UpdateSpeechAnimation();
    LARGE_INTEGER _pa1;
    QueryPerformanceCounter(&_pa1);
    s_perfAnimMs = static_cast<double>(_pa1.QuadPart - _pa0.QuadPart) * 1000.0
        / static_cast<double>(s_perfFreq.QuadPart);
    UpdateConversationHold();
    PollVoiceCaptureBuffers();
    UpdateVoiceCaptureHotkey();

    if (g_state.awaitingInput)
    {
        ConsumeSubmittedInput();
    }

    if (RecoverStaleTextInputMenu("stale text input"))
    {
        return;
    }

    const bool httpTransport = g_debugConfig.transport == BridgeTransport::Http;
    LARGE_INTEGER _pc0;
    QueryPerformanceCounter(&_pc0);
    if (g_state.awaitingReply
        || (g_debugConfig.drainQueuedChunksAfterFinal
            && (!g_state.pendingAudioChunks.empty() || (!httpTransport && HasPendingChunkFiles()))))
    {
        if (httpTransport)
        {
            // The worker feeds staged chunks + the terminal reply into g_httpInbox.
            // DrainHttpInbox queues the chunks and (when present) runs the reply
            // pipeline; PlayQueuedAudioChunk then pumps the streaming buffer.
            DrainHttpInbox();
        }
        else
        {
            ConsumeAudioChunks();
        }
        PlayQueuedAudioChunk();
    }
    LARGE_INTEGER _pc1;
    QueryPerformanceCounter(&_pc1);
    // Phase 3: drive the single streaming buffer every frame (lip-sync scheduling +
    // end-detection) even after chunk delivery stops, while it plays out.
    UpdateStreamingVoice();
    LARGE_INTEGER _pc2;
    QueryPerformanceCounter(&_pc2);
    // --- perf instrumentation (temporary): log when any speech phase spikes. ---
    {
        const double _chunksMs = static_cast<double>(_pc1.QuadPart - _pc0.QuadPart) * 1000.0
            / static_cast<double>(s_perfFreq.QuadPart);
        const double _streamMs = static_cast<double>(_pc2.QuadPart - _pc1.QuadPart) * 1000.0
            / static_cast<double>(s_perfFreq.QuadPart);
        double _maxMs = s_perfAnimMs;
        if (_chunksMs > _maxMs) _maxMs = _chunksMs;
        if (_streamMs > _maxMs) _maxMs = _streamMs;
        static DWORD s_perfLastLog = 0;
        const DWORD _pnow = GetTickCount();
        if (g_state.streamActive && _maxMs > 6.0 && (_pnow - s_perfLastLog) > 400)
        {
            s_perfLastLog = _pnow;
            TraceRequestEvent(g_state.activeRequestId, "frame_perf_slow", {},
                {
                    { "anim_ms", s_perfAnimMs },
                    { "chunks_ms", _chunksMs },
                    { "stream_ms", _streamMs },
                });
        }
    }

    if (g_state.awaitingReply)
    {
        if (!httpTransport)
        {
            // File transport reads the outbox reply here. In HTTP mode the reply was
            // already processed by DrainHttpInbox above (the worker feeds it in).
            ConsumeReply();
        }
        RecoverStaleReplyState();
    }

    WriteRuntimeHeartbeatIfNeeded(false);

    // Music (task/music): the song queue is polled UNCONDITIONALLY (it's delivered
    // after the turn, so `awaitingReply` is already false), and the active
    // performance's guitar idle is maintained every frame.
    ProcessSongDeliveries();
    UpdateActiveSong();

    if (g_state.ignoreHotkeysUntilTick && GetTickCount() < g_state.ignoreHotkeysUntilTick)
    {
        PrimeHotkeyEdgeStateFromKeyboard();
        return;
    }
    g_state.ignoreHotkeysUntilTick = 0;

    if (g_state.awaitingInput || g_state.voiceCapture.active || IsTextInputMenuActive())
    {
        g_state.keyDownLastFrame = (GetAsyncKeyState(g_hotkeys.chatVk) & 0x8000) != 0;
        g_state.adminKeyDownLastFrame = (GetAsyncKeyState(g_hotkeys.adminChatVk) & 0x8000) != 0;
        return;
    }

    const bool adminKeyDown = (GetAsyncKeyState(g_hotkeys.adminChatVk) & 0x8000) != 0;
    const bool adminPressedNow = adminKeyDown && !g_state.adminKeyDownLastFrame;
    g_state.adminKeyDownLastFrame = adminKeyDown;

    const bool keyDown = (GetAsyncKeyState(g_hotkeys.chatVk) & 0x8000) != 0;
    const bool pressedNow = keyDown && !g_state.keyDownLastFrame;
    g_state.keyDownLastFrame = keyDown;

    if (adminPressedNow || pressedNow)
    {
        ClearIdleOutboxArtifacts("chat_hotkey_idle_stale_response");
        if (HasQueuedOrPlayingReply())
        {
            InterruptBridgeReplyAndPlayback(adminPressedNow ? "admin_chat_hotkey_interrupt" : "chat_hotkey_interrupt");
        }
    }

    if (adminPressedNow)
    {
        if (g_state.saveStateSyncPending)
        {
            if (!g_state.saveStateSyncHudMessageTick || (GetTickCount() - g_state.saveStateSyncHudMessageTick) >= kSaveStateSyncHudCooldownMs)
            {
                ShowHudMessage("Bridge syncing save state. Wait a moment.");
                g_state.saveStateSyncHudMessageTick = GetTickCount();
            }
            return;
        }

        StartAdminChatInput();
        return;
    }

    if (!pressedNow)
    {
        return;
    }

    if (g_state.saveStateSyncPending)
    {
        if (!g_state.saveStateSyncHudMessageTick || (GetTickCount() - g_state.saveStateSyncHudMessageTick) >= kSaveStateSyncHudCooldownMs)
        {
            ShowHudMessage("Bridge syncing save state. Wait a moment.");
            g_state.saveStateSyncHudMessageTick = GetTickCount();
        }
        return;
    }

    if (g_state.awaitingInput)
    {
        return;
    }

    PlayerCharacter* player = GetPlayer();
    const auto target = FindFocusedMappedNpcForChat(player);
    if (target.has_value())
    {
        StartChatWithResolvedTarget(*target);
        return;
    }

    if (FindNearbyMappedNpcsForGroupChat(player, kGroupChatNearbyRadiusMeters).empty())
    {
        ShowHudMessage("No mapped NPC within 10 meters.");
        return;
    }

    StartAmbientChatInput();
}

// ============================================================================
// Companions — chasm-authored followers on the NVCompanions.esp template pool.
// Protocol + design: docs/companions-architecture.md. Everything here logs
// with a "companions:" prefix for in-game diagnosability.
// ============================================================================

fs::path CompanionCommandDir() { return BridgeDir() / "control" / "companions"; }
fs::path CompanionAckDir() { return CompanionCommandDir() / "acks"; }
fs::path CompanionRegistryPath() { return BridgeDir() / "companions" / "registry.txt"; }

std::string HexEncodeBytes(const void* data, size_t length)
{
    static const char* digits = "0123456789abcdef";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i)
    {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0F]);
    }
    return out;
}

bool HexDecodeBytes(const std::string& hex, void* out, size_t length)
{
    if (hex.size() != length * 2)
    {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto* bytes = static_cast<unsigned char*>(out);
    for (size_t i = 0; i < length; ++i)
    {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        bytes[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

// "ModName.esm:LOCALHEX" — load-order independent form reference for the registry.
std::string FormToModLocalRef(TESForm* form)
{
    if (!form)
    {
        return "";
    }
    DataHandler* dataHandler = GetDataHandler();
    const UInt8 modIndex = static_cast<UInt8>(form->refID >> 24);
    if (!dataHandler || modIndex >= dataHandler->modList.loadedModCount)
    {
        return "";
    }
    ModInfo* modInfo = dataHandler->modList.loadedMods[modIndex];
    if (!modInfo || !modInfo->name[0])
    {
        return "";
    }
    return std::string(modInfo->name) + ":" + FormIdHex(form->refID & 0x00FFFFFF);
}

TESForm* ResolveModLocalRefString(const std::string& refString)
{
    const size_t colon = refString.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= refString.size())
    {
        return nullptr;
    }
    const std::string modName = refString.substr(0, colon);
    const UInt32 localId = static_cast<UInt32>(strtoul(refString.c_str() + colon + 1, nullptr, 16));
    return ResolveModLocalForm(modName.c_str(), localId);
}

TESNPC* ResolveCompanionBase(UInt32 slot)
{
    if (slot >= kCompanionPoolSize)
    {
        return nullptr;
    }
    TESForm* form = ResolveModLocalForm(kCompanionsEspName, kCompanionBaseLocalId + slot);
    TESNPC* npc = form ? DYNAMIC_CAST(form, TESForm, TESNPC) : nullptr;
    if (npc)
    {
        g_companions.slotBaseFormIds[slot] = npc->refID;
    }
    return npc;
}

TESObjectREFR* ResolveCompanionRef(UInt32 slot)
{
    if (slot >= kCompanionPoolSize)
    {
        return nullptr;
    }
    TESForm* form = ResolveModLocalForm(kCompanionsEspName, kCompanionRefLocalId + slot);
    return form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr;
}

std::optional<std::pair<std::string, std::string>> ResolveCompanionNpcForRef(TESObjectREFR* ref)
{
    if (!ref || !ref->baseForm || ref->baseForm->typeID != kFormType_TESNPC)
    {
        return std::nullopt;
    }
    const UInt32 baseId = ref->baseForm->refID;
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        CompanionSlot& entry = g_companions.slots[slot];
        if (!entry.claimed)
        {
            continue;
        }
        if (!g_companions.slotBaseFormIds[slot])
        {
            ResolveCompanionBase(slot);
        }
        if (g_companions.slotBaseFormIds[slot] && g_companions.slotBaseFormIds[slot] == baseId)
        {
            return std::make_pair(entry.npcKey, entry.name);
        }
    }
    return std::nullopt;
}

// Summon: Enable (no-op when already enabled) + MoveTo the player. Despawn:
// Disable — the vanilla show/hide-actor pattern; the enable state is ref-level
// and persists in saves. Scripts compile independently so one failing helper
// can't take down the other path.
bool EnsureCompanionSummonScript()
{
    if (g_companionMoveToTargetScript)
    {
        return true;
    }
    if (!g_scriptInterface)
    {
        LogLine("companions: script interface unavailable.");
        return false;
    }
    constexpr char kSummonScript[] = R"(
ref rActor
ref rTarget
float fIssued

Begin Function { rActor, rTarget }
    let fIssued := 0
    if rActor && rTarget
        rActor.Enable
        rActor.MoveTo rTarget 96 96 8
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";
    g_companionMoveToTargetScript = g_scriptInterface->CompileScript(kSummonScript);
    if (!g_companionMoveToTargetScript)
    {
        LogLine("companions: failed to compile summon (Enable+MoveTo) helper.");
    }
    return g_companionMoveToTargetScript != nullptr;
}

bool EnsureCompanionDespawnScript()
{
    if (g_companionMoveToHoldScript)
    {
        return true;
    }
    if (!g_scriptInterface)
    {
        LogLine("companions: script interface unavailable.");
        return false;
    }
    constexpr char kDespawnScript[] = R"(
ref rActor
float fIssued

Begin Function { rActor }
    let fIssued := 0
    if rActor
        rActor.Disable
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";
    g_companionMoveToHoldScript = g_scriptInterface->CompileScript(kDespawnScript);
    if (!g_companionMoveToHoldScript)
    {
        LogLine("companions: failed to compile despawn (Disable) helper.");
    }
    return g_companionMoveToHoldScript != nullptr;
}

// ShowRaceMenu must run as a SCRIPT command (the vanilla chargen quests call
// it from scripts). The generic console helper only prints to the console, so
// routing it there silently does nothing — learned the hard way.
bool CompanionShowRaceMenu(PlayerCharacter* player)
{
    if (!player || !g_scriptInterface)
    {
        return false;
    }
    if (!g_companionShowRaceMenuScript)
    {
        constexpr char kShowRaceMenuScript[] = R"(
Begin Function { }
    ShowRaceMenu
End
)";
        g_companionShowRaceMenuScript = g_scriptInterface->CompileScript(kShowRaceMenuScript);
        if (!g_companionShowRaceMenuScript)
        {
            LogLine("companions: failed to compile ShowRaceMenu helper.");
            return false;
        }
    }
    if (!g_scriptInterface->CallFunctionAlt(g_companionShowRaceMenuScript, player, 0))
    {
        LogLine("companions: CallFunctionAlt failed for ShowRaceMenu helper.");
        return false;
    }
    return true;
}

bool CompanionMoveToRef(TESObjectREFR* actorRef, TESObjectREFR* targetRef)
{
    if (!actorRef || !targetRef || !EnsureCompanionSummonScript())
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_companionMoveToTargetScript, actorRef, nullptr, &result, 2, actorRef, targetRef);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!issued)
    {
        LogLine("companions: MoveTo helper failed for %08X -> %08X (call_ok=%d).", actorRef->refID, targetRef->refID, callOk ? 1 : 0);
    }
    return issued;
}

// ---------------------------------------------------------------------------
// Movement engine: MoveTo a target ref with an explicit X/Y/Z offset (game units).
// The travel engine (chasm) drives an NPC along its route by anchoring on the
// destination MARKER ref (resolved by form id, so this works regardless of where
// the player is — even inside an interior) and offsetting BACK toward the start by
// the remaining fraction. MoveTo is the cross-cell-safe primitive: the engine
// reassigns the exterior cell for the resulting coordinates, so a large offset
// spanning many cells is fine.
Script* g_moveToPosScript = nullptr;

bool EnsureMoveToPosScript()
{
    if (g_moveToPosScript)
    {
        return true;
    }
    if (!g_scriptInterface)
    {
        LogLine("movement: script interface unavailable.");
        return false;
    }
    constexpr char kScript[] = R"(
ref rActor
ref rTarget
float fX
float fY
float fZ
float fIssued

Begin Function { rActor, rTarget, fX, fY, fZ }
    let fIssued := 0
    if rActor && rTarget
        rActor.Enable
        rActor.MoveTo rTarget fX fY fZ
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";
    g_moveToPosScript = g_scriptInterface->CompileScript(kScript);
    if (!g_moveToPosScript)
    {
        LogLine("movement: failed to compile MoveTo-offset helper.");
    }
    return g_moveToPosScript != nullptr;
}

// CallFunction reads each vararg as a 4-byte void* and reinterprets the slot as a
// float for a Float param (`*((float*)&arg)`). A float passed directly would be
// promoted to an 8-byte double and misread, so we pack each float's raw 32 bits
// into the pointer-sized slot.
static void* FloatArg(float value)
{
    UInt32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return reinterpret_cast<void*>(bits);
}

bool MoveRefToOffset(TESObjectREFR* actorRef, TESObjectREFR* targetRef, float dx, float dy, float dz)
{
    if (!actorRef || !targetRef || !EnsureMoveToPosScript())
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(
        g_moveToPosScript, actorRef, nullptr, &result, 5,
        actorRef, targetRef, FloatArg(dx), FloatArg(dy), FloatArg(dz));
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!issued)
    {
        LogLine("movement: MoveTo-offset helper failed for %08X (call_ok=%d).", actorRef->refID, callOk ? 1 : 0);
    }
    return issued;
}

// Resolve a map-marker ref by its runtime form id (worldspace-independent — works
// while the player is in an interior), falling back to a name search. Then place
// `actorRef` at the absolute world position (`x`,`y`,`z`) by offsetting from the
// marker. Returns false if neither the marker nor the actor could be resolved.
bool MoveRefToWorldPos(TESObjectREFR* actorRef, UInt32 markerFormId, const std::string& markerName,
    float x, float y, float z, std::string& outError)
{
    if (!actorRef)
    {
        outError = "actor_unresolved";
        return false;
    }
    TESObjectREFR* marker = nullptr;
    if (markerFormId != 0)
    {
        if (TESForm* form = LookupFormByID(markerFormId))
        {
            // Accept any reference the manifest handed us — a map marker OR a
            // building's front door (both are valid, resolvable destinations).
            marker = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
        }
    }
    if (!marker && !markerName.empty())
    {
        marker = FindMapMarkerByName(markerName);
    }
    if (!marker)
    {
        outError = "marker_unresolved";
        return false;
    }
    const float dx = x - marker->posX;
    const float dy = y - marker->posY;
    const float dz = z - marker->posZ;
    if (!MoveRefToOffset(actorRef, marker, dx, dy, dz))
    {
        outError = "move_failed";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Movement engine Phase 2: REAL walking when the NPC is loaded. JIP LN NVSE's
// TravelToRef drops a temporary travel package on the actor (AddBackUpPackage),
// so it physically walks/paths the navmesh to the target ref with animation — a
// pure-runtime primitive, no esp/package edit. `run`=1 makes them run instead of
// walk. Only meaningful for a loaded (rendered/high-process) actor; the engine
// won't path an unloaded one (that's what the off-screen sim is for).
Script* g_travelToRefScript = nullptr;

bool EnsureTravelToRefScript()
{
    if (g_travelToRefScript)
    {
        return true;
    }
    if (!g_scriptInterface)
    {
        return false;
    }
    constexpr char kScript[] = R"(
ref rActor
ref rTarget
int iRun
float fIssued

Begin Function { rActor, rTarget, iRun }
    let fIssued := 0
    if rActor && rTarget
        rActor.TravelToRef rTarget iRun
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";
    g_travelToRefScript = g_scriptInterface->CompileScript(kScript);
    if (!g_travelToRefScript)
    {
        LogLine("movement: failed to compile TravelToRef helper (is JIP LN NVSE installed?).");
    }
    return g_travelToRefScript != nullptr;
}

bool TravelActorToRef(TESObjectREFR* actorRef, TESObjectREFR* targetRef, bool run)
{
    if (!actorRef || !targetRef || !EnsureTravelToRefScript())
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    // Integer param: CallFunction casts the pointer-sized arg slot straight to
    // SInt32 (unlike float params, which are bit-reinterpreted), so pass the value.
    void* runArg = reinterpret_cast<void*>(static_cast<UInt32>(run ? 1 : 0));
    const bool callOk = g_scriptInterface->CallFunction(
        g_travelToRefScript, actorRef, nullptr, &result, 3, actorRef, targetRef, runArg);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!issued)
    {
        LogLine("movement: TravelToRef helper failed for %08X (call_ok=%d).", actorRef->refID, callOk ? 1 : 0);
    }
    return issued;
}

// An NPC is "loaded" (rendered / high-process, so the engine will actually walk
// it) when it has a 3D render node. Unloaded actors have none — those get the
// off-screen position sim instead.
bool IsActorRenderLoaded(TESObjectREFR* ref)
{
    return ref && ref->GetNiNode() != nullptr;
}

// ---------------------------------------------------------------------------
// Movement engine: travel via a real AI Travel package. Set the NPC's linked ref to
// the destination, then add the "aaChasmTravel" package (location = Near Linked
// Reference, authored in NVCompanions.esp). The engine paths them there — through
// load doors, in and out of buildings — and the added package outranks their editor
// routine so they don't wander home. THIS is the travel primitive: no TravelToRef,
// no MoveTo, no door hacks.
TESForm* ResolveChasmTravelPackage()
{
    TESForm* form = ResolveModLocalForm(kCompanionsPluginName, kChasmTravelPackageLocalFormId);
    if (!form)
    {
        LogLine("movement: aaChasmTravel package not found in %s.", kCompanionsPluginName);
        return nullptr;
    }
    if (!DYNAMIC_CAST(form, TESForm, TESPackage))
    {
        LogLine("movement: aaChasmTravel resolved to %08X but is not a package.", form->refID);
        return nullptr;
    }
    return form;
}

Script* g_setLinkedRefScript = nullptr;
bool EnsureSetLinkedRefScript()
{
    if (g_setLinkedRefScript)
    {
        return true;
    }
    if (!g_scriptInterface)
    {
        return false;
    }
    // JIP LN NVSE's SetLinkedReference — the target the Near-Linked-Reference travel
    // package heads for.
    constexpr char kScript[] = R"(
ref rActor
ref rTarget
float fIssued

Begin Function { rActor, rTarget }
    let fIssued := 0
    if rActor && rTarget
        rActor.SetLinkedReference rTarget
        let fIssued := 1
    endif
    SetFunctionValue fIssued
End
)";
    g_setLinkedRefScript = g_scriptInterface->CompileScript(kScript);
    if (!g_setLinkedRefScript)
    {
        LogLine("movement: failed to compile SetLinkedReference helper (is JIP LN NVSE installed?).");
    }
    return g_setLinkedRefScript != nullptr;
}

bool SetActorLinkedRef(TESObjectREFR* actor, TESObjectREFR* target)
{
    if (!actor || !target || !EnsureSetLinkedRefScript())
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_setLinkedRefScript, actor, nullptr, &result, 2, actor, target);
    return callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
}

// Send `npc` to `target` via the travel package: set the linked ref, add the
// package, evaluate. The engine handles the rest (pathing, doors, routine override).
bool TravelViaPackage(TESObjectREFR* npc, TESObjectREFR* target)
{
    if (!npc || !target)
    {
        return false;
    }
    TESForm* pkg = ResolveChasmTravelPackage();
    if (!pkg)
    {
        return false;
    }
    if (!SetActorLinkedRef(npc, target))
    {
        LogLine("movement: SetLinkedReference failed for %08X.", npc->refID);
        return false;
    }
    return AddActorScriptPackage(npc, pkg, "aaChasmTravel", "movement_travel_package");
}

// Send an NPC out the front door of the building they're in: find the exterior door
// of their current interior cell (from the door-link manifest g_buildingEntrances,
// keyed by the building's display name) and MoveTo it. This is the ONLY thing we do
// for an indoor start — no interior pathing, no AI inside; they "appear at the door"
// and the chasm engine walks/simulates them from there. Returns false when there's
// no mapped door for the cell (then the caller leaves them put).
bool StepOutFrontDoor(TESObjectREFR* actorRef)
{
    if (!actorRef || !actorRef->parentCell || !actorRef->parentCell->IsInterior())
    {
        return false;
    }
    const std::string cellName = ToLowerAscii(InteriorBuildingName(actorRef->parentCell));
    const auto it = g_buildingEntrances.find(cellName);
    if (it == g_buildingEntrances.end() || it->second.formId == 0)
    {
        LogLine("movement: no mapped front door for '%s'; cannot step %08X out.", cellName.c_str(), actorRef->refID);
        return false;
    }
    TESForm* doorForm = LookupFormByID(it->second.formId);
    TESObjectREFR* frontDoor = doorForm ? DYNAMIC_CAST(doorForm, TESForm, TESObjectREFR) : nullptr;
    if (!frontDoor || !MoveRefToOffset(actorRef, frontDoor, 0.0f, 0.0f, 0.0f))
    {
        LogLine("movement: front door for '%s' unresolved; %08X stays put.", cellName.c_str(), actorRef->refID);
        return false;
    }
    LogLine("movement: stepped %08X out the front door of '%s'.", actorRef->refID, cellName.c_str());
    return true;
}

bool CompanionMoveToHold(TESObjectREFR* actorRef)
{
    if (!actorRef || !EnsureCompanionDespawnScript())
    {
        return false;
    }
    NVSEArrayVarInterface::Element result;
    const bool callOk = g_scriptInterface->CallFunction(g_companionMoveToHoldScript, actorRef, nullptr, &result, 1, actorRef);
    const bool issued = callOk
        && result.GetType() == NVSEArrayVarInterface::Element::kType_Numeric
        && result.GetNumber() != 0.0;
    if (!issued)
    {
        LogLine("companions: despawn (Disable) helper failed for %08X (call_ok=%d).", actorRef->refID, callOk ? 1 : 0);
    }
    return issued;
}

void CompanionApplyName(UInt32 slot)
{
    CompanionSlot& entry = g_companions.slots[slot];
    TESNPC* base = ResolveCompanionBase(slot);
    if (!base || entry.name.empty())
    {
        return;
    }
    const std::string engineName = ToUiAscii(entry.name);
    base->fullName.name.Set(engineName.c_str());
    LogLine("companions: slot %u named '%s'.", slot, engineName.c_str());
}

// Read the appearance-relevant fields from a TESNPC into a blob. The facegen
// coefficient buffers are flat float arrays behind FaceGenData::values
// (50/30/50 floats — FGGS/FGGA/FGTS); counts are clamped defensively.
bool CaptureAppearanceFromNpc(TESNPC* npc, CompanionAppearance& out)
{
    if (!npc)
    {
        return false;
    }
    out = CompanionAppearance{};

    struct FgSpec { UInt32 index; float* dest; UInt32 expected; };
    const FgSpec specs[] = {
        { 0, out.fggs, 50 },
        { 1, out.fgga, 30 },
        { 2, out.fgts, 50 },
    };
    for (const FgSpec& spec : specs)
    {
        const auto& fg = npc->faceGenData[spec.index];
        const float* values = reinterpret_cast<const float*>(fg.values);
        if (!values)
        {
            LogLine("companions: facegen[%u] has no values on %08X; capture aborted.", spec.index, npc->refID);
            return false;
        }
        const UInt32 count = fg.count ? fg.count : spec.expected;
        if (count != spec.expected)
        {
            LogLine("companions: facegen[%u] count %u != expected %u on %08X (clamping).", spec.index, count, spec.expected, npc->refID);
        }
        memcpy(spec.dest, values, sizeof(float) * min(count, spec.expected));
    }

    out.raceRef = FormToModLocalRef(npc->race.race);
    out.hairRef = FormToModLocalRef(npc->hair);
    out.eyesRef = FormToModLocalRef(npc->eyes);
    for (auto iter = npc->headPart.Begin(); !iter.End(); ++iter)
    {
        if (*iter)
        {
            out.headPartRefs.push_back(FormToModLocalRef(*iter));
        }
    }
    out.hairColor = npc->hairColor;
    out.hairLength = npc->hairLength;
    out.height = npc->height;
    out.weight = npc->weight;
    out.female = npc->baseData.IsFemale();
    out.valid = true;
    return true;
}

void CompanionSetSex(TESNPC* npc, bool female)
{
    if (!npc || npc->baseData.IsFemale() == female)
    {
        return;
    }
    // Engine change-tracked flag setter on TESActorBaseData (mask 1 = Female).
    ThisStdCall<void>(kActorBaseDataFlagSetterAddress, &npc->baseData, 1u, female ? 1u : 0u, 1u);
    LogLine("companions: set sex female=%d on %08X (now female=%d).", female ? 1 : 0, npc->refID, npc->baseData.IsFemale() ? 1 : 0);
}

// Write a stored appearance blob onto a TESNPC — the manual mirror of
// TESNPC::CopyAppearance (JIP appearance-undo pattern): copy float contents,
// assign shared form pointers, rebuild the owned head-part list.
bool ApplyAppearanceToNpc(TESNPC* npc, const CompanionAppearance& app, bool isPlayerBase)
{
    if (!npc || !app.valid)
    {
        return false;
    }

    CompanionSetSex(npc, app.female);

    TESForm* raceForm = ResolveModLocalRefString(app.raceRef);
    TESRace* race = raceForm ? DYNAMIC_CAST(raceForm, TESForm, TESRace) : nullptr;
    if (race && npc->race.race != race)
    {
        if (isPlayerBase)
        {
            LogLine("companions: player race restore via 0x60B240 (%s).", app.raceRef.c_str());
            ThisStdCall<void>(kPlayerRaceChangeAddress, npc, race, 0);
        }
        else
        {
            npc->race.race = race;
        }
    }
    else if (!race && !app.raceRef.empty())
    {
        LogLine("companions: could not resolve race %s; keeping current.", app.raceRef.c_str());
    }

    struct FgSpec { UInt32 index; const float* src; UInt32 expected; };
    const FgSpec specs[] = {
        { 0, app.fggs, 50 },
        { 1, app.fgga, 30 },
        { 2, app.fgts, 50 },
    };
    for (const FgSpec& spec : specs)
    {
        auto& fg = npc->faceGenData[spec.index];
        float* values = reinterpret_cast<float*>(fg.values);
        if (!values)
        {
            LogLine("companions: facegen[%u] has no values on %08X; apply skipped.", spec.index, npc->refID);
            continue;
        }
        const UInt32 count = fg.count ? fg.count : spec.expected;
        memcpy(values, spec.src, sizeof(float) * min(count, spec.expected));
    }

    if (TESForm* hairForm = ResolveModLocalRefString(app.hairRef))
    {
        if (TESHair* hair = DYNAMIC_CAST(hairForm, TESForm, TESHair))
        {
            npc->hair = hair;
        }
    }
    if (TESForm* eyesForm = ResolveModLocalRefString(app.eyesRef))
    {
        if (TESEyes* eyes = DYNAMIC_CAST(eyesForm, TESForm, TESEyes))
        {
            npc->eyes = eyes;
        }
    }
    if (!app.headPartRefs.empty())
    {
        std::vector<BGSHeadPart*> parts;
        for (const std::string& partRef : app.headPartRefs)
        {
            TESForm* partForm = ResolveModLocalRefString(partRef);
            BGSHeadPart* part = partForm ? DYNAMIC_CAST(partForm, TESForm, BGSHeadPart) : nullptr;
            if (part)
            {
                parts.push_back(part);
            }
            else
            {
                LogLine("companions: could not resolve head part %s.", partRef.c_str());
            }
        }
        if (!parts.empty())
        {
            npc->headPart.RemoveAll();
            for (BGSHeadPart* part : parts)
            {
                npc->headPart.Append(part);
            }
        }
    }
    npc->hairColor = app.hairColor;
    npc->hairLength = app.hairLength;
    npc->height = app.height;
    npc->weight = app.weight;
    return true;
}

// Regenerate a loaded actor's 3D (incl. the facegen head). No-op when the ref
// has no 3D — the engine builds the head from current base data on next load.
void CompanionRefreshActor3D(TESObjectREFR* ref, const char* reason)
{
    if (!ref)
    {
        return;
    }
    if (!ref->GetNiNode())
    {
        LogLine("companions: 3D refresh skipped for %08X (%s): no loaded 3D.", ref->refID, reason);
        return;
    }
    *reinterpret_cast<bool*>(kLoadFaceGenHeadEGTFilesAddress) = true;
    ThisStdCall<void>(kCharacterRebuild3DAddress, ref);
    LogLine("companions: 3D refreshed for %08X (%s).", ref->refID, reason);
}

void SaveCompanionRegistry()
{
    ++g_companions.rev;
    std::ostringstream out;
    out << kCompanionRegistryVersion << "\r\n";
    out << "rev=" << g_companions.rev << "\r\n";
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        const CompanionSlot& entry = g_companions.slots[slot];
        const std::string prefix = "slot" + std::to_string(slot) + ".";
        out << prefix << "claimed=" << (entry.claimed ? 1 : 0) << "\r\n";
        if (!entry.claimed)
        {
            continue;
        }
        out << prefix << "npc_key=" << entry.npcKey << "\r\n";
        out << prefix << "name_base64=" << EncodeBase64(reinterpret_cast<const unsigned char*>(entry.name.c_str()), entry.name.size()) << "\r\n";
        out << prefix << "character_base64=" << EncodeBase64(reinterpret_cast<const unsigned char*>(entry.characterName.c_str()), entry.characterName.size()) << "\r\n";
        out << prefix << "voice=" << entry.voice << "\r\n";
        out << prefix << "female=" << (entry.female ? 1 : 0) << "\r\n";
        // Game-neutral body id for chasm (chasm never interprets the female flag;
        // body ids are declared by the game profile's companions block).
        out << prefix << "body=" << (entry.female ? "female" : "male") << "\r\n";
        out << prefix << "face_designed=" << (entry.faceDesigned ? 1 : 0) << "\r\n";
        out << prefix << "waiting=" << (entry.waiting ? 1 : 0) << "\r\n";
        out << prefix << "status=" << entry.status << "\r\n";
        const CompanionAppearance& app = entry.appearance;
        out << prefix << "app.valid=" << (app.valid ? 1 : 0) << "\r\n";
        if (app.valid)
        {
            out << prefix << "app.female=" << (app.female ? 1 : 0) << "\r\n";
            out << prefix << "app.race=" << app.raceRef << "\r\n";
            out << prefix << "app.hair=" << app.hairRef << "\r\n";
            out << prefix << "app.eyes=" << app.eyesRef << "\r\n";
            std::string headParts;
            for (const std::string& part : app.headPartRefs)
            {
                if (!headParts.empty())
                {
                    headParts += ",";
                }
                headParts += part;
            }
            out << prefix << "app.head_parts=" << headParts << "\r\n";
            out << prefix << "app.hair_color=" << FormIdHex(app.hairColor) << "\r\n";
            out << prefix << "app.hair_length=" << app.hairLength << "\r\n";
            out << prefix << "app.height=" << app.height << "\r\n";
            out << prefix << "app.weight=" << app.weight << "\r\n";
            out << prefix << "app.fggs=" << HexEncodeBytes(app.fggs, sizeof(app.fggs)) << "\r\n";
            out << prefix << "app.fgga=" << HexEncodeBytes(app.fgga, sizeof(app.fgga)) << "\r\n";
            out << prefix << "app.fgts=" << HexEncodeBytes(app.fgts, sizeof(app.fgts)) << "\r\n";
        }
    }

    EnsureBridgeDirectories();
    const fs::path finalPath = CompanionRegistryPath();
    const fs::path tempPath = finalPath.string() + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            LogLine("companions: could not write registry temp file.");
            return;
        }
        file << out.str();
    }
    std::error_code ec;
    fs::rename(tempPath, finalPath, ec);
    if (ec)
    {
        LogLine("companions: registry rename failed: %s", ec.message().c_str());
    }
}

void LoadCompanionRegistry()
{
    g_companions.registryLoaded = true;
    std::ifstream in(CompanionRegistryPath(), std::ios::binary);
    if (!in)
    {
        LogLine("companions: no registry yet (fresh install).");
        return;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty() || Trim(lines[0]) != kCompanionRegistryVersion)
    {
        LogLine("companions: registry has unknown header; ignoring.");
        return;
    }
    const auto fields = ParseKeyValueLines(lines, 1);
    g_companions.rev = static_cast<UInt32>(atoi(GetField(fields, "rev").c_str()));

    auto decodeText = [&fields](const std::string& key) -> std::string {
        const auto decoded = DecodeBase64String(GetField(fields, key.c_str()), 4096);
        return decoded.has_value() ? *decoded : std::string();
    };

    UInt32 claimedCount = 0;
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        const std::string prefix = "slot" + std::to_string(slot) + ".";
        CompanionSlot& entry = g_companions.slots[slot];
        entry = CompanionSlot{};
        if (GetField(fields, (prefix + "claimed").c_str()) != "1")
        {
            continue;
        }
        entry.claimed = true;
        entry.npcKey = GetField(fields, (prefix + "npc_key").c_str());
        entry.name = decodeText(prefix + "name_base64");
        entry.characterName = decodeText(prefix + "character_base64");
        entry.voice = GetField(fields, (prefix + "voice").c_str());
        entry.female = GetField(fields, (prefix + "female").c_str()) == "1";
        entry.faceDesigned = GetField(fields, (prefix + "face_designed").c_str()) == "1";
        entry.waiting = GetField(fields, (prefix + "waiting").c_str()) == "1";
        entry.status = GetField(fields, (prefix + "status").c_str());
        if (entry.status.empty())
        {
            entry.status = "claimed";
        }
        CompanionAppearance& app = entry.appearance;
        if (GetField(fields, (prefix + "app.valid").c_str()) == "1")
        {
            app.female = GetField(fields, (prefix + "app.female").c_str()) == "1";
            app.raceRef = GetField(fields, (prefix + "app.race").c_str());
            app.hairRef = GetField(fields, (prefix + "app.hair").c_str());
            app.eyesRef = GetField(fields, (prefix + "app.eyes").c_str());
            std::istringstream partStream(GetField(fields, (prefix + "app.head_parts").c_str()));
            std::string part;
            while (std::getline(partStream, part, ','))
            {
                part = Trim(part);
                if (!part.empty())
                {
                    app.headPartRefs.push_back(part);
                }
            }
            app.hairColor = static_cast<UInt32>(strtoul(GetField(fields, (prefix + "app.hair_color").c_str()).c_str(), nullptr, 16));
            app.hairLength = static_cast<float>(atof(GetField(fields, (prefix + "app.hair_length").c_str()).c_str()));
            app.height = static_cast<float>(atof(GetField(fields, (prefix + "app.height").c_str()).c_str()));
            app.weight = static_cast<float>(atof(GetField(fields, (prefix + "app.weight").c_str()).c_str()));
            const bool floatsOk =
                HexDecodeBytes(GetField(fields, (prefix + "app.fggs").c_str()), app.fggs, sizeof(app.fggs))
                && HexDecodeBytes(GetField(fields, (prefix + "app.fgga").c_str()), app.fgga, sizeof(app.fgga))
                && HexDecodeBytes(GetField(fields, (prefix + "app.fgts").c_str()), app.fgts, sizeof(app.fgts));
            app.valid = floatsOk;
            if (!floatsOk)
            {
                LogLine("companions: slot %u appearance floats corrupt; face will use template default.", slot);
            }
        }
        ++claimedCount;
    }
    LogLine("companions: registry loaded (rev=%u, %u claimed).", g_companions.rev, claimedCount);
}

void WriteCompanionAck(const std::string& requestId, bool ok, const std::string& error,
    const std::string& op, int slot, const std::string& npcKey)
{
    if (requestId.empty())
    {
        return;
    }
    EnsureBridgeDirectories();
    const fs::path finalPath = CompanionAckDir() / (requestId + ".txt");
    const fs::path tempPath = finalPath.string() + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        file << kCompanionAckVersion << "\r\n";
        file << "request_id=" << requestId << "\r\n";
        file << "ok=" << (ok ? 1 : 0) << "\r\n";
        file << "error=" << error << "\r\n";
        file << "op=" << op << "\r\n";
        file << "slot=" << slot << "\r\n";
        file << "npc_key=" << npcKey << "\r\n";
    }
    std::error_code ec;
    fs::rename(tempPath, finalPath, ec);
    LogLine("companions: ack %s op=%s ok=%d slot=%d %s", requestId.c_str(), op.c_str(), ok ? 1 : 0, slot, error.c_str());
}

// Summon = teleport to the player and recruit. Follow comes from the esp
// package (CTDA GetPlayerTeammate == 1); the teammate flag persists in saves.
bool CompanionSummon(UInt32 slot, std::string& outError)
{
    TESObjectREFR* ref = ResolveCompanionRef(slot);
    PlayerCharacter* player = GetPlayer();
    if (!ref || !player)
    {
        outError = ref ? "no_player" : "companion_ref_unresolved";
        return false;
    }
    if (!CompanionMoveToRef(ref, player))
    {
        outError = "move_failed";
        return false;
    }
    if (!SetActorPlayerTeammate(ref, true, "companion_summon_teammate"))
    {
        outError = "teammate_failed";
        return false;
    }
    EvaluateActorPackage(ref);
    g_companions.slots[slot].status = "spawned";
    RememberNpcTarget(g_companions.slots[slot].npcKey, g_companions.slots[slot].name, CaptureSpeakerSnapshot(ref));
    return true;
}

// Scheduler: move a specific actor ref to a travel destination.
//   * a named place ("prospector saloon") -> the matching map-marker ref, so the
//     actor actually travels THERE (MoveTo teleports across cells).
//   * "player"/"me"/"you"/"here"/empty, or an unresolved name -> the player, the
//     reliable rendezvous fallback that works from anywhere.
// Reuses the proven Enable + MoveTo primitive WITHOUT forcing teammate/follow.
// Shared by both the companion path and the conversing-NPC path below.
bool TravelRefTo(TESObjectREFR* ref, const std::string& destName, std::string& outError)
{
    PlayerCharacter* player = GetPlayer();
    if (!ref || !player)
    {
        outError = ref ? "no_player" : "actor_unresolved";
        return false;
    }
    const std::string destLower = ToLowerAscii(Trim(destName));
    const bool toPlayer = destLower.empty() || destLower == "player" || destLower == "me"
        || destLower == "you" || destLower == "the player" || destLower == "here";
    TESObjectREFR* target = toPlayer ? static_cast<TESObjectREFR*>(player) : FindMapMarkerByName(destName);
    if (!target)
    {
        // A NAMED place that didn't resolve to a map marker. Do NOT yank the actor
        // onto the player — that "teleport to me" is worse than nothing. Fail so the
        // journey is marked failed rather than silently rendezvousing.
        outError = "destination_unresolved";
        LogLine("scheduler: destination '%s' unresolved — not moving (no player fallback).",
            destName.c_str());
        return false;
    }
    if (!CompanionMoveToRef(ref, target))
    {
        outError = "move_failed";
        return false;
    }
    EvaluateActorPackage(ref);
    LogLine("scheduler: %08X travelled to %s (dest='%s').", ref->refID,
        toPlayer ? "the player" : GetMapMarkerDisplayName(target).c_str(),
        destName.c_str());
    return true;
}

// Companion travel: resolve the slot's ref, then move it.
bool CompanionTravelTo(UInt32 slot, const std::string& destName, std::string& outError)
{
    TESObjectREFR* ref = ResolveCompanionRef(slot);
    if (!ref)
    {
        outError = "companion_ref_unresolved";
        LogLine("scheduler: travel_to slot %u ref unresolved.", slot);
        return false;
    }
    if (!TravelRefTo(ref, destName, outError))
    {
        LogLine("scheduler: travel_to slot %u failed (%s).", slot, outError.c_str());
        return false;
    }
    g_companions.slots[slot].status = "spawned";
    RememberNpcTarget(g_companions.slots[slot].npcKey, g_companions.slots[slot].name, CaptureSpeakerSnapshot(ref));
    return true;
}

bool CompanionStopFollowing(UInt32 slot, std::string& outError)
{
    TESObjectREFR* ref = ResolveCompanionRef(slot);
    if (!ref)
    {
        outError = "companion_ref_unresolved";
        return false;
    }
    SetActorPlayerTeammate(ref, false, "companion_dismiss_teammate");
    EvaluateActorPackage(ref);
    return true;
}

bool CompanionDespawn(UInt32 slot, std::string& outError)
{
    TESObjectREFR* ref = ResolveCompanionRef(slot);
    if (!ref)
    {
        outError = "companion_ref_unresolved";
        return false;
    }
    SetActorPlayerTeammate(ref, false, "companion_despawn_teammate");
    if (!CompanionMoveToHold(ref))
    {
        outError = "move_to_hold_failed";
        return false;
    }
    EvaluateActorPackage(ref);
    g_companions.slots[slot].status = "dismissed";
    return true;
}

bool StartCompanionFaceSession(UInt32 slot, const std::string& requestId, const std::string& op, bool spawnAfter, std::string& outError)
{
    if (g_companions.face.phase != 0)
    {
        outError = "face_design_busy";
        return false;
    }
    if (!ResolveCompanionBase(slot))
    {
        outError = "companion_base_unresolved";
        return false;
    }
    g_companions.face = CompanionFaceSession{};
    g_companions.face.phase = 1;
    g_companions.face.slot = slot;
    g_companions.face.requestId = requestId;
    g_companions.face.op = op;
    g_companions.face.spawnAfter = spawnAfter;
    LogLine("companions: face design queued for slot %u (spawn_after=%d) — waiting for game focus.", slot, spawnAfter ? 1 : 0);
    return true;
}

void AbortCompanionFaceSession(const char* reason)
{
    if (g_companions.face.phase == 0)
    {
        return;
    }
    LogLine("companions: face session aborted (%s).", reason);
    WriteCompanionAck(g_companions.face.requestId, false, reason, g_companions.face.op,
        static_cast<int>(g_companions.face.slot), g_companions.slots[g_companions.face.slot].npcKey);
    g_companions.face = CompanionFaceSession{};
}

void FinalizeCompanionFaceSession()
{
    CompanionFaceSession& session = g_companions.face;
    const UInt32 slot = session.slot;
    CompanionSlot& entry = g_companions.slots[slot];

    PlayerCharacter* player = GetPlayer();
    TESNPC* playerBase = (player && player->baseForm && player->baseForm->typeID == kFormType_TESNPC)
        ? static_cast<TESNPC*>(player->baseForm)
        : nullptr;
    TESNPC* slotBase = ResolveCompanionBase(slot);
    if (!playerBase || !slotBase)
    {
        AbortCompanionFaceSession(playerBase ? "companion_base_unresolved" : "player_base_unresolved");
        return;
    }

    // 1) Companion <- player (post-menu face): sex + race first, then the
    //    engine's own appearance copy (facegen arrays, hair, eyes, head parts).
    const bool designedFemale = playerBase->baseData.IsFemale();
    CompanionSetSex(slotBase, designedFemale);
    slotBase->race.race = playerBase->race.race;
    ThisStdCall<void>(kTESNPCCopyAppearanceAddress, slotBase, playerBase);
    LogLine("companions: CopyAppearance player -> slot %u done.", slot);

    entry.female = designedFemale;
    if (!CaptureAppearanceFromNpc(slotBase, entry.appearance))
    {
        LogLine("companions: WARNING — appearance blob capture failed; face will not survive reload.");
    }

    // 2) Player <- pre-menu snapshot (manual field restore + 3D rebuild).
    if (session.playerSnapshot.valid)
    {
        ApplyAppearanceToNpc(playerBase, session.playerSnapshot, true);
        CompanionRefreshActor3D(player, "player_restore");
    }
    else
    {
        LogLine("companions: WARNING — no player snapshot; player keeps designed face!");
    }

    // 3) Refresh the companion if their 3D is loaded (usually not yet spawned).
    CompanionRefreshActor3D(ResolveCompanionRef(slot), "companion_face_applied");

    entry.faceDesigned = true;
    std::string error;
    bool ok = true;
    if (session.spawnAfter)
    {
        ok = CompanionSummon(slot, error);
    }
    SaveCompanionRegistry();
    WriteCompanionAck(session.requestId, ok, error, session.op, static_cast<int>(slot), entry.npcKey);
    ShowHudMessage(entry.name.empty() ? "Companion face saved." : (ToUiAscii(entry.name) + " joined you."));
    g_companions.face = CompanionFaceSession{};
}

void UpdateCompanionFaceDesignSession()
{
    CompanionFaceSession& session = g_companions.face;
    if (session.phase == 0)
    {
        return;
    }
    if (!g_state.loadedIntoGame)
    {
        return;
    }

    if (session.phase == 1)
    {
        if (!GameWindowHasFocus() || IsTextInputMenuActive() || IsMenuVisible(kRaceSexMenuType))
        {
            return;
        }
        PlayerCharacter* player = GetPlayer();
        TESNPC* playerBase = (player && player->baseForm && player->baseForm->typeID == kFormType_TESNPC)
            ? static_cast<TESNPC*>(player->baseForm)
            : nullptr;
        if (!playerBase)
        {
            AbortCompanionFaceSession("player_base_unresolved");
            return;
        }

        // Alt-tabbing back in lands in the auto-pause Start menu (and the user
        // may have others open) — chargen can't open over a menu, and there is
        // no reliable native "close all menus". Wait for gamemode and nudge the
        // user; the pause menu closes the moment they hit Continue.
        UInt32 blockingMenu = 0;
        for (UInt32 menuType = kMenuType_Min; menuType <= kMenuType_Max; ++menuType)
        {
            if (menuType == kMenuType_HUDMain)
            {
                continue;
            }
            if (IsMenuVisible(menuType))
            {
                blockingMenu = menuType;
                break;
            }
        }
        if (blockingMenu)
        {
            const DWORD now = GetTickCount();
            if (now >= session.nextMenuCloseTick)
            {
                session.nextMenuCloseTick = now + 5000;
                LogLine("companions: waiting for menu %u to close before chargen.", blockingMenu);
                ShowHudMessage("Close the menu to design your companion's face.");
            }
            return;
        }

        if (!CaptureAppearanceFromNpc(playerBase, session.playerSnapshot))
        {
            AbortCompanionFaceSession("player_snapshot_failed");
            return;
        }
        ShowHudMessage("Design your companion's face, then confirm to continue.");
        if (!CompanionShowRaceMenu(player))
        {
            AbortCompanionFaceSession("showracemenu_failed");
            return;
        }
        session.phase = 2;
        session.menuOpenDeadlineTick = GetTickCount() + kCompanionMenuOpenTimeoutMs;
        LogLine("companions: ShowRaceMenu issued for slot %u.", session.slot);
        return;
    }

    if (session.phase == 2)
    {
        if (IsMenuVisible(kRaceSexMenuType))
        {
            session.phase = 3;
            LogLine("companions: RaceSexMenu open.");
            return;
        }
        if (GetTickCount() > session.menuOpenDeadlineTick)
        {
            AbortCompanionFaceSession("menu_never_opened");
        }
        return;
    }

    if (session.phase == 3)
    {
        if (IsMenuVisible(kRaceSexMenuType))
        {
            return;
        }
        LogLine("companions: RaceSexMenu closed; finalizing face design.");
        FinalizeCompanionFaceSession();
    }
}

std::string MakeCompanionNpcKey(const std::string& name, UInt32 slot)
{
    std::string key = Slugify(name);
    if (key.empty())
    {
        key = "companion";
    }
    for (UInt32 other = 0; other < kCompanionPoolSize; ++other)
    {
        if (other != slot && g_companions.slots[other].claimed && g_companions.slots[other].npcKey == key)
        {
            key += "_" + std::to_string(slot + 1);
            break;
        }
    }
    return key;
}

void HandleCompanionCommand(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogLine("companions: command file unreadable: %s", path.filename().string().c_str());
        return;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    in.close();
    if (lines.empty() || Trim(lines[0]) != kCompanionCommandVersion)
    {
        LogLine("companions: ignoring command %s (bad header).", path.filename().string().c_str());
        return;
    }
    const auto fields = ParseKeyValueLines(lines, 1);
    std::string requestId = Trim(GetField(fields, "request_id"));
    if (requestId.empty())
    {
        requestId = path.stem().string();
    }
    const std::string op = ToLowerAscii(Trim(GetField(fields, "op")));
    auto decodeText = [&fields](const char* key) -> std::string {
        const auto decoded = DecodeBase64String(GetField(fields, key), 4096);
        return decoded.has_value() ? Trim(*decoded) : std::string();
    };
    LogLine("companions: command %s op=%s", requestId.c_str(), op.c_str());

    if (op == "create")
    {
        // Prefer the game-neutral body id; the legacy female flag is the fallback.
        const std::string body = ToLowerAscii(Trim(GetField(fields, "body")));
        const bool female = body.empty() ? (GetField(fields, "female") == "1") : (body == "female");
        std::string name = decodeText("name_base64");
        if (name.empty())
        {
            name = Trim(GetField(fields, "name"));
        }
        if (name.empty())
        {
            WriteCompanionAck(requestId, false, "missing_name", op, -1, "");
            return;
        }
        // Dedup: a re-create for an already-claimed character reuses its slot
        // (retries otherwise claim one slot per attempt).
        std::string characterName = decodeText("character_base64");
        if (characterName.empty())
        {
            characterName = name;
        }
        int slot = -1;
        for (UInt32 i = 0; i < kCompanionPoolSize; ++i)
        {
            if (g_companions.slots[i].claimed
                && _stricmp(g_companions.slots[i].characterName.c_str(), characterName.c_str()) == 0)
            {
                slot = static_cast<int>(i);
                LogLine("companions: create for '%s' reuses claimed slot %d.", characterName.c_str(), slot);
                break;
            }
        }
        if (slot < 0)
        {
            const UInt32 first = female ? kCompanionFemaleSlotStart : 0;
            const UInt32 last = female ? kCompanionPoolSize : kCompanionFemaleSlotStart;
            for (UInt32 i = first; i < last; ++i)
            {
                if (!g_companions.slots[i].claimed)
                {
                    slot = static_cast<int>(i);
                    break;
                }
            }
        }
        if (slot < 0)
        {
            WriteCompanionAck(requestId, false, "no_free_slot", op, -1, "");
            return;
        }
        if (!ResolveCompanionBase(static_cast<UInt32>(slot)))
        {
            WriteCompanionAck(requestId, false, "esp_missing_or_disabled", op, slot, "");
            return;
        }

        CompanionSlot& entry = g_companions.slots[slot];
        const bool reusedSlot = entry.claimed;
        if (!reusedSlot)
        {
            entry = CompanionSlot{};
            entry.claimed = true;
            entry.female = female;
        }
        entry.name = name;
        entry.characterName = characterName;
        entry.voice = Trim(GetField(fields, "voice"));
        if (entry.status.empty() || entry.status == "unclaimed")
        {
            entry.status = "claimed";
        }
        if (entry.npcKey.empty())
        {
            entry.npcKey = MakeCompanionNpcKey(name, static_cast<UInt32>(slot));
        }
        CompanionApplyName(static_cast<UInt32>(slot));

        const bool wantFaceDesign = GetField(fields, "face_design") == "1";
        std::string error;
        bool ok = true;
        if (wantFaceDesign)
        {
            ok = StartCompanionFaceSession(static_cast<UInt32>(slot), requestId, op, true, error);
            SaveCompanionRegistry();
            if (!ok)
            {
                WriteCompanionAck(requestId, false, error, op, slot, entry.npcKey);
            }
            // success: ack deferred until the face session finishes in-game
            return;
        }
        ok = CompanionSummon(static_cast<UInt32>(slot), error);
        SaveCompanionRegistry();
        WriteCompanionAck(requestId, ok, error, op, slot, entry.npcKey);
        if (ok)
        {
            ShowHudMessage(ToUiAscii(entry.name) + " joined you.");
        }
        return;
    }

    // Scheduler: rendezvous a companion with the player (fired scheduled task).
    // Resolves the slot from `slot=` or, failing that, `npc_key=` (chasm knows the
    // owner's npc_key; the slot is looked up from the registry but npc_key is the
    // durable id). Handled before the generic slot check so npc_key alone works.
    if (op == "travel_to")
    {
        int tslot = -1;
        const std::string slotField = Trim(GetField(fields, "slot"));
        if (!slotField.empty())
        {
            tslot = atoi(slotField.c_str());
        }
        const std::string npcKey = Trim(GetField(fields, "npc_key"));
        const bool slotOk = tslot >= 0 && tslot < static_cast<int>(kCompanionPoolSize)
            && g_companions.slots[tslot].claimed;
        if (!slotOk && !npcKey.empty())
        {
            for (UInt32 i = 0; i < kCompanionPoolSize; ++i)
            {
                if (g_companions.slots[i].claimed && g_companions.slots[i].npcKey == npcKey)
                {
                    tslot = static_cast<int>(i);
                    break;
                }
            }
        }
        const std::string destName = decodeText("dest_name_base64");
        std::string npcName = decodeText("npc_name_base64");
        if (npcName.empty())
        {
            npcName = Trim(GetField(fields, "npc_name"));
        }
        const bool haveCompanion = tslot >= 0 && tslot < static_cast<int>(kCompanionPoolSize)
            && g_companions.slots[tslot].claimed;

        std::string travelError;
        bool travelled = false;
        int ackSlot = tslot;
        std::string ackKey = npcKey;
        std::string displayName = npcName.empty() ? npcKey : npcName;

        if (haveCompanion)
        {
            travelled = CompanionTravelTo(static_cast<UInt32>(tslot), destName, travelError);
            SaveCompanionRegistry();
            ackKey = g_companions.slots[tslot].npcKey;
            displayName = g_companions.slots[tslot].name;
        }
        else
        {
            // Not a companion -> the conversing NPC (e.g. Chet). Resolve their ref
            // by npc_key/name (they just spoke, so it's remembered) and move it.
            ackSlot = -1;
            if (const auto snap = ResolveSpeakerSnapshotForNpc(npcKey, npcName); snap.has_value())
            {
                if (TESObjectREFR* ref = ResolveSpeakerRef(*snap))
                {
                    travelled = TravelRefTo(ref, destName, travelError);
                }
                else
                {
                    travelError = "npc_ref_unresolved";
                }
            }
            else
            {
                travelError = "npc_not_found";
            }
        }

        WriteCompanionAck(requestId, travelled, travelError, op, ackSlot, ackKey);
        if (travelled)
        {
            ShowHudMessage(ToUiAscii(displayName) + (destName.empty()
                ? std::string(" set off to meet you.")
                : (" travelled to " + ToUiAscii(destName) + ".")));
        }
        else
        {
            LogLine("scheduler: travel_to failed for npc_key='%s' (%s).", npcKey.c_str(), travelError.c_str());
        }
        return;
    }

    // Movement engine: place a travelling NPC at an absolute world position — a
    // waypoint along its route, or the exact arrival point. Anchors on the
    // destination marker (by runtime form id, so it resolves regardless of where
    // the player is) and offsets to the coordinates chasm computed. Resolves the
    // actor exactly like `travel_to` (a claimed companion slot, else the conversing
    // NPC by key/name).
    if (op == "move_to_pos")
    {
        const std::string npcKey = Trim(GetField(fields, "npc_key"));
        std::string npcName = decodeText("npc_name_base64");
        if (npcName.empty())
        {
            npcName = Trim(GetField(fields, "npc_name"));
        }
        const std::string markerName = decodeText("dest_name_base64");
        const UInt32 markerFormId =
            static_cast<UInt32>(strtoul(GetField(fields, "dest_form_id").c_str(), nullptr, 10));
        const bool toPlayer = atoi(GetField(fields, "to_player").c_str()) != 0;
        const float x = static_cast<float>(atof(GetField(fields, "x").c_str()));
        const float y = static_cast<float>(atof(GetField(fields, "y").c_str()));
        const float z = static_cast<float>(atof(GetField(fields, "z").c_str()));

        TESObjectREFR* ref = nullptr;
        for (UInt32 i = 0; i < kCompanionPoolSize; ++i)
        {
            if (g_companions.slots[i].claimed && g_companions.slots[i].npcKey == npcKey)
            {
                ref = ResolveCompanionRef(i);
                break;
            }
        }
        if (!ref)
        {
            if (const auto snap = ResolveSpeakerSnapshotForNpc(npcKey, npcName); snap.has_value())
            {
                ref = ResolveSpeakerRef(*snap);
            }
        }

        std::string moveError;
        bool moved = false;
        if (toPlayer)
        {
            // Off-screen sim toward a MOVING target (the player): anchor the MoveTo on
            // the player ref (worldspace-safe) and place at absolute (x,y,z).
            TESObjectREFR* player = GetPlayer();
            if (!ref) { moveError = "actor_unresolved"; }
            else if (!player) { moveError = "player_unresolved"; }
            else
            {
                moved = MoveRefToOffset(ref, player, x - player->posX, y - player->posY, z - player->posZ);
                if (!moved) { moveError = "move_failed"; }
            }
        }
        else
        {
            moved = MoveRefToWorldPos(ref, markerFormId, markerName, x, y, z, moveError);
        }
        WriteCompanionAck(requestId, moved, moveError, op, -1, npcKey);
        if (!moved)
        {
            LogLine("movement: move_to_pos failed for npc_key='%s' (%s).", npcKey.c_str(), moveError.c_str());
        }
        return;
    }

    // Movement engine: one journey step — (re)apply the travel package that walks the
    // NPC to its target (a place/building/door, the player, or another NPC), and
    // report position + arrival back to chasm. The engine handles pathing and doors.
    if (op == "travel_step")
    {
        const std::string npcKey = Trim(GetField(fields, "npc_key"));
        std::string npcName = decodeText("npc_name_base64");
        if (npcName.empty())
        {
            npcName = Trim(GetField(fields, "npc_name"));
        }
        const std::string markerName = decodeText("dest_name_base64");
        const UInt32 markerFormId =
            static_cast<UInt32>(strtoul(GetField(fields, "dest_form_id").c_str(), nullptr, 10));
        const bool toPlayer = atoi(GetField(fields, "to_player").c_str()) != 0;

        TESObjectREFR* ref = nullptr;
        for (UInt32 i = 0; i < kCompanionPoolSize; ++i)
        {
            if (g_companions.slots[i].claimed && g_companions.slots[i].npcKey == npcKey)
            {
                ref = ResolveCompanionRef(i);
                break;
            }
        }
        if (!ref)
        {
            if (const auto snap = ResolveSpeakerSnapshotForNpc(npcKey, npcName); snap.has_value())
            {
                ref = ResolveSpeakerRef(*snap);
            }
        }
        if (!ref)
        {
            WriteCompanionAck(requestId, false, "actor_unresolved", op, -1, npcKey);
            return;
        }

        // Resolve the TARGET to travel to: the player, a map marker / building door
        // (by form id), or ANOTHER NPC named by dest_name.
        TESObjectREFR* target = nullptr;
        if (toPlayer)
        {
            target = GetPlayer();
        }
        if (!target && markerFormId != 0)
        {
            if (TESForm* form = LookupFormByID(markerFormId))
            {
                target = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
            }
        }
        if (!target && !markerName.empty())
        {
            target = FindMapMarkerByName(markerName); // a place
            if (!target)
            {
                // … else another NPC by that name (travel TO someone).
                if (const auto snap = ResolveSpeakerSnapshotForNpc(markerName, markerName); snap.has_value())
                {
                    target = ResolveSpeakerRef(*snap);
                }
            }
        }

        const ULONGLONG nowMs = GetTickCount64();
        TravelerStatus& status = g_travelers[npcKey];

        // `hold`=1 means "keep them where they are" (chasm's linger-at-destination):
        // re-apply the travel package but DON'T step them out the front door.
        const bool hold = atoi(GetField(fields, "hold").c_str()) != 0;

        // Scope arrival to THIS journey: a new journey_id clears the previous trip's
        // `arrived` (and throttle) so chasm never reads a stale "arrived=true".
        const std::string journeyId = Trim(GetField(fields, "journey_id"));
        if (!journeyId.empty() && journeyId != status.journeyId)
        {
            status.journeyId = journeyId;
            status.arrived = false;
            status.lastWalkIssueMs = 0;
        }

        std::string stepError;
        bool ok = false;
        if (!target)
        {
            stepError = "destination_unresolved";
        }
        else
        {
            TESObjectREFR* player = GetPlayer();
            const bool npcInterior = ref->parentCell && ref->parentCell->IsInterior();
            const bool playerSameCell = player && ref->parentCell == player->parentCell;
            if (!hold && npcInterior && !playerSameCell)
            {
                // Indoors and unwatched: don't path them around inside — just step them
                // out the front door. THROTTLE the physical step-out (~6s): a vendor's
                // sandbox that keeps pulling them back in would otherwise cause visible
                // in/out spam every tick. Between step-outs we still re-assert the travel
                // package so it can win and carry them off to the destination.
                const bool stepDue = status.lastWalkIssueMs == 0 || nowMs - status.lastWalkIssueMs > 6000;
                if (stepDue)
                {
                    StepOutFrontDoor(ref); // logs its own success/failure
                    // The moment they're outside, apply the AlwaysRun travel package so
                    // their AI becomes "go to the destination", not "go back to my store".
                    ok = TravelViaPackage(ref, target);
                    status.lastWalkIssueMs = nowMs;
                    stepError = ok ? "" : "travel_failed";
                }
                else
                {
                    ok = true; // recently stepped out — give the package time to take hold
                }
            }
            else if (status.lastWalkIssueMs == 0 || nowMs - status.lastWalkIssueMs > 10000)
            {
                // Loaded, or indoors WITH the player watching: the engine walks them for
                // real (to the door, through it, over to the target). Throttled so the
                // path stays stable rather than re-planning every tick.
                ok = TravelViaPackage(ref, target);
                if (ok) { status.lastWalkIssueMs = nowMs; }
                else { stepError = "travel_failed"; }
            }
            else
            {
                ok = true; // package already applied, still travelling
            }
        }

        // Arrival, LATCHED for this journey: once they reach the destination it STAYS
        // arrived even if their AI bolts them back out a frame later (a brief in-and-out
        // still counts) — and the slower-sampling backend can't miss it. We use a
        // generous approach radius (not a tight 6 m) so a travel_step lands while they're
        // still walking up, plus a building-name match for "walked inside the saloon".
        // (journey_id reset above clears the latch for a fresh trip.)
        const bool npcInteriorNow = ref->parentCell && ref->parentCell->IsInterior();
        bool reachedNow = false;
        if (target)
        {
            const double r = static_cast<double>(12.0f * kGameUnitsPerMeter);
            reachedNow = DistanceSquared3D(ref, target) < (r * r);
        }
        if (!reachedNow && npcInteriorNow && !toPlayer)
        {
            std::string b = ToLowerAscii(InteriorBuildingName(ref->parentCell));
            std::string d = ToLowerAscii(markerName);
            for (const std::string& pfx : { std::string("the "), std::string("inside "), std::string("outside ") })
            {
                if (d.rfind(pfx, 0) == 0) { d = d.substr(pfx.size()); break; }
            }
            if (!b.empty() && !d.empty() && (b.find(d) != std::string::npos || d.find(b) != std::string::npos))
            {
                reachedNow = true;
            }
        }
        if (reachedNow)
        {
            status.arrived = true; // latch
        }
        status.loaded = IsActorRenderLoaded(ref);
        status.interior = npcInteriorNow;
        status.building = npcInteriorNow ? InteriorBuildingName(ref->parentCell) : std::string();
        status.x = ref->posX;
        status.y = ref->posY;
        status.z = ref->posZ;
        status.updatedMs = nowMs;

        WriteCompanionAck(requestId, ok, stepError, op, -1, npcKey);
        if (!ok)
        {
            LogLine("movement: travel_step failed for npc_key='%s' (%s).", npcKey.c_str(), stepError.c_str());
        }
        return;
    }

    // Remaining ops address an existing slot.
    const int slot = atoi(GetField(fields, "slot").c_str());
    if (slot < 0 || slot >= static_cast<int>(kCompanionPoolSize) || !g_companions.slots[slot].claimed)
    {
        WriteCompanionAck(requestId, false, "slot_not_claimed", op, slot, "");
        return;
    }
    CompanionSlot& entry = g_companions.slots[slot];
    std::string error;
    bool ok = false;

    if (op == "face_design")
    {
        const bool spawnAfter = entry.status != "spawned";
        ok = StartCompanionFaceSession(static_cast<UInt32>(slot), requestId, op, spawnAfter, error);
        if (!ok)
        {
            WriteCompanionAck(requestId, false, error, op, slot, entry.npcKey);
        }
        return; // ack deferred on success
    }
    if (op == "rename")
    {
        const std::string name = decodeText("name_base64");
        if (name.empty())
        {
            WriteCompanionAck(requestId, false, "missing_name", op, slot, entry.npcKey);
            return;
        }
        entry.name = name;
        // The display name IS the card binding (chasm resolves the character
        // card by npc_name), so the character mapping follows the rename.
        entry.characterName = name;
        CompanionApplyName(static_cast<UInt32>(slot));
        SaveCompanionRegistry();
        WriteCompanionAck(requestId, true, "", op, slot, entry.npcKey);
        return;
    }
    if (op == "summon")
    {
        ok = CompanionSummon(static_cast<UInt32>(slot), error);
    }
    else if (op == "dismiss")
    {
        ok = CompanionStopFollowing(static_cast<UInt32>(slot), error);
    }
    else if (op == "despawn")
    {
        ok = CompanionDespawn(static_cast<UInt32>(slot), error);
    }
    else if (op == "release")
    {
        std::string despawnError;
        CompanionDespawn(static_cast<UInt32>(slot), despawnError); // best effort
        const std::string npcKey = entry.npcKey;
        entry = CompanionSlot{};
        SaveCompanionRegistry();
        WriteCompanionAck(requestId, true, "", op, slot, npcKey);
        return;
    }
    else
    {
        error = "unknown_op";
    }
    SaveCompanionRegistry();
    WriteCompanionAck(requestId, ok, error, op, slot, entry.npcKey);
}

void PollCompanionCommands(bool force)
{
    if (!g_state.loadedIntoGame || !g_companions.registryLoaded)
    {
        return; // leave command files queued until a game session is live
    }
    const DWORD now = GetTickCount();
    if (!force && now < g_companions.nextPollTick)
    {
        return;
    }
    g_companions.nextPollTick = now + kCompanionPollIntervalMs;

    const fs::path directory = CompanionCommandDir();
    std::error_code iterEc;
    if (!fs::exists(directory, iterEc))
    {
        return;
    }
    for (const auto& dirEntry : fs::directory_iterator(directory, iterEc))
    {
        if (iterEc)
        {
            break;
        }
        std::error_code fileEc;
        if (!dirEntry.is_regular_file(fileEc))
        {
            continue;
        }
        if (ToLowerAscii(dirEntry.path().extension().string()) != ".txt")
        {
            continue;
        }
        HandleCompanionCommand(dirEntry.path());
        std::error_code removeEc;
        fs::remove(dirEntry.path(), removeEc);
        if (removeEc)
        {
            LogLine("companions: failed to remove command %s: %s", dirEntry.path().filename().string().c_str(), removeEc.message().c_str());
        }
    }
}

// F7: the manual companion trigger. Processes any queued companion commands
// immediately, else starts face design / summons the first claimed-but-idle
// slot, else tells the user nothing is pending. Always gives HUD feedback so
// a press is never silent.
void UpdateCompanionHotkey()
{
    const bool keyDown = (GetAsyncKeyState(kCompanionHotkeyVirtualKey) & 0x8000) != 0;
    const bool pressedNow = keyDown && !g_companions.hotkeyDownLastFrame;
    g_companions.hotkeyDownLastFrame = keyDown;
    if (!pressedNow || !g_state.loadedIntoGame || !GameWindowHasFocus() || IsTextInputMenuActive())
    {
        return;
    }

    LogLine("companions: hotkey pressed.");
    if (g_companions.face.phase != 0)
    {
        ShowHudMessage("Companion face design already in progress.");
        return;
    }

    // 1) Anything queued from chasm? Process it right now.
    bool hadQueued = false;
    std::error_code iterEc;
    for (const auto& dirEntry : fs::directory_iterator(CompanionCommandDir(), iterEc))
    {
        std::error_code fileEc;
        if (!iterEc && dirEntry.is_regular_file(fileEc)
            && ToLowerAscii(dirEntry.path().extension().string()) == ".txt")
        {
            hadQueued = true;
            break;
        }
    }
    if (hadQueued)
    {
        ShowHudMessage("Processing pending companion request...");
        PollCompanionCommands(true);
        return;
    }

    // 2) A claimed slot that never made it in-world? Resume it.
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        CompanionSlot& entry = g_companions.slots[slot];
        if (!entry.claimed || entry.status == "spawned")
        {
            continue;
        }
        std::string error;
        if (!entry.faceDesigned)
        {
            const std::string requestId = "hotkey_face_slot" + std::to_string(slot);
            if (StartCompanionFaceSession(slot, requestId, "face_design", true, error))
            {
                ShowHudMessage("Opening the character creator for " + ToUiAscii(entry.name) + "...");
            }
            else
            {
                ShowHudMessage("Companion face design could not start: " + error);
            }
            return;
        }
        if (CompanionSummon(slot, error))
        {
            SaveCompanionRegistry();
            ShowHudMessage(ToUiAscii(entry.name) + " joined you.");
        }
        else
        {
            ShowHudMessage("Companion summon failed: " + error);
            LogLine("companions: hotkey summon failed for slot %u: %s", slot, error.c_str());
        }
        return;
    }

    ShowHudMessage("No companion pending. Create one on chasm's Companions page.");
}

void CompanionsLogEngineProbes()
{
    if (g_companions.engineProbesLogged)
    {
        return;
    }
    g_companions.engineProbesLogged = true;
    const struct { const char* label; UInt32 address; } probes[] = {
        { "CopyAppearance", kTESNPCCopyAppearanceAddress },
        { "FlagSetter", kActorBaseDataFlagSetterAddress },
        { "Rebuild3D", kCharacterRebuild3DAddress },
        { "PlayerRaceChange", kPlayerRaceChangeAddress },
    };
    for (const auto& probe : probes)
    {
        unsigned char bytes[8]{};
        memcpy(bytes, reinterpret_cast<const void*>(probe.address), sizeof(bytes));
        LogLine("companions: engine probe %s@%08X = %s", probe.label, probe.address, HexEncodeBytes(bytes, sizeof(bytes)).c_str());
    }
    LogLine("companions: bLoadFaceGenHeadEGTFiles(cached)@%08X was %d; forcing 1 for runtime facegen tints.",
        kLoadFaceGenHeadEGTFilesAddress, *reinterpret_cast<bool*>(kLoadFaceGenHeadEGTFilesAddress) ? 1 : 0);
}

void CompanionsOnDeferredInit()
{
    CompanionsLogEngineProbes();
    // Runtime facegen tinting (the ini-less bLoadFaceGenHeadEGTFiles=1): copied
    // faces render with correct skin tints without shipping pregen assets.
    *reinterpret_cast<bool*>(kLoadFaceGenHeadEGTFilesAddress) = true;
    if (!g_companions.registryLoaded)
    {
        LoadCompanionRegistry();
    }
}

// Re-apply session-only base-form state (name, face) after every load/new-game;
// ref-level state (position, teammate flag, inventory) persists in the save.
void CompanionsOnSessionReady(const char* reason)
{
    if (!g_companions.registryLoaded)
    {
        LoadCompanionRegistry();
    }
    if (g_companions.face.phase > 1)
    {
        AbortCompanionFaceSession("session_interrupted_by_load");
    }
    *reinterpret_cast<bool*>(kLoadFaceGenHeadEGTFilesAddress) = true;
    UInt32 applied = 0;
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        CompanionSlot& entry = g_companions.slots[slot];
        if (!entry.claimed)
        {
            continue;
        }
        TESNPC* base = ResolveCompanionBase(slot);
        if (!base)
        {
            LogLine("companions: slot %u base unresolved on %s (esp missing?).", slot, reason);
            continue;
        }
        CompanionApplyName(slot);
        if (entry.appearance.valid)
        {
            ApplyAppearanceToNpc(base, entry.appearance, false);
            CompanionRefreshActor3D(ResolveCompanionRef(slot), "session_ready_reapply");
        }
        ++applied;
    }
    if (applied)
    {
        LogLine("companions: re-applied %u companion(s) on %s.", applied, reason);
        // Rewrite the registry so on-disk format upgrades (new fields) propagate
        // without waiting for the next state change.
        SaveCompanionRegistry();
    }

    // Normalize UNCLAIMED slots: a save from before a `release` may still have
    // the template following (the teammate flag lives in the save). Clear it
    // and send the ref home so freed slots never trail the player.
    for (UInt32 slot = 0; slot < kCompanionPoolSize; ++slot)
    {
        if (g_companions.slots[slot].claimed)
        {
            continue;
        }
        // Only bother when the esp is present (base resolves) — otherwise the
        // pool doesn't exist in this load order at all.
        if (!ResolveCompanionBase(slot))
        {
            continue;
        }
        TESObjectREFR* ref = ResolveCompanionRef(slot);
        if (!ref)
        {
            continue;
        }
        SetActorPlayerTeammate(ref, false, "companion_unclaimed_normalize");
        CompanionMoveToHold(ref);
    }
}

void HandleNvseMessage(NVSEMessagingInterface::Message* msg)
{
    switch (msg->type)
    {
    case NVSEMessagingInterface::kMessage_DeferredInit:
    case NVSEMessagingInterface::kMessage_PostLoad:
        EnsureBridgeDirectories();
        MaybeRequestBridgeStackStartup("plugin_init");
        CompanionsOnDeferredInit();
        RegisterGameEventHandlers();
        RegisterCombatEventHandlers();
        LogLine("FNV bridge native plugin initialized.");
        break;

    case NVSEMessagingInterface::kMessage_SaveGame:
    {
        const std::string savePath = msg && msg->data ? reinterpret_cast<const char*>(msg->data) : "";
        if (!savePath.empty())
        {
            // Flush pending gameplay events FIRST so they land in the same
            // bridge poll as (and are ingested before) the save checkpoint —
            // they belong to the timeline being saved.
            FlushGameEvents(true);
            DispatchSaveStateEvent("save", savePath, false);
            // Persona: a save is THE capture trigger (docs/persona.md). Marks a
            // pending capture; the main loop takes it when the idle gates allow.
            RequestPersonaCaptureForSave(savePath);
        }
        break;
    }

    case NVSEMessagingInterface::kMessage_NewGame:
        g_state.loadedIntoGame = true;
        ResetRuntimeState();
        ResetGameEventRuntime("new_game");
        EnsureBridgeDirectories();
        MaybeRequestBridgeStackStartup("new_game");
        g_state.pendingLoadSavePath.clear();
        DispatchSaveStateEvent("new_game", "", true);
        CompanionsOnSessionReady("new_game");
        LogLine("Game session ready.");
        break;

    case NVSEMessagingInterface::kMessage_ExitToMainMenu:
        g_state.loadedIntoGame = false;
        ResetRuntimeState();
        ResetGameEventRuntime("exit_to_main_menu");
        g_state.pendingLoadSavePath.clear();
        LogLine("Game session reset.");
        break;

    case NVSEMessagingInterface::kMessage_ExitGame:
        g_state.loadedIntoGame = false;
        ResetRuntimeState();
        g_state.pendingLoadSavePath.clear();
        // Process is exiting: stop the HTTP worker thread cleanly.
        ShutdownHttpWorker();
        LogLine("Game session reset.");
        break;

    case NVSEMessagingInterface::kMessage_PreLoadGame:
        g_state.loadedIntoGame = false;
        ResetRuntimeState();
        // Un-flushed events belong to the timeline being abandoned — drop them
        // so they can never leak into the restored (post-rollback) log.
        ResetGameEventRuntime("pre_load_game");
        g_state.pendingLoadSavePath = msg && msg->data ? reinterpret_cast<const char*>(msg->data) : "";
        LogLine("Preparing load for %s.", g_state.pendingLoadSavePath.c_str());
        break;

    case NVSEMessagingInterface::kMessage_PostLoadGame:
    {
        const bool loadSucceeded = msg && msg->data != nullptr;
        if (!loadSucceeded)
        {
            g_state.loadedIntoGame = false;
            ResetRuntimeState();
            g_state.pendingLoadSavePath.clear();
            LogLine("Game load failed.");
            break;
        }

        g_state.loadedIntoGame = true;
        ResetRuntimeState();
        ResetGameEventRuntime("post_load_game");
        EnsureBridgeDirectories();
        MaybeRequestBridgeStackStartup("post_load_game");
        std::string savePath = g_state.pendingLoadSavePath;
        if (savePath.empty() && g_serialization && g_serialization->GetSavePath)
        {
            const char* currentSavePath = g_serialization->GetSavePath();
            if (currentSavePath)
            {
                savePath = currentSavePath;
            }
        }
        g_state.pendingLoadSavePath.clear();
        DispatchSaveStateEvent("load", savePath, true);
        CompanionsOnSessionReady("post_load_game");
        LogLine("Game session ready.");
        break;
    }

    case NVSEMessagingInterface::kMessage_MainGameLoop:
        OnMainGameLoop();
        break;

    default:
        break;
    }
}

// Copy the mod-shipped chasm profile bundle out of MO2's virtual filesystem and
// into the shared bridge folder, where the separate chasm process can read it.
//   Source: <RuntimeDir>\Data\chasm-profile   (MO2 overlays a mod's CONTENTS into
//           Data, so the bundle lands here beside Data\nvse; resolves via MO2 VFS)
//   Dest:   <bridge_root>\chasm-profile
// Runs once per process. Non-fatal: any failure is logged and never blocks init.
void StageProfileBundle()
{
    static bool s_staged = false;
    if (s_staged)
    {
        return;
    }
    s_staged = true;

    std::error_code ec;

    // MO2 overlays a mod's contents into Data, so the shipped bundle is at
    // Data\chasm-profile (sibling of Data\nvse). Fall back to a nested
    // Data\NVBridge\chasm-profile in case of an alternate install layout.
    fs::path source = DataDir() / "chasm-profile";
    if (!fs::exists(source, ec) || !fs::is_directory(source, ec))
    {
        source = DataDir() / "NVBridge" / "chasm-profile";
    }
    if (!fs::exists(source, ec) || !fs::is_directory(source, ec))
    {
        LogLine("No chasm profile bundle at %s; skipping staging.", source.string().c_str());
        return;
    }

    const fs::path dest = BridgeDir() / "chasm-profile";

    // Optimization: skip the (~12MB) recursive copy when the staged bundle already
    // matches the shipped one. We compare the bundleVersion in the first profile.json
    // under each side; if that comparison is uncertain for ANY reason, we fall back
    // to copying rather than risk shipping a stale bundle.
    auto readFirstBundleVersion = [&](const fs::path& root) -> std::string {
        std::error_code lec;
        for (fs::directory_iterator it(root, lec), end; !lec && it != end; it.increment(lec))
        {
            if (!it->is_directory(lec))
            {
                continue;
            }
            const fs::path profileJson = it->path() / "profile.json";
            if (!fs::exists(profileJson, lec))
            {
                continue;
            }
            std::ifstream in(profileJson, std::ios::binary);
            if (!in)
            {
                continue;
            }
            const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string version;
            if (JsonGetString(body, "bundleVersion", version) && !version.empty())
            {
                return version;
            }
            double num = 0.0;
            if (JsonGetNumber(body, "bundleVersion", num))
            {
                std::ostringstream os;
                os << num;
                return os.str();
            }
        }
        return "";
    };

    bool shouldCopy = true;
    if (fs::exists(dest, ec) && fs::is_directory(dest, ec))
    {
        const std::string srcVersion = readFirstBundleVersion(source);
        const std::string dstVersion = readFirstBundleVersion(dest);
        if (!srcVersion.empty() && !dstVersion.empty() && srcVersion == dstVersion)
        {
            shouldCopy = false;
            LogLine("Profile bundle already staged at %s (bundleVersion %s); skipping copy.",
                dest.string().c_str(), dstVersion.c_str());
        }
    }

    if (!shouldCopy)
    {
        return;
    }

    try
    {
        fs::create_directories(dest);
        fs::copy(source, dest,
            fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        LogLine("Staged profile bundle to %s.", dest.string().c_str());
    }
    catch (const std::exception& e)
    {
        LogLine("Failed to stage profile bundle to %s: %s", dest.string().c_str(), e.what());
    }
}
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = kPluginName;
    info->version = kPluginVersion;

    if (nvse->nvseVersion < kMinimumNvseVersion)
    {
        return false;
    }

    if (nvse->isEditor)
    {
        return false;
    }

    if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525 || nvse->isNogore)
    {
        return false;
    }

    return true;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface* nvse)
{
    g_nvse = nvse;
    g_pluginHandle = nvse->GetPluginHandle();
    g_serialization = static_cast<NVSESerializationInterface*>(nvse->QueryInterface(kInterface_Serialization));
    g_messaging = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
    g_scriptInterface = static_cast<NVSEScriptInterface*>(nvse->QueryInterface(kInterface_Script));
    g_eventManager = static_cast<NVSEEventManagerInterface*>(nvse->QueryInterface(kInterface_EventManager));
    if (!g_messaging || !g_scriptInterface)
    {
        return false;
    }

    g_messaging->RegisterListener(g_pluginHandle, "NVSE", HandleNvseMessage);
    EnsureBridgeDirectories();
    StageProfileBundle();
    EnsureInputCallbackScript();
    EnsureDialoguePlaybackScripts();
    LogLine("FNV bridge native plugin loaded.");
    return true;
}
