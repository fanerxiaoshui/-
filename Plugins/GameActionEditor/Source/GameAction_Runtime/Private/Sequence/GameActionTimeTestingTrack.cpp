// Fill out your copyright notice in the Description page of Project Settings.


#include "Sequence/GameActionTimeTestingTrack.h"

#define LOCTEXT_NAMESPACE "GameActionTimeTestingTrack"

UGameActionTimeTestingTrack::UGameActionTimeTestingTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

FText UGameActionTimeTestingTrack::GetDisplayName() const
{
	return LOCTEXT("条件跳转检测", "条件跳转检测");
}

bool UGameActionTimeTestingTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UGameActionTimeTestingSection::StaticClass();
}

#undef LOCTEXT_NAMESPACE
