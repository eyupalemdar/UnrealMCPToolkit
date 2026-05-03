// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeUI.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "CommonActivatableWidget.h"
#include "CommonInputSubsystem.h"
#include "Components/InputComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBinding.h"
#include "Input/UIActionBindingHandle.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
FString CommonInputTypeToString(ECommonInputType InputType)
{
	switch (InputType)
	{
	case ECommonInputType::MouseAndKeyboard:
		return TEXT("MouseAndKeyboard");
	case ECommonInputType::Gamepad:
		return TEXT("Gamepad");
	case ECommonInputType::Touch:
		return TEXT("Touch");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildRuntimeInputComponentJson(UInputComponent* InputComponent)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), InputComponent != nullptr);
	if (!InputComponent)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), InputComponent->GetName());
	Data->SetStringField(TEXT("path"), InputComponent->GetPathName());
	Data->SetStringField(TEXT("class"), InputComponent->GetClass() ? InputComponent->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("registered"), InputComponent->IsRegistered());
	Data->SetBoolField(TEXT("active"), InputComponent->IsActive());
	Data->SetNumberField(TEXT("priority"), InputComponent->Priority);
	Data->SetBoolField(TEXT("block_input"), InputComponent->bBlockInput != 0);
	Data->SetNumberField(TEXT("action_binding_count"), InputComponent->GetNumActionBindings());
	Data->SetNumberField(TEXT("key_binding_count"), InputComponent->KeyBindings.Num());
	Data->SetNumberField(TEXT("axis_binding_count"), InputComponent->AxisBindings.Num());
	Data->SetNumberField(TEXT("axis_key_binding_count"), InputComponent->AxisKeyBindings.Num());
	Data->SetNumberField(TEXT("vector_axis_binding_count"), InputComponent->VectorAxisBindings.Num());
	Data->SetNumberField(TEXT("touch_binding_count"), InputComponent->TouchBindings.Num());
	Data->SetNumberField(TEXT("gesture_binding_count"), InputComponent->GestureBindings.Num());

	if (AActor* Owner = InputComponent->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_label"), Owner->GetActorLabel());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
		Data->SetStringField(TEXT("owner_class"), Owner->GetClass() ? Owner->GetClass()->GetPathName() : TEXT(""));
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
	{
		TSharedPtr<FJsonObject> EnhancedJson = MakeShared<FJsonObject>();
		EnhancedJson->SetNumberField(TEXT("action_event_binding_count"), EnhancedInputComponent->GetActionEventBindings().Num());
		EnhancedJson->SetNumberField(TEXT("action_value_binding_count"), EnhancedInputComponent->GetActionValueBindings().Num());
		EnhancedJson->SetNumberField(TEXT("debug_key_binding_count"), EnhancedInputComponent->GetDebugKeyBindings().Num());
		EnhancedJson->SetBoolField(TEXT("fire_delegates_in_editor"), EnhancedInputComponent->ShouldFireDelegatesInEditor());
		Data->SetObjectField(TEXT("enhanced_input"), EnhancedJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimePlayerInputJson(APlayerController* Controller)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UPlayerInput* PlayerInput = Controller ? Controller->PlayerInput : nullptr;
	Data->SetBoolField(TEXT("present"), PlayerInput != nullptr);
	if (!PlayerInput)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), PlayerInput->GetName());
	Data->SetStringField(TEXT("path"), PlayerInput->GetPathName());
	Data->SetStringField(TEXT("class"), PlayerInput->GetClass() ? PlayerInput->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("is_enhanced_player_input"), PlayerInput->IsA<UEnhancedPlayerInput>());

	if (UEnhancedPlayerInput* EnhancedPlayerInput = Cast<UEnhancedPlayerInput>(PlayerInput))
	{
		Data->SetBoolField(TEXT("enhanced_player_input_available"), EnhancedPlayerInput != nullptr);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeEnhancedInputSubsystemJson(ULocalPlayer* LocalPlayer)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UEnhancedInputLocalPlayerSubsystem* EnhancedSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	Data->SetBoolField(TEXT("subsystem_present"), EnhancedSubsystem != nullptr);
	if (!EnhancedSubsystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("input_mode_tags"), EnhancedSubsystem->GetInputMode().ToStringSimple());
	Data->SetNumberField(TEXT("player_mappable_action_key_mapping_count"), EnhancedSubsystem->GetAllPlayerMappableActionKeyMappings().Num());

	UEnhancedPlayerInput* PlayerInput = EnhancedSubsystem->GetPlayerInput();
	Data->SetBoolField(TEXT("player_input_present"), PlayerInput != nullptr);
	if (PlayerInput)
	{
		Data->SetStringField(TEXT("player_input_name"), PlayerInput->GetName());
		Data->SetStringField(TEXT("player_input_path"), PlayerInput->GetPathName());
		Data->SetStringField(TEXT("player_input_class"), PlayerInput->GetClass() ? PlayerInput->GetClass()->GetPathName() : TEXT(""));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeCommonInputJson(ULocalPlayer* LocalPlayer)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(LocalPlayer);
	Data->SetBoolField(TEXT("subsystem_present"), CommonInputSubsystem != nullptr);
	if (!CommonInputSubsystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("current_input_type"), CommonInputTypeToString(CommonInputSubsystem->GetCurrentInputType()));
	Data->SetStringField(TEXT("default_input_type"), CommonInputTypeToString(CommonInputSubsystem->GetDefaultInputType()));
	Data->SetStringField(TEXT("current_gamepad_name"), CommonInputSubsystem->GetCurrentGamepadName().ToString());
	Data->SetBoolField(TEXT("using_pointer_input"), CommonInputSubsystem->IsUsingPointerInput());
	Data->SetBoolField(TEXT("should_show_input_keys"), CommonInputSubsystem->ShouldShowInputKeys());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeControllerInputRouteJson(APlayerController* Controller)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Controller)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Controller->GetName());
	Data->SetStringField(TEXT("path"), Controller->GetPathName());
	Data->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
	Data->SetObjectField(TEXT("input_component"), BuildRuntimeInputComponentJson(Controller->InputComponent));
	Data->SetObjectField(TEXT("player_input"), BuildRuntimePlayerInputJson(Controller));

	if (APawn* Pawn = Controller->GetPawn())
	{
		TSharedPtr<FJsonObject> PawnJson = BuildActorJson(Pawn);
		PawnJson->SetObjectField(TEXT("input_component"), BuildRuntimeInputComponentJson(Pawn->InputComponent));
		Data->SetObjectField(TEXT("pawn"), PawnJson);
	}
	else
	{
		Data->SetBoolField(TEXT("pawn_present"), false);
	}

	return Data;
}

FString MouseCaptureModeToString(EMouseCaptureMode Mode)
{
	switch (Mode)
	{
	case EMouseCaptureMode::NoCapture:
		return TEXT("NoCapture");
	case EMouseCaptureMode::CapturePermanently:
		return TEXT("CapturePermanently");
	case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown:
		return TEXT("CapturePermanently_IncludingInitialMouseDown");
	case EMouseCaptureMode::CaptureDuringMouseDown:
		return TEXT("CaptureDuringMouseDown");
	case EMouseCaptureMode::CaptureDuringRightMouseDown:
		return TEXT("CaptureDuringRightMouseDown");
	default:
		return TEXT("Unknown");
	}
}

FString MouseLockModeToString(EMouseLockMode Mode)
{
	switch (Mode)
	{
	case EMouseLockMode::DoNotLock:
		return TEXT("DoNotLock");
	case EMouseLockMode::LockOnCapture:
		return TEXT("LockOnCapture");
	case EMouseLockMode::LockAlways:
		return TEXT("LockAlways");
	case EMouseLockMode::LockInFullscreen:
		return TEXT("LockInFullscreen");
	default:
		return TEXT("Unknown");
	}
}

FString InputEventToString(EInputEvent InputEvent)
{
	switch (InputEvent)
	{
	case IE_Pressed:
		return TEXT("Pressed");
	case IE_Released:
		return TEXT("Released");
	case IE_Repeat:
		return TEXT("Repeat");
	case IE_DoubleClick:
		return TEXT("DoubleClick");
	case IE_Axis:
		return TEXT("Axis");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildUIInputConfigJson(const FUIInputConfig& Config)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("summary"), Config.ToString());
	Data->SetStringField(TEXT("input_mode"), LexToString(Config.GetInputMode()));
	Data->SetStringField(TEXT("mouse_capture_mode"), MouseCaptureModeToString(Config.GetMouseCaptureMode()));
	Data->SetStringField(TEXT("mouse_lock_mode"), MouseLockModeToString(Config.GetMouseLockMode()));
	Data->SetBoolField(TEXT("ignore_move_input"), Config.bIgnoreMoveInput);
	Data->SetBoolField(TEXT("ignore_look_input"), Config.bIgnoreLookInput);
	Data->SetBoolField(TEXT("hide_cursor_during_viewport_capture"), Config.HideCursorDuringViewportCapture());
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonUIActionBindingJson(const FUIActionBindingHandle& Handle)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("handle_valid"), Handle.IsValid());
	Data->SetStringField(TEXT("action_name"), Handle.GetActionName().ToString());
	Data->SetStringField(TEXT("display_name"), Handle.GetDisplayName().ToString());
	Data->SetBoolField(TEXT("display_in_action_bar"), Handle.GetDisplayInActionBar());
	Data->SetObjectField(TEXT("bound_widget"), BuildObjectReferenceJson(Handle.GetBoundWidget()));

	if (TSharedPtr<FUIActionBinding> Binding = FUIActionBinding::FindBinding(Handle))
	{
		Data->SetBoolField(TEXT("registration_present"), true);
		Data->SetStringField(TEXT("debug_string"), Binding->ToDebugString());
		Data->SetStringField(TEXT("input_mode"), LexToString(Binding->InputMode));
		Data->SetStringField(TEXT("input_event"), InputEventToString(Binding->InputEvent));
		Data->SetBoolField(TEXT("consumes_input"), Binding->bConsumesInput);
		Data->SetBoolField(TEXT("persistent"), Binding->bIsPersistent);
		Data->SetBoolField(TEXT("display_in_action_bar_registration"), Binding->bDisplayInActionBar);
		Data->SetNumberField(TEXT("priority_within_collection"), Binding->PriorityWithinCollection);
		Data->SetNumberField(TEXT("user_index"), Binding->UserIndex);
		Data->SetNumberField(TEXT("normal_mapping_count"), Binding->NormalMappings.Num());
		Data->SetNumberField(TEXT("hold_mapping_count"), Binding->HoldMappings.Num());
		Data->SetObjectField(TEXT("input_action"), BuildObjectReferenceJson(Binding->InputAction.Get()));
		if (Binding->LegacyActionTableRow.DataTable)
		{
			Data->SetObjectField(TEXT("legacy_action_table"), BuildObjectReferenceJson(Binding->LegacyActionTableRow.DataTable));
			Data->SetStringField(TEXT("legacy_action_row"), Binding->LegacyActionTableRow.RowName.ToString());
		}
	}
	else
	{
		Data->SetBoolField(TEXT("registration_present"), false);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonUIActionRouterJson(UCommonUIActionRouterBase* Router, bool bIncludeBindings, int32 BindingLimit)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Router);
	Data->SetBoolField(TEXT("present"), Router != nullptr);
	if (!Router)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("local_player_index"), Router->GetLocalPlayerIndex());
	Data->SetStringField(TEXT("active_input_mode"), LexToString(Router->GetActiveInputMode(ECommonInputMode::All)));
	Data->SetStringField(TEXT("active_mouse_capture_mode"), MouseCaptureModeToString(Router->GetActiveMouseCaptureMode(EMouseCaptureMode::NoCapture)));
	Data->SetBoolField(TEXT("can_process_normal_game_input"), Router->CanProcessNormalGameInput());
	Data->SetBoolField(TEXT("pending_tree_change"), Router->IsPendingTreeChange());
	Data->SetBoolField(TEXT("always_show_cursor"), Router->ShouldAlwaysShowCursor());
	Data->SetBoolField(TEXT("common_analog_cursor_present"), Router->GetCommonAnalogCursor().IsValid());

	const TArray<FUIActionBindingHandle> ActiveBindings = Router->GatherActiveBindings();
	Data->SetNumberField(TEXT("active_binding_count"), ActiveBindings.Num());
	Data->SetBoolField(TEXT("include_bindings"), bIncludeBindings);
	if (bIncludeBindings)
	{
		TArray<TSharedPtr<FJsonValue>> BindingsJson;
		for (const FUIActionBindingHandle& Binding : ActiveBindings)
		{
			if (BindingsJson.Num() >= BindingLimit)
			{
				break;
			}
			BindingsJson.Add(MakeShared<FJsonValueObject>(BuildCommonUIActionBindingJson(Binding)));
		}
		Data->SetNumberField(TEXT("returned_binding_count"), BindingsJson.Num());
		Data->SetBoolField(TEXT("bindings_truncated"), ActiveBindings.Num() > BindingsJson.Num());
		Data->SetArrayField(TEXT("active_bindings"), BindingsJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonActivatableWidgetJson(UCommonActivatableWidget* Widget)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Widget);
	if (!Widget)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("activated"), Widget->IsActivated());
	Data->SetBoolField(TEXT("modal"), Widget->IsModal());
	Data->SetBoolField(TEXT("supports_activation_focus"), Widget->SupportsActivationFocus());
	Data->SetBoolField(TEXT("auto_restores_focus"), Widget->AutoRestoresFocus());
	Data->SetBoolField(TEXT("sets_visibility_on_activated"), Widget->SetsVisibilityOnActivated());
	Data->SetBoolField(TEXT("sets_visibility_on_deactivated"), Widget->SetsVisibilityOnDeactivated());
	Data->SetStringField(TEXT("visibility"), StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(Widget->GetVisibility())));
	Data->SetBoolField(TEXT("visible"), Widget->IsVisible());
	Data->SetBoolField(TEXT("input_tree_node_present"), Widget->GetInputTreeNode().IsValid());
	Data->SetObjectField(TEXT("desired_focus_target"), BuildObjectReferenceJson(Widget->GetDesiredFocusTarget()));

	if (ULocalPlayer* LocalPlayer = Widget->GetOwningLocalPlayer())
	{
		Data->SetBoolField(TEXT("owning_local_player_present"), true);
		Data->SetNumberField(TEXT("owning_local_player_controller_id"), LocalPlayer->GetControllerId());
		Data->SetStringField(TEXT("owning_local_player_name"), LocalPlayer->GetName());
		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			Data->SetBoolField(TEXT("in_active_root"), Router->IsWidgetInActiveRoot(Widget));
		}
	}
	else
	{
		Data->SetBoolField(TEXT("owning_local_player_present"), false);
	}

	const TOptional<FUIInputConfig> DesiredInputConfig = Widget->GetDesiredInputConfig();
	Data->SetBoolField(TEXT("desired_input_config_present"), DesiredInputConfig.IsSet());
	if (DesiredInputConfig.IsSet())
	{
		Data->SetObjectField(TEXT("desired_input_config"), BuildUIInputConfigJson(DesiredInputConfig.GetValue()));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonActivatableContainerJson(UCommonActivatableWidgetContainerBase* Container, int32 WidgetLimit)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Container);
	if (!Container)
	{
		return Data;
	}

	UCommonActivatableWidget* ActiveWidget = Container->GetActiveWidget();
	Data->SetObjectField(TEXT("active_widget"), BuildObjectReferenceJson(ActiveWidget));
	Data->SetNumberField(TEXT("widget_count"), Container->GetNumWidgets());
	Data->SetNumberField(TEXT("transition_duration"), Container->GetTransitionDuration());

	TArray<TSharedPtr<FJsonValue>> WidgetsJson;
	const TArray<UCommonActivatableWidget*>& Widgets = Container->GetWidgetList();
	for (UCommonActivatableWidget* Widget : Widgets)
	{
		if (!Widget)
		{
			continue;
		}
		if (WidgetsJson.Num() >= WidgetLimit)
		{
			break;
		}

		TSharedPtr<FJsonObject> WidgetJson = BuildObjectReferenceJson(Widget);
		WidgetJson->SetBoolField(TEXT("active_widget"), Widget == ActiveWidget);
		WidgetJson->SetBoolField(TEXT("activated"), Widget->IsActivated());
		WidgetsJson.Add(MakeShared<FJsonValueObject>(WidgetJson));
	}
	Data->SetNumberField(TEXT("returned_widget_count"), WidgetsJson.Num());
	Data->SetBoolField(TEXT("widgets_truncated"), Widgets.Num() > WidgetsJson.Num());
	Data->SetArrayField(TEXT("widgets"), WidgetsJson);
	return Data;
}

}

TSharedPtr<FJsonObject> BuildInputRoutingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; input routing reflects the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}
			Controllers.Add(MakeShared<FJsonValueObject>(BuildRuntimeControllerInputRouteJson(Controller)));
		}
		Data->SetArrayField(TEXT("controllers"), Controllers);
		Data->SetNumberField(TEXT("controller_count"), Controllers.Num());
		if (Controllers.Num() == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no player controllers for input routing")));
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
				LocalPlayerJson->SetStringField(TEXT("path"), LocalPlayer->GetPathName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				LocalPlayerJson->SetNumberField(TEXT("controller_id"), LocalPlayer->GetControllerId());
				LocalPlayerJson->SetObjectField(TEXT("common_input"), BuildRuntimeCommonInputJson(LocalPlayer));
				LocalPlayerJson->SetObjectField(TEXT("enhanced_input"), BuildRuntimeEnhancedInputSubsystemJson(LocalPlayer));

				if (APlayerController* Controller = LocalPlayer->GetPlayerController(World))
				{
					LocalPlayerJson->SetStringField(TEXT("player_controller_path"), Controller->GetPathName());
					LocalPlayerJson->SetObjectField(TEXT("player_input"), BuildRuntimePlayerInputJson(Controller));
				}
				else
				{
					LocalPlayerJson->SetBoolField(TEXT("player_controller_present"), false);
				}

				LocalPlayers.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}
		Data->SetArrayField(TEXT("local_players"), LocalPlayers);
		Data->SetNumberField(TEXT("local_player_count"), LocalPlayers.Num());
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


TSharedPtr<FJsonObject> BuildCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeWidgets = true;
	bool bIncludeBindings = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_widgets"), bIncludeWidgets);
		Params->TryGetBoolField(TEXT("include_bindings"), bIncludeBindings);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	const int32 LocalPlayerLimit = ReadClampedIntField(Params, TEXT("local_player_limit"), 8, 1, 32);
	const int32 WidgetLimit = ReadClampedIntField(Params, TEXT("widget_limit"), 100, 0, 1000);
	const int32 ContainerLimit = ReadClampedIntField(Params, TEXT("container_limit"), 100, 0, 1000);
	const int32 BindingLimit = ReadClampedIntField(Params, TEXT("binding_limit"), 100, 0, 1000);
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; CommonUI diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetBoolField(TEXT("include_widgets"), bIncludeWidgets);
		Data->SetBoolField(TEXT("include_bindings"), bIncludeBindings);
		Data->SetNumberField(TEXT("local_player_limit"), LocalPlayerLimit);
		Data->SetNumberField(TEXT("widget_limit"), WidgetLimit);
		Data->SetNumberField(TEXT("container_limit"), ContainerLimit);
		Data->SetNumberField(TEXT("binding_limit"), BindingLimit);

		TArray<TSharedPtr<FJsonValue>> LocalPlayersJson;
		int32 MatchedLocalPlayerCount = 0;
		int32 ActionRouterCount = 0;
		int32 ActiveBindingCount = 0;
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				++MatchedLocalPlayerCount;
				if (LocalPlayersJson.Num() >= LocalPlayerLimit)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LocalPlayerJson = MakeShared<FJsonObject>();
				LocalPlayerJson->SetStringField(TEXT("name"), LocalPlayer->GetName());
				LocalPlayerJson->SetStringField(TEXT("path"), LocalPlayer->GetPathName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				LocalPlayerJson->SetNumberField(TEXT("controller_id"), LocalPlayer->GetControllerId());
				LocalPlayerJson->SetObjectField(TEXT("common_input"), BuildRuntimeCommonInputJson(LocalPlayer));
				if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
				{
					++ActionRouterCount;
					const TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();
					ActiveBindingCount += Bindings.Num();
					LocalPlayerJson->SetObjectField(TEXT("action_router"), BuildCommonUIActionRouterJson(Router, bIncludeBindings, BindingLimit));
				}
				else
				{
					TSharedPtr<FJsonObject> RouterJson = MakeShared<FJsonObject>();
					RouterJson->SetBoolField(TEXT("present"), false);
					LocalPlayerJson->SetObjectField(TEXT("action_router"), RouterJson);
				}
				LocalPlayersJson.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}
		Data->SetNumberField(TEXT("matched_local_player_count"), MatchedLocalPlayerCount);
		Data->SetNumberField(TEXT("returned_local_player_count"), LocalPlayersJson.Num());
		Data->SetBoolField(TEXT("local_players_truncated"), MatchedLocalPlayerCount > LocalPlayersJson.Num());
		Data->SetNumberField(TEXT("action_router_count"), ActionRouterCount);
		Data->SetNumberField(TEXT("active_binding_count"), ActiveBindingCount);
		Data->SetArrayField(TEXT("local_players"), LocalPlayersJson);

		auto WidgetMatchesFilters = [&NameFilter, &ClassFilter](const UWidget* Widget)
		{
			if (!Widget)
			{
				return false;
			}
			const FString Name = Widget->GetName();
			const FString ClassPath = Widget->GetClass() ? Widget->GetClass()->GetPathName() : TEXT("");
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter))
			{
				return false;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
			{
				return false;
			}
			return true;
		};

		int32 MatchedActivatableWidgetCount = 0;
		int32 ActivatedWidgetCount = 0;
		int32 ModalWidgetCount = 0;
		int32 FocusSupportingWidgetCount = 0;
		TArray<TSharedPtr<FJsonValue>> WidgetsJson;
		for (TObjectIterator<UCommonActivatableWidget> It; It; ++It)
		{
			UCommonActivatableWidget* Widget = *It;
			if (!Widget || Widget->GetWorld() != World || !WidgetMatchesFilters(Widget))
			{
				continue;
			}

			++MatchedActivatableWidgetCount;
			if (Widget->IsActivated())
			{
				++ActivatedWidgetCount;
			}
			if (Widget->IsModal())
			{
				++ModalWidgetCount;
			}
			if (Widget->SupportsActivationFocus())
			{
				++FocusSupportingWidgetCount;
			}
			if (bIncludeWidgets && WidgetsJson.Num() < WidgetLimit)
			{
				WidgetsJson.Add(MakeShared<FJsonValueObject>(BuildCommonActivatableWidgetJson(Widget)));
			}
		}
		Data->SetNumberField(TEXT("matched_activatable_widget_count"), MatchedActivatableWidgetCount);
		Data->SetNumberField(TEXT("activated_widget_count"), ActivatedWidgetCount);
		Data->SetNumberField(TEXT("modal_widget_count"), ModalWidgetCount);
		Data->SetNumberField(TEXT("focus_supporting_widget_count"), FocusSupportingWidgetCount);
		if (bIncludeWidgets)
		{
			Data->SetNumberField(TEXT("returned_widget_count"), WidgetsJson.Num());
			Data->SetBoolField(TEXT("widgets_truncated"), MatchedActivatableWidgetCount > WidgetsJson.Num());
			Data->SetArrayField(TEXT("activatable_widgets"), WidgetsJson);
		}

		int32 MatchedContainerCount = 0;
		int32 NonEmptyContainerCount = 0;
		TArray<TSharedPtr<FJsonValue>> ContainersJson;
		for (TObjectIterator<UCommonActivatableWidgetContainerBase> It; It; ++It)
		{
			UCommonActivatableWidgetContainerBase* Container = *It;
			if (!Container || Container->GetWorld() != World || !WidgetMatchesFilters(Container))
			{
				continue;
			}

			++MatchedContainerCount;
			if (Container->GetNumWidgets() > 0)
			{
				++NonEmptyContainerCount;
			}
			if (bIncludeWidgets && ContainersJson.Num() < ContainerLimit)
			{
				ContainersJson.Add(MakeShared<FJsonValueObject>(BuildCommonActivatableContainerJson(Container, WidgetLimit)));
			}
		}
		Data->SetNumberField(TEXT("matched_container_count"), MatchedContainerCount);
		Data->SetNumberField(TEXT("non_empty_container_count"), NonEmptyContainerCount);
		if (bIncludeWidgets)
		{
			Data->SetNumberField(TEXT("returned_container_count"), ContainersJson.Num());
			Data->SetBoolField(TEXT("containers_truncated"), MatchedContainerCount > ContainersJson.Num());
			Data->SetArrayField(TEXT("activatable_containers"), ContainersJson);
		}

		if (MatchedLocalPlayerCount == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no local players for CommonUI routing")));
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


}
