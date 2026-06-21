#pragma once
#include "EnginePatchTypes.h"

class FEnginePatchFileLoader
{
public:
	// Scan directory for *.json files, parse each into FEnginePatch. Returns parsed patches.
	static TArray<FEnginePatch> LoadPatchesFromDirectory(const FString& Directory);

private:
	static bool ParsePatchJson(const FString& JsonContent, FEnginePatch& OutPatch, FString& OutError);
};
