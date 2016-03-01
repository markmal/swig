%module structmpl

%inline %{

typedef void* qptrdiff;

struct QStr
{
    static const int Constant = 0;
};

template <typename T> struct QAtomicAdditiveType
{
    typedef T AdditiveT;
    static const int AddScaleS = 1;
};

template <typename T> struct QAtomicAdditiveType<T *>
{
    typedef qptrdiff AdditiveT;
    static const int AddScaleP = sizeof(T);
};

%}

%template(QAtomicAdditiveType_int) QAtomicAdditiveType<int>;
