// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeCore.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace MCPToolkit::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildWorldInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return nullptr;
		}

		return BuildWorldJson(World, WorldSource);
}


TSharedPtr<FJsonObject> BuildPlayerList(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return nullptr;
		}

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ControllerJson = MakeShared<FJsonObject>();
			ControllerJson->SetStringField(TEXT("name"), Controller->GetName());
			ControllerJson->SetStringField(TEXT("path"), Controller->GetPathName());
			ControllerJson->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
			ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
			if (APlayerState* PlayerState = Controller->PlayerState.Get())
			{
				ControllerJson->SetStringField(TEXT("player_state_name"), PlayerState->GetPlayerName());
				ControllerJson->SetStringField(TEXT("player_state_class"), PlayerState->GetClass() ? PlayerState->GetClass()->GetPathName() : TEXT(""));
			}
			if (APawn* Pawn = Controller->GetPawn())
			{
				ControllerJson->SetObjectField(TEXT("pawn"), BuildActorJson(Pawn));
			}
			Controllers.Add(MakeShared<FJsonValueObject>(ControllerJson));
		}

		TArray<TSharedPtr<FJsonValue>> LocalPlayers;
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LocalPlayerJson = MakeShared<FJsonObject>();
				LocalPlayerJson->SetStringField(TEXT("name"), LocalPlayer->GetName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				if (APlayerController* Controller = LocalPlayer->GetPlayerController(World))
				{
					LocalPlayerJson->SetStringField(TEXT("player_controller_path"), Controller->GetPathName());
				}
				LocalPlayers.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}

		TSharedPtr<FJsonObject> Data = BuildWorldJson(World, WorldSource);
		Data->SetArrayField(TEXT("controllers"), Controllers);
		Data->SetArrayField(TEXT("local_players"), LocalPlayers);
		Data->SetNumberField(TEXT("controller_count"), Controllers.Num());
		Data->SetNumberField(TEXT("local_player_count"), LocalPlayers.Num());
		return Data;
}


TSharedPtr<FJsonObject> BuildRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	bool bIncludeComponents = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 100, 0, 1000);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			return Data;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; diagnostics reflect the editor world")));
		}

		TSharedPtr<FJsonObject> WorldJson = BuildWorldJson(World, WorldSource);
		Data->SetObjectField(TEXT("world"), WorldJson);

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ControllerJson = MakeShared<FJsonObject>();
			ControllerJson->SetStringField(TEXT("name"), Controller->GetName());
			ControllerJson->SetStringField(TEXT("path"), Controller->GetPathName());
			ControllerJson->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
			ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
			if (APawn* Pawn = Controller->GetPawn())
			{
				ControllerJson->SetObjectField(TEXT("pawn"), BuildActorJson(Pawn));
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("PlayerController '%s' has no possessed pawn"), *Controller->GetName())));
			}
			Controllers.Add(MakeShared<FJsonValueObject>(ControllerJson));
		}

		TSharedPtr<FJsonObject> PlayersJson = MakeShared<FJsonObject>();
		PlayersJson->SetArrayField(TEXT("controllers"), Controllers);
		PlayersJson->SetNumberField(TEXT("controller_count"), Controllers.Num());
		if (Controllers.Num() == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no player controllers")));
		}
		Data->SetObjectField(TEXT("players"), PlayersJson);

		const bool bActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		Data->SetBoolField(TEXT("selected_actor_requested"), bActorRequested);
		if (bActorRequested)
		{
			AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				TSharedPtr<FJsonObject> ActorJson = BuildActorJson(Actor);
				if (bIncludeComponents)
				{
					TArray<UActorComponent*> Components;
					Actor->GetComponents(Components);
					TArray<TSharedPtr<FJsonValue>> ComponentsJson;
					for (UActorComponent* Component : Components)
					{
						if (!Component)
						{
							continue;
						}
						if (ComponentsJson.Num() < ComponentLimit)
						{
							ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildComponentJson(Component)));
						}
					}
					ActorJson->SetNumberField(TEXT("component_count"), Components.Num());
					ActorJson->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
					ActorJson->SetBoolField(TEXT("components_truncated"), Components.Num() > ComponentsJson.Num());
					ActorJson->SetArrayField(TEXT("components"), ComponentsJson);
				}
				Data->SetObjectField(TEXT("selected_actor"), ActorJson);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


}
