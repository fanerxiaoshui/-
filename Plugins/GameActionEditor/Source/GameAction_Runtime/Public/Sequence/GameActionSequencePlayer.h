// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneSpawnRegister.h"
#include "GameActionSequencePlayer.generated.h"

enum class EGameActionPlayerEndAction : uint8;
class UGameActionInstanceBase;
class UGameActionSequenceCustomSpawnerBase;
class UGameActionSequenceSpawnerSettingsBase;
class UGameActionDynamicSpawnSectionBase;
class UGameActionSequence;

/**
 * 
 */

// Hack 运行时用来传参至所需的上下文中
struct GAMEACTION_RUNTIME_API FGameActionPlayerContext
{
	static UGameActionSequenceCustomSpawnerBase* CurrentSpawner;
	static bool bIsInActionTransition;
	static bool bIsPlayFinished;
	static bool bIsPlayAborted;
	static const UGameActionSequenceSpawnerSettingsBase* CurrentSpawnerSettings;
};

class GAMEACTION_RUNTIME_API FGameActionSpawnRegister : public FMovieSceneSpawnRegister
{
public:
	FGameActionSpawnRegister();
protected:
	/** ~ FMovieSceneSpawnRegister interface */
	UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player) override;
	void DestroySpawnedObject(UObject& Object) override;

	TMap<TWeakObjectPtr<AActor>, const UGameActionSequenceSpawnerSettingsBase*> SpawnOwnershipMap;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionSequencePlayer : public UObject, public IMovieScenePlayer, public FGameActionSpawnRegister
{
	GENERATED_BODY()

public:
	UGameActionSequencePlayer();

	void Initialize(UGameActionInstanceBase* InGameAction, UGameActionSequence* InSequence, float InPlayRate, EGameActionPlayerEndAction EndAction);
	void SetFrameRange(int32 NewStartTime, int32 Duration);
	void Play() { PlayInternal(); }
	void Stop() { StopInternal(0); }
	void StopAtCurrentTime() { StopInternal(PlayPosition.GetCurrentPosition()); }
	void JumpToSeconds(float TimeInSeconds) { JumpToFrame(TimeInSeconds * PlayPosition.GetInputRate()); }
	
	bool IsPlaying() const { return Status == EMovieScenePlayerStatus::Playing; }
	bool IsPaused() const { return Status == EMovieScenePlayerStatus::Paused; }
	FQualifiedFrameTime GetCurrentTime() const { return FQualifiedFrameTime(PlayPosition.GetCurrentPosition(), PlayPosition.GetInputRate()); }

	DECLARE_MULTICAST_DELEGATE(FOnGameActionFinished);
	FOnGameActionFinished OnFinished;
protected:
	void PlayInternal();
	void StopInternal(FFrameTime TimeToResetTo);
	void Scrub();
	void Pause();
	void JumpToFrame(FFrameTime NewPosition);
	void PlayToFrame(FFrameTime NewPosition);
	void ScrubToFrame(FFrameTime NewPosition);
	
	void UpdateTimeCursorPosition(FFrameTime NewPosition, EUpdatePositionMethod Method);
	void UpdateMovieSceneInstance(FMovieSceneEvaluationRange InRange, EMovieScenePlayerStatus::Type PlayerStatus, bool bHasJumped = false);
	void UpdateNetworkSyncProperties();
	void ApplyLatentActions();

	bool IsSupportedForNetworking() const override { return true; }
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void PostNetReceive() override;

	FFrameTime GetLastValidTime() const;
	bool ShouldStopOrLoop(FFrameTime NewPosition) const;

	TSharedPtr<FMovieSceneTimeController> TimeController;
	
	UPROPERTY(replicated)
	FFrameNumber StartTime;
	
	UPROPERTY(replicated)
	int32 DurationFrames;
	
	UPROPERTY(transient)
	UGameActionSequence* Sequence;

	UPROPERTY(transient)
	FMovieSceneRootEvaluationTemplateInstance RootTemplateInstance;

	uint32 bIsEvaluating : 1;
	uint32 bPendingOnStartedPlaying : 1;

	struct FLatentAction
	{
		enum class EType : uint8 { Stop, Pause, Update, Play };

		FLatentAction(EType InType, FFrameTime DesiredTime = 0)
			: Type(InType), Position(DesiredTime)
		{}

		FLatentAction(EUpdatePositionMethod InUpdateMethod, FFrameTime DesiredTime)
			: Type(EType::Update), UpdateMethod(InUpdateMethod), Position(DesiredTime)
		{}

		EType                 Type;
		EUpdatePositionMethod UpdateMethod;
		FFrameTime            Position;
	};
	TArray<FLatentAction> LatentActions;
	
	FMovieScenePlaybackPosition PlayPosition;

	UPROPERTY()
	TEnumAsByte<EMovieScenePlayerStatus::Type> Status;

	UPROPERTY(transient)
	int32 CurrentNumLoops;
	
	UPROPERTY(replicated)
	FMovieSceneSequenceReplProperties NetSyncProps;

	TOptional<double> OldMaxTickRate;
	TOptional<float> LastTickGameTimeSeconds;

	EGameActionPlayerEndAction PlayEndAction;
	float PlayRate = 1.f;
	static constexpr bool bReversePlayback = false;
private:
	bool HasAuthority() const;
	bool IsLocalControlled() const;

	FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() override { return RootTemplateInstance; }
	FMovieSceneSpawnRegister& GetSpawnRegister() override { return *this; }
	UObject* AsUObject() override { return this; }
	void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override {}

	void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) override { }
	void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override {}
	EMovieScenePlayerStatus::Type GetPlaybackStatus() const override { return Status; }
public:
	UObject* GetPlaybackContext() const override;
	IMovieScenePlaybackClient* GetPlaybackClient() override;
	bool IsLoopIndefinitely() const;

	UPROPERTY()
	UGameActionInstanceBase* GameAction = nullptr;
	
	// 当帧间隔大于该值时使用该间隔进行多次求解
	// 例如在Act游戏中的攻击判定时设置该值，防止帧间隔过大错过重要判定
	float SubStepDuration = FLT_MAX;
	uint8 bIsInSubStepState : 1;
	
	void Update(const float DeltaSeconds);
protected:
	uint8 bInCameraCutState : 1;
	void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) override;
};
