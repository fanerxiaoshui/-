// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "GameActionAnimationTrack.generated.h"

/**
 * 
 */

class UAnimMontage;

UENUM()
enum class EGameActionAnimationTearDownStrategy
{
	Stop UMETA(DisplayName = "停止"),
	PlayToEnd UMETA(DisplayName = "播放至结束")
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionAnimationParams
{
	GENERATED_BODY()
public:
	FGameActionAnimationParams();
	
	/** Gets the animation duration, modified by play rate */
	float GetDuration() const;

	/** Gets the animation sequence length, not modified by play rate */
	float GetSequenceLength() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, meta=(AllowedClasses = "AnimSequence,AnimComposite,AnimStreamable,AnimMontage"))
	class UAnimSequenceBase* Animation = nullptr;
#endif

	UPROPERTY(VisibleAnywhere)
	UAnimMontage* Montage = nullptr;

	UPROPERTY(EditAnywhere)
	FFrameNumber FirstLoopStartFrameOffset;

	UPROPERTY(EditAnywhere)
	FFrameNumber StartFrameOffset;

	UPROPERTY(EditAnywhere)
	FFrameNumber EndFrameOffset;

	UPROPERTY(EditAnywhere)
	float PlayRate = 1.f;

	UPROPERTY()
	FMovieSceneFloatChannel Weight;

	UPROPERTY()
	FMovieSceneByteChannel TearDownStrategy;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionAnimationSectionTemplateParameters : public FGameActionAnimationParams
{
	GENERATED_BODY()

	FGameActionAnimationSectionTemplateParameters() = default;
	FGameActionAnimationSectionTemplateParameters(const FGameActionAnimationParams& BaseParams, FFrameNumber InSectionStartTime, FFrameNumber InSectionEndTime)
		: FGameActionAnimationParams(BaseParams)
		, SectionStartTime(InSectionStartTime)
		, SectionEndTime(InSectionEndTime)
	{}

	float MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const;

	UPROPERTY()
	FFrameNumber SectionStartTime;

	UPROPERTY()
	FFrameNumber SectionEndTime;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionAnimationSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FGameActionAnimationSectionTemplate() = default;
	FGameActionAnimationSectionTemplate(const class UGameActionAnimationSection& Section);

	UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	UPROPERTY()
	FGameActionAnimationSectionTemplateParameters Params;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionAnimationSection : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	UGameActionAnimationSection();
	
	float MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const;

	TOptional<TRange<FFrameNumber> > GetAutoSizeRange() const override;
	void TrimSection(FQualifiedFrameTime TrimTime, bool bTrimLeft, bool bDeleteKeys) override;
	UMovieSceneSection* SplitSection(FQualifiedFrameTime SplitTime, bool bDeleteKeys) override;
	void GetSnapTimes(TArray<FFrameNumber>& OutSnapTimes, bool bGetSectionBorders) const override;
	TOptional<FFrameTime> GetOffsetTime() const override;
	float GetTotalWeightValue(FFrameTime InTime) const override;
	
	UPROPERTY(EditAnywhere, meta = (ShowOnlyInnerProperties = true))
	FGameActionAnimationParams Params;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionAnimationTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()
public:
	UGameActionAnimationTrack();
	
#if WITH_EDITORONLY_DATA
	FText GetDefaultDisplayName() const override;
#endif

	bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	void RemoveAllAnimationData() override { Sections.Empty(); }
	bool HasSection(const UMovieSceneSection& Section) const override { return Sections.Contains(&Section); }
	void AddSection(UMovieSceneSection& Section) override { Sections.Add(CastChecked<UGameActionAnimationSection>(&Section)); }
	void RemoveSection(UMovieSceneSection& Section) override { Sections.Remove(CastChecked<UGameActionAnimationSection>(&Section)); }
	void RemoveSectionAt(int32 SectionIndex) override { Sections.RemoveAt(SectionIndex); }
	bool IsEmpty() const override { return Sections.Num() == 0; }
	const TArray<UMovieSceneSection*>& GetAllSections() const override { return reinterpret_cast<const TArray<UMovieSceneSection*>&>(Sections); }
	bool SupportsMultipleRows() const override { return true; }
	UMovieSceneSection* CreateNewSection() override;
	FMovieSceneTrackRowSegmentBlenderPtr GetRowSegmentBlender() const override;
	FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

#if WITH_EDITOR
	static UAnimMontage* CreateMontage(UObject* Outer, UAnimSequenceBase* AnimSequence);
	UGameActionAnimationSection* AddNewAnimationOnRow(FFrameNumber KeyTime, class UAnimSequenceBase* AnimSequence, int32 RowIndex);
#endif

	UPROPERTY()
	TArray<UGameActionAnimationSection*> Sections;
};
