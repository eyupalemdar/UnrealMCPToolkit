// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTSplineCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

namespace MCPToolkit::CommandHandlers::Spline
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

int32 ReadIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const int32 DefaultValue, const int32 MinValue, const int32 MaxValue)
{
	double NumberValue = static_cast<double>(DefaultValue);
	if (Params.IsValid())
	{
		Params->TryGetNumberField(FieldName, NumberValue);
	}
	return FMath::Clamp(FMath::RoundToInt(NumberValue), MinValue, MaxValue);
}

TSharedPtr<FJsonObject> BuildVectorJson(const FVector& Value)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Value.X);
	Data->SetNumberField(TEXT("y"), Value.Y);
	Data->SetNumberField(TEXT("z"), Value.Z);
	return Data;
}

TSharedPtr<FJsonObject> BuildRotatorJson(const FRotator& Value)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("pitch"), Value.Pitch);
	Data->SetNumberField(TEXT("yaw"), Value.Yaw);
	Data->SetNumberField(TEXT("roll"), Value.Roll);
	return Data;
}

bool TryReadVectorObject(TSharedPtr<FJsonObject> Object, FVector& OutValue)
{
	if (!Object.IsValid())
	{
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	const bool bHasX = Object->TryGetNumberField(TEXT("x"), X);
	const bool bHasY = Object->TryGetNumberField(TEXT("y"), Y);
	const bool bHasZ = Object->TryGetNumberField(TEXT("z"), Z);
	if (!bHasX && !bHasY && !bHasZ)
	{
		return false;
	}

	OutValue.X = bHasX ? X : 0.0;
	OutValue.Y = bHasY ? Y : 0.0;
	OutValue.Z = bHasZ ? Z : 0.0;
	return true;
}

bool TryReadVectorField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, FVector& OutValue)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (Params.IsValid() && Params->TryGetObjectField(FieldName, Object) && Object && Object->IsValid())
	{
		return TryReadVectorObject(*Object, OutValue);
	}
	return false;
}

bool TryReadSplinePointLocation(TSharedPtr<FJsonObject> PointObject, FVector& OutValue)
{
	if (!PointObject.IsValid())
	{
		return false;
	}

	if (TryReadVectorField(PointObject, TEXT("location"), OutValue))
	{
		return true;
	}
	return TryReadVectorObject(PointObject, OutValue);
}

ESplineCoordinateSpace::Type ParseCoordinateSpace(const FString& Value, FString& OutError)
{
	const FString Lower = Value.IsEmpty() ? TEXT("world") : Value.ToLower();
	if (Lower == TEXT("world"))
	{
		return ESplineCoordinateSpace::World;
	}
	if (Lower == TEXT("local"))
	{
		return ESplineCoordinateSpace::Local;
	}

	OutError = FString::Printf(TEXT("Unsupported coordinate_space '%s'. Expected 'world' or 'local'"), *Value);
	return ESplineCoordinateSpace::World;
}

FString SplinePointTypeToString(const ESplinePointType::Type PointType)
{
	switch (PointType)
	{
	case ESplinePointType::Linear:
		return TEXT("Linear");
	case ESplinePointType::Curve:
		return TEXT("Curve");
	case ESplinePointType::Constant:
		return TEXT("Constant");
	case ESplinePointType::CurveClamped:
		return TEXT("CurveClamped");
	case ESplinePointType::CurveCustomTangent:
		return TEXT("CurveCustomTangent");
	default:
		return TEXT("Unknown");
	}
}

bool TryParseSplinePointType(const FString& Value, ESplinePointType::Type& OutType, FString& OutError)
{
	const FString Lower = Value.IsEmpty() ? TEXT("curve") : Value.ToLower();
	if (Lower == TEXT("curve"))
	{
		OutType = ESplinePointType::Curve;
		return true;
	}
	if (Lower == TEXT("linear"))
	{
		OutType = ESplinePointType::Linear;
		return true;
	}
	if (Lower == TEXT("constant"))
	{
		OutType = ESplinePointType::Constant;
		return true;
	}
	if (Lower == TEXT("curveclamped") || Lower == TEXT("curve_clamped"))
	{
		OutType = ESplinePointType::CurveClamped;
		return true;
	}
	if (Lower == TEXT("curvecustomtangent") || Lower == TEXT("curve_custom_tangent"))
	{
		OutType = ESplinePointType::CurveCustomTangent;
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported spline point type '%s'"), *Value);
	return false;
}

UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

AActor* FindActorByLabel(UWorld* World, const FString& ActorLabel)
{
	if (!World || ActorLabel.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetActorLabel() == ActorLabel)
		{
			return Actor;
		}
	}
	return nullptr;
}

bool ReadActorSelector(TSharedPtr<FJsonObject> Params, FString& OutActorPath, FString& OutActorLabel, FString& OutActorName)
{
	OutActorPath = ReadStringField(Params, TEXT("actor_path"));
	OutActorLabel = ReadStringField(Params, TEXT("actor_label"));
	OutActorName = ReadStringField(Params, TEXT("actor_name"));
	return !OutActorPath.IsEmpty() || !OutActorLabel.IsEmpty() || !OutActorName.IsEmpty();
}

TSharedPtr<FJsonObject> BuildActorSummaryJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Actor != nullptr);
	if (!Actor)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Actor->GetName());
	Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("path"), Actor->GetPathName());
	Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("class_path"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Actor->GetActorLocation()));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(Actor->GetActorRotation()));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(Actor->GetActorScale3D()));
	return Data;
}

void GetSplineComponents(AActor* Actor, TArray<USplineComponent*>& OutComponents)
{
	OutComponents.Reset();
	if (Actor)
	{
		Actor->GetComponents<USplineComponent>(OutComponents);
	}
}

USplineComponent* ResolveSplineComponent(AActor* Actor, const FString& ComponentName, FString& OutError)
{
	TArray<USplineComponent*> Components;
	GetSplineComponents(Actor, Components);
	if (Components.IsEmpty())
	{
		OutError = TEXT("Actor does not have a SplineComponent");
		return nullptr;
	}

	if (ComponentName.IsEmpty())
	{
		if (Components.Num() > 1)
		{
			OutError = TEXT("Actor has multiple SplineComponents; provide component_name");
			return nullptr;
		}
		return Components[0];
	}

	for (USplineComponent* Component : Components)
	{
		if (Component && (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) || Component->GetFName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase)))
		{
			return Component;
		}
	}

	OutError = FString::Printf(TEXT("SplineComponent not found: %s"), *ComponentName);
	return nullptr;
}

TSharedPtr<FJsonObject> BuildSplinePointJson(USplineComponent* SplineComponent, const int32 PointIndex)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), PointIndex);
	if (!SplineComponent || PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints())
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	Data->SetBoolField(TEXT("present"), true);
	Data->SetStringField(TEXT("type"), SplinePointTypeToString(SplineComponent->GetSplinePointType(PointIndex)));
	Data->SetNumberField(TEXT("input_key"), SplineComponent->GetInputKeyValueAtSplinePoint(PointIndex));
	Data->SetNumberField(TEXT("distance"), SplineComponent->GetDistanceAlongSplineAtSplinePoint(PointIndex));
	Data->SetObjectField(TEXT("world_location"), BuildVectorJson(SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World)));
	Data->SetObjectField(TEXT("local_location"), BuildVectorJson(SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local)));
	Data->SetObjectField(TEXT("world_arrive_tangent"), BuildVectorJson(SplineComponent->GetArriveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World)));
	Data->SetObjectField(TEXT("world_leave_tangent"), BuildVectorJson(SplineComponent->GetLeaveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World)));
	Data->SetObjectField(TEXT("local_arrive_tangent"), BuildVectorJson(SplineComponent->GetArriveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local)));
	Data->SetObjectField(TEXT("local_leave_tangent"), BuildVectorJson(SplineComponent->GetLeaveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local)));
	Data->SetObjectField(TEXT("world_rotation"), BuildRotatorJson(SplineComponent->GetRotationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World)));
	Data->SetObjectField(TEXT("local_rotation"), BuildRotatorJson(SplineComponent->GetRotationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local)));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(SplineComponent->GetScaleAtSplinePoint(PointIndex)));
	return Data;
}

TSharedPtr<FJsonObject> BuildSplineComponentJson(USplineComponent* SplineComponent, const bool bIncludePoints, const int32 PointLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), SplineComponent != nullptr);
	if (!SplineComponent)
	{
		return Data;
	}

	const int32 PointCount = SplineComponent->GetNumberOfSplinePoints();
	Data->SetStringField(TEXT("name"), SplineComponent->GetName());
	Data->SetStringField(TEXT("path"), SplineComponent->GetPathName());
	Data->SetStringField(TEXT("class"), SplineComponent->GetClass() ? SplineComponent->GetClass()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("class_path"), SplineComponent->GetClass() ? SplineComponent->GetClass()->GetPathName() : TEXT(""));
	Data->SetNumberField(TEXT("point_count"), PointCount);
	Data->SetNumberField(TEXT("segment_count"), SplineComponent->GetNumberOfSplineSegments());
	Data->SetNumberField(TEXT("spline_length"), SplineComponent->GetSplineLength());
	Data->SetNumberField(TEXT("duration"), SplineComponent->Duration);
	Data->SetBoolField(TEXT("closed_loop"), SplineComponent->IsClosedLoop());
	Data->SetObjectField(TEXT("default_up_vector_world"), BuildVectorJson(SplineComponent->GetDefaultUpVector(ESplineCoordinateSpace::World)));
	Data->SetObjectField(TEXT("default_up_vector_local"), BuildVectorJson(SplineComponent->GetDefaultUpVector(ESplineCoordinateSpace::Local)));

	if (bIncludePoints)
	{
		TArray<TSharedPtr<FJsonValue>> PointsJson;
		const int32 SafePointLimit = FMath::Clamp(PointLimit, 0, 20000);
		for (int32 PointIndex = 0; PointIndex < PointCount && PointsJson.Num() < SafePointLimit; ++PointIndex)
		{
			PointsJson.Add(MakeShared<FJsonValueObject>(BuildSplinePointJson(SplineComponent, PointIndex)));
		}
		Data->SetArrayField(TEXT("points"), PointsJson);
		Data->SetNumberField(TEXT("returned_point_count"), PointsJson.Num());
		Data->SetBoolField(TEXT("points_truncated"), PointsJson.Num() < PointCount);
	}

	return Data;
}

struct FParsedSplinePoint
{
	FVector Location = FVector::ZeroVector;
	ESplinePointType::Type Type = ESplinePointType::Curve;
	bool bHasArriveTangent = false;
	bool bHasLeaveTangent = false;
	bool bHasScale = false;
	FVector ArriveTangent = FVector::ZeroVector;
	FVector LeaveTangent = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
};

bool ParseSplinePointsFromJson(
	const TArray<TSharedPtr<FJsonValue>>& Points,
	const ESplinePointType::Type DefaultPointType,
	TArray<FParsedSplinePoint>& OutPoints,
	FString& OutError)
{
	OutPoints.Reset();
	if (Points.IsEmpty())
	{
		OutError = TEXT("'points' must contain at least one point");
		return false;
	}

	for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
	{
		const TSharedPtr<FJsonObject>* PointObject = nullptr;
		if (!Points[PointIndex].IsValid() || !Points[PointIndex]->TryGetObject(PointObject) || !PointObject || !PointObject->IsValid())
		{
			OutError = FString::Printf(TEXT("Point %d is not an object"), PointIndex);
			return false;
		}

		FParsedSplinePoint ParsedPoint;
		if (!TryReadSplinePointLocation(*PointObject, ParsedPoint.Location))
		{
			OutError = FString::Printf(TEXT("Point %d is missing location/x/y/z"), PointIndex);
			return false;
		}

		FString TypeValue;
		(*PointObject)->TryGetStringField(TEXT("type"), TypeValue);
		ParsedPoint.Type = DefaultPointType;
		if (!TypeValue.IsEmpty() && !TryParseSplinePointType(TypeValue, ParsedPoint.Type, OutError))
		{
			OutError = FString::Printf(TEXT("Point %d: %s"), PointIndex, *OutError);
			return false;
		}

		ParsedPoint.bHasArriveTangent = TryReadVectorField(*PointObject, TEXT("arrive_tangent"), ParsedPoint.ArriveTangent);
		ParsedPoint.bHasLeaveTangent = TryReadVectorField(*PointObject, TEXT("leave_tangent"), ParsedPoint.LeaveTangent);
		ParsedPoint.bHasScale = TryReadVectorField(*PointObject, TEXT("scale"), ParsedPoint.Scale);
		OutPoints.Add(ParsedPoint);
	}

	return true;
}

bool ApplySplinePointsFromJson(
	USplineComponent* SplineComponent,
	const TArray<TSharedPtr<FJsonValue>>& Points,
	const ESplineCoordinateSpace::Type CoordinateSpace,
	const ESplinePointType::Type DefaultPointType,
	FString& OutError)
{
	if (!SplineComponent)
	{
		OutError = TEXT("Missing SplineComponent");
		return false;
	}

	TArray<FParsedSplinePoint> ParsedPoints;
	if (!ParseSplinePointsFromJson(Points, DefaultPointType, ParsedPoints, OutError))
	{
		return false;
	}

	SplineComponent->ClearSplinePoints(false);
	for (int32 PointIndex = 0; PointIndex < ParsedPoints.Num(); ++PointIndex)
	{
		const FParsedSplinePoint& ParsedPoint = ParsedPoints[PointIndex];
		SplineComponent->AddSplinePoint(ParsedPoint.Location, CoordinateSpace, false);
		SplineComponent->SetSplinePointType(PointIndex, ParsedPoint.Type, false);

		FVector ArriveTangent;
		FVector LeaveTangent;
		if (ParsedPoint.bHasArriveTangent || ParsedPoint.bHasLeaveTangent)
		{
			if (ParsedPoint.bHasArriveTangent)
			{
				ArriveTangent = ParsedPoint.ArriveTangent;
			}
			else
			{
				ArriveTangent = SplineComponent->GetArriveTangentAtSplinePoint(PointIndex, CoordinateSpace);
			}
			if (ParsedPoint.bHasLeaveTangent)
			{
				LeaveTangent = ParsedPoint.LeaveTangent;
			}
			else
			{
				LeaveTangent = SplineComponent->GetLeaveTangentAtSplinePoint(PointIndex, CoordinateSpace);
			}
			SplineComponent->SetTangentsAtSplinePoint(PointIndex, ArriveTangent, LeaveTangent, CoordinateSpace, false);
		}

		if (ParsedPoint.bHasScale)
		{
			SplineComponent->SetScaleAtSplinePoint(PointIndex, ParsedPoint.Scale, false);
		}
	}

	SplineComponent->UpdateSpline();
	return true;
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleSplineActorCreate(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString ActorLabel = ReadStringField(Params, TEXT("actor_label"));
	FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	if (ComponentName.IsEmpty())
	{
		ComponentName = TEXT("SplineComponent");
	}

	FVector Location = FVector::ZeroVector;
	TryReadVectorField(Params, TEXT("location"), Location);

	const FString CoordinateSpaceText = ReadStringField(Params, TEXT("coordinate_space"));
	const FString DefaultPointTypeText = ReadStringField(Params, TEXT("point_type"));
	const bool bClosedLoop = ReadBoolField(Params, TEXT("closed_loop"), false);
	const bool bSetClosedLoop = ReadBoolField(Params, TEXT("set_closed_loop"), Params->HasField(TEXT("closed_loop")));
	const FString OnConflict = ReadStringField(Params, TEXT("on_conflict")).IsEmpty() ? TEXT("error") : ReadStringField(Params, TEXT("on_conflict")).ToLower();

	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	Params->TryGetArrayField(TEXT("points"), PointsArray);

	return RunOnGameThread([ActorLabel, ComponentName, Location, CoordinateSpaceText, DefaultPointTypeText, bClosedLoop, bSetClosedLoop, OnConflict, PointsArray]()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return CreateErrorResponse(TEXT("No editor world is available"));
		}

		if (!ActorLabel.IsEmpty())
		{
			if (AActor* ExistingActor = FindActorByLabel(World, ActorLabel))
			{
				if (OnConflict == TEXT("skip"))
				{
					TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
					Data->SetBoolField(TEXT("created"), false);
					Data->SetBoolField(TEXT("existed"), true);
					Data->SetObjectField(TEXT("actor"), BuildActorSummaryJson(ExistingActor));
					TArray<USplineComponent*> Components;
					GetSplineComponents(ExistingActor, Components);
					if (!Components.IsEmpty())
					{
						Data->SetObjectField(TEXT("component"), BuildSplineComponentJson(Components[0], true, 500));
					}
					return CreateSuccessResponse(Data);
				}
				return CreateErrorResponse(FString::Printf(TEXT("Actor label already exists: %s"), *ActorLabel));
			}
		}

		FString Error;
		const ESplineCoordinateSpace::Type CoordinateSpace = ParseCoordinateSpace(CoordinateSpaceText, Error);
		if (!Error.IsEmpty())
		{
			return CreateErrorResponse(Error);
		}

		ESplinePointType::Type DefaultPointType = ESplinePointType::Curve;
		if (!TryParseSplinePointType(DefaultPointTypeText, DefaultPointType, Error))
		{
			return CreateErrorResponse(Error);
		}

		const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "SplineActorCreate", "AI Create Spline Actor"));
		World->Modify();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
		if (!Actor)
		{
			return CreateErrorResponse(TEXT("Failed to spawn spline actor"));
		}

		Actor->Modify();
		if (!ActorLabel.IsEmpty())
		{
			Actor->SetActorLabel(ActorLabel);
		}

		const FName RootName = MakeUniqueObjectName(Actor, USceneComponent::StaticClass(), TEXT("DefaultSceneRoot"));
		USceneComponent* RootComponent = NewObject<USceneComponent>(Actor, RootName);
		RootComponent->CreationMethod = EComponentCreationMethod::Instance;
		RootComponent->Mobility = EComponentMobility::Movable;
		Actor->SetRootComponent(RootComponent);
		Actor->AddInstanceComponent(RootComponent);
		RootComponent->RegisterComponent();

		const FName SplineName = MakeUniqueObjectName(Actor, USplineComponent::StaticClass(), FName(*ComponentName));
		USplineComponent* SplineComponent = NewObject<USplineComponent>(Actor, SplineName);
		SplineComponent->CreationMethod = EComponentCreationMethod::Instance;
		SplineComponent->Mobility = EComponentMobility::Movable;
		SplineComponent->SetupAttachment(RootComponent);
		Actor->AddInstanceComponent(SplineComponent);
		SplineComponent->RegisterComponent();

		if (PointsArray && PointsArray->Num() > 0)
		{
			if (!ApplySplinePointsFromJson(SplineComponent, *PointsArray, CoordinateSpace, DefaultPointType, Error))
			{
				World->EditorDestroyActor(Actor, false);
				return CreateErrorResponse(Error);
			}
		}
		if (bSetClosedLoop)
		{
			SplineComponent->SetClosedLoop(bClosedLoop, true);
		}

		Actor->MarkPackageDirty();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("created"), true);
		Data->SetBoolField(TEXT("existed"), false);
		Data->SetObjectField(TEXT("actor"), BuildActorSummaryJson(Actor));
		Data->SetObjectField(TEXT("component"), BuildSplineComponentJson(SplineComponent, true, 500));
		return CreateSuccessResponse(Data);
	}, TEXT("Spline actor create timed out"));
}

FString HandleSplineComponentInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	if (!ReadActorSelector(Params, ActorPath, ActorLabel, ActorName))
	{
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));
	}

	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString WorldSelector = ReadStringField(Params, TEXT("world")).IsEmpty() ? TEXT("editor") : ReadStringField(Params, TEXT("world"));
	const bool bIncludePoints = ReadBoolField(Params, TEXT("include_points"), true);
	const int32 PointLimit = ReadIntField(Params, TEXT("point_limit"), 500, 0, 20000);

	return RunOnGameThread([ActorPath, ActorLabel, ActorName, ComponentName, WorldSelector, bIncludePoints, PointLimit]()
	{
		FString WorldSource;
		UWorld* World = MCPToolkit::RuntimeDiagnostics::SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(FString::Printf(TEXT("No world is available for selector: %s"), *WorldSelector));
		}

		AActor* Actor = MCPToolkit::RuntimeDiagnostics::FindActor(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			return CreateErrorResponse(TEXT("Actor not found"));
		}

		TArray<USplineComponent*> Components;
		GetSplineComponents(Actor, Components);

		TArray<TSharedPtr<FJsonValue>> ComponentsJson;
		for (USplineComponent* Component : Components)
		{
			if (!ComponentName.IsEmpty() && Component && !Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildSplineComponentJson(Component, bIncludePoints, PointLimit)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetObjectField(TEXT("actor"), BuildActorSummaryJson(Actor));
		Data->SetNumberField(TEXT("component_count"), Components.Num());
		Data->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
		Data->SetArrayField(TEXT("components"), ComponentsJson);
		if (!ComponentName.IsEmpty() && ComponentsJson.IsEmpty())
		{
			Data->SetStringField(TEXT("missing_component_name"), ComponentName);
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Spline component info timed out"));
}

FString HandleSplineComponentSetPoints(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	if (!ReadActorSelector(Params, ActorPath, ActorLabel, ActorName))
	{
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));
	}

	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArray) || !PointsArray)
	{
		return CreateErrorResponse(TEXT("Missing 'points' parameter"));
	}

	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString CoordinateSpaceText = ReadStringField(Params, TEXT("coordinate_space"));
	const FString DefaultPointTypeText = ReadStringField(Params, TEXT("point_type"));
	const bool bSetClosedLoop = ReadBoolField(Params, TEXT("set_closed_loop"), false);
	const bool bClosedLoop = ReadBoolField(Params, TEXT("closed_loop"), false);

	return RunOnGameThread([ActorPath, ActorLabel, ActorName, ComponentName, CoordinateSpaceText, DefaultPointTypeText, PointsArray, bSetClosedLoop, bClosedLoop]()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return CreateErrorResponse(TEXT("No editor world is available"));
		}

		AActor* Actor = MCPToolkit::RuntimeDiagnostics::FindActor(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			return CreateErrorResponse(TEXT("Actor not found"));
		}

		FString Error;
		USplineComponent* SplineComponent = ResolveSplineComponent(Actor, ComponentName, Error);
		if (!SplineComponent)
		{
			return CreateErrorResponse(Error);
		}

		const ESplineCoordinateSpace::Type CoordinateSpace = ParseCoordinateSpace(CoordinateSpaceText, Error);
		if (!Error.IsEmpty())
		{
			return CreateErrorResponse(Error);
		}

		ESplinePointType::Type DefaultPointType = ESplinePointType::Curve;
		if (!TryParseSplinePointType(DefaultPointTypeText, DefaultPointType, Error))
		{
			return CreateErrorResponse(Error);
		}

		const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "SplineComponentSetPoints", "AI Set Spline Points"));
		Actor->Modify();
		SplineComponent->Modify();

		if (!ApplySplinePointsFromJson(SplineComponent, *PointsArray, CoordinateSpace, DefaultPointType, Error))
		{
			return CreateErrorResponse(Error);
		}

		if (bSetClosedLoop)
		{
			SplineComponent->SetClosedLoop(bClosedLoop, true);
		}

		SplineComponent->MarkRenderStateDirty();
		Actor->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("updated"), true);
		Data->SetObjectField(TEXT("actor"), BuildActorSummaryJson(Actor));
		Data->SetObjectField(TEXT("component"), BuildSplineComponentJson(SplineComponent, true, 500));
		return CreateSuccessResponse(Data);
	}, TEXT("Spline component set points timed out"));
}
}
