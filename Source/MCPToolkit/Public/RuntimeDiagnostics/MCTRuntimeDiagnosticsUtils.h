// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::RuntimeDiagnostics
{
int32 ReadClampedIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue);
double ReadClampedDoubleField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, double DefaultValue, double MinValue, double MaxValue);
FString ReadLowerStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue);
FString TruncateString(const FString& Value, int32 Limit);
FString NetModeToString(ENetMode NetMode);
void AppendStringArrayField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, TArray<FString>& OutValues);

UWorld* SelectWorld(const FString& RequestedWorld, FString& OutWorldSource);
AActor* FindActor(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName);

TSharedPtr<FJsonObject> BuildPIEStateJson();
TSharedPtr<FJsonObject> BuildVectorJson(const FVector& Vector);
TSharedPtr<FJsonObject> BuildVector2DJson(const FVector2D& Vector);
TSharedPtr<FJsonObject> BuildRotatorJson(const FRotator& Rotator);
TSharedPtr<FJsonObject> BuildBoxJson(const FBox& Box);
TSharedPtr<FJsonObject> BuildObjectReferenceJson(const UObject* Object);
TSharedPtr<FJsonObject> BuildActorJson(AActor* Actor);
TSharedPtr<FJsonObject> BuildComponentJson(UActorComponent* Component);
TSharedPtr<FJsonObject> BuildWorldJson(UWorld* World, const FString& WorldSource);

void AddReflectedSettingsPropertyJson(TSharedPtr<FJsonObject> Target, const UObject* Settings, const TCHAR* PropertyName, const TCHAR* JsonName);
}
