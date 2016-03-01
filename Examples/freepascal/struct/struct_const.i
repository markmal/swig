%module struct_const

%inline %{

template<typename T> struct QAtomicIntegerTraits { enum { IsInteger = 0 }; };

template <typename T> struct QAtomicAdditiveType
{
    typedef T AdditiveT;
    static const int AddScale = 1;
};

%}
