/* File : example.i */
%module example

/* Force the generated Pascal code to use the C constant values rather than making a external call */
%pascalconst(2);

/*
%inline %{
//enum Enm {zero, one, two, three};
//enum Enm {zero=0L, one, two, three=(one+two)};
%}
*/

/* A few preprocessor macros */

#define    csconst     1
#define    CSCONST     2
#define    CSConst     3


#define    XCONST      0xFF

#define    Xpr         10u + 3L * ( 5 + 2ULL )


#define    LLConst     4200LL
#define    LCONST      420l
#define    ICONST      42i
#define    FCONST      2.1828f
#define    DCONST      2.1828 + FCONST
#define    DLCONST     21.828ll
#define    ECONST      2.1828e-8l
#define    CCONST      'x'
#define    CCONST2     '\n'
#define    SCONST      "Hello World"
#define    SCONST2     "\"Hello World\""


/* This should work just fine */
#define    EXPR        ICONST + 3*(FCONST)
#define    SEXPR       SCONST " expression"
//#define    SCONC       SCONST ## " concat"
#define    XEXPR        XCONST && 0x0F; 

/* This shouldn't do anything */
#define    EXTERN      extern

/* Neither should this (BAR isn't defined) */
#define    FOO         (ICONST + BAR)


/* The following directives also produce constants */
%constant int iconst37 = 37;
%constant double dpi = 3.14;


