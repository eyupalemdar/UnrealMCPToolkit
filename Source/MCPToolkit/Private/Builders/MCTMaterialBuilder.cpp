// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTMaterialBuilder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MCTMaterialBuilder)

// Material core
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstanceConstant.h"

// Material editing
#include "MaterialEditingLibrary.h"

// Factory
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

// Asset management
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Compile
#include "MaterialShared.h"

// Undo/Redo
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MCTMaterialBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogMCTMaterialBuilder, Log, All);

// =============================================================================
// MATERIAL LIFECYCLE
// =============================================================================

UMaterial* UMCTMaterialBuilder::CreateMaterial(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& Domain,
	const FString& BlendMode,
	const FString& ShadingModel,
	bool bTwoSided)
{
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CreateMaterial: PackagePath and AssetName required"));
		return nullptr;
	}

	FString FullPath = PackagePath / AssetName;

	// Return existing if found
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FullPath))
	{
		UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("CreateMaterial: Already exists at '%s', returning existing"), *FullPath);
		return Existing;
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CreateMaterial: Failed to create package at '%s'"), *FullPath);
		return nullptr;
	}

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = Cast<UMaterial>(
		Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewMaterial)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CreateMaterial: Factory failed for '%s'"), *AssetName);
		return nullptr;
	}

	// Set domain
	if (Domain.Equals(TEXT("Surface"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_Surface;
	else if (Domain.Equals(TEXT("PostProcess"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_PostProcess;
	else if (Domain.Equals(TEXT("UI"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_UI;
	else if (Domain.Equals(TEXT("Volume"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_Volume;
	else if (Domain.Equals(TEXT("LightFunction"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_LightFunction;
	else if (Domain.Equals(TEXT("DeferredDecal"), ESearchCase::IgnoreCase)) NewMaterial->MaterialDomain = MD_DeferredDecal;

	// Set blend mode
	if (BlendMode.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Opaque;
	else if (BlendMode.Equals(TEXT("Masked"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Masked;
	else if (BlendMode.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Translucent;
	else if (BlendMode.Equals(TEXT("Additive"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Additive;
	else if (BlendMode.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Modulate;
	else if (BlendMode.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_AlphaComposite;

	// Set shading model
	if (ShadingModel.Equals(TEXT("DefaultLit"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_DefaultLit);
	else if (ShadingModel.Equals(TEXT("Unlit"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Unlit);
	else if (ShadingModel.Equals(TEXT("Subsurface"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Subsurface);
	else if (ShadingModel.Equals(TEXT("TwoSidedFoliage"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_TwoSidedFoliage);
	else if (ShadingModel.Equals(TEXT("ClearCoat"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_ClearCoat);
	else if (ShadingModel.Equals(TEXT("Hair"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Hair);
	else if (ShadingModel.Equals(TEXT("Cloth"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Cloth);
	else if (ShadingModel.Equals(TEXT("Eye"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Eye);
	else if (ShadingModel.Equals(TEXT("ThinTranslucent"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_ThinTranslucent);
	else if (ShadingModel.Equals(TEXT("SingleLayerWater"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_SingleLayerWater);

	NewMaterial->TwoSided = bTwoSided;

	FAssetRegistryModule::AssetCreated(NewMaterial);
	NewMaterial->MarkPackageDirty();

	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("CreateMaterial: Created '%s' (Domain=%s, Blend=%s, Shading=%s)"),
		*FullPath, *Domain, *BlendMode, *ShadingModel);
	return NewMaterial;
}

bool UMCTMaterialBuilder::SetMaterialProperty(
	UMaterial* Material,
	const FString& PropertyName,
	const FString& Value)
{
	if (!Material) return false;

	FScopedTransaction Transaction(LOCTEXT("AISetMaterialProp", "AI: Set Material Property"));

	bool bSuccess = SetPropertyByPath(Material, PropertyName, Value);
	if (bSuccess)
	{
		Material->MarkPackageDirty();
	}
	else
	{
		UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("SetMaterialProperty: Failed '%s'='%s' on '%s'"),
			*PropertyName, *Value, *Material->GetName());
	}
	return bSuccess;
}

bool UMCTMaterialBuilder::CompileMaterial(
	UMaterial* Material,
	TArray<FString>* OutWarnings)
{
	if (!Material)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CompileMaterial: Material is null"));
		return false;
	}

	// Use UMaterialEditingLibrary for the high-level recompile
	UMaterialEditingLibrary::RecompileMaterial(Material);

	// Save
	bool bSaved = SaveAsset(Material);

	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("CompileMaterial: '%s' compiled, saved=%s"),
		*Material->GetName(), bSaved ? TEXT("true") : TEXT("false"));
	return true;
}

UMaterial* UMCTMaterialBuilder::LoadMaterial(const FString& AssetPath)
{
	FString CleanPath = AssetPath;
	if (!CleanPath.EndsWith(TEXT(".")) && !CleanPath.Contains(TEXT(".")))
	{
		// Add object name: "/Game/Materials/M_Gold" -> "/Game/Materials/M_Gold.M_Gold"
		FString AssetName = FPaths::GetCleanFilename(CleanPath);
		CleanPath = CleanPath + TEXT(".") + AssetName;
	}

	UMaterial* Material = LoadObject<UMaterial>(nullptr, *CleanPath);
	if (!Material)
	{
		// Try without suffix
		Material = LoadObject<UMaterial>(nullptr, *AssetPath);
	}
	if (!Material)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("LoadMaterial: Not found at '%s'"), *AssetPath);
	}
	return Material;
}

// =============================================================================
// EXPRESSION MANIPULATION
// =============================================================================

UMaterialExpression* UMCTMaterialBuilder::AddExpression(
	UMaterial* Material,
	const FString& ExpressionClass,
	const FString& NodeName,
	int32 PosX,
	int32 PosY)
{
	if (!Material)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("AddExpression: Material is null"));
		return nullptr;
	}

	UClass* ExprClass = ResolveExpressionClass(ExpressionClass);
	if (!ExprClass)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("AddExpression: Cannot resolve class '%s'"), *ExpressionClass);
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddExpression", "AI: Add Material Expression"));

	UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
		Material, ExprClass, PosX, PosY);

	if (!Expr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("AddExpression: CreateMaterialExpression failed for '%s'"),
			*ExpressionClass);
		return nullptr;
	}

	// Store NodeName in Desc field for lookup
	Expr->Desc = NodeName;

	// If it's a parameter expression, also set ParameterName
	if (FProperty* ParamNameProp = ExprClass->FindPropertyByName(FName(TEXT("ParameterName"))))
	{
		if (FNameProperty* NameProp = CastField<FNameProperty>(ParamNameProp))
		{
			NameProp->SetPropertyValue_InContainer(Expr, FName(*NodeName));
		}
	}

	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("AddExpression: Added '%s' (%s) at (%d,%d)"),
		*NodeName, *ExpressionClass, PosX, PosY);

	return Expr;
}

bool UMCTMaterialBuilder::SetExpressionProperty(
	UMaterial* Material,
	const FString& NodeName,
	const FString& PropertyName,
	const FString& Value)
{
	if (!Material) return false;

	UMaterialExpression* Expr = FindExpressionByName(Material, NodeName);
	if (!Expr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("SetExpressionProperty: Node '%s' not found"), *NodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetExpressionProp", "AI: Set Expression Property"));

	bool bSuccess = SetPropertyByPath(Expr, PropertyName, Value);
	if (bSuccess)
	{
		Material->MarkPackageDirty();
	}
	else
	{
		UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("SetExpressionProperty: Failed '%s'='%s' on node '%s'"),
			*PropertyName, *Value, *NodeName);
	}
	return bSuccess;
}

bool UMCTMaterialBuilder::ConnectExpressions(
	UMaterial* Material,
	const FString& FromNodeName,
	const FString& FromOutput,
	const FString& ToNodeName,
	const FString& ToInput)
{
	if (!Material) return false;

	UMaterialExpression* FromExpr = FindExpressionByName(Material, FromNodeName);
	if (!FromExpr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("ConnectExpressions: From node '%s' not found"), *FromNodeName);
		return false;
	}

	UMaterialExpression* ToExpr = FindExpressionByName(Material, ToNodeName);
	if (!ToExpr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("ConnectExpressions: To node '%s' not found"), *ToNodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIConnectExpressions", "AI: Connect Material Expressions"));

	UMaterialEditingLibrary::ConnectMaterialExpressions(
		FromExpr, FromOutput, ToExpr, ToInput);

	Material->MarkPackageDirty();
	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("ConnectExpressions: '%s'.%s -> '%s'.%s"),
		*FromNodeName, *FromOutput, *ToNodeName, *ToInput);

	return true;
}

bool UMCTMaterialBuilder::ConnectToMaterialProperty(
	UMaterial* Material,
	const FString& FromNodeName,
	const FString& FromOutput,
	const FString& MaterialProperty)
{
	if (!Material) return false;

	UMaterialExpression* FromExpr = FindExpressionByName(Material, FromNodeName);
	if (!FromExpr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("ConnectToMaterialProperty: Node '%s' not found"), *FromNodeName);
		return false;
	}

	EMaterialProperty Prop = ResolveMaterialProperty(MaterialProperty);
	if (Prop == MP_MAX)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("ConnectToMaterialProperty: Unknown property '%s'"), *MaterialProperty);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIConnectToMaterialProp", "AI: Connect To Material Property"));

	bool bResult = UMaterialEditingLibrary::ConnectMaterialProperty(FromExpr, FromOutput, Prop);

	Material->MarkPackageDirty();
	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("ConnectToMaterialProperty: '%s'.%s -> %s (result=%s)"),
		*FromNodeName, *FromOutput, *MaterialProperty, bResult ? TEXT("true") : TEXT("false"));

	return bResult;
}

bool UMCTMaterialBuilder::DisconnectInput(
	UMaterial* Material,
	const FString& NodeName,
	const FString& InputName)
{
	if (!Material) return false;

	UMaterialExpression* Expr = FindExpressionByName(Material, NodeName);
	if (!Expr) return false;

	FScopedTransaction Transaction(LOCTEXT("AIDisconnectInput", "AI: Disconnect Material Input"));

	for (FExpressionInputIterator It{Expr}; It; ++It)
	{
		FString ExprInputName = Expr->GetInputName(It.Index).ToString();
		if (ExprInputName.Equals(InputName, ESearchCase::IgnoreCase))
		{
			It.Input->Expression = nullptr;
			It.Input->OutputIndex = 0;
			Material->MarkPackageDirty();
			return true;
		}
	}

	UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("DisconnectInput: Input '%s' not found on node '%s'"),
		*InputName, *NodeName);
	return false;
}

bool UMCTMaterialBuilder::RemoveExpression(
	UMaterial* Material,
	const FString& NodeName)
{
	if (!Material) return false;

	UMaterialExpression* Expr = FindExpressionByName(Material, NodeName);
	if (!Expr)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("RemoveExpression: Node '%s' not found"), *NodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveExpression", "AI: Remove Material Expression"));

	UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
	Material->MarkPackageDirty();

	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("RemoveExpression: Removed '%s'"), *NodeName);
	return true;
}

// =============================================================================
// MATERIAL INSTANCE
// =============================================================================

UMaterialInstanceConstant* UMCTMaterialBuilder::CreateMaterialInstance(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentMaterialPath)
{
	FString FullPath = PackagePath / AssetName;

	if (UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(nullptr, *FullPath))
	{
		UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("CreateMaterialInstance: Already exists at '%s'"), *FullPath);
		return Existing;
	}

	// Load parent — can be UMaterial or UMaterialInterface
	UMaterialInterface* ParentMaterial = LoadObject<UMaterialInterface>(nullptr, *ParentMaterialPath);
	if (!ParentMaterial)
	{
		// Try with .AssetName suffix
		FString ParentAssetName = FPaths::GetCleanFilename(ParentMaterialPath);
		FString FullParentPath = ParentMaterialPath + TEXT(".") + ParentAssetName;
		ParentMaterial = LoadObject<UMaterialInterface>(nullptr, *FullParentPath);
	}
	if (!ParentMaterial)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CreateMaterialInstance: Parent material not found: '%s'"), *ParentMaterialPath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package) return nullptr;

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(
		Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn));

	if (!MIC)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("CreateMaterialInstance: Factory failed"));
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(MIC);
	MIC->MarkPackageDirty();

	UE_LOG(LogMCTMaterialBuilder, Log, TEXT("CreateMaterialInstance: Created '%s' with parent '%s'"),
		*FullPath, *ParentMaterialPath);
	return MIC;
}

bool UMCTMaterialBuilder::SetInstanceParameter(
	UMaterialInstanceConstant* MIC,
	const FString& ParamName,
	const FString& ParamType,
	const FString& Value)
{
	if (!MIC) return false;

	FScopedTransaction Transaction(LOCTEXT("AISetInstanceParam", "AI: Set Instance Parameter"));

	FMaterialParameterInfo ParamInfo{FName(*ParamName)};

	if (ParamType.Equals(TEXT("scalar"), ESearchCase::IgnoreCase))
	{
		float ScalarValue = FCString::Atof(*Value);
		MIC->SetScalarParameterValueEditorOnly(ParamInfo, ScalarValue);
		MIC->MarkPackageDirty();
		return true;
	}
	else if (ParamType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
	{
		FLinearColor Color;
		if (Color.InitFromString(Value))
		{
			MIC->SetVectorParameterValueEditorOnly(ParamInfo, Color);
			MIC->MarkPackageDirty();
			return true;
		}
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("SetInstanceParameter: Failed to parse vector value '%s'"), *Value);
		return false;
	}
	else if (ParamType.Equals(TEXT("texture"), ESearchCase::IgnoreCase))
	{
		UTexture* Texture = LoadObject<UTexture>(nullptr, *Value);
		if (!Texture)
		{
			UE_LOG(LogMCTMaterialBuilder, Error, TEXT("SetInstanceParameter: Texture not found: '%s'"), *Value);
			return false;
		}
		MIC->SetTextureParameterValueEditorOnly(ParamInfo, Texture);
		MIC->MarkPackageDirty();
		return true;
	}

	UE_LOG(LogMCTMaterialBuilder, Error, TEXT("SetInstanceParameter: Unknown param_type '%s'. Use 'scalar', 'vector', or 'texture'"), *ParamType);
	return false;
}

bool UMCTMaterialBuilder::SaveMaterialInstance(UMaterialInstanceConstant* MIC)
{
	if (!MIC) return false;

	MIC->PreEditChange(nullptr);
	MIC->PostEditChange();

	return SaveAsset(MIC);
}

UMaterialInstanceConstant* UMCTMaterialBuilder::LoadMaterialInstance(const FString& AssetPath)
{
	FString CleanPath = AssetPath;
	if (!CleanPath.Contains(TEXT(".")))
	{
		FString AssetName = FPaths::GetCleanFilename(CleanPath);
		CleanPath = CleanPath + TEXT(".") + AssetName;
	}

	UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *CleanPath);
	if (!MIC)
	{
		MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath);
	}
	if (!MIC)
	{
		UE_LOG(LogMCTMaterialBuilder, Error, TEXT("LoadMaterialInstance: Not found at '%s'"), *AssetPath);
	}
	return MIC;
}

TSharedPtr<FJsonObject> UMCTMaterialBuilder::GetMaterialInstanceInfoAsJson(UMaterialInstanceConstant* MIC)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!MIC)
	{
		Result->SetStringField(TEXT("error"), TEXT("MIC is null"));
		return Result;
	}

	Result->SetStringField(TEXT("asset_path"), MIC->GetPathName());
	if (MIC->Parent)
	{
		Result->SetStringField(TEXT("parent"), MIC->Parent->GetPathName());
	}

	// Scalar parameters
	TArray<TSharedPtr<FJsonValue>> ScalarArray;
	for (const auto& Param : MIC->ScalarParameterValues)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		P->SetNumberField(TEXT("value"), Param.ParameterValue);
		ScalarArray.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("scalar_parameters"), ScalarArray);

	// Vector parameters
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	for (const auto& Param : MIC->VectorParameterValues)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		P->SetStringField(TEXT("value"), Param.ParameterValue.ToString());
		VectorArray.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("vector_parameters"), VectorArray);

	// Texture parameters
	TArray<TSharedPtr<FJsonValue>> TexArray;
	for (const auto& Param : MIC->TextureParameterValues)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		P->SetStringField(TEXT("value"), Param.ParameterValue ? Param.ParameterValue->GetPathName() : TEXT("None"));
		TexArray.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("texture_parameters"), TexArray);

	return Result;
}

// =============================================================================
// UTILITY — EXPRESSION LOOKUP
// =============================================================================

UMaterialExpression* UMCTMaterialBuilder::FindExpressionByName(
	UMaterial* Material,
	const FString& NodeName)
{
	if (!Material) return nullptr;

	TArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressionCollection().Expressions;

	// Pass 1: Search by Desc field (primary storage)
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && Expr->Desc == NodeName)
		{
			return Expr;
		}
	}

	// Pass 2: Search by ParameterName (for parameter expressions)
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;
		FProperty* ParamProp = Expr->GetClass()->FindPropertyByName(FName(TEXT("ParameterName")));
		if (FNameProperty* NameProp = CastField<FNameProperty>(ParamProp))
		{
			FName ParamName = NameProp->GetPropertyValue_InContainer(Expr);
			if (ParamName.ToString() == NodeName)
			{
				return Expr;
			}
		}
	}

	// Pass 3: Auto-generated name
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && Expr->GetName() == NodeName)
		{
			return Expr;
		}
	}

	return nullptr;
}

UClass* UMCTMaterialBuilder::ResolveExpressionClass(const FString& ClassNameOrPath)
{
	// Full path support
	if (ClassNameOrPath.StartsWith(TEXT("/")))
	{
		return LoadObject<UClass>(nullptr, *ClassNameOrPath);
	}

	TMap<FString, UClass*>& Map = GetExpressionClassMap();

	if (UClass** Found = Map.Find(ClassNameOrPath))
	{
		return *Found;
	}

	// Case-insensitive fallback
	for (const auto& Pair : Map)
	{
		if (Pair.Key.Equals(ClassNameOrPath, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("ResolveExpressionClass: Cannot resolve '%s'"), *ClassNameOrPath);
	return nullptr;
}

TMap<FString, UClass*>& UMCTMaterialBuilder::GetExpressionClassMap()
{
	static TMap<FString, UClass*> ClassMap;

	if (ClassMap.Num() == 0)
	{
		// Enumerate all UMaterialExpression subclasses
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UMaterialExpression::StaticClass())) continue;
			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;

			FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_"))) continue;

			ClassMap.Add(ClassName, Class);

			// Strip "MaterialExpression" prefix as alias
			FString ShortName = ClassName;
			if (ShortName.RemoveFromStart(TEXT("MaterialExpression")))
			{
				if (!ClassMap.Contains(ShortName))
			{
				ClassMap.Add(ShortName, Class);
			}
			}
		}

		// Explicit aliases for convenience
		auto AddAlias = [&](const FString& Alias, const FString& FullName)
		{
			if (UClass** Found = ClassMap.Find(FullName))
			{
				ClassMap.Add(Alias, *Found);
			}
		};

		AddAlias(TEXT("Color"),          TEXT("MaterialExpressionConstant3Vector"));
		AddAlias(TEXT("Constant3"),      TEXT("MaterialExpressionConstant3Vector"));
		AddAlias(TEXT("Constant4"),      TEXT("MaterialExpressionConstant4Vector"));
		AddAlias(TEXT("ScalarParam"),    TEXT("MaterialExpressionScalarParameter"));
		AddAlias(TEXT("VectorParam"),    TEXT("MaterialExpressionVectorParameter"));
		AddAlias(TEXT("Texture"),        TEXT("MaterialExpressionTextureSample"));
		AddAlias(TEXT("UV"),             TEXT("MaterialExpressionTextureCoordinate"));
		AddAlias(TEXT("TexCoord"),       TEXT("MaterialExpressionTextureCoordinate"));
		AddAlias(TEXT("Lerp"),           TEXT("MaterialExpressionLinearInterpolate"));
		AddAlias(TEXT("1-x"),            TEXT("MaterialExpressionOneMinus"));
		AddAlias(TEXT("Mask"),           TEXT("MaterialExpressionComponentMask"));
		AddAlias(TEXT("Dot"),            TEXT("MaterialExpressionDotProduct"));
		AddAlias(TEXT("Cross"),          TEXT("MaterialExpressionCrossProduct"));
		AddAlias(TEXT("WorldPos"),       TEXT("MaterialExpressionWorldPosition"));
		AddAlias(TEXT("CameraVector"),   TEXT("MaterialExpressionCameraVectorWS"));
		AddAlias(TEXT("ObjectPos"),      TEXT("MaterialExpressionObjectPositionWS"));
		AddAlias(TEXT("PixelNormal"),    TEXT("MaterialExpressionPixelNormalWS"));
		AddAlias(TEXT("StaticSwitch"),   TEXT("MaterialExpressionStaticSwitch"));
		AddAlias(TEXT("StaticBool"),     TEXT("MaterialExpressionStaticBoolParameter"));
		AddAlias(TEXT("Sin"),            TEXT("MaterialExpressionSine"));
		AddAlias(TEXT("Cos"),            TEXT("MaterialExpressionCosine"));
	}

	return ClassMap;
}

TArray<FString> UMCTMaterialBuilder::GetAvailableExpressionClasses()
{
	TArray<FString> Result;
	TMap<FString, UClass*>& Map = GetExpressionClassMap();
	Map.GetKeys(Result);
	Result.Sort();
	return Result;
}

EMaterialProperty UMCTMaterialBuilder::ResolveMaterialProperty(const FString& PropertyName)
{
	static TMap<FString, EMaterialProperty> PropMap;
	if (PropMap.Num() == 0)
	{
		PropMap.Add(TEXT("BaseColor"),             MP_BaseColor);
		PropMap.Add(TEXT("Metallic"),              MP_Metallic);
		PropMap.Add(TEXT("Specular"),              MP_Specular);
		PropMap.Add(TEXT("Roughness"),             MP_Roughness);
		PropMap.Add(TEXT("Anisotropy"),            MP_Anisotropy);
		PropMap.Add(TEXT("EmissiveColor"),         MP_EmissiveColor);
		PropMap.Add(TEXT("Opacity"),               MP_Opacity);
		PropMap.Add(TEXT("OpacityMask"),           MP_OpacityMask);
		PropMap.Add(TEXT("Normal"),                MP_Normal);
		PropMap.Add(TEXT("Tangent"),               MP_Tangent);
		PropMap.Add(TEXT("WorldPositionOffset"),   MP_WorldPositionOffset);
		PropMap.Add(TEXT("SubsurfaceColor"),       MP_SubsurfaceColor);
		PropMap.Add(TEXT("CustomData0"),           MP_CustomData0);
		PropMap.Add(TEXT("CustomData1"),           MP_CustomData1);
		PropMap.Add(TEXT("AmbientOcclusion"),      MP_AmbientOcclusion);
		PropMap.Add(TEXT("Refraction"),            MP_Refraction);
		PropMap.Add(TEXT("PixelDepthOffset"),      MP_PixelDepthOffset);
		PropMap.Add(TEXT("ShadingModel"),          MP_ShadingModel);

		// Aliases
		PropMap.Add(TEXT("Emissive"),              MP_EmissiveColor);
		PropMap.Add(TEXT("WPO"),                   MP_WorldPositionOffset);
		PropMap.Add(TEXT("AO"),                    MP_AmbientOcclusion);
		PropMap.Add(TEXT("PDO"),                   MP_PixelDepthOffset);
	}

	const EMaterialProperty* Found = PropMap.Find(PropertyName);
	return Found ? *Found : MP_MAX;
}

// =============================================================================
// GET MATERIAL GRAPH AS JSON
// =============================================================================

TSharedPtr<FJsonObject> UMCTMaterialBuilder::ExpressionToJson(UMaterialExpression* Expr, int32 Index)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Expr) return Obj;

	FString Name = Expr->Desc.IsEmpty() ? Expr->GetName() : Expr->Desc;
	Obj->SetStringField(TEXT("name"), Name);
	Obj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
	Obj->SetNumberField(TEXT("index"), Index);
	Obj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
	Obj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);

	// If parameter, include parameter name
	if (FProperty* ParamProp = Expr->GetClass()->FindPropertyByName(FName(TEXT("ParameterName"))))
	{
		if (FNameProperty* NameProp = CastField<FNameProperty>(ParamProp))
		{
			FName ParamName = NameProp->GetPropertyValue_InContainer(Expr);
			Obj->SetStringField(TEXT("parameter_name"), ParamName.ToString());
		}
	}

	// Outputs
	TArray<TSharedPtr<FJsonValue>> OutputsArr;
	const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	for (int32 i = 0; i < Outputs.Num(); ++i)
	{
		TSharedPtr<FJsonObject> OutObj = MakeShared<FJsonObject>();
		OutObj->SetNumberField(TEXT("index"), i);
		OutObj->SetStringField(TEXT("name"), Outputs[i].OutputName.ToString());
		OutputsArr.Add(MakeShared<FJsonValueObject>(OutObj));
	}
	Obj->SetArrayField(TEXT("outputs"), OutputsArr);

	return Obj;
}

TSharedPtr<FJsonObject> UMCTMaterialBuilder::GetMaterialGraphAsJson(UMaterial* Material)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), TEXT("Material is null"));
		return Result;
	}

	Result->SetStringField(TEXT("asset_path"), Material->GetPathName());

	// Domain
	UEnum* DomainEnum = StaticEnum<EMaterialDomain>();
	if (DomainEnum)
	{
		Result->SetStringField(TEXT("domain"),
			DomainEnum->GetNameStringByValue((int64)Material->MaterialDomain));
	}

	// BlendMode
	UEnum* BlendEnum = StaticEnum<EBlendMode>();
	if (BlendEnum)
	{
		Result->SetStringField(TEXT("blend_mode"),
			BlendEnum->GetNameStringByValue((int64)Material->BlendMode));
	}

	Result->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());

	// Expressions
	TArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressionCollection().Expressions;
	TArray<TSharedPtr<FJsonValue>> ExprArray;

	for (int32 i = 0; i < Expressions.Num(); ++i)
	{
		if (Expressions[i])
		{
			ExprArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(Expressions[i], i)));
		}
	}
	Result->SetArrayField(TEXT("expressions"), ExprArray);

	// Connections
	TArray<TSharedPtr<FJsonValue>> ConnArray;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;
		FString ToName = Expr->Desc.IsEmpty() ? Expr->GetName() : Expr->Desc;
		for (FExpressionInputIterator It{Expr}; It; ++It)
		{
			if (It.Input && It.Input->Expression)
			{
				TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
				FString FromName = It.Input->Expression->Desc.IsEmpty()
					? It.Input->Expression->GetName() : It.Input->Expression->Desc;
				Conn->SetStringField(TEXT("from"), FromName);
				Conn->SetNumberField(TEXT("from_output"), It.Input->OutputIndex);
				Conn->SetStringField(TEXT("to"), ToName);
				Conn->SetStringField(TEXT("to_input"), Expr->GetInputName(It.Index).ToString());
				ConnArray.Add(MakeShared<FJsonValueObject>(Conn));
			}
		}
	}
	Result->SetArrayField(TEXT("connections"), ConnArray);

	return Result;
}

// =============================================================================
// PRIVATE HELPERS
// =============================================================================

bool UMCTMaterialBuilder::SetPropertyByPath(
	UObject* Object,
	const FString& PropertyPath,
	const FString& Value)
{
	if (!Object || PropertyPath.IsEmpty())
	{
		return false;
	}

	// Split dot-notation path
	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Object->GetClass();
	void* CurrentContainer = Object;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			UE_LOG(LogMCTMaterialBuilder, Warning, TEXT("SetPropertyByPath: Property '%s' not found on %s"),
				*PathParts[i], *CurrentStruct->GetName());
			return false;
		}

		if (i == PathParts.Num() - 1)
		{
			// Leaf property — set the value using ImportText
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);
			const TCHAR* ValueCStr = *Value;
			const TCHAR* Result = Prop->ImportText_Direct(ValueCStr, ValuePtr, Object, PPF_None);
			if (Result != nullptr)
			{
				return true;
			}

			// Fallback: handle enum properties with alternative name formats
			FByteProperty* ByteProp = CastField<FByteProperty>(Prop);
			FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop);
			UEnum* Enum = nullptr;
			if (ByteProp && ByteProp->Enum)
			{
				Enum = ByteProp->Enum;
			}
			else if (EnumProp)
			{
				Enum = EnumProp->GetEnum();
			}

			if (Enum)
			{
				FString CleanValue = Value;

				// Strip enum class prefix: "EHorizontalAlignment::Center" -> "Center"
				int32 ColonIdx;
				if (CleanValue.FindLastChar(TEXT(':'), ColonIdx))
				{
					CleanValue = CleanValue.Mid(ColonIdx + 1);
				}

				for (int32 EnumIdx = 0; EnumIdx < Enum->NumEnums() - 1; ++EnumIdx)
				{
					FString EnumName = Enum->GetNameStringByIndex(EnumIdx);
					FString DisplayName = Enum->GetDisplayNameTextByIndex(EnumIdx).ToString();

					if (EnumName.Equals(CleanValue, ESearchCase::IgnoreCase) ||
						DisplayName.Equals(CleanValue, ESearchCase::IgnoreCase))
					{
						int64 EnumValue = Enum->GetValueByIndex(EnumIdx);
						if (ByteProp)
						{
							ByteProp->SetPropertyValue_InContainer(CurrentContainer, (uint8)EnumValue);
						}
						else if (EnumProp)
						{
							FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
							if (UnderlyingProp)
							{
								UnderlyingProp->SetIntPropertyValue(
									Prop->ContainerPtrToValuePtr<void>(CurrentContainer), EnumValue);
							}
						}
						UE_LOG(LogMCTMaterialBuilder, Verbose,
							TEXT("SetPropertyByPath: Enum fallback matched '%s' -> '%s' = %lld"),
							*Value, *EnumName, EnumValue);
						return true;
					}
				}

				UE_LOG(LogMCTMaterialBuilder, Warning,
					TEXT("SetPropertyByPath: Enum '%s' has no value matching '%s'"),
					*Enum->GetName(), *Value);
			}

			return false;
		}
		else
		{
			// Navigate into struct property
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp)
			{
				UE_LOG(LogMCTMaterialBuilder, Warning,
					TEXT("SetPropertyByPath: '%s' is not a struct, cannot navigate further"),
					*PathParts[i]);
				return false;
			}

			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
	}

	return false;
}

bool UMCTMaterialBuilder::SaveAsset(UObject* Asset)
{
	if (!Asset) return false;

	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;

	FString PackageFilename;
	FString PackagePath;

	if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
	{
		PackagePath = PackageFilename;
	}
	else
	{
		PackagePath = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *PackagePath, SaveArgs);
}

#undef LOCTEXT_NAMESPACE
