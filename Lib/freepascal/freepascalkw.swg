#ifndef FREEPASCAL_FREEPASCALKW_SWG_
#define FREEPASCAL_FREEPASCALKW_SWG_

/* Warnings for Free Pascal keywords */

#define FREEPASCALKW(x) %keywordwarn("'" `x` "' is a Free Pascal keyword, renaming to " `x` "_'",rename="%s_")  `x`

#define FREEPASCALCLASSKW(x) %keywordwarn("'" `x` "' is a special method name used in the Free Pascal wrapper classes, class renamed to '" `x` "_'",%$isclass,rename="%s_") `x`

/*
   from
   http://wiki.freepascal.org/Reserved_words#Reserved_words_in_Free_Pascal

   The reserved words in Free Pascal mode include:
    Turbo Pascal mode reserved words
    Object Pascal mode reserved words
*/

/* Reserved words in Turbo Pascal */

FREEPASCALKW(and); /*Boolean operator requiring both conditions are true for the result to be true*/
FREEPASCALKW(array); /*multiple elements with the same name*/
FREEPASCALKW(asm); /*start of code written in assembly language*/
FREEPASCALKW(begin);	 /*start of a block of code*/
FREEPASCALKW(break);	 /*exit a case statemenr*/
FREEPASCALKW(case);	 /*select a particular segement of code to execute based on a value*/
FREEPASCALKW(const);	 /*declare an identifier with a fixed value, or a variable with an initialized value*/
FREEPASCALKW(constructor);	 /*routine used to create an object*/
FREEPASCALKW(continue);	 /*skips an iteration in a for-loop and restart execution at the beginning of the loop*/
FREEPASCALKW(destructor);	 /*routine used to deallocate an object*/
FREEPASCALKW(div);	 /*integer divide operator*/
FREEPASCALKW(do);	 /*used to indicate start of a loop*/
FREEPASCALKW(downto);	 /*used in a for loop to indicate the index variable is decremented*/
FREEPASCALKW(else);	 /*used in if statement to provide an execution path when the if test fails*/
FREEPASCALKW(end);	 /*end of a block of code, a record or certain other constructs]*/
FREEPASCALKW(false);	 /*boolean value indicating a test failed; opposite of true*/
FREEPASCALKW(file);	 /*external data structure, typically stored on disc*/
FREEPASCALKW(for);	 /*loop used to increment or decrement a control variable*/
FREEPASCALKW(function);	 /*define start of a routine that returns a result value*/
FREEPASCALKW(goto);	 /*used to exit a segment of code and jump to another point*/
FREEPASCALKW(if);	 /*test a condition and perform a set of instructions based on the result*/
FREEPASCALKW(implementation);	 /*define the internal routines in unit*/
FREEPASCALKW(in);	 /*identifies elements in a collection*/
FREEPASCALKW(inline);	 /*machine code inserted directly into a routine*/
FREEPASCALKW(interface);	 /*public declarations of routines in a unit*/
FREEPASCALKW(label);	 /*defines the target jump point for a goto*/
FREEPASCALKW(mod);	 /*operator used to return the remainder of an integer division*/
FREEPASCALKW(nil);	 /*pointer value indicating the pointer does not contain a value*/
FREEPASCALKW(not);	 /*boolean operator that negates the result of a test*/
FREEPASCALKW(object);	 /*defines an object construct*/
FREEPASCALKW(of);	 /*defines the characteristics of a variable*/
FREEPASCALKW(on);	 /**/
FREEPASCALKW(operator);	 /*defines a routine used to implement an operator*/
FREEPASCALKW(or);	 /*boolean operator which allows either of two choices to be used*/
FREEPASCALKW(packed);	 /*indicates the elements of an array are to use less space (this keyword is primarily for compatibility with older programs as packing of array elements is generally automatic);*/
FREEPASCALKW(procedure);	 /*define start of a routine that does not return a result value*/
FREEPASCALKW(program);	 /*defines start of an application. This keyword is usually optional.*/
FREEPASCALKW(record);	 /*group a series of variables under a single name*/
FREEPASCALKW(repeat);	 /*loop through a section of code through an until statement as long as the result of the test is true*/
FREEPASCALKW(set);	 /*group a collection*/
FREEPASCALKW(shl);	 /*operator to shift a value to the left; equivalent to multiplying by a power of 2*/
FREEPASCALKW(shr);	 /*operator to shift a value to the right; equivalent to dividing by a power of 2*/
FREEPASCALKW(string);	 /*declares a variable that contains multiple characters*/
FREEPASCALKW(then);	 /*indicates start of code in an if test*/
FREEPASCALKW(to);	 /*indicates a for variable is to be incremented*/
FREEPASCALKW(true);	 /*boolean value indicating a test succeeded; opposite of false*/
FREEPASCALKW(type);	 /*declares kinds of records or new classes of variables*/
FREEPASCALKW(unit);	 /*separately compiled module*/
FREEPASCALKW(until);	 /*indicates end test of a repeat statement*/
FREEPASCALKW(uses);	 /*names units this program or unit refers to*/
FREEPASCALKW(var);	 /*declare variables*/
FREEPASCALKW(while);	 /*test a value and if true, loop through a section of code*/
FREEPASCALKW(with);	 /*reference the internal variables within a record without having to refer to the record itself*/
FREEPASCALKW(xor);	 /*boolean operator used to invert an or test */

/* Reserved words in Object Pascal */

FREEPASCALKW(as);
FREEPASCALKW(class);
FREEPASCALKW(dispose);
FREEPASCALKW(except);
FREEPASCALKW(exit);
FREEPASCALKW(exports);
FREEPASCALKW(finalization);
FREEPASCALKW(finally);
FREEPASCALKW(inherited);
FREEPASCALKW(initialization);
FREEPASCALKW(is);
FREEPASCALKW(library);
FREEPASCALKW(new);
FREEPASCALKW(out);
FREEPASCALKW(property);
FREEPASCALKW(raise);
FREEPASCALKW(self);
FREEPASCALKW(threadvar);
FREEPASCALKW(try);

/* Modifiers (directives) */

FREEPASCALKW(absolute);
FREEPASCALKW(abstract);
FREEPASCALKW(alias);
FREEPASCALKW(assembler);
FREEPASCALKW(cdecl);
FREEPASCALKW(cppdecl);
FREEPASCALKW(default);
FREEPASCALKW(export);
FREEPASCALKW(external);
FREEPASCALKW(forward);
FREEPASCALKW(index);
FREEPASCALKW(local);
FREEPASCALKW(name);
FREEPASCALKW(nostackframe);
FREEPASCALKW(oldfpccall);
FREEPASCALKW(override);
FREEPASCALKW(pascal);
FREEPASCALKW(private);
FREEPASCALKW(protected);
FREEPASCALKW(public);
FREEPASCALKW(published);
FREEPASCALKW(read);
FREEPASCALKW(register);
FREEPASCALKW(reintroduce);
FREEPASCALKW(safecall);
FREEPASCALKW(softfloat);
FREEPASCALKW(stdcall);
FREEPASCALKW(virtual);
FREEPASCALKW(write);


FREEPASCALCLASSKW(create);
FREEPASCALCLASSKW(destroy);

#undef FREEPASCALKW

#endif //FREEPASCAL_FREEPASCALKW_SWG_
