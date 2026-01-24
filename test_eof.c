#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>

static inline bool isSpaceOrPunctuation(char c)
{
    return c != '_' && (isspace(c) || ispunct(c));
}

int main() {
    printf("EOF = %d\n", EOF);
    printf("isSpaceOrPunctuation(EOF) = %d\n", isSpaceOrPunctuation(EOF));
    printf("isspace(EOF) = %d\n", isspace(EOF));
    printf("ispunct(EOF) = %d\n", ispunct(EOF));
    return 0;
}