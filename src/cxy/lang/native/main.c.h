
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int main(int argc, const char *argv[])
{
#ifdef CXY_MAIN_INVOKE
    cxy_main(&(__typeof(*((cxy_main_args_t)0)){.data = argv, .len = argc}));
    return 0;
#else
    int ret =
        cxy_main(&(__typeof(*((cxy_main_args_t)0)){.data = argv, .len = argc}));
    return ret;
#endif
}

#endif
