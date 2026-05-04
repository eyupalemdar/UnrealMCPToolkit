// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTProjectCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"

#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PluginDescriptor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MCPToolkit::CommandHandlers::Project
{
namespace
{
FString ResolveProjectFilePath(FString& OutError)
{
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectFile.IsEmpty() || !FPaths::FileExists(ProjectFile))
	{
		OutError = FString::Printf(TEXT("Current project file not found: %s"), *ProjectFile);
		return FString();
	}
	return ProjectFile;
}

bool LoadProjectJson(TSharedPtr<FJsonObject>& OutRoot, FString& OutProjectFile, FString& OutError)
{
	OutProjectFile = ResolveProjectFilePath(OutError);
	if (OutProjectFile.IsEmpty())
	{
		return false;
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *OutProjectFile))
	{
		OutError = FString::Printf(TEXT("Could not read project file: %s"), *OutProjectFile);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
	{
		OutError = FString::Printf(TEXT("Could not parse project JSON: %s"), *OutProjectFile);
		return false;
	}

	return true;
}

bool SaveProjectJson(const TSharedPtr<FJsonObject>& Root, const FString& ProjectFile, FString& OutError)
{
	if (!Root.IsValid())
	{
		OutError = TEXT("Project JSON root is invalid");
		return false;
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("Could not serialize project JSON");
		return false;
	}
	Output.AppendChar(TEXT('\n'));

	if (!FFileHelper::SaveStringToFile(Output, *ProjectFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Could not write project file: %s"), *ProjectFile);
		return false;
	}

	return true;
}

bool BackupFile(const FString& FilePath, bool bCreateBackup, FString& OutBackupPath, FString& OutError)
{
	if (!bCreateBackup)
	{
		return true;
	}

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	OutBackupPath = FString::Printf(TEXT("%s.commonai.%s.bak"), *FilePath, *Timestamp);
	if (IFileManager::Get().Copy(*OutBackupPath, *FilePath) != COPY_OK)
	{
		OutError = FString::Printf(TEXT("Could not create backup file: %s"), *OutBackupPath);
		return false;
	}

	return true;
}

FString PluginTypeToString(EPluginType Type)
{
	switch (Type)
	{
	case EPluginType::Engine:
		return TEXT("Engine");
	case EPluginType::Enterprise:
		return TEXT("Enterprise");
	case EPluginType::Project:
		return TEXT("Project");
	case EPluginType::External:
		return TEXT("External");
	case EPluginType::Mod:
		return TEXT("Mod");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildPluginReferenceJson(const TSharedPtr<FJsonObject>& Reference)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Reference.IsValid())
	{
		return Data;
	}

	FString Name;
	Reference->TryGetStringField(TEXT("Name"), Name);
	Data->SetStringField(TEXT("name"), Name);

	bool bEnabled = false;
	if (Reference->TryGetBoolField(TEXT("Enabled"), bEnabled))
	{
		Data->SetBoolField(TEXT("enabled_in_project_file"), bEnabled);
	}
	Data->SetObjectField(TEXT("reference"), Reference);

	if (!Name.IsEmpty())
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
		Data->SetBoolField(TEXT("installed"), Plugin.IsValid());
		if (Plugin.IsValid())
		{
			const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
			Data->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
			Data->SetStringField(TEXT("descriptor_file"), Plugin->GetDescriptorFileName());
			Data->SetStringField(TEXT("base_dir"), Plugin->GetBaseDir());
			Data->SetStringField(TEXT("type"), PluginTypeToString(Plugin->GetType()));
			Data->SetBoolField(TEXT("currently_enabled"), Plugin->IsEnabled());
			Data->SetBoolField(TEXT("mounted"), Plugin->IsMounted());
			Data->SetBoolField(TEXT("can_contain_content"), Plugin->CanContainContent());
			Data->SetStringField(TEXT("version_name"), Descriptor.VersionName);
			Data->SetStringField(TEXT("description"), Descriptor.Description);
			Data->SetStringField(TEXT("category"), Descriptor.Category);
		}
	}

	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildModuleArray(const TArray<TSharedPtr<FJsonValue>>* ModulesArray)
{
	TArray<TSharedPtr<FJsonValue>> Modules;
	if (!ModulesArray)
	{
		return Modules;
	}

	for (const TSharedPtr<FJsonValue>& Value : *ModulesArray)
	{
		const TSharedPtr<FJsonObject>* ModuleObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ModuleObject) || !ModuleObject || !ModuleObject->IsValid())
		{
			continue;
		}
		Modules.Add(MakeShared<FJsonValueObject>(*ModuleObject));
	}

	return Modules;
}

TArray<TSharedPtr<FJsonValue>> BuildProjectPluginReferences(const TArray<TSharedPtr<FJsonValue>>* PluginsArray)
{
	TArray<TSharedPtr<FJsonValue>> Plugins;
	if (!PluginsArray)
	{
		return Plugins;
	}

	for (const TSharedPtr<FJsonValue>& Value : *PluginsArray)
	{
		const TSharedPtr<FJsonObject>* PluginObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(PluginObject) || !PluginObject || !PluginObject->IsValid())
		{
			continue;
		}
		Plugins.Add(MakeShared<FJsonValueObject>(BuildPluginReferenceJson(*PluginObject)));
	}

	return Plugins;
}

FString NormalizeConfigFileName(FString ConfigNameOrFile)
{
	ConfigNameOrFile.TrimStartAndEndInline();
	if (ConfigNameOrFile.IsEmpty())
	{
		ConfigNameOrFile = TEXT("Engine");
	}

	ConfigNameOrFile = FPaths::GetBaseFilename(FPaths::GetCleanFilename(ConfigNameOrFile));
	if (!ConfigNameOrFile.StartsWith(TEXT("Default")))
	{
		ConfigNameOrFile = FString::Printf(TEXT("Default%s"), *ConfigNameOrFile);
	}
	return ConfigNameOrFile + TEXT(".ini");
}

bool ResolveConfigPath(TSharedPtr<FJsonObject> Params, FString& OutConfigPath, FString& OutConfigFile, FString& OutError)
{
	FString ConfigName;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("config_name"), ConfigName);
		if (ConfigName.IsEmpty())
		{
			Params->TryGetStringField(TEXT("config_file"), ConfigName);
		}
	}

	OutConfigFile = NormalizeConfigFileName(ConfigName);
	const FString ConfigDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir());
	OutConfigPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ConfigDir, OutConfigFile));
	if (!FPaths::IsUnderDirectory(OutConfigPath, ConfigDir))
	{
		OutError = TEXT("Config file must resolve under the current project Config directory");
		return false;
	}
	return true;
}

bool LoadConfigFile(TSharedPtr<FJsonObject> Params, FConfigFile& OutConfigFile, FString& OutConfigPath, FString& OutConfigName, FString& OutError)
{
	if (!ResolveConfigPath(Params, OutConfigPath, OutConfigName, OutError))
	{
		return false;
	}
	if (!FPaths::FileExists(OutConfigPath))
	{
		OutError = FString::Printf(TEXT("Config file not found: %s"), *OutConfigPath);
		return false;
	}

	OutConfigFile.Read(OutConfigPath);
	return true;
}

void AddConfigIdentity(TSharedPtr<FJsonObject> Data, const FString& ConfigPath, const FString& ConfigName)
{
	Data->SetStringField(TEXT("config_file"), ConfigName);
	Data->SetStringField(TEXT("config_path"), ConfigPath);
}

bool ReadRequiredString(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, FString& OutValue, FString& OutError)
{
	if (!Params.IsValid() || !Params->TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Missing '%s' parameter"), FieldName);
		return false;
	}
	return true;
}

TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
}
}

FString HandleProjectInfo(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<FJsonObject> Root;
	FString ProjectFile;
	FString Error;
	if (!LoadProjectJson(Root, ProjectFile, Error))
	{
		return CreateErrorResponse(Error);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetStringField(TEXT("project_dir"), FPaths::GetPath(ProjectFile));
	Data->SetStringField(TEXT("project_name"), FPaths::GetBaseFilename(ProjectFile));

	FString StringValue;
	if (Root->TryGetStringField(TEXT("EngineAssociation"), StringValue))
	{
		Data->SetStringField(TEXT("engine_association"), StringValue);
	}
	if (Root->TryGetStringField(TEXT("Category"), StringValue))
	{
		Data->SetStringField(TEXT("category"), StringValue);
	}
	if (Root->TryGetStringField(TEXT("Description"), StringValue))
	{
		Data->SetStringField(TEXT("description"), StringValue);
	}

	double FileVersion = 0.0;
	if (Root->TryGetNumberField(TEXT("FileVersion"), FileVersion))
	{
		Data->SetNumberField(TEXT("file_version"), FileVersion);
	}

	const TArray<TSharedPtr<FJsonValue>>* PluginsArray = nullptr;
	Root->TryGetArrayField(TEXT("Plugins"), PluginsArray);
	Data->SetArrayField(TEXT("plugins"), BuildProjectPluginReferences(PluginsArray));

	const TArray<TSharedPtr<FJsonValue>>* ModulesArray = nullptr;
	Root->TryGetArrayField(TEXT("Modules"), ModulesArray);
	Data->SetArrayField(TEXT("modules"), BuildModuleArray(ModulesArray));

	const TArray<TSharedPtr<FJsonValue>>* TargetPlatformsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("TargetPlatforms"), TargetPlatformsArray) && TargetPlatformsArray)
	{
		Data->SetArrayField(TEXT("target_platforms"), *TargetPlatformsArray);
	}

	return CreateSuccessResponse(Data);
}

FString HandleProjectPluginList(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<FJsonObject> Root;
	FString ProjectFile;
	FString Error;
	if (!LoadProjectJson(Root, ProjectFile, Error))
	{
		return CreateErrorResponse(Error);
	}

	bool bIncludeEnabledPlugins = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_enabled_plugins"), bIncludeEnabledPlugins);
	}

	const TArray<TSharedPtr<FJsonValue>>* PluginsArray = nullptr;
	Root->TryGetArrayField(TEXT("Plugins"), PluginsArray);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetArrayField(TEXT("project_plugins"), BuildProjectPluginReferences(PluginsArray));

	if (bIncludeEnabledPlugins)
	{
		TArray<TSharedPtr<FJsonValue>> EnabledPlugins;
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			TSharedPtr<FJsonObject> PluginJson = MakeShared<FJsonObject>();
			const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
			PluginJson->SetStringField(TEXT("name"), Plugin->GetName());
			PluginJson->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
			PluginJson->SetStringField(TEXT("type"), PluginTypeToString(Plugin->GetType()));
			PluginJson->SetStringField(TEXT("base_dir"), Plugin->GetBaseDir());
			PluginJson->SetStringField(TEXT("descriptor_file"), Plugin->GetDescriptorFileName());
			PluginJson->SetBoolField(TEXT("can_contain_content"), Plugin->CanContainContent());
			PluginJson->SetStringField(TEXT("version_name"), Descriptor.VersionName);
			PluginJson->SetStringField(TEXT("category"), Descriptor.Category);
			EnabledPlugins.Add(MakeShared<FJsonValueObject>(PluginJson));
		}
		Data->SetArrayField(TEXT("enabled_plugins"), EnabledPlugins);
	}

	return CreateSuccessResponse(Data);
}

FString HandleProjectPluginSetEnabled(TSharedPtr<FJsonObject> Params)
{
	FString PluginName;
	FString Error;
	if (!ReadRequiredString(Params, TEXT("plugin_name"), PluginName, Error))
	{
		return CreateErrorResponse(Error);
	}

	bool bEnabled = true;
	bool bCreateBackup = true;
	bool bAllowUnknownPlugin = false;
	Params->TryGetBoolField(TEXT("enabled"), bEnabled);
	Params->TryGetBoolField(TEXT("create_backup"), bCreateBackup);
	Params->TryGetBoolField(TEXT("allow_unknown_plugin"), bAllowUnknownPlugin);

	TSharedPtr<IPlugin> InstalledPlugin = IPluginManager::Get().FindPlugin(PluginName);
	if (!InstalledPlugin.IsValid() && !bAllowUnknownPlugin)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Plugin is not installed or discoverable: %s"), *PluginName));
	}

	TSharedPtr<FJsonObject> Root;
	FString ProjectFile;
	if (!LoadProjectJson(Root, ProjectFile, Error))
	{
		return CreateErrorResponse(Error);
	}

	const TArray<TSharedPtr<FJsonValue>>* ExistingPluginsArray = nullptr;
	TArray<TSharedPtr<FJsonValue>> PluginsArray;
	if (Root->TryGetArrayField(TEXT("Plugins"), ExistingPluginsArray) && ExistingPluginsArray)
	{
		PluginsArray = *ExistingPluginsArray;
	}

	bool bFound = false;
	for (const TSharedPtr<FJsonValue>& Value : PluginsArray)
	{
		const TSharedPtr<FJsonObject>* PluginObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(PluginObject) || !PluginObject || !PluginObject->IsValid())
		{
			continue;
		}

		FString ExistingName;
		if ((*PluginObject)->TryGetStringField(TEXT("Name"), ExistingName) && ExistingName.Equals(PluginName, ESearchCase::IgnoreCase))
		{
			(*PluginObject)->SetStringField(TEXT("Name"), PluginName);
			(*PluginObject)->SetBoolField(TEXT("Enabled"), bEnabled);
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		TSharedPtr<FJsonObject> NewPluginReference = MakeShared<FJsonObject>();
		NewPluginReference->SetStringField(TEXT("Name"), PluginName);
		NewPluginReference->SetBoolField(TEXT("Enabled"), bEnabled);
		PluginsArray.Add(MakeShared<FJsonValueObject>(NewPluginReference));
	}

	Root->SetArrayField(TEXT("Plugins"), PluginsArray);

	FString BackupPath;
	if (!BackupFile(ProjectFile, bCreateBackup, BackupPath, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!SaveProjectJson(Root, ProjectFile, Error))
	{
		return CreateErrorResponse(Error);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetStringField(TEXT("plugin_name"), PluginName);
	Data->SetBoolField(TEXT("enabled"), bEnabled);
	Data->SetBoolField(TEXT("added_reference"), !bFound);
	Data->SetBoolField(TEXT("installed"), InstalledPlugin.IsValid());
	Data->SetBoolField(TEXT("restart_required"), true);
	if (!BackupPath.IsEmpty())
	{
		Data->SetStringField(TEXT("backup_path"), BackupPath);
	}
	return CreateSuccessResponse(Data);
}

FString HandleProjectModuleList(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<FJsonObject> Root;
	FString ProjectFile;
	FString Error;
	if (!LoadProjectJson(Root, ProjectFile, Error))
	{
		return CreateErrorResponse(Error);
	}

	const TArray<TSharedPtr<FJsonValue>>* ModulesArray = nullptr;
	Root->TryGetArrayField(TEXT("Modules"), ModulesArray);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetArrayField(TEXT("modules"), BuildModuleArray(ModulesArray));
	return CreateSuccessResponse(Data);
}

FString HandleProjectConfigGet(TSharedPtr<FJsonObject> Params)
{
	FString Section;
	FString Key;
	FString Error;
	if (!ReadRequiredString(Params, TEXT("section"), Section, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!ReadRequiredString(Params, TEXT("key"), Key, Error))
	{
		return CreateErrorResponse(Error);
	}

	FConfigFile ConfigFile;
	FString ConfigPath;
	FString ConfigName;
	if (!LoadConfigFile(Params, ConfigFile, ConfigPath, ConfigName, Error))
	{
		return CreateErrorResponse(Error);
	}

	const FConfigSection* ConfigSection = ConfigFile.FindSection(Section);
	if (!ConfigSection)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Config section not found: %s"), *Section));
	}

	FString Value;
	const bool bFound = ConfigSection->GetString(*Key, Value);
	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Config key not found: %s.%s"), *Section, *Key));
	}

	TArray<FString> Values;
	ConfigSection->MultiFind(FName(*Key), Values, true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddConfigIdentity(Data, ConfigPath, ConfigName);
	Data->SetStringField(TEXT("section"), Section);
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("value"), Value);
	Data->SetArrayField(TEXT("values"), StringArrayToJson(Values));
	Data->SetNumberField(TEXT("value_count"), Values.Num());
	return CreateSuccessResponse(Data);
}

FString HandleProjectConfigSet(TSharedPtr<FJsonObject> Params)
{
	FString Section;
	FString Key;
	FString Value;
	FString Error;
	if (!ReadRequiredString(Params, TEXT("section"), Section, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!ReadRequiredString(Params, TEXT("key"), Key, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!ReadRequiredString(Params, TEXT("value"), Value, Error))
	{
		return CreateErrorResponse(Error);
	}

	bool bCreateBackup = true;
	Params->TryGetBoolField(TEXT("create_backup"), bCreateBackup);

	FString ConfigPath;
	FString ConfigName;
	if (!ResolveConfigPath(Params, ConfigPath, ConfigName, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString BackupPath;
	if (FPaths::FileExists(ConfigPath) && !BackupFile(ConfigPath, bCreateBackup, BackupPath, Error))
	{
		return CreateErrorResponse(Error);
	}

	GConfig->SetString(*Section, *Key, *Value, ConfigPath);
	GConfig->Flush(false, ConfigPath);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddConfigIdentity(Data, ConfigPath, ConfigName);
	Data->SetStringField(TEXT("section"), Section);
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("value"), Value);
	if (!BackupPath.IsEmpty())
	{
		Data->SetStringField(TEXT("backup_path"), BackupPath);
	}
	return CreateSuccessResponse(Data);
}

FString HandleProjectConfigDelete(TSharedPtr<FJsonObject> Params)
{
	FString Section;
	FString Key;
	FString Error;
	if (!ReadRequiredString(Params, TEXT("section"), Section, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!ReadRequiredString(Params, TEXT("key"), Key, Error))
	{
		return CreateErrorResponse(Error);
	}

	bool bCreateBackup = true;
	Params->TryGetBoolField(TEXT("create_backup"), bCreateBackup);

	FString ConfigPath;
	FString ConfigName;
	if (!ResolveConfigPath(Params, ConfigPath, ConfigName, Error))
	{
		return CreateErrorResponse(Error);
	}
	if (!FPaths::FileExists(ConfigPath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Config file not found: %s"), *ConfigPath));
	}

	FString BackupPath;
	if (!BackupFile(ConfigPath, bCreateBackup, BackupPath, Error))
	{
		return CreateErrorResponse(Error);
	}

	const bool bRemoved = GConfig->RemoveKey(*Section, *Key, ConfigPath);
	if (bRemoved)
	{
		GConfig->Flush(false, ConfigPath);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddConfigIdentity(Data, ConfigPath, ConfigName);
	Data->SetStringField(TEXT("section"), Section);
	Data->SetStringField(TEXT("key"), Key);
	Data->SetBoolField(TEXT("removed"), bRemoved);
	if (!BackupPath.IsEmpty())
	{
		Data->SetStringField(TEXT("backup_path"), BackupPath);
	}
	return CreateSuccessResponse(Data);
}

FString HandleProjectConfigListSections(TSharedPtr<FJsonObject> Params)
{
	FConfigFile ConfigFile;
	FString ConfigPath;
	FString ConfigName;
	FString Error;
	if (!LoadConfigFile(Params, ConfigFile, ConfigPath, ConfigName, Error))
	{
		return CreateErrorResponse(Error);
	}

	TArray<FString> Sections;
	ConfigFile.GetKeys(Sections);
	Sections.Sort();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddConfigIdentity(Data, ConfigPath, ConfigName);
	Data->SetArrayField(TEXT("sections"), StringArrayToJson(Sections));
	Data->SetNumberField(TEXT("section_count"), Sections.Num());
	return CreateSuccessResponse(Data);
}

FString HandleProjectConfigListKeys(TSharedPtr<FJsonObject> Params)
{
	FString Section;
	FString Error;
	if (!ReadRequiredString(Params, TEXT("section"), Section, Error))
	{
		return CreateErrorResponse(Error);
	}

	bool bIncludeValues = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_values"), bIncludeValues);
	}

	FConfigFile ConfigFile;
	FString ConfigPath;
	FString ConfigName;
	if (!LoadConfigFile(Params, ConfigFile, ConfigPath, ConfigName, Error))
	{
		return CreateErrorResponse(Error);
	}

	const FConfigSection* ConfigSection = ConfigFile.FindSection(Section);
	if (!ConfigSection)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Config section not found: %s"), *Section));
	}

	TSet<FString> UniqueKeys;
	for (const TPair<FName, FConfigValue>& Pair : *ConfigSection)
	{
		UniqueKeys.Add(Pair.Key.ToString());
	}

	TArray<FString> SortedKeys = UniqueKeys.Array();
	SortedKeys.Sort();

	TArray<TSharedPtr<FJsonValue>> KeyValues;
	for (const FString& ConfigKey : SortedKeys)
	{
		if (!bIncludeValues)
		{
			KeyValues.Add(MakeShared<FJsonValueString>(ConfigKey));
			continue;
		}

		TArray<FString> Values;
		ConfigSection->MultiFind(FName(*ConfigKey), Values, true);

		TSharedPtr<FJsonObject> KeyObject = MakeShared<FJsonObject>();
		KeyObject->SetStringField(TEXT("key"), ConfigKey);
		KeyObject->SetArrayField(TEXT("values"), StringArrayToJson(Values));
		KeyObject->SetNumberField(TEXT("value_count"), Values.Num());
		KeyValues.Add(MakeShared<FJsonValueObject>(KeyObject));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddConfigIdentity(Data, ConfigPath, ConfigName);
	Data->SetStringField(TEXT("section"), Section);
	Data->SetBoolField(TEXT("include_values"), bIncludeValues);
	Data->SetArrayField(TEXT("keys"), KeyValues);
	Data->SetNumberField(TEXT("key_count"), SortedKeys.Num());
	return CreateSuccessResponse(Data);
}
}
