#include <string>
#include <cctype>
#include <memory>
#include <vector>


enum Token {
    tok_eof=-1,

    tok_def=-2,
    tok_extern=-3,

    tok_identifier=-4, //IdentfierStr
    tok_number=-5   //NumVal
};


static std::string IdentfierStr;
static double NumVal;


static int gettok() // lexer
{
    static int LastChar = ' ';

    while (isspace(LastChar))
        LastChar = getchar();

    if (isalpha(LastChar))  // first char cannot be number if it is keyword/variable
    {
        IdentfierStr = LastChar;
        LastChar = getchar();
        
        while (isalnum(LastChar))
        {
            IdentfierStr.push_back(LastChar);
            LastChar = getchar();
        }

        if (IdentfierStr == "def")
            return tok_def;

        if (IdentfierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.')   // Num values (Double (float64))
    {
        std::string NumStr;
        do 
        {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = std::stod(NumStr, 0);
        return tok_number;
    }

    if (LastChar = '#')   // comments
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
    PrototypeAST(const std::string &name, std::vector<std::string> &args)
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
    
}
