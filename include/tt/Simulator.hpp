/**
 * Simulator.hpp - "What If" command simulation
 */

#pragma once

#include <string>
#include <vector>

namespace tt {

class GeminiClient;

struct SimulationResult {
    std::string predicted_output;
    std::vector<std::string> files_affected;
    std::vector<std::string> warnings;
    bool is_destructive;
};

class Simulator {
public:
    explicit Simulator(GeminiClient& gemini);
    ~Simulator();
    
    SimulationResult simulate(const std::string& command);
    bool isDangerous(const std::string& command);
    
private:
    GeminiClient& gemini_;
    
    std::vector<std::string> dangerous_patterns_;
};

} // namespace tt
