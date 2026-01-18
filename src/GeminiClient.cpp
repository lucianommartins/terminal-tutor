/**
 * GeminiClient.cpp - HTTP client for Gemini API
 * 
 * Uses cpp-httplib for HTTPS requests to Gemini API.
 * Supports optional multi-turn conversations with named sessions.
 */

#include "tt/GeminiClient.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <curl/curl.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace tt {

static const std::string GEMINI_API_BASE = "generativelanguage.googleapis.com";
static const std::string DEFAULT_MODEL = "gemini-3-flash-preview";
static const std::string DEFAULT_LANGUAGE = "en-us";
static const size_t MAX_HISTORY_TURNS = 10;

std::string getSessionDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.tt";
}

struct GeminiClient::Impl {
    std::string api_key;
    std::string model;
    std::string language;
    std::string session_path; // empty = no persistence
    std::unique_ptr<httplib::SSLClient> client;
    json history;
    
    Impl(const std::string& key, const std::string& model_name, 
         const std::string& lang, const std::string& session_name) 
        : api_key(key), 
          model(model_name.empty() ? DEFAULT_MODEL : model_name),
          language(lang.empty() ? DEFAULT_LANGUAGE : lang) {
        
        // Set up session path if name provided
        if (!session_name.empty()) {
            std::string dir = getSessionDir();
            if (!dir.empty()) {
                std::filesystem::create_directories(dir);
                std::filesystem::permissions(dir, std::filesystem::perms::owner_all, 
                                            std::filesystem::perm_options::replace);
                session_path = dir + "/" + session_name + ".json";
            }
        }
        
        client = std::make_unique<httplib::SSLClient>(GEMINI_API_BASE);
        client->set_connection_timeout(30);
        client->set_read_timeout(60);
        client->set_write_timeout(30);
        
        loadSession();
    }
    
    void loadSession() {
        history = json::array();
        if (session_path.empty()) return;
        
        std::ifstream file(session_path);
        if (file.good()) {
            try {
                file >> history;
                if (!history.is_array()) {
                    history = json::array();
                }
            } catch (...) {
                history = json::array();
            }
        }
    }
    
    void saveSession() {
        if (session_path.empty()) return;
        
        // Trim history if too long
        while (history.size() > MAX_HISTORY_TURNS * 2) {
            history.erase(history.begin());
            history.erase(history.begin());
        }
        
        std::ofstream file(session_path);
        file << history.dump(2);
        file.close();
        std::filesystem::permissions(session_path, 
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
    }
    
    void addToHistory(const std::string& role, const std::string& text) {
        if (session_path.empty()) return;
        
        history.push_back({
            {"role", role},
            {"parts", {{{"text", text}}}}
        });
        saveSession();
    }
    
    std::string getLanguageInstruction() {
        if (language == "en-us" || language == "en") {
            return "Respond in English.";
        } else if (language == "pt-br" || language == "pt") {
            return "Respond in Portuguese (Brazilian).";
        } else if (language == "es" || language == "es-es") {
            return "Respond in Spanish.";
        } else {
            return "Respond in " + language + ".";
        }
    }
    
    std::string buildEndpoint() {
        return "/v1beta/models/" + model + ":generateContent?key=" + api_key;
    }
    
    GeminiResponse sendRequest(const std::string& prompt, bool use_history = true) {
        GeminiResponse response;
        
        json contents = json::array();
        
        // Include history only if session is active
        if (use_history && !session_path.empty() && !history.empty()) {
            contents = history;
        }
        
        contents.push_back({
            {"role", "user"},
            {"parts", {{{"text", prompt}}}}
        });
        
        json request_body = {{"contents", contents}};
        
        auto res = client->Post(buildEndpoint(), request_body.dump(), "application/json");
        
        if (!res) {
            response.success = false;
            response.error = "Network error: " + httplib::to_string(res.error());
            return response;
        }
        
        if (res->status != 200) {
            response.success = false;
            response.error = "API error: HTTP " + std::to_string(res->status);
            try {
                json error_json = json::parse(res->body);
                if (error_json.contains("error")) {
                    response.error += " - " + error_json["error"]["message"].get<std::string>();
                }
            } catch (...) {}
            return response;
        }
        
        try {
            json res_json = json::parse(res->body);
            
            if (res_json.contains("candidates") && 
                !res_json["candidates"].empty() &&
                res_json["candidates"][0].contains("content") &&
                res_json["candidates"][0]["content"].contains("parts") &&
                !res_json["candidates"][0]["content"]["parts"].empty()) {
                
                response.content = res_json["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                response.success = true;
                
                // Save to history only if session is active
                if (use_history && !session_path.empty()) {
                    history.push_back({
                        {"role", "user"},
                        {"parts", {{{"text", prompt}}}}
                    });
                    history.push_back({
                        {"role", "model"},
                        {"parts", {{{"text", response.content}}}}
                    });
                    saveSession();
                }
            } else {
                response.success = false;
                response.error = "Invalid response structure";
            }
        } catch (const std::exception& e) {
            response.success = false;
            response.error = std::string("JSON parse error: ") + e.what();
        }
        
        return response;
    }
};

GeminiClient::GeminiClient(const std::string& api_key, const std::string& model, 
                           const std::string& language, const std::string& session_name)
    : impl_(std::make_unique<Impl>(api_key, model, language, session_name)) {}

GeminiClient::~GeminiClient() = default;

std::string GeminiClient::getDefaultModel() {
    return DEFAULT_MODEL;
}

std::string GeminiClient::getDefaultLanguage() {
    return DEFAULT_LANGUAGE;
}

std::vector<std::string> GeminiClient::listSessions() {
    std::vector<std::string> sessions;
    std::string dir = getSessionDir();
    if (dir.empty() || !std::filesystem::exists(dir)) {
        return sessions;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            sessions.push_back(entry.path().stem().string());
        }
    }
    return sessions;
}

bool GeminiClient::validate(std::string& error_message) {
    auto response = impl_->sendRequest("Respond with only the word OK", false);
    if (!response.success) {
        error_message = response.error;
        return false;
    }
    return true;
}

void GeminiClient::addCommandOutput(const std::string& command, const std::string& output) {
    // Add as a "user" message showing what command was executed and its output
    // This gives the model context for follow-up questions
    std::string context = "I executed: " + command + "\n\nOutput:\n" + output;
    impl_->addToHistory("user", context);
    impl_->addToHistory("model", "Got it. I'll remember this output for context.");
}

int GeminiClient::countSessionTokens() {
    // If no session, return 0
    if (impl_->session_path.empty() || impl_->history.empty()) {
        return 0;
    }
    
    // Build request body with session contents
    nlohmann::json request_body;
    request_body["contents"] = impl_->history;
    
    std::string path = "/v1beta/models/" + impl_->model + ":countTokens?key=" + impl_->api_key;
    
    httplib::SSLClient client("generativelanguage.googleapis.com");
    client.set_connection_timeout(10);
    client.set_read_timeout(10);
    
    auto res = client.Post(path, request_body.dump(), "application/json");
    
    if (!res || res->status != 200) {
        return -1;
    }
    
    try {
        auto json_res = nlohmann::json::parse(res->body);
        if (json_res.contains("totalTokens")) {
            return json_res["totalTokens"].get<int>();
        }
    } catch (...) {}
    
    return -1;
}

SmartResponse GeminiClient::smartQuery(const std::string& query) {
    SmartResponse result;
    result.success = false;
    
    std::ostringstream prompt;
    prompt << "User request: " << query << "\n\n"
           << "Analyze the request:\n"
           << "1. EXECUTE: If user wants to DO something with the system (find files, list processes, check disk, etc.)\n"
           << "2. EXPLAIN: For greetings, questions about concepts, explanations (hi, hello, why, what is, how does X work)\n\n"
           << "Greetings like 'hi', 'hello', 'ola' are ALWAYS type explain.\n"
           << "Only use execute if the user clearly wants to run a shell command.\n\n"
           << "Respond with ONLY valid JSON:\n"
           << "Execute: {\"type\":\"execute\",\"command\":\"shell command\",\"explanation\":\"1-line plain text explanation\"}\n"
           << "Explain: {\"type\":\"explain\",\"response\":\"plain text response\"}\n\n"
           << "CRITICAL: No markdown, no backticks, no asterisks, no formatting. Plain text only.\n"
           << impl_->getLanguageInstruction();
    
    auto response = impl_->sendRequest(prompt.str());
    
    if (!response.success) {
        result.type = SmartResponse::Type::ERROR;
        result.error = response.error;
        return result;
    }
    
    // Parse JSON response
    try {
        // Clean up response - remove any markdown formatting
        std::string content = response.content;
        
        // Remove ```json and ``` if present
        size_t start = content.find('{');
        size_t end = content.rfind('}');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            content = content.substr(start, end - start + 1);
        }
        
        auto json_response = nlohmann::json::parse(content);
        std::string type = json_response["type"].get<std::string>();
        
        if (type == "execute") {
            result.type = SmartResponse::Type::EXECUTE;
            result.command = json_response["command"].get<std::string>();
            // Get explanation if present
            if (json_response.contains("explanation")) {
                result.explanation = json_response["explanation"].get<std::string>();
            }
            result.success = true;
        } else if (type == "explain") {
            result.type = SmartResponse::Type::EXPLAIN;
            result.explanation = json_response["response"].get<std::string>();
            result.success = true;
        } else {
            result.type = SmartResponse::Type::ERROR;
            result.error = "Unknown response type: " + type;
        }
    } catch (const std::exception& e) {
        // Fallback: treat as explanation if JSON parsing fails
        result.type = SmartResponse::Type::EXPLAIN;
        result.explanation = response.content;
        result.success = true;
    }
    
    return result;
}

// Curl write callback data
struct CurlStreamContext {
    std::string buffer;
    std::string accumulated;
    GeminiClient::StreamCallback callback;
    bool type_determined = false;
    std::string type;
};

static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<CurlStreamContext*>(userdata);
    size_t total = size * nmemb;
    ctx->buffer.append(ptr, total);
    
    // Parse SSE events as they arrive
    size_t pos;
    while ((pos = ctx->buffer.find("\n")) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);
        
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.rfind("data: ", 0) == 0) {
            std::string json_str = line.substr(6);
            try {
                auto json_event = nlohmann::json::parse(json_str);
                if (json_event.contains("candidates") && 
                    !json_event["candidates"].empty() &&
                    json_event["candidates"][0].contains("content") &&
                    json_event["candidates"][0]["content"].contains("parts") &&
                    !json_event["candidates"][0]["content"]["parts"].empty()) {
                    
                    std::string chunk = json_event["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                    ctx->accumulated += chunk;
                    
                    // Stream output immediately for visual feedback
                    if (ctx->callback) {
                        ctx->callback(chunk);
                    }
                }
            } catch (...) {}
        }
    }
    
    return total;
}

SmartResponse GeminiClient::smartQueryStreaming(const std::string& query, StreamCallback on_chunk) {
    SmartResponse result;
    result.success = false;
    
    std::ostringstream prompt;
    prompt << "User request: " << query << "\n\n"
           << "Analyze the request:\n"
           << "1. EXECUTE: If user wants to DO something with the system (find files, list processes, check disk, etc.)\n"
           << "2. EXPLAIN: For greetings, questions about concepts, explanations (hi, hello, why, what is, how does X work)\n\n"
           << "Greetings like 'hi', 'hello', 'ola' are ALWAYS type explain.\n"
           << "Only use execute if the user clearly wants to run a shell command.\n\n"
           << "Respond with ONLY valid JSON:\n"
           << "Execute: {\"type\":\"execute\",\"command\":\"shell command\",\"explanation\":\"1-line plain text explanation\"}\n"
           << "Explain: {\"type\":\"explain\",\"response\":\"plain text response\"}\n\n"
           << "CRITICAL: No markdown, no backticks, no asterisks, no formatting. Plain text only.\n"
           << impl_->getLanguageInstruction();
    
    // Build request body
    nlohmann::json contents = nlohmann::json::array();
    if (!impl_->session_path.empty() && !impl_->history.empty()) {
        contents = impl_->history;
    }
    contents.push_back({
        {"role", "user"},
        {"parts", {{{"text", prompt.str()}}}}
    });
    
    nlohmann::json request_body = {{"contents", contents}};
    std::string body = request_body.dump();
    
    // Build URL
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + 
                      impl_->model + ":streamGenerateContent?alt=sse&key=" + impl_->api_key;
    
    // Setup curl
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.type = SmartResponse::Type::ERROR;
        result.error = "Failed to initialize curl";
        return result;
    }
    
    CurlStreamContext ctx;
    ctx.callback = on_chunk;
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    // Streaming options - minimize buffering
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        result.type = SmartResponse::Type::ERROR;
        result.error = std::string("Curl error: ") + curl_easy_strerror(res);
        return result;
    }
    
    // Parse the accumulated JSON response
    try {
        size_t start = ctx.accumulated.find('{');
        size_t end = ctx.accumulated.rfind('}');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string json_content = ctx.accumulated.substr(start, end - start + 1);
            auto json_response = nlohmann::json::parse(json_content);
            std::string type = json_response["type"].get<std::string>();
            
            if (type == "execute") {
                result.type = SmartResponse::Type::EXECUTE;
                result.command = json_response["command"].get<std::string>();
                if (json_response.contains("explanation")) {
                    result.explanation = json_response["explanation"].get<std::string>();
                }
                result.success = true;
            } else if (type == "explain") {
                result.type = SmartResponse::Type::EXPLAIN;
                result.explanation = json_response["response"].get<std::string>();
                result.success = true;
            }
        }
    } catch (const std::exception& e) {
        // Fallback: treat as explanation
        result.type = SmartResponse::Type::EXPLAIN;
        result.explanation = ctx.accumulated;
        result.success = true;
    }
    
    return result;
}

void GeminiClient::generateContentStreaming(const std::string& prompt, StreamCallback on_chunk) {
    // Build request body with plain text prompt
    nlohmann::json contents = nlohmann::json::array();
    if (!impl_->session_path.empty() && !impl_->history.empty()) {
        contents = impl_->history;
    }
    
    std::string full_prompt = prompt + "\n\n" + impl_->getLanguageInstruction() + 
                              "\n\nCRITICAL: Respond in plain text only. No markdown, no formatting.";
    
    contents.push_back({
        {"role", "user"},
        {"parts", {{{"text", full_prompt}}}}
    });
    
    nlohmann::json request_body = {{"contents", contents}};
    std::string body = request_body.dump();
    
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + 
                      impl_->model + ":streamGenerateContent?alt=sse&key=" + impl_->api_key;
    
    CURL* curl = curl_easy_init();
    if (!curl) return;
    
    CurlStreamContext ctx;
    ctx.callback = on_chunk;
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    // Add to session history
    if (!impl_->session_path.empty()) {
        impl_->addToHistory("user", prompt);
        impl_->addToHistory("model", ctx.accumulated);
    }
}

GeminiResponse GeminiClient::getCommandForTask(const std::string& task) {
    GeminiResponse result;
    
    std::ostringstream prompt;
    prompt << "User wants to: " << task << "\n\n"
           << "Respond with ONLY a JSON object:\n"
           << "{\"command\":\"the shell command\",\"explanation\":\"1-line explanation\"}\n\n"
           << "CRITICAL: Return ONLY valid JSON. No markdown, no text before or after.";
    
    auto response = impl_->sendRequest(prompt.str());
    
    if (!response.success) {
        return response;
    }
    
    // Parse JSON to extract command
    try {
        size_t start = response.content.find('{');
        size_t end = response.content.rfind('}');
        if (start != std::string::npos && end != std::string::npos) {
            auto json = nlohmann::json::parse(response.content.substr(start, end - start + 1));
            result.content = json["command"].get<std::string>();
            result.success = true;
            result.error = json.contains("explanation") ? json["explanation"].get<std::string>() : "";
        }
    } catch (...) {
        result.content = response.content;
        result.success = true;
    }
    
    return result;
}

GeminiResponse GeminiClient::generateContent(const std::string& prompt) {
    return impl_->sendRequest(prompt);
}

GeminiResponse GeminiClient::explainCommand(const std::string& command) {
    std::ostringstream prompt;
    prompt << "Explain this command briefly and directly: " << command << "\n\n"
           << "Format: One short paragraph with what it does, then each flag explained in one line. "
           << "No emojis, no bullet points, no headers. Keep it under 100 words. "
           << impl_->getLanguageInstruction();
    
    return impl_->sendRequest(prompt.str());
}

GeminiResponse GeminiClient::suggestCommand(const std::string& task_description) {
    std::ostringstream prompt;
    prompt << "User wants to: " << task_description << "\n\n"
           << "Give the exact command, then one sentence explaining it. "
           << "No emojis, no bullet points. Keep it very short. "
           << impl_->getLanguageInstruction();
    
    return impl_->sendRequest(prompt.str());
}

GeminiResponse GeminiClient::getCommandOnly(const std::string& task_description) {
    std::ostringstream prompt;
    prompt << "User wants to: " << task_description << "\n\n"
           << "Respond with ONLY the exact shell command, nothing else. "
           << "No explanation, no quotes, no backticks. Just the raw command.";
    
    return impl_->sendRequest(prompt.str()); // Uses history for context
}

GeminiResponse GeminiClient::simulateCommand(const std::string& command, const std::string& context) {
    std::ostringstream prompt;
    prompt << "Predict what happens when running: " << command << "\n";
    
    if (!context.empty()) {
        prompt << "Context: " << context << "\n";
    }
    
    prompt << "\nGive a brief prediction: what files are affected, expected output, any risks. "
           << "No emojis, no bullet points. Keep it very short and direct. "
           << impl_->getLanguageInstruction();
    
    return impl_->sendRequest(prompt.str());
}

} // namespace tt
