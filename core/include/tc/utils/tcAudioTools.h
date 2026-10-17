#pragma once

namespace trussc::mcp::internal { class Server; }

namespace trussc::mcp::internal {
void reserveAudioSnapshot(size_t frames, size_t channels);
void registerAudioTools(Server& server);
}
