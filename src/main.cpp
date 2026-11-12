#include "backend.hpp"
#include "semantic/semantic_analyzer.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace rolang;

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::ostringstream data; data << in.rdbuf(); return data.str();
}
static void write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << data; if (!out) throw std::runtime_error("failed writing '" + path + "'");
}
static void usage() {
    std::cerr << "Rolange C++ compiler\n"
              << "usage: rolang <tokens|ast|check|compile|run> source.rol [-o output]\n";
}
int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 2; }
    std::string command=argv[1], source_path=argv[2], output_path;
    for(int i=3;i<argc;++i) {
        if(std::string(argv[i])=="-o" && i+1<argc) output_path=argv[++i];
        else { usage(); return 2; }
    }
    try {
        std::string source=read_file(source_path);
        if(command=="tokens") {
            for(const auto& t:tokenize(source)) std::cout<<t.line<<":"<<t.column<<" "<<token_kind_name(t.kind)<<" "<<t.text<<"\n";
        } else {
            Node tree=parse(tokenize(source));
            if(command=="check") {
                semantics::AnalysisOptions options;
                options.source_name=source_path;
                const auto result=semantics::SemanticAnalyzer{}.analyze(tree,options);
                if(result.has_errors()) {
                    for(const auto& diagnostic:result.diagnostics) {
                        std::cerr<<diagnostic.location.source_name<<":"<<diagnostic.location.line<<":"
                                 <<diagnostic.location.column<<": semantic error["
                                 <<semantics::diagnostic_code_name(diagnostic.code)<<"]: "
                                 <<diagnostic.message<<"\n";
                        for(const auto& related:diagnostic.related) {
                            std::cerr<<related.location.source_name<<":"<<related.location.line<<":"
                                     <<related.location.column<<": note: "<<related.message<<"\n";
                        }
                    }
                    return 1;
                }
                std::cout<<source_path<<": syntax OK\n";
            }
            else if(command=="ast") std::cout<<ast_to_string(tree);
            else if(command=="compile") {
                Code code=compile(tree); std::string dest=output_path.empty()?source_path+".rbc":output_path;
                write_file(dest,disassemble(code)); std::cout<<"compiled "<<source_path<<" -> "<<dest<<"\n";
            } else if(command=="run") { VM vm; vm.run(compile(tree)); }
            else { usage(); return 2; }
        }
        return 0;
    } catch(const std::exception& e) { std::cerr<<"error: "<<e.what()<<"\n"; return 1; }
}
