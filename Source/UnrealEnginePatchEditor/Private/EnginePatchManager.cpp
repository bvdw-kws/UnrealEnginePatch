// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0


#include "EnginePatchManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Runtime/Launch/Resources/Version.h"

static void MakeWritable(const FString& FilePath)
{
	if (IFileManager::Get().IsReadOnly(*FilePath))
	{
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FilePath, false);
	}
}

FString FEnginePatchManager::GetCurrentEngineVersion()
{
	return FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
}

FString FEnginePatchManager::ResolveEnginePath(const FString& RelativePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source") / RelativePath);
}

FString FEnginePatchManager::MakeBeginMarker(const FString& PatchId, const FString& OpId)
{
	return FString::Printf(TEXT("// @@PATCH_BEGIN(%s::%s)"), *PatchId, *OpId);
}

FString FEnginePatchManager::MakeEndMarker(const FString& PatchId, const FString& OpId)
{
	return FString::Printf(TEXT("// @@PATCH_END(%s::%s)"), *PatchId, *OpId);
}

bool FEnginePatchManager::IsOperationApplied(const FString& FilePath, const FString& PatchId, const FString& OpId)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath)) return false;
	return Content.Contains(MakeBeginMarker(PatchId, OpId));
}

EPatchStatus FEnginePatchManager::GetPatchStatus(const FEnginePatch& Patch)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });

	if (!MatchedVersion) return EPatchStatus::NotApplicable;

	// Consider Applied if ALL operations in all files are applied.
	int32 AppliedCount = 0;
	int32 TotalCount = 0;
	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);
		for (const FEnginePatchOperation& Op : PFile.Operations)
		{
			++TotalCount;
			if (IsOperationApplied(FullPath, Patch.PatchId, Op.Id)) ++AppliedCount;
		}
	}
	if (TotalCount == 0) return EPatchStatus::NotApplied;
	if (AppliedCount == TotalCount) return EPatchStatus::Applied;
	if (AppliedCount == 0) return EPatchStatus::NotApplied;
	return EPatchStatus::Error; // partial application
}

bool FEnginePatchManager::ApplyOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError)
{
	if (IsOperationApplied(FilePath, PatchId, Op.Id)) return true; // idempotent

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot read file: %s"), *FilePath);
		return false;
	}

	int32 LineIndex = Op.Line - 1; // convert to 0-based

	// Validate remove lines match
	if (Op.Remove.Num() > 0)
	{
		for (int32 i = 0; i < Op.Remove.Num(); ++i)
		{
			if (LineIndex + i >= Lines.Num())
			{
				OutError = FString::Printf(TEXT("Line %d out of range in %s"), Op.Line + i, *FilePath);
				return false;
			}
			FString Expected = Op.Remove[i].TrimStartAndEnd();
			FString Actual   = Lines[LineIndex + i].TrimStartAndEnd();
			if (Expected != Actual)
			{
				OutError = FString::Printf(TEXT("Line %d mismatch in %s\nExpected: %s\nActual:   %s"),
					Op.Line + i, *FilePath, *Expected, *Actual);
				return false;
			}
		}
	}

	// Build replacement block
	TArray<FString> Block;
	Block.Add(MakeBeginMarker(PatchId, Op.Id));
	for (const FString& RemovedLine : Op.Remove)
	{
		Block.Add(FString::Printf(TEXT("// @@REMOVED: %s"), *RemovedLine));
	}
	for (const FString& AddedLine : Op.Add)
	{
		Block.Add(AddedLine);
	}
	Block.Add(MakeEndMarker(PatchId, Op.Id));

	// Replace remove lines (or insert if Remove is empty)
	int32 RemoveCount = FMath::Max(Op.Remove.Num(), 0);
	if (RemoveCount > 0)
	{
		Lines.RemoveAt(LineIndex, RemoveCount);
	}
	for (int32 i = 0; i < Block.Num(); ++i)
	{
		Lines.Insert(Block[i], LineIndex + i);
	}

	MakeWritable(FilePath);
	if (!FFileHelper::SaveStringArrayToFile(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot write file: %s"), *FilePath);
		return false;
	}
	return true;
}

bool FEnginePatchManager::ApplyPatch(const FEnginePatch& Patch, FString& OutError)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });
	if (!MatchedVersion) return true; // NotApplicable — not an error

	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);

		// Load file once
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FullPath))
		{
			OutError = FString::Printf(TEXT("Cannot read file: %s"), *FullPath);
			return false;
		}

		int32 LineOffset = 0;

		// Apply each operation in order, tracking line offset
		for (const FEnginePatchOperation& Op : PFile.Operations)
		{
			// Check idempotency: search for begin marker in current Lines
			FString BeginMarker = MakeBeginMarker(Patch.PatchId, Op.Id);
			bool bAlreadyApplied = false;
			for (const FString& Line : Lines)
			{
				if (Line.Contains(BeginMarker))
				{
					bAlreadyApplied = true;
					break;
				}
			}

			// If already applied, skip this operation (do NOT update LineOffset)
			if (bAlreadyApplied)
			{
				continue;
			}

			int32 AdjustedLine = (Op.Line - 1) + LineOffset; // Convert to 0-based and apply offset

			// Validate remove lines match
			if (Op.Remove.Num() > 0)
			{
				for (int32 i = 0; i < Op.Remove.Num(); ++i)
				{
					if (AdjustedLine + i >= Lines.Num())
					{
						OutError = FString::Printf(TEXT("Line %d out of range in %s"), Op.Line + i, *FullPath);
						return false;
					}
					FString Expected = Op.Remove[i].TrimStartAndEnd();
					FString Actual   = Lines[AdjustedLine + i].TrimStartAndEnd();
					if (Expected != Actual)
					{
						OutError = FString::Printf(TEXT("Line %d mismatch in %s\nExpected: %s\nActual:   %s"),
							Op.Line + i, *FullPath, *Expected, *Actual);
						return false;
					}
				}
			}

			// Build marker block (BEGIN, @@REMOVED: lines, add lines, END)
			TArray<FString> Block;
			Block.Add(MakeBeginMarker(Patch.PatchId, Op.Id));
			for (const FString& RemovedLine : Op.Remove)
			{
				Block.Add(FString::Printf(TEXT("// @@REMOVED: %s"), *RemovedLine));
			}
			for (const FString& AddedLine : Op.Add)
			{
				Block.Add(AddedLine);
			}
			Block.Add(MakeEndMarker(Patch.PatchId, Op.Id));

			// Replace/insert: remove old lines, insert block
			int32 RemoveCount = FMath::Max(Op.Remove.Num(), 0);
			if (RemoveCount > 0)
			{
				Lines.RemoveAt(AdjustedLine, RemoveCount);
			}
			for (int32 i = 0; i < Block.Num(); ++i)
			{
				Lines.Insert(Block[i], AdjustedLine + i);
			}

			// Update line offset for next operation
			LineOffset += Block.Num() - RemoveCount;
		}

		// Save file once at the end
		MakeWritable(FullPath);
		if (!FFileHelper::SaveStringArrayToFile(Lines, *FullPath))
		{
			OutError = FString::Printf(TEXT("Cannot write file: %s"), *FullPath);
			return false;
		}
	}
	return true;
}

bool FEnginePatchManager::UnpatchPatch(const FEnginePatch& Patch, FString& OutError)
{
	FString EngineVersion = GetCurrentEngineVersion();
	const FEnginePatchVersion* MatchedVersion = Patch.Versions.FindByPredicate(
		[&](const FEnginePatchVersion& V) { return V.EngineVersion == EngineVersion; });
	if (!MatchedVersion) return true; // NotApplicable — not an error

	// Iterate operations in reverse order to handle line drift correctly during removal
	for (const FEnginePatchFile& PFile : MatchedVersion->Files)
	{
		FString FullPath = ResolveEnginePath(PFile.File);
		for (int32 i = PFile.Operations.Num() - 1; i >= 0; --i)
		{
			const FEnginePatchOperation& Op = PFile.Operations[i];
			if (!UnpatchOperation(FullPath, Patch.PatchId, Op, OutError)) return false;
		}
	}
	return true;
}

bool FEnginePatchManager::UnpatchOperation(const FString& FilePath, const FString& PatchId, const FEnginePatchOperation& Op, FString& OutError)
{
	if (!IsOperationApplied(FilePath, PatchId, Op.Id)) return true; // already reverted

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot read file: %s"), *FilePath);
		return false;
	}

	FString BeginMarker = MakeBeginMarker(PatchId, Op.Id);
	FString EndMarker   = MakeEndMarker(PatchId, Op.Id);
	static const FString RemovedPrefix = TEXT("// @@REMOVED: ");

	int32 BeginIndex = INDEX_NONE;
	int32 EndIndex   = INDEX_NONE;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		if (Lines[i].TrimStartAndEnd() == BeginMarker) BeginIndex = i;
		if (Lines[i].TrimStartAndEnd() == EndMarker)   { EndIndex = i; break; }
	}

	if (BeginIndex == INDEX_NONE || EndIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Markers not found in %s for op %s::%s"), *FilePath, *PatchId, *Op.Id);
		return false;
	}

	// Collect restored lines from @@REMOVED: comments
	TArray<FString> Restored;
	for (int32 i = BeginIndex + 1; i < EndIndex; ++i)
	{
		FString Trimmed = Lines[i].TrimStartAndEnd();
		if (Trimmed.StartsWith(RemovedPrefix))
		{
			Restored.Add(Trimmed.Mid(RemovedPrefix.Len()));
		}
	}

	// Remove the entire BEGIN..END block
	int32 BlockLen = EndIndex - BeginIndex + 1;
	Lines.RemoveAt(BeginIndex, BlockLen);

	// Re-insert the original lines
	for (int32 i = 0; i < Restored.Num(); ++i)
	{
		Lines.Insert(Restored[i], BeginIndex + i);
	}

	MakeWritable(FilePath);
	if (!FFileHelper::SaveStringArrayToFile(Lines, *FilePath))
	{
		OutError = FString::Printf(TEXT("Cannot write file: %s"), *FilePath);
		return false;
	}
	return true;
}
