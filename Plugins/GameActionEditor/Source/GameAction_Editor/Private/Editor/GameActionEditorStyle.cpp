// Fill out your copyright notice in the Description page of Project Settings.


#include "Editor/GameActionEditorStyle.h"
#include <Misc/Paths.h>
#include <Styling/SlateStyleRegistry.h>
#include <Modules/ModuleManager.h>

#include "GameAction_Editor.h"

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define TTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".ttf")), __VA_ARGS__)
#define OTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".otf")), __VA_ARGS__)

FGameActionEditorStyle::FGameActionEditorStyle()
	:FSlateStyleSet("GameActionEditorStyle")
{
	const FVector2D Icon20x20(20.f, 20.f);
	const FVector2D Icon40x40(40.f, 40.f);
	SetContentRoot(FPaths::ProjectPluginsDir() / TEXT("GameActionEditor/Resources"));

	Set("GameActionEditor.EnableSimulation", new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_Enable_Simulation_40px.png"), Icon40x40));
	Set("GameActionEditor.EnableSimulation.Small", new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_Enable_Simulation_40px.png"), Icon20x20));
	
	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FGameActionEditorStyle::~FGameActionEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FGameActionEditorStyle& FGameActionEditorStyle::Get()
{
	FGameAction_EditorModule& GameActionEditorModule = FModuleManager::LoadModuleChecked<FGameAction_EditorModule>("GameAction_Editor");
	return GameActionEditorModule.GameActionEditorStyle;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT
