// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <MovieSceneNameableTrack.h>
#include <MovieSceneSection.h>
#include <Evaluation/MovieSceneEvalTemplate.h>
#include <Channels/MovieSceneChannel.h>
#include <Channels/MovieSceneChannelData.h>
#include <Channels/MovieSceneChannelTraits.h>

#include "Compilation/IMovieSceneTrackTemplateProducer.h"

#include "GameActionEventTrack.generated.h"

class UGameActionKeyEventSection;
class UGameActionKeyEvent;
class UGameActionStateEvent;
class UGameActionStateInnerKeyEvent;

/**
 * 
 */
UCLASS()
class GAMEACTION_RUNTIME_API UGameActionKeyEventTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()
public:
	UGameActionKeyEventTrack(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITORONLY_DATA
	FText GetDefaultDisplayName() const override;
#endif
	
	bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	void RemoveAllAnimationData() override { EventSections.Empty(); }
	bool HasSection(const UMovieSceneSection& Section) const override { return EventSections.Contains(&Section); }
	void AddSection(UMovieSceneSection& Section) override { EventSections.Add(CastChecked<UGameActionKeyEventSection>(&Section)); }
	void RemoveSection(UMovieSceneSection& Section) override { EventSections.Remove(CastChecked<UGameActionKeyEventSection>(&Section)); }
	void RemoveSectionAt(int32 SectionIndex) override { EventSections.RemoveAt(SectionIndex); }
	bool IsEmpty() const override { return EventSections.Num() == 0; }
	const TArray<UMovieSceneSection*>& GetAllSections() const override { return reinterpret_cast<const TArray<UMovieSceneSection*>&>(EventSections); }
	bool SupportsMultipleRows() const override { return true; }
	UMovieSceneSection* CreateNewSection() override;
	FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	void PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const override;
public:
	UPROPERTY()
	TArray<UGameActionKeyEventSection*> EventSections;
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionKeyEventValue
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Instanced)
	UGameActionKeyEvent* KeyEvent;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionKeyEventChannel : public FMovieSceneChannel
{
	GENERATED_BODY()
public:
	FORCEINLINE TMovieSceneChannelData<FGameActionKeyEventValue> GetData() { return TMovieSceneChannelData<FGameActionKeyEventValue>(&Times, &KeyValues, &KeyHandles); }
	FORCEINLINE TMovieSceneChannelData<const FGameActionKeyEventValue> GetData() const { return TMovieSceneChannelData<const FGameActionKeyEventValue>(&Times, &KeyValues); }
	TArrayView<const FFrameNumber> GetKeyTimes() const { return Times; }
	TArrayView<const FGameActionKeyEventValue> GetKeyValues() const { return KeyValues; }
public:
	// ~ FMovieSceneChannel Interface
	void GetKeys(const TRange<FFrameNumber>& WithinRange, TArray<FFrameNumber>* OutKeyTimes, TArray<FKeyHandle>* OutKeyHandles) override { GetData().GetKeys(WithinRange, OutKeyTimes, OutKeyHandles); }
	void GetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<FFrameNumber> OutKeyTimes) override { GetData().GetKeyTimes(InHandles, OutKeyTimes); }
	void SetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<const FFrameNumber> InKeyTimes) override { GetData().SetKeyTimes(InHandles, InKeyTimes); }
	void DuplicateKeys(TArrayView<const FKeyHandle> InHandles, TArrayView<FKeyHandle> OutNewHandles) override { GetData().DuplicateKeys(InHandles, OutNewHandles); }
	void DeleteKeys(TArrayView<const FKeyHandle> InHandles) override { GetData().DeleteKeys(InHandles); }
	void DeleteKeysFrom(FFrameNumber InTime, bool bDeleteKeysBefore) override { GetData().DeleteKeysFrom(InTime, bDeleteKeysBefore); }
	void ChangeFrameResolution(FFrameRate SourceRate, FFrameRate DestinationRate) override { GetData().ChangeFrameResolution(SourceRate, DestinationRate); }
	TRange<FFrameNumber> ComputeEffectiveRange() const override { return GetData().GetTotalRange(); }
	int32 GetNumKeys() const override { return Times.Num(); }
	void Reset() override
	{
		Times.Reset();
		KeyValues.Reset();
		KeyHandles.Reset();
	}
	void Offset(FFrameNumber DeltaPosition) override { GetData().Offset(DeltaPosition); }
	void Optimize(const FKeyDataOptimizationParams& InParameters) override {}
	void ClearDefault() override {}
private:
	UPROPERTY(meta=(KeyTimes))
	TArray<FFrameNumber> Times;

	UPROPERTY(meta=(KeyValues))
	TArray<FGameActionKeyEventValue> KeyValues;

	FMovieSceneKeyHandleMap KeyHandles;
};

template<>
struct TMovieSceneChannelTraits<FGameActionKeyEventChannel> : TMovieSceneChannelTraitsBase<FGameActionKeyEventChannel>
{
	enum { SupportsDefaults = false };
};

inline bool EvaluateChannel(const FGameActionKeyEventChannel* InChannel, FFrameTime InTime, FGameActionKeyEventValue& OutValue)
{
	return false;
}

inline bool ValueExistsAtTime(const FGameActionKeyEventChannel* InChannel, FFrameNumber Time, const FGameActionKeyEventValue& Value)
{
	return InChannel->GetData().FindKey(Time) != INDEX_NONE;
}

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionKeyEventSection : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	UGameActionKeyEventSection();
public:
 	UPROPERTY()
 	FGameActionKeyEventChannel KeyEventChannel;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionKeyEventSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FGameActionKeyEventSectionTemplate(const UGameActionKeyEventSection* Section = nullptr)
		:Section(Section)
	{}

	UPROPERTY()
	const UGameActionKeyEventSection* Section;

private:
	UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	void EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GAMEACTION_RUNTIME_API FGameActionStateEventInnerKeyValue
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Instanced)
	UGameActionStateInnerKeyEvent* KeyEvent;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionStateEventInnerKeyChannel : public FMovieSceneChannel
{
	GENERATED_BODY()
public:
	FORCEINLINE TMovieSceneChannelData<FGameActionStateEventInnerKeyValue> GetData() { return TMovieSceneChannelData<FGameActionStateEventInnerKeyValue>(&Times, &KeyValues, &KeyHandles); }
	FORCEINLINE TMovieSceneChannelData<const FGameActionStateEventInnerKeyValue> GetData() const { return TMovieSceneChannelData<const FGameActionStateEventInnerKeyValue>(&Times, &KeyValues); }
	TArrayView<const FFrameNumber> GetKeyTimes() const { return Times; }
	TArrayView<const FGameActionStateEventInnerKeyValue> GetKeyValues() const { return KeyValues; }
public:
	// ~ FMovieSceneChannel Interface
	void GetKeys(const TRange<FFrameNumber>& WithinRange, TArray<FFrameNumber>* OutKeyTimes, TArray<FKeyHandle>* OutKeyHandles) override { GetData().GetKeys(WithinRange, OutKeyTimes, OutKeyHandles); }
	void GetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<FFrameNumber> OutKeyTimes) override { GetData().GetKeyTimes(InHandles, OutKeyTimes); }
	void SetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<const FFrameNumber> InKeyTimes) override { GetData().SetKeyTimes(InHandles, InKeyTimes); }
	void DuplicateKeys(TArrayView<const FKeyHandle> InHandles, TArrayView<FKeyHandle> OutNewHandles) override { GetData().DuplicateKeys(InHandles, OutNewHandles); }
	void DeleteKeys(TArrayView<const FKeyHandle> InHandles) override { GetData().DeleteKeys(InHandles); }
	void DeleteKeysFrom(FFrameNumber InTime, bool bDeleteKeysBefore) override { GetData().DeleteKeysFrom(InTime, bDeleteKeysBefore); }
	void ChangeFrameResolution(FFrameRate SourceRate, FFrameRate DestinationRate) override { GetData().ChangeFrameResolution(SourceRate, DestinationRate); }
	TRange<FFrameNumber> ComputeEffectiveRange() const override { return GetData().GetTotalRange(); }
	int32 GetNumKeys() const override { return Times.Num(); }
	void Reset() override
	{
		Times.Reset();
		KeyValues.Reset();
		KeyHandles.Reset();
	}
	void Offset(FFrameNumber DeltaPosition) override { GetData().Offset(DeltaPosition); }
	void Optimize(const FKeyDataOptimizationParams& InParameters) override {}
	void ClearDefault() override {}
private:
	UPROPERTY(meta=(KeyTimes))
	TArray<FFrameNumber> Times;

	UPROPERTY(meta=(KeyValues))
	TArray<FGameActionStateEventInnerKeyValue> KeyValues;

	FMovieSceneKeyHandleMap KeyHandles;
};

template<>
struct TMovieSceneChannelTraits<FGameActionStateEventInnerKeyChannel> : TMovieSceneChannelTraitsBase<FGameActionStateEventInnerKeyChannel>
{
	enum { SupportsDefaults = false };
};

inline bool EvaluateChannel(const FGameActionStateEventInnerKeyChannel* InChannel, FFrameTime InTime, FGameActionStateEventInnerKeyValue& OutValue)
{
	return false;
}

inline bool ValueExistsAtTime(const FGameActionStateEventInnerKeyChannel* InChannel, FFrameNumber Time, const FGameActionStateEventInnerKeyValue& Value)
{
	return InChannel->GetData().FindKey(Time) != INDEX_NONE;
}

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionStateEventSection : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	UGameActionStateEventSection();
public:
	UPROPERTY(VisibleAnywhere, Instanced, Category = GameAction)
	UGameActionStateEvent* StateEvent;

	UPROPERTY()
	FGameActionStateEventInnerKeyChannel InnerKeyChannel;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionStateEventTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()
public:
	UGameActionStateEventTrack(const FObjectInitializer& ObjectInitializer);
	
#if WITH_EDITORONLY_DATA
	FText GetDefaultDisplayName() const override;
#endif
	
	bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	void RemoveAllAnimationData() override { EventSections.Empty(); }
	bool HasSection(const UMovieSceneSection& Section) const override { return EventSections.Contains(&Section); }
	void AddSection(UMovieSceneSection& Section) override { EventSections.Add(CastChecked<UGameActionStateEventSection>(&Section)); }
	void RemoveSection(UMovieSceneSection& Section) override { EventSections.Remove(CastChecked<UGameActionStateEventSection>(&Section)); }
	void RemoveSectionAt(int32 SectionIndex) override { EventSections.RemoveAt(SectionIndex); }
	bool IsEmpty() const override { return EventSections.Num() == 0; }
	const TArray<UMovieSceneSection*>& GetAllSections() const override { return reinterpret_cast<const TArray<UMovieSceneSection*>&>(EventSections); }
	bool SupportsMultipleRows() const override { return true; }
	UMovieSceneSection* CreateNewSection() override;
	FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	void PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const override;
public:
	UPROPERTY()
	TArray<UGameActionStateEventSection*> EventSections;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionStateEventSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FGameActionStateEventSectionTemplate(const UGameActionStateEventSection* Section = nullptr)
		:Section(Section)
	{}

private:
	void SetupOverrides() override { EnableOverrides(RequiresInitializeFlag | RequiresTearDownFlag); }
	UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	void Initialize(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	void EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
	void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	
	UPROPERTY()
	const UGameActionStateEventSection* Section;
};
