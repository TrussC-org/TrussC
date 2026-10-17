#pragma once
// Internal CPU analysis shared by the MCP handlers and headless regression test.
namespace trussc::mcp::internal {
bool loadMcpImage(Pixels& pixels, const fs::path& path);
void validateAnalyzeArguments(const json& args);
json analyzeImage(const Pixels& pixels, const json& args, const char* colorSpace);
}
