/* -----------------------------------------------------------------------------
 * See the LICENSE file for information on copyright, usage and redistribution
 * of SWIG, and the README file for authors - http://www.swig.org/release.html.
 *
 * freepascalhead.swg
 *
 * freepascal support code
 * ----------------------------------------------------------------------------- */

%insert(runtime) %{

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
%}

#if 0
%insert(runtime) %{
/* Support for throwing Free Pascal exceptions */
typedef enum {
  SWIG_FreePascalOutOfMemoryError = 1, 
  SWIG_FreePascalIOException, 
  SWIG_FreePascalRuntimeException, 
  SWIG_FreePascalIndexOutOfBoundsException,
  SWIG_FreePascalArithmeticException,
  SWIG_FreePascalIllegalArgumentException,
  SWIG_FreePascalNullPointerException,
  SWIG_FreePascalUnknownError
} SWIG_FreePascalExceptionCodes;

typedef struct {
  SWIG_FreePascalExceptionCodes code;
  const char *FreePascal_exception;
} SWIG_FreePascalExceptions_t;

#if defined(SWIG_NOINCLUDE)
void SWIG_FreePascalThrowException(FPEnv *fpenv, SWIG_FreePascalExceptionCodes code, const char *msg);
#else
%}
%insert(runtime) {
void SWIG_FreePascalThrowException(FPEnv *fpenv, SWIG_FreePascalExceptionCodes code, const char *msg) {
  pasclass excep;
  static const SWIG_FreePascalExceptions_t FreePascal_exceptions[] = {
    { SWIG_FreePascalOutOfMemoryError, "FreePascal/lang/OutOfMemoryError" },
    { SWIG_FreePascalIOException, "FreePascal/io/IOException" },
    { SWIG_FreePascalRuntimeException, "FreePascal/lang/RuntimeException" },
    { SWIG_FreePascalIndexOutOfBoundsException, "FreePascal/lang/IndexOutOfBoundsException" },
    { SWIG_FreePascalArithmeticException, "FreePascal/lang/ArithmeticException" },
    { SWIG_FreePascalIllegalArgumentException, "FreePascal/lang/IllegalArgumentException" },
    { SWIG_FreePascalNullPointerException, "FreePascal/lang/NullPointerException" },
    { SWIG_FreePascalUnknownError,  "FreePascal/lang/UnknownError" },
    { (SWIG_FreePascalExceptionCodes)0,  "FreePascal/lang/UnknownError" } };
  const SWIG_FreePascalExceptions_t *except_ptr = FreePascal_exceptions;

  while (except_ptr->code != code && except_ptr->code)
    except_ptr++;

  FPCALL0(ExceptionClear, jenv);
  excep = JCALL1(FindClass, jenv, except_ptr->FreePascal_exception);
  if (excep)
    FPCALL2(ThrowNew, jenv, excep, msg);
}
}
%insert(runtime) %{
#endif
%}
#endif
