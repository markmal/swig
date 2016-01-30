/* -----------------------------------------------------------------------------
 * See the LICENSE file for information on copyright, usage and redistribution
 * of SWIG, and the README file for authors - http://www.swig.org/release.html.
 *
 * std_string.i
 *
 * Typemaps for std::string and const std::string&
 * These are mapped to a Pascal String and are passed around by value.
 *
 * To use non-const std::string references use the following %apply.  Note 
 * that they are passed by value.
 * %apply const std::string & {std::string &};
 * ----------------------------------------------------------------------------- */

%{
#include <string>
#if defined(_WIN32) || defined(__WIN32__)
#include <windows.h>
#endif
%}

namespace std {

%naturalvar string;

class string;

%typemap(ctype) string "char *"
%typemap(in) string  (string  temp)
%{ temp = ($*1_ltype)$input; 
  $1 = &temp; %}
%typemap(out) string  (string  temp)
%{ 
#if defined(_WIN32) || defined(__WIN32__)
	 $result = (char *)HeapAlloc(GetProcessHeap(), 0, result.size()+1);
	 strcpy($result, result.c_str()); 
#else
   $result = (char *)malloc(result.size()+sizeof(int)*2);
   ((int*)$result)[0] = 0; 
   ((int*)$result)[1] = result.size();
   strcpy($result+sizeof(int)*2, result.c_str()); 

   //*((int*)$result[0]) = 0; //reference count to 0.
   //*((int*)$result[sizeof(int)]) = length;  //length of the string.

   //strcpy($result[sizeof(int) * 2], result.c_ptr); //copy hello to the pascal string.
#endif
 %}

%typemap(pasrawintype) string "PChar"
%typemap(pasrawouttype) string "PChar"
%typemap(pasrawrettype) string "PChar"
%typemap(pasout) string "$result := $imcall"
%typemap(pasin) string "$input"
%typemap(paswraprettype) string "PChar"
%typemap(paswrapintype) string "PChar"
%typemap(paswrapouttype) string "PChar"
%typemap(paswraptype) string "PChar"


/* test named types for wrappers 
ctype - used in C to C++ wrappers
pasraw*type  - used in Pascal to C external calls
paswrap*type - used in Pascal proxy class
*/
%typemap(ctype)            Employee * "$1_type"
%typemap(pasrawtype)       Employee * "P$1_basetype"
%typemap(pasrawintype)     Employee * "P$1_basetype"
%typemap(pasrawouttype)    Employee * "P$1_basetype"
%typemap(pasrawrettype)    Employee * "P$1_basetype"
//%typemap(pasin)     Employee * "$1_basetype.CObjPtr"
//%typemap(pasout)    Employee * "$1_basetype.CObjPtr"
%typemap(paswraptype)      Employee * "$1_basetype"
%typemap(paswrapintype)    Employee * "$1_basetype"
%typemap(paswrapouttype)   Employee * "$1_basetype"
%typemap(paswraprettype)   Employee * "$1_basetype"

%typemap(pasrawtype)       EmployeeList * "P$1_basetype"
%typemap(pasrawintype)     EmployeeList * "P$1_basetype"
%typemap(pasrawouttype)    EmployeeList * "P$1_basetype"
%typemap(pasrawrettype)    EmployeeList * "P$1_basetype"
%typemap(pasgetcptr_intf)  EmployeeList * "P$1_basetype"
%typemap(pascode)  EmployeeList * "P$1_basetype"


#if 0

// string
%typemap(ctype) string "char *"
%typemap(imtype) string "string"
%typemap(cstype) string "string"

%typemap(pasdirectorin) string "$iminput"
%typemap(pasdirectorout) string "$cscall"

%typemap(in, canthrow=1) string 
%{ if (!$input) {
    SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null string", 0);
    return $null;
   }
   $1.assign($input); %}
%typemap(out) string %{ $result = SWIG_csharp_string_callback($1.c_str()); %}

%typemap(directorout, canthrow=1) string 
%{ if (!$input) {
    SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null string", 0);
    return $null;
   }
   $result.assign($input); %}

%typemap(directorin) string %{ $input = SWIG_csharp_string_callback($1.c_str()); %}

%typemap(pasin) string "$input"
%typemap(pasout, excode=SWIGEXCODE) string {
    string ret = $imcall;$excode
    return ret;
  }

%typemap(typecheck) string = char *;

%typemap(throws, canthrow=1) string
%{ SWIG_CSharpSetPendingException(SWIG_CSharpApplicationException, $1.c_str());
   return $null; %}

// const string &
%typemap(ctype) const string & "char *"
%typemap(imtype) const string & "string"
%typemap(cstype) const string & "string"

%typemap(csdirectorin) const string & "$iminput"
%typemap(csdirectorout) const string & "$cscall"

%typemap(in, canthrow=1) const string &
%{ if (!$input) {
    SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null string", 0);
    return $null;
   }
   std::string $1_str($input);
   $1 = &$1_str; %}
%typemap(out) const string & %{ $result = SWIG_csharp_string_callback($1->c_str()); %}

%typemap(csin) const string & "$csinput"
%typemap(csout, excode=SWIGEXCODE) const string & {
    string ret = $imcall;$excode
    return ret;
  }

%typemap(directorout, canthrow=1, warning=SWIGWARN_TYPEMAP_THREAD_UNSAFE_MSG) const string &
%{ if (!$input) {
    SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null string", 0);
    return $null;
   }
   /* possible thread/reentrant code problem */
   static std::string $1_str;
   $1_str = $input;
   $result = &$1_str; %}

%typemap(directorin) const string & %{ $input = SWIG_csharp_string_callback($1.c_str()); %}

%typemap(csvarin, excode=SWIGEXCODE2) const string & %{
    set {
      $imcall;$excode
    } %}
%typemap(csvarout, excode=SWIGEXCODE2) const string & %{
    get {
      string ret = $imcall;$excode
      return ret;
    } %}

%typemap(typecheck) const string & = char *;

%typemap(throws, canthrow=1) const string &
%{ SWIG_CSharpSetPendingException(SWIG_CSharpApplicationException, $1.c_str());
   return $null; %}

#endif

}

