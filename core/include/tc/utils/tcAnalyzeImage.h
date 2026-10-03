#pragma once
// Internal CPU analysis shared by the MCP handlers and headless regression test.
namespace trussc::mcp::detail {
json analyzeImage(const Pixels& pixels, const json& args, const char* colorSpace);
}
