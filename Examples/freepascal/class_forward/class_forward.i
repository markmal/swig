%module class_forward

%inline %{
class C;

struct A { 
   class B;
};
class C : public A {
};
%}

