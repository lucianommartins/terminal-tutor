/**
 * ExplainerEngine.cpp - Command explanation engine with multiple modes
 */

#include "tt/ExplainerEngine.hpp"
#include "tt/GeminiClient.hpp"

#include <sstream>

namespace tt {

ExplainerEngine::ExplainerEngine(GeminiClient& gemini)
    : gemini_(gemini) {}

ExplainerEngine::~ExplainerEngine() = default;

std::string ExplainerEngine::buildExplainPrompt(const std::string& command, ExplainMode mode) {
    std::ostringstream prompt;
    
    switch (mode) {
        case ExplainMode::ELI5:
            prompt << "Voce e um professor muito paciente explicando comandos de terminal para uma crianca de 5 anos. "
                   << "Use analogias simples do dia-a-dia, evite jargao tecnico, e seja amigavel.\n\n"
                   << "Comando: " << command << "\n\n"
                   << "Explique o que esse comando faz como se estivesse explicando para uma crianca. "
                   << "Use exemplos do mundo real (como organizar brinquedos, encontrar coisas em casa, etc).";
            break;
            
        case ExplainMode::DETAILED:
            prompt << "Voce e um instrutor Linux avancado. Forneca uma explicacao tecnica detalhada.\n\n"
                   << "Comando: " << command << "\n\n"
                   << "Inclua:\n"
                   << "1. Sintaxe completa e todas as opcoes disponiveis\n"
                   << "2. Exemplos praticos de uso\n"
                   << "3. Comandos relacionados\n"
                   << "4. Armadilhas comuns e melhores praticas\n"
                   << "5. Como combinar com outros comandos (pipes, redirecionamento)\n\n"
                   << "Responda em Portugues (Brasil).";
            break;
            
        case ExplainMode::NORMAL:
        default:
            prompt << "Voce e um assistente de ensino de CLI. Explique o seguinte comando de forma clara e educativa.\n\n"
                   << "Comando: " << command << "\n\n"
                   << "Forneca:\n"
                   << "1. Um resumo breve do que ele faz\n"
                   << "2. Explicacao de cada flag/opcao usada\n"
                   << "3. Um exemplo pratico de quando usar\n\n"
                   << "Mantenha a explicacao concisa mas informativa. Responda em Portugues (Brasil).";
            break;
    }
    
    return prompt.str();
}

std::string ExplainerEngine::explain(const std::string& command, ExplainMode mode) {
    std::string prompt = buildExplainPrompt(command, mode);
    auto response = gemini_.generateContent(prompt);
    
    if (!response.success) {
        return "Erro ao gerar explicacao: " + response.error;
    }
    
    return response.content;
}

std::string ExplainerEngine::suggestFix(const std::string& failed_command, const std::string& error_msg) {
    std::ostringstream prompt;
    prompt << "Voce e um assistente de CLI ajudando a corrigir um comando que falhou.\n\n"
           << "Comando que falhou: " << failed_command << "\n"
           << "Mensagem de erro: " << error_msg << "\n\n"
           << "Forneca:\n"
           << "1. O que causou o erro\n"
           << "2. O comando corrigido\n"
           << "3. Uma breve explicacao da correcao\n\n"
           << "Responda em Portugues (Brasil). Seja direto e pratico.";
    
    auto response = gemini_.generateContent(prompt.str());
    
    if (!response.success) {
        return "Erro ao gerar sugestao: " + response.error;
    }
    
    return response.content;
}

std::string ExplainerEngine::translateQuestion(const std::string& question) {
    auto response = gemini_.suggestCommand(question);
    
    if (!response.success) {
        return "Erro ao processar pergunta: " + response.error;
    }
    
    return response.content;
}

} // namespace tt
