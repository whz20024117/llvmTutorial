#include <string>


enum Token {
    tok_eof=-1,

    tok_def=-2,
    tok_extern=-3,

    tok_identifier=-4, //IdentfierStr
    tok_number=-5   //NumVal
};


static std::string IdentfierStr;
static double NumVal;


