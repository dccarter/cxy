
#ifdef __MAIN_RETURN_TYPE__
#ifdef __MAIN_RETURNS___
__MAIN_RETURN_TYPE__ main(int argc, char *argv[]) {
#else
int main(int argc, char *argv[]) {
#endif
#ifdef __MAIN_PARAM_TYPE__
    __MAIN_PARAM_TYPE__ args = { .data = argv, .len = argc };
#define __MAIN_ARGS__ args
#else
#define __MAIN_ARGS__
#endif

#ifdef __MAIN_RAISED_TYPE__
    __MAIN_RAISED_TYPE__ ex = _main(__MAIN_ARGS__);
    if (ex.tag == 1) {
         panicUnhandledException(ex._1);
         unreachable();
    }
#ifdef __MAIN_RETURNS__
    __MAIN_RETURN_TYPE__ ret = ex._0;
    __MAIN_RAISED_TYPE_DCTOR__(&ex);
    return ret;
#else
     __MAIN_RAISED_TYPE_DCTOR__(&ex);
     return 0;
#endif
#else
#ifdef __MAIN_RETURNS__
      return _main(__MAIN_ARGS__);
#else
      _main(__MAIN_ARGS__);
      return 0;
#endif
#endif
}

#endif

void builtins__ClosureTuple_cleanup(void *ptr)
{
    struct builtins__ClosureTuple { void *_0; } *closure = ptr;
    __smart_ptr_drop(closure->_0);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#else
#error \"Unsupported compiler\"
#endif

