/**
 * ExplainerEngine.hpp - Command explanation engine with multiple modes
 */

#pragma once

#include <string>

namespace tt {

class GeminiClient;

enum class ExplainMode {
    NORMAL,     // Technical explanation
    ELI5,       // Explain Like I'm 5
    DETAILED    // With examples and use cases
};

class ExplainerEngine {
public:
    explicit ExplainerEngine(GeminiClient& gemini);
    ~ExplainerEngine();
    
    std::string explain(const std::string& command, ExplainMode mode);
    std::string suggestFix(const std::string& failed_command, const std::string& error_msg);
    std::string translateQuestion(const std::string& question);
    
private:
    GeminiClient& gemini_;
    
    std::string buildExplainPrompt(const std::string& command, ExplainMode mode);
};

} // namespace tt
