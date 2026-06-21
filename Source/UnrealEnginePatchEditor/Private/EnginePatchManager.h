#pragma once
#include "EnginePatchTypes.h"

class FEnginePatchManager
{
public:
	// Detect the current engine version string, e.g. "5.8"
	static FString GetCurrentEngineVersion();

	// Check if a patch is already applied (by looking for @@PATCH_BEGIN in target files).
	// Returns Applied, NotApplied, NotApplicable, or Error.
	static EPatchStatus GetPatchStatus(const FEnginePatch& Patch);

	// Apply all operations for the matching engine version. Idempotent.
	// Returns true on full success; sets OutError on failure.
	static bool ApplyPatch(const FEnginePatch& Patch, FString& OutError);

	// Revert all operations for the matching engine version.
	static bool UnpatchPatch(const FEnginePatch& Patch, FString& OutError);

	// Create marker strings for patch boundaries (public for Task 5 - UnpatchOperation)
	static FString MakeBeginMarker(const FString& PatchId, const FString& OpId);
	static FString MakeEndMarker(const FString& PatchId, const FString& OpId);

private:
	static FString ResolveEnginePath(const FString& RelativePath);
	static bool ApplyOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError);
	static bool UnpatchOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError);
	static bool IsOperationApplied(const FString& FilePath, const FString& PatchId, const FString& OpId);
};
