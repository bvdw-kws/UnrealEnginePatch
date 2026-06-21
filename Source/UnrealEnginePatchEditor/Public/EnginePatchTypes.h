#pragma once
#include "CoreMinimal.h"

enum class EPatchStatus : uint8
{
	Unknown,
	Applied,
	NotApplied,
	NotApplicable,  // no version entry matches current engine
	Error,
};

struct FEnginePatchOperation
{
	FString Id;          // unique within the patch, e.g. "add-blueprint-type"
	int32   Line = 0;    // 1-based line in the UNPATCHED file
	TArray<FString> Remove; // lines to replace (empty = pure insertion)
	TArray<FString> Add;    // lines to insert
};

struct FEnginePatchFile
{
	FString File;       // relative to Engine/Source/, e.g. "Runtime/MassEntity/Public/MassProcessingTypes.h"
	TArray<FEnginePatchOperation> Operations;
};

struct FEnginePatchVersion
{
	FString EngineVersion;  // e.g. "5.8"
	TArray<FEnginePatchFile> Files;
};

struct FEnginePatch
{
	FString PatchId;
	FString Description;
	TArray<FEnginePatchVersion> Versions;

	// Runtime state (not from JSON)
	EPatchStatus Status = EPatchStatus::Unknown;
	FString ErrorMessage;
};
