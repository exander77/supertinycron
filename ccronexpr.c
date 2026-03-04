/*
 * Copyright 2015, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * File:   ccronexpr.c
 * Author: alex
 *
 * Created on February 24, 2015, 9:35 AM
 */

#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "ccronexpr.h"

#define CRON_MAX_SECONDS 60
#define CRON_MAX_LEAP_SECONDS 2
#define CRON_MAX_MINUTES 60
#define CRON_MAX_HOURS 24
#define CRON_MAX_DAYS_OF_MONTH 32
#define CRON_MAX_DAYS_OF_WEEK 7
#define CRON_MAX_MONTHS 12
#define CRON_MIN_YEARS 1970
#define CRON_MAX_YEARS 2200
#define CRON_MAX_YEARS_DIFF 4
#define CRON_MAX_DO_NEXTPREV_ITERS 100000
#define CRON_MAX_CRON_ITERS 100000
#define CRON_MKTIME_SAFE_MAX_MINUTES 2880

#define CRON_YEAR_OFFSET 1900
#define CRON_DAY_SECONDS 24 * 60 * 60
#define CRON_WEEK_DAYS 7

#define CRON_CF_SECOND 0
#define CRON_CF_MINUTE 1
#define CRON_CF_HOUR_OF_DAY 2
#define CRON_CF_DAY_OF_WEEK 3
#define CRON_CF_DAY_OF_MONTH 4
#define CRON_CF_MONTH 5
#define CRON_CF_YEAR 6
#define CRON_CF_NEXT 7

#define CRON_CF_ARR_LEN 7

static const char* const DAYS_ARR[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
#define CRON_DAYS_ARR_LEN 7
static const char* const MONTHS_ARR[] = { "FOO", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
#define CRON_MONTHS_ARR_LEN 13

time_t cron_mktime(struct tm* tm);
struct tm* cron_time(time_t* date, struct tm* out);

#ifdef CRON_USE_LOCAL_TIME
#define CMP_FIELD(field) if (a->field != b->field) return a->field < b->field ? -1 : 1
static int cron_tm_cmp_ymdhms(const struct tm* a, const struct tm* b) {
    CMP_FIELD(tm_year); CMP_FIELD(tm_mon); CMP_FIELD(tm_mday); CMP_FIELD(tm_hour); CMP_FIELD(tm_min); CMP_FIELD(tm_sec); return 0;
}
#endif

/*
 * Local timezones may have missing wall-clock instants around DST jumps.
 * When mktime normalizes to the opposite direction, probe in the requested
 * direction only to preserve monotonic next/prev stepping.
 */
static time_t cron_mktime_safe_dir(struct tm* tm, int direction) {
#ifdef CRON_USE_LOCAL_TIME
    struct tm requested, normalized, probe;
    time_t t;
    int step, pass, delta;
    if (!tm) return CRON_INVALID_INSTANT;
    requested = *tm;
    t = cron_mktime(tm);
    if (CRON_INVALID_INSTANT != t && cron_time(&t, &normalized)) {
        int cmp = cron_tm_cmp_ymdhms(&normalized, &requested);
        if ((direction > 0 && cmp >= 0) || (direction < 0 && cmp <= 0) || direction == 0) {
            *tm = normalized;
            return t;
        }
    }
    for (step = 1; step <= CRON_MKTIME_SAFE_MAX_MINUTES; step++) {
        for (pass = 0; pass < (direction == 0 ? 2 : 1); pass++) {
            delta = direction < 0 ? -step : step;
            if (direction == 0 && pass == 1) delta = -step;
            probe = requested;
            probe.tm_min += delta;
            probe.tm_isdst = -1;
            t = cron_mktime(&probe);
            if (CRON_INVALID_INSTANT == t) continue;
            if (!cron_time(&t, &normalized)) continue;
            if (direction != 0) {
                int cmp = cron_tm_cmp_ymdhms(&normalized, &requested);
                if ((direction > 0 && cmp < 0) || (direction < 0 && cmp > 0)) continue;
            }
            *tm = normalized;
            return t;
        }
    }
    return CRON_INVALID_INSTANT;
#else
    (void)direction;
    if (!tm) return CRON_INVALID_INSTANT;
    return cron_mktime(tm);
#endif
}

/**
 * Time functions from standard library.
 * This part defines: cron_mktime: create time_t from tm
 *                    cron_time:   create tm from time_t
 */

/* forward declarations for platforms that may need them */
/* can be hidden in time.h */
#if !defined(_WIN32) && !defined(__AVR__) && !defined(ESP8266) && !defined(ESP_PLATFORM) && !defined(ANDROID) && !defined(TARGET_LIKE_MBED)
struct tm *gmtime_r(const time_t *timep, struct tm *result);
time_t timegm(struct tm* __tp);
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif /* PLEASE CHECK _WIN32 AND ANDROID NEEDS FOR THESE DECLARATIONS */
#ifdef __MINGW32__
/* To avoid warning when building with mingw */
time_t _mkgmtime(struct tm* tm);
#endif /* __MINGW32__ */

/* Defining 'cron_' time functions to use use UTC (default) or local time */
#ifndef CRON_USE_LOCAL_TIME
time_t cron_mktime(struct tm* tm) {
#if defined(_WIN32)
/* http://stackoverflow.com/a/22557778 */
    return _mkgmtime(tm);
#elif defined(__AVR__)
/* https://www.nongnu.org/avr-libc/user-manual/group__avr__time.html */
    return mk_gmtime(tm);
#elif defined(ESP8266) || defined(ESP_PLATFORM) || defined(TARGET_LIKE_MBED)
#error "timegm() is not supported on the ESP platform, please use this library with CRON_USE_LOCAL_TIME"
#elif defined(ANDROID) && !defined(__LP64__)
    /* https://github.com/adobe/chromium/blob/cfe5bf0b51b1f6b9fe239c2a3c2f2364da9967d7/base/os_compat_android.cc#L20 */
    static const time_t kTimeMax = ~(1L << (sizeof (time_t) * CHAR_BIT - 1));
    static const time_t kTimeMin = (1L << (sizeof (time_t) * CHAR_BIT - 1));
    time64_t result = timegm64(tm);
    if (result < kTimeMin || result > kTimeMax) return -1;
    return result;
#else
    return timegm(tm);
#endif
}
struct tm* cron_time(time_t* date, struct tm* out) {
#if defined(__MINGW32__)
    (void)(out); /* To avoid unused warning */
    return gmtime(date);
#elif defined(_WIN32)
    errno_t err = gmtime_s(out, date);
    return 0 == err ? out : NULL;
#elif defined(__AVR__)
    /* https://www.nongnu.org/avr-libc/user-manual/group__avr__time.html */
    gmtime_r(date, out);
    return out;
#else
    return gmtime_r(date, out);
#endif
}
#else /* CRON_USE_LOCAL_TIME */
time_t cron_mktime(struct tm* tm) {
    return mktime(tm);
}
struct tm* cron_time(time_t* date, struct tm* out) {
#if defined(_WIN32)
    errno_t err = localtime_s(out, date);
    return 0 == err ? out : NULL;
#elif defined(__AVR__)
    /* https://www.nongnu.org/avr-libc/user-manual/group__avr__time.html */
    localtime_r(date, out);
    return out;
#else
    return localtime_r(date, out);
#endif
}
#endif /* CRON_USE_LOCAL_TIME */

#define CRON_reset_all_min(calendar, fields) reset_all(reset_min, calendar, fields);
#define CRON_reset_all_max(calendar, fields) reset_all(reset_max, calendar, fields);
#define CRON_PARSE_ERROR(message)            { context->err = message; goto error; }
#define CRON_ERROR(message)                  { *error = message; goto error; }
#define CRON_TOKEN_COMPARE(context, token)   if (context->err) goto error; if (token == context->type) token_next(context); else goto compare_error;
#define GET_BYTE(idx)                        (uint8_t) (idx / 8)
#define GET_BIT(idx)                         (uint8_t) (idx % 8)
#define CRON_MKTIME(calendar)                if (CRON_INVALID_INSTANT == cron_mktime_safe_dir(calendar, offset)) goto return_error;
#define CRON_STRCATC(dest, buf, inc_len)     do { len += inc_len; if (len > buffer_len) return -1; strcat(dest, buf); } while (0)
#define CRON_GFC(dest, bits, min, max, offset, overflow, buffer_len) \
        { tmp = generate_field(dest, bits, min, max, offset, overflow, buffer_len); if (tmp < 0) return tmp; else len += tmp; }

void    cron_set_bit(      uint8_t* rbyte, int idx) { rbyte[GET_BYTE(idx)] |= (uint8_t) (1 << GET_BIT(idx)); }
void    cron_del_bit(      uint8_t* rbyte, int idx) { rbyte[GET_BYTE(idx)] &= (uint8_t)~(1 << GET_BIT(idx)); }
uint8_t cron_get_bit(const uint8_t* rbyte, int idx) { return (uint8_t)(rbyte[GET_BYTE(idx)] & (1 << GET_BIT(idx))); }

static int next_set_bit(uint8_t* bits, int max, int from_index) {
    for (; from_index < max; from_index++) if (cron_get_bit(bits, from_index)) return from_index;
    return -1;
}

static int prev_set_bit(uint8_t* bits, int from_index, int to_index) {
    for (; from_index >= to_index; from_index--) if (cron_get_bit(bits, from_index)) return from_index;
    return -1;
}

static int* get_field_ptr(struct tm* calendar, int field) {
    switch (field) {
    case CRON_CF_SECOND:       return &calendar->tm_sec;
    case CRON_CF_MINUTE:       return &calendar->tm_min;
    case CRON_CF_HOUR_OF_DAY:  return &calendar->tm_hour;
    case CRON_CF_DAY_OF_WEEK:  return &calendar->tm_wday;
    case CRON_CF_DAY_OF_MONTH: return &calendar->tm_mday;
    case CRON_CF_MONTH:        return &calendar->tm_mon;
    case CRON_CF_YEAR:         return &calendar->tm_year;
    default:                   return NULL; /* unknown field */
    }
}

static int last_day_of_month(int month, int year, int is_weekday) {
    struct tm calendar;
    time_t t;
    memset(&calendar, 0, sizeof(struct tm));
    calendar.tm_mon = month + 1; /* next month */
    calendar.tm_year = year; /* years since 1900 */
    t = cron_mktime(&calendar);

    if (is_weekday) {
        /* If the last day of the month is a Saturday (6) or Sunday (0), decrement the day. But it is shifted to (5) and (6). */
        while (cron_time(&t, &calendar)->tm_wday == 6 || cron_time(&t, &calendar)->tm_wday == 0) t -= CRON_DAY_SECONDS; /* subtract a day */
    }

    return cron_time(&t, &calendar)->tm_mday;
}

static int closest_weekday(int day_of_month, int month, int year) {
    struct tm calendar;
    time_t t;
    int wday;
    memset(&calendar, 0, sizeof(struct tm));
    calendar.tm_mon = month; /* given month */
    calendar.tm_mday = day_of_month + 1;
    calendar.tm_year = year; /* years since 1900 */
    t = cron_mktime(&calendar);

    wday = cron_time(&t, &calendar)->tm_wday;

    /* If it's a Sunday */
    if (wday == 0) {
        /* If it's the last day of the month, go to the previous Friday */
        if (day_of_month + 1 == last_day_of_month(month, year, 0)) t -= 2 * CRON_DAY_SECONDS;
        else t += CRON_DAY_SECONDS; /* go to the next Monday */
    /* If it's a Saturday */
    } else if (wday == 6) {
        /* If it's the first day of the month, go to the next Monday */
        if (day_of_month == 0) t += 2 * CRON_DAY_SECONDS;
        else t -= CRON_DAY_SECONDS; /* go to the previous Friday */
    }

    /* If it's a weekday */
    return cron_time(&t, &calendar)->tm_mday;
}

static void set_field(struct tm* calendar, int field, int val) {
    *get_field_ptr(calendar, field) = val;
#ifdef CRON_USE_LOCAL_TIME
    /* Force libc to recalculate DST after any manual calendar-field change. */
    calendar->tm_isdst = -1;
#endif
    /* Reset day of month after month change to it's maximum. */
    if (field == CRON_CF_MONTH) {
        val = last_day_of_month(calendar->tm_mon, calendar->tm_year, 0);
        if (calendar->tm_mday > val) calendar->tm_mday = val;
    }
}

static void add_to_field(struct tm* calendar, int field, int val) {
    set_field(calendar, field, *get_field_ptr(calendar, field) + val);
}

/*
 * Carry operations from seconds->minutes and minutes->hours must move on the
 * timeline (not only in wall-clock fields), otherwise DST gaps can normalize
 * backwards carries into the future (and vice versa).
 */
static int roll_carry_field(struct tm* calendar, int nextField, int offset) {
    time_t t;

    if (CRON_CF_MINUTE == nextField || CRON_CF_HOUR_OF_DAY == nextField) {
        t = cron_mktime_safe_dir(calendar, offset);
        if (CRON_INVALID_INSTANT == t) return -1;
        t += (time_t)offset * (CRON_CF_MINUTE == nextField ? 60 : 60 * 60);
        if (!cron_time(&t, calendar)) return -1;
        return 0;
    }

    add_to_field(calendar, nextField, offset);
    return CRON_INVALID_INSTANT == cron_mktime_safe_dir(calendar, offset) ? -1 : 0;
}

/**
 * Reset the calendar setting all the fields provided to zero or max value.
 */
static void reset_min(struct tm* calendar, int field) {
    set_field(calendar, field, field == CRON_CF_DAY_OF_MONTH);
}
static void reset_max(struct tm* calendar, int field) {
         if (CRON_CF_SECOND       == field) set_field(calendar, field, CRON_MAX_SECONDS-1);
    else if (CRON_CF_MINUTE       == field) set_field(calendar, field, CRON_MAX_MINUTES-1);
    else if (CRON_CF_HOUR_OF_DAY  == field) set_field(calendar, field, CRON_MAX_HOURS  -1);
    else if (CRON_CF_DAY_OF_MONTH == field) set_field(calendar, field, last_day_of_month(calendar->tm_mon, calendar->tm_year, 0));
    else if (CRON_CF_MONTH        == field) set_field(calendar, field, CRON_MAX_MONTHS -1);
}

static void reset_all(void (*fn)(struct tm* calendar, int field), struct tm* calendar, uint8_t* fields) {
    int i; for (i = 0; i < CRON_CF_ARR_LEN; i++) if (cron_get_bit(fields, i)) fn(calendar, i);
}

typedef enum { T_ASTERISK, T_QUESTION, T_NUMBER, T_COMMA, T_SLASH, T_L, T_W, T_HASH, T_MINUS, T_WS, T_EOF, T_INVALID } TokenType;
typedef struct { const char* input; TokenType type; cron_expr* target; int field_type, value, min, max, offset, fix_dow; uint8_t* field; const char* err; } ParserContext;

static int compare_strings(const char* str1, const char* str2, size_t len) {
    size_t i; for (i = 0; i < len; i++) if (toupper(str1[i]) != str2[i]) return str1[i] - str2[i]; return 0;
}

static int match_ordinals(const char* str, const char* const* arr, size_t arr_len) {
    size_t i; for (i = 0; i < arr_len; i++) if (!compare_strings(str, arr[i], strlen(arr[i]))) return (int)i; return -1;
}

static int count_fields(const char* str) {
    int count = 0, in_field = 0;
    if (!str) return -1;
    while (*str) {
        if (isspace((unsigned char)*str)) in_field = 0;
        else if (!in_field) {
            in_field = 1;
            count++;
        }
        str++;
    }
    return count;
}

static void token_next(ParserContext* context) {
    const char *input = context->input;
    context->type = T_INVALID;
    context->value = 0;
    if (*context->input == '\0') context->type = T_EOF;
    else if (isspace(*context->input)) {
        do ++context->input; while (isspace(*context->input));
        context->type = T_WS;
    } else if (isdigit(*context->input)) {
        do {
            context->value = context->value * 10 + (*context->input - '0');
            ++context->input;
        } while (isdigit(*context->input));
        context->type = T_NUMBER;
    } else if (isalpha(*input)) {
        do  ++input; while (isalpha(*input));
        context->value = match_ordinals(context->input, DAYS_ARR, CRON_DAYS_ARR_LEN);
        if (context->value < 0) context->value = match_ordinals(context->input, MONTHS_ARR, CRON_MONTHS_ARR_LEN);
        if (context->value < 0) goto rest;
        context->input = input;
        context->type = T_NUMBER;
    } else {
        rest: switch (*context->input) {
        case '*':  context->type = T_ASTERISK; break;
        case '?':  context->type = T_QUESTION; break;
        case ',':  context->type = T_COMMA;    break;
        case '/':  context->type = T_SLASH;    break;
        case 'L':  context->type = T_L;        break;
        case 'W':  context->type = T_W;        break;
        case '#':  context->type = T_HASH;     break;
        case '-':  context->type = T_MINUS;    break;
        }
        ++context->input;
    }
    if (T_INVALID == context->type) context->err = "Invalid token";
}

static int Number(ParserContext* context) {
    int value = 0;
    switch (context->type) {
    case T_MINUS:
        token_next(context);
        if (T_NUMBER == context->type) {
            value = -context->value;
            token_next(context);
        } else CRON_PARSE_ERROR("Number '-' follows with number");
        break;
    case T_NUMBER: value = context->value; token_next(context); break;
    default: CRON_PARSE_ERROR("Number - error");
    }
    error: return value;
}

static int Frequency(ParserContext* context, int delta, int* to, int range) {
    switch (context->type) {
    case T_SLASH:
        token_next(context);
        if (T_NUMBER == context->type) {
            delta = context->value;
            if (delta < 1) CRON_PARSE_ERROR("Frequency - needs to be at least 1");
            if (!range) *to = context->max - 1;
            token_next(context);
        } else CRON_PARSE_ERROR("Frequency - '/' follows with number");
        break;
    case T_COMMA: case T_WS: case T_EOF: break;
    default: CRON_PARSE_ERROR("Frequency - error");
    }
    error: return delta;
}

static int Range(ParserContext* context, int* from, int to) {
    int i;
    switch (context->type) {
    case T_HASH:
        if (CRON_CF_DAY_OF_WEEK == context->field_type) {
            token_next(context);
            if (*context->target->day_in_month)
                CRON_PARSE_ERROR("Nth-day - support for specifying multiple '#' segments is not implemented");
            *context->target->day_in_month = (int8_t)Number(context);
            if (*context->target->day_in_month > 5 || *context->target->day_in_month < -5)
                CRON_PARSE_ERROR("Nth-day - '#' can follow only with -5..5");
        } else  CRON_PARSE_ERROR("Nth-day - '#' allowed only for day of week");
        break;
    case T_MINUS:
        token_next(context);
        if (T_NUMBER == context->type) {
            to = context->value;
            token_next(context);
        } else  CRON_PARSE_ERROR("Range '-' follows with number");
        break;
    case T_W:
        *context->target->day_in_month = (int8_t)to;
        *from = context->min;
        for (i = 1; i <= 5; i++) cron_set_bit(context->target->days_of_week, i);
        cron_set_bit(context->target->flags, 2);
        to = context->max - 1;
        token_next(context);
        break;
    case T_L:
        if (CRON_CF_DAY_OF_WEEK == context->field_type) {
            *context->target->day_in_month = -1;
            token_next(context);
        } else  CRON_PARSE_ERROR("Range - 'L' allowed only for day of week");
        break;
    case T_WS: case T_SLASH: case T_COMMA: case T_EOF: break;
    default:    CRON_PARSE_ERROR("Range - error");
    }
    error: return to;
}

static void Segment(ParserContext* context) {
    int i, from = context->min, to = context->max - 1, delta = 1, leap = 0;
    start: switch (context->type) {
    case T_ASTERISK: token_next(context); delta = Frequency(context, delta, &to, 0); break;
    case T_NUMBER:
        from = context->value;
        token_next(context);
        to = Range(context, &from, from);
        if (context->err) goto error;
        delta = Frequency(context, delta, &to, from != to);
        break;
    case T_L:
        token_next(context);
        switch (context->field_type) {
            case CRON_CF_SECOND: if (!leap) context->max += CRON_MAX_LEAP_SECONDS; leap = 1; goto start;
            case CRON_CF_DAY_OF_MONTH:
                *context->target->day_in_month = -1;
                switch (context->type) {
                case T_MINUS: case T_NUMBER: *context->target->day_in_month = (int8_t)((int)*context->target->day_in_month + Number(context)); break;
                case T_W:
                    if (CRON_CF_DAY_OF_MONTH == context->field_type) {
                        token_next(context);
                        for (i = 1; i <= 5; i++) cron_set_bit(context->target->days_of_week, i);
                        cron_set_bit(context->target->flags, 1);
                        context->fix_dow = 1;
                        goto done;
                    } else CRON_PARSE_ERROR("Offset - 'W' allowed only for day of month");
                case T_COMMA: case T_WS: case T_EOF: break;
                default: CRON_PARSE_ERROR("Offset - error");
                }
                /* Note 0..6 and not 1..7, see end of set_days_of_week. */
                for (i = 0; i <= 6; i++) cron_set_bit(context->target->days_of_week, i);
                cron_set_bit(context->target->flags, 0);
                context->fix_dow = 1;
                break;
            case CRON_CF_DAY_OF_WEEK: from = to = 0; break;
            default: CRON_PARSE_ERROR("Segment 'L' allowed only for day of month and leap seconds")
        }
        break;
    case T_W:
        for (i = 1; i <= 5; i++) cron_set_bit(context->target->days_of_week, i);
        token_next(context);
        context->fix_dow = 1;
        break;
    case T_QUESTION: token_next(context); break;
    default: CRON_PARSE_ERROR("Segment - error");
    }
    done: if (context->err) goto error;
    if (CRON_CF_DAY_OF_WEEK == context->field_type && context->fix_dow) return;
    if (from  < context->min || to  < context->min) CRON_PARSE_ERROR("Range - specified range is less than minimum");
    if (from >= context->max || to >= context->max) CRON_PARSE_ERROR("Range - specified range exceeds maximum");
    if (from > to)                                  CRON_PARSE_ERROR("Range - specified range start exceeds range end");
    for (; from <= to; from+=delta) cron_set_bit(context->field, from+context->offset);
    if (CRON_CF_DAY_OF_WEEK == context->field_type) {
        if (cron_get_bit(context->field, 7)) {
            /* Sunday can be represented as 0 or 7*/
            cron_set_bit(context->field, 0);
            cron_del_bit(context->field, 7);
        }
    }
    error: return;
}

static void Field(ParserContext* context) {
    Segment(context);
    if (context->err) goto error;
    switch (context->type) {
    case T_COMMA: token_next(context); Field(context); break;
    case T_WS: case T_EOF: break;
    default: CRON_PARSE_ERROR("FieldRest - error");
    }
    error: return;
}

static void FieldWrapper(ParserContext* context, int field_type, int min, int max, int offset, uint8_t* field) {
    context->field_type = field_type;
    context->min = min;
    context->max = max;
    context->offset = offset;
    context->field = field;
    Field(context);
}

static void Fields(ParserContext* context, int len) {
    token_next(context);
    if (len < 6) cron_set_bit(context->target->seconds, 0);
    else {
        FieldWrapper(context, CRON_CF_SECOND,   0, CRON_MAX_SECONDS,          0, context->target->seconds);
        CRON_TOKEN_COMPARE(context, T_WS);
    }
    FieldWrapper(context, CRON_CF_MINUTE,       0, CRON_MAX_MINUTES,          0, context->target->minutes);
    CRON_TOKEN_COMPARE(context, T_WS);
    FieldWrapper(context, CRON_CF_HOUR_OF_DAY,  0, CRON_MAX_HOURS,            0, context->target->hours);
    CRON_TOKEN_COMPARE(context, T_WS);
    FieldWrapper(context, CRON_CF_DAY_OF_MONTH, 1, CRON_MAX_DAYS_OF_MONTH,    0, context->target->days_of_month);
    CRON_TOKEN_COMPARE(context, T_WS);
    FieldWrapper(context, CRON_CF_MONTH,        1, CRON_MAX_MONTHS + 1,      -1, context->target->months);
    CRON_TOKEN_COMPARE(context, T_WS);
    FieldWrapper(context, CRON_CF_DAY_OF_WEEK,  0, CRON_MAX_DAYS_OF_WEEK + 1, 0, context->target->days_of_week);
#ifndef CRON_DISABLE_YEARS
    if (len < 7) cron_set_bit(context->target->years, EXPR_YEARS_LENGTH*8-1);
    else {
        CRON_TOKEN_COMPARE(context, T_WS);
        FieldWrapper(context, CRON_CF_YEAR, CRON_MIN_YEARS, CRON_MAX_YEARS, -CRON_MIN_YEARS, context->target->years);
    }
#endif
    return;
    compare_error: CRON_PARSE_ERROR("Fields - expected whitespace separator");
    error: return;
}

/**
 * Search the bits provided for the next/prev set bit after the value provided, and reset the calendar.
 */
static int find_nextprev(uint8_t* bits, int max, int value, int value_offset, struct tm* calendar, int field, int nextField, uint8_t* lower_orders, int offset) {
    int next_value = (offset > 0 ? next_set_bit(bits, max, value+value_offset) : prev_set_bit(bits, value+value_offset, 0));
    if(next_value == -1 && field == CRON_CF_YEAR) return -1;
    next_value -= value_offset;
    /* roll under if needed */
    if (next_value < 0) {
        if (offset > 0) reset_max(calendar, field); else reset_min(calendar, field);
        if (0 != roll_carry_field(calendar, nextField, offset)) goto return_error;
        next_value = offset > 0 ? next_set_bit(bits, max, 0) : prev_set_bit(bits, max - 1, value);
    }
    if (next_value < 0 || next_value != value) {
        if (offset > 0) CRON_reset_all_min(calendar, lower_orders) else CRON_reset_all_max(calendar, lower_orders);
        set_field(calendar, field, next_value < 0 ? 0 : next_value); CRON_MKTIME(calendar);
    }
    return next_value < 0 ? 0 : next_value; return_error: return -1;
}

static int find_day_condition(struct tm* calendar, const uint8_t* days_of_month, const int8_t* dim, int dom, const uint8_t* days_of_week, int dow, const uint8_t* flags, int* day) {
    int tmp_day = *day;
    if (tmp_day < 0) {
        if ((!*flags && *dim < 0) || *flags & 1) tmp_day = last_day_of_month(calendar->tm_mon, calendar->tm_year, 0);
        else if (*flags & 2)                     tmp_day = last_day_of_month(calendar->tm_mon, calendar->tm_year, 1);
        else if (*flags & 4)                     tmp_day = closest_weekday(*dim-1, calendar->tm_mon, calendar->tm_year);
        *day = tmp_day;
    }
    if (!cron_get_bit(days_of_month, dom) || !cron_get_bit(days_of_week,  dow))                  return 1;
    if (*flags) {
        if ((*flags & 3) && dom != tmp_day+1+*dim)                                               return 1;
        if ((*flags & 4) && dom != tmp_day)                                                      return 1;
    } else {
        if (*dim < 0 && (dom < tmp_day+CRON_WEEK_DAYS**dim+1 || dom >= tmp_day+CRON_WEEK_DAYS*(*dim+1)+1)) return 1;
        if (*dim > 0 && (dom < CRON_WEEK_DAYS*(*dim-1)+1     || dom >= CRON_WEEK_DAYS**dim+1))             return 1;
    }
    return 0;
}

static int find_day(struct tm* calendar, const uint8_t* days_of_month, const int8_t* dim, int dom, const uint8_t* days_of_week, int dow, const uint8_t* flags, uint8_t* resets, int offset) {
    int day = -1, year = calendar->tm_year, month = calendar->tm_mon;
    unsigned int count = 0, max = 366;
    while (count < max && find_day_condition(calendar, days_of_month, dim, dom, days_of_week, dow, flags, &day)) {
        if (offset > 0) CRON_reset_all_min(calendar, resets) else CRON_reset_all_max(calendar, resets);
        add_to_field(calendar, CRON_CF_DAY_OF_MONTH, offset); CRON_MKTIME(calendar);
        /* These two conditions may be unecessary. Verify in the future. */
        if(offset < 0 && (calendar->tm_year < CRON_MIN_YEARS - CRON_YEAR_OFFSET)) goto return_error;
        if(offset > 0 && (calendar->tm_year > CRON_MAX_YEARS - CRON_YEAR_OFFSET)) goto return_error;
        count++;
        dom = calendar->tm_mday;
        dow = calendar->tm_wday;
        if (year != calendar->tm_year) {
            year = calendar->tm_year;
            day = -1; /* This should not be needed unless there is as single day month in libc. */
        }
        if (month != calendar->tm_mon) {
            month = calendar->tm_mon;
            day = -1;
        }
    }
    if (find_day_condition(calendar, days_of_month, dim, dom, days_of_week, dow, flags, &day)) goto return_error;
    return dom; return_error: return -1;
}

#ifdef CRON_STRICT_MATCH
static int calendar_matches_expr(const cron_expr* expr, struct tm* calendar) {
    int day = -1;
#ifndef CRON_DISABLE_YEARS
    int yidx = 0;
#endif

    if (!cron_get_bit(expr->seconds, calendar->tm_sec)) return 0;
    if (!cron_get_bit(expr->minutes, calendar->tm_min)) return 0;
    if (!cron_get_bit(expr->hours, calendar->tm_hour)) return 0;
    if (!cron_get_bit(expr->months, calendar->tm_mon)) return 0;
    if (find_day_condition(calendar, expr->days_of_month, expr->day_in_month,
            calendar->tm_mday, expr->days_of_week, calendar->tm_wday, expr->flags, &day)) return 0;
#ifndef CRON_DISABLE_YEARS
    if (!cron_get_bit(expr->years, EXPR_YEARS_LENGTH*8-1)) {
        yidx = calendar->tm_year + CRON_YEAR_OFFSET - CRON_MIN_YEARS;
        if (yidx < 0 || yidx >= EXPR_YEARS_LENGTH*8) return 0;
        if (!cron_get_bit(expr->years, yidx)) return 0;
    }
#endif
    return 1;
}
#endif

#define RI(field, expr_field, min, max, nextField) \
        value = *get_field_ptr(calendar, field); update_value = find_nextprev(expr_field, max, value, min, calendar, field, nextField, resets, offset);
#define RF(field) if (update_value < 0) break; cron_set_bit(resets, field); if (value == update_value)

static int do_nextprev(cron_expr* expr, struct tm* calendar, int dot, int offset) {
    int value = 0, update_value = 0;
    uint8_t resets[1];
    unsigned int iter_guard = 0;

    for (iter_guard = 0; iter_guard <= CRON_MAX_DO_NEXTPREV_ITERS; iter_guard++) {
        *resets = 0;
        RI(CRON_CF_SECOND,        expr->seconds, 0, CRON_MAX_SECONDS + CRON_MAX_LEAP_SECONDS, CRON_CF_MINUTE);
        RF(CRON_CF_SECOND);       else if (update_value >= CRON_MAX_SECONDS) continue;
        RI(CRON_CF_MINUTE,        expr->minutes, 0, CRON_MAX_MINUTES,                         CRON_CF_HOUR_OF_DAY);
        RF(CRON_CF_MINUTE);       else continue;
        RI(CRON_CF_HOUR_OF_DAY,   expr->hours,   0, CRON_MAX_HOURS,                           CRON_CF_DAY_OF_MONTH);
        RF(CRON_CF_HOUR_OF_DAY);  else continue;
        value = *get_field_ptr(calendar,            CRON_CF_DAY_OF_MONTH);
        update_value = find_day(calendar,
                expr->days_of_month, expr->day_in_month, value, expr->days_of_week, calendar->tm_wday, expr->flags, resets, offset);
        RF(CRON_CF_DAY_OF_MONTH); else continue;
        RI(CRON_CF_MONTH,         expr->months, 0, CRON_MAX_MONTHS,                           CRON_CF_YEAR);
        if (update_value < 0) break;
        if (value != update_value) {
            if (abs(calendar->tm_year - dot) > CRON_MAX_YEARS_DIFF) { update_value = -1; break; }
            continue;
        }
#ifdef CRON_DISABLE_YEARS
        else break;
#else
        if (cron_get_bit(expr->years, EXPR_YEARS_LENGTH*8-1)) break;
        RF(CRON_CF_MONTH);        else continue;
        RI(CRON_CF_YEAR, expr->years, CRON_YEAR_OFFSET-CRON_MIN_YEARS, CRON_MAX_YEARS-CRON_MIN_YEARS, CRON_CF_NEXT);
        if (update_value < 0 || value == update_value) break;
#endif
    }
    if (iter_guard > CRON_MAX_DO_NEXTPREV_ITERS) update_value = -1;

    return update_value >= 0 ? 0 : update_value;
}

static int generate_field(char *dest, uint8_t *bits, int min, int max, int offset, int overflow, int buffer_len) {
    char buf[33];
    int len = 0, first = 1, i, fs = -1, ls = -1, d = -1, n = 0, from, dt;
    for (i = min; i < max; i++) {
        if (cron_get_bit(bits, i + offset)) {
            n++;
            if (fs == -1) fs = i;
            else {
                dt = i - ls;
                if (d == -1) d = dt;
                else if (d != dt) { d = -1; break; }
            }
            ls = i;
        }
    }
    if (n == max - min) len += sprintf(buf, "*");
    else if (d < 2) goto values;
    else if (fs == min && ls + d > max - 1) len += sprintf(buf, "*/%d", d);
    else if (n < 3) goto values;
    else if (((max + overflow - fs - 1) / d) + 1 != n) len += sprintf(buf, "%d-%d/%d", fs, ls, d);
    else len += sprintf(buf, "%d/%d", fs, d);
    CRON_STRCATC(dest, buf, 0);
    return len;
values:
        from = -1;
        for (i = fs; i <= max; i++) {
            if (i < max ? cron_get_bit(bits, i + offset) : 0) { if (from == -1) from = i; }
            else if (from != -1) {
                if (!first) CRON_STRCATC(dest, ",", 1);
                first = 0;
                len += from == i-1 ? sprintf(buf, "%d", from) : sprintf(buf, "%d-%d", from, i-1);
                from = -1;
                CRON_STRCATC(dest, buf, 0);
            }
        }
        return len;
}

int cron_generate_expr(cron_expr *source, char *buffer, int buffer_len, int cron_len, const char **error) {
    const char* err_local;
    char buf[32];
    int i, leap = 0, tmp, len = 1;
    uint8_t days_of_week = *source->days_of_week;
    *buffer = '\0';
    if (!error) error = &err_local;

    if (cron_len > 5) {
        for (i = CRON_MAX_SECONDS; i < CRON_MAX_SECONDS + CRON_MAX_LEAP_SECONDS; i++) {
            if (cron_get_bit(source->seconds, i)) {
                leap = 1;
                CRON_STRCATC(buffer, "L", 1);
                break;
            }
        }
        CRON_GFC(buffer, source->seconds, 0, CRON_MAX_SECONDS + (leap ? CRON_MAX_LEAP_SECONDS : 0), 0, 0, buffer_len - len);
        CRON_STRCATC(buffer, " ", 1);
    }
    CRON_GFC(buffer, source->minutes, 0, CRON_MAX_MINUTES, 0, 0, buffer_len - len);
    CRON_STRCATC(buffer, " ", 1);
    CRON_GFC(buffer, source->hours, 0, CRON_MAX_HOURS, 0, 0, buffer_len - len);
    CRON_STRCATC(buffer, " ", 1);
    if (cron_get_bit(source->flags, 0)) {
        CRON_STRCATC(buffer, "L", 1);
        if (*source->day_in_month < -1) CRON_STRCATC(buffer, buf, sprintf(buf, "%d", *source->day_in_month + 1));
    } else if (cron_get_bit(source->flags, 1)) CRON_STRCATC(buffer, "LW", 2);
    else if (cron_get_bit(source->flags, 2)) CRON_STRCATC(buffer, buf, sprintf(buf, "%dW", *source->day_in_month));
    else if (*source->day_in_month != 0) CRON_STRCATC(buffer, "?", 1);
    else CRON_GFC(buffer, source->days_of_month, 1, CRON_MAX_DAYS_OF_MONTH, 0, 0, buffer_len - len);
    CRON_STRCATC(buffer, " ", 1);
    CRON_GFC(buffer, source->months, 1, CRON_MAX_MONTHS + 1, -1, 0, buffer_len - len);
    CRON_STRCATC(buffer, " ", 1);
    if (*source->flags) CRON_STRCATC(buffer, "?", 1);
    else {
        if (*source->day_in_month != 0) {
            CRON_GFC(buffer, &days_of_week, 0, CRON_MAX_DAYS_OF_WEEK, 0, 1, buffer_len - len);
            if (*source->day_in_month == -1) CRON_STRCATC(buffer, "L", 1);
            else CRON_STRCATC(buffer, buf, sprintf(buf, "#%d", *source->day_in_month));
        } else CRON_GFC(buffer, &days_of_week, 0, CRON_MAX_DAYS_OF_WEEK, 0, 1, buffer_len - len);
    }
#ifndef CRON_DISABLE_YEARS
    if (cron_len > 6) {
        if (cron_get_bit(source->years, EXPR_YEARS_LENGTH*8-1)) CRON_STRCATC(buffer, " *", 2);
        else {
            CRON_STRCATC(buffer, " ", 1);
            CRON_GFC(buffer, source->years, CRON_MIN_YEARS, CRON_MAX_YEARS, -CRON_MIN_YEARS, 0, buffer_len - len);
        }
    }
#endif

    (void)error;
    return len;
}

static int cron_once(cron_expr* expr, time_t date, int offset, time_t* result) {
    /*
     The plan:

     1 Round up to the next whole second

     2 If seconds match move on, otherwise find the next match:
     2.1 If next match is in the next minute then roll forwards

     3 If minute matches move on, otherwise find the next match
     3.1 If next match is in the next hour then roll forwards
     3.2 Reset the seconds and go to 2

     4 If hour matches move on, otherwise find the next match
     4.1 If next match is in the next day then roll forwards,
     4.2 Reset the minutes and seconds and go to 2

     ...
     */
    struct tm calval, *calendar;
    time_t original, calculated, stepped;
    if (!expr || !result) goto return_error;
    memset(&calval, 0, sizeof(struct tm));
    calendar = cron_time(&date, &calval);
    if (!calendar) goto return_error;
    original = cron_mktime_safe_dir(calendar, 0);
    if (CRON_INVALID_INSTANT == original) goto return_error;
    if (0 != do_nextprev(expr, calendar, calendar->tm_year, offset)) goto return_error;
    calculated = cron_mktime_safe_dir(calendar, offset);
    if (CRON_INVALID_INSTANT == calculated) goto return_error;
    if (calculated == original) {
        /* Step on the absolute timeline to avoid ambiguous local-time normalization loops. */
        stepped = original + offset;
        if (!cron_time(&stepped, calendar)) goto return_error;
        if (0 != do_nextprev(expr, calendar, calendar->tm_year, offset)) goto return_error;
    }

    *result = cron_mktime_safe_dir(calendar, offset);
    return CRON_INVALID_INSTANT == *result ? -1 : 0;
    return_error: return -1;
}

static time_t cron(cron_expr* expr, time_t date, int offset) {
    time_t candidate = CRON_INVALID_INSTANT;
#ifndef CRON_STRICT_MATCH
    if (0 != cron_once(expr, date, offset, &candidate)) goto return_error;
    return candidate;
#else
    /*
     * Enforce candidate validity after libc time normalization.
     * This prevents returning non-matching instants under DST/local-time quirks.
     */
    struct tm calval, *calendar;
    time_t seed = date;
    unsigned int guard = 0;

    while (guard++ < CRON_MAX_CRON_ITERS) {
        if (0 != cron_once(expr, seed, offset, &candidate)) goto return_error;
        if ((offset > 0 && candidate <= seed) || (offset < 0 && candidate >= seed)) {
            seed += offset;
            continue;
        }
        memset(&calval, 0, sizeof(struct tm));
        calendar = cron_time(&candidate, &calval);
        if (!calendar) goto return_error;
        if (calendar_matches_expr(expr, calendar)) return candidate;
        seed = candidate + offset;
    }
#endif

    return_error: return CRON_INVALID_INSTANT;
}

void cron_parse_expr(const char* expression, cron_expr* target, const char** error) {
    const char* err_local;
    int len = 0;
    ParserContext context;
    if (!error) error = &err_local;
    *error = NULL;
    if (!expression)                                                        CRON_ERROR("Invalid NULL expression");
    if (!target)                                                                CRON_ERROR("Invalid NULL target");
    if ('@' == *expression) {
        expression++;
             if (!strcmp("annually", expression) || !strcmp("yearly", expression))   expression = "0 0 0 1 1 *";
        else if (!strcmp("monthly",  expression))                                    expression = "0 0 0 1 * *";
        else if (!strcmp("weekly",   expression))                                    expression = "0 0 0 * * 0";
        else if (!strcmp("daily",    expression) || !strcmp("midnight", expression)) expression = "0 0 0 * * *";
        else if (!strcmp("hourly",   expression))                                    expression = "0 0 * * * *";
        else if (!strcmp("minutely", expression))                                    expression = "0 * * * * *";
        else if (!strcmp("secondly", expression))                                    expression = "* * * * * * *";
        else if (!strcmp("reboot",   expression))                           CRON_ERROR("@reboot not implemented");
    }
    len = count_fields(expression);
    if (len < 5 || len > 7) CRON_ERROR("Invalid number of fields, expression must consist of 5-7 fields");
    memset(target, 0, sizeof(*target));
    memset(&context, 0, sizeof(context));
    context.input = expression;
    context.target = target;
    Fields(&context, len);
    *error = context.err;
    error: return;
}

time_t cron_next(cron_expr* expr, time_t date) { return cron(expr, date, +1); }
time_t cron_prev(cron_expr* expr, time_t date) { return cron(expr, date, -1); }
