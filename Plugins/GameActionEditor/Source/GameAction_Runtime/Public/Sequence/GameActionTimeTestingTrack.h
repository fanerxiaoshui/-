// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <MovieSceneTrack.h>
#include <MovieSceneSection.h>
#include <Evaluation/MovieSceneEvalTemplate.h>
#include "GameActionTimeTestingTrack.generated.h"

/**
 * 
 */
UCLASS()
class GAMEACTION_RUNTIME_API UGameActionTimeTestingSection : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	UGameActionTimeTestingSection()
		: bIsEnable(true)
	{}

	UPROPERTY()
	uint8 bIsEnable : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UObject* TimeTestingNode;
	class UBPNode_SequenceTimeTestingNode* GetTimeTestingNode() const { return (UBPNode_SequenceTimeTestingNode*)TimeTestingNode; }
#endif
};

UCLASS()
class GAMEACTION_RUNTIME_API UGameActionTimeTestingTrack : public UMovieSceneTrack
{
	GENERATED_BODY()
public:
	UGameActionTimeTestingTrack(const FObjectInitializer& ObjectInitializer);

	FText GetDisplayName() const;
	bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	void RemoveAllAnimationData() override { EventSections.Empty(); }
	bool HasSection(const UMovieSceneSection& Section) const override { return EventSections.Contains(&Section); }
	void AddSection(UMovieSceneSection& Section) override { EventSections.Add(CastChecked<UGameActionTimeTestingSection>(&Section)); }
	void RemoveSection(UMovieSceneSection& Section) override { EventSections.Remove(CastChecked<UGameActionTimeTestingSection>(&Section)); }
	void RemoveSectionAt(int32 SectionIndex) override { EventSections.RemoveAt(SectionIndex); }
	bool IsEmpty() const override { return EventSections.Num() == 0; }
	const TArray<UMovieSceneSection*>& GetAllSections() const override { return reinterpret_cast<const TArray<UMovieSceneSection*>&>(EventSections); }
	bool SupportsMultipleRows() const override { return true; }
public:
	UPROPERTY()
	TArray<UGameActionTimeTestingSection*> EventSections;
};
