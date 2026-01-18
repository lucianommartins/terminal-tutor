/**
 * CommandParser.hpp - Parse user queries and shell commands
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace tt {

struct ParsedCommand {
    std::string executable;
    std::vector<std::string> args;
    std::vector<std::string> flags;
    std::string raw_input;
    bool is_question;
};

class CommandParser {
public:
    CommandParser();
    ~CommandParser();
    
    ParsedCommand parse(const std::string& input);
    bool isQuestion(const std::string& input);
    std::string extractIntent(const std::string& question);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tt
