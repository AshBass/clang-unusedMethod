#include <iostream>
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

using namespace std;
using namespace clang;
using namespace llvm;

namespace MethodAnalyzer
{
    typedef map<string,ObjCMethodDecl*> MethodDeclMap;
    typedef map<string,bool> MethodIsUsedMap;
    
    class InterfaceModel {
    public:
        MethodDeclMap methodDeclMap = MethodDeclMap();
        MethodIsUsedMap methodIsUsedMap = MethodIsUsedMap();
    };
    
    typedef map<string,InterfaceModel> InterfaceMap;
    static InterfaceMap interfaceMap = InterfaceMap();
    
    class MyASTVisitor: public
    RecursiveASTVisitor < MyASTVisitor >
    {
private:
        CompilerInstance & Instance;
        ASTContext *context;
public:
        MyASTVisitor(CompilerInstance &Instance) : Instance(Instance) {
            
        }
        void setContext(ASTContext &context)
        {
            this->context = &context;
        }
        
        bool VisitDecl(Decl *decl)
        {
            if (isUserSourceDecl(decl)) {
                if (isa <ObjCInterfaceDecl> (decl)) {
                    // 找到一个类的定义
                    ObjCInterfaceDecl *interfaceDecl = (ObjCInterfaceDecl *)decl;
                    string interfaceName = interfaceDecl->getNameAsString();
                    if (interfaceMap.find(interfaceName) != interfaceMap.end()) {
                        InterfaceModel interfaceModel = InterfaceModel();
                        interfaceMap[interfaceName] = interfaceModel;
                    }
                }
                if (isa <ObjCMethodDecl> (decl)) {
                    ObjCMethodDecl *methodDecl = (ObjCMethodDecl *)decl;
                    ObjCInterfaceDecl *interfaceDecl = methodDecl->getClassInterface();
                    if (interfaceDecl != NULL &&
                        isUserSourceDecl(interfaceDecl) &&
                        !isProtocolMethod(interfaceDecl,methodDecl) &&
                        !isSystemMethod(methodDecl)) {
                        addMethod(interfaceDecl,methodDecl);
                    }
                }
            }
            return true;
        }
        
        bool VisitStmt(Stmt *s)
        {
            if(isUserSourceStmt(s)) {
                if(isa <ObjCMessageExpr> (s)) {
                    ObjCMessageExpr *objcExpr = (ObjCMessageExpr*)s;
                    ObjCInterfaceDecl *interfaceDecl = objcExpr->getReceiverInterface();
                    if (interfaceDecl != NULL && isUserSourceDecl(interfaceDecl)) {
                        addExpr(interfaceDecl,objcExpr);
                    }
                }
            }
            return true;
        }
        
        void DiagnosticationResult() {
            DiagnosticsEngine &Diagnostics = Instance.getDiagnostics();
            for (InterfaceMap::iterator interfaceIter = interfaceMap.begin(); interfaceIter != interfaceMap.end(); ++interfaceIter) {
                InterfaceModel *model = &interfaceIter->second;
                for (MethodIsUsedMap::iterator methodIter = model->methodIsUsedMap.begin(); methodIter != model->methodIsUsedMap.end(); ++methodIter) {
                    string selector = methodIter->first;
                    bool isUsed = methodIter->second;
                    if (!isUsed) {
                        ObjCMethodDecl *methodDecl = model->methodDeclMap[selector];
                        if (methodDecl != NULL) {
                            int diagID = Diagnostics.getCustomDiagID(DiagnosticsEngine::Warning, "未使用方法 SEL : %0 ");
                            Diagnostics.Report(methodDecl->getLocStart(), diagID) << methodDecl->getSelector().getAsString();
                        }
                    }
                }
            }
        }
        
        bool isProtocolMethod(ObjCInterfaceDecl *interfaceDecl, ObjCMethodDecl *methodDecl) {
            string selectorName = methodDecl->getSelector().getAsString();
            for (auto *protocolDecl : interfaceDecl->all_referenced_protocols()){
                if (protocolDecl->lookupMethod(methodDecl->getSelector(), methodDecl->isInstanceMethod())) {
                    return true;
                }
            }
            return false;
        }
        
        bool isSystemMethod (ObjCMethodDecl *methodDecl) {
            ObjCInterfaceDecl *interfaceDecl = methodDecl->getClassInterface();
            while (isUserSourceDecl(interfaceDecl)) {
                interfaceDecl = interfaceDecl->getSuperClass();
            }
            if (interfaceDecl->lookupMethod(methodDecl->getSelector(), methodDecl->isInstanceMethod())) {
                return true;
            }
            return false;
        }
        
        void addMethod(ObjCInterfaceDecl *interfaceDecl, ObjCMethodDecl *methodDecl) {
            ObjCInterfaceDecl *decl = interfaceDecl;
            while (decl != NULL) {
                if (decl->lookupMethod(methodDecl->getSelector(), methodDecl->isInstanceMethod())) {
                    string interfaceName = decl->getNameAsString();
                    string selector = methodDecl->getSelector().getAsString();
                    InterfaceModel *interfaceModel = &interfaceMap[interfaceName];
                    bool isExist = (interfaceModel->methodIsUsedMap.find(selector) != interfaceModel->methodIsUsedMap.end());
                    if (!isExist) {
                        interfaceModel->methodDeclMap[selector] = methodDecl;
                        interfaceModel->methodIsUsedMap[selector] = false;
                    }else {
                        interfaceModel->methodDeclMap[selector] = methodDecl;
                    }
                }
                decl = decl->getSuperClass();
            }
        }
        
        void addExpr(ObjCInterfaceDecl *interfaceDecl, ObjCMessageExpr *objcExpr) {
            ObjCMethodDecl *methodDecl = objcExpr->getMethodDecl();
            ObjCInterfaceDecl *decl = interfaceDecl;
            while (decl != NULL && isUserSourceDecl(decl)) {
                if (decl->lookupMethod(methodDecl->getSelector(), methodDecl->isInstanceMethod())) {
                    string interfaceName = decl->getNameAsString();
                    string selector = methodDecl->getSelector().getAsString();
                    InterfaceModel *interfaceModel = &interfaceMap[interfaceName];
                    bool isExist = (interfaceModel->methodIsUsedMap.find(selector) != interfaceModel->methodIsUsedMap.end());
                    if (!isExist) {
                        interfaceModel->methodDeclMap[selector] = methodDecl;
                        interfaceModel->methodIsUsedMap[selector] = true;
                    }else {
                        interfaceModel->methodIsUsedMap[selector] = true;
                    }
                }
                decl = decl->getSuperClass();
            }
        }
        
        //获取decl路径名
        string getFileNameDecl(const Decl *decl)
        {
            return Instance.getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
        }
        //获取stmt路径名
        string getFileNameStmt(const Stmt *stmt)
        {
            return Instance.getSourceManager().getFilename(stmt->getSourceRange().getBegin()).str();
        }
        //判断decl来源是不是用户文件
        bool isUserSourceDecl(const Decl *decl)
        {
            string filename = getFileNameDecl(decl);
            return isUserSourceUsefulFileWithFilename(filename);
        }
        //判断stmt来源是不是用户文件
        bool isUserSourceStmt(const Stmt *stmt)
        {
            string filename = getFileNameStmt(stmt);
            return isUserSourceUsefulFileWithFilename(filename);
        }
        //有效文件（非系统文件、且非Pods文件、且是.m文件）
        bool isUserSourceUsefulFileWithFilename(const string filename) {
            bool result = (!isSystemSourceWithFilename(filename) &&
                           !isPodFileWithFilename(filename));
            return result;
        }
        //.h文件
//        bool is_m_fileWithFilename(const string filename)
//        {
//            return (filename.find(".h") != filename.npos);
//        }
        //.m文件
//        bool is_m_fileWithFilename(const string filename)
//        {
//            return (filename.find(".m") != filename.npos);
//        }
        //Pods文件
        bool isPodFileWithFilename(const string filename)
        {
            return (filename.find("/Pods/") != filename.npos);
        }
        //系统文件
        bool isSystemSourceWithFilename(const string filename)
        {
            return (filename.find("/Applications/Xcode.app/") != filename.npos);
        }
    };
    
    class MyASTConsumer: public ASTConsumer
    {
        private:
            MyASTVisitor visitor;
            void HandleTranslationUnit(ASTContext &context)
            {
                visitor.setContext(context);
                visitor.TraverseDecl(context.getTranslationUnitDecl());
                visitor.DiagnosticationResult();
            }
        
        public:
        MyASTConsumer(CompilerInstance &Instance) : visitor(Instance){}
    };
    
    class MyASTAction: public PluginASTAction
    {
public:
        unique_ptr < ASTConsumer > CreateASTConsumer(CompilerInstance & Compiler, StringRef InFile) {
            return unique_ptr < MyASTConsumer > (new MyASTConsumer(Compiler));
        }
        bool ParseArgs(const CompilerInstance &CI, const std::vector < std::string >& args)
        {
            return true;
        }
    };
}

static clang::FrontendPluginRegistry::Add
< MethodAnalyzer::MyASTAction > X("MethodAnalyzer",
                            "MethodAnalyzer desc");
