/**
 * CommandParser.cpp - Parse user queries and shell commands
 */

#include "tt/CommandParser.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

namespace tt {

struct CommandParser::Impl {
    std::vector<std::string> question_patterns = {
        "como", "what", "how", "why", "quando", "where", "qual", "quais",
        "o que", "por que", "porque", "explain", "explique"
    };
    
    std::vector<std::string> tokenize(const std::string& input) {
        std::vector<std::string> tokens;
        std::istringstream iss(input);
        std::string token;
        
        bool in_quotes = false;
        std::string quoted_token;
        
        while (iss >> token) {
            if (token.front() == '"' || token.front() == '\'') {
                in_quotes = true;
                quoted_token = token.substr(1);
            } else if (in_quotes) {
                if (token.back() == '"' || token.back() == '\'') {
                    quoted_token += " " + token.substr(0, token.size() - 1);
                    tokens.push_back(quoted_token);
                    in_quotes = false;
                } else {
                    quoted_token += " " + token;
                }
            } else {
                tokens.push_back(token);
            }
        }
        
        return tokens;
    }
};

CommandParser::CommandParser() : impl_(std::make_unique<Impl>()) {}

CommandParser::~CommandParser() = default;

ParsedCommand CommandParser::parse(const std::string& input) {
    ParsedCommand result;
    result.raw_input = input;
    result.is_question = isQuestion(input);
    
    if (result.is_question) {
        return result;
    }
    
    auto tokens = impl_->tokenize(input);
    
    if (tokens.empty()) {
        return result;
    }
    
    result.executable = tokens[0];
    
    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.front() == '-') {
            result.flags.push_back(token);
        } else {
            result.args.push_back(token);
        }
    }
    
    return result;
}

bool CommandParser::isQuestion(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Check for question mark
    if (lower.find('?') != std::string::npos) {
        return true;
    }
    
    // Check for question patterns
    for (const auto& pattern : impl_->question_patterns) {
        if (lower.find(pattern) == 0 || lower.find(" " + pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string CommandParser::extractIntent(const std::string& question) {
    // Remove question marks and common prefixes
    std::string intent = question;
    
    // Remove trailing punctuation
    while (!intent.empty() && (intent.back() == '?' || intent.back() == '.')) {
        intent.pop_back();
    }
    
    // Remove common prefixes
    std::vector<std::string> prefixes = {
        "como eu ", "como posso ", "how do i ", "how can i ",
        "o que faz ", "what does ", "me explica ", "explain "
    };
    
    std::string lower = intent;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    for (const auto& prefix : prefixes) {
        if (lower.find(prefix) == 0) {
            intent = intent.substr(prefix.size());
            break;
        }
    }
    
    return intent;
}

} // namespace tt
