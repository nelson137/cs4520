#define _POSIX_C_SOURCE 200809L  // For strnlen
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/sstring.h"

bool string_valid(const char *str, const size_t length)
{
    if (str == NULL || length == 0)
        return false;
    return str[length-1] == '\0';
}

char *string_duplicate(const char *str, const size_t length)
{
    if (str == NULL || length == 0)
        return NULL;
    char *dest = malloc(sizeof(char) * length);
    if (dest != NULL) {
        strncpy(dest, str, length-1);
        dest[length-1] = '\0';
    }
    return dest;
}

bool string_equal(const char *str_a, const char *str_b, const size_t length)
{
    if (str_a == NULL || str_b == NULL || length == 0)
        return false;
    return strncmp(str_a, str_b, length) == 0;
}

int string_length(const char *str, const size_t length)
{
    if (str == NULL || length == 0)
        return -1;
    return strnlen(str, length);
}

int string_tokenize(const char *str, const char *delims, const size_t str_length, char **tokens, const size_t max_token_length, const size_t requested_tokens)
{
    // Expand to the smaller of a and b
    #define MIN(_a, _b) ((_a) <= (_b) ? (_a) : (_b))
    // Evaluate to v, upper bound clamped to the maximum token size, not including NULL terminator
    #define CLAMP_TOK_SIZE(_v) MIN((_v), (max_token_length-1))

    if (tokens == NULL || str == NULL || delims == NULL)
        return 0;
    if (str_length == 0 || max_token_length == 0 || requested_tokens == 0)
        return 0;
    for (int i=0; i<requested_tokens; i++)
        if (tokens[i] == NULL)
            return -1;

    // Helper constants
    const char *str_end = str + strlen(str);
    const int delim_size = strlen(delims);

    // Variables named t_* are for a token of str
    int t_size, i = 0;
    const char *t_begin = str;
    const char *t_end = t_begin;
    for (; i<requested_tokens && t_end; i++, t_begin=t_end+delim_size) {
        // Will be NULL if delims not found (i.e. this is the last token)
        t_end = strstr(t_begin, delims);
        // If t_end is not NULL use the size of the token
        // Otherwise use the size of the current location (t_begin) to the end of str
        t_size = CLAMP_TOK_SIZE(t_end ? t_end-t_begin : str_end-t_begin);
        // Copy the token into the tokens buffer array
        strncpy(tokens[i], t_begin, t_size);
        tokens[i][t_size] = '\0';
    }

    return i;

    #undef MIN
    #undef CLAMP_TOK_SIZE
}

bool string_to_int(const char *str, int *converted_value)
{
    if (str == NULL || converted_value == NULL)
        return false;

    // Will be LONG_MIN or LONG_MAX if the conversion would be an under/overflow
    long value = strtol(str, NULL, 10);

    // Ensure that false is returned if str contains a number too large to be a long
    // or if it is small enough to be a long but too large to be an int.
    if (value != (int)value)
        return false;

    *converted_value = value;
    return true;
}
