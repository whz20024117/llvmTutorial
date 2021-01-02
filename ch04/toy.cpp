#include <string>
#include <cctype>
#include <memory>
#include <vector>
#include <unordered_map>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/LegacyPassManager.h>
#include "../include/KaleidoscopeJIT.h"
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>



enum Token {
    tok_eof=-1,

    tok_def=-2,
    tok_extern=-3,

    tok_identifier=-4, //IdentifierStr
    tok_number=-5   //NumVal
};


static std::string IdentifierStr;
static double NumVal;


static int gettok() // lexer
{
    static int LastChar = ' ';

    while (isspace(LastChar))
        LastChar = getchar();

    if (isalpha(LastChar))  // first char cannot be number if it is keyword/variable
    {
        IdentifierStr = LastChar;
        LastChar = getchar();
        
        while (isalnum(LastChar))
        {
            IdentifierStr.push_back(LastChar);
            LastChar = getchar();
        }

        if (IdentifierStr == "def")
            return tok_def;

        if (IdentifierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.')   // Num values (Double (float64))
    {
        std::string NumStr;
        do 
        {
            NumStr.push_back(LastChar);
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = std::stod(NumStr, 0);
        return tok_number;
    }

    if (LastChar == '#')   // comments
    {
        do
        {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return gettok();    // ignore (no token for) comments, recursively find next token.
    }

    if (LastChar == EOF)
        return tok_eof;
    else
    {
        int ThisChar = LastChar;
        LastChar = getchar();
        return ThisChar;
    }
}


// AST tree nodes classes

class ExprAST
{
public:
    virtual ~ExprAST()
    {}
    virtual llvm::Value* codegen() = 0;
};
using uptrArgsV = typename std::vector<std::unique_ptr<ExprAST>>;
using uptrAST = typename std::unique_ptr<ExprAST>;


class NumberExprAST : public ExprAST
{
    double Val;

public:
    NumberExprAST(double val)
        : Val(val){}

    virtual llvm::Value *codegen();
};


class VariableExprAST : public ExprAST
{
    std::string Name;

public:
    VariableExprAST(const std::string &name)
        :Name(name){}

    virtual llvm::Value *codegen();
};

class BinaryExprAST : public ExprAST
{
    char Op;
    uptrAST LHS, RHS;

public:
    BinaryExprAST(char op, uptrAST lhs, uptrAST rhs)
        :Op(op), LHS(std::move(lhs)), RHS(std::move(rhs))
    {}

    virtual llvm::Value *codegen();
};

class CallExprAST : public ExprAST
{
    std::string Callee;
    uptrArgsV Args;

public:
    CallExprAST(const std::string &callee, uptrArgsV args)
        : Callee(callee), Args(std::move(args)) {}

    virtual llvm::Value *codegen();
};


// Function Prototype
class PrototypeAST
{
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string &name, std::vector<std::string> args)
        : Name(name), Args(args) {}

    const std::string &getName() const 
    {
        return Name;
    }

    virtual llvm::Function *codegen();
};


// Function defination
using uptrProto = typename std::unique_ptr<PrototypeAST>;
class FunctionAST
{
    uptrProto Proto;
    uptrAST Body;

public:
    FunctionAST(uptrProto proto, uptrAST body)
        : Proto(std::move(proto)), Body(std::move(body)) {}

    virtual llvm::Function *codegen();
};

// Parser
static int Curtok;
static int getNextToken() 
{
    Curtok = gettok();
    return Curtok;
}

// Error Handling
uptrAST LogError(const char *str)
{
    fprintf(stderr, "LogError: %s\n", str);
    return nullptr;
}

uptrProto LogErrorP(const char *str)
{
    LogError(str);
    return nullptr;
}

// Forward declarations For parsing functions
static uptrAST ParseNumberExpr();
static uptrAST ParseExpression();
static uptrAST ParsePrimary();
static uptrAST ParseParenExpr();
static uptrAST ParseIdentifierExpr();
static uptrAST ParseBinOpRHS(int, uptrAST);


// Expr Parsing
static uptrAST ParseNumberExpr()
{
    auto Result = std::unique_ptr<NumberExprAST>(new NumberExprAST(NumVal));
    getNextToken();

    return std::move(Result);
}


static uptrAST ParseParenExpr()
{
    getNextToken(); // ( gone

    auto V = ParseExpression();

    if (!V)
        return nullptr;

    if (Curtok != ')')
        return LogError("expected ')'");

    getNextToken(); // )
    return V;
}


static uptrAST ParseIdentifierExpr()
{
    std::string IdName = IdentifierStr;
    
    getNextToken();

    if (Curtok != '(')  // form identifier are Variables
        return std::unique_ptr<VariableExprAST>(new VariableExprAST(IdName));

    getNextToken(); // ( gone


    /* Identifier() is a call */
    uptrArgsV Args;
    if (Curtok != ')')
    {
        while (true)
        {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (Curtok == ')')
                break;

            if (Curtok != ',')
                return LogError("Expected ')' or ','");
            getNextToken();
        }
    }

    getNextToken();

    return std::unique_ptr<CallExprAST>(new CallExprAST(IdName, std::move(Args)));   
}

static uptrAST ParsePrimary()
{
    switch (Curtok)
    {
    case tok_identifier:
        return ParseIdentifierExpr();
        break;
    case tok_number:
        return ParseNumberExpr();
        break;
    case '(':
        return ParseParenExpr();
        break;
    default:
        return LogError("Unknown Token");
    }
}

// Binops
static std::unordered_map<char, int> BinOpPrecedence;

static int GetTokenPrecedence()
{
    if (!isascii(Curtok))
        return -1;

    if (BinOpPrecedence.find(Curtok) == BinOpPrecedence.end())
    {
        return -1;
    }
    else
    {
        return BinOpPrecedence[Curtok];
    }
    
}


static uptrAST ParseExpression()
{
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}


static uptrAST ParseBinOpRHS(int ExprPrec, uptrAST LHS)
{
    while (true)
    {
        int TokPrec = GetTokenPrecedence();

        if (TokPrec < ExprPrec) // ExprPrec start from 0, lease op token prec is 1.
            return LHS;
        
        int BinOp = Curtok; // All non operation token will have prec = -1
        getNextToken();

        auto RHS = ParsePrimary();  // Recursively parse next
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokenPrecedence();
        if (TokPrec < NextPrec)
        {
            /* If next precedent is higher, process next first */
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }
        
        // Merge lhs and rhs
        LHS = std::unique_ptr<BinaryExprAST>(
            new BinaryExprAST(BinOp, std::move(LHS), std::move(RHS))
        );
    }
}

// Prototype

static uptrProto ParsePrototype()
{
    if (Curtok != tok_identifier)
        return LogErrorP("Expected function name in prototype");
    
    std::string fnName = IdentifierStr;
    getNextToken();

    if (Curtok != '(')
        return LogErrorP("Expected ( ");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);

    if (Curtok != ')')
        return LogErrorP("Expected )");

    getNextToken();
    
    return std::unique_ptr<PrototypeAST>(
        new PrototypeAST(fnName, std::move(ArgNames))
    );
}


static std::unique_ptr<FunctionAST> ParseDefinition()
{
    getNextToken(); // eat def
    auto Proto = ParsePrototype();
    if (!Proto)
        return nullptr;

    auto Expr = ParseExpression();
    if (!Expr)
        return nullptr;
        
    return std::unique_ptr<FunctionAST>(
            new FunctionAST(std::move(Proto), std::move(Expr))
        );
}

static uptrProto ParseExtern()
{
    getNextToken(); //eat extern;
    return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr()
{
    if (auto Expr = ParseExpression())
    {
        auto Proto = std::unique_ptr<PrototypeAST>(
            new PrototypeAST("__anon__", std::vector<std::string>())
        );
        return std::unique_ptr<FunctionAST>(
            new FunctionAST(std::move(Proto), std::move(Expr))
        );
    }
    return nullptr;
}

/* LLVM */
static llvm::LLVMContext TheContext;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::unique_ptr<llvm::Module> TheModule;
static std::unordered_map<std::string, llvm::Value*> NamedValues;
static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

llvm::Value *LogErrorV(const char *Str)
{
    LogError(Str);
    return nullptr;
}

llvm::Value* NumberExprAST::codegen()
{
    return llvm::ConstantFP::get(TheContext, llvm::APFloat(Val));
}


llvm::Value* VariableExprAST::codegen()
{
    auto *V = NamedValues[Name];
    if (!V)
        LogErrorV("Unknown variable name");
    return V;
}

llvm::Value* BinaryExprAST::codegen()
{
    auto L = LHS->codegen();
    auto R = RHS->codegen();

    if (!L || !R)
        return nullptr;

    switch (Op)
    {
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '<':
            L = Builder->CreateFCmpULT(L, R, "cmptmp");
            return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(TheContext), "boolcmp");
        
        default:
            return LogErrorV("Invalid operator");
    }
}


llvm::Value* CallExprAST::codegen()
{
    llvm::Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect number of args");

    std::vector<llvm::Value *> ArgsV;
    for (unsigned i=0, e = Args.size(); i != e; ++i)
    {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())  // Check if it is nullptr
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}


llvm::Function *PrototypeAST::codegen()
{
    std::vector<llvm::Type*> Doubles(Args.size(), 
                                    llvm::Type::getDoubleTy(TheContext));

    llvm::FunctionType* FT = 
        llvm::FunctionType::get(llvm::Type::getDoubleTy(TheContext), Doubles, false);
    
    llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, 
                                                Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    
    return F;
}


//TODO: add function individual modules....

llvm::Function* FunctionAST::codegen()
{
    // Find if the function is already defined using extern
    llvm::Function* TheFunction = TheModule->getFunction(Proto->getName());

    if (!TheFunction)
        TheFunction = Proto->codegen();

    if (!TheFunction)
        return nullptr;

    if (!TheFunction->empty())
        return (llvm::Function*) LogErrorV("Function Cannot be redefined");

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(TheContext, "Entry", TheFunction);
    Builder->SetInsertPoint(BB);

    NamedValues.clear();

    for (auto& Arg: TheFunction->args())
        NamedValues[Arg.getName()] = &Arg;

    if (llvm::Value* RetVal = Body->codegen())
    {
        Builder->CreateRet(RetVal);

        llvm::verifyFunction(*TheFunction);

        // Opt passes
        TheFPM->run(*TheFunction);

        return TheFunction;
    }

    TheFunction->eraseFromParent(); //Remove if error in the body
    return nullptr;
}



// Driver
static void InitializeModuleAndPasses() {

    TheModule = std::unique_ptr<llvm::Module>(
        new llvm::Module("my cool jit", TheContext)
    );

    // Configure JIT
    TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());

    // Create a new builder for the module.
    Builder = std::unique_ptr<llvm::IRBuilder<>>(
        new llvm::IRBuilder<>(TheContext)
    );

    // Passes
    TheFPM = std::unique_ptr<llvm::legacy::FunctionPassManager>(
        new llvm::legacy::FunctionPassManager(TheModule.get())
    );

    TheFPM->add(llvm::createInstructionCombiningPass());
    TheFPM->add(llvm::createReassociatePass());
    TheFPM->add(llvm::createGVNPass());
    TheFPM->add(llvm::createCFGSimplificationPass());

    TheFPM->doInitialization();
    
}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition: \n");
            FnIR->print(llvm::errs());
            fprintf(stderr, "\n");
        }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern: \n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
// Evaluate a top-level expression into an anonymous function.
    if (auto FnAST = ParseTopLevelExpr()) 
    {
        if (auto *FnIR = FnAST->codegen()) 
        {
            fprintf(stderr, "Read top-level expression:");
            FnIR->print(llvm::errs());
            fprintf(stderr, "\n");

            // Remove the anonymous expression.
            // FnIR->eraseFromParent();

            /*************** JIT ******************/
            // Create Handle
            auto H = TheJIT->addModule(std::move(TheModule));
            InitializeModuleAndPasses();

            // Search symbol
            auto ExprSymbol = TheJIT->findSymbol("__anon__");
            assert(ExprSymbol && "Function not found");

            double (*FP)() = (double (*)())(intptr_t)llvm::cantFail(ExprSymbol.getAddress());
            
            fprintf(stderr, "Evaluated to %f\n", FP());

            TheJIT->removeModule(H);

        }
    } 
    else 
    {
    // Skip token for error recovery.
    getNextToken();
    }
}

static void MainLoop()
{
    while (true)
    {
        fprintf(stderr, "ready> ");

        switch (Curtok)
        {
        case tok_eof:
            return;
        case ';':
            getNextToken();
            break;
        case tok_def:
            HandleDefinition();
            break;
        case tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}


int main()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    BinOpPrecedence['<'] = 10;
    BinOpPrecedence['+'] = 20;
    BinOpPrecedence['-'] = 20;
    BinOpPrecedence['*'] = 40;

    fprintf(stderr, "ready> ");
    getNextToken();

    TheJIT = std::make_unique<llvm::orc::KaleidoscopeJIT>();

    InitializeModuleAndPasses();

    MainLoop();

    TheModule->print(llvm::errs(), nullptr);

    return 0;
}