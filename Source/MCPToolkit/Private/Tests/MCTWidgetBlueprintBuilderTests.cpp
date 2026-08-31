// Copyright (c) 2026 Alemdar Labs. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Builders/MCTWidgetBlueprintBuilder.h"
#include "Tests/MCTWidgetBuilderBindWidgetTestTypes.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"

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

#endif
