/* -----------------------------------------------------------------------------
* See the LICENSE file for information on copyright, usage and redistribution
* of SWIG, and the README file for authors - http://www.swig.org/release.html.
*
* freepascal.cxx
*
* freepascal language module for SWIG.
* ----------------------------------------------------------------------------- */

char cvsroot_freepascal_cxx[] = "$Header: /cvsroot/swig/SWIG/Source/Modules/freepascal.cxx Exp $";

/* Some documentation: */

/****************************************************************************************************************

This module is based on the Modula3 module contained in the standard SWIG distribuition

***************************************************************************************************************

The purpouse of this module is to generate import units for DLL


Parameters:


***************************************************************************************************************

The generated unit's code can be customized in the .i file using the following sections of code that are injected
in the .pas file

interface_uses
interface_type_begin
interface_type_end
interface_functions
interface_functions_wrapper
implementation_uses
implementation_type_begin
implementation_type_end
implementation_functions
implementation_functions_wrapper
implementation_type_end

The generated unit has this structure:

unit modulename;

interface

uses
%interface_uses %{
%}

%interface_type_begin %{
%}

....

%interface_type_end %{
%}

%interface_functions %{
%}

%interface_functions_wrapper %{
%}


implementation

initialization

uses

%implementation_uses %{
%}

%implementation_type_begin %{
%}

....

%implementation_type_end %{
%}


%implementation_functions %{
%}

%implementation_functions_wrapper %{
%}

%initialization %{
%}

finalization

%finalization %{
%}

end;


*/



/*

- Char() can be used to transform a String to (char *)
which can be used in GDB or for output with printf

- What is the while (checkAttribute()) loop in functionWrapper good for?
Appearently for skipping (numinputs=0) typemaps.

- enumeration items should definitely be equipped with its plain numerical value
One could add tag 'numvalue' in CParse/parser.y,
but it is still possible that someone declares an
enumeration using a symbolic constant.
I have quickly hacked
that the successive number is assigned
if "enumvalue" has suffix "+1".
The ultimate solution would be to generate a C program
which includes the header and outputs all constants.
This program might be compiled and run
by 'make' or by SWIG and the resulting output is fed back to SWIG.

- What's the difference between "sym:name" and "name" ?
"name" is the original name and "sym:name" is probably modified by the user using %rename

Swig's odds:
- arguments of type (Node *) for SWIG functions
should be most often better (const Node *):
Swig_symbol_qualified, Getattr, nodeType, parentNode
- unique identifier style instead of
NewString, Getattr, firstChild
- 'class'.name is qualified,
'enum'.name and 'enumitem'.name is not
- Swig_symbol_qualified() returns NIL for enumeration nodes

- Is there a function that creates a C representation of a SWIG type string?

ToDo:
- create WeakRefs only for resources returned by function marked with %newobject
-> part of output conversion
- clean typemap conception
- should a multi-typemap for paswrapouttype skip the corresponding input parameters?
when yes - How to handle inout-arguments? In this case like in-argument.
- C++ exceptions
- call-back functions
- emit enumeration definitions when its first item is declared,
currently enumerations are emitted at the beginning of the file

- cleanup zoo of unused or similar functions  
 
*/




#include "swig.h"
#include "swigmod.h"

#include <limits.h>    // for INT_MAX
#include <ctype.h>
#include <vector>
#include <string>

#include <execinfo.h>

#include "parser.h"

using namespace std;

int loglevel = 0; 

#define LOG(lvl,msg) {fprintf(stderr, "%s:'%s'  func:%s in file: %s(%d)\n", lvl, msg, __FUNCTION__, __FILE__, __LINE__ );}
#define LOG_INFO(msg) {if (loglevel>=1) LOG("INFO",msg);}
#define LOG_ERROR(msg) {if (loglevel>=2) LOG("ERROR",msg);}
#define LOG_WARNING(msg) {if (loglevel>=3) LOG("WARNING",msg);}
#define LOG_DEBUG(msg) {if (loglevel>=4) LOG("DEBUG",msg);}
#define LOG_TRACE(msg) {if (loglevel>=5) LOG("TRACE",msg);}

int ___call_depth = 0;
#define TRACE_FUNC_ENTR { \
  if (loglevel>=5){ \
    for(int i=0; i<___call_depth; i++) fprintf(stderr,"  "); \
    fprintf(stderr, "entr %s; file:%s(%d) -cd:%d\n", __FUNCTION__, __FILE__, __LINE__ ,___call_depth ); \
    ___call_depth++; \
  } \
}

#define TRACE_FUNC_EXIT { \
  if (loglevel>=5){ \
    ___call_depth--; \
    for(int i=0; i<___call_depth; i++) fprintf(stderr,"  "); \
    fprintf(stderr, "exit %s; file:%s(%d) -cd:%d\n", __FUNCTION__, __FILE__, __LINE__, ___call_depth ); \
  } \
}

// GDB fails when watched vars Deleted
#define IN_GDB 1
#define DELETE if ( ! IN_GDB) Delete

bool logdebug_print_nodes = false;
#define LOG_NODE_DEBUG(n) {if ((loglevel>=4) && logdebug_print_nodes) Swig_print_the_node(n);}
#define LOG_TREE_DEBUG(n) {if ((loglevel>=4) && logdebug_print_nodes) Swig_print_node(n);}

const char usageArgDir[] = "paswrapargdir typemap expect values: in, out, inout\n";

/* Hash type used for upcalls from C/C++ */
typedef DOH UpcallData;

class FREEPASCAL:public Language {
public:
  enum block_type { no_block, constant, variable, blocktype, revelation };

private:
  struct PASFile {
    String *f;
    Hash *import;
    block_type bt;
    /* VC++ 6 doesn't allow the access to 'no_block'
    if it is a private member of FREEPASCAL class */
    PASFile():f(NewString("")), import(NewHash()), bt(no_block) {
    };
    ~PASFile() {
      Delete(f);
      Delete(import);
    };

    /* -----------------------------------------------------------------------------
    * enterBlock()
    *
    * Make sure that a given declaration is written to the right declaration block,
    * that is constants are written after "CONST" and so on ...
    * ----------------------------------------------------------------------------- */
    void enterBlock(block_type newbt) {
      static const char *ident[] = { "", "\nconst\n", "\nvar\n", "\ntype\n", "\nREVEAL\n" };
#ifdef DEBUG
      if ((bt < 0) || (4 < bt)) {
        printf("bt %d out of range\n", bt);
      };
#endif
      if (newbt != bt) {
        Append(f, ident[newbt]);
        bt = newbt;
      };
    };

  };

  static const char *usage;
  const String *empty_string;

  String * callconv;


  Hash *swig_types_hash;
  File *f_runtime;
  File *f_runtime_h;
  File *f_header;
  File *f_wrappers;
  File *f_init;
  File *f_directors;
  File *f_directors_h;


  String* m_namespace;
  bool opaque_record;
  bool proxy_flag;    // Flag for generating proxy classes
  bool have_default_constructor_flag;

  bool nowrap_function_flag;  // Flag for when wrapping a native function

  bool native_function_flag;  // Flag for when wrapping a native function
  bool enum_constant_flag;  // Flag for when wrapping an enum or constant
  bool static_flag;    // Flag for when wrapping a static functions or member variables
  bool wrapping_member_flag;  // Flag for when wrapping a member variable/enum/const
  bool global_variable_flag;  // Flag for when wrapping a global variable    bool unsafe_module;
  bool wrapping_constructor_flag; // Flag for when wrapping a class constructor
  bool unsafe_module;
  bool variable_wrapper_flag;  // Flag for when wrapping a nonstatic member variable

  bool enum_wrap_flag;
  String *outfile;
        String *outfile_ext;

  bool old_variable_names;  // Flag for old style variable names in the intermediary class
  bool global_runtime_const_flag; // Flag for when wrapping a class constructor

  String * intf_uses;
  int enum_begin;
  String *pasraw_module_name;    // raw interface name
  String *pasraw_ext;
  PASFile pasraw_intf;    // raw interface
  PASFile pasraw_impl;    // raw implementation (usually empty)
  String *paswrap_name;    // wrapper module
  PASFile paswrap_intf;
  PASFile paswrap_impl;
  String *pasmakefile;
  String *targetlibrary;
  //String *CObjPtrTypeName;  // name of type for pointer to C++ class object
  String *proxy_class_def;
  String *proxy_class_constructor_def;
  String *proxy_class_code;
  String *proxy_class_name;
  String *raw_class_name;
  String *variable_name;  //Name of a variable being wrapped
  String *variable_type;  //Type of this variable
  String *enumeration_name;  //Name of the current enumeration type
  Hash *enumeration_items;  //and its members


  Hash *nested_classes_hash;

  Hash *types_hash;  //and its members
  File *types_decl;    // raw implementation (usually empty)

  String *interface_uses;
  String *interface_type_begin;
  String *interface_type_end;  
  String *interface_functions;
  String *interface_functions_wrapper;
  String *implementation_uses;
  String *implementation_type_begin;
  String *implementation_type_end;
  String *implementation_functions;
  String *implementation_functions_wrapper;
  String *constant_initialization;
  String *initialization;
  String *finalization;
  Hash *reserved_keyword;
  String *wrapdllname;
  String *dllname;
  String *wrapsourcename;

  int enumeration_max;
  Hash *enumeration_coll;  //Collection of all enumerations.
  /* The items are nodes with members:
  "items"  - hash of with key 'itemname' and content 'itemvalue'
  "max"    - maximum value in item list
  */
  Hash *template_coll;  //Collection of all templates.

  String *constant_values;
  String *constantfilename;
  String *renamefilename;
  String *typemapfilename;
  String *pasraw_imports;  //intermediary class imports from %pragma
  String *module_imports;  //module imports from %pragma
  String *pasraw_baseclass;  //inheritance for intermediary class class from %pragma
  String *module_baseclass;  //inheritance for module class from %pragma
  String *pasraw_interfaces;  //interfaces for intermediary class class from %pragma
  String *module_interfaces;  //interfaces for module class from %pragma
  String *pasraw_class_modifiers;  //class modifiers for intermediary class overriden by %pragma
  String *paswrap_modifiers;  //class modifiers for module class overriden by %pragma
  String *upcasts_code;    //C++ casts for inheritance hierarchies C++ code
  String *pasraw_cppcasts_code;  //C++ casts up inheritance hierarchies intermediary class code
  String *destructor_call;  //C++ destructor call if any

  String *director_callback_typedefs;  // Director function pointer typedefs for callbacks
  String *director_callbacks;  // Director callback function pointer member variables
  String *director_delegate_callback;  // Director callback method that delegates are set to call
  String *director_delegate_definitions;  // Director delegates definitions in proxy class
  String *director_delegate_instances;  // Director delegates member variables in proxy class
  String *director_method_types;  // Director method types
  String *director_connect_parms;  // Director delegates parameter list for director connect call


  // Director method stuff:
  List *dmethods_seq;
  Hash *dmethods_table;
  int n_dmethods;
  int n_directors;
  int first_class_dmethod;
  int curr_class_dmethod;

  enum type_additions { none, pointer, reference };

  DOH *GlobalConstantList;


public:

  /* -----------------------------------------------------------------------------
  * FREEPASCAL()
  * ----------------------------------------------------------------------------- */

  FREEPASCAL():

    pasraw_intf(),
      pasraw_impl(),
      paswrap_intf(),
      paswrap_impl()

    {

      empty_string = NewString("");

      callconv = 0;
      swig_types_hash = NULL;
      f_runtime = NULL;
      f_runtime_h = NULL;
      f_header = NULL;
      f_wrappers = NULL;
      f_init = NULL;
      f_directors = NULL;
      f_directors_h = NULL;
      opaque_record = true;
      proxy_flag = true;
      have_default_constructor_flag = false;
      nowrap_function_flag = false;
      native_function_flag = false;
      enum_constant_flag = false;
      static_flag = false;
      wrapping_member_flag = false;
      global_variable_flag = false;     
      wrapping_constructor_flag = false;
      unsafe_module = false;
      variable_wrapper_flag = false;
      enum_wrap_flag = false;
      outfile = NULL;
      outfile_ext = NULL;
      old_variable_names = false;
      global_runtime_const_flag = false;
      pasraw_module_name = NULL;
      pasraw_ext = NULL;
      paswrap_name = NULL;    
      pasmakefile = NULL;
      targetlibrary= NULL;
      proxy_class_def= NULL;
      proxy_class_constructor_def= NULL;
      proxy_class_code= NULL;
      proxy_class_name= NULL;
      raw_class_name= NULL;
      variable_name= NULL;
      variable_type= NULL;
      enumeration_name= NULL;
      enumeration_items= NULL;
      enumeration_max= 0;
      enumeration_coll= NULL;
      constant_values= NULL;
      constantfilename= NULL;
      renamefilename= NULL;
      typemapfilename= NULL;
      pasraw_imports= NULL;
      module_imports = NULL;
      pasraw_baseclass= NULL;
      module_baseclass= NULL;
      pasraw_interfaces= NULL;
      module_interfaces= NULL;
      pasraw_class_modifiers= NULL;
      paswrap_modifiers= NULL;
      upcasts_code= NULL;
      pasraw_cppcasts_code= NULL;
      types_hash= NULL;
      types_decl= NULL;
      intf_uses= NULL;
      dllname= NULL;
      nested_classes_hash = NULL;
      wrapsourcename= NULL;
      director_callback_typedefs= NULL;
      director_callbacks= NULL;
      director_delegate_callback= NULL;
      director_delegate_definitions= NULL;
      director_delegate_instances= NULL;
      director_method_types= NULL;
      director_connect_parms= NULL;
      dmethods_seq= NULL;
      dmethods_table= NULL;
      n_dmethods= 0;
      n_directors= 0;
      destructor_call = NULL;
      reserved_keyword = NULL;
      wrapdllname = NULL;
      // SM 
      director_language = 0;  
      //      String *name = NewString("self");
      reserved_keyword = NewHash();
      m_namespace = NewString("");

      /* Reserved words from http://wiki.freepascal.org/Reserved_words */
      Setattr(reserved_keyword, "AND", "1"); /*BOOLEAN OPERATOR REQUIRING BOTH CONDITIONS ARE TRUE FOR THE RESULT TO BE TRUE*/
      Setattr(reserved_keyword, "ARRAY", "1"); /*MULTIPLE ELEMENTS WITH THE SAME NAME*/
      Setattr(reserved_keyword, "ASM", "1"); /*START OF CODE WRITTEN IN ASSEMBLY LANGUAGE*/
      Setattr(reserved_keyword, "BEGIN", "1");	 /*START OF A BLOCK OF CODE*/
      Setattr(reserved_keyword, "BREAK", "1");	 /*EXIT A CASE STATEMENR*/
      Setattr(reserved_keyword, "CASE", "1");	 /*SELECT A PARTICULAR SEGEMENT OF CODE TO EXECUTE BASED ON A VALUE*/
      Setattr(reserved_keyword, "CONST", "1");	 /*DECLARE AN IDENTIFIER WITH A FIXED VALUE, OR A VARIABLE WITH AN INITIALIZED VALUE*/
      Setattr(reserved_keyword, "CONSTRUCTOR", "1");	 /*ROUTINE USED TO CREATE AN OBJECT*/
      Setattr(reserved_keyword, "CONTINUE", "1");	 /*SKIPS AN ITERATION IN A FOR-LOOP AND RESTART EXECUTION AT THE BEGINNING OF THE LOOP*/
      Setattr(reserved_keyword, "DESTRUCTOR", "1");	 /*ROUTINE USED TO DEALLOCATE AN OBJECT*/
      Setattr(reserved_keyword, "DIV", "1");	 /*INTEGER DIVIDE OPERATOR*/
      Setattr(reserved_keyword, "DO", "1");	 /*USED TO INDICATE START OF A LOOP*/
      Setattr(reserved_keyword, "DOWNTO", "1");	 /*USED IN A FOR LOOP TO INDICATE THE INDEX VARIABLE IS DECREMENTED*/
      Setattr(reserved_keyword, "ELSE", "1");	 /*USED IN IF STATEMENT TO PROVIDE AN EXECUTION PATH WHEN THE IF TEST FAILS*/
      Setattr(reserved_keyword, "END", "1");	 /*END OF A BLOCK OF CODE, A RECORD OR CERTAIN OTHER CONSTRUCTS]*/
      Setattr(reserved_keyword, "FALSE", "1");	 /*BOOLEAN VALUE INDICATING A TEST FAILED; OPPOSITE OF TRUE*/
      Setattr(reserved_keyword, "FILE", "1");	 /*EXTERNAL DATA STRUCTURE, TYPICALLY STORED ON DISC*/
      Setattr(reserved_keyword, "FOR", "1");	 /*LOOP USED TO INCREMENT OR DECREMENT A CONTROL VARIABLE*/
      Setattr(reserved_keyword, "FUNCTION", "1");	 /*DEFINE START OF A ROUTINE THAT RETURNS A RESULT VALUE*/
      Setattr(reserved_keyword, "GOTO", "1");	 /*USED TO EXIT A SEGMENT OF CODE AND JUMP TO ANOTHER POINT*/
      Setattr(reserved_keyword, "IF", "1");	 /*TEST A CONDITION AND PERFORM A SET OF INSTRUCTIONS BASED ON THE RESULT*/
      Setattr(reserved_keyword, "IMPLEMENTATION", "1");	 /*DEFINE THE INTERNAL ROUTINES IN UNIT*/
      Setattr(reserved_keyword, "IN", "1");	 /*IDENTIFIES ELEMENTS IN A COLLECTION*/
      Setattr(reserved_keyword, "INLINE", "1");	 /*MACHINE CODE INSERTED DIRECTLY INTO A ROUTINE*/
      Setattr(reserved_keyword, "INTERFACE", "1");	 /*PUBLIC DECLARATIONS OF ROUTINES IN A UNIT*/
      Setattr(reserved_keyword, "LABEL", "1");	 /*DEFINES THE TARGET JUMP POINT FOR A GOTO*/
      Setattr(reserved_keyword, "MOD", "1");	 /*OPERATOR USED TO RETURN THE REMAINDER OF AN INTEGER DIVISION*/
      Setattr(reserved_keyword, "NIL", "1");	 /*POINTER VALUE INDICATING THE POINTER DOES NOT CONTAIN A VALUE*/
      Setattr(reserved_keyword, "NOT", "1");	 /*BOOLEAN OPERATOR THAT NEGATES THE RESULT OF A TEST*/
      Setattr(reserved_keyword, "OBJECT", "1");	 /*DEFINES AN OBJECT CONSTRUCT*/
      Setattr(reserved_keyword, "OF", "1");	 /*DEFINES THE CHARACTERISTICS OF A VARIABLE*/
      Setattr(reserved_keyword, "ON", "1");	 /**/
      Setattr(reserved_keyword, "OPERATOR", "1");	 /*DEFINES A ROUTINE USED TO IMPLEMENT AN OPERATOR*/
      Setattr(reserved_keyword, "OR", "1");	 /*BOOLEAN OPERATOR WHICH ALLOWS EITHER OF TWO CHOICES TO BE USED*/
      Setattr(reserved_keyword, "PACKED", "1");	 /*INDICATES THE ELEMENTS OF AN ARRAY ARE TO USE LESS SPACE (THIS KEYWORD IS PRIMARILY FOR COMPATIBILITY WITH OLDER PROGRAMS AS PACKING OF ARRAY ELEMENTS IS GENERALLY AUTOMATIC;*/
      Setattr(reserved_keyword, "PROCEDURE", "1");	 /*DEFINE START OF A ROUTINE THAT DOES NOT RETURN A RESULT VALUE*/
      Setattr(reserved_keyword, "PROGRAM", "1");	 /*DEFINES START OF AN APPLICATION. THIS KEYWORD IS USUALLY OPTIONAL.*/
      Setattr(reserved_keyword, "RECORD", "1");	 /*GROUP A SERIES OF VARIABLES UNDER A SINGLE NAME*/
      Setattr(reserved_keyword, "REPEAT", "1");	 /*LOOP THROUGH A SECTION OF CODE THROUGH AN UNTIL STATEMENT AS LONG AS THE RESULT OF THE TEST IS TRUE*/
      Setattr(reserved_keyword, "SET", "1");	 /*GROUP A COLLECTION*/
      Setattr(reserved_keyword, "SHL", "1");	 /*OPERATOR TO SHIFT A VALUE TO THE LEFT; EQUIVALENT TO MULTIPLYING BY A POWER OF 2*/
      Setattr(reserved_keyword, "SHR", "1");	 /*OPERATOR TO SHIFT A VALUE TO THE RIGHT; EQUIVALENT TO DIVIDING BY A POWER OF 2*/
      Setattr(reserved_keyword, "STRING", "1");	 /*DECLARES A VARIABLE THAT CONTAINS MULTIPLE CHARACTERS*/
      Setattr(reserved_keyword, "THEN", "1");	 /*INDICATES START OF CODE IN AN IF TEST*/
      Setattr(reserved_keyword, "TO", "1");	 /*INDICATES A FOR VARIABLE IS TO BE INCREMENTED*/
      Setattr(reserved_keyword, "TRUE", "1");	 /*BOOLEAN VALUE INDICATING A TEST SUCCEEDED; OPPOSITE OF FALSE*/
      Setattr(reserved_keyword, "TYPE", "1");	 /*DECLARES KINDS OF RECORDS OR NEW CLASSES OF VARIABLES*/
      Setattr(reserved_keyword, "UNIT", "1");	 /*SEPARATELY COMPILED MODULE*/
      Setattr(reserved_keyword, "UNTIL", "1");	 /*INDICATES END TEST OF A REPEAT STATEMENT*/
      Setattr(reserved_keyword, "USES", "1");	 /*NAMES UNITS THIS PROGRAM OR UNIT REFERS TO*/
      Setattr(reserved_keyword, "VAR", "1");	 /*DECLARE VARIABLES*/
      Setattr(reserved_keyword, "WHILE", "1");	 /*TEST A VALUE AND IF TRUE, LOOP THROUGH A SECTION OF CODE*/
      Setattr(reserved_keyword, "WITH", "1");	 /*REFERENCE THE INTERNAL VARIABLES WITHIN A RECORD WITHOUT HAVING TO REFER TO THE RECORD ITSELF*/
      Setattr(reserved_keyword, "XOR", "1");	 /*BOOLEAN OPERATOR USED TO INVERT AN OR TEST */
  
  /* Reserved words in Object Pascal */
  
      Setattr(reserved_keyword, "AS", "1");
      Setattr(reserved_keyword, "CLASS", "1");
      Setattr(reserved_keyword, "DISPOSE", "1");
      Setattr(reserved_keyword, "EXCEPT", "1");
      Setattr(reserved_keyword, "EXIT", "1");
      Setattr(reserved_keyword, "EXPORTS", "1");
      Setattr(reserved_keyword, "FINALIZATION", "1");
      Setattr(reserved_keyword, "FINALLY", "1");
      Setattr(reserved_keyword, "INHERITED", "1");
      Setattr(reserved_keyword, "INITIALIZATION", "1");
      Setattr(reserved_keyword, "IS", "1");
      Setattr(reserved_keyword, "LIBRARY", "1");
      Setattr(reserved_keyword, "NEW", "1");
      Setattr(reserved_keyword, "OUT", "1");
      Setattr(reserved_keyword, "PROPERTY", "1");
      Setattr(reserved_keyword, "RAISE", "1");
      Setattr(reserved_keyword, "SELF", "1");
      Setattr(reserved_keyword, "THREADVAR", "1");
      Setattr(reserved_keyword, "TRY", "1");
  
      /* Modifiers (directives) */
      // do we have to rename them?
      Setattr(reserved_keyword, "ABSOLUTE", "1");
      Setattr(reserved_keyword, "ABSTRACT", "1");
      Setattr(reserved_keyword, "ALIAS", "1");
      Setattr(reserved_keyword, "ASSEMBLER", "1");
      Setattr(reserved_keyword, "CDECL", "1");
      Setattr(reserved_keyword, "CPPDECL", "1");
      Setattr(reserved_keyword, "DEFAULT", "1");
      Setattr(reserved_keyword, "EXPORT", "1");
      Setattr(reserved_keyword, "EXTERNAL", "1");
      Setattr(reserved_keyword, "FORWARD", "1");
      Setattr(reserved_keyword, "INDEX", "1");
      Setattr(reserved_keyword, "LOCAL", "1");
      Setattr(reserved_keyword, "NAME", "1");
      Setattr(reserved_keyword, "NOSTACKFRAME", "1");
      Setattr(reserved_keyword, "OLDFPCCALL", "1");
      Setattr(reserved_keyword, "OVERRIDE", "1");
      Setattr(reserved_keyword, "PASCAL", "1");
      Setattr(reserved_keyword, "PRIVATE", "1");
      Setattr(reserved_keyword, "PROTECTED", "1");
      Setattr(reserved_keyword, "PUBLIC", "1");
      Setattr(reserved_keyword, "PUBLISHED", "1");
      Setattr(reserved_keyword, "READ", "1");
      Setattr(reserved_keyword, "REGISTER", "1");
      Setattr(reserved_keyword, "REINTRODUCE", "1");
      Setattr(reserved_keyword, "SAFECALL", "1");
      Setattr(reserved_keyword, "SOFTFLOAT", "1");
      Setattr(reserved_keyword, "STDCALL", "1");
      Setattr(reserved_keyword, "VIRTUAL", "1");
      Setattr(reserved_keyword, "WRITE", "1");
      
      // not sure if we should rename following  
      Setattr(reserved_keyword, "CREATE", "1");
      Setattr(reserved_keyword, "DESTROY", "1");

    }

    virtual ~FREEPASCAL()
    {
      Delete(reserved_keyword);
    }

    /************** some utility functions ***************/

    int SplitString(const string& input, 
      const string& delimiter, vector<string>& results, 
      bool includeEmpties)
    {
      int iPos = 0;
      int newPos = -1;
      int sizeS2 = (int)delimiter.size();
      int isize = (int)input.size();

      if( 
        ( isize == 0 )
        ||
        ( sizeS2 == 0 )
        )
      {
        return 0;
      }

      vector<int> positions;

      newPos = (int)input.find (delimiter, 0);

      if( newPos < 0 )
      { 
        results.push_back(input);
        return 0; 
      }

      int numFound = 0;

      while( newPos >= iPos )
      {
        numFound++;
        positions.push_back(newPos);
        iPos = newPos;
        newPos = (int)input.find (delimiter, iPos+sizeS2);
      }

      if( numFound == 0 )
      {
        return 0;
      }

      for( int i=0; i <= (int)positions.size(); ++i )
      {
        string s("");
        if( i == 0 ) 
        { 
          s = input.substr( i, positions[i] ); 
        }
        else {
          int offset = positions[i-1] + sizeS2;
          if( offset < isize )
          {
            if( i == (int)positions.size() )
            {
              s = input.substr(offset);
            }
            else if( i > 0 )
            {
              s = input.substr( positions[i-1] + sizeS2, 
                positions[i] - positions[i-1] - sizeS2 );
            }
          }
        }
        if( includeEmpties || ( s.size() > 0 ) )
        {
          results.push_back(s);
        }
      }
      return numFound;
    }

    void RemoveNamespace(String * &tm)
    {

      if (tm == 0)
        return;

      //      char *ns ;
      //      char *ctm ;
      //      char *next;

      char tmp[1024];
      strcpy(tmp, Char(tm));

      vector<string> v;

      SplitString(tmp,"::",v,true);

      Delete(tm);

      if (v.size())

        tm = NewString(v.back().c_str());
      else
        tm = NewString("");

      // Replace(tm,m_namespace,"", DOH_REPLACE_ANY );

      /*
      ctm = strtok (tmp,"::");

      while (ctm != 0) 
      {

      next = strtok (0, "::");

      if (!next) {

      tm = NewString(ctm);
      }

      ctm = next;

      }
      */
      return;
#if 0

      if (ns != 0) {

        char ss[256];
        /*  if (ctm[0]=='P')
        ctm++;*/
        strncpy(ss, ctm, (int)ns - (int)ctm+2);
        ss[(int)ns - (int)ctm+2]= 0;

        Replace(tm,ss,"", DOH_REPLACE_ANY );

      }
#endif
    }

    /* -----------------------------------------------------------------------------
    * getMappedType()
    *
    * Return the type of 'p' mapped by 'map'.
    * Print a standard warning if 'p' can't be mapped.
    * ----------------------------------------------------------------------------- */

    String *getMappedType(Node *p, const char *map, bool warn = true) {
      String *mapattr = NewString("tmap:");
      Append(mapattr, map);

      String *tm = Getattr(p, mapattr);
      if ((tm == NIL)&&warn) {
        Swig_warning(WARN_FREEPASCAL_TYPEMAP_TYPE_UNDEF, input_file, line_number,
          "No '%s' typemap defined for type '%s'\n", map, SwigType_str(Getattr(p, "type"), 0));
      }
      Delete(mapattr);

      //RemoveNamespace(tm);

      return tm;
    }

    /* -----------------------------------------------------------------------------
    * getMappedTypeNew()
    *
    * Similar to getMappedType but uses Swig_type_lookup_new.
    * ----------------------------------------------------------------------------- */

    String *getMappedTypeNew(Node *n, const char *map, const char *lname = "", bool warn = true) {

      String * tm  = Swig_typemap_lookup(map, n, lname, 0);
      //  String *tm = Swig_typemap_lookup_new(map, n, lname, 0);
      if ((tm == NIL) && warn) {
        Swig_warning(WARN_FREEPASCAL_TYPEMAP_TYPE_UNDEF, input_file, line_number,
          "No '%s' typemap defined for type '%s'\n", map, SwigType_str(Getattr(n, "type"), 0));
      }
      //RemoveNamespace(tm);
      return tm;
    }

    /* -----------------------------------------------------------------------------
    * attachMappedType()
    *
    * Obtain the type mapped by 'map' and attach it to the node
    * ----------------------------------------------------------------------------- */

    void attachMappedType(Node *n, const char *map, const char *lname = "") {
      //String *tm = Swig_typemap_lookup_new(map, n, lname, 0);
      String *tm = Swig_typemap_lookup(map, n, lname, 0);
      if (tm != NIL) {
        String *attr = NewStringf("tmap:%s", map);
        Setattr(n, attr, tm);
        Delete(attr);
      }
    }

    /* -----------------------------------------------------------------------------
    * skipIgnored()
    *
    * Skip all parameters that have 'numinputs=0'
    * with respect to a given typemap.
    * ----------------------------------------------------------------------------- */

    Node *skipIgnored(Node *p, const char *map) {
      String *niattr = NewStringf("tmap:%s:numinputs", map);
      String *nextattr = NewStringf("tmap:%s:next", map);

      while ((p != NIL) && checkAttribute(p, niattr, "0")) {
        p = Getattr(p, nextattr);
      }

      Delete(nextattr);
      Delete(niattr);
      return p;
    }

    /* -----------------------------------------------------------------------------
    * isInParam()
    * isOutParam()
    *
    * Check if the parameter is intended for input or for output.
    * ----------------------------------------------------------------------------- */

    bool isInParam(Node *p) {
      String *dir = Getattr(p, "tmap:paswrapargdir");
      //printf("dir for %s: %s\n", Char(Getattr(p,"name")), Char(dir));
      if ((dir == NIL) || (Strcmp(dir, "in") == 0)
        || (Strcmp(dir, "inout") == 0)) {
          return true;
      } else if (Strcmp(dir, "out") == 0) {
        return false;
      } else {
        printf(usageArgDir);
        return false;
      }
    }

    bool isOutParam(Node *p) {
      String *dir = Getattr(p, "tmap:paswrapargdir");
      if ((dir == NIL) || (Strcmp(dir, "in") == 0)) {
        return false;
      } else if ((Strcmp(dir, "out") == 0) || (Strcmp(dir, "inout") == 0)) {
        return true;
      } else {
        printf(usageArgDir);
        return false;
      }
    }

    /* -----------------------------------------------------------------------------
    * printAttrs()
    *
    * For debugging: Show all attributes of a node and their values.
    * ----------------------------------------------------------------------------- */
    void printAttrs(Node *n) {
      Iterator it;
      for (it = First(n); it.key != NIL; it = Next(it)) {
        printf("%s = %s\n", Char(it.key), Char(Getattr(n, it.key)));
      }
    }

    /* -----------------------------------------------------------------------------
    * hasPrefix()
    *
    * Check if a string have a given prefix.
    * ----------------------------------------------------------------------------- */
    bool hasPrefix(const String *str, const String *prefix) {
      int len_prefix = Len(prefix);
      return (Len(str) > len_prefix)
        && (Strncmp(str, prefix, len_prefix) == 0);
    }

    /* -----------------------------------------------------------------------------
    * getQualifiedName()
    *
    * Return fully qualified identifier of n.
    * ----------------------------------------------------------------------------- */
#if 0
    // Swig_symbol_qualified returns NIL for enumeration nodes
    String *getQualifiedName(Node *n) {
      String *qual = Swig_symbol_qualified(n);
      String *name = Getattr(n, "name");
      if (hasContent(qual)) {
        return NewStringf("%s::%s", qual, name);
      } else {
        return name;
      }
    }
#else
    String *getQualifiedName(Node *n) {
      String *name = Copy(Getattr(n, "sym:name"));
      n = parentNode(n);
      while (n != NIL) {
        const String *type = nodeType(n);
        if ( Equal(type, "class") || Equal(type, "struct") || Equal(type, "namespace") ) {
          String *newname = NewStringf("%s.%s", Getattr(n, "sym:name"), name);
          Delete(name);
          return newname;
        }
        n = parentNode(n);
      }
      //printf("qualified name: %s\n", Char(name));
      return name;
    }
#endif

    /* -----------------------------------------------------------------------------
    * makePascalName(Node *n)
    * ----------------------------------------------------------------------------- */
    String *getQualifiedPascalName(Node *n) {
      LOG_NODE_DEBUG(n);
      String *R = 0;
      String *symname = getQualifiedName(n);

      String *suf = 0;
      if (Getattr(n,"overloaded")) 
        suf = Getattr(n,"sym:override");
      else
      if (GetInt(n,"pascal:cinumber")>0) 
        suf = Getattr(n,"pascal:cisuffix");
      
      if (suf)
        R = NewStringf("%s%s", symname, suf);
      else
        R = Copy(symname);
        
      return R;
    }

    String *getPascalName(Node *n) {
      LOG_NODE_DEBUG(n);
      String *R = 0;
      String *symname = Getattr(n,"sym:name");

      String *suf = 0;
      if (Getattr(n,"overloaded")) 
        suf = Getattr(n,"sym:override");
      else
      if (GetInt(n,"pascal:cinumber")>0) 
        suf = Getattr(n,"pascal:cisuffix");
      
      if (suf)
        R = NewStringf("%s%s", symname, suf);
      else
        R = Copy(symname);
        
      return R;
    }

    /* -----------------------------------------------------------------------------
    * hasContent()
    *
    * Check if the string exists and contains something.
    * ----------------------------------------------------------------------------- */
    bool hasContent(const String *str) {
      if (!str) 
        return false;
      
      if (str != NIL)
        if (Char(str) != 0)
	  return (Strcmp(str, "") != 0);
	
      return false;
    }

    /* -----------------------------------------------------------------------------
    * openWriteFile()
    *
    * Caution: The file must be freshly allocated and will be destroyed
    *          by this routine.
    * ----------------------------------------------------------------------------- */

    File *openWriteFile(String *name) {
      File *file = NewFile(name, "w", SWIG_output_files());
      if (!file) {
        FileErrorDisplay(name);
        SWIG_exit(EXIT_FAILURE);
      }
      Delete(name);
      return file;
    }

    /* -----------------------------------------------------------------------------
    * aToL()
    *
    * like atol but with additional user warning
    * ----------------------------------------------------------------------------- */

    long aToL(const String *value) {
      char *endptr;
      long numvalue = strtol(Char(value), &endptr, 0);
      if (*endptr != 0) {
        Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "The string <%s> does not denote a numeric value.\n", value);
      }
      return numvalue;
    }

    /* -----------------------------------------------------------------------------
    * strToL()
    *
    * like strtol but returns if the conversion was successful
    * ----------------------------------------------------------------------------- */

    bool strToL(const String *value, long &numvalue) {
      char *endptr;
      numvalue = strtol(Char(value), &endptr, 0);
      return (*endptr == 0);
    }

    /* -----------------------------------------------------------------------------
    * evalExpr()
    *
    * Evaluate simple expression as they may occur in "enumvalue" attributes.
    * ----------------------------------------------------------------------------- */

    bool evalExpr(String *value, long &numvalue) {
      // Split changes file status of String and thus cannot receive 'const' strings
      //printf("evaluate <%s>\n", Char(value));
      List *summands = Split(value, '+', INT_MAX);
      Iterator sm = First(summands);
      numvalue = 0;
      for (; sm.item != NIL; sm = Next(sm)) {
        String *smvalue = Getattr(constant_values, sm.item);
        long smnumvalue;
        if (smvalue != NIL) {
          if (!strToL(smvalue, smnumvalue)) {
            //printf("evaluation: abort 0 <%s>\n", Char(smvalue));
            return false;
          }
        } else {
          if (!strToL(sm.item, smnumvalue)) {
            //printf("evaluation: abort 1 <%s>\n", Char(sm));
            return false;
          }
        }
        numvalue += smnumvalue;
      }
      //printf("evaluation: return %ld\n", numvalue);
      return true;
    }

    /* -----------------------------------------------------------------------------
    * log2()
    *
    * Determine the position of the single bit of a power of two.
    * Returns true if the given number is a power of two.
    * ----------------------------------------------------------------------------- */

    bool log2(long n, long &exp) {
      exp = 0;
      while (n > 0) {
        if ((n & 1) != 0) {
          return n == 1;
        }
        exp++;
        n >>= 1;
      }
      return false;
    }

    /* -----------------------------------------------------------------------------
    * writeArg
    *
    * Write a function argument or RECORD entry definition.
    * Bundles arguments of same type and default value.
    * 'name.next==NIL' denotes the end of the entry or argument list.
    * ----------------------------------------------------------------------------- */

    bool equalNilStr(const String *str0, const String *str1) {
      if (str0 == NIL) {
        return (str1 == NIL);
        //return (str0==NIL) == (str1==NIL);
      } else {
        return (str1 != NIL) && (Cmp(str0, str1) == 0);
        //return Cmp(str0,str1)==0;
      }
    }

    struct writeArgState {
      String *mode, *name, *type, *value;
      bool hold;
      writeArgState():mode(NIL), name(NIL), type(NIL), value(NIL), hold(false) {
      };
    };

    void writeArg(const Node* n, File *f, writeArgState & state, String *mode, String *name, String *type, String *value) {
      TRACE_FUNC_ENTR;
      /* skip the first argument,
      only store the information for the next call in this case */

      //String * newname = nameToPascal(n, name, false) ;
      String * newname = Copy(name);

      if (state.name != NIL) {
        if ((!state.hold) && (state.mode != NIL)) {
          Printf(f, "%s ", state.mode);
        }
        if ((newname != NIL) && equalNilStr(state.mode, mode) && equalNilStr(state.type, type) && (state.value == NIL) && (value == NIL)
          /* the same expression may have different values
          due to side effects of the called function */
          /*equalNilStr(state.value,value) */
          ) {
        Printf(f, " %s, ", state.name);
        state.hold = true; // SM
        } else {
          Append(f, state.name);
          if (state.type != NIL) {
            Printf(f, ":%s", state.type);
          }
          if (state.value != NIL) {
            Printf(f, ":=%s", state.value);
          }
          Append(f, "; ");
          state.hold = false;
        }
      }
      /* at the next call the current argument will be the previous one */
      state.mode = mode;
      state.name = newname;
      state.type = type;
      state.value = value;
      TRACE_FUNC_EXIT;
    }

    /* -----------------------------------------------------------------------------
    * getProxyName()
    *
    * Test to see if a type corresponds to something wrapped with a proxy class
    * Return NULL if not otherwise the proxy class name
    * ----------------------------------------------------------------------------- */

    String *getProxyName(SwigType *t) {
      if (proxy_flag) {
        Node *n = classLookup(t);
        if (n) {
          return Getattr(n, "wrap:name");
        }
      }
      return NULL;
    }

    /*************** language processing ********************/

    /* ------------------------------------------------------------
    * main()
    * ------------------------------------------------------------ */

    virtual void main(int argc, char *argv[]) {
 
      SWIG_library_directory("freepascal");

      callconv = NewString("stdcall");

      wrapdllname = NewString("");
      dllname = NewString("");
      wrapsourcename = NewString("");
      outfile_ext = NewString("pas");
      pasraw_ext = NewString("pas");

      // Look for certain command line options
      for (int i = 1; i < argc; i++) {
        if (argv[i]) {    

          if (strcmp(argv[i], "-wrapdllname") == 0) {
            if (argv[i + 1]) {
              wrapdllname = NewString(argv[i + 1]);
              Swig_mark_arg(i);
              Swig_mark_arg(i + 1);
              i++;
            } else {
              Swig_arg_error();
            }
          }    
          else 
            if (strcmp(argv[i], "-dllname") == 0) {
              if (argv[i + 1]) {
                dllname = NewString(argv[i + 1]);
                Swig_mark_arg(i);
                Swig_mark_arg(i + 1);
                i++;
              } else {
                Swig_arg_error();
              }
            }    
            else 
              if (strcmp(argv[i], "-wrapsourcename") == 0) {
                if (argv[i + 1]) {
                  wrapsourcename = NewString(argv[i + 1]);
                  Swig_mark_arg(i);
                  Swig_mark_arg(i + 1);
                  i++;
                } else {
                  Swig_arg_error();
                }
              }    
              else 
                if (strcmp(argv[i], "-callconv") == 0) {
                  if (argv[i + 1]) {
                    callconv = NewString(argv[i + 1]);
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                }    
                else if (strcmp(argv[i], "-generateconst") == 0) {
                  if (argv[i + 1]) {
                    constantfilename = NewString(argv[i + 1]);
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                } else if (strcmp(argv[i], "-generaterename") == 0) {
                  if (argv[i + 1]) {
                    renamefilename = NewString(argv[i + 1]);
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                } else if (strcmp(argv[i], "-generatetypemap") == 0) {
                  if (argv[i + 1]) {
                    typemapfilename = NewString(argv[i + 1]);
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                } else if (strcmp(argv[i], "-noproxy") == 0) {
                  Swig_mark_arg(i);
                  proxy_flag = false;
                } else if (strcmp(argv[i], "-runtime_const") == 0) {
                  Swig_mark_arg(i);
                  global_runtime_const_flag = true;
                } else if (strcmp(argv[i], "-opaquerecord") == 0) {
                  Swig_mark_arg(i);
                  opaque_record = true;
                } else if (strcmp(argv[i], "-oldvarnames") == 0) {
                  Swig_mark_arg(i);
                  old_variable_names = true;
                } else if (strcmp(argv[i], "-help") == 0) {
                  Printf(stdout, "%s\n", usage);
                } else if (strcmp(argv[i], "-pasext") == 0) {
                  if (argv[i + 1]) {
                    pasraw_ext = NewString(argv[i + 1]);
                    outfile_ext = pasraw_ext;
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                } else if (strcmp(argv[i], "-fp-loglevel") == 0) {
                  if (argv[i + 1]) {
                    loglevel = atoi(argv[i + 1]);
		    if (loglevel>5) 
		      yydebug=1; // also set `# define YYDEBUG 1' in parser.h and parser.c
                    Swig_mark_arg(i);
                    Swig_mark_arg(i + 1);
                    i++;
                  } else {
                    Swig_arg_error();
                  }
                } else if (strcmp(argv[i], "-fp-debug-print-nodes") == 0) {
                  Swig_mark_arg(i);
                  logdebug_print_nodes = true;
                }
		
		
        }
      }
      TRACE_FUNC_ENTR; // not really an entrance, but loglevel is set only here

      // Add a symbol to the parser for conditional compilation
      Preprocessor_define("SWIGFREEPASCAL 1", 0);

      // Add typemap definitions
      SWIG_typemap_lang("freepascal");
      SWIG_config_file("freepascal.swg");

      allow_overloading();

      TRACE_FUNC_EXIT;
   }

    /* ---------------------------------------------------------------------
    * top()
    * --------------------------------------------------------------------- */

    virtual int top(Node *n) {
      TRACE_FUNC_ENTR;
      if (hasContent(constantfilename) || hasContent(renamefilename) || hasContent(typemapfilename)) {
        int result = SWIG_OK;
        if (hasContent(constantfilename)) {
          result = generateConstantTop(n) && result;
        }
        if (hasContent(renamefilename)) {
          //result = generateRenameTop(n) && result;
        }
        if (hasContent(typemapfilename)) {
          result = generateTypemapTop(n) && result;
        }
        return result;
      } else {
        return generatePASTop(n);
      }
      TRACE_FUNC_EXIT;
    }
 
  void print_hash(Hash *h) {
      Iterator it = First(h);
      while (it.key) {
	LOG_NODE_DEBUG(it.item);
	String *type = nodeType(it.item);
	String *name = Getattr(it.item,"name");
	Printf(stdout, " %s %s %s (%s) %s\n", it, it.key, it.item, (type)?type:"", (name)?name:"");
	it = Next(it);
      }
  }

  void print_list(List *h) {
      Iterator it = First(h);
      while (it.item) {
	Printf(stdout, " %s (%s) %s %s %s\n", it, nodeType(it.item), Getattr(it.item,"name"), Getattr(it.item,"sym:name"),
	   Getattr(it.item,"pascal:ciname") );
	it = Next(it);
      }
  }

  int search_list(List *l, DOH *item) {
      Iterator it = First(l);
      int i = 0;
      while (it.item) {
	if ( Equal(it.item, item) )
	  return i;
	i++;  
	it = Next(it);
      }
      return -1;
  }

  void print_symtab(Node *n, const char* tab) {
    Hash *symtab = Getattr(n, tab);
    if (symtab)
      print_hash(symtab);
    else 
      Printf(stdout, "empty\n") ;
  }

    /**
     * scanAllSymbolDupsCaseInsensive(Node *top)
     * 
     * to solve case insensitivity issue
     * it scans all symbols and assigns "sym:cisuffix" when   
     * "sym:name" CI-equals with "sym:name" of previous symbols 
     * of same "sym:symtab".cisymtab .
     * If "sym:name" CI-equals with "sym:name" of parents "sym:symtab".cisymtab 
     * and "sym:name" not CS-equals with parent's "sym:name"
     * then "pascal:ciname" (uppercased "sym:name")
     * and "pascal:cisuffix" (_SWIG_%N)are also assigned to keep names distinguished.
     * 
     * cisymtab List is added to "sym:symtab" to maintain original order, 
     * because Hash symtab is not ordered. 
     * 
     * Adding "pascal:ciname" and cisymtab are done in first tree-walk - scanAllAddCIName
     * Assing "pascal:cisuffix" done in second tree-walk.
     * 
    **/ 

    void scanAllAddCIName(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n); 
      Hash *symsymtab = Getattr(n, "sym:symtab");
      Hash *symtab = NIL;
      if (symsymtab) {
        symtab = Getattr(symsymtab,"symtab");
      }
      if (symtab) {
	List *cisymtab = Getattr(symsymtab,"cisymtab");
	if ( cisymtab == NIL ) {
	  cisymtab = NewList();
	  Setattr(symsymtab, "cisymtab", cisymtab);
	}

	print_hash(symsymtab);

	String *nodesymname = Getattr(n, "sym:name");
	if (Getattr(symtab, nodesymname)) {
	  String *nodeCIsymname = Swig_string_upper(nodesymname);
	  Setattr(n, "pascal:ciname", nodeCIsymname);
	  LOG_NODE_DEBUG(n); 
	  if ( search_list( cisymtab, n ) == -1 )
	    Append(cisymtab, n); // add node only once
	  print_list(cisymtab);
	}
      }      
        
      Node *child = firstChild(n);
      while (child != NIL) {
	scanAllAddCIName(child);
        child = nextSibling(child);
      }	  
      TRACE_FUNC_EXIT;
    }

    /**
     * count same CI-names going up-list from position b 
    **/
    int count_up_cinames(List *l, String *ciname, int b) {
      int cnt = 0;
      for(int i = b; i>=0; i--) {
	Node *node = Getitem(l, i);
	String *cisymname = Getattr(node, "pascal:ciname");
	if ( Equal(cisymname, ciname) )
	  cnt++;
      }
      return cnt;
    }


    void scanAllAddCISuffix(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n); 
      Hash *symsymtab = Getattr(n, "sym:symtab");
      if ( symsymtab ) {
	LOG_NODE_DEBUG(symsymtab); 
	//print_hash(symsymtab);
	List *cisymtab = Getattr(symsymtab,"cisymtab");

	int myId = search_list( cisymtab, n );
	String *cisymname = Getattr(n, "pascal:ciname");
	
	int cnt = count_up_cinames(cisymtab, cisymname, myId-1);
	if (cnt > 0) {
	  Setattr( n, "pascal:cisuffix", NewStringf("__SWIG_%d", cnt) );
	}
	SetInt( n, "pascal:cinumber", cnt); // may be it will be needed
	  
	LOG_NODE_DEBUG(n); 
	cnt = 0;
      }
      
      Node *child = firstChild(n);
      while (child != NIL) {
	scanAllAddCISuffix(child);
        child = nextSibling(child);
      }	  
      TRACE_FUNC_EXIT;
    }

/*
    void scanAllSymbolDupsCaseInsensive(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n); 
      
      scanAllAddCIName(n);
      scanAllAddCISuffix(n);
      TRACE_FUNC_EXIT;
    }
          
    void scanAllSymbolDupsCaseInsensive(Node *n) {
      Hash *symsymtab = Getattr(n, "sym:symtab");
      if (symsymtab) {
	LOG_NODE_DEBUG(symsymtab); 
        print_symtab(symsymtab, "symtab");
      }
      List *cisymtab = Getattr(symsymtab,"cisymtab");
      
      Node *child = firstChild(n);
      while (child != NIL) {
        String *constname = NIL;
        String *constvalue = NIL;
	
	LOG_NODE_DEBUG(child);
	
        String *type = nodeType(child);
	
        if ( (Equal(type, "cdecl")) || (Equal(type, "procedure")) || (Equal(type, "function"))
	  || (Equal(type, "class")) || (Equal(type, "typedef")) || (Equal(type, "template"))
	  || (Equal(type, "enum"))
	) {
	    int n1 = scanbackCaseInsensitiveName(child); // scan on same level
	    if (n1 > 0) {
	      String *symcisuffix = NewStringf("_SWIG_%d",n1);
	      Setattr(child, "sym:cisuffix", symcisuffix);
	    }
        }
	else if ( (Equal(type, "enumitem")) || (Equal(type, "constant")) ) {

	    int n1 = scanbackCaseInsensitiveName(child); // scan on same level
	    int n2 = scanbackGlobalConstantList(child); // scan all constants
	    if (n1 > 0 || n2 > 0) {
	      String *symcisuffix = NewStringf("_SWIG_%d",n1+n2);
	      Setattr(child, "sym:cisuffix", symcisuffix);
	    }
	    Append(GlobalConstantList,child);

            constname = getQualifiedName(child);
	    constvalue = Getattr(child, "value");
	    
            if ((!hasContent(constname))
              || (('0' <= *Char(constname)) && (*Char(constname) <= '9'))) {
                constname = Getattr(child, "name");
            }

        }
	else if ( (Equal(type, "namespace")) ){
	  // do nothing
	}
	else if ( (Equal(type, "include")) || (Equal(type, "access"))
	  || (Equal(type, "typemap")) || (Equal(type, "typemapitem"))
	  || (Equal(type, "insert")) || (Equal(type, "module")) 
	  || (Equal(type, "pragma")) || (Equal(type, "apply"))
	){
	  // do nothing
	}
	else {
	  fprintf(stderr, "UNKNOWN TYPE %s\n", Char(type));
	}
        
	if (constname != NIL) {
          //Printf(file, "  printf(\"%%%%constnumeric(%%Lg) %s;\\n\", (long double)%s);\n", constname, constname);
        }
        scanAllSymbolDupsCaseInsensive(child);
        child = nextSibling(child);
      }
      TRACE_FUNC_EXIT;
    }
*/

    void scanConstant(File *file, Node *n) {
      TRACE_FUNC_ENTR;
      Node *child = firstChild(n);
      while (child != NIL) {
        String *constname = NIL;
        String *type = nodeType(child);
        if ((Equal(type, "enumitem")) || (Equal(type, "constant"))) {

            constname = getQualifiedName(child);
	    constname = Getattr(child, "value");
	    
            if ((!hasContent(constname))
              || (('0' <= *Char(constname)) && (*Char(constname) <= '9'))) {
                constname = Getattr(child, "name");
            }

        }
        if (constname != NIL) {
          Printf(file, "  printf(\"%%%%constnumeric(%%Lg) %s;\\n\", (long double)%s);\n", constname, constname);
        }
        scanConstant(file, child);
        child = nextSibling(child);
      }
      TRACE_FUNC_EXIT;
    }

    int generateConstantTop(Node *n) {
      TRACE_FUNC_ENTR;
      File *file = openWriteFile(NewStringf("%s.c", constantfilename));
      if (CPlusPlus) {
        Printf(file, "#include <cstdio>\n");
      } else {
        Printf(file, "#include <stdio.h>\n");
      }
      Printf(file, "#include \"%s\"\n", input_file);
      Printf(file, "\n");
      Printf(file, "int main (int argc, char *argv[]) {\n");
      Printf(file, "\
             /*This progam must work for floating point numbers and integers.\n\
             Thus all numbers are converted to double precision floating point format.*/\n");
      scanConstant(file, n);
      Printf(file, "  return 0;\n");
      Printf(file, "}\n");
      Delete(file);
      return SWIG_OK;
      TRACE_FUNC_EXIT;
    }

/*
    void scanRename(File *file, Node *n) {
      Node *child = firstChild(n);
      while (child != NIL) {
        String *type = nodeType(child);
        if (Strcmp(type, "cdecl") == 0) {
          ParmList *p = Getattr(child, "parms");
          if (p != NIL) {
            String *name = getQualifiedName(child);
            String *pasname = nameToPascal(child, name, true);
            //don't know how to get the original C type identifiers 
            //String *arguments = createCSignature (child);
            Printf(file, "%%rename(\"%s\") %s;\n", pasname, name);
            //Printf(file, "%%rename(\"%s\") %s %s(%s);\n",
            //pasname, Getattr(n,"type"), name, arguments); 
            Delete(name);
            Delete(pasname);
            //Delete (arguments);
          }
        }
        scanRename(file, child);
        child = nextSibling(child);
      }
    }
*/

#if 0
    int generateRenameTop(Node *n) {
      File *file = openWriteFile(NewStringf("%s.i", renamefilename));
      Printf(file, "\
              This file was generated from %s\n\
             by SWIG with option -generaterename. */\n\
             \n", input_file);
      scanRename(file, n);
      Delete(file);
      return SWIG_OK;
    }
#endif

    void scanTypemap(File *file, Node *n) {
      Node *child = firstChild(n);
      while (child != NIL) {
        String *type = nodeType(child);
        //printf("nodetype %s\n", Char(type));
        String *storage = Getattr(child, "storage");
        if ((Strcmp(type, "class") == 0) || ((Strcmp(type, "cdecl") == 0) && (storage != NIL)
          && (Strcmp(storage, "typedef") == 0))) {
            String *name = getQualifiedName(child);
            String *pasname = getQualifiedPascalName(child);
            Printf(file, "%%typemap(\"paswrapintype\") %s %%{%s%%}\n", name, pasname);
            Printf(file, "%%typemap(\"pasrawintype\") %s %%{%s%%}\n", name, pasname);
            Printf(file, "\n");
        }
        scanTypemap(file, child);
        child = nextSibling(child);
      }
    }

    int generateTypemapTop(Node *n) {
      File *file = openWriteFile(NewStringf("%s.i", typemapfilename));
      Printf(file, "\
             /* This file was generated from %s\n\
             by SWIG with option -generatetypemap. */\n\
             \n", input_file);
      scanTypemap(file, n);
      Delete(file);
      return SWIG_OK;
    }

    void emitTypes(File *f_swigtype, String *classname, SwigType *type, Node *n) {
     TRACE_FUNC_ENTR;
      String *swigtype = NewString("");

      // Emit banner name
      emitBanner(f_swigtype);

      // Pure Pascal baseclass and interfaces
      const String *pure_baseclass = typemapLookup(n,"pasbase", type, WARN_NONE);
      const String *pure_interfaces = typemapLookup(n,"pasinterfaces", type, WARN_NONE);

      // Emit the class
      Printv(swigtype, typemapLookup(n,"pasimports", type, WARN_NONE),  // Import statements
        "\n", typemapLookup(n,"pasclassmodifiers", type, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF),  // Class modifiers
        " class $pasclassname",  // Class name and bases
        *Char(pure_baseclass) ? " : " : "", pure_baseclass, *Char(pure_interfaces) ?  // Interfaces
        " : " : "", pure_interfaces, " {\n", "  private IntPtr swigCPtr;\n", "\n", "  ", typemapLookup(n,"pasptrconstructormodifiers", type, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF),  // pointer constructor modifiers
        " $pasclassname(IntPtr cPtr, bool bFutureUse) {\n",  // Constructor used for wrapping pointers
        "    swigCPtr = cPtr;\n", "  }\n", "\n", "  protected $pasclassname() {\n",  // Default constructor
        "    swigCPtr = IntPtr.Zero;\n", "  }\n", typemapLookup(n,"pasgetcptr", type, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF),  // getCPtr method
        typemapLookup(n,"pascode", type, WARN_NONE),  // extra Pascal code
        "}\n", "\n", NIL);

      Replaceall(swigtype, "$pasclassname", classname);
      Printv(f_swigtype, swigtype, NIL);


      Delete(swigtype);
      TRACE_FUNC_ENTR;
    }



    int generatePASTop(Node *n) {
      TRACE_FUNC_ENTR;

      /* Initialize all of the output files */
      outfile = Getattr(n, "outfile");
      String *outfile_h = Getattr(n, "outfile_h");

      f_runtime = NewFile(outfile, "w", SWIG_output_files());
      if (!f_runtime) {
        FileErrorDisplay(outfile);
        SWIG_exit(EXIT_FAILURE);
      }

      if (directorsEnabled()) {
        if (!outfile_h) {
          Printf(stderr, "Unable to determine outfile_h\n");
          SWIG_exit(EXIT_FAILURE);
        }
        f_runtime_h = NewFile(outfile_h, "w", SWIG_output_files());
        if (!f_runtime_h) {
          FileErrorDisplay(outfile_h);
          SWIG_exit(EXIT_FAILURE);
        }
      }

      f_init = NewString("");
      f_header = NewString("");
      f_wrappers = NewString("");
      f_directors_h = NewString("");
      f_directors = NewString("");

      pasmakefile = NewString("");

      /* Register file targets with the SWIG file handler */
      Swig_register_filebyname("header", f_header);
      Swig_register_filebyname("wrapper", f_wrappers);
      Swig_register_filebyname("runtime", f_runtime);
      Swig_register_filebyname("init", f_init);

      Swig_register_filebyname("director", f_directors);
      Swig_register_filebyname("director_h", f_directors_h);


      Swig_register_filebyname("pasrawintf", pasraw_intf.f);
      Swig_register_filebyname("pasrawimpl", pasraw_impl.f);
      Swig_register_filebyname("paswrapintf", paswrap_intf.f);
      Swig_register_filebyname("paswrapimpl", paswrap_impl.f);



      interface_uses = NewString("\n// interface_uses\n");
      interface_type_begin= NewString("\n// interface_type_begin \n");
      interface_type_end= NewString("\n// interface_type_end \n");
      interface_functions= NewString("\n// interface_functions\n");
      interface_functions_wrapper= NewString("\n// interface_functions_wrapper\n");
      implementation_uses= NewString("\n// implementation_uses\n");
      implementation_type_begin= NewString("\n// implementation_type_begin\n");
      implementation_type_end= NewString("\n// implementation_type_end\n");
      implementation_functions= NewString("\n// implementation_functions\n");
      implementation_functions_wrapper= NewString("\n// implementation_functions_wrapper\n");
      implementation_type_end= NewString("\n// implementation_type_end\n");

      initialization= NewString("\n// initialization\n\n");
      finalization= NewString("\n// finalization\n\n");

      constant_initialization = NewString("\n// constant_initialization\n");



      //Swig_register_filebyname("ugo",  interface_uses); 
      Swig_register_filebyname("interface_uses",  interface_uses); 
      Swig_register_filebyname("interface_type_begin",  interface_type_begin);
      Swig_register_filebyname("interface_type_end",  interface_type_end);
      Swig_register_filebyname("interface_functions",  interface_functions);
      Swig_register_filebyname("interface_functions_wrapper",  interface_functions_wrapper);
      Swig_register_filebyname("implementation_uses",  implementation_uses);
      Swig_register_filebyname("implementation_type_begin",  implementation_type_begin);
      Swig_register_filebyname("implementation_type_end",  implementation_type_end);
      Swig_register_filebyname("implementation_functions",  implementation_functions);
      Swig_register_filebyname("implementation_functions_wrapper",  implementation_functions_wrapper);
      Swig_register_filebyname("initialization",  initialization);
      Swig_register_filebyname("finalization",  finalization);



      Swig_register_filebyname("pasmakefile", pasmakefile);

      swig_types_hash = NewHash();

      String *name = Getattr(n, "name");
      // Make the intermediary class and module class names. The intermediary class name can be set in the module directive.
      Node *optionsnode = Getattr(Getattr(n, "module"), "options");
      if (optionsnode != NIL) {
        String *pasraw_name_tmp = Getattr(optionsnode, "pasrawname");
        if (pasraw_name_tmp != NIL) {
          pasraw_module_name = Copy(pasraw_name_tmp);
        }


        /* check if directors are enabled for this module.  note: this 
        * is a "master" switch, without which no director code will be
        * emitted.  %feature("director") statements are also required
        * to enable directors for individual classes or methods.
        *
        * use %module(directors="1") modulename at the start of the 
        * interface file to enable director generation.
        */
        if (Getattr(optionsnode, "directors")) {
          allow_directors();
        }
      }
      if (pasraw_module_name == NIL) {
        pasraw_module_name = NewStringf("%s", name);
      }
      Setattr(paswrap_impl.import, pasraw_module_name, "");

      paswrap_name = Copy(name);

      proxy_class_constructor_def = NewString("\n// proxy_class_constructor_def\n");
      proxy_class_def = NewString("\n// proxy_class_def\n");
      proxy_class_code = NewString("\n// proxy_class_code\n");
      pasraw_baseclass = NewString("\n// pasraw_baseclass\n");
      pasraw_interfaces = NewString("\n// pasraw_interfaces\n");
      pasraw_class_modifiers = NewString("\n// pasraw_class_modifiers\n");  // package access only to the intermediary class by default
      pasraw_imports = NewString("\n// pasraw_imports\n");
      pasraw_cppcasts_code = NewString("\n// pasraw_cppcasts_code\n");
      paswrap_modifiers = NewString("\n// paswrap_modifiers\n public");
      module_baseclass = NewString("\n// module_baseclass\n");
      module_interfaces = NewString("\n// module_interfaces\n");
      module_imports = NewString("\n// module_imports\n");
      upcasts_code = NewString("\n// upcasts_code\n");

      Swig_banner(f_runtime);  // Print the SWIG banner message

      if (directorsEnabled()) {
        Printf(f_runtime, "#define SWIG_DIRECTORS\n");

        char module_class_name[] = "CLASS";

        /* Emit initial director header and director code: */
        Swig_banner(f_directors_h);
        Printf(f_directors_h, "#ifndef SWIG_%s_WRAP_H_\n", module_class_name);
        Printf(f_directors_h, "#define SWIG_%s_WRAP_H_\n\n", module_class_name);

        Printf(f_directors, "\n\n");
        Printf(f_directors, "/* ---------------------------------------------------\n");
        Printf(f_directors, " * C++ director class methods\n");
        Printf(f_directors, " * --------------------------------------------------- */\n\n");
        if (outfile_h)
          Printf(f_directors, "#include \"%s\"\n\n", Swig_file_filename(outfile_h));
      }


      String *wrapper_name = NewString("");

      Printf(wrapper_name, "%f", pasraw_module_name);
      //  Swig_name_register((char *) "wrapper", Char(wrapper_name));
      Swig_name_register((char *) "wrapper", (char*)"%f");

      if (old_variable_names) {
        Swig_name_register((char *) "set", (char *) "set_%v");
        Swig_name_register((char *) "get", (char *) "get_%v");
      }

      Delete(wrapper_name);

      Printf(f_wrappers, "\n#ifdef __cplusplus\n");
      Printf(f_wrappers, "extern \"C\" {\n");
      Printf(f_wrappers, "#endif\n\n");

      constant_values = NewHash();
      //scanForConstPragmas(n);
      
      enumeration_coll = NewHash();
      collectEnumerations(enumeration_coll, n);
      
      template_coll = NewHash();
      collectTemplates(template_coll,n);

      types_hash = NewHash();
      nested_classes_hash = NewHash();
      //  types_decl = openWriteFile(NewStringf("%s%s.inc", Swig_file_dirname(outfile), pasraw_module_name));
      Swig_register_filebyname("freepascal_typesdecl", types_decl);


      if (Strcmp(dllname, "") == 0)
        dllname = NewStringf("%s.dll",pasraw_module_name);

      if (Strcmp(wrapdllname, "") == 0)
        wrapdllname = NewStringf("%s.dll",pasraw_module_name);


                  //Printf(implementation_type_begin,"\n{$IFDEF LINUX}");
      //Printf(implementation_type_begin,"\nconst __DLLNAME= 'lib%s.so';\n\n", pasraw_module_name);
      //Printf(implementation_type_begin,"\nconst __WRAPDLLNAME= 'lib%s.so';\n\n", pasraw_module_name);
      //Printf(implementation_type_begin,"\n{$ENDIF}");
    
      //printf("name:%s\n", Char(name));
      //SwigType *type = Getattr(n, "syn:type");
      //printf("type:%s\n", Char(type));
      //const String *pasrawimpl_begin = NewString("\n// pasrawimpl_begin\n");
        //typemapLookup(n, "pasrawimpl_begin", classDeclarationName, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF);
                  //if (hasContent(pasrawimpl_begin))
        //printf("pasrawimpl_begin:%s\n", Char(pasrawimpl_begin));
      //Printv(implementation_functions, pasrawimpl_begin, NIL);
      //Replaceall(implementation_functions, "$passclassname", pasraw_module_name);
      
      GlobalConstantList = DohNewList(); 

      scanAllAddCIName(n);
      scanAllAddCISuffix(n);

    
      /* Emit code */
      Language::top(n);

      if (directorsEnabled()) {
        // Insert director runtime into the f_runtime file (make it occur before %header section)
        Swig_insert_file("director.swg", f_runtime);
      }



#if 0
      // Generate pasmakefile
      // This will be unnecessary if SWIG is invoked from Quake.
      {
        File *file = openWriteFile(NewStringf("%spasmakefile", Swig_file_dirname(outfile)));

        Printf(file, "%% automatically generated quake file for %s\n\n", name);

        /* Write the fragments written by '%insert'
        collected while 'top' processed the parse tree */
        Printv(file, pasmakefile, NIL);

        Printf(file, "import(\"libpas\")\n");
        //Printf(file, "import_lib(\"%s\",\"/usr/lib\")\n", name);
        Printf(file, "module(\"%s\")\n", pasraw_module_name);
        Printf(file, "module(\"%s\")\n\n", paswrap_name);

        if (targetlibrary != NIL) {
          Printf(file, "library(\"%s\")\n", targetlibrary);
        } else {
          Printf(file, "library(\"pas%s\")\n", name);
        }
        Close(file);
      }
#endif 
#if 0
      // Generate the raw interface
      {
        File *file = openWriteFile(NewStringf("%s%s.inc", Swig_file_dirname(outfile), pasraw_module_name));

        emitBanner(file);

        Printf(file, "interface %s;\n\n", pasraw_module_name);

        emitImportStatements(pasraw_intf.import, file);
        Printf(file, "\n");

        // Write the interface generated within 'top'
        Printv(file, pasraw_intf.f, NIL);

        // Printf(file, "\nEND %s.\n", pasraw_module_name);
        Close(file);
      }

      // Generate the raw module
      {
        File *file = openWriteFile(NewStringf("%s%s.%s", Swig_file_dirname(outfile), pasraw_module_name, pasraw_ext));

        emitBanner(file);

        Printf(file, "{$include '%s'};\n\n", NewStringf("%s.inc",  pasraw_module_name));

        Printf(file, "implementation\n\n", pasraw_module_name);

        emitImportStatements(pasraw_impl.import, file);
        Printf(file, "\n");

        // will be empty usually
        Printv(file, pasraw_impl.f, NIL);

        Printf(file, "initialization\n");
        Printf(file, "finalization\n");
        Printf(file, "end.\n");
        Close(file);
      }
#else
      // Generate the raw interface


      {
        File *file = openWriteFile(NewStringf("%s%s.%s", Swig_file_dirname(outfile), pasraw_module_name, pasraw_ext));

        emitBanner(file);

        Printf(file, "unit %s;\n\n", pasraw_module_name);

        Printf(file, "{$mode objfpc}{$H+}\n\n");

        Printf(file, "{$define %s_FUNCTION_WRAPPER}\n",  pasraw_module_name);
        Printf(file, "{$define %s_CLASS_WRAPPER}\n",  pasraw_module_name);


        //interface

        Printf(file, "\ninterface\n\n");


        {
          Iterator it = First(enumeration_coll);
          if (it.key != NIL) {
            Printf(file, "type\n");
          }
          for (; it.key != NIL; it = Next(it)) {
            Printf(file, "\n");
            emitEnumeration(file, it.key, it.item);
          }

        }

        emitImportStatements(pasraw_intf.import, file);
        Printf(file, "\n");

        Printv(file, interface_uses, NIL);

        Printv(file, interface_type_begin, NIL);

        Printv(file, "//pasraw_intf.f\n", NIL);
        Printv(file, pasraw_intf.f, NIL);

        Printv(file, interface_type_end, NIL);
        
        Printv(file, interface_functions, NIL);
        Printf(file, "\n{$ifdef %s_FUNCTION_WRAPPER}\n",  pasraw_module_name);
        Printv(file, interface_functions_wrapper, NIL);
        Printf(file, "\n{$endif} //%s_FUNCTION_WRAPPER\n",  pasraw_module_name);

        Printf(file, "\n{$ifdef %s_CLASS_WRAPPER}\n",  pasraw_module_name);
        Printv(file, paswrap_intf.f, NIL);  
        Printf(file, "\n{$endif} //%s_CLASS_WRAPPER\n",  pasraw_module_name);



        Printf(file,"// Output a Pascal type wrapper class for each SWIG type");
        for (Iterator swig_type = First(swig_types_hash); swig_type.item != NIL; swig_type = Next(swig_type)) {
          // emitTypes(file, swig_type.key, swig_type.item);
          // emitTypeWrapperClass(swig_type.key, swig_type.item);
        }  


        //implementation

        Printf(file, "\nimplementation\n\n", pasraw_module_name);
        
        Printv(file, implementation_type_begin, NIL);

        // emitImportStatements(pasraw_impl.import, file);
        // Printf(file, "\n");

        Printv(file, implementation_uses, NIL);

        // will be empty usually
        Printv(file, pasraw_impl.f, NIL);

        Printv(file, implementation_type_end, NIL);
        Replaceall(implementation_functions, "$pasclassname", pasraw_module_name);
        Printv(file, implementation_functions, NIL);

        Printf(file, "\n{$ifdef %s_FUNCTION_WRAPPER}\n",  pasraw_module_name);
        Replaceall(implementation_functions_wrapper, "$pasclassname", pasraw_module_name);
        Printv(file, implementation_functions_wrapper, NIL);
        Printf(file, "\n{$endif} //%s_FUNCTION_WRAPPER\n",  pasraw_module_name);

        Printf(file, "\n{$ifdef %s_CLASS_WRAPPER}\n",  pasraw_module_name);
        Printv(file, paswrap_impl.f, NIL);  
        Printf(file, "\n{$endif} //%s_CLASS_WRAPPER\n",  pasraw_module_name);


        Printf(file, "\ninitialization\n\n", pasraw_module_name);
        Printv(file, constant_initialization, NIL);      
        Printv(file, initialization   , NIL);

        Printf(file, "\nfinalization\n", pasraw_module_name);
        Printv(file, finalization   , NIL);
        Printf(file, "\nend.\n", pasraw_module_name);


        Delete(file);

        if (upcasts_code)
          Printv(f_wrappers, upcasts_code, NIL);

        Printf(f_wrappers, "#ifdef __cplusplus\n");
        Printf(f_wrappers, "}\n");
        Printf(f_wrappers, "#endif\n");

      }

#endif
#if 0
      // Generate the interface for the comfort wrappers
      {
        File *file = openWriteFile(NewStringf("%s%s.i3", Swig_file_dirname(outfile), paswrap_name));

        emitBanner(file);

        Printf(file, "INTERFACE %s;\n", paswrap_name);

        emitImportStatements(paswrap_intf.import, file);
        Printf(file, "\n");

        {
          Iterator it = First(enumeration_coll);
          if (it.key != NIL) {
            Printf(file, "TYPE\n");
          }
          for (; it.key != NIL; it = Next(it)) {
            Printf(file, "\n");
            emitEnumeration(file, it.key, it.item);
          }
        }

        // Add the wrapper methods
        Printv(file, paswrap_intf.f, NIL);

        // Finish off the class
        Printf(file, "\nEND %s.\n", paswrap_name);
        Close(file);
      }

      // Generate the wrapper routines implemented in Pascal
      {
        File *file = openWriteFile(NewStringf("%s%s.%s", Swig_file_dirname(outfile), paswrap_name, pasraw_ext));

        emitBanner(file);

        if (unsafe_module) {
          Printf(file, "UNSAFE ");
        }
        Printf(file, "MODULE %s;\n\n", paswrap_name);

        emitImportStatements(paswrap_impl.import, file);
        Printf(file, "\n");

        // Add the wrapper methods
        Printv(file, paswrap_impl.f, NIL);

        Printf(file, "\nBEGIN\nEND %s.\n", paswrap_name);
        Delete(file);
      }

      if (upcasts_code)
        Printv(f_wrappers, upcasts_code, NIL);

      Printf(f_wrappers, "#ifdef __cplusplus\n");
      Printf(f_wrappers, "}\n");
      Printf(f_wrappers, "#endif\n");

      // Output a Pascal type wrapper class for each SWIG type
      for (Iterator swig_type = First(swig_types_hash); swig_type.item != NIL; swig_type = Next(swig_type)) {
        emitTypeWrapperClass(swig_type.key, swig_type.item, n);
      }
#endif
      Delete(swig_types_hash);
      swig_types_hash = NULL;
      Delete(constant_values);
      constant_values = NULL;
      Delete(enumeration_coll);
      enumeration_coll = NULL;
      Delete(pasraw_module_name);
      pasraw_module_name = NULL;
      Delete(pasraw_baseclass);
      pasraw_baseclass = NULL;
      Delete(pasraw_interfaces);
      pasraw_interfaces = NULL;
      Delete(pasraw_class_modifiers);
      pasraw_class_modifiers = NULL;
      Delete(pasraw_imports);
      pasraw_imports = NULL;
      Delete(pasraw_cppcasts_code);
      pasraw_cppcasts_code = NULL;
      Delete(proxy_class_def);
      Delete(proxy_class_constructor_def);
      proxy_class_def = NULL;
      Delete(proxy_class_code);
      proxy_class_code = NULL;
      Delete(paswrap_name);
      paswrap_name = NULL;
      Delete(paswrap_modifiers);
      paswrap_modifiers = NULL;
      Delete(targetlibrary);
      targetlibrary = NULL;
      Delete(module_baseclass);
      module_baseclass = NULL;
      Delete(module_interfaces);
      module_interfaces = NULL;
      Delete(module_imports);
      module_imports = NULL;
      Delete(upcasts_code);
      upcasts_code = NULL;
      Delete(constantfilename);
      constantfilename = NULL;
      Delete(renamefilename);
      renamefilename = NULL;
      Delete(typemapfilename);
      typemapfilename = NULL;

      Delete(nested_classes_hash);
      nested_classes_hash = NULL;
      /*
      Close(types_decl);
      Delete(types_decl);
      types_decl = 0;
      */
      Delete(types_hash);
      types_hash = 0;


      /* Close all of the files */
      Dump(f_header, f_runtime);

      if (directorsEnabled()) {
        Dump(f_directors, f_runtime);
        Dump(f_directors_h, f_runtime_h);

        Printf(f_runtime_h, "\n");
        Printf(f_runtime_h, "#endif\n");

        //Close(f_runtime_h);
        Delete(f_runtime_h);
        f_runtime_h = NULL;
        Delete(f_directors);
        f_directors = NULL;
        Delete(f_directors_h);
        f_directors_h = NULL;
      }


      Dump(f_wrappers, f_runtime);
      Wrapper_pretty_print(f_init, f_runtime);
      Delete(f_header);
      Delete(f_wrappers);
      Delete(f_init);
      //Close(f_runtime);
      Delete(f_runtime);


      Delete(interface_uses);
      Delete(interface_type_begin);
      Delete(interface_type_end);
      Delete(interface_functions);
      Delete(interface_functions_wrapper);
      Delete(implementation_uses);
      Delete(implementation_type_begin);
      Delete(implementation_type_end);
      Delete(implementation_functions);
      Delete(implementation_functions_wrapper);
      Delete(initialization);
      Delete(finalization);

      TRACE_FUNC_EXIT;

      return SWIG_OK;
    }

    /* -----------------------------------------------------------------------------
    * emitBanner()
    * ----------------------------------------------------------------------------- */

    void emitBanner(File *f) {
      Printf(f, "(* ----------------------------------------------------------------------------\n");
      Swig_banner_target_lang(f, " *");
      Printf(f, " * ----------------------------------------------------------------------------- *)\n\n");
    }

    /* ----------------------------------------------------------------------
    * nativeWrapper()
    * ---------------------------------------------------------------------- */

    virtual int nativeWrapper(Node *n) {
      String *wrapname = Getattr(n, "wrap:name");

      if (!addSymbol(wrapname, n))
        return SWIG_ERROR;

      if (Getattr(n, "type")) {
        Swig_save("nativeWrapper", n, "name", NIL);
        Setattr(n, "name", wrapname);
        native_function_flag = true;
        functionWrapper(n);
        Swig_restore(n);
        native_function_flag = false;
      } else {
        Printf(stderr, "%s : Line %d. No return type for %%native method %s.\n", input_file, line_number, Getattr(n, "wrap:name"));
      }

      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * functionWrapper()
    * ---------------------------------------------------------------------- */

    virtual int functionWrapper(Node *n) {
      String *type = nodeType(n);
      String *funcType = Getattr(n, "freepascal:functype");
      String *rawname = Copy(Getattr(n, "name"));
      String *symname = Copy(Getattr(n, "sym:name"));

      int nowrap_function = Getfeature(n, "freepascal:nowrap_function") != 0;    

      Replace(rawname,"::","_", DOH_REPLACE_ANY);
      Replace(symname,"::","_", DOH_REPLACE_ANY);


      String *capname = Copy(symname);
      String *wname = Swig_name_wrapper(symname);

      // global name. We need to add prefix to global wrapped C names 
      // so they differ from C++ ones to avoid linker collisions. 
      String *gname; 
      SwigType *view = Getattr(n, "view");
      if (Equal(view,"globalfunctionHandler")) {
	gname = NewStringf("__%s",wname);
	wname = Copy(gname);
      }


      //printf("function: %s\n", Char(symname));
      //printf(" purpose: %s\n", Char(funcType));

      if (Strcmp(type, "constant") == 0) {
        Setattr(n, "freepascal:funcname", capname);
        emitCWrapper(n, wname);
        emitPasRawPrototype(n, wname, capname);
      }

      if (Strcmp(type, "enumitem") == 0) {
        Setattr(n, "freepascal:funcname", capname);
        emitCWrapper(n, wname);
        emitPasRawPrototype(n, wname, capname);
      }

      if (Strcmp(type, "cdecl") == 0) {
        if (funcType == NIL) {
          // no wrapper needed for plain functions ?

          if (wrapping_member_flag && static_flag ) {
            emitCWrapper(n, wname);
            emitPasRawPrototype(n, wname, symname);
          }
          else if (Getattr(n,"template") != 0) {
            emitCWrapper(n, wname);
            emitPasRawPrototype(n, wname, symname);
          }
          else if (variable_wrapper_flag || global_variable_flag) {

            Setattr(n, "proxyfuncname", capname);
            Setattr(n, "imfuncname", symname);
            if (hasPrefix(capname, "Set")) {
              Setattr(n, "freepascal:setname", capname);
            } else {
              Setattr(n, "freepascal:getname", capname);
            }

            //char *ch = Char(capname);

            //          printf("%X\n", n);

            emitCWrapper(n, wname);
            emitPasRawPrototype(n, wname, capname);
            /*
            emitPasRawPrototype(n, rawname, symname);
            emitCWrapper(n, wname);
            */
          }
          else {

            if (native_function_flag || nowrap_function)

              emitPasRawPrototype(n, rawname, symname);            

            else
              emitPasRawPrototype(n, wname, capname);

            if (!nowrap_function)

              emitCWrapper(n, wname);
          }
          //  emitPasWrapper(n, symname);
        } else if (Strcmp(funcType, "method") == 0) {
          Setattr(n, "freepascal:funcname", capname);
          emitCWrapper(n, wname);
          emitPasRawPrototype(n, wname, capname);
          //emitPasWrapper(n, capname);
        } else if (Strcmp(funcType, "accessor") == 0) {
          /*
          * Generate the proxy class properties for public member variables.
          * Not for enums and constants.
          */
          if (/*proxy_flag && */wrapping_member_flag && !enum_constant_flag) {
            // Capitalize the first letter in the function name
            Setattr(n, "proxyfuncname", capname);
            Setattr(n, "imfuncname", symname);
            if (hasPrefix(capname, "Set")) {
              Setattr(n, "freepascal:setname", capname);
            } else {
              Setattr(n, "freepascal:getname", capname);
            }

            //char *ch = Char(capname);

            //          printf("%X\n", n);

            emitCWrapper(n, wname);
            emitPasRawPrototype(n, wname, capname);
            //    emitPasWrapper(n, capname);
            if (proxy_flag)  
              proxyClassFunctionHandler(n);

          }
#ifdef DEBUG
        } else {
          Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Function type <%s> unknown.\n", Char(funcType));
#endif
        }
      } else if ((Strcmp(type, "constructor") == 0) || (Strcmp(type, "destructor") == 0)) {
        emitCWrapper(n, wname);
        emitPasRawPrototype(n, wname, capname);
        //   emitPasWrapper(n, capname);
      }
#if 0
      // a Java relict
      if (!(proxy_flag && is_wrapping_class()) && !enum_constant_flag) {
        emitPasWrapper(n, capname);
      }
#endif
#if 0
      //  Delete(capname);
      //  Delete(wname);
      //  Delete(rawname);
      //  Delete(symname);
#endif 
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * emitCWrapper()
    *
    * Generate the wrapper in C which calls C++ methods.
    * ---------------------------------------------------------------------- */

#if old

    virtual int emitCWrapper(Node *n, const String *wname) {
      TRACE_FUNC_ENTR;

      String *rawname = Getattr(n, "name");
      String *c_return_type = NewString("");
      String *cleanup = NewString("");
      String *outarg = NewString("");
      String *body = NewString("");
      Hash *throws_hash = NewHash();
      ParmList *l = Getattr(n, "parms");
      SwigType *t = Getattr(n, "type");
      String *symname = Getattr(n, "sym:name");
      //String *view = Getattr(n, "view");

      String * overloaded = Getattr(n,"sym:overloaded");
      String * overloaded_name = Getattr(n,"sym:overname");


      String *fname;




      if (overloaded) // overloaded has a pointer, but string value can be 0x0
        fname = NewStringf("%s%s", wname, overloaded_name);
      else
        fname = Copy(wname);


      char *ch = 0;

      ch = Char(rawname);

      int br =  strcmp(ch, "paList");

      if (!Getattr(n, "sym:overloaded")) {
        if (!addSymbol(wname, n)) {
          return SWIG_ERROR;
        }
      }
      // A new wrapper function object
      Wrapper *f = NewWrapper();


      /* Attach the non-standard typemaps to the parameter list. */
      Swig_typemap_attach_parms("ctype", l, f);

      /* Get return types */
      {
        String *tm = getMappedTypeNew(n, "ctype", "");
        if (tm != NIL) {
          Printf(c_return_type, "%s", tm);
        }
      }

      bool is_void_return = (Cmp(c_return_type, "void") == 0);
      if (!is_void_return) {
        Wrapper_add_localv(f, "cresult", c_return_type, "cresult = 0", NIL);
      }

      Printv(f->def, " SWIGEXPORT ", c_return_type, " SWIGSTDCALL ", fname, "(", NIL);

      // Emit all of the local variables for holding arguments.
      emit_parameter_variables(t, l, f);

      /* Attach the standard typemaps */
      emit_attach_parmmaps(l, f);
      Setattr(n, "wrap:name", Copy(fname));
      Setattr(n, "wrap:parms", l);

      // Generate signature and argument conversion for C wrapper
      {
        Parm *p;
        attachParameterNames(n, "tmap:name", "c:wrapname", "pasarg%d");
        bool gencomma = false;
        for (p = skipIgnored(l, "in"); p != NULL; p = skipIgnored(p, "in")) {

          String *arg = Getattr(p, "c:wrapname");
          {
            /* Get the ctype types of the parameter */
            String *c_param_type = getMappedType(p, "ctype");
            // Add parameter to C function
            Printv(f->def, gencomma ? ", " : "", c_param_type, " ", arg, NIL);
#if 0
            Delete(c_param_type);
#endif
            gencomma = true;
          }

          // Get typemap for this argument
          String *tm = getMappedType(p, "in");
          if (tm != NIL) {
            addThrows(throws_hash, "in", p);
            Replaceall(tm, "$input", arg);
            Setattr(p, "emit:input", arg);  /*??? */
            Printf(f->code, "%s\n", tm);
            p = Getattr(p, "tmap:in:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert constraint checking code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:check");
          if (tm != NIL) {
            addThrows(throws_hash, "check", p);
            Replaceall(tm, "$target", Getattr(p, "lname"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(f->code, tm, "\n", NIL);
            p = Getattr(p, "tmap:check:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert cleanup code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:freearg");
          if (tm != NIL) {
            addThrows(throws_hash, "freearg", p);
            Replaceall(tm, "$source", Getattr(p, "emit:input"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(cleanup, tm, "\n", NIL);
            p = Getattr(p, "tmap:freearg:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert argument output code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:argout");
          if (tm != NIL) {
            addThrows(throws_hash, "argout", p);
            Replaceall(tm, "$source", Getattr(p, "emit:input"));  /* deprecated */
            Replaceall(tm, "$target", Getattr(p, "lname"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$result", "cresult");
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(outarg, tm, "\n", NIL);
            p = Getattr(p, "tmap:argout:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      // Get any Pascal exception classes in the throws typemap
      ParmList *throw_parm_list = NULL;
      if ((throw_parm_list = Getattr(n, "throws"))) {
        Swig_typemap_attach_parms("throws", throw_parm_list, f);
        Parm *p;
        for (p = throw_parm_list; p; p = nextSibling(p)) {
          addThrows(throws_hash, "throws", p);
        }
      }

      if (Cmp(nodeType(n), "constant") == 0) {
        // Wrapping a constant hack
        Swig_save("functionWrapper", n, "wrap:action", NIL);

        // below based on Swig_VargetToFunction()
        SwigType *ty = Swig_wrapped_var_type(Getattr(n, "type"), use_naturalvar_mode(n));
        Setattr(n, "wrap:action", NewStringf("result = (%s) %s;\n", SwigType_lstr(ty, 0), Getattr(n, "value")));
      }
      // Now write code to make the function call
      if (!native_function_flag) {
        emit_action(n, f);
      }

      if (Cmp(nodeType(n), "constant") == 0) {
        Swig_restore(n);
      }

      /* Return value if necessary  */
      if (!native_function_flag) {
        String *tm = getMappedTypeNew(n, "out", "result");
        if (tm != NIL) {
          addThrows(throws_hash, "out", n);
          Replaceall(tm, "$source", "result");  /* deprecated */
          Replaceall(tm, "$target", "cresult");  /* deprecated */
          Replaceall(tm, "$result", "cresult");
          Printf(f->code, "%s", tm);
          if (hasContent(tm))
            Printf(f->code, "\n");
        } else {
          Swig_warning(WARN_TYPEMAP_OUT_UNDEF, input_file, line_number, "Unable to use return type %s in function %s.\n", SwigType_str(t, 0), rawname);
        }
      }

      /* Output argument output code */
      Printv(f->code, outarg, NIL);

      /* Output cleanup code */
      Printv(f->code, cleanup, NIL);

      /* Look to see if there is any newfree cleanup code */
      if (GetFlag(n, "feature:new")) {
        //    String *tm = Swig_typemap_lookup_new("newfree", n, "result", 0);
        String *tm = Swig_typemap_lookup("newfree", n, "result", 0);

        if (tm != NIL) {
          addThrows(throws_hash, "newfree", n);
          Replaceall(tm, "$source", "result");  /* deprecated */
          Printf(f->code, "%s\n", tm);
        }
      }

      /* See if there is any return cleanup code */
      if (!native_function_flag) {
        //String *tm = Swig_typemap_lookup_new("ret", n, "result", 0);
        String *tm = Swig_typemap_lookup("ret", n, "result", 0);
        if (tm != NIL) {
          Replaceall(tm, "$source", "result");  /* deprecated */
          Printf(f->code, "%s\n", tm);
        }
      }

      /* Finish C wrapper */
      Printf(f->def, ") {");

      if (!is_void_return)
        Printv(f->code, "    return cresult;\n", NIL);
      Printf(f->code, "}\n");

      /* Substitute the cleanup code */
      Replaceall(f->code, "$cleanup", cleanup);

      /* Substitute the function name */
      Replaceall(f->code, "$symname", symname);

      if (!is_void_return) {
        Replaceall(f->code, "$null", "0");
      } else {
        Replaceall(f->code, "$null", "");
      }

      /* Dump the function out */
      if (!native_function_flag) {
        Wrapper_print(f, f_wrappers);
      }

      //  Setattr(n, "wrap:parms", 0);



#if 0
      Delete(c_return_type);
      Delete(cleanup);
      Delete(outarg);
      Delete(body);
      Delete(throws_hash);
      DelWrapper(f);
      Delete(fname);
#endif

      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

#endif

    virtual int emitCWrapper(Node *n, const String *wname) {
      TRACE_FUNC_ENTR;
      String *rawname = Getattr(n, "name");
      String *c_return_type = NewString("");
      String *cleanup = NewString("");
      String *outarg = NewString("");
      //String *body = NewString("");
      Hash *throws_hash = NewHash();
      ParmList *l = Getattr(n, "parms");
      SwigType *t = Getattr(n, "type");
      String *symname = Getattr(n, "sym:name");
      
      String * overloaded = Getattr(n,"sym:overloaded");
      String * overloaded_name = Getattr(n,"sym:overname");
      String *fname;

      if (overloaded) // overloaded has a pointer, but string value can be 0x0
        fname = NewStringf("%s%s", wname, overloaded_name);
      else
        fname = Copy(wname);


      if (!Getattr(n, "sym:overloaded")) {
        if (!addSymbol(wname, n)) {
          return SWIG_ERROR;
        }
      }
      // A new wrapper function object
      Wrapper *f = NewWrapper();

      /* Attach the non-standard typemaps to the parameter list. */
      Swig_typemap_attach_parms("ctype", l, f);

      /* Get return types */
      {
        String *tm = getMappedTypeNew(n, "ctype", "");
        if (tm != NIL) {
          Printf(c_return_type, "%s", tm);
        }
      }

      bool is_void_return = (Cmp(c_return_type, "void") == 0);
      if (!is_void_return) {
        Wrapper_add_localv(f, "cresult", c_return_type, "cresult = 0", NIL);
      }

      Printv(f->def, " SWIGEXPORT ", c_return_type, " ", fname, "(", NIL);

      // Emit all of the local variables for holding arguments.
      emit_parameter_variables(l, f);

      /* Attach the standard typemaps */
      emit_attach_parmmaps(l, f);
      Setattr(n, "wrap:parms", l);

      // Generate signature and argument conversion for C wrapper
      {
        Parm *p;
        attachParameterNames(n, "tmap:name", "c:wrapname", "pasarg%d");
        bool gencomma = false;
        for (p = skipIgnored(l, "in"); p != NULL; p = skipIgnored(p, "in")) {
	  
	  LOG_NODE_DEBUG(p);

          String *arg = Getattr(p, "c:wrapname");
          {
            /* Get the ctype types of the parameter */
            String *c_param_type = getMappedType(p, "ctype");
            // Add parameter to C function
            Printv(f->def, gencomma ? ", " : "", c_param_type, " ", arg, NIL);
#if 0  
            Delete(c_param_type);
#endif
            gencomma = true;
          }

          // Get typemap for this argument
          String *tm = getMappedType(p, "in");
          if (tm != NIL) {
            addThrows(throws_hash, "in", p);
            Replaceall(tm, "$input", arg);
            Setattr(p, "emit:input", arg);  /*??? */
            Printf(f->code, "%s\n", tm);
            p = Getattr(p, "tmap:in:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert constraint checking code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:check");
          if (tm != NIL) {
            addThrows(throws_hash, "check", p);
            Replaceall(tm, "$target", Getattr(p, "lname"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(f->code, tm, "\n", NIL);
            p = Getattr(p, "tmap:check:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert cleanup code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:freearg");
          if (tm != NIL) {
            addThrows(throws_hash, "freearg", p);
            Replaceall(tm, "$source", Getattr(p, "emit:input"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(cleanup, tm, "\n", NIL);
            p = Getattr(p, "tmap:freearg:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Insert argument output code */
      {
        Parm *p;
        for (p = l; p;) {
          String *tm = Getattr(p, "tmap:argout");
          if (tm != NIL) {
            addThrows(throws_hash, "argout", p);
            Replaceall(tm, "$source", Getattr(p, "emit:input"));  /* deprecated */
            Replaceall(tm, "$target", Getattr(p, "lname"));  /* deprecated */
            Replaceall(tm, "$arg", Getattr(p, "emit:input"));  /* deprecated? */
            Replaceall(tm, "$result", "cresult");
            Replaceall(tm, "$input", Getattr(p, "emit:input"));
            Printv(outarg, tm, "\n", NIL);
            p = Getattr(p, "tmap:argout:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      // Get any Pascal exception classes in the throws typemap
      ParmList *throw_parm_list = NULL;
      if ((throw_parm_list = Getattr(n, "throws"))) {
        Swig_typemap_attach_parms("throws", throw_parm_list, f);
        Parm *p;
        for (p = throw_parm_list; p; p = nextSibling(p)) {
          addThrows(throws_hash, "throws", p);
        }
      }

      if (Cmp(nodeType(n), "constant") == 0) {
        // Wrapping a constant hack
        Swig_save("functionWrapper", n, "wrap:action", NIL);

        // below based on Swig_VargetToFunction()
        SwigType *ty = Swig_wrapped_var_type(Getattr(n, "type"), use_naturalvar_mode(n));
        Setattr(n, "wrap:action", NewStringf("result = (%s) %s;", SwigType_lstr(ty, 0), Getattr(n, "value")));
      }

      Setattr(n, "wrap:name", wname);

      // Now write code to make the function call
      if (!native_function_flag) {
        String *actioncode = emit_action(n);

        if (Cmp(nodeType(n), "constant") == 0) {
          Swig_restore(n);
        }

        /* Return value if necessary  */
        String *tm;
        if ((tm = Swig_typemap_lookup_out("out", n, "result", f, actioncode))) {
          addThrows(throws_hash, "out", n);
          Replaceall(tm, "$source", "result");  /* deprecated */
          Replaceall(tm, "$target", "cresult");  /* deprecated */
          Replaceall(tm, "$result", "cresult");
          Printf(f->code, "%s", tm);
          if (hasContent(tm))
            Printf(f->code, "\n");
        } else {
          Swig_warning(WARN_TYPEMAP_OUT_UNDEF, input_file, line_number, "Unable to use return type %s in function %s.\n", SwigType_str(t, 0), rawname);
        }
        emit_return_variable(n, t, f);
      }

      /* Output argument output code */
      Printv(f->code, outarg, NIL);

      /* Output cleanup code */
      Printv(f->code, cleanup, NIL);

      /* Look to see if there is any newfree cleanup code */
      if (GetFlag(n, "feature:new")) {
        String *tm = Swig_typemap_lookup("newfree", n, "result", 0);
        if (tm != NIL) {
          addThrows(throws_hash, "newfree", n);
          Replaceall(tm, "$source", "result");  /* deprecated */
          Printf(f->code, "%s\n", tm);
        }
      }

      /* See if there is any return cleanup code */
      if (!native_function_flag) {
        String *tm = Swig_typemap_lookup("ret", n, "result", 0);
        if (tm != NIL) {
          Replaceall(tm, "$source", "result");  /* deprecated */
          Printf(f->code, "%s\n", tm);
        }
      }

      /* Finish C wrapper */
      Printf(f->def, ") {");

      if (!is_void_return)
        Printv(f->code, "    return cresult;\n", NIL);
      Printf(f->code, "}\n");

      /* Substitute the cleanup code */
      Replaceall(f->code, "$cleanup", cleanup);

      /* Substitute the function name */
      Replaceall(f->code, "$symname", symname);

      if (!is_void_return) {
        Replaceall(f->code, "$null", "0");
      } else {
        Replaceall(f->code, "$null", "");
      }

      /* Dump the function out */
      if (!native_function_flag) {
        Wrapper_print(f, f_wrappers);
      }
#if 0
      Delete(c_return_type);
      Delete(cleanup);
      Delete(outarg);
      Delete(body);
      Delete(throws_hash);
      DelWrapper(f);
#endif
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }



    /* ----------------------------------------------------------------------
    * emitPasRawPrototype()
    *
    * Generate an EXTERNAL procedure declaration in Pascal
    * which is the interface to an existing C routine or a C wrapper.
    * ---------------------------------------------------------------------- */


    char * ChopCh(char * S, int ch)
    {
      int len = (int)strlen(S);
      if (S[len-1] == ch)
        S[len-1] = 0;
      return S;

    }

    virtual int emitPasRawPrototype1(Node *n, const String *cname, const String *pasname, String  *f, char *_external, Hash *import, String *_storage) {
      //String *type = nodeType(n);
      String * overloaded = Getattr(n,"sym:overloaded");
      String * overloaded_name = Getattr(n,"sym:overname");
      String *im_return_type = NewString("");
      //String   *symname = Getattr(n,"sym:name");
      ParmList *l = Getattr(n, "parms");

      //String *storage= 0;//= Getattr(n, "storage");
      //storage = NewStringf(callconv);

      String *_pars = NewStringf("");

      /* Attach the non-standard typemaps to the parameter list. */
      Swig_typemap_attach_parms("pasrawinmode", l, NULL);
      Swig_typemap_attach_parms("pasrawintype", l, NULL);


      //       if (strcmp(Char(cname), "Delete_gamma_lut_type") == 0) __asm { int 3};

      /* Get return types */
      bool has_return = false;

      /*{
        if (Equal(type,"constructor")) {
          im_return_type = CObjPtrTypeName;
          has_return = true;
        }  
        else {
	  */		
	  String *tm = getMappedTypeNew(n, "pasrawrettype", "");
      if (tm != NIL) {
         RemoveNamespace(tm);
	     Printf(im_return_type, "%s", tm);
	    
	      //set type to named pointer for other wrappers returning C object of our type
	     //if (Equal(im_return_type, proxy_class_name)) {
	     // im_return_type = CObjPtrTypeName;
	     //}
	
         has_return = hasContent(tm);
	  }
        //}  
      //}

      Setattr(n, "freepascal:pasname", pasname);

      /* cname is the original name if 'n' denotes a C function
      and it is the relabeled name (sym:name) if 'n' denotes a C++ method or similar */
      //f.enterBlock(no_block);
      if (has_return) 
        Printf(f, "\n  function %s(", pasname);
      else 
              Printf(f, "\n  procedure %s(", pasname);
      
      // Generate signature for raw interface
      {
        Parm *p;
        writeArgState state;
        attachParameterNames(n, "tmap:rawinname", "freepascal:rawname", "arg%d");
        for (p = skipIgnored(l, "pasrawintype"); p; p = skipIgnored(p, "pasrawintype")) {

    /* Get argument passing mode, should be one of VALUE, VAR, CONST */
    String *mode = Getattr(p, "tmap:pasrawinmode");
    String *argname = Getattr(p, "freepascal:rawname");
    String *im_param_type = getMappedType(p, "pasrawintype");
    //SwigType *argtype  = Getattr(p,"type");
    RemoveNamespace(im_param_type);
    //Replace(im_param_type," const", "", DOH_REPLACE_FIRST);

/*
    // set pointer to flat C type for destructors and static members
    //if (Equal(argname,"self") && Equal(im_param_type, proxy_class_name)) {
    if (Equal(im_param_type, proxy_class_name)) {
      im_param_type = CObjPtrTypeName;
    }
*/
    if (import)
      addImports(import, "pasrawintype", p);
    
    if (argname != NULL) {  // varargs skipped"
      writeArg(p, _pars, state, mode, argname, im_param_type, NIL);
    }
    
    if (im_param_type != NIL) {
      p = Getattr(p, "tmap:pasrawintype:next");
    } 
    else {
      p = nextSibling(p);
    }
        }
        writeArg(p, _pars, state, NIL, NIL, NIL, NIL);
      }

      /* Finish PAS raw prototype */
      //printf("//_pars:'%s'\n",Char(_pars));
      Chop(_pars);
      Printf(f, "%s )", ChopCh(Char(_pars),';'));
      // neither a C wrapper nor a plain C function may throw an exception
      //generateThrowsClause(throws_hash, pasraw_intf.f);

      if (has_return) {
        Printf(f, ":%s", im_return_type);
      }

      Printf(f, "");

      Printf(f, "; %s", _storage);

      if (overloaded) // overloaded has a pointer, but string value can be 0x0
        Printf(f, "; overload" );


      //char * ss = Char(storage);

      if (_external != 0) {

        if (overloaded) // overloaded has a pointer, but string value can be 0x0
          Printf(f, "; external %s name '%s%s'", _external, cname, overloaded_name);
        else
          Printf(f, "; external %s name '%s'", _external, cname);
      }

      Printf(f, ";\n");
#if 0
      Delete(_pars);
      Delete(im_return_type);
#endif

      return SWIG_OK;

    }

    virtual int emitPasRawPrototype(Node *n, const String *cname, const String *pasname) {

      String *lib = 0;
      String *storage = 0;

      /* 
       * MM note: currently there is no difference that variable_wrapper_flag makes. 
       * It generates single C++ wrapper DLL where all wrappers are: 
       * both, methods, and generated Set/Get-ters for public vars.
      */
      if (variable_wrapper_flag) {
        lib = NewString("__WRAPDLLNAME");
        storage=NewString("stdcall");
      }
      else {
        lib = NewString("__WRAPDLLNAME");
        storage=Copy(callconv);
      }

      if (variable_wrapper_flag)
        emitPasRawPrototype1(n, cname, pasname, interface_functions_wrapper, 0, pasraw_intf.import, storage);
      else
        emitPasRawPrototype1(n, cname, pasname, interface_functions, 0, pasraw_intf.import, storage);



      if (variable_wrapper_flag)
        return emitPasRawPrototype1(n, cname, pasname, implementation_functions_wrapper, Char(lib), pasraw_intf.import, storage);
      else
        return emitPasRawPrototype1(n, cname, pasname, implementation_functions, Char(lib), pasraw_intf.import, storage);

#if 0
      Delete(lib);
      Delete(storage);
#endif
    }

    /* -----------------------------------------------------------------------
    * variableWrapper()
    * ----------------------------------------------------------------------- */

    virtual int variableWrapper(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);
      Language::variableWrapper(n);
      LOG_NODE_DEBUG(n);
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* -----------------------------------------------------------------------
    * globalvariableHandler()
    * ----------------------------------------------------------------------- */

    virtual int globalvariableHandler(Node *n) {
      SwigType *t = Getattr(n, "type");
      String *tm;

      // Get the variable type
      if ((tm = getMappedTypeNew(n, "paswraptype", ""))) {
        substituteClassname(t, tm);
      }

      variable_name = Getattr(n, "sym:name");
      variable_type = Copy(tm);

      // Get the variable type expressed in terms of Pascal equivalents of C types
      /*
      if ((tm = getMappedTypeNew(n, "pasrawtype", ""))) {
      pasraw_intf.enterBlock(no_block);
      Printf(pasraw_intf.f, "\n<* EXTERNAL *> var %s: %s;\n", variable_name, tm);
      }
      */
      // Output the property's accessor methods

      global_variable_flag = true;
      /*int ret = */Language::globalvariableHandler(n);
      global_variable_flag = false;


      Printf(paswrap_impl.f, "\n\n");

      //return ret;
      return 1;
    }

    long getConstNumeric(Node *n) {
      String *constnumeric = Getfeature(n, "constnumeric");
      String *name = Getattr(n, "name");
      long numvalue;
      if (constnumeric == NIL) {
        Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Feature 'constnumeric' is necessary to obtain value of %s.\n", name);
        return 0;
      } else if (!strToL(constnumeric, numvalue)) {
        Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number,
          "The feature 'constnumeric' of %s specifies value <%s> which is not an integer constant.\n", name, constnumeric);
        return 0;
      } else {
        return numvalue;
      }
    }


    /* ------------------------------------------------------------------------
    * generateIntConstant()
    *
    * Considers node as an integer constant definition
    * and generate a Pascal constant definition.
    * 
    * MM: TODO replace with set constants got from C domain
    * ------------------------------------------------------------------------ */
    int generateIntConstant(Node *n, String *name) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      String *value = Getattr(n, "value");
      String *type = Getfeature(n, "freepascal:constint:type");
      String *conv = Getfeature(n, "freepascal:constint:conv");

      if (name == NIL) {
        name = Getattr(n, "sym:name");
      }

      long numvalue;
      bool isSimpleNum = strToL(value, numvalue);
      if (!isSimpleNum) {
        numvalue = getConstNumeric(n);
      }

      String *pasvalue;
      if ((conv == NIL) || ((Strcmp(conv, "set:int") != 0) && (Strcmp(conv, "int:set") != 0))) {
        /* The original value of the constant has precedence over
        'constnumeric' feature since we like to keep
        the style (that is the base) of simple numeric constants */
        if (isSimpleNum) {
          if (hasPrefix(value, "0x")) {
            pasvalue = NewStringf("16_%s", Char(value) + 2);
          } else if ((Len(value) > 1) && (*Char(value) == '0')) {
            pasvalue = NewStringf("8_%s", Char(value) + 1);
          } else {
            pasvalue = Copy(value);
          }
          /* If we cannot easily obtain the value of a numeric constant,
          we use the results given by a C compiler. */
        } else {
          pasvalue = Copy(Getfeature(n, "constnumeric"));
        }
      } else {
        // if the value can't be converted, it is ignored
        if (convertInt(numvalue, numvalue, conv)) {
          pasvalue = NewStringf("%d", numvalue);
        } else {
          pasvalue = NIL;
        }
      }

      if (pasvalue != NIL) {
        paswrap_intf.enterBlock(constant);
        Printf(paswrap_intf.f, "%s", name);
        if (hasContent(type)) {
          Printf(paswrap_intf.f, ": %s", type);
        }
        Printf(paswrap_intf.f, " = %s;\n", pasvalue);
#if 0
        Delete(pasvalue);
#endif
      }
      TRACE_FUNC_EXIT;
      return R;
    }

    /* -----------------------------------------------------------------------
    * generateSetConstant()
    *
    * Considers node as a set constant definition
    * and generate a Pascal constant definition.
    * 
    * MM: TODO replace with set constants got from C domain
    * ------------------------------------------------------------------------ */
    void generateSetConstant(Node *n, String *name) {
      TRACE_FUNC_ENTR;
      String *value = Getattr(n, "value");
      String *type = Getfeature(n, "freepascal:constset:type");
      String *setname = Getfeature(n, "freepascal:constset:set");
      String *basename = Getfeature(n, "freepascal:constset:base");
      String *conv = Getfeature(n, "freepascal:constset:conv");

      paswrap_intf.enterBlock(constant);

      Printf(paswrap_intf.f, "%s", name);
      if (type != NIL) {
        Printf(paswrap_intf.f, ":%s ", type);
      }
      Printf(paswrap_intf.f, " = %s{", setname);

      long numvalue = 0;
      if (!strToL(value, numvalue)) {
        numvalue = getConstNumeric(n);
      }
      convertInt(numvalue, numvalue, conv);

      bool isIntType = Strcmp(basename, "CARDINAL") == 0;
      Hash *items = NIL;
      if (!isIntType) {
        Hash *enumeration = Getattr(enumeration_coll, basename);
        if (enumeration == NIL) {
          Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "There is no enumeration <%s> as needed for the set.\n", setname);
          isIntType = true;
        } else {
          items = Getattr(enumeration, "items");
        }
      }

      bool gencomma = false;
      int bitpos = 0;
      while (numvalue > 0) {
        if ((numvalue & 1) != 0) {
          if (isIntType) {
            if (gencomma) {
              Printv(paswrap_intf.f, ",", NIL);
            }
            gencomma = true;
            Printf(paswrap_intf.f, "%d", bitpos);
          } else {
            char bitval[15];
            sprintf(bitval, "%d", bitpos);
            String *bitname = Getattr(items, bitval);
            if (bitname == NIL) {
              Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Enumeration <%s> has no value <%s>.\n", setname, bitval);
            } else {
              if (gencomma) {
                Printv(paswrap_intf.f, ",", NIL);
              }
              gencomma = true;
              Printf(paswrap_intf.f, "%s.%s", basename, bitname);
            }
          }
        }
        numvalue >>= 1;
        bitpos++;
      }
      Printf(paswrap_intf.f, "};\n");
      TRACE_FUNC_EXIT;
    }


    /*
    * MM: TODO replace with set constants got from C kingdom via getters
    */ 

    int generateConstantPascal(Node *n) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      // any of the special interpretation disables the default behaviour
      /*  String *enumitem = Getfeature(n, "freepascal:enumitem:name");
      String *constset = Getfeature(n, "freepascal:constset:name");
      String *constint = Getfeature(n, "freepascal:constint:name");
      if (hasContent(enumitem) || hasContent(constset) || hasContent(constint)) {
      if (hasContent(constset)) {
      generateSetConstant(n, constset);
      }
      if (hasContent(constint)) {
      generateIntConstant(n, constint);
      }
      } else {*/
      
	
        int runtime_const_flag = (Getfeature(n, "freepascal:runtime_const") != 0) || global_runtime_const_flag ;  

        String * value;
        String * pasname;
        if (!enum_wrap_flag)
          value = Getattr(n, "value");
        else {
          value = Getattr(n, "enumvalue");

          if (hasContent(value) == 0) {
            value = NewStringf("%d", enum_begin++);
          }

        }

        String *name = 0; 
/*
        if (enumeration_name)
          name = NewStringf("%s_%s", enumeration_name,  Getattr(n, "sym:name"));
        else
          name = Copy(Getattr(n, "sym:name"));
        if (name == NIL) {
          name = Getattr(n, "name");
        }
*/
        //pasname = Getattr(n, "freepascal:pasname");
	pasname = getPascalName(n); //uppercase constants

        if (runtime_const_flag) {
          variableWrapper(n);

          pasraw_intf.enterBlock(variable);  
          String *pasct = getMappedTypeNew(n, "pasrawtype", "");
          //Printf(constant_initialization, "   %s := %s_get;\n", name,name);
          Printf(constant_initialization, "   %s := %s;\n", pasname,pasname);

          Printf(pasraw_intf.f, "");
          Printf(pasraw_intf.f, "   %s : %s ; // constant initialized at runtime\n", pasname,pasct);

        }
        else {
          pasraw_intf.enterBlock(constant);  
        }

        String *type = Getattr(n,"type");
        if (Strcmp(type, "char") == 0 || Strcmp(type, "p.char") == 0 || Strcmp(type, "p.q(const).char") == 0 ) {
	  R = generateStringConstant(n, pasname, value);
          }
        else 
	  R = generateNumExprConstant(n, pasname, value);

        Delete(value);
        Delete(name);
    
      TRACE_FUNC_EXIT;
      return R;
    }

    int generateConstantCall(Node *n){
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      TRACE_FUNC_EXIT;
      return R;
    }

    int generateConstantVar(Node *n){
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      TRACE_FUNC_EXIT;
      return R;
    }

  Node *findUpNodeByName(Node *n, char *c) {
    Hash *syssymtab = Getattr(n, "sym:symtab");
    Hash *symtab = Getattr(syssymtab, "symtab");
    Node *result = Getattr(symtab, c);
    return result;
  }


   /*
    * tokenize, parse, and replace some C tokens to Pascal ones.
    * also treat properly prefixed and suffixed numbers (like hex, octals and typed ones). 
    */
#define SWIG_TOKEN_LONG_DOUBLE 201
    
    String* convertNumExpression(Node *n, String *s) {
      int token;
      String *type = Getattr(n,"type");
      
      Scanner *scan = NewScanner();
      Seek(s, 0, SEEK_SET);
      Scanner_clear(scan);
      String *R = NewString("");
      
      s = Copy(s);
      Seek(s, SEEK_SET, 0);
      Scanner_push(scan, s);
      
      while ((token = Scanner_token(scan))) {

        if ((token == SWIG_TOKEN_INT) || (token == SWIG_TOKEN_UINT) 
	 || (token == SWIG_TOKEN_LONG) || (token == SWIG_TOKEN_ULONG)
	 || (token == SWIG_TOKEN_LONGLONG) || (token == SWIG_TOKEN_ULONGLONG)
	 ) {
	  char *c = Char(Scanner_text(scan));
	  String *V = NewString(c);
	  // prefix
	  if (strstr(c,"0x")==c) Replace(V,"0x","$", DOH_REPLACE_FIRST ); // hex
	  else if (strstr(c,"0X")==c) Replace(V,"0X","$", DOH_REPLACE_FIRST ); // hex
	  else if (strstr(c,"0b")==c) Replace(V,"0b","%", DOH_REPLACE_FIRST ); //bin
	  else if (strstr(c,"0B")==c) Replace(V,"0B","%", DOH_REPLACE_FIRST ); //bin
	  else if (strstr(c,"0")==c) Replace(V,"0","&0", DOH_REPLACE_FIRST ); //oct

	  // suffix
          Replace(V,"L","", DOH_REPLACE_NUMBER_END ); //long long
          Replace(V,"l","", DOH_REPLACE_NUMBER_END ); //long long
          Replace(V,"L","", DOH_REPLACE_NUMBER_END ); //long
          Replace(V,"l","", DOH_REPLACE_NUMBER_END ); //long
          Replace(V,"U","", DOH_REPLACE_NUMBER_END ); //unsigned
          Replace(V,"u","", DOH_REPLACE_NUMBER_END ); //unsigned
	  
	  switch (token) {
	    case SWIG_TOKEN_INT:       Printf(R,"integer(%s)",V); break;
	    case SWIG_TOKEN_UINT:      Printf(R,"word(%s)",V); break;
	    case SWIG_TOKEN_LONG:      Printf(R,"longint(%s)",V); break;
	    case SWIG_TOKEN_ULONG:     Printf(R,"longword(%s)",V); break;
	    case SWIG_TOKEN_LONGLONG:  Printf(R,"int64(%s)",V); break;
	    case SWIG_TOKEN_ULONGLONG: Printf(R,"qword(%s)",V); break;
	  }
	  
	}  
 	else if ((token == SWIG_TOKEN_FLOAT) || (token == SWIG_TOKEN_DOUBLE)) {
	  char *c = Char(Scanner_text(scan));
	  String *V = NewString(c);
          if ((strcmp(c,"L")>0) || (strcmp(c,"l")>0))
	    token = SWIG_TOKEN_LONG_DOUBLE;
	  
	  // suffix. no suffix means double
          Replace(V,"L","", DOH_REPLACE_NUMBER_END ); //long double
          Replace(V,"l","", DOH_REPLACE_NUMBER_END ); //long double
          Replace(V,"F","", DOH_REPLACE_NUMBER_END ); //float
          Replace(V,"f","", DOH_REPLACE_NUMBER_END ); //float
	  
	  switch (token) {
	    case SWIG_TOKEN_FLOAT:       Printf(R,"single(%s)",V); break;
	    case SWIG_TOKEN_DOUBLE:      Printf(R,"double(%s)",V); break;
	    case SWIG_TOKEN_LONG_DOUBLE:      Printf(R,"extended(%s)",V); break;
	  }
	}  
 	else if (token == SWIG_TOKEN_ID) {
	  char *c = Char(Scanner_text(scan));
	  String *name = NewString(c);
	  Node *itemnode = findUpNodeByName(n, c);
	  String *pasname = getPascalName(itemnode);
          Append(R, pasname);
          Delete(name);
          Delete(pasname);
	}  
 	else 
	  switch (token) {
	    case SWIG_TOKEN_EQUALTO: /* == */ Printf(R,"="); break;
	    case SWIG_TOKEN_NOTEQUAL: /* != */ Printf(R,"<>"); break;
	    case SWIG_TOKEN_AND: /* & */ Printf(R," and "); break;
	    case SWIG_TOKEN_LAND: /* && */ Printf(R," and "); break;
	    case SWIG_TOKEN_OR: /* | */ Printf(R," or "); break;
	    case SWIG_TOKEN_LOR: /* || */ Printf(R," or "); break;
	    case SWIG_TOKEN_XOR: /* ^ */ Printf(R," xor "); break;
	    case SWIG_TOKEN_NOT: /* ~ */ Printf(R," not "); break;
	    case SWIG_TOKEN_LNOT: /* ! */ Printf(R," not "); break;
	    case SWIG_TOKEN_MODULO: /* % */ Printf(R," mod "); break;
	    case SWIG_TOKEN_LSHIFT: /* << */ Printf(R," shl "); break;
	    case SWIG_TOKEN_RSHIFT: /* >> */ Printf(R," shr "); break;
	    case SWIG_TOKEN_PLUSPLUS: /* ++ */ Printf(R," inc "); break; // !!! needs rearranging from L ++ R to inc(L,R)
	    case SWIG_TOKEN_MINUSMINUS: /* -- */ Printf(R," dec "); break; // !!! needs rearranging from L ++ R to dec(L,R)
	    case SWIG_TOKEN_COMMENT: /*  */ Printf(R," (* "); break; // needs closing *) for sub expr
	    
	    case SWIG_TOKEN_DIVIDE: /* / */ 
	      if (Equal(type,"int") || Equal(type,"long") || Equal(type,"long long") || Equal(type,"byte"))
		Printf(R," div "); 
	      else Printf(R," / ");
	      break;
	    default:   
	      char *c = Char(Scanner_text(scan));
	      String *something = NewString(c);
	      Append(R, something);
	      Delete(something);
	}  
      } //while
      return R;
    }

    int generateNumExprConstant(Node *n, String *name, String *value) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
        String *Rvalue = convertNumExpression(n, value);
	Printf(pasraw_intf.f, "   %s = %s ;\n", name, Rvalue);
      TRACE_FUNC_EXIT;
      return R;
    }

    int generateStringConstant(Node *n, String *name, String *value) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;

      Printf(pasraw_intf.f, "   %s = '%s';\n", name, value);

      TRACE_FUNC_EXIT;
      return R;
    }

    int generateEnumItem(Node *n){
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      
      LOG_NODE_DEBUG(n)
      
      //String *enumvalueex = Copy(Getattr(n, "enumvalueex"));
      String *enumvalue = Copy(Getattr(n, "enumvalue"));
      //String *name = Copy(Getattr(n, "sym:name"));
      String *type = Copy(Getattr(n, "type"));
      String *comma;
      if (GetFlag(n,"firstenumitem")==1) comma = NewString(""); else comma = NewString(",");

      String *pasname = getPascalName(n);

      if (hasContent(enumvalue)) {
	String *Rvalue = convertNumExpression(n, enumvalue);
        //Printf(pasraw_intf.f, "%s %s=%s( %s )", comma, pasname, type, Rvalue);
	Printf(pasraw_intf.f, "%s %s=%s", comma, pasname, Rvalue);
      }
      else
        Printf(pasraw_intf.f, "%s %s", comma, pasname);
	
      DELETE(comma);
      TRACE_FUNC_EXIT;
      return R;
    }

    void emitEnumeration(File *file, String *name, Node *n) {
      TRACE_FUNC_ENTR;
      Printf(file, "%s = {", name);
      int i;
      bool gencomma = false;
      int max = aToL(Getattr(n, "max"));
      Hash *items = Getattr(n, "items");
      for (i = 0; i <= max; i++) {
        if (gencomma) {
          Printf(file, ",");
        }
        Printf(file, "\n");
        gencomma = true;
        char numstr[15];
        sprintf(numstr, "%d", i);
        String *name = Getattr(items, numstr);
        if (name != NIL) {
          Printv(file, name, NIL);
        } else {
          Printf(file, "Dummy%d", i);
        }
      }
      Printf(file, "\n};\n");
      TRACE_FUNC_EXIT;
    }

  /* -----------------------------------------------------------------------
   * constantWrapper()
   * Used for wrapping constants - #define or %constant.
   * Also for inline initialised const static primitive type member variables (short, int, double, enums etc).
   * External C calls or Pascal constants are generated for these. %pascalconst(N), N can be 
   * 0 - external function calls C wrapper that gets constant from C kingdom.
   * 1 - constant value from C kingdom is used to initialise the Pascal variable or read-only property of class. Not done.
   * 2 - simple integer or string constants generated in Pascal kingdom. 
   * However, if the %pascalconstvalue feature is used, it overrides all other ways to generate the initialisation.
   * Also note that this method might be called for wrapping enum items (when the enum is using %javaconst(0)).
   * ------------------------------------------------------------------------ */


    virtual int constantWrapper(Node *n) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      
      LOG_NODE_DEBUG(n);
      
      // const or enum
      bool is_enum_item = (Cmp(nodeType(n), "enumitem") == 0);
      // The %pascalconst feature determines how the constant value is obtained
      int const_feature_flag = GetInt(n, "feature:pascal:const");

      if (is_enum_item) 
        generateEnumItem(n);
      else 
      switch (const_feature_flag) {
        case 0: R = generateConstantCall(n); break;
        case 1: R = generateConstantVar(n); break;
        case 2: R = generateConstantPascal(n); break;
      }
    
      //Insert(GlobalConstantList, Len(GlobalConstantList)+1, n);
    
      TRACE_FUNC_EXIT;
      return R;
    }


#if 1
    // enumerations are handled like constant definitions
    /* -----------------------------------------------------------------------------
    * enumDeclaration()
    * ----------------------------------------------------------------------------- */

    void fixUnnamed(String *s)
    {

      Replace(s,"$","_", DOH_REPLACE_ANY);

    }

    virtual int enumDeclaration(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);
      /*
        | name         - "Enm"
	| sym:symtab   - 0x7ffff7f3b410
	| sym:name     - "Enm"
	| enumtype     - "Enm"
	| type         - "enum Enm"
	| sym:overname - "__SWIG_0"
	| enumkey      - "enum"
	| feature:pascal:const - "2"
      */
      int R = SWIG_OK;

      String *symname = getPascalName(n);
      //String *symname = Getattr(n, "sym:name");
      //String *unnamed = Getattr(n, "unnamed");
      String *name = Getattr(n, "name");
      String *tdname = Getattr(n, "tdname");  

      int const_feature_flag = GetInt(n, "feature:pascal:const");
      
      //enumerationStart(symname);

      enum_begin = 0;

      String * p = 0;

      if (hasContent(name))
        p = name;
      else if (hasContent(symname))
        p = symname;
      else if (hasContent(tdname))
        p = tdname;

      Replace(p,"::","_", DOH_REPLACE_ANY);

      fixUnnamed(p);

      p = getPascalName(n);

      if (p) {
        pasraw_intf.enterBlock(blocktype);
        Printf(pasraw_intf.f,"  %s = (", p);
	enumeration_name = p;
	R = Language::enumDeclaration(n);
	enumeration_name = 0;
	Printf(pasraw_intf.f," );\n", p);
      }

      Delete(symname);
      TRACE_FUNC_EXIT;
      return R;
    }
#endif

    /* -----------------------------------------------------------------------------
    * enumvalueDeclaration()
    * ----------------------------------------------------------------------------- */

    virtual int enumvalueDeclaration(Node *n) {
      TRACE_FUNC_ENTR;
      int R = SWIG_OK;
      
      LOG_NODE_DEBUG(n);

      enum_wrap_flag = true;
      R = generateEnumItem(n);
      enum_wrap_flag = false;

      TRACE_FUNC_EXIT;
      return R;
    }

    /* -----------------------------------------------------------------------------
    * pragmaDirective()
    *
    * Valid Pragmas:
    * imclassbase            - base (extends) for the intermediary class
    * imclassclassmodifiers  - class modifiers for the intermediary class
    * imclasscode            - text (Pascal code) is copied verbatim to the intermediary class
    * imclassimports         - import statements for the intermediary class
    * imclassinterfaces      - interface (implements) for the intermediary class
    *
    * modulebase              - base (extends) for the module cl
    * moduleclassmodifiers    - class modifiers for the module class
    * modulecode              - text (Pascal code) is copied verbatim to the module class
    * moduleimports           - import statements for the module class
    * moduleinterfaces        - interface (implements) for the module class
    *
    * ----------------------------------------------------------------------------- */

    virtual int pragmaDirective(Node *n) {
      if (!ImportMode) {
        String *lang = Getattr(n, "lang");
        String *code = Getattr(n, "name");
        String *value = Getattr(n, "value");

        if (Strcmp(lang, "freepascal") == 0) {

          String *strvalue = NewString(value);
          Replaceall(strvalue, "\\\"", "\"");
          /*
          bool isEnumItem = Strcmp(code, "enumitem") == 0;
          bool isSetItem  = Strcmp(code, "setitem")  == 0;
          */
          if (Strcmp(code, "imclassbase") == 0) {
            Delete(pasraw_baseclass);
            pasraw_baseclass = Copy(strvalue);
          } else if (Strcmp(code, "imclassclassmodifiers") == 0) {
            Delete(pasraw_class_modifiers);
            pasraw_class_modifiers = Copy(strvalue);
          } else if (Strcmp(code, "imclasscode") == 0) {
            Printf(pasraw_intf.f, "%s\n", strvalue);
          } else if (Strcmp(code, "imclassimports") == 0) {
            Delete(pasraw_imports);
            pasraw_imports = Copy(strvalue);
          } else if (Strcmp(code, "imclassinterfaces") == 0) {
            Delete(pasraw_interfaces);
            pasraw_interfaces = Copy(strvalue);
          } else if (Strcmp(code, "modulebase") == 0) {
            Delete(module_baseclass);
            module_baseclass = Copy(strvalue);
          } else if (Strcmp(code, "moduleclassmodifiers") == 0) {
            Delete(paswrap_modifiers);
            paswrap_modifiers = Copy(strvalue);
          } else if (Strcmp(code, "modulecode") == 0) {
            Printf(paswrap_impl.f, "%s\n", strvalue);
          } else if (Strcmp(code, "moduleimports") == 0) {
            Delete(module_imports);
            module_imports = Copy(strvalue);
          } else if (Strcmp(code, "moduleinterfaces") == 0) {
            Delete(module_interfaces);
            module_interfaces = Copy(strvalue);
          } else if (Strcmp(code, "unsafe") == 0) {
            unsafe_module = true;
          } else if (Strcmp(code, "library") == 0) {
            if (targetlibrary != NULL) {
              Delete(targetlibrary);
            }
            targetlibrary = Copy(strvalue);
          } else if (Strcmp(code, "enumitem") == 0) {
          } else if (Strcmp(code, "constset") == 0) {
          } else if (Strcmp(code, "constint") == 0) {
          } else if (Strcmp(code, "makesetofenum") == 0) {
            paswrap_intf.enterBlock(blocktype);
            Printf(paswrap_intf.f, "%sSet = SET OF %s;\n", value, value);

          } else if (Strcmp(code, "WRAPDLLNAME") == 0 && value) {
            //  wrapdllname = Copy(value);

          } else if (Strcmp(code, "DLLNAME") == 0 && value) {
            dllname = Copy(value);
          } else if (Strcmp(code, "WRAPSOURCENAME") == 0 && value) {
            //  wrapsourcename = Copy(value);   
          } else if (Strcmp(code, "CALLCONV") == 0 && value) {
            callconv = Copy(value);   
          } else if (Strcmp(code, "USES") == 0 && value) {
            intf_uses = Copy(value);   
          } else if (Strcmp(code, "NAMESPACE") == 0 && value) {
            m_namespace = Copy(value);   
          }  else if (Strcmp(code, "nowrap_function") == 0) {
            // todo

          } else {
            Swig_warning(WARN_FREEPASCAL_UNKNOWN_PRAGMA, input_file, line_number, "Unrecognized pragma <%s>.\n", code);
          }
          Delete(strvalue);
        }
      }
      return Language::pragmaDirective(n);
    }

    void Setfeature(Node *n, const char *feature, const String *value, bool warn = false) {
      //printf("tag feature <%s> with value <%s>\n", feature, Char(value));
      String *attr = NewStringf("feature:%s", feature);
      if ((Setattr(n, attr, value) != 0) && warn) {
        Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Feature <%s> of %s did already exist.\n", feature, Getattr(n, "name"));
      }

      Delete(attr);
    }

    String *Getfeature(Node *n, const char *feature) {
      //printf("retrieve feature <%s> with value <%s>\n", feature, Char(value));
      String *attr = NewStringf("feature:%s", feature);
      String *result = Getattr(n, attr);
      Delete(attr);
      return result;
    }

    bool convertInt(long in, long &out, const String *mode) {
      if ((mode == NIL) || (Strcmp(mode, "int:int") == 0) || (Strcmp(mode, "set:set") == 0)) {
        out = in;
        return true;
      } else if (Strcmp(mode, "set:int") == 0) {
        return log2(in, out);
      } else if (Strcmp(mode, "int:set") == 0) {
        out = 1L << in;
        return unsigned (in) < (sizeof(out) * 8);
      } else {
        Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Unknown integer conversion method <%s>.\n", mode);
        return false;
      }
    }

    void collectEnumerations(Hash *enums, Node *n) {
      Node *child = firstChild(n);
      while (child != NIL) {
        String *name = Getattr(child, "name");
        const bool isConstant = Strcmp(nodeType(child), "constant") == 0;
        const bool isEnumItem = Strcmp(nodeType(child), "enumitem") == 0;
        if (isConstant || isEnumItem) {
          //printf("%s%s name %s\n", isConstant?"constant":"", isEnumItem?"enumitem":"", Char(name));
          {
            String *pasname = Getfeature(child, "freepascal:enumitem:name");
            String *pasenum = Getfeature(child, "freepascal:enumitem:enum");
            String *conv = Getfeature(child, "freepascal:enumitem:conv");

            if (pasenum != NIL) {
              //printf("pasenum %s\n", Char(pasenum));
              if (pasname == NIL) {
                pasname = name;
              }

              long max = -1;
              Hash *items;
              Hash *enumnode = Getattr(enums, pasenum);
              if (enumnode == NIL) {
                enumnode = NewHash();
                items = NewHash();
                Setattr(enumnode, "items", items);
                Setattr(enums, pasenum, enumnode);
              } else {
                String *maxstr = Getattr(enumnode, "max");
                if (maxstr != NIL) {
                  max = aToL(maxstr);
                }
                items = Getattr(enumnode, "items");
              }
              long numvalue;
              String *value = Getattr(child, "value");
              //printf("value: %s\n", Char(value));
              if ((value == NIL) || (!strToL(value, numvalue))) {
                value = Getattr(child, "enumvalue");
                if ((value == NIL) || (!evalExpr(value, numvalue))) {
                  numvalue = getConstNumeric(child);
                }
                //printf("constnumeric: %s\n", Char(value));
              }
              Setattr(constant_values, name, NewStringf("%d", numvalue));
              if (convertInt(numvalue, numvalue, conv)) {
                String *newvalue = NewStringf("%d", numvalue);
                String *oldname = Getattr(items, newvalue);
                if (oldname != NIL) {
                  Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "The value <%s> is already assigned to <%s>.\n", value, oldname);
                }
                //printf("items %lx, set %s = %s\n", (long) items, Char(newvalue), Char(pasname));
                Setattr(items, newvalue, pasname);
                if (max < numvalue) {
                  max = numvalue;
                }
                Setattr(enumnode, "max", NewStringf("%d", max));
              }
            }
          }
        }

        collectEnumerations(enums, child);
        child = nextSibling(child);
      }
    }

  /* 
   * Collect templates. Probably for automatic instantiation for all major types, like %template does. If not too late.
   * If too late, it can be used to spool %template defs and exit. These can be manually added to .i
   */
    void collectTemplates(Hash *templs, Node *n) {
      Node *child = firstChild(n);
      while (child != NIL) {
        //String *name = Getattr(child, "name");
        const bool isTemplate = Strcmp(nodeType(child), "template") == 0;
        if (isTemplate) {
 	   LOG_NODE_DEBUG(child);
	   appendChild(templs, child);
        }else // skip nested templates
           collectTemplates(templs, child);
	   
        child = nextSibling(child);
      }
    }



#if 0
// TODO: May be just for Modula. remove later. 
    enum const_pragma_type { cpt_none, cpt_constint, cpt_constset, cpt_enumitem };

    struct const_id_pattern {
      String *prefix, *parentEnum;
    };

    void tagConstants(Node *first, String *parentEnum, const const_id_pattern & pat, const String *pragma, List *convdesc) {
      Node *n = first;
      while (n != NIL) {
        String *name = getQualifiedName(n); //?
        bool isConstant = Strcmp(nodeType(n), "constant") == 0;
        bool isEnumItem = Strcmp(nodeType(n), "enumitem") == 0;
        if ((isConstant || isEnumItem) && ((pat.prefix == NIL) 
	 || (hasPrefix(name, pat.prefix))) && ((pat.parentEnum == NIL) 
	 || ((parentEnum != NIL) && (Strcmp(pat.parentEnum, parentEnum) == 0)))) 
	 {
            //printf("tag %s\n", Char(name));
            String *srctype = Getitem(convdesc, 1);
            String *relationstr = Getitem(convdesc, 3);
            List *relationdesc = Split(relationstr, ',', 2);

            // transform name from C to FreePascal style
            String *srcstyle = NIL;
            String *newprefix = NIL;
            {
              //printf("name conversion <%s>\n", Char(Getitem(convdesc,2)));
              List *namedesc = Split(Getitem(convdesc, 2), ',', INT_MAX);
              Iterator nameit = First(namedesc);
              for (; nameit.item != NIL; nameit = Next(nameit)) {
                List *nameassign = Split(nameit.item, '=', 2);
                String *tag = Getitem(nameassign, 0);
                String *data = Getitem(nameassign, 1);
                //printf("name conv <%s> = <%s>\n", Char(tag), Char(data));
                if (Strcmp(tag, "srcstyle") == 0) {
                  srcstyle = Copy(data);
                } else if (Strcmp(tag, "prefix") == 0) {
                  newprefix = Copy(data);
                } else {
                  Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Unknown name conversion tag <%s> with value <%s>.\n", tag, data);
                }
                Delete(nameassign);
              };
              Delete(namedesc);
            }
            
	    String *stem = Copy(name);
            if (pat.prefix) {
              //printf("pat.prefix %s for %s\n", Char(pat.prefix), Char(name));
              //stem += Len(pat.prefix);
	      Replace(stem, pat.prefix, DOH_REPLACE_FIRST);
            }
            String *newname;
            if (Strcmp(srcstyle, "underscore") == 0) {
              if (newprefix != NIL) {
                String *newstem = nameToPascal(n, stem, true);
                newname = NewStringf("%s%s", newprefix, newstem);
                Delete(newstem);
              } else {
                newname = nameToPascal(n, stem, true);
              }
            } else {
              if (srcstyle != NIL) {
                Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Unknown C identifier style <%s>.\n", srcstyle);
              }
              newname = Copy(name);
            }

            if (Strcmp(pragma, "enumitem") == 0) {
              if (Len(relationdesc) != 1) {
                Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Expected <enumeration>, got <%s>.\n", relationstr);
              }
              Setfeature(n, "freepascal:enumitem:name", newname, true);
              Setfeature(n, "freepascal:enumitem:enum", relationstr, true);
              Setfeature(n, "freepascal:enumitem:conv", NewStringf("%s:int", srctype), true);
            } else if (Strcmp(pragma, "constint") == 0) {
              if (Len(relationdesc) != 1) {
                Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Expected <ordinal type>, got <%s>.\n", relationstr);
              }
              Setfeature(n, "freepascal:constint:name", newname, true);
              Setfeature(n, "freepascal:constint:type", Getitem(relationdesc, 0), true);
              Setfeature(n, "freepascal:constint:conv", NewStringf("%s:int", srctype), true);
            } else if (Strcmp(pragma, "constset") == 0) {
              if (Len(relationdesc) != 2) {
                Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Expected <set type,base type>, got <%s>.\n", relationstr);
              }
              String *settype = Getitem(relationdesc, 0);
              Setfeature(n, "freepascal:constset:name", newname, true);
              //Setfeature(n,"freepascal:constset:type",settype,true);
              Setfeature(n, "freepascal:constset:set", settype, true);
              Setfeature(n, "freepascal:constset:base", Getitem(relationdesc, 1), true);
              Setfeature(n, "freepascal:constset:conv", NewStringf("%s:set", srctype), true);
            }

            Delete(newname);
            Delete(relationdesc);
        }

        if (Strcmp(nodeType(n), "enum") == 0) {
          //printf("explore enum %s, qualification %s\n", Char(name), Char(Swig_symbol_qualified(n)));
          tagConstants(firstChild(n), name, pat, pragma, convdesc);
        } else {
          tagConstants(firstChild(n), NIL, pat, pragma, convdesc);
        }
        n = nextSibling(n);
      }
    }
#endif

#if 0
// TODO: remove later
    void scanForConstPragmas(Node *n) {
      TRACE_FUNC_ENTR;
      Node *child = firstChild(n);
      while (child != NIL) {
        LOG_NODE_DEBUG(child);
        const String *type = nodeType(child);
        if (Strcmp(type, "pragma") == 0) {
          const String *lang = Getattr(child, "lang");
          const String *code = Getattr(child, "name");
          String *value = Getattr(child, "value");

          if (Strcmp(lang, "freepascal") == 0) {
            const_pragma_type cpt = cpt_none;
            if (Strcmp(code, "constint") == 0) {
              cpt = cpt_constint;
            } else if (Strcmp(code, "constset") == 0) {
              cpt = cpt_constset;
            } else if (Strcmp(code, "enumitem") == 0) {
              cpt = cpt_enumitem;
            }
            if (cpt != cpt_none) {
              const_id_pattern pat = { NIL, NIL };

              List *convdesc = Split(value, ';', 4);
              List *patterndesc = Split(Getitem(convdesc, 0), ',', INT_MAX);
              Iterator patternit;
              for (patternit = First(patterndesc); patternit.item != NIL; patternit = Next(patternit)) {
                List *patternassign = Split(patternit.item, '=', 2);
                String *tag = Getitem(patternassign, 0);
                String *data = Getitem(patternassign, 1);
                if (Strcmp(tag, "prefix") == 0) {
                  pat.prefix = Copy(data);
                } else if (Strcmp(tag, "enum") == 0) {
                  pat.parentEnum = Copy(data);
                } else {
                  Swig_warning(WARN_FREEPASCAL_BAD_ENUMERATION, input_file, line_number, "Unknown identification tag <%s> with value <%s>.\n", tag, data);
                }
                Delete(patternassign);
              }
              tagConstants(child, NIL, pat, code, convdesc);

              Delete(patterndesc);
            }
          }
        }
        scanForConstPragmas(child);
        child = nextSibling(child);
      }
      TRACE_FUNC_EXIT;
    }
#endif

   // void emitProxyClassMethods(Node * /*n*/) {
#if 0
     TRACE_FUNC_ENTR;
      enum access_privilege { acc_public, acc_protected, acc_private };
      int max_acc = acc_public;

      const char *acc_name[3] = { "public", "protected", "private" };
      String *methods[3];
      int acc;
      for (acc = acc_public; acc <= acc_private; acc++) {
        String *methodattr = NewStringf("freepascal:method:%s", acc_name[acc]);
        methods[acc] = Getattr(n, methodattr);
        Delete(methodattr);
        max_acc = max_acc > acc ? max_acc : acc;
      }
      /* Determine the name of the base class */
      String *baseclassname = NewString("");
      {
        List *baselist = Getattr(n, "bases");
        if (baselist) {
          /* Look for the first (principal?) base class -
          Pascal does not support multiple inheritance */
          Iterator base = First(baselist);
          Append(baseclassname, Getattr(base.item, "sym:name"));
          base = Next(base);
          if (base.item != NIL) {
            Swig_warning(WARN_FREEPASCAL_MULTIPLE_INHERITANCE, input_file,
              line_number,
              "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Pascal.\n",
              proxy_class_name, Getattr(base.item, "name"));
          }
        }
      }

      /* the private class of the base class and only this
      need a pointer to the C++ object */
      bool need_private = !hasContent(baseclassname);
      max_acc = need_private ? acc_private : max_acc;

      /* Declare C++ object as abstract pointer in Pascal */

      if (hasContent(baseclassname)) {
        pasraw_intf.enterBlock(blocktype);
        Printf(pasraw_intf.f, "%s = %s;\n", proxy_class_name, baseclassname);
      } else {
        pasraw_intf.enterBlock(blocktype);
        Printf(pasraw_intf.f, "%s <: ADDRESS;\n", proxy_class_name);
        pasraw_impl.enterBlock(revelation);
        Printf(pasraw_impl.f, "%s = UNTRACED BRANDED REF RECORD (*Dummy*) END;\n", proxy_class_name);
      }

      String *superclass;
      paswrap_intf.enterBlock(blocktype);
      if (hasContent(methods[acc_public])) {
        superclass = NewStringf("%sPublic", proxy_class_name);
      } else if (hasContent(baseclassname)) {
        superclass = Copy(baseclassname);
      } else {
        superclass = NewString("TObject");
      }
      Printf(paswrap_intf.f, "%s <: %s;\n", proxy_class_name, superclass);
      Delete(superclass);

      {
        static const char *acc_passuffix[] = { "Public", "Protected", "Private" };
        int acc;
        for (acc = acc_public; acc <= acc_private; acc++) {
          bool process_private = (acc == acc_private) && need_private;
          if (hasContent(methods[acc]) || process_private) {
            String *subclass = NewStringf("%s%s", proxy_class_name, acc_passuffix[acc]);
            /*
            paswrap_intf.enterBlock(revelation);
            Printf(paswrap_intf.f, "%s <: %s;\n", proxy_class_name, subclass);
            */
            if (acc == max_acc) {
              paswrap_intf.enterBlock(revelation);
              Printf(paswrap_intf.f, "%s =\n", proxy_class_name);
            } else {
              paswrap_intf.enterBlock(blocktype);
              Printf(paswrap_intf.f, "%s =\n", subclass);
            }
            Printf(paswrap_intf.f, "%s BRANDED OBJECT\n", baseclassname);
            if (process_private) {
              Setattr(paswrap_intf.import, pasraw_module_name, "");
              Printf(paswrap_intf.f, "cxxObj:%s.%s;\n", pasraw_module_name, proxy_class_name);
            }
            if (hasContent(methods[acc])) {
              Printf(paswrap_intf.f, "METHODS\n%s", methods[acc]);
            }
            if (acc == max_acc) {
              String *overrides = Getattr(n, "freepascal:override");
              Printf(paswrap_intf.f, "OVERRIDES\n%s", overrides);
            }
            Printf(paswrap_intf.f, "END;\n");
            Delete(baseclassname);
            baseclassname = subclass;

          }
        }
      }

      Delete(methods[acc_public]);
      Delete(methods[acc_protected]);
      Delete(methods[acc_private]);

     TRACE_FUNC_EXIT;
#endif
   // }


    /* -----------------------------------------------------------------------------
    * emitProxyClassDefAndCPPCasts()
    * ----------------------------------------------------------------------------- */

    void emitProxyClassDefAndCPPCasts(Node *n) {
      LOG_DEBUG("start");

      String *baseclass = NULL;
      String *c_baseclassname = NULL;
      //String *name = Getattr(n, "name");
      String *symName = Getattr(n, "sym:name"); //name of original class (how it is in C++)
      //String *rawName = Getattr(n, "raw:name"); //name of C pointer
      //String *classDeclarationName = Getattr(n, "wrap:name");
      //String *proxyName = Getattr(n, "wrap:name"); //name of Pascal proxy wrapper
      
      //String *pure_interfaces = NewString("");
        
        
      /* Deal with inheritance */
      List *baselist = Getattr(n, "bases");
      if (baselist != NIL) {
        Iterator base = First(baselist);
        c_baseclassname = Getattr(base.item, "name");
        baseclass = Copy(getProxyName(c_baseclassname));
        base = Next(base);
        if (base.item != NIL) {
          Swig_warning(WARN_FREEPASCAL_MULTIPLE_INHERITANCE, input_file,
            line_number,
            "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Pascal.\n",
            symName, Getattr(base.item, "name"));
        }
      }

      bool derived = baseclass && getProxyName(c_baseclassname);
      //printf("baseclass:%s\n",Char(baseclass));
      if (baseclass) { 
      // Inheritance from pure Pascal classes
      //fprintf(stderr, "classDeclarationName:%s\n",Char(classDeclarationName));
        const String *pure_baseclass = typemapLookup(n,"pasbase", baseclass, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF);
        if (hasContent(pure_baseclass) && hasContent(baseclass)) {
        Swig_warning(WARN_FREEPASCAL_MULTIPLE_INHERITANCE, input_file,
          line_number,
          "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Pascal.\n", symName, pure_baseclass);
        }
      } else
          baseclass = NewString("");

      // Pure Pascal interfaces
      /*const String *pure_interfaces = typemapLookup(n,derived ? "pasinterfaces_derived" : "pasinterfaces",
      classDeclarationName, WARN_NONE);*/

      Printv(proxy_class_def, "",
        derived ? typemapLookup(n,"pasclassdef_derived", symName, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF) :  
                  typemapLookup(n,"pasclassdef_base", symName, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF),
        NIL);

#if 0

      if (!derived)
        Printv(proxy_class_code,
        typemapLookup(n,"pasclass_iunknown", symName, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF)
        ,NIL);


      //     Replace(proxy_class_def,"$pasclassname", proxy_class_name, DOH_REPLACE_ANY);

      Printv(proxy_class_def, "\n// ",proxyName, NIL);
          Printv(proxy_class_def, "\n  //",
             derived ? " is derived ": "is not derived ",
             " pure_baseclass:", pure_baseclass,
             " pure_interfaces:", pure_interfaces,
             " baseclass:",baseclass,
             " classDeclarationName:",classDeclarationName,
             "\n\n", NIL);
#endif

    //bool is_pure_baseclass = hasContent(pure_baseclass);
    //bool is_pure_interfaces = hasContent(pure_interfaces);
    
#if 0
    // Start writing the proxy class
    Printv(proxy_class_def, 
        typemapLookup(n,"pasclassmodifiers", classDeclarationName, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF,n),  // Class modifiers
        "  $pasclassname = class ", NIL);
      
    if (derived || is_pure_baseclass || is_pure_interfaces ) {
      Printv(proxy_class_def, "(", NIL);
      
      if (derived) 
        Printv(proxy_class_def, baseclass,NIL);
      
      if (is_pure_baseclass) 
        Printv(proxy_class_def, pure_baseclass,NIL);

      if (is_pure_interfaces) 
        Printv(proxy_class_def, pure_interfaces,NIL);

        Printf(proxy_class_def, ")\n");
      } 
      
      //Printf(proxy_class_def,  "\n  private\n");
      
      if (!derived)
      Printv(proxy_class_def,  
        "  FCObjPtr : pointer;\n",  // Member variables for memory handling
        "  FOwnCObjPtr : boolean;\n",
        NIL);
          
        if derived ? "" : "  protected\n FOwnCObjPtr : boolean;\n", 
        "  ", typemapLookup(n,"pasptrconstructormodifiers", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF,n),  // pointer constructor modifiers
        " constructor $pasclassname(IntPtr cPtr, bool cMemoryOwn);",  // Constructor used for wrapping pointers
        //derived ?
        //": base($imclassname.$pasclassnameTo$baseclass(cPtr), cMemoryOwn) {\n"
        //: "{\n    swigCMemOwn = cMemoryOwn;\n", "    swigCPtr = cPtr;\n", "  }\n"
        "");
#endif

      if (!have_default_constructor_flag && !derived) {  // All proxy classes need a constructor
        Printv(proxy_class_def, typemapLookup(n,"pasdefault_constructor_def", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF), NIL);
        Printv(proxy_class_code, typemapLookup(n,"pasdefault_constructor_code", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF), NIL);
      }
      Printv(proxy_class_def, proxy_class_constructor_def, NIL);

      // C++ destructor is wrapped by the Free method
      // Note that the method name is specified in a typemap attribute called methodname
      String *destruct = NewString("");
      const String *tm_def = NULL;
      const String *tm_code = NULL;
      Node *attributes = NewHash();
      String *destruct_methodname = NULL;
      
      tm_def = typemapLookup(n,"pasdestruct_def", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF);
      //Printv(proxy_class_def,"  // tm_def:",tm_def,"\n", NIL);
      
      if (derived) {
        tm_code = typemapLookup(n,"pasdestruct_derived_code", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF, attributes);
        destruct_methodname = Getattr(attributes, "tmap:pasdestruct_derived_code:methodname");
      } else {
        tm_code = typemapLookup(n,"pasdestruct_code", symName, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF, attributes);
        destruct_methodname = Getattr(attributes, "tmap:pasdestruct_code:methodname");
      }
      if (!hasContent(destruct_methodname)) {
        Printf(proxy_class_def,"  // no destructor");
        destruct_methodname = NewString("Destroy");
      //  Swig_error(input_file, line_number, "No methodname attribute defined in pasdestruct%s typemap for %s\n", (derived ? "_derived" : ""), proxy_class_name);
      }
      // Emit the Finalize and Dispose methods
      if (hasContent(tm_code)) {
        // Finalize method
        if (hasContent(destructor_call)) {
          //Printv(proxy_class_def, typemapLookup(n,"pasfinalize", classDeclarationName, WARN_NONE), NIL);
        }
        // Dispose method
        Printv(destruct, tm_code, NIL);
        if (hasContent(destructor_call))
          Replaceall(destruct, "$imcall", destructor_call);
        else
          Replaceall(destruct, "$imcall", "throw new MethodAccessException(\"C++ destructor does not have public access\")");

        if (hasContent(destruct)) {
          //Printv(proxy_class_def, "\n  public ", derived ? "override" : "virtual", " void ", destruct_methodname, "() ", destruct, "\n", NIL);
          Printv(proxy_class_def, tm_def, NIL);
          Printv(proxy_class_code, destruct, NIL);
        }

      }
      else
          Printf(proxy_class_code,"  // no destructor");
          
      Delete(attributes);
      Delete(destruct);

      // Emit various other methods
      Printf(proxy_class_def, "\n  //various other methods\n");
      Printv(proxy_class_def, 
        derived?"":typemapLookup(n,"pasgetcptr_intf", symName, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF),  // getCPtr method
        typemapLookup(n,"pascode", symName, WARN_NONE),  // extra Pascal code
        NIL);

      //emitProxyClassMethods(n);

      Printv(proxy_class_def, typemapLookup(n,"pasclassdef_end", symName, WARN_FREEPASCAL_TYPEMAP_UNDEF),NIL);

      // Substitute various strings into the above template
      Replaceall(proxy_class_def, "$pasclassname", proxy_class_name);
      Replaceall(proxy_class_code, "$pasclassname", proxy_class_name);

      Replaceall(proxy_class_def, "$baseclass", baseclass);
      Replaceall(proxy_class_code, "$baseclass", baseclass);

      Replaceall(proxy_class_def, "$imclassname", pasraw_module_name);
      Replaceall(proxy_class_code, "$imclassname", pasraw_module_name);
      
      //Replaceall(proxy_class_def, "$CObjPtrTypeName", CObjPtrTypeName);
      //Replaceall(proxy_class_code, "$CObjPtrTypeName", CObjPtrTypeName);
      

#if 0
      // Add code to do C++ casting to base class (only for classes in an inheritance hierarchy)
      if (derived) {
        Printv(pasraw_cppcasts_code, "\n  [DllImport(\"", paswrap_name, "\", EntryPoint=\"FreePascal_", proxy_class_name, "To", baseclass, "\")]\n", NIL);
        Printv(pasraw_cppcasts_code, "  public static extern IntPtr ", "$pasclassnameTo$baseclass(IntPtr objectRef);\n", NIL);

        Replaceall(pasraw_cppcasts_code, "$pasclassname", proxy_class_name);
        Replaceall(pasraw_cppcasts_code, "$baseclass", baseclass);

        Printv(upcasts_code,
          "SWIGEXPORT long FreePascal_$imclazznameTo$imbaseclass",
          "(long objectRef) {\n",
          "    long baseptr = 0;\n" "    *($cbaseclass **)&baseptr = *($cclass **)&objectRef;\n" "    return baseptr;\n" "}\n", "\n", NIL);

        Replaceall(upcasts_code, "$imbaseclass", baseclass);
        Replaceall(upcasts_code, "$cbaseclass", c_baseclass);
        Replaceall(upcasts_code, "$imclazzname", proxy_class_name);
        Replaceall(upcasts_code, "$cclass", c_classname);
      }
#endif  
      Delete(baseclass);

     TRACE_FUNC_EXIT;

    }

    /* ----------------------------------------------------------------------
    * getAttrString()
    *
    * If necessary create and return the string
    * associated with a certain attribute of 'n'.
    * ---------------------------------------------------------------------- */

    String *getAttrString(Node *n, const char *attr) {
      String *str = Getattr(n, attr);
      if (str == NIL) {
        str = NewString("");
        Setattr(n, attr, str);
      }
      return str;
    }

    /* ----------------------------------------------------------------------
    * getMethodDeclarations()
    *
    * If necessary create and return the handle
    * where the methods of the current access can be written to.
    * 'n' must be a member of a struct or a class.
    * ---------------------------------------------------------------------- */

    String *getMethodDeclarations(Node *n) {
      String *acc_str = Getattr(n, "access");
      String *methodattr;
      if (acc_str == NIL) {
        methodattr = NewString("freepascal:method:public");
      } else {
        methodattr = NewStringf("freepascal:method:%s", acc_str);
      }
      String *methods = getAttrString(parentNode(n), Char(methodattr));
      Delete(methodattr);
      return methods;
    }

    virtual int cDeclaration(Node *n) {


      return Language::cDeclaration(n);
    }

    virtual int emitFreePascalFunctionPointer(Node *n, const String *cname, const String *pasname, String  *f, char * /*_external*/, Hash *import, String *_storage) {
      TRACE_FUNC_ENTR;

      //String * overloaded = Getattr(n,"sym:overloaded");
      //String * overloaded_name = Getattr(n,"sym:overname");
      String *im_return_type = NewString("");
      //String   *symname = Getattr(n,"sym:name");
      ParmList *l = Getattr(n, "parms");

      //String *storage= 0;//= Getattr(n, "storage");
      //storage = NewStringf(callconv);

      String *_pars = NewStringf("");
      //String *tt = Getattr(n, "type");
      //  attachMappedType(n, "pasrawrettype");

      /* Attach the non-standard typemaps to the parameter list. */
      Swig_typemap_attach_parms("pasrawinmode", l, NULL);
      Swig_typemap_attach_parms("pasrawintype", l, NULL);

      /* Get return types */
      bool has_return=false;

      {
        String *tm = getMappedTypeNew(n, "pasrawrettype","");
        if (tm != NIL) {
          Printf(im_return_type, "%s", tm);
        }
        has_return = hasContent(tm);
      }

      Printf(f, "\ntype \n  %s = ",pasname);

      /* cname is the original name if 'n' denotes a C function
      and it is the relabeled name (sym:name) if 'n' denotes a C++ method or similar */
      //f.enterBlock(no_block);
      if (has_return) 
        Printf(f, "function  (", cname);
      else 
        Printf(f, "procedure (", cname);
      // Generate signature for raw interface
      {
        Parm *p;
        writeArgState state;
        attachParameterNames(n, "tmap:rawinname", "freepascal:rawname", "arg%d");
        for (p = skipIgnored(l, "pasrawintype"); p != NULL; p = skipIgnored(p, "pasrawintype")) {

          /* Get argument passing mode, should be one of VALUE, VAR, CONST */
          String *mode = Getattr(p, "tmap:pasrawinmode");
          String *argname = Getattr(p, "freepascal:rawname");
          String *im_param_type = getMappedType(p, "pasrawintype");
          //SwigType *type  = Getattr(p,"type");



          Replace(im_param_type," const", "", DOH_REPLACE_FIRST);

          if (import)
            addImports(import, "pasrawintype", p);
          if (argname != NULL)  // varargs skipped"
            writeArg(p, _pars, state, mode, argname, im_param_type, NIL);
          if (im_param_type != NIL) {
            p = Getattr(p, "tmap:pasrawintype:next");
          } 
          else {
            p = nextSibling(p);
          }
        }

        writeArg(n, _pars, state, NIL, NIL, NIL, NIL);
      }

      /* Finish PAS raw prototype */
      Chop(_pars);
      Printf(f, "%s)", ChopCh(Char(_pars),';'));
      // neither a C wrapper nor a plain C function may throw an exception
      //generateThrowsClause(throws_hash, pasraw_intf.f);

      if (has_return) {
        Printf(f, ": %s", im_return_type);
      }

      Printf(f, "");
      Printf(f, "; %s", _storage);
      Printf(f, ";\n");

      Delete(_pars);
      Delete(im_return_type);
      return SWIG_OK;

      TRACE_FUNC_EXIT;
    }


    virtual int typedefHandler(Node *n) {
      TRACE_FUNC_ENTR;

      String *tt = Getattr(n, "type");

      int ret = 0;
      ret = Language::typedefHandler(n);

      if (Getattr(n,"template"))
        registerTypemapsForTemplatedClass(n);

      SwigType *name = Getattr(n, "name");
      SwigType *decl = Getattr(n, "decl");

      if (SwigType_ispointer(decl)) {
        // ret = Language::typedefHandler(n);
        Swig_save("FPHandler",n,"type","kind","decl", NIL);

        tt = Getattr(n, "type");

        SwigType  *bb = SwigType_base(tt);

        Setattr(n, "kind","function");
        //   Delattr(n, "storage");
        SwigType_del_pointer(decl);
        Setattr(n, "type",bb);

        if (SwigType_isfunction(decl)) {
          //SwigType *pname = Copy(name);
          SwigType *pname = Copy(typemapLookup(n, "pasrawtype", name, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF));
          emitFreePascalFunctionPointer(n, name, pname, interface_functions, 0, pasraw_intf.import, callconv);


          Delete(pname);

          Swig_restore(n);
        }
        else if (Cmp(bb, "void") == 0 ) { 

          /* since this is a recurring issue, we are going to remember the
          typedef pointer, if already it is not a pointer or reference, as
          in

          typedef void NT;
          int func(NT *p); 

          see director_basic.i for example.
          */

          String *record = NewString("");


          Printf(record, 
            "   $RecordName = type pointer;\n"
            "   P$RecordNameArray = ^$RecordNameArray_;\n"
            "   $RecordNameArray_=array[0..(MaxInt div sizeof($RecordName))-1] of $RecordName;\n"            
            );
          Replace(record,"$RecordName", name, DOH_REPLACE_ANY);


          pasraw_intf.enterBlock(blocktype);
          Printf(pasraw_intf.f, "%s", record);

          Delete(record);


          Swig_restore(n);

        }
	else {        
          Swig_restore(n);
          //  ret = Language::typedefHandler(n);

        }

        // SwigType_add_pointer(decl);

        // Add Function pointer declaration

      }
      //  else   ret = Language::typedefHandler(n);

      TRACE_FUNC_EXIT;
      return ret;
    } //virtual int typedefHandler(Node *n)

    virtual int classforwardDeclaration(Node *n)
    {
      TRACE_FUNC_ENTR;
      //raw_class_name = Copy(Getattr(n, "sym:name"));
      String *name = Getattr(n,"name");
      proxy_class_name = Copy(typemapLookup(n, "paswraptype", name, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF));
      raw_class_name = Getattr(n,"sym:name");

      String *forward_declared = Getattr(n,"forward_declared");


      /* This will invoke memberfunctionHandler, membervariableHandler ...
      and finally it may invoke functionWrapper
      for wrappers and member variable accessors.
      It will invoke Language:constructorDeclaration
      which decides whether to call FREEPASCAL::constructorHandler */
      // Language::classHandler(n);

      {
        String *kind = Getattr(n, "kind");
        if (Cmp(kind, "struct") == 0) {

          String *entries = NewString("");

          if (forward_declared != 0) {

            pasraw_intf.enterBlock(blocktype);
            // Printf(pasraw_intf.f, "P%s = ^%s;\n", proxy_class_name, proxy_class_name);
            // Printf(pasraw_intf.f, "%s = integer; (* opaque record*)\n", proxy_class_name);

            Setattr(n,"forward_declared","1");  
            //Printf(pasraw_intf.f, "   %s = type pointer;\n", proxy_class_name);
          }

          Delete(entries);
        } 
        else if (Cmp(kind, "class") == 0) {
          enum access_privilege { acc_public, acc_protected, acc_private };
          //int max_acc = acc_public;

          if (forward_declared != 0) {

            pasraw_intf.enterBlock(blocktype);
            // Printf(pasraw_intf.f, "%s = integer; (* opaque record*)\n", proxy_class_name);

            Setattr(n,"forward_declared","1");  
            //Printf(pasraw_intf.f, "   %s = type pointer;\n", proxy_class_name);
            Printf(pasraw_intf.f, "   %s = Pointer;\n", 
	      typemapLookup(n, "pasrawtype", name, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF));
            //Printf(pasraw_intf.f, "   %s = class;\n", proxy_class_name);
          }
        }  
      }
      TRACE_FUNC_EXIT;
      return Language::classforwardDeclaration(n);
    }

    /* ----------------------------------------------------------------------
    * Automatically registers typemaps for pasraw* methods for a type
    * ---------------------------------------------------------------------- */
    virtual void registerTypemapPasRawMethodsForType(ParmList *pattern, const_String_or_char_ptr newName) 
    {
      TRACE_FUNC_ENTR;
        //Swig_typemap_register("ctype", pattern, symname, NULL, NULL); //not required for C++ wrapper
        Swig_typemap_register("pasrawtype", pattern, newName, NULL, NULL);
        Swig_typemap_register("pasrawintype", pattern, newName, NULL, NULL);
        Swig_typemap_register("pasrawouttype", pattern, newName, NULL, NULL);
        Swig_typemap_register("pasrawrettype", pattern, newName, NULL, NULL);
      TRACE_FUNC_EXIT;
    }
    /* ----------------------------------------------------------------------
    * Automatically registers typemaps for template for a type
    * ---------------------------------------------------------------------- */
    virtual void registerTypemapsForType(Node *n, String *name) {
      TRACE_FUNC_ENTR;
        Parm *pattern = NewParm(name, NULL, n);
	String *symname = Getattr(n,"sym:name");
        String *newName = NewStringf("%s%s", "CP",symname);
	SwigType *ptype = Getattr(pattern, "type");

	registerTypemapPasRawMethodsForType(pattern, newName);

        SwigType_add_pointer(ptype);
	registerTypemapPasRawMethodsForType(pattern, newName);

        SwigType_add_qualifier(ptype, "const");
	registerTypemapPasRawMethodsForType(pattern, newName);
	SwigType_del_qualifier(ptype);
	SwigType_del_pointer(ptype);

	SwigType_add_reference(ptype);
	registerTypemapPasRawMethodsForType(pattern, newName);

        SwigType_add_qualifier(ptype, "const");
	registerTypemapPasRawMethodsForType(pattern, newName);
	SwigType_del_qualifier(ptype);
	SwigType_del_reference(ptype);
	
	Delete(pattern);
	Delete(symname);
	Delete(newName);
      TRACE_FUNC_EXIT;
    }

    /* ----------------------------------------------------------------------
    * Automatically registers typemaps for template for all major types
    * ---------------------------------------------------------------------- */
    virtual void registerTypemapsForTemplatedClass(Node *n) {
      TRACE_FUNC_ENTR;
      if (!n) return;
      String *name = Getattr(n,"name");
	
      ParmList *templateparms = Getattr(n,"templateparms");
      if (!templateparms) return;
      Parm *a = templateparms;
      while (a) {
	LOG_NODE_DEBUG(a);
	registerTypemapsForType(n, name);
	a = nextSibling(a);
      }
      TRACE_FUNC_EXIT;
    }


    virtual int constantHandler(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);

      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * classHandler()
    * ---------------------------------------------------------------------- */

    virtual int classHandler(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);

      String *templat = Getattr(n,"template");
      //String *templateargs = Getattr(n,"templateargs");
      if (templat) {
	LOG_NODE_DEBUG(templat);
        //Swig_print_node(n);
        registerTypemapsForTemplatedClass(n);
      }
      //proxy_class_name = Copy(Getattr(n, "sym:name"));
      String *symname = Getattr(n,"sym:name");
      //CObjPtrTypeName = getMappedTypeNew(n, "pasrawtype", name);
      Setattr(n, "raw:name", typemapLookup(n,"pasrawtype", symname, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF));
      Setattr(n, "wrap:name", typemapLookup(n,"paswraptype", symname, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF));
      proxy_class_name = Copy(Getattr(n, "wrap:name"));
      raw_class_name = Copy(symname);

      PASFile output;

      bool hasnested = false;

      Node * LastNested = 0;
      {
        Node *child;
        for (child = firstChild(n); child != NIL; child = nextSibling(child)) {
          String *childType = nodeType(child);
          if (Strcmp(childType, "cdecl") == 0) {
            //String *member = Getattr(child, "sym:name");
            String *type = Getattr(child, "type");
            //        char *xx = Char(member);
            char *nested = Char(Getattr(child, "nested"));

            if (nested != 0) {
              hasnested = true;

              Node * sib;
              for (sib = nextSibling(n); sib != NIL; sib = nextSibling(sib)) {
                String *tn = Getattr(sib, "sym:name");

                if (Strcmp(tn, type) == 0) {
                  //  Setattr(sib, "porca put","1");
                  LastNested = sib;

                }

              }


            }

          }
        }

        /*  if (hasnested)
        output = NewStringf("");
        else
        output = NewStringf("");
        */
      }
      if (proxy_flag) {
        if (!addSymbol(proxy_class_name, n))
          return SWIG_ERROR;

        if (Cmp(proxy_class_name, pasraw_module_name) == 0) {
          Printf(stderr, "Class name cannot be equal to intermediary class name: %s\n", proxy_class_name);
          SWIG_exit(EXIT_FAILURE);
        }

        if (Cmp(proxy_class_name, paswrap_name) == 0) {
          Printf(stderr, "Class name cannot be equal to module class name: %s\n", proxy_class_name);
          SWIG_exit(EXIT_FAILURE);
        }


        //  emitBanner(paswrap_intf.f);

        Clear(proxy_class_def);
        Clear(proxy_class_code);
        Clear(proxy_class_constructor_def);

        have_default_constructor_flag = false;
        destructor_call = NewString("");
      }

      /* This will invoke memberfunctionHandler, membervariableHandler ...
      and finally it may invoke functionWrapper
      for wrappers and member variable accessors.
      It will invoke Language:constructorDeclaration
      which decides whether to call FREEPASCAL::constructorHandler */
      Language::classHandler(n);
      {
        String *kind = Getattr(n, "kind");
        if (Cmp(kind, "struct") == 0 && !opaque_record) {

          String *entries = NewString("");
          String *record = NewString("");

          Node *child;
          writeArgState state;
          for (child = firstChild(n); child != NIL; child = nextSibling(child)) {
            String *childType = nodeType(child);
            if (Strcmp(childType, "cdecl") == 0) {
              String *member = Getattr(child, "sym:name");
              //        char *xx = Char(member);

              ParmList *pl = Getattr(child, "parms");
              if (pl == NIL) {
                // Get the variable type in Pascal type equivalents
                String *pasct = getMappedTypeNew(child, "pasrawtype", "");
                writeArg(child, entries, state, NIL, member, pasct, NIL);
              }
            }
          }
          writeArg(n, entries, state, NIL, NIL, NIL, NIL);

          //  pasraw_intf.enterBlock(blocktype);
          output.enterBlock(blocktype);
          /*Printf(record, "P%s = ^%s;\n", proxy_class_name, proxy_class_name);
          Printf(record, "%s = record\n"
          "%s"
          "end;\n", proxy_class_name, entries);
          */

          //char * xx;
          //xx = Char(proxy_class_name);


          String * pt = NewString("P$RecordName");
          String * pat = NewString("P$RecordNameArray");

          Replace(pt,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);
          Replace(pat,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);



          Setattr(types_hash,proxy_class_name,"1");
          Setattr(types_hash,pt,"1");
          Setattr(types_hash,pat,"2");

          Delete(pt);
          Delete(pat);

          Printf(record, "P$RecordName = ^$RecordName;\n"
            "$RecordName = record\n"
            "$fields\n"
            "end;\n"
            "P$RecordNameArray = ^$RecordNameArray_;\n"
            "$RecordNameArray_=array[0..(MaxInt div sizeof($RecordName))-1] of   $RecordName;\n"            
            );
          Replace(record,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);
          Replace(record,"$fields", entries, DOH_REPLACE_ANY);

          //Printf(pasraw_intf.f, "%s", record);
          Printf(output.f, "%s", record);

          //    Printf(types_decl, "%s", record);


          Delete(entries);
          Delete(record);


        }
        else if (Cmp(kind, "union") == 0 && !opaque_record) {

          String *entries = NewString("");
          String *record = NewString("");
          String *fields = NewString("");
          //String *fields1 = NewString("");

          int cc = 0;

          Node *child;
          writeArgState state;
          for (child = firstChild(n); child != NIL; child = nextSibling(child)) {
            String *childType = nodeType(child);
            if (Strcmp(childType, "cdecl") == 0) {
              String *member = Getattr(child, "sym:name");
              //        char *xx = Char(member);

              ParmList *pl = Getattr(child, "parms");
              if (pl == NIL) {
                // Get the variable type in Pascal type equivalents
                String *pasct = getMappedTypeNew(child, "pasrawtype", "");
                fields = NewStringf("");    
                //          writeArg(entries, state, NIL, member, pasct, NIL);
                writeArg(child, fields, state, NIL, member, pasct, NIL); 

                if (hasContent(fields))
                  Printf(entries,"%d:(%s);\n", cc++, fields);

              }
            }
          }
          fields = NewStringf("");
          //    writeArg(entries, state, NIL, NIL, NIL, NIL);

          writeArg(n, fields, state, NIL, NIL, NIL, NIL); 
          Printf(entries,"%d:(%s);\n", cc++, fields);

          //pasraw_intf.enterBlock(blocktype);
          output.enterBlock(blocktype);
          /*Printf(record, "P%s = ^%s;\n", proxy_class_name, proxy_class_name);
          Printf(record, "%s = record\n"
          "%s"
          "end;\n", proxy_class_name, entries);
          */

          //char * xx;
          //xx = Char(proxy_class_name);

          String * pt = NewString("$RecordName");
          String * pat = NewString("$RecordNameArray");

          Replace(pt,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);
          Replace(pat,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);

          Setattr(types_hash,proxy_class_name,"1");
          Setattr(types_hash,pt,"1");
          Setattr(types_hash,pat,"2");

          Delete(pt);
          Delete(pat);
          Printf(record, "$RecordName = ^$RecordName;\n"
            "$RecordName = record case integer of\n"
            "$fields\n"
            "end;\n"
            "P$RecordNameArray = ^$RecordNameArray_;\n"
            "$RecordNameArray_=array[0..(MaxInt div sizeof($RecordName))-1] of   $RecordName;\n"            
            );
          Replace(record,"$RecordName", proxy_class_name, DOH_REPLACE_ANY);
          Replace(record,"$fields", entries, DOH_REPLACE_ANY);

          //Printf(pasraw_intf.f, "%s", record);
          Printf(output.f, "%s", record);

          //    Printf(types_decl, "%s", record);

          Delete(entries);
          Delete(record);
        }
        else if (Cmp(kind, "class") == 0 || opaque_record) {

          String *record = NewString("");
          String *forward_declared = Getattr(n,"forward_declared");

          if (forward_declared != 0) {
            Printf(record, "  %s = pointer;\n", Getattr(n,"raw:name"));
          }else {
            Printf(record, "  %s = pointer;\n", Getattr(n,"raw:name"));
            Setattr(n,"forward_declared","1");
          }

          pasraw_intf.enterBlock(blocktype);
          Printf(pasraw_intf.f, "%s", record);

          Delete(record);

        } else {
          Swig_warning(WARN_FREEPASCAL_TYPECONSTRUCTOR_UNKNOWN, input_file, line_number, "Unknown type constructor %s\n", kind);
        }
      }

      //   Language::classHandler(n);
      /*  if (hasnested)
      output = NewStringf("");
      else
      output = NewStringf("");
      */

      if (proxy_flag) {

        emitProxyClassDefAndCPPCasts(n);
        paswrap_intf.enterBlock(blocktype);
        Printv(paswrap_intf.f, proxy_class_def, NIL);
        Printv(paswrap_impl.f, proxy_class_code, NIL);  

        //Printv(f_proxy, proxy_class_def, proxy_class_code, NIL);

        //  Printf(f_proxy, "}\n");
        //Close(f_proxy);
        //f_proxy = NULL;

        Delete(proxy_class_name);
        proxy_class_name = NULL;
        Delete(destructor_call);
        destructor_call = NULL;
      }

      if (hasnested) {
        assert (LastNested != 0);
        Setattr(LastNested, "Container", NewStringf(output.f));

      }
      else {  

        Printf(pasraw_intf.f,"%s", output.f);
        String * container = Getattr(n,"Container");

        if (container != 0) {

          Printf(pasraw_intf.f,"\n%s\n", container);
          Delete(container);

        }


      }

      pasraw_intf.enterBlock(no_block);

      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * memberfunctionHandler()
    * ---------------------------------------------------------------------- */

    virtual int memberfunctionHandler(Node *n) {
      TRACE_FUNC_ENTR;
      Setattr(n, "freepascal:functype", "method");
      Language::memberfunctionHandler(n);

      {
        /* Language::memberfunctionHandler will remove the mapped types
        that emitPasWrapper may attach */
        ParmList *pl = Getattr(n, "parms");
        Swig_typemap_attach_parms("paswrapinmode", pl, NULL);
        Swig_typemap_attach_parms("paswrapinname", pl, NULL);
        Swig_typemap_attach_parms("paswrapintype", pl, NULL);
        Swig_typemap_attach_parms("paswrapindefault", pl, NULL);
        attachParameterNames(n, "tmap:paswrapinname", "autoname", "arg%d");
        String *rettype = getMappedTypeNew(n, "paswrapouttype", "");

        String *methodname = Getattr(n, "sym:name");
        /*
        if (methodname==NIL) {
        methodname = Getattr(n,"name");
        }
        */
        String *arguments = createPasSignature(n);
        String *storage = Getattr(n, "storage");
        String *overridden = Getattr(n, "override");

        //printf("memberfunctionHandler storage:%s  override:%s\n", Char(storage), Char(overridden));
        //bool isVirtual = (storage != NIL) && (Strcmp(storage, "virtual") == 0);

        bool isVirtual = Equal(storage, "virtual");
        //printf("memberfunctionHandler isvirtual %s %s\n", Char(storage), isVirtual?"yes":"no");
        bool isOverridden = Equal(overridden, "1");

        if ((!isVirtual) || (!isOverridden)) {
          {
            String *methods = getMethodDeclarations(n);
            Printf(methods, "%s(%s)%s%s;%s\n",
              methodname, arguments,
              hasContent(rettype) ? ": " : "", hasContent(rettype) ? (const String *) rettype : "", isVirtual ? "  (* base method *)" : "");
          }
          {
            /* this was attached by functionWrapper
            invoked by Language::memberfunctionHandler */
            String *fname = Getattr(n, "freepascal:funcname");
            String *overrides = getAttrString(parentNode(n), "freepascal:override");
            Printf(overrides, "%s := %s;\n", methodname, fname);
          }
        }
      }

      if (proxy_flag) {
        String *overloaded_name = getOverloadedName(n);
        //String *intermediary_function_name = Swig_name_member(proxy_class_name, overloaded_name);
        String *intermediary_function_name = Swig_name_member(0, proxy_class_name, overloaded_name);
        Setattr(n, "proxyfuncname", Getattr(n, "sym:name"));
        Setattr(n, "imfuncname", intermediary_function_name);
        proxyClassFunctionHandler(n);
        Delete(overloaded_name);
      }
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * staticmemberfunctionHandler()
    * 
    * MM: Deprecated. We treat const members as read only vars so value is taken from C domain.
    * ---------------------------------------------------------------------- */

    virtual int staticmemberfunctionHandler(Node *n) {
      TRACE_FUNC_ENTR;

      static_flag = true;

      Language::staticmemberfunctionHandler(n);

      if (proxy_flag) {
        String *overloaded_name = getOverloadedName(n);
        //String *intermediary_function_name = Swig_name_member(proxy_class_name, overloaded_name);
        String *intermediary_function_name = Swig_name_member(0, proxy_class_name, overloaded_name);
        Setattr(n, "proxyfuncname", Getattr(n, "sym:name"));
        Setattr(n, "imfuncname", intermediary_function_name);
        proxyClassFunctionHandler(n);
        Delete(overloaded_name);
      }
      static_flag = false;

      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* -----------------------------------------------------------------------------
    * proxyClassFunctionHandler()
    *
    * Function called for creating a Pascal wrapper function around a c++ function in the 
    * proxy class. Used for both static and non-static C++ class functions.
    * C++ class static functions map to Pascal static functions.
    * Two extra attributes in the Node must be available. These are "proxyfuncname" - 
    * the name of the Pascal class proxy function, which in turn will call "imfuncname" - 
    * the intermediary (PInvoke) function name in the intermediary class.
    * ----------------------------------------------------------------------------- */

#if 0

    String * overloaded = Getattr(n,"sym:overloaded");
    String * overloaded_name = Getattr(n,"sym:overname");
    String *im_return_type = NewString("");
    //String   *symname = Getattr(n,"sym:name");
    ParmList *l = Getattr(n, "parms");

    String *storage= 0;//= Getattr(n, "storage");

    storage = NewStringf(callconv);

    String *_pars = NewStringf("");

    /* Attach the non-standard typemaps to the parameter list. */
    Swig_typemap_attach_parms("pasrawinmode", l, NULL);
    Swig_typemap_attach_parms("pasrawintype", l, NULL);


    //       if (strcmp(Char(cname), "Delete_gamma_lut_type") == 0) __asm { int 3};

    /* Get return types */
    bool has_return;
    {
      String *tm = getMappedTypeNew(n, "pasrawrettype", "");



      if (tm != NIL) {
        Printf(im_return_type, "%s", tm);
      }
      has_return = hasContent(tm);
    }

    /* cname is the original name if 'n' denotes a C function
    and it is the relabeled name (sym:name) if 'n' denotes a C++ method or similar */
    //f.enterBlock(no_block);
    if (has_return) 
      //Printf(f, "\nfunction %s (", cname, pasname);
      Printf(f, "\nfunction %s (", pasname);
    else 
      //Printf(f, "\nprocedure %s (", cname, pasname);
      Printf(f, "\nprocedure %s (", pasname);
    // G
    // Generate signature for raw interface
    {
      Parm *p;
      writeArgState state;
      attachParameterNames(n, "tmap:rawinname", "freepascal:rawname", "arg%d");
      for (p = skipIgnored(l, "pasrawintype"); p != NULL; p = skipIgnored(p, "pasrawintype")) {

        /* Get argument passing mode, should be one of VALUE, VAR, CONST */
        String *mode = Getattr(p, "tmap:pasrawinmode");
        String *argname = Getattr(p, "freepascal:rawname");
        String *im_param_type = getMappedType(p, "pasrawintype");
        SwigType *type  = Getattr(p,"type");

        Replace(im_param_type," const", "", DOH_REPLACE_FIRST);

        if (import)
          addImports(import, "pasrawintype", p);
        if (argname != NULL)  // varargs skipped"
          writeArg(_pars, state, mode, argname, im_param_type, NIL);
        if (im_param_type != NIL) {
          p = Getattr(p, "tmap:pasrawintype:next");
        } 
        else {
          p = nextSibling(p);
        }
      }

      writeArg(_pars, state, NIL, NIL, NIL, NIL);
    }

    /* Finish PAS raw prototype */
    Chop(_pars);
    Printf(f, "%s)", ChopCh(Char(_pars),';'));
    // neither a C wrapper nor a plain C function may throw an exception
    //generateThrowsClause(throws_hash, pasraw_intf.f);

    if (has_return) {
      Printf(f, ": %s", im_return_type);
    }

    Printf(f, "");

    Printf(f, "; %s", _storage);

    if (overloaded) // overloaded has a pointer, but string value can be 0x0
      Printf(f, "; overload" );


    char * ss = Char(storage);

    if (_external != 0) {

      if (overloaded) // overloaded has a pointer, but string value can be 0x0
        Printf(f, "; external %s name '%s%s'", _external, cname, overloaded_name);
      else
        Printf(f, "; external %s name '%s'", _external, cname);
    }

    Printf(f, ";\n");

    Delete(_pars);
    Delete(im_return_type);
    return SWIG_OK;


#endif

    /*
    function_declaration;
    function_locals;
    begin
    function_code;
    end;

    */

    void proxyClassFunctionHandler(Node *n) {
      TRACE_FUNC_ENTR;
      SwigType *t = Getattr(n, "type");
      ParmList *l = Getattr(n, "parms");
      Hash *throws_hash = NewHash();
      String *intermediary_function_name = 0; /* = Getattr(n, "imfuncname");*/

      String *pasname_function_name = Getattr(n, "freepascal:pasname" );
      // In order to use freepascal side overloaded functions name
      if (pasname_function_name != 0)
        intermediary_function_name = pasname_function_name;
      else 
        intermediary_function_name = Getattr(n, "imfuncname");


      String *proxy_function_name = Getattr(n, "proxyfuncname");
      //String *overloaded = Getattr(n,"sym:overloaded");
      //String *name = Getattr(n,"sym:name");
      String *tm;
      Parm *p;
      int i;
      String *pre_code = NewString("");
      String *post_code = NewString("");
      String *imcall = NewString("");
      String *return_type = NewString("");
      String *function_declaration = NewString("");
      String *function_locals = NewString("");
      String *function_code = NewString("");
      String *pasname = NewString("");
      bool setter_flag = false;
      bool has_return = false;
      String *method_kind = NewString("");
      // SM  return;

      if (!proxy_flag)
        return;

      if (l) {
        if (SwigType_type(Getattr(l, "type")) == T_VOID) {
          l = nextSibling(l);
        }
      }

      /* Attach the non-standard typemaps to the parameter list */
      Swig_typemap_attach_parms("in", l, NULL);
      Swig_typemap_attach_parms("paswraptype", l, NULL);
      Swig_typemap_attach_parms("paswrapinmode", l, NULL);
      Swig_typemap_attach_parms("paswrapintype", l, NULL);

      Swig_typemap_attach_parms("pasin_locals", l, NULL);
      Swig_typemap_attach_parms("pasin_pre", l, NULL);
      Swig_typemap_attach_parms("pasin_post", l, NULL);


      Swig_typemap_attach_parms("pasin", l, NULL);



      /* Get return types */
      if (!wrapping_constructor_flag && (tm = getMappedTypeNew(n, "paswraptype", ""))) {
        substituteClassname(t, tm);
        RemoveNamespace(tm);
        Printf(return_type, "%s", tm);
        has_return = hasContent(tm);
      }
      else 
        has_return = false;

      if (proxy_flag && wrapping_member_flag && !enum_constant_flag) {
        // Properties
        pasname = getPascalName(n);
        //setter_flag = (Cmp(Getattr(n, "sym:name"), Swig_name_set(Swig_name_member(proxy_class_name, variable_name)))
	String *nameSet = Swig_name_set(getNSpace(), Swig_name_member(0, raw_class_name, variable_name));
	String *symName = Getattr(n, "sym:name");
        setter_flag = (Cmp(symName, nameSet) == 0);
        if (setter_flag) 
          proxy_function_name = NewStringf("Set%s", pasname);
        else
          proxy_function_name = NewStringf("Get%s", pasname);
      }


      if (wrapping_constructor_flag) 
        Printf(method_kind, "%s", "constructor");
      else {

        if (has_return)
          Printf(method_kind, "%s", "function");
        else
          Printf(method_kind, "%s", "procedure");
      }

      /* Start generating the proxy function */
      //Printf(function_code, "  %s ", Getattr(n, "feature:freepascal:methodmodifiers"));

      Printf(proxy_class_constructor_def, "\n");


      if (static_flag) {
        Printf(function_declaration, "class ");
        Printf(proxy_class_constructor_def, "    class ");
      }
      else {
        Printf(function_declaration, "");
        Printf(proxy_class_constructor_def, "    ");
      }

      Printf(proxy_class_constructor_def, "%s %s (", method_kind, proxy_function_name);
      Printf(function_declaration, "%s %s.%s (", method_kind, proxy_class_name, proxy_function_name);

      // Begin parameters list


      if (wrapping_constructor_flag) {


        Printv(pre_code, 
          "  inherited Create;\n",
          "  FOwnCObjPtr := true;\n",
          NIL);

        Printv(imcall, 
          "  FCObjPtr := ", pasraw_module_name, ".", intermediary_function_name, "(",
          NIL);

      }
      else {
        Printv(pre_code, !static_flag ?"  assert(FCObjPtr <> nil);\n" : "\n",NIL);

        Printv(imcall, pasraw_module_name, ".", intermediary_function_name, "(", NIL);
      }
      if (!static_flag && !wrapping_constructor_flag)
        Printv(imcall, "Self.FCObjPtr", NIL);

      emit_mark_varargs(l);

      int gencomma = !static_flag && !wrapping_constructor_flag;

      /* Output each parameter */
      writeArgState state;
      for (i = 0, p = l; p; i++) {

        /* Ignored varargs */
        if (checkAttribute(p, "varargs:ignore", "1")) {
          p = nextSibling(p);
          continue;
        }

        /* Ignored parameters */
        if (checkAttribute(p, "tmap:in:numinputs", "0")) {
          p = Getattr(p, "tmap:in:next");
          continue;
        }

        /* Ignore the 'this' argument for variable wrappers */
        if (!(variable_wrapper_flag && i == 0)) {
          SwigType *pt = Getattr(p, "type");
          String *param_type = NewString("");

          String *param_mode = NewString("");
          if ((tm = Getattr(p, "tmap:paswrapinmode"))) {
            substituteClassname(pt, tm);
            Printf(param_mode, "%s", tm);
          }



          /* Get the Pascal parameter type */
          if ((tm = getMappedType(p, "paswrapintype"))) {
            substituteClassname(pt, tm);
            Printf(param_type, "%s", tm);
	  }


          if (gencomma)
            Printf(imcall, ", ");

          String *arg = variable_wrapper_flag ? NewString("value") : makeParameterName(n,
            p,
            i);
          Replaceall(arg,"::","_");

          // Use typemaps to transform type used in Pascal wrapper function (in proxy class) to type used in PInvoke function (in intermediary class)
          if ((tm = getMappedType(p, "pasin"))) {
            addThrows(throws_hash, "pasin", p);
            substituteClassname(pt, tm);
            Replaceall(tm, "$input", arg);
            Printv(imcall, tm, NIL);

            String *pre = getMappedType(p, "pasin_pre",false);
            if (pre) {
              substituteClassname(pt, pre);
              Replaceall(pre, "$input", arg);
              Printv(pre_code, pre, NIL);
              Printf(pre_code, "\n");
            }

            String *post = getMappedType(p, "pasin_post",false);
            if (post) {
              substituteClassname(pt, post);
              Replaceall(post, "$input", arg);
              //if (Len(p_code) > 0)
              Printv(post_code, post, NIL);
              Printf(post_code, "\n");            
            }

            String *locals = getMappedType(p, "pasin_locals",false);
            if (locals) {
              substituteClassname(pt, locals);
              Replaceall(locals, "$input", arg);
              //if (Len(p_code) > 0)
              Printv(function_locals, locals, NIL);
              Printf(function_locals, "\n");            
            }

#if 0
            String *pre = Getattr(p, "tmap:pasin:pre");
            if (pre) {
              substituteClassname(pt, pre);
              Replaceall(pre, "$input", arg);
              Printv(pre_code, pre, NIL);
              Printf(pre_code, "\n");
            }

            String *post = Getattr(p, "tmap:pasin:post");
            if (post) {
              substituteClassname(pt, post);
              Replaceall(post, "$input", arg);
              //if (Len(p_code) > 0)
              Printv(post_code, post, NIL);
              Printf(post_code, "\n");            
            }

            String *locals = Getattr(p, "tmap:pasin:locals");
            if (locals) {
              substituteClassname(pt, locals);
              Replaceall(locals, "$input", arg);
              //if (Len(p_code) > 0)
              Printv(function_locals, locals, NIL);
              Printf(function_locals, "\n");            
            }
#endif

          }

          /* Add parameter to proxy function */
          if (gencomma >= 2) {
            Printf(proxy_class_constructor_def, "; ");
            Printf(function_declaration, "; ");
          }
          gencomma = 2;

          RemoveNamespace(param_type);

          Printf(proxy_class_constructor_def, "%s %s: %s", param_mode, arg, param_type);
          Printf(function_declaration, "%s %s: %s", param_mode, arg, param_type);

          Delete(arg);
          Delete(param_type);
          Delete(param_mode);
        }
        p = Getattr(p, "tmap:in:next");
      }

      Printf(imcall, ")");
      //Printf(imcall, ";");


      // end parameter list

      if (!gencomma && wrapping_constructor_flag)    // We must have a default constructor
        have_default_constructor_flag = true;

      Printf(proxy_class_constructor_def,")", method_kind, proxy_function_name);
      Printf(function_declaration, ")", method_kind, proxy_function_name);

      if (has_return) {
        Printf(proxy_class_constructor_def,": %s;", return_type);
        Printf(function_declaration, ": %s;", return_type);
      }
      else {
        Printf(proxy_class_constructor_def, ";");
        Printf(function_declaration, ";");
      }

      if (Getattr(n,"sym:overloaded") || wrapping_constructor_flag) {
        Printf(proxy_class_constructor_def, "overload; ");
      }

      if (Getattr(n, "override")) {
        Printf(proxy_class_constructor_def, "override; ");
      }
      else if (checkAttribute(n, "storage", "virtual")) {
        Printf(proxy_class_constructor_def, "virtual; ");
      }

      // Transform return type used in PInvoke function (in intermediary class) to type used in Pascal wrapper function (in proxy class)
      if (!wrapping_constructor_flag && (tm = getMappedTypeNew(n, "pasout", ""))) {

        if (has_return) {

          //String *tm1 = getMappedTypeNew(n, "ctype", "");

          /*
          String *pre = getMappedTypeNew(n, "pasout_locals", "");
          if (pre) {
          substituteClassname(pt, pre);
          Replaceall(pre, "$input", arg);
          //if (Len(pre_code) > 0)
          //  Printf(pre_code, "\n");
          Printv(pre_code, pre, NIL);
          Printf(pre_code, "\n");
          }

          String *post = Getattr(p, "tmap:pasin:post");
          if (post) {
          substituteClassname(pt, post);
          Replaceall(post, "$input", arg);
          //if (Len(p_code) > 0)
          Printv(post_code, post, NIL);
          Printf(post_code, "\n");            
          }
          */
          //const String *locals = typemapLookup(n,"pasout_locals",  tm1, WARN_NONE);  
          const String *locals = Getattr(n, "tmap:pasout:locals");
          if (locals) {
            //          substituteClassname(pt, locals);
            //Replaceall(locals, "$input", arg);
            //if (Len(p_code) > 0)
            Printv(function_locals, locals, NIL);
            Printf(function_locals, "\n");            
          }

        }

        addThrows(throws_hash, "pasout", n);
        if (GetFlag(n, "feature:new"))
          Replaceall(tm, "$owner", "true");
        else
          Replaceall(tm, "$owner", "false");

        substituteClassname(t, tm);
        Replaceall(tm, "$imcall", imcall);
        Replaceall(tm, "$result", "Result");
      } 

      if (wrapping_constructor_flag)
      {
        tm = imcall;
      };

      Printf(function_code,"%s",pre_code);
      generateThrowsClause(throws_hash, function_code);
      Printf(function_code, " %s;\n", tm ? (const String *) tm : empty_string);
      Printf(function_code,"%s",post_code);
      Printf(proxy_class_constructor_def, "\n");


      if (proxy_flag && wrapping_member_flag && !enum_constant_flag) {
        Printv(proxy_class_code, 
          "\n",
          function_declaration,"\n", 
          function_locals, //hasContent(function_locals)?"\n":"",
          "begin\n",
          function_code,
          "end;\n",
          NIL);
      } 
      else {
        // Normal function call
        Printv(proxy_class_code, 
          "\n",
          function_declaration,"\n", 
          function_locals,//hasContent(function_locals)?"\n":"",
          "begin\n",
          function_code,
          "end;\n",
          NIL);
        //Printv(proxy_class_code, function_code, NIL);
      }

      if (proxy_flag && wrapping_member_flag && !enum_constant_flag) 
        Delete(proxy_function_name);
      Delete(pasname);
      Delete(pre_code);
      Delete(post_code);
      Delete(function_declaration);
      Delete(function_locals);
      Delete(method_kind);
      Delete(function_code);
      Delete(return_type);
      Delete(imcall);
      Delete(throws_hash);
      TRACE_FUNC_EXIT;
    }

    /* ----------------------------------------------------------------------
    * constructorHandler()
    * ---------------------------------------------------------------------- */

    virtual int constructorHandler(Node *n) {
      TRACE_FUNC_ENTR;
      // this invokes functionWrapper
      wrapping_constructor_flag = true;
      Language::constructorHandler(n);

      if (proxy_flag) {

        String *overloaded_name = getOverloadedName(n);
        Setattr(n, "proxyfuncname", "Create");
        Setattr(n, "imfuncname",Swig_name_construct(getNSpace(), overloaded_name));
        proxyClassFunctionHandler(n);

#if 0

        ParmList *l = Getattr(n, "parms");

        Hash *throws_hash = NewHash();

        String *imcall = NewString("");
        String *function_code = NewString("");
        String *helper_code = NewString(""); // Holds code for the constructor helper method generated only when the csin typemap has code in the pre or post attributes
        String *helper_args = NewString("");
        String *pre_code = NewString("");
        String *post_code = NewString("");
        String *im_return_type = NewString("");
        bool feature_director = (parentNode(n) && Swig_directorclass(n));

        //    Printf(proxy_class_def, "    >-constructor Create(", Getattr(n, "feature:freepascal:methodmodifiers"), proxy_class_name);

        Printf(proxy_class_constructor_def, "  constructor Create(");

        Printf(proxy_class_code, "\nconstructor T%s.Create(",  proxy_class_name);

        //Printf(proxy_class_code, "  %s %s(", Getattr(n, "feature:freepascal:methodmodifiers"), proxy_class_name);
        //Printv(imcall, " : this(", pasraw_module_name, ".", Swig_name_construct(overloaded_name), "(", NIL);

        Printv(imcall, 
          "begin\n"
          "  inherited Create;\n"
          "  FOwnCObjPtr := true;\n"
          "  FCObjPtr := ", pasraw_module_name, ".", Swig_name_construct(overloaded_name), "(",
          NIL);


        /* Attach the non-standard typemaps to the parameter list */
        Swig_typemap_attach_parms("in", l, NULL);
        Swig_typemap_attach_parms("paswraptype", l, NULL);
        Swig_typemap_attach_parms("paswrapinmode", l, NULL);
        Swig_typemap_attach_parms("paswrapintype", l, NULL);
        Swig_typemap_attach_parms("pasin", l, NULL);

        emit_mark_varargs(l);

        int gencomma = 0;

        String *tm;
        Parm *p = l;
        int i;

        /* Output each parameter */
        for (i = 0; p; i++) {

          /* Ignored varargs */
          if (checkAttribute(p, "varargs:ignore", "1")) {
            p = nextSibling(p);
            continue;
          }

          /* Ignored parameters */
          if (checkAttribute(p, "tmap:in:numinputs", "0")) {
            p = Getattr(p, "tmap:in:next");
            continue;
          }

          SwigType *pt = Getattr(p, "type");
          String *param_type = NewString("");
          String *param_mode = NewString("");

          /* Get the Pascal parameter type */
          if ((tm = getMappedType(p, "paswrapintype"))) {
            substituteClassname(pt, tm);
            Printf(param_type, "%s", tm);
          }

          if ((tm = Getattr(p, "tmap:paswrapinmode"))) {
            substituteClassname(pt, tm);
            Printf(param_mode, "%s", tm);
          }


          if (gencomma)
            Printf(imcall, "\n\t ");

          String *arg = makeParameterName(n, p, i);

          // Use typemaps to transform type used in Pascal wrapper function (in proxy class) to type used in PInvoke function (in intermediary class)
          if ((tm = getMappedType(p, "in"))) {
            addThrows(throws_hash, "in", p);
            substituteClassname(pt, tm);
            Replaceall(tm, "$input", arg);
            Printv(imcall, tm, NIL);
          }

          /* Add parameter to proxy function */
          if (gencomma) {
            Printf(proxy_class_code, ", ");
            Printf(proxy_class_constructor_def, ", ");
          }
          //    Printf(proxy_class_code, "%s : %s",  arg, param_type);
          Printf(proxy_class_code, "%s %s : %s", param_mode, arg , param_type);

          gencomma = 1;

          Delete(arg);
          Delete(param_type);
          Delete(param_mode);
          p = Getattr(p, "tmap:in:next");
        }

        Printf(imcall, ")\n",
          "end;",NIL);

        Printf(proxy_class_constructor_def, "); overloaded;\n");
        Printf(proxy_class_code, ");");
        Printf(proxy_class_code, "\n%s", imcall);
        //generateThrowsClause(throws_hash, proxy_class_code);
        Printf(proxy_class_code, "end;", NIL);

        if (!gencomma)    // We must have a default constructor
          have_default_constructor_flag = true;


        Delete(helper_args);
        Delete(im_return_type);
        Delete(pre_code);
        Delete(post_code);
        //   Delete(construct_tm);
        //  Delete(attributes);
        Delete(imcall);
#endif
#if 0
        Delete(overloaded_name);
#endif    
      }
      wrapping_constructor_flag = false;
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * destructorHandler()
    * ---------------------------------------------------------------------- */

    virtual int destructorHandler(Node *n) {
      TRACE_FUNC_ENTR;
      Language::destructorHandler(n);
      String *symname = Getattr(n, "sym:name");

      if (proxy_flag) {
        Printv(destructor_call, pasraw_module_name, ".", Swig_name_destroy(getNSpace(), symname), "(FCObjPtr)", NIL);
      }
      return SWIG_OK;
      TRACE_FUNC_EXIT;
    }

    /* ----------------------------------------------------------------------
    * membervariableHandler()
    * ---------------------------------------------------------------------- */

    virtual int membervariableHandler(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);
      
      SwigType *t = Getattr(n, "type");
      String *tm;

      // Get the variable type
      if ((tm = getMappedTypeNew(n, "paswraptype", ""))) {
        substituteClassname(t, tm);
      }

      variable_name = Getattr(n, "sym:name");
      //printf("member variable: %s\n", Char(variable_name));

      // Output the property's field declaration and accessor methods
      //SM  Printf(proxy_class_code, "  public %s %s {", tm, variable_name);

      // SM: produces bugs in functionwrapper
      Setattr(n, "freepascal:functype", "accessor");
      wrapping_member_flag = true;
      variable_wrapper_flag = true;
      Language::membervariableHandler(n);
      wrapping_member_flag = false;
      variable_wrapper_flag = false;

      {
        //String *methods = getMethodDeclarations(n);
        //String *overrides = getAttrString(parentNode(n), "freepascal:override");
        SwigType *type = Getattr(n, "type");
        String *pasname = getPascalName(n);

        const String * prw;
	
	String * immut = Getattr(n,"variableHandler:feature:immutable");

	// lookup for RW or RO property definition
        if (SwigType_isconst(type) || Equal(immut,"1")) {
          prw = typemapLookup(n,"pasvarro", type, WARN_NONE);
        }
        else {
          prw = typemapLookup(n,"pasvarrw", type, WARN_NONE);      
        }

        Printv(proxy_class_constructor_def, prw, NIL);
        Replaceall(proxy_class_constructor_def,"$varname",pasname);
        Replaceall(proxy_class_constructor_def,"$vartype",tm);


        Delete(pasname);

      }

#if 0
      // SM Printf(proxy_class_code, "\n  }\n\n");

      {
        String *methods = getMethodDeclarations(n);
        String *overrides = getAttrString(parentNode(n), "freepascal:override");
        SwigType *type = Getattr(n, "type");
        String *pasname = capitalizeFirst(variable_name);
        //String *pasname    = nameToPascal(variable_name,true);
        if (!SwigType_isconst(type)) {
          {
            String *inmode = getMappedTypeNew(n, "paswrapinmode", "", false);
            String *intype = getMappedTypeNew(n, "paswrapintype", "");
            Printf(methods, "set%s(%s val:%s);\n", pasname, (inmode != NIL) ? (const String *) inmode : "", intype);
          }
          {
            /* this was attached by functionWrapper
            invoked by Language::memberfunctionHandler */
            String *fname = Getattr(n, "freepascal:setname");
            Printf(overrides, "set%s := %s;\n", pasname, fname);
          }
        }
        {
          {
            String *outtype = getMappedTypeNew(n, "paswrapouttype", "");
            Printf(methods, "get%s():%s;\n", pasname, outtype);
          }
          {
            /* this was attached by functionWrapper
            invoked by Language::memberfunctionHandler */
            String *fname = Getattr(n, "freepascal:getname");
            Printf(overrides, "get%s := %s;\n", pasname, fname);
          }
        }
        Delete(pasname);
      }
      //printf("end membervariableHandler(%s)\n", Char(Getattr(n,"name")));
#endif
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* ----------------------------------------------------------------------
    * staticmembervariableHandler()
    * ---------------------------------------------------------------------- */

    virtual int staticmembervariableHandler(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);
      
      int retval = SWIG_ERROR;
      /*MM: We just treat constant member as read only variable member.
       * So a constants simply gets value from C domain
       */
      retval = membervariableHandler(n);
      
#if 000
      bool static_const_member_flag = (Getattr(n, "value") == 0);

      Setattr(n, "freepascal:functype", "accessor");

      if (static_const_member_flag) {
        SwigType *t = Getattr(n, "type");
        String *tm;

        // Get the variable type
        if ((tm = getMappedTypeNew(n, "paswraptype", ""))) {
          substituteClassname(t, tm);
        }
        // Output the property's field declaration and accessor methods
        //  Printf(proxy_class_code, "  public static %s %s {", tm, Getattr(n, "sym:name"));
      }

      variable_name = Getattr(n, "sym:name");

      wrapping_member_flag = true;
      static_flag = true;
      Language::staticmembervariableHandler(n);
      wrapping_member_flag = false;
      static_flag = false;
      /*
      if (static_const_member_flag)
      Printf(proxy_class_code, "\n  }\n\n");
      */
#endif
      TRACE_FUNC_EXIT;
      return retval;
    }

    /* ----------------------------------------------------------------------
    * memberconstantHandler()
    * 
    * Should be retired because we treat constants as read-only variables.
    * They get values from C domain via getters (accessors).
    * ---------------------------------------------------------------------- */

    virtual int memberconstantHandler(Node *n) {
      TRACE_FUNC_ENTR;
      LOG_NODE_DEBUG(n);
      variable_name = Getattr(n, "sym:name");
      wrapping_member_flag = true;
      Language::memberconstantHandler(n);
      wrapping_member_flag = false;
      TRACE_FUNC_EXIT;
      return SWIG_OK;
    }

    /* -----------------------------------------------------------------------------
    * getOverloadedName()
    * ----------------------------------------------------------------------------- */

    String *getOverloadedName(Node *n) {
      String *overloaded_name = Copy(Getattr(n, "sym:name"));

      if (Getattr(n, "sym:overloaded")) {
        Printv(overloaded_name, Getattr(n, "sym:overname"), NIL);
      }

      return overloaded_name;
    }

    /* -----------------------------------------------------------------------------
    * emitPasWrapper()
    * It is also used for set and get methods of global variables.
    * ----------------------------------------------------------------------------- */

    void emitPasWrapper(Node *n, const String *func_name) {
      TRACE_FUNC_ENTR;
      SwigType *t = Getattr(n, "type");
      ParmList *l = Getattr(n, "parms");
      Hash *throws_hash = NewHash();
      int num_exceptions = 0;
      int num_returns = 0;
      String *rawcall = NewString("");
      String *reccall = NewString("");
      String *local_variables = NewString("");
      String *local_constants = NewString("");
      String *incheck = NewString("");
      String *outcheck = NewString("");
      String *setup = NewString("");
      String *cleanup = NewString("");
      String *outarg = NewString("");  /* don't mix up with 'autark' :-] */
      String *storeout = NewString("");
      String *result_name = NewString("");
      String *return_variables = NewString("");
      const char *result_return = "ret";
      String *function_code = NewString("");
      /*several names for the same function */
      String *raw_name = Getattr(n, "name");  /*original C function name */
      //String     *func_name = Getattr(n,"sym:name");  /*final FreePascal name chosen by the user*/
      bool setter_flag = false;
      int multiretval = GetFlag(n, "feature:freepascal:multiretval");

      if (l) {
        if (SwigType_type(Getattr(l, "type")) == T_VOID) {
          l = nextSibling(l);
        }
      }

      /* Attach the non-standard typemaps to the parameter list */
      Swig_typemap_attach_parms("paswrapargvar", l, NULL);
      Swig_typemap_attach_parms("paswrapargconst", l, NULL);
      Swig_typemap_attach_parms("paswrapargraw", l, NULL);
      Swig_typemap_attach_parms("paswrapargdir", l, NULL);
      Swig_typemap_attach_parms("paswrapinmode", l, NULL);
      Swig_typemap_attach_parms("paswrapinname", l, NULL);
      Swig_typemap_attach_parms("paswrapintype", l, NULL);
      Swig_typemap_attach_parms("paswrapindefault", l, NULL);
      Swig_typemap_attach_parms("paswrapinconv", l, NULL);
      Swig_typemap_attach_parms("paswrapincheck", l, NULL);
      Swig_typemap_attach_parms("paswrapoutname", l, NULL);
      Swig_typemap_attach_parms("paswrapouttype", l, NULL);
      Swig_typemap_attach_parms("paswrapoutconv", l, NULL);
      Swig_typemap_attach_parms("paswrapoutcheck", l, NULL);

      attachMappedType(n, "paswrapretraw");
      attachMappedType(n, "paswrapretname");
      attachMappedType(n, "paswraprettype");
      attachMappedType(n, "paswrapretvar");
      attachMappedType(n, "paswrapretconv");
      attachMappedType(n, "paswrapretcheck");

      Swig_typemap_attach_parms("paswrapfreearg", l, NULL);

      /*
      Swig_typemap_attach_parms("paswrapargvar:throws", l, NULL);
      Swig_typemap_attach_parms("paswrapargraw:throws", l, NULL);
      Swig_typemap_attach_parms("paswrapinconv:throws", l, NULL);
      Swig_typemap_attach_parms("paswrapincheck:throws", l, NULL);
      Swig_typemap_attach_parms("paswrapoutconv:throws", l, NULL);
      Swig_typemap_attach_parms("paswrapoutcheck:throws", l, NULL);

      attachMappedType(n, "paswrapretvar:throws");
      attachMappedType(n, "paswrapretconv:throws");
      attachMappedType(n, "paswrapretcheck:throws");

      Swig_typemap_attach_parms("paswrapfreearg:throws", l, NULL);
      */

      /* Attach argument names to the parameter list */
      /* should be a separate procedure making use of hashes */
      attachParameterNames(n, "tmap:paswrapinname", "autoname", "arg%d");

      /* Get return types */
      String *result_pasrawtype = Copy(getMappedTypeNew(n, "pasrawrettype", ""));
      String *result_paswraptype = Copy(getMappedTypeNew(n, "paswraprettype", ""));
      bool has_return_raw = hasContent(result_pasrawtype);
      bool has_return_pas = hasContent(result_paswraptype);
      if (has_return_pas) {
        num_returns++;
        //printf("%s: %s\n", Char(func_name),Char(result_paswraptype));
      }

      String *arguments = createPasSignature(n);

      /* Create local variables or RECORD fields for return values
      and determine return type that might result from a converted VAR argument. */
      {
        writeArgState state;
        if (multiretval && has_return_pas) {
          writeArg(n, return_variables, state, NIL, NewString(result_return), result_paswraptype, NIL);
        }

        Parm *p = skipIgnored(l, "paswrapouttype");
        while (p != NIL) {
          
	  LOG_NODE_DEBUG(p);
	  
          String *arg = Getattr(p, "tmap:paswrapoutname");
          if (arg == NIL) {
            arg = Getattr(p, "name");
          }

          String *tm = Getattr(p, "tmap:paswrapouttype");
          if (tm != NIL) {
            if (isOutParam(p)) {
              if (!multiretval) {
                if (num_returns == 0) {
                  Printv(result_name, arg, NIL);
                  Clear(result_paswraptype);
                  Printv(result_paswraptype, tm, NIL);
                } else {
                  Swig_warning(WARN_FREEPASCAL_TYPEMAP_MULTIPLE_RETURN,
                    input_file, line_number,
                    "Typemap paswrapargdir set to 'out' for %s implies a RETURN value, but the routine %s has already one.\nUse %%multiretval feature.\n",
                    SwigType_str(Getattr(p, "type"), 0), raw_name);
                }
              }
              num_returns++;
              addImports(paswrap_intf.import, "paswrapouttype", p);
              writeArg(p, return_variables, state, NIL, arg, tm, NIL);
            }
            p = skipIgnored(Getattr(p, "tmap:paswrapouttype:next"), "paswrapouttype");
          } else {
            p = nextSibling(p);
          }
        }
        writeArg(n, return_variables, state, NIL, NIL, NIL, NIL);

        if (multiretval) {
          Printv(result_name, "result", NIL);
          Printf(result_paswraptype, "%sResult", func_name);
          paswrap_intf.enterBlock(blocktype);
          Printf(paswrap_intf.f, "%s =\nRECORD\n%sEND;\n", result_paswraptype, return_variables);
          Printf(local_variables, "%s: %s;\n", result_name, result_paswraptype);
        } else {
          Append(local_variables, return_variables);
        }
      }

      /* Declare local constants e.g. for storing argument names. */
      {
        Parm *p = l;
        while (p != NIL) {

          String *arg = Getattr(p, "autoname");

          String *tm = Getattr(p, "tmap:paswrapargconst");
          if (tm != NIL) {
            addImports(paswrap_impl.import, "paswrapargconst", p);
            Replaceall(tm, "$input", arg);
            Printv(local_constants, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapargconst:next");
          } else {
            p = nextSibling(p);
          }

        }
      }

      /* Declare local variables e.g. for converted input values. */
      {
        String *tm = getMappedTypeNew(n, "paswrapretvar", "", false);
        if (tm != NIL) {
          addImports(paswrap_impl.import, "paswrapretvar", n);
          addThrows(throws_hash, "paswrapretvar", n);
          Printv(local_variables, tm, "\n", NIL);
        }

        Parm *p = l;
        while (p != NIL) {

          String *arg = Getattr(p, "autoname");

          tm = Getattr(p, "tmap:paswrapargvar");
          if (tm != NIL) {
            /* exceptions that may be raised but can't be catched,
            thus we won't count them in num_exceptions */
            addImports(paswrap_impl.import, "paswrapargvar", p);
            addThrows(throws_hash, "paswrapargvar", p);
            Replaceall(tm, "$input", arg);
            Printv(local_variables, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapargvar:next");
          } else {
            p = nextSibling(p);
          }

        }
      }

      /* Convert input values from Pascal to C. */
      {
        Parm *p = l;
        while (p != NIL) {

          String *arg = Getattr(p, "autoname");

          String *tm = Getattr(p, "tmap:paswrapinconv");
          if (tm != NIL) {
            addImports(paswrap_impl.import, "paswrapinconv", p);
            num_exceptions += addThrows(throws_hash, "paswrapinconv", p);
            Replaceall(tm, "$input", arg);
            Printv(setup, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapinconv:next");
          } else {
            p = nextSibling(p);
          }

        }
      }

      /* Generate checks for input value integrity. */
      {
        Parm *p = l;
        while (p != NIL) {

          String *arg = Getattr(p, "autoname");

          String *tm = Getattr(p, "tmap:paswrapincheck");
          if (tm != NIL) {
            addImports(paswrap_impl.import, "paswrapincheck", p);
            num_exceptions += addThrows(throws_hash, "paswrapincheck", p);
            Replaceall(tm, "$input", arg);
            Printv(incheck, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapincheck:next");
          } else {
            p = nextSibling(p);
          }

        }
      }

      Printv(rawcall, pasraw_module_name, ".", func_name, "(", NIL);
      /* Arguments to the raw C function */
      {
        bool gencomma = false;
        Parm *p = l;
        while (p != NIL) {
          if (gencomma) {
            Printf(rawcall, ", ");
          }
          gencomma = true;
          addImports(paswrap_impl.import, "paswrapargraw", p);
          num_exceptions += addThrows(throws_hash, "paswrapargraw", p);

          String *arg = Getattr(p, "autoname");
          String *qualarg = NewString("");
          if (!isInParam(p)) {
            String *tmparg = Getattr(p, "tmap:paswrapoutname");
            if (tmparg != NIL) {
              arg = tmparg;
            }
            if (multiretval /*&& isOutParam(p) - automatically fulfilled */ ) {
              Printf(qualarg, "%s.", result_name);
            }
          }
          Append(qualarg, arg);
          Setattr(p, "pasoutarg", qualarg);

          String *tm = Getattr(p, "tmap:paswrapargraw");
          if (tm != NIL) {
            Replaceall(tm, "$input", arg);
            Replaceall(tm, "$output", qualarg);
            Printv(rawcall, tm, NIL);
            p = Getattr(p, "tmap:paswrapargraw:next");
          } else {
            //Printv(rawcall, Getattr(p,"lname"), NIL);
            Printv(rawcall, qualarg, NIL);
            p = nextSibling(p);
          }
          Delete(qualarg);
        }
      }
      Printf(rawcall, ")");

      /* Check for error codes and integrity of results */
      {
        String *tm = getMappedTypeNew(n, "paswrapretcheck", "", false);
        if (tm != NIL) {
          addImports(paswrap_impl.import, "paswrapretcheck", n);
          num_exceptions += addThrows(throws_hash, "paswrapretcheck", n);
          Printv(outcheck, tm, "\n", NIL);
        }

        Parm *p = l;
        while (p != NIL) {
          tm = Getattr(p, "tmap:paswrapoutcheck");
          if (tm != NIL) {
            String *arg = Getattr(p, "autoname");
            String *outarg = Getattr(p, "pasoutarg");
            addImports(paswrap_impl.import, "paswrapoutcheck", p);
            num_exceptions += addThrows(throws_hash, "paswrapoutcheck", p);
            //substituteClassname(Getattr(p,"type"), tm);
            Replaceall(tm, "$input", arg);
            Replaceall(tm, "$output", outarg);
            Printv(outcheck, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapoutcheck:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Convert the results to Pascal data structures and
      put them in the record prepared for returning */
      {
        /* paswrapretconv is processed
        when it is clear if there is some output conversion and checking code */
        Parm *p = l;
        while (p != NIL) {
          String *tm = Getattr(p, "tmap:paswrapoutconv");
          if (tm != NIL) {
            String *arg = Getattr(p, "autoname");
            String *outarg = Getattr(p, "pasoutarg");
            addImports(paswrap_impl.import, "paswrapoutconv", n);
            num_exceptions += addThrows(throws_hash, "paswrapoutconv", p);
            //substituteClassname(Getattr(p,"type"), tm);
            Replaceall(tm, "$input", arg);
            Replaceall(tm, "$output", outarg);
            Printf(storeout, "%s := %s;\n", outarg, tm);
            p = Getattr(p, "tmap:paswrapoutconv:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      /* Generate cleanup code */
      {
        Parm *p = l;
        while (p != NIL) {
          String *tm = Getattr(p, "tmap:paswrapfreearg");
          if (tm != NIL) {
            String *arg = Getattr(p, "autoname");
            String *outarg = Getattr(p, "pasoutarg");
            addImports(paswrap_impl.import, "paswrapfreearg", p);
            num_exceptions += addThrows(throws_hash, "paswrapfreearg", p);
            //substituteClassname(Getattr(p,"type"), tm);
            Replaceall(tm, "$input", arg);
            Replaceall(tm, "$output", outarg);
            Printv(cleanup, tm, "\n", NIL);
            p = Getattr(p, "tmap:paswrapfreearg:next");
          } else {
            p = nextSibling(p);
          }
        }
      }

      {
        /* Currently I don't know how a typemap similar to the original 'out' typemap
        could help returning the return value. */
        /* Receive result from call to raw library function */
        if (!has_return_raw) {
          /*
          rawcall(arg1);
          result.val := arg1;
          RETURN result;
          */
          /*
          rawcall(arg1);
          RETURN arg1;
          */
          Printf(reccall, "%s;\n", rawcall);

          if (hasContent(result_name)) {
            Printf(outarg, "RETURN %s;\n", result_name);
          }
        } else {
          /*
          arg0 := rawcall(arg1);
          result.ret := Convert(arg0);
          result.val := arg1;
          RETURN result;
          */
          /*
          arg0 := rawcall();
          RETURN Convert(arg0);
          */
          /*
          RETURN rawcall();
          */
          String *return_raw = getMappedTypeNew(n, "paswrapretraw", "", false);
          String *return_conv = getMappedTypeNew(n, "paswrapretconv", "", false);

          /* immediate RETURN would skip result checking */
          if ((hasContent(outcheck) || hasContent(storeout)
            || hasContent(cleanup)) && (!hasContent(result_name))
            && (return_raw == NIL)) {
              Printv(result_name, "result", NIL);
              Printf(local_variables, "%s: %s;\n", result_name, result_paswraptype);
          }

          String *result_lvalue = Copy(result_name);
          if (multiretval) {
            Printf(result_lvalue, ".%s", result_return);
          }
          if (return_raw != NIL) {
            Printf(reccall, "%s := %s;\n", return_raw, rawcall);
          } else if (hasContent(result_name)) {
            Printf(reccall, "%s := %s;\n", result_lvalue, rawcall);
          } else {
            Printf(outarg, "RETURN %s;\n", rawcall);
          }
          if (return_conv != NIL) {
            addImports(paswrap_impl.import, "paswrapretconv", n);
            num_exceptions += addThrows(throws_hash, "paswrapretconv", n);
            if (hasContent(result_name)) {
              Printf(reccall, "%s := %s;\n", result_lvalue, return_conv);
              Printf(outarg, "RETURN %s;\n", result_name);
            } else {
              Printf(outarg, "RETURN %s;\n", return_conv);
            }
          } else {
            if (hasContent(result_name)) {
              Printf(outarg, "RETURN %s;\n", result_name);
            }
          }
        }
      }

      /* Create procedure header */
      {
        String *header = NewStringf("PROCEDURE %s (%s)",
          func_name, arguments);

        if ((num_returns > 0) || multiretval) {
          Printf(header, ": %s", result_paswraptype);
        }
        generateThrowsClause(throws_hash, header);

        Append(function_code, header);

        paswrap_intf.enterBlock(no_block);
        Printf(paswrap_intf.f, "%s;\n\n", header, NIL);
      }

      {
        String *body = NewStringf("%s%s%s%s%s",
          incheck,
          setup,
          reccall,
          outcheck,
          storeout);

        String *exc_handler;
        if (hasContent(cleanup) && (num_exceptions > 0)) {
          exc_handler = NewStringf("TRY\n%sFINALLY\n%sEND;\n", body, cleanup);
        } else {
          exc_handler = NewStringf("%s%s", body, cleanup);
        }

        Printf(function_code, " =\n%s%s%s%sBEGIN\n%s%sEND %s;\n\n",
          hasContent(local_constants) ? "CONST\n" : "", local_constants,
          hasContent(local_variables) ? "VAR\n" : "", local_variables, exc_handler, outarg, func_name);

        Delete(exc_handler);
        Delete(body);
      }

      paswrap_impl.enterBlock(no_block);
      if (proxy_flag && global_variable_flag) {
        // Properties
        if (setter_flag) {
          // Setter method
          String *tm = getMappedTypeNew(n, "pasvarin", "");
          if (tm != NIL) {
            if (GetFlag(n, "feature:new")) {
              Replaceall(tm, "$owner", "true");
            } else {
              Replaceall(tm, "$owner", "false");
            }
            substituteClassname(t, tm);
            Replaceall(tm, "$rawcall", rawcall);
            Replaceall(tm, "$vartype", variable_type);  /* $type is already replaced by some super class */
            Replaceall(tm, "$var", variable_name);
            Printf(paswrap_impl.f, "%s", tm);
          }
        } else {
          // Getter method
          String *tm = getMappedTypeNew(n, "pasvarout", "");
          if (tm != NIL) {
            if (GetFlag(n, "feature:new"))
              Replaceall(tm, "$owner", "true");
            else
              Replaceall(tm, "$owner", "false");
            substituteClassname(t, tm);
            Replaceall(tm, "$rawcall", rawcall);
            Replaceall(tm, "$vartype", variable_type);
            Replaceall(tm, "$var", variable_name);
            Printf(paswrap_impl.f, "%s", tm);
          }
        }
      } else {
        // Normal function call
        Printv(paswrap_impl.f, function_code, NIL);
      }

      Delete(arguments);
      Delete(return_variables);
      Delete(local_variables);
      Delete(local_constants);
      Delete(outarg);
      Delete(incheck);
      Delete(outcheck);
      Delete(setup);
      Delete(cleanup);
      Delete(storeout);
      Delete(function_code);
      Delete(result_name);
      Delete(result_paswraptype);
      Delete(reccall);
      Delete(rawcall);
      Delete(throws_hash);

      TRACE_FUNC_EXIT;
   }

 
    /* -----------------------------------------------------------------------------
    * substituteClassname()
    *
    * Substitute $pasclassname with the proxy class name for classes/structs/unions that SWIG knows about.
    * Otherwise use the $descriptor name for the Pascal class name. Note that the $&pasclassname substitution
    * is the same as a $&descriptor substitution, ie one pointer added to descriptor name.
    * Inputs:
    *   pt - parameter type
    *   tm - paswraptype typemap
    * Outputs:
    *   tm - paswraptype typemap with $pasclassname substitution
    * Return:
    *   substitution_performed - flag indicating if a substitution was performed
    * ----------------------------------------------------------------------------- */

    bool substituteClassname(SwigType *pt, String *tm) {
      bool substitution_performed = false;

      //char * xx;
      //xx = Char(tm);

      if (Strstr(tm, "$pasclassname") || Strstr(tm, "$&pasclassname")) {
        String *classname = getProxyName(pt);
        if (classname) {
          Replaceall(tm, "$&pasclassname", classname);  // getProxyName() works for pointers to classes too
          Replaceall(tm, "$pasclassname", classname);
        } else {      // use $descriptor if SWIG does not know anything about this type. Note that any typedefs are resolved.
          String *descriptor = NULL;
          SwigType *type = Copy(SwigType_typedef_resolve_all(pt));

          if (Strstr(tm, "$&pasclassname")) {
            SwigType_add_pointer(type);
            descriptor = NewStringf("SWIGTYPE%s", SwigType_manglestr(type));
            Replaceall(tm, "$&pasclassname", descriptor);
          } else {    // $pasclassname
            descriptor = NewStringf("SWIGTYPE%s", SwigType_manglestr(type));
            Replaceall(tm, "$pasclassname", descriptor);
          }

          // Add to hash table so that the type wrapper classes can be created later
          Setattr(swig_types_hash, descriptor, type);
          Delete(descriptor);
          Delete(type);
        }
        substitution_performed = true;
      }
      return substitution_performed;
    }

    /* -----------------------------------------------------------------------------
    * makeParameterName()
    *
    * Inputs: 
    *   n - Node
    *   p - parameter node
    *   arg_num - parameter argument number
    * Return:
    *   arg - a unique parameter name
    * ----------------------------------------------------------------------------- */

    String *makeParameterName(Node *n, Parm *p, int arg_num) {

      // Use C parameter name unless it is a duplicate or an empty parameter name
      String *pn = Getattr(p, "name");
      //  Replace(pn ,"::","_", DOH_REPLACE_ANY);
      int count = 0;
      ParmList *plist = Getattr(n, "parms");
      while (plist) {
        if ((Cmp(pn, Getattr(plist, "name")) == 0))
          count++;
        plist = nextSibling(plist);
      }
      String *arg = (!pn || (count > 1)) ? NewStringf("arg%d",
        arg_num) : Copy(Getattr(p,
        "name"));

      return arg;
    }

    /* -----------------------------------------------------------------------------
    * attachParameterNames()
    *
    * Inputs: 
    *   n      - Node of a function declaration
    *   tmid   - attribute name for overriding C argument names,
    *              e.g. "tmap:paswrapinname",
    *              don't forget to attach the mapped types before
    *   nameid - attribute for attaching the names,
    *              e.g. "freepascal:inname"
    *   fmt    - format for the argument name containing %d
    *              e.g. "arg%d"
    * ----------------------------------------------------------------------------- */

    void attachParameterNames(Node *n, const char *tmid, const char *nameid, const char *fmt) {
      /* Use C parameter name if present and unique,
      otherwise create an 'arg%d' name */
      Hash *hash = NewHash();
      Parm *p = Getattr(n, "parms");
      int count = 0;
      while (p != NIL) {
        String *name = Getattr(p, tmid);
        if (name == NIL) {
          name = Getattr(p, "name");
        }
        String *newname;
        if ((!hasContent(name)) || (Getattr(hash, name) != NIL) || Cmp("c:wrapname", nameid) == 0) {
          newname = NewStringf(fmt, count);
        } else {
          newname = Copy(name);
        }

        Replace(newname,"::","_", DOH_REPLACE_ANY);  
        
        String* newpasname = getPascalName(n);
        Delete(newname);

        if (1 == Setattr(hash, newpasname, "1")) {
          Swig_warning(WARN_FREEPASCAL_DOUBLE_ID, input_file, line_number, "Argument '%s' twice.\n", newpasname);
        }
        Setattr(p, nameid, newpasname);
        p = nextSibling(p);
        count++;
      }
      Delete(hash);
    }

    /* -----------------------------------------------------------------------------
    * createPASSignature()
    *
    * Create signature of PAS wrapper procedure
    * Call attachParameterNames and attach mapped types before!
    *   paswrapintype, paswrapinmode, paswrapindefault
    * ----------------------------------------------------------------------------- */

    String *createPasSignature(Node *n) {
      String *arguments = NewString("");
      Parm *p = skipIgnored(Getattr(n, "parms"), "paswrapintype");
      writeArgState state;
      while (p != NIL) {

        /* Get the PAS parameter type */
        String *tm = getMappedType(p, "paswrapintype");
        if (tm != NIL) {
          if (isInParam(p)) {
            addImports(paswrap_intf.import, "paswrapintype", p);
            addImports(paswrap_impl.import, "paswrapintype", p);
            String *mode = Getattr(p, "tmap:paswrapinmode");
            String *deflt = Getattr(p, "tmap:paswrapindefault");
            String *arg = Getattr(p, "autoname");
            SwigType *pt = Getattr(p, "type");
            substituteClassname(pt, tm);  /* do we need this ? */

            writeArg(p, arguments, state, mode, arg, tm, deflt);
          }
          p = skipIgnored(Getattr(p, "tmap:paswrapintype:next"), "paswrapintype");
        } else {
          p = nextSibling(p);
        }
      }
      writeArg(n, arguments, state, NIL, NIL, NIL, NIL);
      return (arguments);
    }

    /* not used any longer
    - try SwigType_str if required again */
#if 0
    /* -----------------------------------------------------------------------------
    * createCSignature()
    *
    * Create signature of C function
    * ----------------------------------------------------------------------------- */

    String *createCSignature(Node *n) {
      String *arguments = NewString("");
      bool gencomma = false;
      Node *p;
      for (p = Getattr(n, "parms"); p != NIL; p = nextSibling(p)) {
        if (gencomma) {
          Append(arguments, ",");
        }
        gencomma = true;
        String *type = Getattr(p, "type");
        String *ctype = getMappedTypeNew(type, "ctype");
        Append(arguments, ctype);
      }
      return arguments;
    }
#endif

    /* -----------------------------------------------------------------------------
    * emitTypeWrapperClass()
    * ----------------------------------------------------------------------------- */

    void emitTypeWrapperClass(String *classname, SwigType *type, Node *n) {
      TRACE_FUNC_ENTR;
      String *filen = NewStringf("%s%s.%s", Swig_file_dirname(outfile), classname, outfile_ext);
      File *f_swigtype = NewFile(filen, "w", SWIG_output_files());
      if (!f_swigtype) {
        FileErrorDisplay(filen);
        SWIG_exit(EXIT_FAILURE);
      }
      String *swigtype = NewString("");

      // Emit banner name
      emitBanner(f_swigtype);

      const String *pasclassname = typemapLookup(n,"paswraptype", classname, WARN_NONE);
      
      // Pure Pascal baseclass and interfaces
      const String *pure_baseclass = typemapLookup(n,"pasbase", type, WARN_NONE);
      const String *pure_interfaces = typemapLookup(n,"pasinterfaces", type, WARN_NONE);

      // Emit the class
      Printv(swigtype, typemapLookup(n,"pasimports", type, WARN_NONE),  // Import statements
        "\n", typemapLookup(n,"pasclassmodifiers", type, WARN_FREEPASCAL_TYPEMAP_CLASSMOD_UNDEF),  // Class modifiers
        " class $pasclassname",  // Class name and bases
        *Char(pure_baseclass) ? " : " : "", pure_baseclass, *Char(pure_interfaces) ?  // Interfaces
        " : " : "", pure_interfaces, " {\n", "  private IntPtr swigCPtr;\n", "\n", "  ", typemapLookup(n,"pasptrconstructormodifiers", type, WARN_FREEPASCAL_TYPEMAP_PTRCONSTMOD_UNDEF),  // pointer constructor modifiers
        " $pasclassname(IntPtr cPtr, bool bFutureUse) {\n",  // Constructor used for wrapping pointers
        "    swigCPtr = cPtr;\n", "  }\n", "\n", "  protected $pasclassname() {\n",  // Default constructor
        "    swigCPtr = IntPtr.Zero;\n", "  }\n", typemapLookup(n,"pasgetcptr", type, WARN_FREEPASCAL_TYPEMAP_GETCPTR_UNDEF),  // getCPtr method
        typemapLookup(n,"pascode", type, WARN_NONE),  // extra Pascal code
        "}\n", "\n", NIL);

      Replaceall(swigtype, "$pasclassname", pasclassname);
      Printv(f_swigtype, swigtype, NIL);

      Delete(f_swigtype);
      Delete(filen);
      Delete(swigtype);
      TRACE_FUNC_EXIT;
    }

    /* -----------------------------------------------------------------------------
    * typemapLookup(n,)
    * ----------------------------------------------------------------------------- */

    //const String *typemapLookup(n,const String *op, String *type, int warning, Node *typemap_attributes = NULL) {

  /* -----------------------------------------------------------------------------
   * typemapLookup(n,)
   * n - for input only and must contain info for Getfile(n) and Getline(n) to work
   * tmap_method - typemap method name
   * type - typemap type to lookup
   * warning - warning number to issue if no typemaps found
   * typemap_attributes - the typemap attributes are attached to this node and will 
   *   also be used for temporary storage if non null
   * return is never NULL, unlike Swig_typemap_lookup()
   * ----------------------------------------------------------------------------- */

  const String *typemapLookup(Node *n, const_String_or_char_ptr tmap_method, SwigType *type, int warning, Node *typemap_attributes = 0) {
    TRACE_FUNC_ENTR;
    
    /*fprintf(stderr, 
       "n:%s, tmap_method:%s, type:%s, warning:%d, typemap_attributes:%s\n",
       (n)?"node":"null", Char(tmap_method), (type)?"type":"null", warning, Char(typemap_attributes));
    */
    
    String *file = NewString("n/a");
    int line = -1;
    Node *node = !typemap_attributes ? NewHash() : typemap_attributes;
    Setattr(node, "type", type);
    
    if (n) {
      file = Getfile(n);
      line = Getline(n);
  //fprintf(stderr, "Node %s %s:%d\n", Char(name), Char(file), line);
      Setfile(node, file);
      Setline(node, line);
  //fprintf(stderr, "Node set\n");
    }
    
    const String *tm = Swig_typemap_lookup(tmap_method, node, "", 0);
    
    //fprintf(stderr, "tm:%s\n",Char(tm));
    if (!tm) {
      tm = empty_string;
      if (warning != WARN_NONE){
        Swig_warning(warning, file, line, "No %s typemap defined for %s\n", tmap_method, (type)?SwigType_str(type, 0):"(null)");
      }
    }
    if (!typemap_attributes)
      Delete(node);

   TRACE_FUNC_EXIT;
   return tm;
  }


    /* -----------------------------------------------------------------------------
    * addThrows()
    *
    * Add all exceptions to a hash that are associated with the 'typemap'.
    * Return number the number of these exceptions.
    * ----------------------------------------------------------------------------- */

    int addThrows(Hash *throws_hash, const String *typemap, Node *parameter) {
      // Get the comma separated throws clause - held in "throws" attribute in the typemap passed in
      int len = 0;
      String *throws_attribute = NewStringf("%s:throws", typemap);

      addImports(paswrap_intf.import, throws_attribute, parameter);
      addImports(paswrap_impl.import, throws_attribute, parameter);

      String *throws = getMappedTypeNew(parameter, Char(throws_attribute), "", false);
      //printf("got exceptions %s for %s\n", Char(throws), Char(throws_attribute));

      if (throws) {
        // Put the exception classes in the throws clause into a temporary List
        List *temp_classes_list = Split(throws, ',', INT_MAX);
        len = Len(temp_classes_list);

        // Add the exception classes to the node throws list, but don't duplicate if already in list
        if (temp_classes_list /*&& hasContent(temp_classes_list) */ ) {
          for (Iterator cls = First(temp_classes_list); cls.item != NIL; cls = Next(cls)) {
            String *exception_class = NewString(cls.item);
            Replaceall(exception_class, " ", "");  // remove spaces
            Replaceall(exception_class, "\t", "");  // remove tabs
            if (hasContent(exception_class)) {
              // $pasclassname substitution
              SwigType *pt = Getattr(parameter, "type");
              substituteClassname(pt, exception_class);
              // Don't duplicate the exception class in the throws clause
              //printf("add exception %s\n", Char(exception_class));
              Setattr(throws_hash, exception_class, "1");
            }
            Delete(exception_class);
          }
        }
        Delete(temp_classes_list);
      }
      Delete(throws_attribute);
      return len;
    }

    /* -----------------------------------------------------------------------------
    * generateThrowsClause()
    * ----------------------------------------------------------------------------- */

    void generateThrowsClause(Hash *throws_hash, String *code) {
      // Add the throws clause into code
      if (Len(throws_hash) > 0) {
        Iterator cls = First(throws_hash);
        Printf(code, " RAISES {%s", cls.key);
        for (cls = Next(cls); cls.key != NIL; cls = Next(cls)) {
          Printf(code, ", %s", cls.key);
        }
        Printf(code, "}");
      }
    }

    /* -----------------------------------------------------------------------------
    * addImports()
    *
    * Add all imports that are needed for contents of 'typemap'.
    * ----------------------------------------------------------------------------- */

    void addImports(Hash *imports_hash, const String *typemap, Node *node) {
      // Get the comma separated throws clause - held in "throws" attribute in the typemap passed in
      String *imports_attribute = NewStringf("%s:import", typemap);
      String *imports = getMappedTypeNew(node, Char(imports_attribute), "", false);
      //printf("got imports %s for %s\n", Char(imports), Char(imports_attribute));

      if (imports != NIL) {
        List *import_list = Split(imports, ',', INT_MAX);

        // Add the exception classes to the node imports list, but don't duplicate if already in list
        if (import_list != NIL) {
          for (Iterator imp = First(import_list); imp.item != NIL; imp = Next(imp)) {
            List *import_pair = Split(imp.item, ' ', 3);
            if (Len(import_pair) == 1) {
              Setattr(imports_hash, Getitem(import_pair, 0), "");
            } else if ((Len(import_pair) == 3)
              && Strcmp(Getitem(import_pair, 1), "AS") == 0) {
                Setattr(imports_hash, Getitem(import_pair, 0), Getitem(import_pair, 2));
            } else {
              Swig_warning(WARN_FREEPASCAL_BAD_IMPORT, input_file, line_number,
                "Malformed import '%s' for typemap '%s' defined for type '%s'\n", imp, typemap, SwigType_str(Getattr(node, "type"), 0));
            }
            Delete(import_pair);
          }
        }
        Delete(import_list);
      }
      Delete(imports_attribute);
    }

    /* -----------------------------------------------------------------------------
    * emitImportStatements()
    * ----------------------------------------------------------------------------- */

    void emitImportStatements(Hash *imports_hash, String *code) {
      // Add the imports statements into code
      Iterator imp = First(imports_hash);

      bool first = true;
      int c = 0;

      if (intf_uses != 0) {

        Printf(code, "uses \n");
        Printv(code, intf_uses, NIL);
        Printv(code, interface_uses, NIL);
        first = false;
        c++;
      }

      while (imp.key != NIL) {

        if (first) {
          Printf(code, "uses \n");
          Printv(code, interface_uses, NIL);
          Printf(code, "     %s", imp.key);
          first = false;
        }  
        else {
          Printf(code, ",\n");
          Printf(code, "     %s", imp.key);
        }
        c++;

        //String *imp_as = imp.item;
        /* if (hasContent(imp_as)) {
        Printf(code, " AS %s", imp_as);
        }*/
        imp = Next(imp);
      }

      if (c)
        Printf(code, ";\n");

    }
    /*----------------------------------------------------------------------
    * Pas_director_declaration()
    *
    * Generate the director class's declaration
    * e.g. "class SwigDirector_myclass : public myclass, public Swig::Director {"
    *--------------------------------------------------------------------*/

    void Pas_director_declaration(Node *n) {

      String *base = Getattr(n, "classtype");
      String *class_ctor = NewString("Swig::Director()");

      String *classname = Swig_class_name(n);
      String *directorname = NewStringf("SwigDirector_%s", classname);
      String *declaration = Swig_class_declaration(n, directorname);

      Printf(declaration, " : public %s, public Swig::Director", base);

      // Stash stuff for later.
      Setattr(n, "director:decl", declaration);
      Setattr(n, "director:ctor", class_ctor);
    }
    /* C++ director class generation */
    virtual int classDirector(Node *n)
    {
      return Language::classDirector(n);
    }

    virtual int classDirectorInit(Node *n)
    {
      Delete(none_comparison);
      none_comparison = NewString("");  // not used

      Delete(director_ctor_code);
      director_ctor_code = NewString("$director_new");

      Pas_director_declaration(n);

      Printf(f_directors_h, "%s {\n", Getattr(n, "director:decl"));
      Printf(f_directors_h, "\npublic:\n");

      /* Keep track of the director methods for this class */

      first_class_dmethod = curr_class_dmethod = n_dmethods;

      director_callback_typedefs = NewString("");
      director_callbacks = NewString("");
      director_delegate_callback = NewString("");
      director_delegate_definitions = NewString("");
      director_delegate_instances = NewString("");
      director_method_types = NewString("");
      director_connect_parms = NewString("");

      return Language::classDirectorInit(n);

      return Language::classDirectorInit(n);
    }

    /* -----------------------------------------------------------------------------
    * directorClassName()
    * ----------------------------------------------------------------------------- */

    String *directorClassName(Node *n) {
      String *dirclassname;
      const char *attrib = "director:classname";

      if (!(dirclassname = Getattr(n, attrib))) {
        String *classname = Getattr(n, "sym:name");

        dirclassname = NewStringf("SwigDirector_%s", classname);
        Setattr(n, attrib, dirclassname);
      }

      return dirclassname;
    }

    virtual int classDirectorEnd(Node *n)
    {

      int i;
      String *director_classname = directorClassName(n);

      Wrapper *w = NewWrapper();

      if (Len(director_callback_typedefs) > 0) {
        Printf(f_directors_h, "\n%s", director_callback_typedefs);
      }

      Printf(f_directors_h, "    void swig_connect_director(");

      Printf(w->def, "void %s::swig_connect_director(", director_classname);

      for (i = first_class_dmethod; i < curr_class_dmethod; ++i) {
        UpcallData *udata = Getitem(dmethods_seq, i);
        String *methid = Getattr(udata, "class_methodidx");
        String *overname = Getattr(udata, "overname");

        Printf(f_directors_h, "SWIG_Callback%s_t callback%s", methid, overname);
        Printf(w->def, "SWIG_Callback%s_t callback%s", methid, overname);
        Printf(w->code, "swig_callback%s = callback%s;\n", overname, overname);
        if (i != curr_class_dmethod - 1) {
          Printf(f_directors_h, ", ");
          Printf(w->def, ", ");
        }
      }

      Printf(f_directors_h, ");\n");
      Printf(w->def, ") {");


      if (Len(director_callback_typedefs) > 0) {
        Printf(f_directors_h, "\nprivate:\n%s", director_callbacks);
      }
      Printf(f_directors_h, "    void swig_init_callbacks();\n");
      Printf(f_directors_h, "};\n\n");
      Printf(w->code, "}\n\n");

      Printf(w->code, "void %s::swig_init_callbacks() {\n", director_classname);
      for (i = first_class_dmethod; i < curr_class_dmethod; ++i) {
        UpcallData *udata = Getitem(dmethods_seq, i);
        String *overname = Getattr(udata, "overname");
        Printf(w->code, "swig_callback%s = 0;\n", overname);
      }
      Printf(w->code, "}");

      Wrapper_print(w, f_directors);

      DelWrapper(w);


      return Language::classDirectorEnd(n);
    }

    virtual int unrollVirtualMethods(Node *n, Node *parent, List *vm, int default_director, int &virtual_destructor, int protectedbase = 0)
    {
      return Language::unrollVirtualMethods(n,parent, vm,  default_director, virtual_destructor, protectedbase);
    }
    virtual int classDirectorConstructor(Node *n)
    {
      return Language::classDirectorConstructor(n);
    }

    virtual int classDirectorDefaultConstructor(Node *n)
    {
      return Language::classDirectorDefaultConstructor(n);
    }

    virtual int classDirectorMethod(Node *n, Node *parent, String *super)

    {
      return Language::classDirectorMethod(n, parent, super);
    }

    virtual int classDirectorConstructors(Node *n)

    {
      return Language::classDirectorConstructors(n);
    }

    virtual int classDirectorDestructor(Node * /*n*/)
    {
      //return Language::classDirectorDestructor(n);
      return SWIG_OK;
    }
    virtual int classDirectorMethods(Node *n)
    {
      return Language::classDirectorMethods(n);
    }
    virtual int classDirectorDisown(Node * /*n*/)
    {
      //  return Language::classDirectorDisown(n);
      return SWIG_OK;
    }
};        /* class FREEPASCAL */

/* -----------------------------------------------------------------------------
* swig_freepascal()    - Instantiate module
* ----------------------------------------------------------------------------- */
#if 1
extern "C" Language *swig_freepascal(void) {
  return new FREEPASCAL();
}

/* -----------------------------------------------------------------------------
* Static member variables
* ----------------------------------------------------------------------------- */

const char *FREEPASCAL::usage = (char *) "\
  FreePascal Options (available with -freepascal)\n\
  -generateconst <file>   - generate code for computing numeric values of constants\n\
  -generaterename <file>  - generate suggestions for %rename\n\
  -generatetypemap <file> - generate templates for some basic typemaps\n\
  -oldvarnames    - old intermediary method names for variable wrappers\n\
  \n";

/*
-generateconst <file> - stem of the .c source file for computing the numeric values of constants\n\
-generaterename <file> - stem of the .i source file containing %rename suggestions\n\
-generatetypemap <file> - stem of the .i source file containing typemap patterns\n\
*/

#endif 

