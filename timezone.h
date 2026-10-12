#ifndef ELEPHANT_TIMEZONE_H
#define ELEPHANT_TIMEZONE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct {
    char name[64];           /* Display name e.g. "Tokyo, Japan" or "Eastern Time" */
    char abbrev[16];         /* Abbreviation e.g. "JST", "EST", "EDT", "IST" */
    int offset_minutes;      /* UTC offset in minutes e.g. +540 for JST, +330 for IST, -300 for EST */
    char time_str[128];      /* Formatted date and time e.g. "11-Oct-2026 03:06:23 PM" or "Tomorrow, 12-Oct-2026 03:06:23 AM" */
    char date_str[64];       /* Formatted date e.g. "12-Oct-2026" */
    char weekday_str[32];    /* Formatted weekday e.g. "Monday" */
    char offset_str[32];     /* Offset string e.g. "UTC+09:00" */
    char diff_from_local[48];/* Relative diff e.g. "+3h 30m ahead of local" */
    int day_shift;           /* -1 for yesterday, 0 for today, +1 for tomorrow */
} TimezoneResult;

/*
 * Resolves a timezone query string (abbreviation, city name, or numeric offset e.g. "utc+5:30", "tokyo", "est")
 * and converts the given epoch timestamp to that timezone.
 * Returns true if the timezone was recognized and converted successfully.
 */
bool convert_to_timezone(time_t epoch, const char *tz_query, TimezoneResult *res);

/*
 * Same as convert_to_timezone, but accepts an explicit reference timestamp for day-shift comparisons
 * (e.g. comparing converted target date against source date in "5pm est in ist").
 */
bool convert_to_timezone_ex(time_t epoch, time_t ref_time, const char *tz_query, TimezoneResult *res);

/*
 * Checks if a string is a recognized timezone, city, or numeric offset.
 */
bool is_valid_timezone_query(const char *tz_query);

/*
 * Parses a source timezone query and returns its UTC offset in minutes, or returns false if unrecognized.
 * Useful for parsing "5pm est in ist".
 */
bool parse_source_timezone_offset(const char *tz_query, time_t ref_time, int *out_offset_minutes, char *out_abbrev, size_t abbrev_sz);

/*
 * Provides a comma-separated list of popular timezone hints for autocomplete/hints row.
 */
void get_timezone_hints(char *hint_out, size_t hint_sz);

/*
 * Provides matching timezone hints starting with prefix, or popular timezones if prefix is empty.
 */
bool get_matching_timezone_hints(const char *prefix, char *out, size_t out_sz);

/*
 * Business / Working Days Calculations
 */
time_t add_business_days(time_t start, int count);
int count_business_days(time_t start, time_t end);

#endif /* ELEPHANT_TIMEZONE_H */
