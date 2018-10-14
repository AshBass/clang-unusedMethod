add_llvm_loadable_module(MethodAnalyzer 
MethodAnalyzer.cpp
PLUGIN_TOOL clang
)

if(LLVM_ENABLE_PLUGINS AND (WIN32 OR CYGWIN))
  target_link_libraries(MethodAnalyzer PRIVATE
    clangAST
    clangBasic
    clangFrontend
    clangLex
    LLVMSupport
    )
endif()