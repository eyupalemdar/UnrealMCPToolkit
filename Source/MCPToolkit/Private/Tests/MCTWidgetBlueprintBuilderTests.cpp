// Copyright (c) 2026 Alemdar Labs. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Builders/MCTWidgetBlueprintBuilder.h"
#include "Tests/MCTWidgetBuilderBindWidgetTestTypes.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"

namespace
{
UWidgetBlueprint* MakeTransientWidgetBlueprint()
{
	UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(
		GetTransientPackage(), NAME_None, RF_Transient);
	Blueprint->ParentClass = UMCTWidgetBuilderBindWidgetTestParent::StaticClass();
	Blueprint->WidgetTree = NewObject<UWidgetTree>(
		Blueprint, TEXT("WidgetTree"), RF_Transient);
	return Blueprint;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCTWidgetBuilderNativeBindWidgetNamesTest,
	"MCPToolkit.WidgetBuilder.NativeBindWidgetNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCTWidgetBuilderNativeBindWidgetNamesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWidgetBlueprint* Blueprint = MakeTransientWidgetBlueprint();
	TestNotNull(TEXT("Transient Widget Blueprint"), Blueprint);
	if (!Blueprint)
	{
		return false;
	}

	FString Error;
	TestNotNull(TEXT("Ordinary root is created"),
		UMCTWidgetBlueprintBuilder::AddWidget(
			Blueprint, TEXT("Overlay"), TEXT("Root"), TEXT(""), &Error));
	TestNotNull(TEXT("Compatible required BindWidget name is allowed"),
		UMCTWidgetBlueprintBuilder::AddWidget(
			Blueprint, TEXT("TextBlock"), TEXT("RequiredText"), TEXT("Root"), &Error));
	TestNotNull(TEXT("Compatible optional BindWidget name is allowed"),
		UMCTWidgetBlueprintBuilder::AddWidget(
			Blueprint, TEXT("Image"), TEXT("OptionalImage"), TEXT("Root"), &Error));

	AddExpectedError(
		TEXT("shadows incompatible inherited property"),
		EAutomationExpectedErrorFlags::Contains,
		2);
	Error.Reset();
	TestNull(TEXT("Ordinary inherited property collision remains rejected"),
		UMCTWidgetBlueprintBuilder::AddWidget(
			Blueprint, TEXT("TextBlock"), TEXT("OrdinaryText"), TEXT("Root"), &Error));
	TestTrue(TEXT("Ordinary collision returns a precise error"),
		Error.Contains(TEXT("incompatible inherited property")));

	UWidgetBlueprint* IncompatibleBlueprint = MakeTransientWidgetBlueprint();
	UMCTWidgetBlueprintBuilder::AddWidget(
		IncompatibleBlueprint, TEXT("Overlay"), TEXT("Root"), TEXT(""), &Error);
	Error.Reset();
	TestNull(TEXT("Type-incompatible BindWidget name remains rejected"),
		UMCTWidgetBlueprintBuilder::AddWidget(
			IncompatibleBlueprint, TEXT("Image"), TEXT("RequiredText"), TEXT("Root"), &Error));
	TestTrue(TEXT("Incompatible BindWidget returns a precise error"),
		Error.Contains(TEXT("type-compatible BindWidget")));
	return true;
}

namespace
{
// Explicit, one-shot owned-PIE integration fixture. It never starts PIE or edits assets.
FString LastOwnedPointerRequest;
bool bOwnedPointerActive = false;
class FMCTOwnedPointerCommand : public IAutomationLatentCommand
{
public:
	TWeakObjectPtr<APlayerController> Controller;
	TSharedPtr<SWindow> Window;
	FVector2D Start, End;
	int32 Step = 0;
	bool bDragging = false;
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	virtual bool Update() override
	{
		auto& Slate = FSlateApplication::Get();
		const bool bAlive = Controller.IsValid() && Controller->GetWorld() && Controller->GetWorld()->WorldType == EWorldType::PIE;
		const bool bFinish = !bAlive || Step >= 14;
		const FVector2D Position = FMath::Lerp(Start, End, FMath::Clamp(Step / 12.0, 0.0, 1.0));
		const FVector2D Previous = FMath::Lerp(Start, End, FMath::Clamp((Step - 1) / 12.0, 0.0, 1.0));
		TSet<FKey> Pressed;
		if (!bFinish) Pressed.Add(EKeys::LeftMouseButton);
		const FPointerEvent Event(0, Position, Previous, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
		if (Step == 0 && bAlive)
		{
			Result->SetBoolField(TEXT("down_handled"), Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Event));
		}
		else if (!bFinish)
		{
			Slate.ProcessMouseMoveEvent(Event, false);
			bDragging |= Slate.IsDragDropping();
		}
		else
		{
			Slate.ProcessMouseButtonUpEvent(Event);
			bOwnedPointerActive = false;
			Result->SetBoolField(TEXT("drag_detected"), bDragging);
			Result->SetBoolField(TEXT("released"), !Slate.IsDragDropping());
			Result->SetBoolField(TEXT("world_preserved"), bAlive);
			Result->SetBoolField(TEXT("passed"), bDragging && !Slate.IsDragDropping() && bAlive);
			FString Json;
			FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
			FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("Automation/MCPToolkit/OwnedPointerResult.json")));
			return true;
		}
		++Step;
		return false;
	}
};
}

static bool RunOwnedPIEPointerRequest()
{
	FString Json;
	TSharedPtr<FJsonObject> Request;
	if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectSavedDir() / TEXT("Automation/MCPToolkit/OwnedPointerRequest.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Request) || !Request.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("No explicit owned-PIE request."));
		return true;
	}
	if (Request->GetIntegerField(TEXT("pid")) != static_cast<int32>(FPlatformProcess::GetCurrentProcessId())
		|| FMath::Abs(FDateTime::UtcNow().ToUnixTimestamp() - static_cast<int64>(Request->GetNumberField(TEXT("created_unix")))) > 60)
	{
		UE_LOG(LogTemp, Error, TEXT("Owned-PIE request is stale or belongs to another editor."));
		return false;
	}
	APlayerController* PC = FindObject<APlayerController>(nullptr, *Request->GetStringField(TEXT("controller")));
	UUserWidget* HUD = FindObject<UUserWidget>(nullptr, *Request->GetStringField(TEXT("hud")));
	if (!PC || !HUD || !PC->GetWorld() || PC->GetWorld()->WorldType != EWorldType::PIE || HUD->GetOwningPlayer() != PC)
	{
		UE_LOG(LogTemp, Error, TEXT("Expected the exact HUD/controller of an active owned PIE."));
		return false;
	}
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	if (RequestId.IsEmpty() || RequestId == LastOwnedPointerRequest || bOwnedPointerActive)
	{
		UE_LOG(LogTemp, Error, TEXT("Owned pointer request must be new and cannot overlap an active sequence."));
		return false;
	}
	LastOwnedPointerRequest = RequestId;
	TSharedRef<FMCTOwnedPointerCommand> Command = MakeShared<FMCTOwnedPointerCommand>();
	Command->Controller = PC;
	Command->Result->SetStringField(TEXT("request_id"), Request->GetStringField(TEXT("request_id")));
	TArray<TSharedPtr<FJsonValue>> Geometries;
	for (const auto& Value : Request->GetArrayField(TEXT("widgets")))
	{
		UWidget* Widget = FindObject<UWidget>(nullptr, *Value->AsString());
		if (!Widget || !Widget->IsIn(HUD)) { UE_LOG(LogTemp, Error, TEXT("Widget is outside the owned HUD.")); return false; }
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D Position = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D Size = Geometry.GetAbsoluteSize();
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("path"), Widget->GetPathName());
		Row->SetNumberField(TEXT("x"), Position.X); Row->SetNumberField(TEXT("y"), Position.Y);
		Row->SetNumberField(TEXT("width"), Size.X); Row->SetNumberField(TEXT("height"), Size.Y);
		Geometries.Add(MakeShared<FJsonValueObject>(Row));
	}
	Command->Result->SetArrayField(TEXT("geometry"), Geometries);
	if (Request->GetBoolField(TEXT("inspect_only")))
	{
		Command->Result->SetBoolField(TEXT("inspect_only"), true);
		Command->Result->SetBoolField(TEXT("passed"), true);
		FJsonSerializer::Serialize(Command->Result, TJsonWriterFactory<>::Create(&Json));
		return FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("Automation/MCPToolkit/OwnedPointerResult.json")));
	}
	UWidget* Source = FindObject<UWidget>(nullptr, *Request->GetStringField(TEXT("source")));
	UWidget* Target = FindObject<UWidget>(nullptr, *Request->GetStringField(TEXT("target")));
	if (!Source || !Target || !Source->IsIn(HUD) || !Target->IsIn(HUD)) { UE_LOG(LogTemp, Error, TEXT("Pointer widgets outside owned HUD.")); return false; }
	const TSharedPtr<SWidget> SourceSlateWidget = Source->GetCachedWidget();
	if (!SourceSlateWidget.IsValid() || !Target->GetCachedWidget().IsValid()) { UE_LOG(LogTemp, Error, TEXT("Unconstructed pointer widget.")); return false; }
	Command->Window = FSlateApplication::Get().FindWidgetWindow(SourceSlateWidget.ToSharedRef());
	if (!Command->Window.IsValid() || !Command->Window->GetNativeWindow().IsValid()) { UE_LOG(LogTemp, Error, TEXT("No live Slate window.")); return false; }
	const FGeometry& SourceGeometry = Source->GetCachedGeometry();
	const FGeometry& TargetGeometry = Target->GetCachedGeometry();
	if (SourceGeometry.GetLocalSize().IsNearlyZero() || TargetGeometry.GetLocalSize().IsNearlyZero()) { UE_LOG(LogTemp, Error, TEXT("Unarranged pointer target.")); return false; }
	Command->Start = SourceGeometry.LocalToAbsolute(SourceGeometry.GetLocalSize() * .5f);
	Command->End = TargetGeometry.LocalToAbsolute(TargetGeometry.GetLocalSize() * .5f);
	bOwnedPointerActive = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Command](float) { return !Command->Update(); }));
	return true;
}

static FAutoConsoleCommand OwnedPIEPointerConsole(
	TEXT("MCT.OwnedPIEPointer"), TEXT("Run an explicit current-editor owned-PIE pointer/geometry request."),
	FConsoleCommandDelegate::CreateLambda([]() { RunOwnedPIEPointerRequest(); }));

#endif
