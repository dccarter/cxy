
#pragma once
typedef union Float8Conv {
    double d;
    unsigned long long ull64;
    unsigned long long ll64;
    struct {
        unsigned long long u32_1;
        unsigned long long u32_2;
    };
} Float8Conv;

Float8Conv get();
