// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTWidgetCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleCreateWidgetBlueprint(Params);
}

FString FMCTTcpServer::HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleAddWidget(Params);
}

FString FMCTTcpServer::HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleRemoveWidget(Params);
}

FString FMCTTcpServer::HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleMoveWidget(Params);
}

FString FMCTTcpServer::HandleReplaceWidget(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleReplaceWidget(Params);
}

FString FMCTTcpServer::HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleSetWidgetProperty(Params);
}

FString FMCTTcpServer::HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleSetSlotProperty(Params);
}

FString FMCTTcpServer::HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleSetCanvasSlotLayout(Params);
}

FString FMCTTcpServer::HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleSetWidgetProperties(Params);
}

FString FMCTTcpServer::HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleReparentBlueprint(Params);
}

FString FMCTTcpServer::HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleCompileAndSave(Params);
}

FString FMCTTcpServer::HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Widget::HandleGetWidgetTree(Params);
}

FString FMCTTcpServer::HandleListWidgetClasses()
{
	return MCPToolkit::CommandHandlers::Widget::HandleListWidgetClasses();
}
