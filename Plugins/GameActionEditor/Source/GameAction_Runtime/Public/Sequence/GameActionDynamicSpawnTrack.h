// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "GameActionDynamicSpawnTrack.generated.h"

class UGameActionSpawnByTemplateSection;
class UGameActionSpawnBySpawnerSection;
class UGameActionSequenceSpawnerSettingsBase;
class UGameActionSequenceCustomSpawnerBase;
class UGameActionSequenceSpawnerSettings;

/**
 * 
 */
UCLASS()
class GAMEACTION_RUNTIME_API UGameActionDynamicSpawnSectionBase : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	UGameActionDynamicSpawnSectionBase();

	bool AsReference() const;
	
	virtual UGameActionSequenceSpawnerSettingsBase* GetSpawnerSettings() const { return nullptr; }
	
	UPROPERTY()
	FMovieSceneBoolChannel SpawnableCurve;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionSpawnByTemplateSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FGameActionSpawnByTemplateSectionTemplate() = default;
	FGameActionSpawnByTemplateSectionTemplate(const UGameActionSpawnByTemplateSection* SpawnSection)
		: Section(SpawnSection)
	{}

	static FMovieSceneAnimTypeID GetAnimTypeID() { return TMovieSceneAnimTypeID<FGameActionSpawnByTemplateSectionTemplate>(); }
private:
	UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	UPROPERTY()
	const UGameActionSpawnByTemplateSection* Section;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionSpawnByTemplateSection : public UGameActionDynamicSpawnSectionBase
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Instanced)
	UGameActionSequenceSpawnerSettings* SpawnerSettings = nullptr;

	UGameActionSequenceSpawnerSettingsBase* GetSpawnerSettings() const override;
};

USTRUCT()
struct GAMEACTION_RUNTIME_API FGameActionSpawnBySpawnerSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FGameActionSpawnBySpawnerSectionTemplate() {}
	FGameActionSpawnBySpawnerSectionTemplate(const UGameActionSpawnBySpawnerSection* SpawnSection)
		: Section(SpawnSection)
	{}

	static FMovieSceneAnimTypeID GetAnimTypeID() { return TMovieSceneAnimTypeID<FGameActionSpawnBySpawnerSectionTemplate>(); }
private:
	UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	UPROPERTY()
	const UGameActionSpawnBySpawnerSection* Section = nullptr;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionSpawnBySpawnerSection : public UGameActionDynamicSpawnSectionBase
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Instanced)
	UGameActionSequenceCustomSpawnerBase* CustomSpawner = nullptr;

	UGameActionSequenceSpawnerSettingsBase* GetSpawnerSettings() const override;
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionDynamicSpawnTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	FText GetDisplayName() const override;
	void SetDisplayName(const FText& NewDisplayName) override;
	bool ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const override;
#endif

	bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override { return SectionClass == UGameActionDynamicSpawnSectionBase::StaticClass(); }
	void RemoveAllAnimationData() override { SpawnSection.Empty(); }
	bool HasSection(const UMovieSceneSection& Section) const override { return SpawnSection.Contains(&Section); }
	void AddSection(UMovieSceneSection& Section) override { SpawnSection.Add(CastChecked<UGameActionDynamicSpawnSectionBase>(&Section)); }
	void RemoveSection(UMovieSceneSection& Section) override { SpawnSection.Remove(CastChecked<UGameActionDynamicSpawnSectionBase>(&Section)); }
	void RemoveSectionAt(int32 SectionIndex) override { SpawnSection.RemoveAt(SectionIndex); }
	bool IsEmpty() const override { return SpawnSection.Num() == 0; }
	const TArray<UMovieSceneSection*>& GetAllSections() const override { return reinterpret_cast<const TArray<UMovieSceneSection*>&>(SpawnSection); }
	bool SupportsMultipleRows() const override { return false; }
	UMovieSceneSection* CreateNewSection() override;
	FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	void PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const override;
public:
	UPROPERTY()
	TArray<UGameActionDynamicSpawnSectionBase*> SpawnSection;

	FMovieSceneSpawnable* GetOwingSpawnable() const;
};
