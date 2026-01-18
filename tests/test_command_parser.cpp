/**
 * test_command_parser.cpp - Unit tests for CommandParser
 */

#include "tt/CommandParser.hpp"

#include <cassert>
#include <iostream>

void test_parse_simple_command() {
    tt::CommandParser parser;
    
    auto result = parser.parse("ls -la /home");
    
    assert(result.executable == "ls");
    assert(result.flags.size() == 1);
    assert(result.flags[0] == "-la");
    assert(result.args.size() == 1);
    assert(result.args[0] == "/home");
    assert(!result.is_question);
    
    std::cout << "[PASS] test_parse_simple_command\n";
}

void test_parse_complex_command() {
    tt::CommandParser parser;
    
    auto result = parser.parse("find . -type f -name '*.cpp' -exec grep -l TODO {} \\;");
    
    assert(result.executable == "find");
    assert(!result.is_question);
    
    std::cout << "[PASS] test_parse_complex_command\n";
}

void test_detect_question_with_mark() {
    tt::CommandParser parser;
    
    auto result = parser.parse("como eu encontro arquivos grandes?");
    
    assert(result.is_question);
    
    std::cout << "[PASS] test_detect_question_with_mark\n";
}

void test_detect_question_without_mark() {
    tt::CommandParser parser;
    
    auto result = parser.parse("como eu listo arquivos escondidos");
    
    assert(result.is_question);
    
    std::cout << "[PASS] test_detect_question_without_mark\n";
}

void test_detect_english_question() {
    tt::CommandParser parser;
    
    auto result = parser.parse("how do I find large files");
    
    assert(result.is_question);
    
    std::cout << "[PASS] test_detect_english_question\n";
}

void test_extract_intent() {
    tt::CommandParser parser;
    
    std::string intent = parser.extractIntent("como eu encontro arquivos grandes?");
    
    assert(intent.find("?") == std::string::npos);
    
    std::cout << "[PASS] test_extract_intent\n";
}

void test_not_question() {
    tt::CommandParser parser;
    
    auto result = parser.parse("grep -rn pattern .");
    
    assert(!result.is_question);
    
    std::cout << "[PASS] test_not_question\n";
}

int main() {
    std::cout << "Running CommandParser tests...\n\n";
    
    test_parse_simple_command();
    test_parse_complex_command();
    test_detect_question_with_mark();
    test_detect_question_without_mark();
    test_detect_english_question();
    test_extract_intent();
    test_not_question();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
