/*
Copyright 2024 Vlad Mesco

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Apparently memmem is a GNU thing.  // I want this to be portable.
// While there, I also wanted a function to search *backwards* through memory
// So here's a basic KMP implementation.
//
// This could be improved by have a precompiled version where you persist
// T, but let's keep things simple.

#include <stdlib.h>
#include <string.h>

#define MEMMAX(A, B) ((A) > (B) ? (A) : (B))

#define masked_equals(b1, b2, mask) (((b1)&(mask)) == ((b2)&(mask)))

/** memsearch
  * 
  * KMP algorithm implementation for binary strings.
  *
  * haystack[nhaystack]     the long string to search in
  * needle[nneedle]         the short string to look up
  *
  * Returns a pointer to the first occurrence of needle
  * in haystack, or NULL.
  *
  * If haystack or needle are empty or NULL, returns NULL.
  *
  * If haystack is shorted than needle, returns NULL.
  */
void*
memsearch(
        void* restrict vhaystack,
        size_t nhaystack,
        void* restrict vneedle,
        size_t nneedle
        )
{
    unsigned char* haystack = vhaystack;
    unsigned char* needle = vneedle;
    unsigned char* rval = NULL;

    // sanity
    if(!nhaystack || !haystack
    || !nneedle || !needle
    || nneedle > nhaystack)
        return rval;

    // T goes one beyond, because lprefix + lprefixmatched may = nneedle,
    // and we need to write to that position :-)
    // use native ints, as I seriously doubt anyone will type in 
    // more than 4 billion characters.
    int* T = malloc(sizeof(int) * (nneedle + 1)); // TODO use a thread_local 64 byte table for small search strings to save us a call to malloc/free. Apparently thread_local is in C11 so bleh, I don't want to deal with conditional compilation and whatnot
    memset(T, -1, sizeof(int) * (nneedle + 1));

    // build failure function table.
    //
    // we need to find prefix matches within the string itself.
    // think of it as an autocorrelation of sorts.
    //
    // -1 means we can skip everything we've checked so far,
    //    including the current position
    //  0 means restart search from current position;
    //  N means we've matched a prefix of length N, so
    //    restart search from pHaystack[-N] and needle[N]
    for(size_t d = 1; d < nneedle; ++d) {
        int i = 0;
        while(i < nneedle - d) {
            if(needle[i] == needle[d + i]) ++i;
            else break;
        }
        T[d + i] = MEMMAX(T[d + i], i);
    }

    // search
    size_t m = 0;
    size_t i = 0;
    while(m + i < nhaystack) {
        if(haystack[m + i] == needle[i]) {
            ++i;
            if(i == nneedle) {
                rval = &haystack[m];
                goto END;
            } else {
                continue;
            }
        }

        // miss. Check the failure function lookup table
        int d = T[i];

        // -1 means we never wrote to it, which means that
        // we did not hit any prefix whatsoever
        if(d < 0) {
            // m skips entire searched space
            m = m + i + 1;
            // i restarts from 0
            i = 0;
        } else {
            // otherwise, we have hit a prefix.
            // m advances by i and backtracks d (matched prefix length)
            // i is d, because we've hit a prefix of length d
            m = m + i - d;
            i = d;
        }

    }

    // done
END:
    free(T);
    return rval;
}

/** rmemsearch
  * 
  * KMP algorithm implementation for binary strings, but backwards!
  *
  * haystack[nhaystack]     the long string to search in
  * needle[nneedle]         the short string to look up
  *
  * Returns a pointer to the last occurrence of needle
  * in haystack if found, such that:
  *      memcmp(rmemsearch(h, nh, n, nn), n, nn) == 0
  * ...meaning, the returned pointer points at the start
  * of needle and not the end. Add nneedle-1 to get the other end.
  *
  * Returns NULL if needle is not found in haystack.
  *
  * If haystack or needle are empty or NULL, returns NULL.
  *
  * If haystack is shorted than needle, returns NULL.
  */
void*
rmemsearch(
        void* restrict vfhaystack,
        size_t nhaystack,
        void* restrict vfneedle,
        size_t nneedle
        )
{
    // could this whole code be refactored or macro-ised to not look
    // like a big cut-paste job? Yes. Am I lazy? Also yes.
    //
    // The difference for those who aren't keen eyed is that we start
    // from the end (both needle and haystack) and we use negative offsets
    // everywhere. The return value also must subtract nneedle to get you
    // to the start of the match
    unsigned char* fhaystack = vfhaystack;
    unsigned char* fneedle = vfneedle;
    unsigned char* rval = NULL;

    // sanity
    if(!nhaystack || !fhaystack
    || !nneedle || !fneedle
    || nneedle > nhaystack)
        return rval;

    unsigned char* haystack = fhaystack + nhaystack - 1;
    unsigned char* needle = fneedle + nneedle - 1;

    int* T = malloc(sizeof(int) * (nneedle + 1));
    memset(T, -1, sizeof(int) * (nneedle + 1));

    // build table
    for(size_t d = 1; d < nneedle; ++d) {
        int i = 0;
        while(i < nneedle - d) {
            if(needle[-i] == needle[-(d + i)]) ++i;
            else break;
        }
        T[d + i] = MEMMAX(T[d + 1], i);
    }

    // search
    size_t m = 0;
    size_t i = 0;
    while(m + i < nhaystack) {
        if(haystack[-(m + i)] == needle[-i]) {
            ++i;
            if(i == nneedle) {
                rval = &haystack[-m - nneedle + 1];
                goto END;
            } else {
                continue;
            }
        }

        int d = T[i];

        if(d < 0) {
            m += i + 1;
            i = 0;
        } else {
            m += i - d;
            i = d;
        }

    }

    // done
END:
    free(T);
    return rval;
}

/** bpatmemsearch
  * 
  * KMP algorithm implementation for binary strings.
  * Performs comparisons with a bitmask.
  *
  * haystack[nhaystack]     the long string to search in
  * needle[nneedle]         the short string to look up
  * mask[nneedle]           which bits to actually compare;
  *                         set = compare; reset = d/c
  *
  * Returns a pointer to the first occurrence of needle
  * in haystack, or NULL.
  *
  * If haystack or needle are empty or NULL, returns NULL.
  *
  * If haystack is shorted than needle, returns NULL.
  */
void*
bpatmemsearch(
        void* restrict vhaystack,
        size_t nhaystack,
        void* restrict vneedle,
        size_t nneedle,
        void* restrict vmask
        )
{
    unsigned char* haystack = vhaystack;
    unsigned char* needle = vneedle;
    unsigned char* mask = vmask;
    unsigned char* rval = NULL;

    // sanity
    if(!nhaystack || !haystack
    || !nneedle || !needle
    || !mask
    || nneedle > nhaystack)
        return rval;

    // T goes one beyond, because lprefix + lprefixmatched may = nneedle,
    // and we need to write to that position :-)
    // use native ints, as I seriously doubt anyone will type in 
    // more than 4 billion characters.
    int* T = malloc(sizeof(int) * (nneedle + 1)); // TODO use a thread_local 64 byte table for small search strings to save us a call to malloc/free. Apparently thread_local is in C11 so bleh, I don't want to deal with conditional compilation and whatnot
    memset(T, -1, sizeof(int) * (nneedle + 1));

    // build failure function table.
    //
    // we need to find prefix matches within the string itself.
    // think of it as an autocorrelation of sorts.
    //
    // -1 means we can skip everything we've checked so far,
    //    including the current position
    //  0 means restart search from current position;
    //  N means we've matched a prefix of length N, so
    //    restart search from pHaystack[-N] and needle[N]
    for(size_t d = 1; d < nneedle; ++d) {
        int i = 0;
        while(i < nneedle - d) {
            // unclear which mask to pick... so only do this if
            // we have an actual repeat in the pattern
            if(needle[i] == needle[d + i] && mask[i] == mask[d + i]) ++i;
            else break;
        }
        T[d + i] = MEMMAX(T[d + i], i);
    }

    // search
    size_t m = 0;
    size_t i = 0;
    while(m + i < nhaystack) {
        if(masked_equals(haystack[m + i], needle[i], mask[i])) {
            ++i;
            if(i == nneedle) {
                rval = &haystack[m];
                goto END;
            } else {
                continue;
            }
        }

        // miss. Check the failure function lookup table
        int d = T[i];

        // -1 means we never wrote to it, which means that
        // we did not hit any prefix whatsoever
        if(d < 0) {
            // m skips entire searched space
            m = m + i + 1;
            // i restarts from 0
            i = 0;
        } else {
            // otherwise, we have hit a prefix.
            // m advances by i and backtracks d (matched prefix length)
            // i is d, because we've hit a prefix of length d
            m = m + i - d;
            i = d;
        }

    }

    // done
END:
    free(T);
    return rval;
}

/** bpatrmemsearch
  * 
  * KMP algorithm implementation for binary strings, but backwards!
  * Performs comparisons with a bitmask.
  *
  * haystack[nhaystack]     the long string to search in
  * needle[nneedle]         the short string to look up
  * mask[nneedle]           which bits to actually compare;
  *                         set = compare; reset = d/c
  *
  * Returns a pointer to the last occurrence of needle
  * in haystack if found, such that:
  *      memcmp(rmemsearch(h, nh, n, nn), n, nn) == 0
  * ...meaning, the returned pointer points at the start
  * of needle and not the end. Add nneedle-1 to get the other end.
  *
  * Returns NULL if needle is not found in haystack.
  *
  * If haystack or needle are empty or NULL, returns NULL.
  *
  * If haystack is shorted than needle, returns NULL.
  */
void*
bpatrmemsearch(
        void* restrict vfhaystack,
        size_t nhaystack,
        void* restrict vfneedle,
        size_t nneedle,
        void* restrict vfmask
        )
{
    // could this whole code be refactored or macro-ised to not look
    // like a big cut-paste job? Yes. Am I lazy? Also yes.
    //
    // The difference for those who aren't keen eyed is that we start
    // from the end (both needle and haystack) and we use negative offsets
    // everywhere. The return value also must subtract nneedle to get you
    // to the start of the match
    unsigned char* fhaystack = vfhaystack;
    unsigned char* fneedle = vfneedle;
    unsigned char* fmask = vfmask;
    unsigned char* rval = NULL;

    // sanity
    if(!nhaystack || !fhaystack
    || !nneedle || !fneedle
    || !fmask
    || nneedle > nhaystack)
        return rval;

    unsigned char* haystack = fhaystack + nhaystack - 1;
    unsigned char* needle = fneedle + nneedle - 1;
    unsigned char* mask = fmask + nneedle - 1;

    int* T = malloc(sizeof(int) * (nneedle + 1));
    memset(T, -1, sizeof(int) * (nneedle + 1));

    // build table
    for(size_t d = 1; d < nneedle; ++d) {
        int i = 0;
        while(i < nneedle - d) {
            if(needle[-i] == needle[-(d + i)] && mask[-i] == mask[-(d + i)]) ++i;
            else break;
        }
        T[d + i] = MEMMAX(T[d + 1], i);
    }

    // search
    size_t m = 0;
    size_t i = 0;
    while(m + i < nhaystack) {
        if(masked_equals(haystack[-(m + i)], needle[-i], mask[-i])) {
            ++i;
            if(i == nneedle) {
                rval = &haystack[-m - nneedle + 1];
                goto END;
            } else {
                continue;
            }
        }

        int d = T[i];

        if(d < 0) {
            m += i + 1;
            i = 0;
        } else {
            m += i - d;
            i = d;
        }

    }

    // done
END:
    free(T);
    return rval;
}
