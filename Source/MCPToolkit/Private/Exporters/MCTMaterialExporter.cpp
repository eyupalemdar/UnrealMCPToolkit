// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTMaterialExporter.h"

// Material classes
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionNamedReroute.h"

bool UMCTMaterialExporter::CanExport(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	return Asset->IsA<UMaterial>() ||
	       Asset->IsA<UMaterialInstance>();
}

TArray<UClass*> UMCTMaterialExporter::GetSupportedClasses() const
{
	return {
		UMaterial::StaticClass(),
		UMaterialInstance::StaticClass(),
		UMaterialInstanceConstant::StaticClass(),
		UMaterialInstanceDynamic::StaticClass()
	};
}

FString UMCTMaterialExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	// Check Material first (before MaterialInstance, since Material is not a MaterialInstance)
	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		return ExportMaterial(Material, bFilterDefaults);
	}
	else if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Asset))
	{
		return ExportMaterialInstance(MaterialInstance, bFilterDefaults);
	}

	return TEXT("Error: Unsupported material asset type\n");
}

//////////////////////////////////////////////////////////////////////////
// Material Export

FString UMCTMaterialExporter::ExportMaterial(UMaterial* Material, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("MATERIAL: %s"), *Material->GetName()));
	Output += FString::Printf(TEXT("Path: %s\n"), *Material->GetPathName());

	// Metadata
	Output += ExportMaterialMetadata(Material);

	// Settings
	Output += TEXT("\n");
	Output += ExportMaterialSettings(Material);

	// Parameters
	Output += TEXT("\n");
	Output += ExportMaterialParameters(Material, true);

	// Root connections
	Output += TEXT("\n");
	Output += ExportRootConnections(Material);

	// Full graph
	Output += TEXT("\n");
	Output += ExportMaterialGraph(Material);

	return Output;
}

FString UMCTMaterialExporter::ExportMaterialMetadata(UMaterial* Material)
{
	FString Output;

	// Material Domain
	FString DomainStr;
	switch (Material->MaterialDomain)
	{
		case MD_Surface: DomainStr = TEXT("Surface"); break;
		case MD_DeferredDecal: DomainStr = TEXT("DeferredDecal"); break;
		case MD_LightFunction: DomainStr = TEXT("LightFunction"); break;
		case MD_PostProcess: DomainStr = TEXT("PostProcess"); break;
		case MD_UI: DomainStr = TEXT("UI"); break;
		case MD_Volume: DomainStr = TEXT("Volume"); break;
		default: DomainStr = TEXT("Unknown"); break;
	}
	Output += FString::Printf(TEXT("Domain: %s\n"), *DomainStr);

	// Blend Mode
	FString BlendModeStr;
	switch (Material->BlendMode)
	{
		case BLEND_Opaque: BlendModeStr = TEXT("Opaque"); break;
		case BLEND_Masked: BlendModeStr = TEXT("Masked"); break;
		case BLEND_Translucent: BlendModeStr = TEXT("Translucent"); break;
		case BLEND_Additive: BlendModeStr = TEXT("Additive"); break;
		case BLEND_Modulate: BlendModeStr = TEXT("Modulate"); break;
		case BLEND_AlphaComposite: BlendModeStr = TEXT("AlphaComposite"); break;
		case BLEND_AlphaHoldout: BlendModeStr = TEXT("AlphaHoldout"); break;
		default: BlendModeStr = TEXT("Unknown"); break;
	}
	Output += FString::Printf(TEXT("BlendMode: %s\n"), *BlendModeStr);

	// Shading Model
	FString ShadingModelStr;
	switch (Material->GetShadingModels().GetFirstShadingModel())
	{
		case MSM_Unlit: ShadingModelStr = TEXT("Unlit"); break;
		case MSM_DefaultLit: ShadingModelStr = TEXT("DefaultLit"); break;
		case MSM_Subsurface: ShadingModelStr = TEXT("Subsurface"); break;
		case MSM_PreintegratedSkin: ShadingModelStr = TEXT("PreintegratedSkin"); break;
		case MSM_ClearCoat: ShadingModelStr = TEXT("ClearCoat"); break;
		case MSM_SubsurfaceProfile: ShadingModelStr = TEXT("SubsurfaceProfile"); break;
		case MSM_TwoSidedFoliage: ShadingModelStr = TEXT("TwoSidedFoliage"); break;
		case MSM_Hair: ShadingModelStr = TEXT("Hair"); break;
		case MSM_Cloth: ShadingModelStr = TEXT("Cloth"); break;
		case MSM_Eye: ShadingModelStr = TEXT("Eye"); break;
		case MSM_SingleLayerWater: ShadingModelStr = TEXT("SingleLayerWater"); break;
		case MSM_ThinTranslucent: ShadingModelStr = TEXT("ThinTranslucent"); break;
		default: ShadingModelStr = TEXT("Unknown"); break;
	}
	Output += FString::Printf(TEXT("ShadingModel: %s\n"), *ShadingModelStr);

	Output += FString::Printf(TEXT("TwoSided: %s\n"), Material->IsTwoSided() ? TEXT("True") : TEXT("False"));

	return Output;
}

FString UMCTMaterialExporter::ExportMaterialSettings(UMaterial* Material)
{
	FString Output;

	Output += MakeSubsectionHeader(TEXT("Material Settings"));

	Output += FString::Printf(TEXT("bUsedWithStaticLighting: %s\n"), Material->bUsedWithStaticLighting ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bUsedWithSkeletalMesh: %s\n"), Material->bUsedWithSkeletalMesh ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bUsedWithParticleSprites: %s\n"), Material->bUsedWithParticleSprites ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bUsedWithMeshParticles: %s\n"), Material->bUsedWithMeshParticles ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bUsedWithNiagaraSprites: %s\n"), Material->bUsedWithNiagaraSprites ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bUsedWithNiagaraMeshParticles: %s\n"), Material->bUsedWithNiagaraMeshParticles ? TEXT("True") : TEXT("False"));

	if (Material->BlendMode == BLEND_Masked)
	{
		Output += FString::Printf(TEXT("OpacityMaskClipValue: %.3f\n"), Material->OpacityMaskClipValue);
	}

	return Output;
}

FString UMCTMaterialExporter::ExportMaterialParameters(UMaterialInterface* Material, bool bIncludeDefaults)
{
	FString Output;

	// Scalar Parameters
	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FGuid> ScalarGuids;
	Material->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);

	Output += MakeSubsectionHeader(TEXT("Scalar Parameters"));
	if (ScalarParams.Num() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (const FMaterialParameterInfo& Info : ScalarParams)
		{
			float Value = 0.0f;
			Material->GetScalarParameterValue(Info, Value);
			Output += FString::Printf(TEXT("%s = %.4f\n"), *Info.Name.ToString(), Value);
		}
	}

	// Vector Parameters
	TArray<FMaterialParameterInfo> VectorParams;
	TArray<FGuid> VectorGuids;
	Material->GetAllVectorParameterInfo(VectorParams, VectorGuids);

	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Vector Parameters"));
	if (VectorParams.Num() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (const FMaterialParameterInfo& Info : VectorParams)
		{
			FLinearColor Value = FLinearColor::White;
			Material->GetVectorParameterValue(Info, Value);
			Output += FString::Printf(TEXT("%s = (%.3f, %.3f, %.3f, %.3f)\n"),
				*Info.Name.ToString(), Value.R, Value.G, Value.B, Value.A);
		}
	}

	// Texture Parameters
	TArray<FMaterialParameterInfo> TextureParams;
	TArray<FGuid> TextureGuids;
	Material->GetAllTextureParameterInfo(TextureParams, TextureGuids);

	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Texture Parameters"));
	if (TextureParams.Num() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (const FMaterialParameterInfo& Info : TextureParams)
		{
			UTexture* Texture = nullptr;
			Material->GetTextureParameterValue(Info, Texture);
			Output += FString::Printf(TEXT("%s = %s\n"),
				*Info.Name.ToString(),
				Texture ? *Texture->GetPathName() : TEXT("None"));
		}
	}

	// Static Switch Parameters
	TArray<FMaterialParameterInfo> StaticSwitchParams;
	TArray<FGuid> StaticSwitchGuids;
	Material->GetAllStaticSwitchParameterInfo(StaticSwitchParams, StaticSwitchGuids);

	if (StaticSwitchParams.Num() > 0)
	{
		Output += TEXT("\n");
		Output += MakeSubsectionHeader(TEXT("Static Switch Parameters"));
		for (const FMaterialParameterInfo& Info : StaticSwitchParams)
		{
			bool Value = false;
			FGuid OutGuid;
			Material->GetStaticSwitchParameterValue(Info, Value, OutGuid);
			Output += FString::Printf(TEXT("%s = %s\n"),
				*Info.Name.ToString(),
				Value ? TEXT("True") : TEXT("False"));
		}
	}

	return Output;
}

FString UMCTMaterialExporter::ExportRootConnections(UMaterial* Material)
{
	FString Output;

	Output += MakeSubsectionHeader(TEXT("Root Connections"));

#if WITH_EDITORONLY_DATA
	// Helper lambda to format connection info from FExpressionInput pointer
	auto FormatConnection = [](const FExpressionInput* Input) -> FString
	{
		if (!Input || !Input->Expression)
		{
			return TEXT("Not Connected");
		}

		FString ExprName = Input->Expression->GetName();
		FString OutputName;

		// Get output name if available
		if (Input->OutputIndex < Input->Expression->GetOutputs().Num())
		{
			const FExpressionOutput& ExprOutput = Input->Expression->GetOutputs()[Input->OutputIndex];
			if (!ExprOutput.OutputName.IsNone())
			{
				OutputName = TEXT(".") + ExprOutput.OutputName.ToString();
			}
		}

		// Check for mask
		FString MaskStr;
		if (Input->Mask)
		{
			MaskStr = TEXT(".");
			if (Input->MaskR) MaskStr += TEXT("R");
			if (Input->MaskG) MaskStr += TEXT("G");
			if (Input->MaskB) MaskStr += TEXT("B");
			if (Input->MaskA) MaskStr += TEXT("A");
		}

		return FString::Printf(TEXT("%s%s%s"), *ExprName, *OutputName, *MaskStr);
	};

	// Helper lambda to export a property connection
	auto ExportProperty = [&](EMaterialProperty Property, const TCHAR* PropertyName) -> void
	{
		if (Material->IsPropertyConnected(Property))
		{
			const FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
			Output += FString::Printf(TEXT("%s <- %s\n"), PropertyName, *FormatConnection(Input));
		}
	};

	// Export each root property connection with the actual connected expression
	ExportProperty(MP_BaseColor, TEXT("BaseColor"));
	ExportProperty(MP_Metallic, TEXT("Metallic"));
	ExportProperty(MP_Specular, TEXT("Specular"));
	ExportProperty(MP_Roughness, TEXT("Roughness"));
	ExportProperty(MP_Normal, TEXT("Normal"));
	ExportProperty(MP_EmissiveColor, TEXT("EmissiveColor"));
	ExportProperty(MP_Opacity, TEXT("Opacity"));
	ExportProperty(MP_OpacityMask, TEXT("OpacityMask"));
	ExportProperty(MP_AmbientOcclusion, TEXT("AmbientOcclusion"));
	ExportProperty(MP_Refraction, TEXT("Refraction"));
	ExportProperty(MP_WorldPositionOffset, TEXT("WorldPositionOffset"));
	ExportProperty(MP_SubsurfaceColor, TEXT("SubsurfaceColor"));
	ExportProperty(MP_Tangent, TEXT("Tangent"));
	ExportProperty(MP_Anisotropy, TEXT("Anisotropy"));
	ExportProperty(MP_PixelDepthOffset, TEXT("PixelDepthOffset"));
	ExportProperty(MP_ShadingModel, TEXT("ShadingModel"));
#endif

	return Output;
}

FString UMCTMaterialExporter::ExportMaterialGraph(UMaterial* Material)
{
	FString Output;

	Output += MakeSectionHeader(TEXT("MATERIAL GRAPH"));

#if WITH_EDITORONLY_DATA
	auto Expressions = Material->GetExpressions();

	Output += FString::Printf(TEXT("ExpressionCount: %d\n\n"), Expressions.Num());

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;

		Output += ExportMaterialExpression(Expr);
		Output += TEXT("\n");
	}
#else
	Output += TEXT("(Material graph only available with editor data)\n");
#endif

	return Output;
}

FString UMCTMaterialExporter::ExportMaterialExpression(UMaterialExpression* Expression)
{
	FString Output;

	FString TypeName = GetExpressionTypeName(Expression);
	FString DisplayName = GetExpressionDisplayName(Expression);

	// Header: [Type] Name "Description"
	Output += FString::Printf(TEXT("[%s] %s"), *TypeName, *Expression->GetName());
	if (!DisplayName.IsEmpty())
	{
		Output += FString::Printf(TEXT(" \"%s\""), *DisplayName);
	}
	Output += TEXT("\n");

	// Expression-specific properties
	Output += ExportExpressionProperties(Expression);

	// Input connections
	Output += ExportExpressionInputs(Expression);

	// Output connections
	Output += ExportExpressionOutputs(Expression);

	return Output;
}

FString UMCTMaterialExporter::GetExpressionTypeName(UMaterialExpression* Expression) const
{
	if (!Expression) return TEXT("Unknown");

	FString ClassName = Expression->GetClass()->GetName();

	// Remove "MaterialExpression" prefix for cleaner output
	if (ClassName.StartsWith(TEXT("MaterialExpression")))
	{
		ClassName = ClassName.RightChop(18); // strlen("MaterialExpression")
	}

	return ClassName;
}

FString UMCTMaterialExporter::GetExpressionDisplayName(UMaterialExpression* Expression) const
{
	if (!Expression) return TEXT("");

#if WITH_EDITORONLY_DATA
	// Check for description/comment
	if (!Expression->Desc.IsEmpty())
	{
		return Expression->Desc;
	}
#endif

	// For parameters, return the parameter name
	if (UMaterialExpressionParameter* ParamExpr = Cast<UMaterialExpressionParameter>(Expression))
	{
		return ParamExpr->ParameterName.ToString();
	}
	if (UMaterialExpressionTextureObjectParameter* TexParam = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
	{
		return TexParam->ParameterName.ToString();
	}

	return TEXT("");
}

FString UMCTMaterialExporter::ExportExpressionInputs(UMaterialExpression* Expression)
{
	FString Output;

#if WITH_EDITORONLY_DATA
	// Use FExpressionInputIterator to iterate through inputs
	int32 InputIndex = 0;
	for (FExpressionInputIterator It(Expression); It; ++It, ++InputIndex)
	{
		FExpressionInput* Input = It.operator->();
		if (!Input) continue;

		FName InputName = Expression->GetInputName(InputIndex);
		if (InputName == NAME_None)
		{
			InputName = FName(*FString::Printf(TEXT("Input%d"), InputIndex));
		}

		if (Input->Expression)
		{
			// Connected to another expression
			FString SourceName = Input->Expression->GetName();
			FString OutputName = TEXT("Output");

			// Get the output name if there are multiple outputs
			FExpressionOutput* SourceOutput = Input->Expression->GetOutput(Input->OutputIndex);
			if (SourceOutput && !SourceOutput->OutputName.IsNone())
			{
				OutputName = SourceOutput->OutputName.ToString();
			}

			// Check for mask
			FString MaskStr;
			if (Input->Mask)
			{
				MaskStr = TEXT(".");
				if (Input->MaskR) MaskStr += TEXT("R");
				if (Input->MaskG) MaskStr += TEXT("G");
				if (Input->MaskB) MaskStr += TEXT("B");
				if (Input->MaskA) MaskStr += TEXT("A");
			}

			Output += FString::Printf(TEXT("  %s <- %s.%s%s\n"),
				*InputName.ToString(), *SourceName, *OutputName, *MaskStr);
		}
	}
#endif

	return Output;
}

FString UMCTMaterialExporter::ExportExpressionOutputs(UMaterialExpression* Expression)
{
	FString Output;

#if WITH_EDITORONLY_DATA
	const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();

	// For each output, we'd need to find what it connects to
	// This requires scanning all expressions to find incoming connections
	// For now, just list the output names

	if (Outputs.Num() > 1)
	{
		Output += TEXT("  Outputs: ");
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			if (i > 0) Output += TEXT(", ");
			FString OutputName = Outputs[i].OutputName.IsNone() ?
				FString::Printf(TEXT("Output%d"), i) : Outputs[i].OutputName.ToString();
			Output += OutputName;
		}
		Output += TEXT("\n");
	}
#endif

	return Output;
}

FString UMCTMaterialExporter::ExportExpressionProperties(UMaterialExpression* Expression)
{
	FString Output;

	// Constant expressions
	if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
	{
		Output += FString::Printf(TEXT("  Value = %.4f\n"), ConstExpr->R);
	}
	else if (UMaterialExpressionConstant2Vector* Const2Expr = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		Output += FString::Printf(TEXT("  Value = (%.4f, %.4f)\n"), Const2Expr->R, Const2Expr->G);
	}
	else if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		Output += FString::Printf(TEXT("  Value = (%.4f, %.4f, %.4f)\n"),
			Const3Expr->Constant.R, Const3Expr->Constant.G, Const3Expr->Constant.B);
	}
	else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		Output += FString::Printf(TEXT("  Value = (%.4f, %.4f, %.4f, %.4f)\n"),
			Const4Expr->Constant.R, Const4Expr->Constant.G, Const4Expr->Constant.B, Const4Expr->Constant.A);
	}
	// Scalar Parameter
	else if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *ScalarParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  DefaultValue = %.4f\n"), ScalarParam->DefaultValue);
	}
	// Vector Parameter
	else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *VectorParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  DefaultValue = (%.4f, %.4f, %.4f, %.4f)\n"),
			VectorParam->DefaultValue.R, VectorParam->DefaultValue.G,
			VectorParam->DefaultValue.B, VectorParam->DefaultValue.A);
	}
	// Texture Object Parameter
	else if (UMaterialExpressionTextureObjectParameter* TexParam = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *TexParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  Texture = %s\n"),
			TexParam->Texture ? *TexParam->Texture->GetPathName() : TEXT("None"));
	}
	// Texture Sample Parameter
	else if (UMaterialExpressionTextureSampleParameter* TexSampleParam = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *TexSampleParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  Texture = %s\n"),
			TexSampleParam->Texture ? *TexSampleParam->Texture->GetPathName() : TEXT("None"));
	}
	// Texture Sample (non-parameter)
	else if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expression))
	{
		Output += FString::Printf(TEXT("  Texture = %s\n"),
			TexSample->Texture ? *TexSample->Texture->GetPathName() : TEXT("(from input)"));
	}
	// Static Bool Parameter
	else if (UMaterialExpressionStaticBoolParameter* StaticBoolParam = Cast<UMaterialExpressionStaticBoolParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *StaticBoolParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  DefaultValue = %s\n"), StaticBoolParam->DefaultValue ? TEXT("True") : TEXT("False"));
	}
	// Static Switch Parameter
	else if (UMaterialExpressionStaticSwitchParameter* StaticSwitchParam = Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
	{
		Output += FString::Printf(TEXT("  ParameterName = %s\n"), *StaticSwitchParam->ParameterName.ToString());
		Output += FString::Printf(TEXT("  DefaultValue = %s\n"), StaticSwitchParam->DefaultValue ? TEXT("True") : TEXT("False"));
	}
	// Component Mask
	else if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
	{
		FString MaskStr;
		if (Mask->R) MaskStr += TEXT("R");
		if (Mask->G) MaskStr += TEXT("G");
		if (Mask->B) MaskStr += TEXT("B");
		if (Mask->A) MaskStr += TEXT("A");
		Output += FString::Printf(TEXT("  Mask = %s\n"), *MaskStr);
	}
	// Texture Coordinate
	else if (UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(Expression))
	{
		Output += FString::Printf(TEXT("  CoordinateIndex = %d\n"), TexCoord->CoordinateIndex);
		Output += FString::Printf(TEXT("  UTiling = %.4f\n"), TexCoord->UTiling);
		Output += FString::Printf(TEXT("  VTiling = %.4f\n"), TexCoord->VTiling);
	}
	// Panner
	else if (UMaterialExpressionPanner* Panner = Cast<UMaterialExpressionPanner>(Expression))
	{
		Output += FString::Printf(TEXT("  SpeedX = %.4f\n"), Panner->SpeedX);
		Output += FString::Printf(TEXT("  SpeedY = %.4f\n"), Panner->SpeedY);
		Output += FString::Printf(TEXT("  bFractionalPart = %s\n"), Panner->bFractionalPart ? TEXT("True") : TEXT("False"));
	}
	// Material Function Call
	else if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
	{
		if (FuncCall->MaterialFunction)
		{
			Output += FString::Printf(TEXT("  MaterialFunction = %s\n"), *FuncCall->MaterialFunction->GetPathName());
		}
		// Function Inputs
		for (int32 i = 0; i < FuncCall->FunctionInputs.Num(); i++)
		{
			const FFunctionExpressionInput& Input = FuncCall->FunctionInputs[i];
			Output += FString::Printf(TEXT("  FunctionInput[%d] = %s\n"), i, *Input.Input.InputName.ToString());
		}
		// Function Outputs
		for (int32 i = 0; i < FuncCall->FunctionOutputs.Num(); i++)
		{
			const FFunctionExpressionOutput& FuncOutput = FuncCall->FunctionOutputs[i];
			Output += FString::Printf(TEXT("  FunctionOutput[%d] = %s\n"), i, *FuncOutput.Output.OutputName.ToString());
		}
	}
	// Step - exports ConstY and ConstX for threshold comparison
	else if (UMaterialExpressionStep* StepExpr = Cast<UMaterialExpressionStep>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstY = %.4f\n"), StepExpr->ConstY);
		Output += FString::Printf(TEXT("  ConstX = %.4f\n"), StepExpr->ConstX);
	}
	// Multiply - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionMultiply* MulExpr = Cast<UMaterialExpressionMultiply>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), MulExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), MulExpr->ConstB);
	}
	// Subtract - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionSubtract* SubExpr = Cast<UMaterialExpressionSubtract>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), SubExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), SubExpr->ConstB);
	}
	// Add - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), AddExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), AddExpr->ConstB);
	}
	// Divide - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionDivide* DivExpr = Cast<UMaterialExpressionDivide>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), DivExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), DivExpr->ConstB);
	}
	// Power - exports ConstExponent for unconnected exponent input
	else if (UMaterialExpressionPower* PowExpr = Cast<UMaterialExpressionPower>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstExponent = %.4f\n"), PowExpr->ConstExponent);
	}
	// SmoothStep - exports ConstMin, ConstMax, ConstValue for unconnected inputs
	else if (UMaterialExpressionSmoothStep* SmoothExpr = Cast<UMaterialExpressionSmoothStep>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstMin = %.4f\n"), SmoothExpr->ConstMin);
		Output += FString::Printf(TEXT("  ConstMax = %.4f\n"), SmoothExpr->ConstMax);
		Output += FString::Printf(TEXT("  ConstValue = %.4f\n"), SmoothExpr->ConstValue);
	}
	// LinearInterpolate - exports ConstA, ConstB, ConstAlpha for unconnected inputs
	else if (UMaterialExpressionLinearInterpolate* LerpExpr = Cast<UMaterialExpressionLinearInterpolate>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), LerpExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), LerpExpr->ConstB);
		Output += FString::Printf(TEXT("  ConstAlpha = %.4f\n"), LerpExpr->ConstAlpha);
	}
	// Clamp - exports MinDefault and MaxDefault for unconnected inputs
	else if (UMaterialExpressionClamp* ClampExpr = Cast<UMaterialExpressionClamp>(Expression))
	{
		Output += FString::Printf(TEXT("  MinDefault = %.4f\n"), ClampExpr->MinDefault);
		Output += FString::Printf(TEXT("  MaxDefault = %.4f\n"), ClampExpr->MaxDefault);
	}
	// Max - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionMax* MaxExpr = Cast<UMaterialExpressionMax>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), MaxExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), MaxExpr->ConstB);
	}
	// Min - exports ConstA and ConstB for unconnected inputs
	else if (UMaterialExpressionMin* MinExpr = Cast<UMaterialExpressionMin>(Expression))
	{
		Output += FString::Printf(TEXT("  ConstA = %.4f\n"), MinExpr->ConstA);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), MinExpr->ConstB);
	}
	// If - exports threshold values
	else if (UMaterialExpressionIf* IfExpr = Cast<UMaterialExpressionIf>(Expression))
	{
		Output += FString::Printf(TEXT("  EqualsThreshold = %.4f\n"), IfExpr->EqualsThreshold);
		Output += FString::Printf(TEXT("  ConstB = %.4f\n"), IfExpr->ConstB);
	}
	// Cosine - exports Period (critical: Cosine uses cos(Input * 2PI / Period))
	else if (UMaterialExpressionCosine* CosExpr = Cast<UMaterialExpressionCosine>(Expression))
	{
		Output += FString::Printf(TEXT("  Period = %.4f\n"), CosExpr->Period);
	}
	// Sine - exports Period (critical: Sine uses sin(Input * 2PI / Period))
	else if (UMaterialExpressionSine* SinExpr = Cast<UMaterialExpressionSine>(Expression))
	{
		Output += FString::Printf(TEXT("  Period = %.4f\n"), SinExpr->Period);
	}
	// NamedRerouteDeclaration - exports the variable name
	if (Expression->GetClass()->GetName().Contains(TEXT("NamedRerouteDeclaration")))
	{
		UMaterialExpressionNamedRerouteDeclaration* DeclExpr = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression);
		if (DeclExpr)
		{
			FString NameStr = DeclExpr->Name.IsNone() ? TEXT("(unnamed)") : DeclExpr->Name.ToString();
			Output += FString::Printf(TEXT("  Name = %s\n"), *NameStr);
		}
		else
		{
			Output += TEXT("  [Cast failed]\n");
		}
	}
	// NamedRerouteUsage - exports which Declaration it references
	else if (Expression->GetClass()->GetName().Contains(TEXT("NamedRerouteUsage")))
	{
		UMaterialExpressionNamedRerouteUsage* UsageExpr = Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
		if (UsageExpr)
		{
			if (UsageExpr->Declaration)
			{
				FString NameStr = UsageExpr->Declaration->Name.IsNone() ? TEXT("(unnamed)") : UsageExpr->Declaration->Name.ToString();
				Output += FString::Printf(TEXT("  Declaration = %s\n"), *UsageExpr->Declaration->GetName());
				Output += FString::Printf(TEXT("  VariableName = %s\n"), *NameStr);
			}
			else
			{
				Output += FString::Printf(TEXT("  DeclarationGuid = %s\n"), *UsageExpr->DeclarationGuid.ToString());
			}
		}
		else
		{
			Output += TEXT("  [Cast failed]\n");
		}
	}

	return Output;
}

FString UMCTMaterialExporter::GetMaterialPropertyName(int32 PropertyIndex) const
{
	switch (PropertyIndex)
	{
		case MP_BaseColor: return TEXT("BaseColor");
		case MP_Metallic: return TEXT("Metallic");
		case MP_Specular: return TEXT("Specular");
		case MP_Roughness: return TEXT("Roughness");
		case MP_Anisotropy: return TEXT("Anisotropy");
		case MP_EmissiveColor: return TEXT("EmissiveColor");
		case MP_Opacity: return TEXT("Opacity");
		case MP_OpacityMask: return TEXT("OpacityMask");
		case MP_Normal: return TEXT("Normal");
		case MP_Tangent: return TEXT("Tangent");
		case MP_WorldPositionOffset: return TEXT("WorldPositionOffset");
		case MP_SubsurfaceColor: return TEXT("SubsurfaceColor");
		case MP_AmbientOcclusion: return TEXT("AmbientOcclusion");
		case MP_Refraction: return TEXT("Refraction");
		case MP_PixelDepthOffset: return TEXT("PixelDepthOffset");
		case MP_ShadingModel: return TEXT("ShadingModel");
		case MP_FrontMaterial: return TEXT("FrontMaterial");
		case MP_SurfaceThickness: return TEXT("SurfaceThickness");
		case MP_Displacement: return TEXT("Displacement");
		default: return FString::Printf(TEXT("Property%d"), PropertyIndex);
	}
}

//////////////////////////////////////////////////////////////////////////
// MaterialInstance Export

FString UMCTMaterialExporter::ExportMaterialInstance(UMaterialInstance* MaterialInstance, bool bFilterDefaults)
{
	FString Output;

	// Determine instance type for header
	FString InstanceType = TEXT("MATERIAL INSTANCE");
	if (MaterialInstance->IsA<UMaterialInstanceDynamic>())
	{
		InstanceType = TEXT("MATERIAL INSTANCE DYNAMIC");
	}
	else if (MaterialInstance->IsA<UMaterialInstanceConstant>())
	{
		InstanceType = TEXT("MATERIAL INSTANCE CONSTANT");
	}

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("%s: %s"), *InstanceType, *MaterialInstance->GetName()));
	Output += FString::Printf(TEXT("Path: %s\n"), *MaterialInstance->GetPathName());

	// Parent material
	UMaterialInterface* Parent = MaterialInstance->Parent;
	Output += FString::Printf(TEXT("Parent: %s\n"), Parent ? *Parent->GetPathName() : TEXT("None"));

	// Parameters - only export overridden values
	Output += TEXT("\n");
	Output += ExportMaterialParameters(MaterialInstance, false);

	return Output;
}
