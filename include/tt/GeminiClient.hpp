/**
 * GeminiClient.hpp - HTTP client for Gemini API
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tt {

struct GeminiResponse {
    std::string content;
    bool success;
    std::string error;
};

// Smart response with intent detection
struct SmartResponse {
    enum class Type { EXECUTE, EXPLAIN, ERROR };
    Type type;
    std::string command;     // If type == EXECUTE
    std::string explanation; // If type == EXPLAIN
    std::string error;       // If type == ERROR
    bool success;
};

class GeminiClient {
public:
    // Callback for streaming responses
    using StreamCallback = std::function<void(const std::string& chunk)>;
    
    // session_name: empty = no persistence, "name" = ~/.tt/name.json
    GeminiClient(const std::string& api_key, const std::string& model = "", 
                 const std::string& language = "", const std::string& session_name = "");
    ~GeminiClient();
    
    // Smart query - detects if user wants to execute or just get explanation
    SmartResponse smartQuery(const std::string& query);
    
    // Streaming smart query - outputs explanation in real-time
    SmartResponse smartQueryStreaming(const std::string& query, StreamCallback on_chunk);
    
    // Streaming content generation - plain text, real-time output
    void generateContentStreaming(const std::string& prompt, StreamCallback on_chunk);
    
    // Get command for --run mode (returns JSON with command)
    GeminiResponse getCommandForTask(const std::string& task);
    
    GeminiResponse generateContent(const std::string& prompt);
    GeminiResponse explainCommand(const std::string& command);
    GeminiResponse suggestCommand(const std::string& task_description);
    GeminiResponse getCommandOnly(const std::string& task_description);
    GeminiResponse simulateCommand(const std::string& command, const std::string& context);
    
    // Validate API key and model by making a test request
    bool validate(std::string& error_message);
    
    // Add executed command and its output to session history
    void addCommandOutput(const std::string& command, const std::string& output);
    
    // Count tokens in current session, returns -1 on error
    int countSessionTokens();
    
    // List available sessions in ~/.tt/
    static std::vector<std::string> listSessions();
    
    static std::string getDefaultModel();
    static std::string getDefaultLanguage();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tt
