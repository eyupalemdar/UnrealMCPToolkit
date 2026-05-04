// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MCPToolkit::CommandHandlers
{
inline FString CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetBoolField(TEXT("success"), false);
	ResponseObj->SetStringField(TEXT("error"), ErrorMessage);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);
	return OutputString;
}

inline FString CreateSuccessResponse(TSharedPtr<FJsonObject> Data = nullptr)
{
	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetBoolField(TEXT("success"), true);
	if (Data.IsValid())
	{
		ResponseObj->SetObjectField(TEXT("data"), Data);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);
	return OutputString;
}
}
