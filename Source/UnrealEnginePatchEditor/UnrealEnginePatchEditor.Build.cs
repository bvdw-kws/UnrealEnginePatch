using UnrealBuildTool;

public class UnrealEnginePatchEditor : ModuleRules
{
    public UnrealEnginePatchEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "EditorSubsystem",
            "UnrealEd",
            "Json",
            "JsonUtilities",
            "ToolMenus",
            "WorkspaceMenuStructure",
        });
    }
}
