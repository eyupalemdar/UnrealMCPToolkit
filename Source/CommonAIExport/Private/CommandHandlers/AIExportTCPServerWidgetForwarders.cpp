// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportWidgetCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleCreateWidgetBlueprint(Params);
}

FString FAIExportTCPServer::HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleAddWidget(Params);
}

FString FAIExportTCPServer::HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleRemoveWidget(Params);
}

FString FAIExportTCPServer::HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleMoveWidget(Params);
}

FString FAIExportTCPServer::HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetWidgetProperty(Params);
}

FString FAIExportTCPServer::HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetSlotProperty(Params);
}

FString FAIExportTCPServer::HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetCanvasSlotLayout(Params);
}

FString FAIExportTCPServer::HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetWidgetProperties(Params);
}

FString FAIExportTCPServer::HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleReparentBlueprint(Params);
}

FString FAIExportTCPServer::HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleCompileAndSave(Params);
}

FString FAIExportTCPServer::HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleGetWidgetTree(Params);
}

FString FAIExportTCPServer::HandleListWidgetClasses()
{
	return CommonAIExport::CommandHandlers::Widget::HandleListWidgetClasses();
}
