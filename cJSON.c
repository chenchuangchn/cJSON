/*
  Copyright (c) 2009 Dave Gamble

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* cJSON */
/* JSON parser in C. */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include "cJSON.h"

/* define our own boolean type */
typedef int bool;
#define true ((bool)1)
#define false ((bool)0)

static const char *global_ep = NULL;

const char *cJSON_GetErrorPtr(void)
{
    return global_ep;
}

static cJSON_Hooks global_hooks = { malloc, free };

static char* cJSON_strdup(const char* str, const cJSON_Hooks * const hooks)
{
    size_t len = 0;
    char *copy = NULL;

    len = strlen(str) + 1;
    if (!(copy = (char*)hooks->malloc_fn(len)))
    {
        return NULL;
    }
    memcpy(copy, str, len);

    return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks)
    {
        /* Reset hooks */
        global_hooks.malloc_fn = malloc;
        global_hooks.free_fn = free;
        return;
    }

    global_hooks.malloc_fn = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    global_hooks.free_fn = (hooks->free_fn) ? hooks->free_fn : free;
}

/* Internal constructor. */
static cJSON *cJSON_New_Item(const cJSON_Hooks * const hooks)
{
    cJSON* node = (cJSON*)hooks->malloc_fn(sizeof(cJSON));
    if (node)
    {
        memset(node, '\0', sizeof(cJSON));
    }

    return node;
}

/* Delete a cJSON structure. */
void internal_cJSON_Delete(cJSON *c, const cJSON_Hooks * const hooks)
{
    cJSON *next = NULL;
    while (c)
    {
        next = c->next;
        if (!c->is_reference && c->child)
        {
            internal_cJSON_Delete(c->child, hooks);
        }
        if (!c->is_reference && (c->type == cJSON_String) && c->value.string)
        {
            hooks->free_fn(c->value.string);
        }
        if (!c->string_is_const && c->name)
        {
            hooks->free_fn(c->name);
        }
        hooks->free_fn(c);
        c = next;
    }
}
void cJSON_Delete(cJSON *c)
{
    internal_cJSON_Delete(c, &global_hooks);
}

/* Parse the input text to generate a number, and populate the result into item. */
static const char *parse_number(cJSON *item, const char *num)
{
    double number = 0;
    char *endpointer = NULL;

    number = strtod(num, &endpointer);
    if (num == endpointer)
    {
        /* parse error */
        return NULL;
    }

    item->value.number = number;
    item->type = cJSON_Number;

    return endpointer;
}

typedef struct
{
    char *buffer;
    size_t length;
    size_t offset;
} printbuffer;


#define max(a, b) ((a > b) ? a : b)

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static char* ensure(printbuffer *p, size_t needed, const cJSON_Hooks * const hooks)
{
    char *newbuffer = NULL;
    size_t newsize = 0;
    if (!p || !p->buffer)
    {
        return NULL;
    }
    needed += p->offset;
    if (needed <= p->length)
    {
        return p->buffer + p->offset;
    }

    newsize = max(p->length, needed) * 2;
    if (newsize <= needed) {
        return 0;
    }

    newbuffer = (char*)hooks->malloc_fn(newsize);
    if (!newbuffer)
    {
        hooks->free_fn(p->buffer);
        p->length = 0;
        p->buffer = NULL;

        return NULL;
    }
    if (newbuffer)
    {
        memcpy(newbuffer, p->buffer, p->length);
    }
    hooks->free_fn(p->buffer);
    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* calculate the new length of the string in a printbuffer */
static size_t update(const printbuffer *p)
{
    char *str = NULL;
    if (!p || !p->buffer)
    {
        return 0;
    }
    str = p->buffer + p->offset;

    return p->offset + strlen(str);
}

/* Render the number nicely from the given item into a string. */
static char *print_number(const cJSON *item, printbuffer *p, const cJSON_Hooks * const hooks)
{
    char *str = NULL;
    double d = item->value.number;
    /* special case for 0. */
    if (d == 0)
    {
        if (p)
        {
            str = ensure(p, 2, hooks);
        }
        else
        {
            str = (char*)hooks->malloc_fn(2);
        }
        if (str)
        {
            strcpy(str,"0");
        }
    }
    /* value is an int */
    else if ((fabs(floor(item->value.number) - d) <= DBL_EPSILON) && (d <= INT_MAX) && (d >= INT_MIN))
    {
        int value = (int)item->value.number;
        if (p)
        {
            str = ensure(p, 21, hooks);
        }
        else
        {
            /* 2^64+1 can be represented in 21 chars. */
            str = (char*)hooks->malloc_fn(21);
        }
        if (str)
        {
            sprintf(str, "%d", value);
        }
    }
    /* value is a floating point number */
    else
    {
        if (p)
        {
            /* This is a nice tradeoff. */
            str = ensure(p, 64, hooks);
        }
        else
        {
            /* This is a nice tradeoff. */
            str=(char*)hooks->malloc_fn(64);
        }
        if (str)
        {
            /* This checks for NaN and Infinity */
            if ((d * 0) != 0)
            {
                sprintf(str, "null");
            }
            else if ((fabs(floor(d) - d) <= DBL_EPSILON) && (fabs(d) < 1.0e60))
            {
                sprintf(str, "%.0f", d);
            }
            else if ((fabs(d) < 1.0e-6) || (fabs(d) > 1.0e9))
            {
                sprintf(str, "%e", d);
            }
            else
            {
                sprintf(str, "%f", d);
            }
        }
    }
    return str;
}

/* parse 4 digit hexadecimal number */
static unsigned parse_hex4(const char *str)
{
    unsigned h = 0;
    /* first digit */
    if ((*str >= '0') && (*str <= '9'))
    {
        h += (*str) - '0';
    }
    else if ((*str >= 'A') && (*str <= 'F'))
    {
        h += 10 + (*str) - 'A';
    }
    else if ((*str >= 'a') && (*str <= 'f'))
    {
        h += 10 + (*str) - 'a';
    }
    else /* invalid */
    {
        return 0;
    }


    /* second digit */
    h = h << 4;
    str++;
    if ((*str >= '0') && (*str <= '9'))
    {
        h += (*str) - '0';
    }
    else if ((*str >= 'A') && (*str <= 'F'))
    {
        h += 10 + (*str) - 'A';
    }
    else if ((*str >= 'a') && (*str <= 'f'))
    {
        h += 10 + (*str) - 'a';
    }
    else /* invalid */
    {
        return 0;
    }

    /* third digit */
    h = h << 4;
    str++;
    if ((*str >= '0') && (*str <= '9'))
    {
        h += (*str) - '0';
    }
    else if ((*str >= 'A') && (*str <= 'F'))
    {
        h += 10 + (*str) - 'A';
    }
    else if ((*str >= 'a') && (*str <= 'f'))
    {
        h += 10 + (*str) - 'a';
    }
    else /* invalid */
    {
        return 0;
    }

    /* fourth digit */
    h = h << 4;
    str++;
    if ((*str >= '0') && (*str <= '9'))
    {
        h += (*str) - '0';
    }
    else if ((*str >= 'A') && (*str <= 'F'))
    {
        h += 10 + (*str) - 'A';
    }
    else if ((*str >= 'a') && (*str <= 'f'))
    {
        h += 10 + (*str) - 'a';
    }
    else /* invalid */
    {
        return 0;
    }

    return h;
}

/* first bytes of UTF8 encoding for a given length in bytes */
static const unsigned char firstByteMark[7] =
{
    0x00, /* should never happen */
    0x00, /* 0xxxxxxx */
    0xC0, /* 110xxxxx */
    0xE0, /* 1110xxxx */
    0xF0, /* 11110xxx */
    0xF8,
    0xFC
};

/* Parse the input text into an unescaped cstring, and populate item. */
static const char *parse_string(cJSON *item, const char *str, const char **ep, const cJSON_Hooks * const hooks)
{
    const char *ptr = str + 1;
    const char *end_ptr =str + 1;
    char *ptr2 = NULL;
    char *out = NULL;
    size_t len = 0;
    unsigned uc = 0;
    unsigned uc2 = 0;

    /* not a string! */
    if (*str != '\"')
    {
        *ep = str;
        return NULL;
    }

    while ((*end_ptr != '\"') && *end_ptr && ++len)
    {
        if (*end_ptr++ == '\\')
        {
            if (*end_ptr == '\0')
            {
                /* prevent buffer overflow when last input character is a backslash */
                return NULL;
            }
            /* Skip escaped quotes. */
            end_ptr++;
        }
    }

    /* This is at most how long we need for the string, roughly. */
    out = (char*)hooks->malloc_fn(len + 1);
    if (!out)
    {
        return NULL;
    }
    item->value.string = out; /* assign here so out will be deleted during internal_cJSON_Delete() later */
    item->type = cJSON_String;

    ptr = str + 1;
    ptr2 = out;
    /* loop through the string literal */
    while (ptr < end_ptr)
    {
        if (*ptr != '\\')
        {
            *ptr2++ = *ptr++;
        }
        /* escape sequence */
        else
        {
            ptr++;
            switch (*ptr)
            {
                case 'b':
                    *ptr2++ = '\b';
                    break;
                case 'f':
                    *ptr2++ = '\f';
                    break;
                case 'n':
                    *ptr2++ = '\n';
                    break;
                case 'r':
                    *ptr2++ = '\r';
                    break;
                case 't':
                    *ptr2++ = '\t';
                    break;
                case '\"':
                case '\\':
                case '/':
                    *ptr2++ = *ptr;
                    break;
                case 'u':
                    /* transcode utf16 to utf8. See RFC2781 and RFC3629. */
                    uc = parse_hex4(ptr + 1); /* get the unicode char. */
                    ptr += 4;
                    if (ptr >= end_ptr)
                    {
                        /* invalid */
                        *ep = str;
                        return NULL;
                    }
                    /* check for invalid. */
                    if (((uc >= 0xDC00) && (uc <= 0xDFFF)) || (uc == 0))
                    {
                        *ep = str;
                        return NULL;
                    }

                    /* UTF16 surrogate pairs. */
                    if ((uc >= 0xD800) && (uc<=0xDBFF))
                    {
                        if ((ptr + 6) > end_ptr)
                        {
                            /* invalid */
                            *ep = str;
                            return NULL;
                        }
                        if ((ptr[1] != '\\') || (ptr[2] != 'u'))
                        {
                            /* missing second-half of surrogate. */
                            *ep = str;
                            return NULL;
                        }
                        uc2 = parse_hex4(ptr + 3);
                        ptr += 6; /* \uXXXX */
                        if ((uc2 < 0xDC00) || (uc2 > 0xDFFF))
                        {
                            /* invalid second-half of surrogate. */
                            *ep = str;
                            return NULL;
                        }
                        /* calculate unicode codepoint from the surrogate pair */
                        uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                    }

                    /* encode as UTF8
                     * takes at maximum 4 bytes to encode:
                     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
                    len = 4;
                    if (uc < 0x80)
                    {
                        /* normal ascii, encoding 0xxxxxxx */
                        len = 1;
                    }
                    else if (uc < 0x800)
                    {
                        /* two bytes, encoding 110xxxxx 10xxxxxx */
                        len = 2;
                    }
                    else if (uc < 0x10000)
                    {
                        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
                        len = 3;
                    }
                    ptr2 += len;

                    switch (len) {
                        case 4:
                            /* 10xxxxxx */
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 3:
                            /* 10xxxxxx */
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 2:
                            /* 10xxxxxx */
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 1:
                            /* depending on the length in bytes this determines the
                             * encoding ofthe first UTF8 byte */
                            *--ptr2 = (uc | firstByteMark[len]);
                    }
                    ptr2 += len;
                    break;
                default:
                    *ep = str;
                    return NULL;
            }
            ptr++;
        }
    }
    *ptr2 = '\0';
    if (*ptr == '\"')
    {
        ptr++;
    }

    return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str, printbuffer *p, const cJSON_Hooks * const hooks)
{
    const char *ptr = NULL;
    char *ptr2 = NULL;
    char *out = NULL;
    size_t len = 0;
    bool flag = false;
    unsigned char token = '\0';

    /* empty string */
    if (!str)
    {
        if (p)
        {
            out = ensure(p, 3, hooks);
        }
        else
        {
            out = (char*)hooks->malloc_fn(3);
        }
        if (!out)
        {
            return NULL;
        }
        strcpy(out, "\"\"");

        return out;
    }

    /* set "flag" to 1 if something needs to be escaped */
    for (ptr = str; *ptr; ptr++)
    {
        flag |= (((*ptr > 0) && (*ptr < 32)) /* unprintable characters */
                || (*ptr == '\"') /* double quote */
                || (*ptr == '\\')) /* backslash */
            ? 1
            : 0;
    }
    /* no characters have to be escaped */
    if (!flag)
    {
        len = ptr - str;
        if (p)
        {
            out = ensure(p, len + 3, hooks);
        }
        else
        {
            out = (char*)hooks->malloc_fn(len + 3);
        }
        if (!out)
        {
            return NULL;
        }

        ptr2 = out;
        *ptr2++ = '\"';
        strcpy(ptr2, str);
        ptr2[len] = '\"';
        ptr2[len + 1] = '\0';

        return out;
    }

    ptr = str;
    /* calculate additional space that is needed for escaping */
    while ((token = *ptr) && ++len)
    {
        if (strchr("\"\\\b\f\n\r\t", token))
        {
            len++; /* +1 for the backslash */
        }
        else if (token < 32)
        {
            len += 5; /* +5 for \uXXXX */
        }
        ptr++;
    }

    if (p)
    {
        out = ensure(p, len + 3, hooks);
    }
    else
    {
        out = (char*)hooks->malloc_fn(len + 3);
    }
    if (!out)
    {
        return NULL;
    }

    ptr2 = out;
    ptr = str;
    *ptr2++ = '\"';
    /* copy the string */
    while (*ptr)
    {
        if (((unsigned char)*ptr > 31) && (*ptr != '\"') && (*ptr != '\\'))
        {
            /* normal character, copy */
            *ptr2++ = *ptr++;
        }
        else
        {
            /* character needs to be escaped */
            *ptr2++ = '\\';
            switch (token = *ptr++)
            {
                case '\\':
                    *ptr2++ = '\\';
                    break;
                case '\"':
                    *ptr2++ = '\"';
                    break;
                case '\b':
                    *ptr2++ = 'b';
                    break;
                case '\f':
                    *ptr2++ = 'f';
                    break;
                case '\n':
                    *ptr2++ = 'n';
                    break;
                case '\r':
                    *ptr2++ = 'r';
                    break;
                case '\t':
                    *ptr2++ = 't';
                    break;
                default:
                    /* escape and print as unicode codepoint */
                    sprintf(ptr2, "u%04x", token);
                    ptr2 += 5;
                    break;
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2++ = '\0';

    return out;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static char *print_string(const cJSON *item, printbuffer *p, const cJSON_Hooks * const hooks)
{
    return print_string_ptr(item->value.string, p, hooks);
}

/* Predeclare these prototypes. */
static const char *parse_value(cJSON *item, const char *value, const char **ep, const cJSON_Hooks * const hooks);
static char *print_value(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks);
static const char *parse_array(cJSON *item, const char *value, const char **ep, const cJSON_Hooks * const hooks);
static char *print_array(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks);
static const char *parse_object(cJSON *item, const char *value, const char **ep, const cJSON_Hooks * const hooks);
static char *print_object(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks);

/* Utility to jump whitespace and cr/lf */
static const char *skip(const char *in)
{
    while (in && *in && ((unsigned char)*in<=32))
    {
        in++;
    }

    return in;
}

/* Parse an object - create a new root, and populate. */
cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, bool require_null_terminated)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    const char *end = NULL;
    /* use global error pointer if no specific one was given */
    const char **ep = return_parse_end ? return_parse_end : &global_ep;
    cJSON *c = cJSON_New_Item(hooks);
    *ep = NULL;
    if (!c) /* memory fail */
    {
        return NULL;
    }

    end = parse_value(c, skip(value), ep, hooks);
    if (!end)
    {
        /* parse failure. ep is set. */
        internal_cJSON_Delete(c, hooks);
        return NULL;
    }

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated)
    {
        end = skip(end);
        if (*end)
        {
            internal_cJSON_Delete(c, hooks);
            *ep = end;
            return NULL;
        }
    }
    if (return_parse_end)
    {
        *return_parse_end = end;
    }

    return c;
}

/* Default options for cJSON_Parse */
cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

/* Render a cJSON item/entity/structure to text. */
char *cJSON_Print(const cJSON *item)
{
    return print_value(item, 0, 1, 0, &global_hooks);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return print_value(item, 0, 0, 0, &global_hooks);
}

char *cJSON_PrintBuffered(const cJSON *item, size_t prebuffer, bool fmt)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    printbuffer p;
    p.buffer = (char*)hooks->malloc_fn(prebuffer);
    if (!p.buffer)
    {
        return NULL;
    }
    p.length = prebuffer;
    p.offset = 0;

    return print_value(item, 0, fmt, &p, hooks);
}


/* Parser core - when encountering text, process appropriately. */
static const char *parse_value(cJSON *item, const char *value, const char **ep, const cJSON_Hooks * const hooks)
{
    if (!value)
    {
        /* Fail on null. */
        return NULL;
    }

    /* parse the different types of values */
    if (!strncmp(value, "null", 4))
    {
        item->type = cJSON_NULL;
        return value + 4;
    }
    if (!strncmp(value, "false", 5))
    {
        item->type = cJSON_False;
        item->value.boolean = false;
        return value + 5;
    }
    if (!strncmp(value, "true", 4))
    {
        item->type = cJSON_True;
        item->value.boolean = true;
        return value + 4;
    }
    if (*value == '\"')
    {
        return parse_string(item, value, ep, hooks);
    }
    if ((*value == '-') || ((*value >= '0') && (*value <= '9')))
    {
        return parse_number(item, value);
    }
    if (*value == '[')
    {
        return parse_array(item, value, ep, hooks);
    }
    if (*value == '{')
    {
        return parse_object(item, value, ep, hooks);
    }

    /* failure. */
    *ep = value;
    return NULL;
}

/* Render a value to text. */
static char *print_value(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    char *out = NULL;

    if (!item)
    {
        return NULL;
    }
    if (p)
    {
        switch ((item->type) & 0xFF)
        {
            case cJSON_NULL:
                out = ensure(p, 5, hooks);
                if (out)
                {
                    strcpy(out, "null");
                }
                break;
            case cJSON_False:
                out = ensure(p, 6, hooks);
                if (out)
                {
                    strcpy(out, "false");
                }
                break;
            case cJSON_True:
                out = ensure(p, 5, hooks);
                if (out)
                {
                    strcpy(out, "true");
                }
                break;
            case cJSON_Number:
                out = print_number(item, p, hooks);
                break;
            case cJSON_String:
                out = print_string(item, p, hooks);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, p, hooks);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, p, hooks);
                break;
        }
    }
    else
    {
        switch ((item->type) & 0xFF)
        {
            case cJSON_NULL:
                out = cJSON_strdup("null", hooks);
                break;
            case cJSON_False:
                out = cJSON_strdup("false", hooks);
                break;
            case cJSON_True:
                out = cJSON_strdup("true", hooks);
                break;
            case cJSON_Number:
                out = print_number(item, 0, hooks);
                break;
            case cJSON_String:
                out = print_string(item, 0, hooks);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, 0, hooks);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, 0, hooks);
                break;
        }
    }

    return out;
}

/* Build an array from input text. */
static const char *parse_array(cJSON *item,const char *value,const char **ep, const cJSON_Hooks * const hooks)
{
    cJSON *child = NULL;
    if (*value != '[')
    {
        /* not an array! */
        *ep = value;
        return NULL;
    }

    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']')
    {
        /* empty array. */
        return value + 1;
    }

    item->child = child = cJSON_New_Item(hooks);
    if (!item->child)
    {
        /* memory fail */
        return NULL;
    }
    /* skip any spacing, get the value. */
    value = skip(parse_value(child, skip(value), ep, hooks));
    if (!value)
    {
        return NULL;
    }

    /* loop through the comma separated array elements */
    while (*value == ',')
    {
        cJSON *new_item = NULL;
        if (!(new_item = cJSON_New_Item(hooks)))
        {
            /* memory fail */
            return NULL;
        }
        /* add new item to end of the linked list */
        child->next = new_item;
        child = new_item;

        /* go to the next comma */
        value = skip(parse_value(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            /* memory fail */
            return NULL;
        }
    }

    if (*value == ']')
    {
        /* end of array */
        return value + 1;
    }

    /* malformed. */
    *ep = value;

    return NULL;
}

/* Render an array to text */
static char *print_array(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    char **entries;
    char *out = NULL;
    char *ptr = NULL;
    char *ret = NULL;
    size_t len = 5;
    cJSON *child = item->child;
    size_t numentries = 0;
    size_t i = 0;
    bool fail = false;
    size_t tmplen = 0;

    /* How many entries in the array? */
    while (child)
    {
        numentries++;
        child = child->next;
    }

    /* Explicitly handle numentries == 0 */
    if (!numentries)
    {
        if (p)
        {
            out = ensure(p, 3, hooks);
        }
        else
        {
            out = (char*)hooks->malloc_fn(3);
        }
        if (out)
        {
            strcpy(out,"[]");
        }

        return out;
    }

    if (p)
    {
        /* Compose the output array. */
        /* opening square bracket */
        i = p->offset;
        ptr = ensure(p, 1, hooks);
        if (!ptr)
        {
            return NULL;
        }
        *ptr = '[';
        p->offset++;

        child = item->child;
        while (child && !fail)
        {
            print_value(child, depth + 1, fmt, p, hooks);
            p->offset = update(p);
            if (child->next)
            {
                len = fmt ? 2 : 1;
                ptr = ensure(p, len + 1, hooks);
                if (!ptr)
                {
                    return NULL;
                }
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
                p->offset += len;
            }
            child = child->next;
        }
        ptr = ensure(p, 2, hooks);
        if (!ptr)
        {
            return NULL;
        }
        *ptr++ = ']';
        *ptr = '\0';
        out = (p->buffer) + i;
    }
    else
    {
        /* Allocate an array to hold the pointers to all printed values */
        entries = (char**)hooks->malloc_fn(numentries * sizeof(char*));
        if (!entries)
        {
            return NULL;
        }
        memset(entries, '\0', numentries * sizeof(char*));

        /* Retrieve all the results: */
        child = item->child;
        while (child && !fail)
        {
            ret = print_value(child, depth + 1, fmt, 0, hooks);
            entries[i++] = ret;
            if (ret)
            {
                len += strlen(ret) + 2 + (fmt ? 1 : 0);
            }
            else
            {
                fail = true;
            }
            child = child->next;
        }

        /* If we didn't fail, try to malloc the output string */
        if (!fail)
        {
            out = (char*)hooks->malloc_fn(len);
        }
        /* If that fails, we fail. */
        if (!out)
        {
            fail = true;
        }

        /* Handle failure. */
        if (fail)
        {
            /* free all the entries in the array */
            for (i = 0; i < numentries; i++)
            {
                if (entries[i])
                {
                    hooks->free_fn(entries[i]);
                }
            }
            hooks->free_fn(entries);
            return NULL;
        }

        /* Compose the output array. */
        *out='[';
        ptr = out + 1;
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            tmplen = strlen(entries[i]);
            memcpy(ptr, entries[i], tmplen);
            ptr += tmplen;
            if (i != (numentries - 1))
            {
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
            }
            hooks->free_fn(entries[i]);
        }
        hooks->free_fn(entries);
        *ptr++ = ']';
        *ptr++ = '\0';
    }

    return out;
}

/* Build an object from the text. */
static const char *parse_object(cJSON *item, const char *value, const char **ep, const cJSON_Hooks * const hooks)
{
    cJSON *child = NULL;
    if (*value != '{')
    {
        /* not an object! */
        *ep = value;
        return NULL;
    }

    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}')
    {
        /* empty object. */
        return value + 1;
    }

    child = cJSON_New_Item(hooks);
    item->child = child;
    if (!item->child)
    {
        return NULL;
    }
    /* parse first key */
    value = skip(parse_string(child, skip(value), ep, hooks));
    if (!value)
    {
        return NULL;
    }
    /* use parsed string as key, not value */
    child->name = child->value.string;
    child->value.string = NULL;

    if (*value != ':')
    {
        /* invalid object. */
        *ep = value;
        return NULL;
    }
    /* skip any spacing, get the value. */
    value = skip(parse_value(child, skip(value + 1), ep, hooks));
    if (!value)
    {
        return NULL;
    }

    while (*value == ',')
    {
        cJSON *new_item = NULL;
        if (!(new_item = cJSON_New_Item(hooks)))
        {
            /* memory fail */
            return NULL;
        }
        /* add to linked list */
        child->next = new_item;

        child = new_item;
        value = skip(parse_string(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            return NULL;
        }

        /* use parsed string as key, not value */
        child->name = child->value.string;
        child->value.string = NULL;

        if (*value != ':')
        {
            /* invalid object. */
            *ep = value;
            return NULL;
        }
        /* skip any spacing, get the value. */
        value = skip(parse_value(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            return NULL;
        }
    }
    /* end of object */
    if (*value == '}')
    {
        return value + 1;
    }

    /* malformed */
    *ep = value;
    return NULL;
}

/* Render an object to text. */
static char *print_object(const cJSON *item, size_t depth, bool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    char **entries = NULL;
    char **names = NULL;
    char *out = NULL;
    char *ptr = NULL;
    char *ret = NULL;
    char *str = NULL;
    size_t len = 7;
    size_t i = 0;
    size_t j = 0;
    cJSON *child = item->child;
    size_t numentries = 0;
    bool fail = false;
    size_t tmplen = 0;

    /* Count the number of entries. */
    while (child)
    {
        numentries++;
        child = child->next;
    }

    /* Explicitly handle empty object case */
    if (!numentries)
    {
        if (p)
        {
            out = ensure(p, fmt ? depth + 4 : 3, hooks);
        }
        else
        {
            out = (char*)hooks->malloc_fn(fmt ? depth + 4 : 3);
        }
        if (!out)
        {
            return NULL;
        }
        ptr = out;
        *ptr++ = '{';
        if (fmt) {
            *ptr++ = '\n';
            for (i = 0; i < depth; i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';

        return out;
    }

    if (p)
    {
        /* Compose the output: */
        i = p->offset;
        len = fmt ? 2 : 1; /* fmt: {\n */
        ptr = ensure(p, len + 1, hooks);
        if (!ptr)
        {
            return NULL;
        }

        *ptr++ = '{';
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        p->offset += len;

        child = item->child;
        depth++;
        while (child)
        {
            if (fmt)
            {
                ptr = ensure(p, depth, hooks);
                if (!ptr)
                {
                    return NULL;
                }
                for (j = 0; j < depth; j++)
                {
                    *ptr++ = '\t';
                }
                p->offset += depth;
            }

            /* print key */
            print_string_ptr(child->name, p, hooks);
            p->offset = update(p);

            len = fmt ? 2 : 1;
            ptr = ensure(p, len, hooks);
            if (!ptr)
            {
                return NULL;
            }
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            p->offset+=len;

            /* print value */
            print_value(child, depth, fmt, p, hooks);
            p->offset = update(p);

            /* print comma if not last */
            len = (fmt ? 1 : 0) + (child->next ? 1 : 0);
            ptr = ensure(p, len + 1, hooks);
            if (!ptr)
            {
                return NULL;
            }
            if (child->next)
            {
                *ptr++ = ',';
            }

            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            p->offset += len;

            child = child->next;
        }

        ptr = ensure(p, fmt ? (depth + 1) : 2, hooks);
        if (!ptr)
        {
            return NULL;
        }
        if (fmt)
        {
            for (i = 0; i < (depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr = '\0';
        out = (p->buffer) + i;
    }
    else
    {
        /* Allocate space for the names and the objects */
        entries = (char**)hooks->malloc_fn(numentries * sizeof(char*));
        if (!entries)
        {
            return NULL;
        }
        names = (char**)hooks->malloc_fn(numentries * sizeof(char*));
        if (!names)
        {
            hooks->free_fn(entries);
            return NULL;
        }
        memset(entries, '\0', sizeof(char*) * numentries);
        memset(names, '\0', sizeof(char*) * numentries);

        /* Collect all the results into our arrays: */
        child = item->child;
        depth++;
        if (fmt)
        {
            len += depth;
        }
        while (child && !fail)
        {
            names[i] = str = print_string_ptr(child->name, 0, hooks); /* print key */
            entries[i++] = ret = print_value(child, depth, fmt, 0, hooks);
            if (str && ret)
            {
                len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
            }
            else
            {
                fail = true;
            }
            child = child->next;
        }

        /* Try to allocate the output string */
        if (!fail)
        {
            out = (char*)hooks->malloc_fn(len);
        }
        if (!out)
        {
            fail = true;
        }

        /* Handle failure */
        if (fail)
        {
            /* free all the printed keys and values */
            for (i = 0; i < numentries; i++)
            {
                if (names[i])
                {
                    hooks->free_fn(names[i]);
                }
                if (entries[i])
                {
                    hooks->free_fn(entries[i]);
                }
            }
            hooks->free_fn(names);
            hooks->free_fn(entries);
            return NULL;
        }

        /* Compose the output: */
        *out = '{';
        ptr = out + 1;
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            if (fmt)
            {
                for (j = 0; j < depth; j++)
                {
                    *ptr++='\t';
                }
            }
            tmplen = strlen(names[i]);
            memcpy(ptr, names[i], tmplen);
            ptr += tmplen;
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            strcpy(ptr, entries[i]);
            ptr += strlen(entries[i]);
            if (i != (numentries - 1))
            {
                *ptr++ = ',';
            }
            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            hooks->free_fn(names[i]);
            hooks->free_fn(entries[i]);
        }

        hooks->free_fn(names);
        hooks->free_fn(entries);
        if (fmt)
        {
            for (i = 0; i < (depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';
    }

    return out;
}

/* Get Array size/item / object item. */
size_t cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array->child;
    size_t i = 0;
    while(c)
    {
        i++;
        c = c->next;
    }
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, size_t item)
{
    cJSON *c = array ? array->child : NULL;
    while (c && item > 0)
    {
        item--;
        c = c->next;
    }

    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *name)
{
    cJSON *c = object ? object->child : NULL;
    while (c && strcmp(c->name, name))
    {
        c = c->next;
    }
    return c;
}

bool cJSON_HasObjectItem(const cJSON *object,const char *name)
{
    return cJSON_GetObjectItem(object, name) ? 1 : 0;
}

/* Utility for array list handling. */
static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item;
}

/* Utility for handling references. */
static cJSON *create_reference(const cJSON *item, const cJSON_Hooks * const hooks)
{
    cJSON *ref = cJSON_New_Item(hooks);
    if (!ref)
    {
        return NULL;
    }
    memcpy(ref, item, sizeof(cJSON));
    ref->name = NULL;
    ref->is_reference = true;
    ref->next = NULL;
    return ref;
}

/* Add item to array/object. */
void   cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *c = array->child;
    if (!item)
    {
        return;
    }
    if (!c)
    {
        /* list is empty, start new one */
        array->child = item;
    }
    else
    {
        /* append to the end */
        while (c->next)
        {
            c = c->next;
        }
        suffix_object(c, item);
    }
}

void   cJSON_AddItemToObject(cJSON *object, const char *name, cJSON *item)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    if (!item)
    {
        return;
    }

    /* free old key and set new one */
    if (item->name)
    {
        hooks->free_fn(item->name);
    }
    item->name = cJSON_strdup(name, hooks);

    cJSON_AddItemToArray(object,item);
}

/* Add an item to an object with constant string as key */
void   cJSON_AddItemToObjectCS(cJSON *object, const char *name, cJSON *item)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    if (!item)
    {
        return;
    }
    if (!item->string_is_const && item->name)
    {
        hooks->free_fn(item->name);
    }
    item->name = (char*)name;
    item->string_is_const = true;
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)
{
    cJSON_AddItemToArray(array, create_reference(item, &global_hooks));
}

void cJSON_AddItemReferenceToObject(cJSON *object, const char *name, cJSON *item)
{
    cJSON_AddItemToObject(object, name, create_reference(item, &global_hooks));
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, size_t which)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        /* item doesn't exist */
        return NULL;
    }
    if (c == array->child)
    {
        /* first element */
        array->child = c->next;
    }
    else
    {
        /* not the first element */
        previous->next = c->next;
    }

    /* make sure the detached item doesn't point anywhere anymore */
    c->next = NULL;

    return c;
}

void internal_cJSON_DeleteItemFromArray(cJSON *array, size_t which, const cJSON_Hooks * const hooks)
{
    internal_cJSON_Delete(cJSON_DetachItemFromArray(array, which), hooks);
}
void cJSON_DeleteItemFromArray(cJSON * array, size_t which)
{
    internal_cJSON_DeleteItemFromArray(array, which, &global_hooks);
}

cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *name)
{
    size_t i = 0;
    cJSON *c = object->child;
    while (c && strcmp(c->name, name))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        return cJSON_DetachItemFromArray(object, i);
    }

    return NULL;
}

void internal_cJSON_DeleteItemFromObject(cJSON *object, const char *name, const cJSON_Hooks * const hooks)
{
    internal_cJSON_Delete(cJSON_DetachItemFromObject(object, name), hooks);
}
void cJSON_DeleteItemFromObject(cJSON *object, const char *name)
{
    internal_cJSON_DeleteItemFromObject(object, name, &global_hooks);
}

/* Replace array/object items with new ones. */
void cJSON_InsertItemInArray(cJSON *array, size_t which, cJSON *newitem)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        cJSON_AddItemToArray(array, newitem);
        return;
    }
    newitem->next = c;
    if (c == array->child)
    {
        /* first element */
        array->child = newitem;
        return;
    }

    /* not first element */
    previous->next = newitem;
}

void internal_cJSON_ReplaceItemInArray(cJSON *array, size_t which, cJSON *newitem, const cJSON_Hooks * const hooks)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        return;
    }
    newitem->next = c->next;
    if (c == array->child)
    {
        /* first element */
        array->child = newitem;
    }
    else
    {
        /* not first element */
        previous->next = newitem;
    }

    /* make sure the replaced item doesn't point anywhere anymore */
    c->next = NULL;
    internal_cJSON_Delete(c, hooks);
}
void cJSON_ReplaceItemInArray(cJSON *array, size_t which, cJSON *newitem)
{
    internal_cJSON_ReplaceItemInArray(array, which, newitem, &global_hooks);
}

void internal_cJSON_ReplaceItemInObject(cJSON *object, const char *name, cJSON *newitem, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *c = object->child;
    while(c && strcmp(c->name, name))
    {
        i++;
        c = c->next;
    }
    if(c)
    {
        /* free the old name if not const */
        if (!newitem->string_is_const && newitem->name)
        {
             hooks->free_fn(newitem->name);
        }

        newitem->name = cJSON_strdup(name, hooks);
        internal_cJSON_ReplaceItemInArray(object, i, newitem, hooks);
    }
}
void cJSON_ReplaceItemInObject(cJSON *object, const char *name, cJSON *newitem)
{
    internal_cJSON_ReplaceItemInObject(object, name, newitem, &global_hooks);
}

/* Create basic types: */
cJSON *internal_cJSON_CreateNull(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_NULL;
    }

    return item;
}
cJSON *cJSON_CreateNull(void)
{
    return internal_cJSON_CreateNull(&global_hooks);
}

cJSON *internal_cJSON_CreateTrue(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_True;
        item->value.boolean = true;
    }

    return item;
}
cJSON *cJSON_CreateTrue(void)
{
    return internal_cJSON_CreateTrue(&global_hooks);
}

cJSON *internal_cJSON_CreateFalse(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_False;
        item->value.boolean = false;
    }

    return item;
}
cJSON *cJSON_CreateFalse(void)
{
    return internal_cJSON_CreateFalse(&global_hooks);
}

cJSON *internal_cJSON_CreateBool(bool b, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = b ? cJSON_True : cJSON_False;
        item->value.boolean = b;
    }

    return item;
}
cJSON *cJSON_CreateBool(int b)
{
    return internal_cJSON_CreateBool(b, &global_hooks);
}

cJSON *internal_cJSON_CreateNumber(double num, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_Number;
        item->value.number = num;
    }

    return item;
}
cJSON *cJSON_CreateNumber(double num)
{
    return internal_cJSON_CreateNumber(num, &global_hooks);
}

cJSON *internal_cJSON_CreateString(const char *string, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_String;
        item->value.string = cJSON_strdup(string, hooks);
        if(item->value.string == NULL)
        {
            internal_cJSON_Delete(item, hooks);
            return NULL;
        }
    }

    return item;
}
cJSON *cJSON_CreateString(const char *string)
{
    return internal_cJSON_CreateString(string, &global_hooks);
}

cJSON *internal_cJSON_CreateArray(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type=cJSON_Array;
    }

    return item;
}
cJSON *cJSON_CreateArray(void)
{
    return internal_cJSON_CreateArray(&global_hooks);
}

cJSON *internal_cJSON_CreateObject(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if (item)
    {
        item->type = cJSON_Object;
    }

    return item;
}
cJSON *cJSON_CreateObject(void)
{
    return internal_cJSON_CreateObject(&global_hooks);
}

/* Create Arrays: */
cJSON *internal_cJSON_CreateIntArray(const int *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = internal_cJSON_CreateArray(hooks);
    for(i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if (!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateIntArray(const int *numbers, size_t count)
{
    return internal_cJSON_CreateIntArray(numbers, count, &global_hooks);
}

cJSON *internal_cJSON_CreateFloatArray(const float *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = internal_cJSON_CreateArray(hooks);
    for(i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateFloatArray(const float *numbers, size_t count)
{
    return internal_cJSON_CreateFloatArray(numbers, count, &global_hooks);
}

cJSON *internal_cJSON_CreateDoubleArray(const double *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = internal_cJSON_CreateArray(hooks);
    for(i = 0;a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateDoubleArray(const double *numbers, size_t count)
{
    return internal_cJSON_CreateDoubleArray(numbers, count, &global_hooks);
}

cJSON *internal_cJSON_CreateStringArray(const char **strings, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = internal_cJSON_CreateArray(hooks);
    for (i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateString(strings[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p,n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateStringArray(const char **strings, size_t count)
{
    return internal_cJSON_CreateStringArray(strings, count, &global_hooks);
}

/* Duplication */
cJSON *internal_cJSON_Duplicate(const cJSON *item, bool recurse, const cJSON_Hooks * const hooks)
{
    cJSON *newitem = NULL;
    cJSON *cptr = NULL;
    cJSON *nptr = NULL;
    cJSON *newchild = NULL;

    /* Bail on bad ptr */
    if (!item)
    {
        return NULL;
    }
    /* Create new item */
    newitem = cJSON_New_Item(hooks);
    if (!newitem)
    {
        return NULL;
    }
    /* Copy over all vars */
    newitem->type = item->type;
    newitem->value.string = NULL; /* default the pointer to NULL */
    newitem->is_reference = false;
    newitem->string_is_const = false;
    if ((item->type == cJSON_String) && (item->value.string != NULL))
    {
        newitem->value.string = cJSON_strdup(item->value.string, hooks);
        if (newitem->value.string == NULL)
        {
            internal_cJSON_Delete(newitem, hooks);
            return NULL;
        }
    }
    else if (item->type == cJSON_Number)
    {
        newitem->value.number = item->value.number;
    }
    else if (item->type & (cJSON_True | cJSON_False)) /* is boolean */
    {
        newitem->value.boolean = item->value.boolean;
    }
    if (item->name)
    {
        newitem->name = cJSON_strdup(item->name, hooks);
        if (!newitem->name)
        {
            internal_cJSON_Delete(newitem, hooks);
            return NULL;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse)
    {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    cptr = item->child;
    while (cptr)
    {
        newchild = internal_cJSON_Duplicate(cptr, 1, hooks); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild)
        {
            internal_cJSON_Delete(newitem, hooks);
            return NULL;
        }
        if (nptr)
        {
            /* If newitem->child already set, then add newchild and move on */
            nptr->next = newchild;
            nptr = newchild;
        }
        else
        {
            /* Set newitem->child and move to it */
            newitem->child = newchild; nptr = newchild;
        }
        cptr = cptr->next;
    }

    return newitem;
}
cJSON *cJSON_Duplicate(const cJSON *item, int recurse)
{
    return internal_cJSON_Duplicate(item, recurse, &global_hooks);
}

void cJSON_Minify(char *json)
{
    char *into = json;
    while (*json)
    {
        if (*json == ' ')
        {
            json++;
        }
        else if (*json == '\t')
        {
            /* Whitespace characters. */
            json++;
        }
        else if (*json == '\r')
        {
            json++;
        }
        else if (*json=='\n')
        {
            json++;
        }
        else if ((*json == '/') && (json[1] == '/'))
        {
            /* double-slash comments, to end of line. */
            while (*json && (*json != '\n'))
            {
                json++;
            }
        }
        else if ((*json == '/') && (json[1] == '*'))
        {
            /* multiline comments. */
            while (*json && !((*json == '*') && (json[1] == '/')))
            {
                json++;
            }
            json += 2;
        }
        else if (*json == '\"')
        {
            /* string literals, which are \" sensitive. */
            *into++ = *json++;
            while (*json && (*json != '\"'))
            {
                if (*json == '\\')
                {
                    *into++=*json++;
                }
                *into++ = *json++;
            }
            *into++ = *json++;
        }
        else
        {
            /* All other characters. */
            *into++ = *json++;
        }
    }

    /* and null-terminate. */
    *into = '\0';
}
