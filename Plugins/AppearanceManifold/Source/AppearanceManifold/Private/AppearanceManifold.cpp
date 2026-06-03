// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppearanceManifold.h"
#include "Interfaces/IPluginManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Paths.h"


#define LOCTEXT_NAMESPACE "FAppearanceManifoldModule"

void FAppearanceManifoldModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
#if WITH_EDITOR

    RegisterPythonScriptPath();
    EnsurePythonDependenciesInstalled();
    UE_LOG(
        LogTemp,
        Log,
        TEXT("FAppearanceManifoldModule Registered"));
#endif
}

void FAppearanceManifoldModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}



#undef LOCTEXT_NAMESPACE
	
//#if WITH_EDITOR

FString FAppearanceManifoldModule::GetPluginDirectory() const
{
    TSharedPtr<IPlugin> Plugin =
        IPluginManager::Get().FindPlugin(TEXT("AppearanceManifold"));

    return Plugin.IsValid()
        ? Plugin->GetBaseDir()
        : FString();
}

void FAppearanceManifoldModule::RegisterPythonScriptPath()
{
    const FString PluginDir = GetPluginDirectory();

    if (PluginDir.IsEmpty())
    {
        return;
    }

    const FString ScriptsDir =
        FPaths::Combine(
            PluginDir,
            TEXT("Scripts"));

    FString Command =
        FString::Printf(
            TEXT(
                "import sys\n"
                "p=r'%s'\n"
                "if p not in sys.path:\n"
                "    sys.path.append(p)\n"
            ),
            *ScriptsDir.Replace(TEXT("\\"), TEXT("/")));

    IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("AppearanceManifold Script Path Registered : %s"),
        *ScriptsDir);
}

void FAppearanceManifoldModule::EnsurePythonDependenciesInstalled()
{
    const FString PluginDir = GetPluginDirectory();

    if (PluginDir.IsEmpty())
    {
        return;
    }

    const FString WheelDir =FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("PythonWheels"));

    FString PythonCommand;

    PythonCommand += TEXT("import importlib\n");
    PythonCommand += TEXT("import subprocess\n");
    PythonCommand += TEXT("import sys\n");
    PythonCommand += TEXT("import os\n");

    PythonCommand += FString::Printf(
        TEXT("wheel_dir=r'%s'\n"),
        *WheelDir.Replace(TEXT("\\"), TEXT("/")));

    PythonCommand += TEXT(
        "packages={\n"
        " 'numpy':'numpy',\n"
        " 'scipy':'scipy',\n"
        " 'scikit-learn':'sklearn'\n"
        "}\n"
        "\n"
        "missing=[]\n"
        "\n"
        "for pip_name,module_name in packages.items():\n"
        "    try:\n"
        "        importlib.import_module(module_name)\n"
        "    except ImportError:\n"
        "        missing.append(pip_name)\n"
        "\n"
        "if missing:\n"
        "    print('AppearanceManifold Installing:',missing)\n"
        "\n"
        "    for wheel in os.listdir(wheel_dir):\n"
        "        if wheel.endswith('.whl'):\n"
        "            subprocess.check_call([\n"
        "                sys.executable,\n"
        "                '-m',\n"
        "                'pip',\n"
        "                'install',\n"
        "                os.path.join(wheel_dir,wheel)\n"
        "            ])\n"
        "\n"
        "    print('AppearanceManifold Install Complete')\n"
    );

    IPythonScriptPlugin::Get()->ExecPythonCommand(*PythonCommand);
}


//#endif

IMPLEMENT_MODULE(FAppearanceManifoldModule, AppearanceManifold)