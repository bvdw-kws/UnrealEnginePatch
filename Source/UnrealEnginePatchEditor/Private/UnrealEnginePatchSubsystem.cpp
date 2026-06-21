// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "UnrealEnginePatchSubsystem.h"
#include "EnginePatchFileLoader.h"
#include "EnginePatchManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IProjectManager.h"
#include "Interfaces/IPluginManager.h"
#include "ProjectDescriptor.h"

// Uses the live FProjectDescriptor (updated immediately when the user toggles a plugin)
// rather than IPlugin::IsEnabled() which reflects the startup state only.
static bool IsPluginEnabledInUproject(const FString& PluginName)
{
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	if (!Project) return true;

	int32 Idx = Project->FindPluginReferenceIndex(PluginName);
	if (Idx == INDEX_NONE)
	{
		// Not listed in uproject — fall back to the plugin's own EnabledByDefault descriptor field
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		return Plugin.IsValid() && Plugin->IsEnabledByDefault(!Project->bDisableEnginePluginsByDefault);
	}
	return Project->Plugins[Idx].bEnabled;
}

void UUnrealEnginePatchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Default discovery: Recall plugin's EnginePatch directory
	FString RecallPatchDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectPluginsDir() / TEXT("Recall/EnginePatch"));
	PatchDirectories.Add(RecallPatchDir);

	for (const FString& Dir : PatchDirectories)
	{
		TArray<FEnginePatch> Loaded = FEnginePatchFileLoader::LoadPatchesFromDirectory(Dir);
		Patches.Append(MoveTemp(Loaded));
	}

	RefreshStatus();
}

void UUnrealEnginePatchSubsystem::Deinitialize()
{
	SyncPatchesOnClose();
	Super::Deinitialize();
}

void UUnrealEnginePatchSubsystem::SyncPatchesOnClose()
{
	// On editor close, sync patches to plugin state:
	// - Apply patches for enabled plugins (so engine source is ready for next compile)
	// - Remove patches for disabled plugins (keep source clean)
	for (FEnginePatch& Patch : Patches)
	{
		const bool bPluginEnabled = Patch.Plugin.IsEmpty() || IsPluginEnabledInUproject(Patch.Plugin);
		FString Error;

		if (bPluginEnabled && Patch.Status == EPatchStatus::NotApplied)
		{
			if (!FEnginePatchManager::ApplyPatch(Patch, Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("EnginePatch: auto-apply on close failed for [%s]: %s"), *Patch.PatchId, *Error);
			}
		}
		else if (!bPluginEnabled && Patch.Status == EPatchStatus::Applied)
		{
			if (!FEnginePatchManager::UnpatchPatch(Patch, Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("EnginePatch: auto-unpatch on close failed for [%s]: %s"), *Patch.PatchId, *Error);
			}
		}
	}
}

void UUnrealEnginePatchSubsystem::RefreshStatus()
{
	for (FEnginePatch& Patch : Patches)
	{
		Patch.Status = FEnginePatchManager::GetPatchStatus(Patch);
		Patch.ErrorMessage.Empty();
	}
}

void UUnrealEnginePatchSubsystem::ApplyAll(TArray<FString>& OutErrors)
{
	for (FEnginePatch& Patch : Patches)
	{
		if (Patch.Status == EPatchStatus::Applied || Patch.Status == EPatchStatus::NotApplicable) continue;
		FString Error;
		if (!FEnginePatchManager::ApplyPatch(Patch, Error))
		{
			Patch.Status = EPatchStatus::Error;
			Patch.ErrorMessage = Error;
			OutErrors.Add(FString::Printf(TEXT("[%s] %s"), *Patch.PatchId, *Error));
		}
		else
		{
			Patch.Status = FEnginePatchManager::GetPatchStatus(Patch);
		}
	}
}

void UUnrealEnginePatchSubsystem::UnpatchAll(TArray<FString>& OutErrors)
{
	for (FEnginePatch& Patch : Patches)
	{
		if (Patch.Status == EPatchStatus::NotApplied || Patch.Status == EPatchStatus::NotApplicable) continue;
		FString Error;
		if (!FEnginePatchManager::UnpatchPatch(Patch, Error))
		{
			Patch.Status = EPatchStatus::Error;
			Patch.ErrorMessage = Error;
			OutErrors.Add(FString::Printf(TEXT("[%s] %s"), *Patch.PatchId, *Error));
		}
		else
		{
			Patch.Status = FEnginePatchManager::GetPatchStatus(Patch);
		}
	}
}

bool UUnrealEnginePatchSubsystem::ApplyPatch(const FString& PatchId, FString& OutError)
{
	FEnginePatch* Found = Patches.FindByPredicate([&](const FEnginePatch& P) { return P.PatchId == PatchId; });
	if (!Found) { OutError = TEXT("Patch not found"); return false; }
	bool bOk = FEnginePatchManager::ApplyPatch(*Found, OutError);
	Found->Status = FEnginePatchManager::GetPatchStatus(*Found);
	if (!bOk) Found->Status = EPatchStatus::Error;
	return bOk;
}

bool UUnrealEnginePatchSubsystem::UnpatchPatch(const FString& PatchId, FString& OutError)
{
	FEnginePatch* Found = Patches.FindByPredicate([&](const FEnginePatch& P) { return P.PatchId == PatchId; });
	if (!Found) { OutError = TEXT("Patch not found"); return false; }
	bool bOk = FEnginePatchManager::UnpatchPatch(*Found, OutError);
	Found->Status = FEnginePatchManager::GetPatchStatus(*Found);
	if (!bOk) Found->Status = EPatchStatus::Error;
	return bOk;
}
