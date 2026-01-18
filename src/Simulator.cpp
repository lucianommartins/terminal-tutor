/**
 * Simulator.cpp - "What If" command simulation
 */

#include "tt/Simulator.hpp"
#include "tt/GeminiClient.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

namespace tt {

Simulator::Simulator(GeminiClient& gemini) : gemini_(gemini) {
    // Initialize dangerous patterns
    dangerous_patterns_ = {
        "rm -rf",
        "rm -r /",
        "rm -rf /",
        "rm -rf ~",
        "rm -rf *",
        "> /dev/sda",
        "dd if=",
        "mkfs.",
        ":(){:|:&};:",   // fork bomb
        "chmod -R 777 /",
        "chown -R",
        "sudo rm",
        "mv /* ",
        "wget.*|.*sh",
        "curl.*|.*bash"
    };
}

Simulator::~Simulator() = default;

bool Simulator::isDangerous(const std::string& command) {
    std::string lower = command;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    for (const auto& pattern : dangerous_patterns_) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    // Check for sudo with modifying commands
    if (lower.find("sudo") != std::string::npos) {
        std::vector<std::string> dangerous_with_sudo = {
            "rm", "dd", "mkfs", "chmod", "chown", "mv", "cp"
        };
        for (const auto& cmd : dangerous_with_sudo) {
            if (lower.find(cmd) != std::string::npos) {
                return true;
            }
        }
    }
    
    return false;
}

SimulationResult Simulator::simulate(const std::string& command) {
    SimulationResult result;
    result.is_destructive = isDangerous(command);
    
    // Add immediate warnings for dangerous commands
    if (result.is_destructive) {
        result.warnings.push_back("ATENCAO: Este comando e potencialmente destrutivo!");
    }
    
    // Check for specific dangerous patterns and add targeted warnings
    if (command.find("rm") != std::string::npos) {
        if (command.find("-rf") != std::string::npos || command.find("-r") != std::string::npos) {
            result.warnings.push_back("Este comando remove arquivos/diretorios recursivamente.");
        }
        if (command.find("*") != std::string::npos) {
            result.warnings.push_back("O uso de wildcard (*) pode afetar mais arquivos do que o esperado.");
        }
    }
    
    if (command.find("chmod") != std::string::npos && command.find("777") != std::string::npos) {
        result.warnings.push_back("chmod 777 remove todas as restricoes de seguranca do arquivo.");
    }
    
    // Generate prediction via Gemini
    std::ostringstream prompt;
    prompt << "Voce e um simulador de comandos Linux. Preveja o que aconteceria se o seguinte comando fosse executado.\n\n"
           << "Comando: " << command << "\n\n"
           << "Responda em formato estruturado:\n"
           << "ARQUIVOS_AFETADOS: (liste arquivos/diretorios que seriam modificados, criados ou deletados)\n"
           << "SAIDA_ESPERADA: (o que apareceria no terminal)\n"
           << "RISCOS: (possiveis problemas ou efeitos colaterais)\n"
           << "NIVEL_DESTRUTIVIDADE: (BAIXO, MEDIO, ALTO)\n\n"
           << "Responda em Portugues (Brasil). Seja preciso e tecnico.";
    
    auto response = gemini_.generateContent(prompt.str());
    
    if (response.success) {
        result.predicted_output = response.content;
        
        // Parse the response to extract structured data
        std::istringstream iss(response.content);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("ARQUIVOS_AFETADOS:") != std::string::npos) {
                std::string files = line.substr(line.find(":") + 1);
                // Simple parsing - split by comma
                std::istringstream files_stream(files);
                std::string file;
                while (std::getline(files_stream, file, ',')) {
                    // Trim whitespace
                    file.erase(0, file.find_first_not_of(" \t"));
                    file.erase(file.find_last_not_of(" \t") + 1);
                    if (!file.empty()) {
                        result.files_affected.push_back(file);
                    }
                }
            }
            
            if (line.find("NIVEL_DESTRUTIVIDADE: ALTO") != std::string::npos) {
                result.is_destructive = true;
            }
        }
    } else {
        result.predicted_output = "Erro ao simular comando: " + response.error;
    }
    
    return result;
}

} // namespace tt
