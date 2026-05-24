using System.IO;
using UnrealBuildTool;

public class GammaTon : ModuleRules
{
	public GammaTon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true; // STL may need this

		// Embree4: Plugins/GammaTon/ThirdParty/Embree4/
		// ModuleDirectory = Plugins/GammaTon/Source/GammaTon/
		string EmbreeRoot   = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Embree4"));
		string EmbreeBinDir = Path.Combine(EmbreeRoot, "bin");
		PublicIncludePaths.Add(Path.Combine(EmbreeRoot, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(EmbreeRoot, "lib", "embree4.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(EmbreeRoot, "lib", "tbb.lib"));

		// DLLs: ThirdParty/Embree4/bin/ → Binaries/Win64/ (UBT가 빌드 시 자동 복사)
		string PluginBinDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Binaries", "Win64"));
		RuntimeDependencies.Add(Path.Combine(PluginBinDir, "embree4.dll"),   Path.Combine(EmbreeBinDir, "embree4.dll"));
		RuntimeDependencies.Add(Path.Combine(PluginBinDir, "tbb12.dll"),     Path.Combine(EmbreeBinDir, "tbb12.dll"));
		RuntimeDependencies.Add(Path.Combine(PluginBinDir, "tbbmalloc.dll"), Path.Combine(EmbreeBinDir, "tbbmalloc.dll"));

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine",
			"RenderCore", "RHI"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore",
			"InputCore",
			"EditorFramework", "UnrealEd",
			"ToolMenus",
			"StaticMeshDescription",
			"MeshDescription",
			"AssetRegistry",
			"AssetTools",
			"PropertyEditor"
		});
	}
}
