#pragma once

#include <nlohmann/json.hpp>
#include <string>

// Wires up all Crow routes and runs the server.
// This call blocks until the server is stopped.
void run_server(const nlohmann::json &config);
