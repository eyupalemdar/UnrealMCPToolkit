// Copyright (c) 2026 Alemdar Labs. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"

#include "MCTWidgetBuilderBindWidgetTestTypes.generated.h"

class UImage;
class UTextBlock;

/** Reflection fixture for the Widget Builder's native BindWidget name policy. */
UCLASS()
class UMCTWidgetBuilderBindWidgetTestParent final : public UUserWidget
{
	GENERATED_BODY()

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RequiredText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> OptionalImage;

	UPROPERTY()
	TObjectPtr<UTextBlock> OrdinaryText;
};
