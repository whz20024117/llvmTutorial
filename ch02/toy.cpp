#include <string>
#include <cctype>
#include <memory>
#include <vector>
#include <unordered_map>


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
};
using uptrArgsV = typename std::vector<std::unique_ptr<ExprAST>>;
using uptrAST = typename std::unique_ptr<ExprAST>;


class NumberExprAST : public ExprAST
{
    double Val;

public:
    NumberExprAST(double val)
        : Val(val){}
};


class VariableExprAST : public ExprAST
{
    std::string Name;

public:
    VariableExprAST(const std::string &name)
        :Name(name){}
};

class BinaryExprAST : public ExprAST
{
    char Op;
    uptrAST LHS, RHS;

public:
    BinaryExprAST(char op, uptrAST lhs, uptrAST rhs)
        :Op(op), LHS(std::move(lhs)), RHS(std::move(rhs))
    {}
};

class CallExprAST : public ExprAST
{
    std::string Callee;
    uptrArgsV Args;

public:
    CallExprAST(const std::string &callee, uptrArgsV args)
        : Callee(callee), Args(std::move(args)) {}
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
            new PrototypeAST("", std::vector<std::string>())
        );
        return std::unique_ptr<FunctionAST>(
            new FunctionAST(std::move(Proto), std::move(Expr))
        );
    }
    return nullptr;
}

// Driver
static void HandleDefinition()
{
    if (ParseDefinition())
        fprintf(stderr, "Parsed a function definition.\n");
    else
        getNextToken();
}

static void HandleExtern()
{
    if (ParseExtern())
        fprintf(stderr, "Parsed an extern.\n");
    else
        getNextToken();
}

static void HandleTopLevelExpr()
{
    if (ParseTopLevelExpr())
        fprintf(stderr, "Parsed a top level expression.\n");
    else
        getNextToken();
}

static void MainLoop()
{
    while (true)
    {
        fprintf(stderr, "ready> ");

        switch (Curtok)
        {
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
            HandleTopLevelExpr();
            break;
        }
    }
}


int main()
{
    BinOpPrecedence['<'] = 10;
    BinOpPrecedence['+'] = 20;
    BinOpPrecedence['-'] = 20;
    BinOpPrecedence['*'] = 40;

    fprintf(stderr, "ready> ");
    getNextToken();

    MainLoop();
    return 0;
}