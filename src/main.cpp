/**
 * main.cpp - TerminalTutor CLI entry point
 * 
 * Usage:
 *   tt "como eu encontro arquivos grandes?"     # Natural language query
 *   tt explain "find . -type f -size +100M"     # Explain command
 *   tt eli5 "grep -rn pattern ."                # Explain Like I'm 5
 *   tt whatif "rm -rf ./build"                  # Simulate command
 *   tt auth <api_key>                           # Store API key securely
 */

#include "tt/CommandParser.hpp"
#include "tt/GeminiClient.hpp"
#include "tt/ExplainerEngine.hpp"
#include "tt/Simulator.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

#include <libsecret/secret.h>

namespace {

const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
const std::string YELLOW = "\033[33m";
const std::string GREEN = "\033[32m";
const std::string RED = "\033[31m";
const std::string CYAN = "\033[36m";

// Dangerous commands that require confirmation
const std::vector<std::string> DANGEROUS_COMMANDS = {
    // File deletion
    "rm", "rmdir", "unlink", "shred",
    // System control
    "shutdown", "reboot", "poweroff", "halt", "init",
    // Disk/filesystem
    "mkfs", "fdisk", "parted", "dd", "format", "mkswap",
    // Package management (can break system)
    "apt-get remove", "apt remove", "apt-get purge", "apt purge",
    "yum remove", "dnf remove", "pacman -R",
    // Permission/ownership
    "chmod 777", "chmod -R", "chown -R", "chgrp -R",
    // Network
    "iptables -F", "ufw disable",
    // Process control
    "kill -9", "killall", "pkill",
    // Dangerous patterns
    ":(){", "fork bomb",
    // User management
    "userdel", "deluser", "passwd",
    // Elevated privileges
    "sudo",
};

// Dangerous patterns (regex-like checks)
const std::vector<std::string> DANGEROUS_PATTERNS = {
    "> /dev/",      // Writing to devices
    ">/dev/",
    "> /etc/",      // Overwriting system configs
    ">/etc/",
    "> /boot/",     // Overwriting boot
    ">/boot/",
    "| rm",         // Piping to rm
    "|rm",
    "| dd",         // Piping to dd
    "|dd",
    "rf /",         // rm -rf /
    "rf ~/",        // rm -rf home
    "rf ~",
    "rf .",         // rm -rf current dir
    "mv /* ",       // Moving root
    "mv / ",
    "> /",          // Overwriting files with redirect
    "| tee /",      // Overwriting via tee
    "|tee /",
    "chmod 000",    // Removing all permissions
    ":(){ :",       // Fork bomb
    "/dev/null >",  // Redirecting to null
    "/dev/zero",    // dd from zero
    "/dev/random",
};

bool isDangerousCommand(const std::string& cmd) {
    std::string lower_cmd = cmd;
    std::transform(lower_cmd.begin(), lower_cmd.end(), lower_cmd.begin(), ::tolower);
    
    // Check exact command matches (at start or after pipe/sudo)
    for (const auto& dangerous : DANGEROUS_COMMANDS) {
        // Check if command starts with dangerous command
        if (lower_cmd.find(dangerous) == 0) return true;
        // Check after pipe
        if (lower_cmd.find("| " + dangerous) != std::string::npos) return true;
        if (lower_cmd.find("|" + dangerous) != std::string::npos) return true;
        // Check after sudo
        if (lower_cmd.find("sudo " + dangerous) != std::string::npos) return true;
    }
    
    // Check patterns
    for (const auto& pattern : DANGEROUS_PATTERNS) {
        if (lower_cmd.find(pattern) != std::string::npos) return true;
    }
    
    return false;
}

bool askDangerousConfirmation(const std::string& cmd) {
    std::cout << "\n" << RED << BOLD << "⚠️  WARNING: POTENTIALLY DANGEROUS COMMAND!" << RESET << "\n";
    std::cout << RED << "This command may cause irreversible damage to your system or data." << RESET << "\n";
    std::cout << "Command: " << BOLD << cmd << RESET << "\n\n";
    std::cout << YELLOW << "Type 'yes' to confirm execution: " << RESET;
    std::cout.flush();
    
    std::string response;
    std::getline(std::cin, response);
    return (response == "yes");
}

// libsecret schema for storing the API key
const SecretSchema TT_API_SCHEMA = {
    "com.terminaltutor.credentials",
    SECRET_SCHEMA_NONE,
    {
        {"type", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {NULL, SECRET_SCHEMA_ATTRIBUTE_STRING}
    }
};

std::string getFromKeyring(const std::string& type) {
    GError* error = nullptr;
    gchar* value = secret_password_lookup_sync(
        &TT_API_SCHEMA,
        nullptr,
        &error,
        "type", type.c_str(),
        NULL
    );
    
    if (error != nullptr) {
        g_error_free(error);
        return "";
    }
    
    if (value == nullptr) {
        return "";
    }
    
    std::string result(value);
    secret_password_free(value);
    return result;
}

bool storeInKeyring(const std::string& type, const std::string& value, const std::string& label) {
    GError* error = nullptr;
    gboolean success = secret_password_store_sync(
        &TT_API_SCHEMA,
        SECRET_COLLECTION_DEFAULT,
        label.c_str(),
        value.c_str(),
        nullptr,
        &error,
        "type", type.c_str(),
        NULL
    );
    
    if (error != nullptr) {
        std::cerr << RED << "Error saving: " << error->message << RESET << "\n";
        g_error_free(error);
        return false;
    }
    
    return success == TRUE;
}

std::string getApiKey() {
    // 1. Try libsecret/keyring first (most secure)
    std::string key = getFromKeyring("api_key");
    if (!key.empty()) {
        return key;
    }
    
    // 2. Try environment variable
    const char* env_key = std::getenv("GEMINI_API_KEY");
    if (env_key && strlen(env_key) > 0) {
        return env_key;
    }
    
    // 3. Try config file (fallback)
    std::filesystem::path config_path = std::filesystem::path(std::getenv("HOME")) / ".config" / "tt" / "api_key";
    if (std::filesystem::exists(config_path)) {
        std::ifstream file(config_path);
        std::getline(file, key);
        if (!key.empty()) {
            return key;
        }
    }
    
    return "";
}

std::string getModel() {
    std::string model = getFromKeyring("model");
    if (!model.empty()) {
        return model;
    }
    return tt::GeminiClient::getDefaultModel();
}

std::string getLanguage() {
    std::string lang = getFromKeyring("language");
    if (!lang.empty()) {
        return lang;
    }
    return tt::GeminiClient::getDefaultLanguage();
}

void printUsage() {
    std::cout << BOLD << "TerminalTutor" << RESET << " - CLI tutor that lives in your shell\n\n"
              << BOLD << "Usage:" << RESET << "\n"
              << "  tt \"your question\"               Ask anything (streaming)\n"
              << "  tt --run \"task\"                  Execute a command for the task\n"
              << "  tt explain <command>            Explain the command\n"
              << "  tt eli5 <command>               Explain like I'm 5\n"
              << "  tt whatif <command>             Simulate what would happen\n"
              << "  tt --console                    Interactive console mode\n"
              << "  tt --auth                       Store API key securely\n"
              << "  tt --config list                Show current configuration\n"
              << "  tt --config reset               Reset to defaults\n"
              << "  tt --config model=<name>        Set Gemini model\n"
              << "  tt --config language=<lang>     Set response language\n"
              << "  tt --session <name> \"query\"     Persistent conversation\n"
              << "  tt --session list               List sessions\n"
              << "  tt --session delete <name>      Delete session\n"
              << "  tt --help                       Show this help\n\n"
              << BOLD << "Examples:" << RESET << "\n"
              << "  tt \"what is a process?\"                     # streaming explanation\n"
              << "  tt --run \"find the largest file\"            # executes command\n"
              << "  tt --session proj \"build this project\"      # with context\n"
              << "  tt --session proj --run \"run tests\"         # execute with context\n\n"
              << BOLD << "Current Config:" << RESET << "\n"
              << "  Model: " << getModel() << "\n"
              << "  Language: " << getLanguage() << "\n";
}

void printSuggestion(const std::string& content) {
    std::cout << "\n" << YELLOW << "💡" << RESET << " " << content << "\n";
}

void printExplanation(const std::string& content) {
    std::cout << "\n" << CYAN << "📖" << RESET << " " << content << "\n";
}

void printWarning(const std::string& content) {
    std::cout << "\n" << RED << "⚠️  " << BOLD << content << RESET << "\n";
}

void printSimulation(const tt::SimulationResult& result) {
    if (result.is_destructive) {
        printWarning("POTENTIALLY DESTRUCTIVE COMMAND!");
    }
    
    for (const auto& warning : result.warnings) {
        std::cout << RED << "⚠️  " << warning << RESET << "\n";
    }
    
    std::cout << "\n" << CYAN << "🔮 Simulation:" << RESET << "\n";
    std::cout << result.predicted_output << "\n";
    
    if (!result.files_affected.empty()) {
        std::cout << "\n" << BOLD << "Files affected:" << RESET << "\n";
        for (const auto& file : result.files_affected) {
            std::cout << "  - " << file << "\n";
        }
    }
}

bool askConfirmation(const std::string& command) {
    std::cout << "\n" << GREEN << "Execute? [y/N] " << RESET;
    std::string response;
    std::getline(std::cin, response);
    return (response == "y" || response == "Y" || response == "yes");
}

int executeCommand(const std::string& command) {
    return std::system(command.c_str());
}

// Execute command and capture output (for session context)
std::pair<int, std::string> executeAndCapture(const std::string& command) {
    std::string output;
    std::array<char, 256> buffer;
    
    // Redirect stderr to stdout to capture all output
    std::string cmd = command + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {-1, "Failed to execute command"};
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        std::string chunk = buffer.data();
        output += chunk;
        std::cout << chunk; // Also print to terminal in real-time
        std::cout.flush();
    }
    
    int status = pclose(pipe);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    
    // Limit output size for history (first 2000 chars)
    if (output.size() > 2000) {
        output = output.substr(0, 2000) + "\n... [output truncated]";
    }
    
    return {exit_code, output};
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 0;
    }
    
    std::string first_arg = argv[1];
    
    if (first_arg == "--help" || first_arg == "-h") {
        printUsage();
        return 0;
    }
    
    // Parse all flags first (in any order)
    std::string session_name;
    bool run_mode = false;
    int arg_idx = 1;
    
    while (arg_idx < argc) {
        std::string arg = argv[arg_idx];
        
        if (arg == "--session") {
            arg_idx++;
            if (arg_idx >= argc) {
                std::cerr << RED << "Usage: tt --session <name|list|delete> [command]" << RESET << "\n";
                return 1;
            }
            std::string session_arg = argv[arg_idx];
            
            if (session_arg == "list") {
                auto sessions = tt::GeminiClient::listSessions();
                if (sessions.empty()) {
                    std::cout << "No sessions found.\n";
                } else {
                    std::cout << BOLD << "Available sessions:" << RESET << "\n";
                    for (const auto& s : sessions) {
                        std::cout << "  " << s << "\n";
                    }
                }
                return 0;
            }
            
            if (session_arg == "delete") {
                arg_idx++;
                if (arg_idx >= argc) {
                    std::cerr << RED << "Usage: tt --session delete <name>" << RESET << "\n";
                    return 1;
                }
                std::string to_delete = std::string(std::getenv("HOME")) + "/.tt/" + argv[arg_idx] + ".json";
                if (std::remove(to_delete.c_str()) == 0) {
                    std::cout << GREEN << "Session '" << argv[arg_idx] << "' deleted." << RESET << "\n";
                } else {
                    std::cerr << RED << "Session not found." << RESET << "\n";
                    return 1;
                }
                return 0;
            }
            
            session_name = session_arg;
            arg_idx++;
        }
        else if (arg == "--auth") {
            // --auth must be standalone with no other arguments
            if (argc != 2) {
                std::cerr << RED << "Error: --auth must be used alone." << RESET << "\n";
                std::cerr << "Usage: tt --auth\n";
                return 1;
            }
            
            // Prompt for API key without echoing (like password input)
            std::cout << "Paste your API key (hidden input): ";
            std::cout.flush();
            
            struct termios old_term, new_term;
            tcgetattr(STDIN_FILENO, &old_term);
            new_term = old_term;
            new_term.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
            
            std::string new_key;
            std::getline(std::cin, new_key);
            
            tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
            std::cout << "\n";
            
            if (new_key.empty()) {
                std::cerr << RED << "Error: Empty API key." << RESET << "\n";
                return 1;
            }
            
            std::cout << "Validating API key...\n";
            tt::GeminiClient test_client(new_key, getModel(), getLanguage());
            std::string error_msg;
            if (!test_client.validate(error_msg)) {
                std::cerr << RED << "Error: Invalid API key - " << error_msg << RESET << "\n";
                return 1;
            }
            
            if (storeInKeyring("api_key", new_key, "TerminalTutor API Key")) {
                std::cout << GREEN << "API key validated and saved!" << RESET << "\n";
                return 0;
            } else {
                return 1;
            }
        }
        else if (arg == "--config") {
            // --config must be standalone (only with its own argument)
            if (argc != 3) {
                std::cerr << RED << "Error: --config must be used alone with its argument." << RESET << "\n";
                std::cerr << "Usage: tt --config list|reset|model=<name>|language=<lang>\n";
                return 1;
            }
            
            arg_idx++;
            std::string config_arg = argv[arg_idx];
            
            // Handle --config list
            if (config_arg == "list") {
                std::cout << BOLD << "Current Configuration:" << RESET << "\n"
                          << "  Model:    " << getModel() << "\n"
                          << "  Language: " << getLanguage() << "\n";
                return 0;
            }
            
            // Handle --config reset
            if (config_arg == "reset") {
                // Note: libsecret doesn't have a simple delete, so we store the defaults
                storeInKeyring("model", tt::GeminiClient::getDefaultModel(), "TerminalTutor Model");
                storeInKeyring("language", tt::GeminiClient::getDefaultLanguage(), "TerminalTutor Language");
                std::cout << GREEN << "Configuration reset to defaults." << RESET << "\n";
                return 0;
            }
            
            if (config_arg.rfind("model=", 0) == 0) {
                std::string new_model = config_arg.substr(6);
                if (new_model.empty()) {
                    std::cerr << RED << "Error: Empty model name." << RESET << "\n";
                    return 1;
                }
                
                std::string api_key = getApiKey();
                if (api_key.empty()) {
                    std::cerr << RED << "Error: Configure API key first with 'tt auth'" << RESET << "\n";
                    return 1;
                }
                
                std::cout << "Validating model " << new_model << "...\n";
                tt::GeminiClient test_client(api_key, new_model, getLanguage());
                std::string error_msg;
                if (!test_client.validate(error_msg)) {
                    std::cerr << RED << "Error: Invalid model - " << error_msg << RESET << "\n";
                    return 1;
                }
                
                if (storeInKeyring("model", new_model, "TerminalTutor Model")) {
                    std::cout << GREEN << "Model validated and set: " << new_model << RESET << "\n";
                    return 0;
                } else {
                    return 1;
                }
            } else if (config_arg.rfind("language=", 0) == 0) {
                std::string new_lang = config_arg.substr(9);
                if (new_lang.empty()) {
                    std::cerr << RED << "Error: Empty language code." << RESET << "\n";
                    return 1;
                }
                
                if (storeInKeyring("language", new_lang, "TerminalTutor Language")) {
                    std::cout << GREEN << "Language set: " << new_lang << RESET << "\n";
                    return 0;
                } else {
                    return 1;
                }
            } else {
                std::cerr << RED << "Unknown config. Use: tt --config model=<name> or tt --config language=<lang>" << RESET << "\n";
                return 1;
            }
        }
        else if (arg == "--console") {
            // Interactive console mode
            arg_idx++;
            
            // Get API key first
            std::string api_key = getApiKey();
            if (api_key.empty()) {
                std::cerr << RED << "Error: API key not configured." << RESET << "\n";
                std::cerr << "Configure with: tt --auth\n";
                return 1;
            }
            
            // Initialize Gemini client (session may already be set from earlier --session flag)
            std::string model = getModel();
            std::string language = getLanguage();
            tt::GeminiClient gemini(api_key, model, language, session_name);
            
            std::cout << BOLD << "TerminalTutor Interactive Console" << RESET << "\n";
            if (!session_name.empty()) {
                std::cout << "Session: " << GREEN << session_name << RESET << "\n";
            }
            std::cout << "Type 'exit' or 'quit' to leave, 'clear' to clear session\n\n";
            
            std::string line;
            while (true) {
                std::cout << CYAN << "tt > " << RESET;
                std::cout.flush();
                
                if (!std::getline(std::cin, line)) {
                    break;  // EOF
                }
                
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\n\r"));
                line.erase(line.find_last_not_of(" \t\n\r") + 1);
                
                if (line.empty()) continue;
                
                if (line == "exit" || line == "quit") {
                    std::cout << "Goodbye!\n";
                    break;
                }
                
                if (line == "clear") {
                    // Clear session would require a clearHistory method
                    std::cout << "Session cleared.\n";
                    continue;
                }
                
                // Process query with smart query
                auto smart = gemini.smartQuery(line);
                
                if (!smart.success) {
                    std::cerr << RED << "Error: " << smart.error << RESET << "\n";
                    continue;
                }
                
                if (smart.type == tt::SmartResponse::Type::EXECUTE) {
                    std::string cmd = smart.command;
                    cmd.erase(0, cmd.find_first_not_of(" \n\r\t"));
                    cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1);
                    
                    // Show explanation
                    if (!smart.explanation.empty()) {
                        std::cout << "\n" << YELLOW << "💡 " << RESET << smart.explanation << "\n\n";
                    }
                    
                    // Check dangerous
                    if (isDangerousCommand(cmd)) {
                        if (!askDangerousConfirmation(cmd)) {
                            std::cout << "Aborted.\n\n";
                            continue;
                        }
                    }
                    
                    std::cout << CYAN << "$ " << cmd << RESET << "\n\n";
                    
                    auto [exit_code, output] = executeAndCapture(cmd);
                    
                    if (!session_name.empty()) {
                        gemini.addCommandOutput(cmd, output);
                    }
                    
                    std::cout << "\n";
                } else {
                    std::cout << "\n" << YELLOW << "💡 " << RESET << smart.explanation << "\n\n";
                }
            }
            
            return 0;
        }
        else if (arg == "--run") {
            // --run flag sets run_mode, requires a query
            run_mode = true;
            arg_idx++;
            break;  // Stop parsing flags, rest is the query
        }
        else if (arg.rfind("--", 0) == 0) {
            // Unknown flag starting with --
            std::cerr << RED << "Error: Unknown flag '" << arg << "'" << RESET << "\n";
            std::cerr << "Valid flags: --run, --session, --config, --auth, --console, --help\n";
            return 1;
        }
        else {
            // Not a flag, stop parsing flags
            break;
        }
    }
    
    // Remaining args are the command/query
    if (arg_idx >= argc) {
        std::cerr << RED << "Error: No command or question provided." << RESET << "\n";
        printUsage();
        return 1;
    }
    
    first_arg = argv[arg_idx];
    int arg_offset = arg_idx;

    
    // Get API key
    std::string api_key = getApiKey();
    if (api_key.empty()) {
        std::cerr << RED << "Error: API key not configured." << RESET << "\n";
        std::cerr << "Configure with: tt auth\n";
        return 1;
    }
    
    // Initialize components with configured model, language, and optional session
    std::string model = getModel();
    std::string language = getLanguage();
    tt::GeminiClient gemini(api_key, model, language, session_name);
    tt::CommandParser parser;
    tt::Simulator simulator(gemini);
    
    // Check session token usage if using a session
    if (!session_name.empty()) {
        int tokens = gemini.countSessionTokens();
        if (tokens >= 0) {
            const int TOKEN_LIMIT = 1000000;
            double usage = (double)tokens / TOKEN_LIMIT * 100.0;
            
            // DEBUG: Always show for testing
            std::cout << "[DEBUG] Session '" << session_name << "': " 
                      << tokens << " tokens (" << std::fixed << std::setprecision(2) << usage << "%)\n\n";
            
            if (usage >= 80.0) {
                std::cerr << RED << "⚠️  WARNING: Session '" << session_name << "' is using " 
                          << (int)usage << "% of token limit (" << tokens << " tokens).\n"
                          << "Consider creating a new session to avoid context overflow." << RESET << "\n\n";
            } else if (usage >= 50.0) {
                std::cerr << YELLOW << "💡 ATTENTION: Session '" << session_name << "' is using " 
                          << (int)usage << "% of token limit (" << tokens << " tokens).\n"
                          << "Consider creating a new session soon." << RESET << "\n\n";
            }
        }
    }
    
    // Determine mode and process
    if (first_arg == "explain" && argc > arg_offset + 1) {
        // Explain mode: tt explain <command>
        std::string command;
        for (int i = arg_offset + 1; i < argc; ++i) {
            if (i > arg_offset + 1) command += " ";
            command += argv[i];
        }
        
        auto response = gemini.explainCommand(command);
        if (response.success) {
            printExplanation(response.content);
        } else {
            std::cerr << RED << "Error: " << response.error << RESET << "\n";
            return 1;
        }
    }
    else if (first_arg == "eli5" && argc > arg_offset + 1) {
        // ELI5 mode: tt eli5 <command>
        std::string command;
        for (int i = arg_offset + 1; i < argc; ++i) {
            if (i > arg_offset + 1) command += " ";
            command += argv[i];
        }
        
        std::string prompt = 
            "Explain this command to a 5-year-old in 2-3 simple sentences using a real-world analogy: " + command + "\n\n"
            "No emojis, no bullet points. Very short and simple. "
            "Respond in the language corresponding to this locale: " + language + ".";
        
        auto response = gemini.generateContent(prompt);
        if (response.success) {
            printExplanation(response.content);
        } else {
            std::cerr << RED << "Error: " << response.error << RESET << "\n";
            return 1;
        }
    }
    else if (first_arg == "whatif" && argc > arg_offset + 1) {
        // What-if mode: tt whatif <command>
        std::string command;
        for (int i = arg_offset + 1; i < argc; ++i) {
            if (i > arg_offset + 1) command += " ";
            command += argv[i];
        }
        
        auto result = simulator.simulate(command);
        printSimulation(result);
    }
    else {
        // Natural language query mode
        std::string query;
        for (int i = arg_offset; i < argc; ++i) {
            if (i > arg_offset) query += " ";
            query += argv[i];
        }
        
        if (run_mode) {
            // --run mode: Get command and execute
            auto response = gemini.getCommandForTask(query);
            
            if (!response.success) {
                std::cerr << RED << "Error: " << response.error << RESET << "\n";
                return 1;
            }
            
            std::string cmd = response.content;
            cmd.erase(0, cmd.find_first_not_of(" \n\r\t"));
            cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1);
            
            // Show explanation (stored in error field from getCommandForTask)
            if (!response.error.empty()) {
                std::cout << "\n" << YELLOW << "💡 " << RESET << response.error << "\n\n";
            }
            
            // Check if command is dangerous
            if (isDangerousCommand(cmd)) {
                if (!askDangerousConfirmation(cmd)) {
                    std::cout << "Aborted.\n";
                    return 0;
                }
            }
            
            std::cout << CYAN << "$ " << cmd << RESET << "\n\n";
            
            // Execute and capture output for session context
            auto [exit_code, output] = executeAndCapture(cmd);
            
            // Save to session history for context in future queries
            if (!session_name.empty()) {
                gemini.addCommandOutput(cmd, output);
            }
            
            return exit_code;
        } else {
            // Default mode: Streaming explanation
            std::cout << "\n";
            gemini.generateContentStreaming(query, [](const std::string& chunk) {
                std::cout << chunk;
                std::cout.flush();
            });
            std::cout << "\n\n";
        }
    }
    
    return 0;
}
