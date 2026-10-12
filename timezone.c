#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "timezone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

typedef struct {
    const char *query;              /* Lowercase alias e.g. "est", "tokyo", "nyc" */
    const char *iana_name;          /* IANA zone e.g. "America/New_York", "Asia/Tokyo" */
    const char *display;            /* Human-readable region/city e.g. "Tokyo, Japan" */
    int fallback_offset_min;        /* Fallback offset in minutes from UTC */
    const char *fallback_abbrev;    /* Fallback abbreviation e.g. "EST", "JST" */
} TimezoneEntry;

static const TimezoneEntry TZ_TABLE[] = {
    /* Universal */
    { "utc",            "UTC",              "Coordinated Universal Time", 0, "UTC" },
    { "gmt",            "GMT",              "Greenwich Mean Time",        0, "GMT" },
    { "z",              "UTC",              "Zulu Time",                  0, "UTC" },

    /* North America - US / Canada */
    { "est",            "America/New_York", "Eastern Standard Time",   -300, "EST" },
    { "edt",            "America/New_York", "Eastern Daylight Time",   -240, "EDT" },
    { "et",             "America/New_York", "Eastern Time",            -300, "ET"  },
    { "cst",            "America/Chicago",  "Central Standard Time",   -360, "CST" },
    { "cdt",            "America/Chicago",  "Central Daylight Time",   -300, "CDT" },
    { "ct",             "America/Chicago",  "Central Time",            -360, "CT"  },
    { "mst",            "America/Denver",   "Mountain Standard Time",  -420, "MST" },
    { "mdt",            "America/Denver",   "Mountain Daylight Time",  -360, "MDT" },
    { "mt",             "America/Denver",   "Mountain Time",           -420, "MT"  },
    { "pst",            "America/Los_Angeles", "Pacific Standard Time", -480, "PST" },
    { "pdt",            "America/Los_Angeles", "Pacific Daylight Time", -420, "PDT" },
    { "pt",             "America/Los_Angeles", "Pacific Time",         -480, "PT"  },
    { "akst",           "America/Anchorage",   "Alaska Standard Time", -540, "AKST" },
    { "akdt",           "America/Anchorage",   "Alaska Daylight Time", -480, "AKDT" },
    { "hst",            "Pacific/Honolulu",    "Hawaii Standard Time", -600, "HST" },

    /* North America Cities */
    { "new york",       "America/New_York", "New York, USA",           -300, "ET"  },
    { "nyc",            "America/New_York", "New York, USA",           -300, "ET"  },
    { "los angeles",    "America/Los_Angeles", "Los Angeles, USA",     -480, "PT"  },
    { "la",             "America/Los_Angeles", "Los Angeles, USA",     -480, "PT"  },
    { "san francisco",  "America/Los_Angeles", "San Francisco, USA",   -480, "PT"  },
    { "sf",             "America/Los_Angeles", "San Francisco, USA",   -480, "PT"  },
    { "chicago",        "America/Chicago",  "Chicago, USA",            -360, "CT"  },
    { "denver",         "America/Denver",   "Denver, USA",             -420, "MT"  },
    { "seattle",        "America/Los_Angeles", "Seattle, USA",         -480, "PT"  },
    { "boston",         "America/New_York", "Boston, USA",             -300, "ET"  },
    { "miami",          "America/New_York", "Miami, USA",              -300, "ET"  },
    { "toronto",        "America/Toronto",  "Toronto, Canada",         -300, "ET"  },
    { "vancouver",      "America/Vancouver","Vancouver, Canada",       -480, "PT"  },
    { "montreal",       "America/Toronto",  "Montreal, Canada",        -300, "ET"  },
    { "mexico city",    "America/Mexico_City","Mexico City, Mexico",   -360, "CST" },

    /* Europe */
    { "london",         "Europe/London",    "London, UK",                 0, "GMT" },
    { "bst",            "Europe/London",    "British Summer Time",       60, "BST" },
    { "wet",            "Europe/Lisbon",    "Western European Time",      0, "WET" },
    { "west",           "Europe/Lisbon",    "Western European Summer",   60, "WEST"},
    { "cet",            "Europe/Paris",     "Central European Time",     60, "CET" },
    { "cest",           "Europe/Paris",     "Central European Summer",  120, "CEST"},
    { "eet",            "Europe/Athens",    "Eastern European Time",    120, "EET" },
    { "eest",           "Europe/Athens",    "Eastern European Summer",  180, "EEST"},
    { "paris",          "Europe/Paris",     "Paris, France",             60, "CET" },
    { "berlin",         "Europe/Berlin",    "Berlin, Germany",           60, "CET" },
    { "amsterdam",      "Europe/Amsterdam", "Amsterdam, Netherlands",    60, "CET" },
    { "rome",           "Europe/Rome",      "Rome, Italy",               60, "CET" },
    { "madrid",         "Europe/Madrid",    "Madrid, Spain",             60, "CET" },
    { "zurich",         "Europe/Zurich",    "Zurich, Switzerland",       60, "CET" },
    { "vienna",         "Europe/Vienna",    "Vienna, Austria",           60, "CET" },
    { "brussels",       "Europe/Brussels",  "Brussels, Belgium",         60, "CET" },
    { "dublin",         "Europe/Dublin",    "Dublin, Ireland",            0, "GMT" },
    { "lisbon",         "Europe/Lisbon",    "Lisbon, Portugal",           0, "WET" },
    { "stockholm",      "Europe/Stockholm", "Stockholm, Sweden",         60, "CET" },
    { "oslo",           "Europe/Oslo",      "Oslo, Norway",              60, "CET" },
    { "copenhagen",     "Europe/Copenhagen","Copenhagen, Denmark",       60, "CET" },
    { "helsinki",       "Europe/Helsinki",  "Helsinki, Finland",        120, "EET" },
    { "athens",         "Europe/Athens",    "Athens, Greece",           120, "EET" },
    { "istanbul",       "Europe/Istanbul",  "Istanbul, Turkey",         180, "TRT" },
    { "moscow",         "Europe/Moscow",    "Moscow, Russia",           180, "MSK" },
    { "msk",            "Europe/Moscow",    "Moscow Standard Time",     180, "MSK" },

    /* Asia */
    { "ist",            "Asia/Kolkata",     "India Standard Time",      330, "IST" },
    { "delhi",          "Asia/Kolkata",     "Delhi, India",             330, "IST" },
    { "new delhi",      "Asia/Kolkata",     "New Delhi, India",         330, "IST" },
    { "mumbai",         "Asia/Kolkata",     "Mumbai, India",            330, "IST" },
    { "bombay",         "Asia/Kolkata",     "Mumbai, India",            330, "IST" },
    { "bangalore",      "Asia/Kolkata",     "Bengaluru, India",         330, "IST" },
    { "bengaluru",      "Asia/Kolkata",     "Bengaluru, India",         330, "IST" },
    { "kolkata",        "Asia/Kolkata",     "Kolkata, India",           330, "IST" },
    { "calcutta",       "Asia/Kolkata",     "Kolkata, India",           330, "IST" },
    { "chennai",        "Asia/Kolkata",     "Chennai, India",           330, "IST" },
    { "madras",         "Asia/Kolkata",     "Chennai, India",           330, "IST" },
    { "hyderabad",      "Asia/Kolkata",     "Hyderabad, India",         330, "IST" },
    { "dubai",          "Asia/Dubai",       "Dubai, UAE",               240, "GST" },
    { "gst",            "Asia/Dubai",       "Gulf Standard Time",       240, "GST" },
    { "abu dhabi",      "Asia/Dubai",       "Abu Dhabi, UAE",           240, "GST" },
    { "doha",           "Asia/Qatar",       "Doha, Qatar",              180, "AST" },
    { "riyadh",         "Asia/Riyadh",      "Riyadh, Saudi Arabia",     180, "AST" },
    { "tokyo",          "Asia/Tokyo",       "Tokyo, Japan",             540, "JST" },
    { "jst",            "Asia/Tokyo",       "Japan Standard Time",      540, "JST" },
    { "japan",          "Asia/Tokyo",       "Japan",                    540, "JST" },
    { "seoul",          "Asia/Seoul",       "Seoul, South Korea",       540, "KST" },
    { "kst",            "Asia/Seoul",       "Korea Standard Time",      540, "KST" },
    { "korea",          "Asia/Seoul",       "South Korea",              540, "KST" },
    { "singapore",      "Asia/Singapore",   "Singapore",                480, "SGT" },
    { "sgt",            "Asia/Singapore",   "Singapore Time",           480, "SGT" },
    { "hong kong",      "Asia/Hong_Kong",   "Hong Kong",                480, "HKT" },
    { "hkt",            "Asia/Hong_Kong",   "Hong Kong Time",           480, "HKT" },
    { "shanghai",       "Asia/Shanghai",    "Shanghai, China",          480, "CST" },
    { "beijing",        "Asia/Shanghai",    "Beijing, China",           480, "CST" },
    { "china",          "Asia/Shanghai",    "China Standard Time",      480, "CST" },
    { "taipei",         "Asia/Taipei",      "Taipei, Taiwan",           480, "CST" },
    { "taiwan",         "Asia/Taipei",      "Taiwan",                   480, "CST" },
    { "bangkok",        "Asia/Bangkok",     "Bangkok, Thailand",        420, "ICT" },
    { "thailand",       "Asia/Bangkok",     "Thailand",                 420, "ICT" },
    { "jakarta",        "Asia/Jakarta",     "Jakarta, Indonesia",       420, "WIB" },
    { "bali",           "Asia/Makassar",    "Bali, Indonesia",          480, "WITA"},
    { "kuala lumpur",   "Asia/Kuala_Lumpur","Kuala Lumpur, Malaysia",   480, "MYT" },
    { "malaysia",       "Asia/Kuala_Lumpur","Malaysia",                 480, "MYT" },
    { "manila",         "Asia/Manila",      "Manila, Philippines",      480, "PHT" },
    { "philippines",    "Asia/Manila",      "Philippines",              480, "PHT" },
    { "karachi",        "Asia/Karachi",     "Karachi, Pakistan",        300, "PKT" },
    { "pkt",            "Asia/Karachi",     "Pakistan Standard Time",   300, "PKT" },
    { "pakistan",       "Asia/Karachi",     "Pakistan",                 300, "PKT" },
    { "dhaka",          "Asia/Dhaka",       "Dhaka, Bangladesh",        360, "BDT" },
    { "bdt",            "Asia/Dhaka",       "Bangladesh Standard Time", 360, "BDT" },
    { "kathmandu",      "Asia/Kathmandu",   "Kathmandu, Nepal",         345, "NPT" },
    { "npt",            "Asia/Kathmandu",   "Nepal Time",               345, "NPT" },
    { "nepal",          "Asia/Kathmandu",   "Nepal",                    345, "NPT" },
    { "colombo",        "Asia/Colombo",     "Colombo, Sri Lanka",       330, "IST" },

    /* Oceania / Australia / NZ */
    { "sydney",         "Australia/Sydney", "Sydney, Australia",        600, "AEST"},
    { "melbourne",      "Australia/Melbourne","Melbourne, Australia",   600, "AEST"},
    { "brisbane",       "Australia/Brisbane","Brisbane, Australia",     600, "AEST"},
    { "perth",          "Australia/Perth",  "Perth, Australia",         480, "AWST"},
    { "adelaide",       "Australia/Adelaide","Adelaide, Australia",     570, "ACST"},
    { "aest",           "Australia/Sydney", "Australian Eastern Time",  600, "AEST"},
    { "aedt",           "Australia/Sydney", "Australian Eastern Daylight",660,"AEDT"},
    { "acst",           "Australia/Adelaide","Australian Central Time", 570, "ACST"},
    { "awst",           "Australia/Perth",  "Australian Western Time",  480, "AWST"},
    { "auckland",       "Pacific/Auckland", "Auckland, New Zealand",    720, "NZST"},
    { "nz",             "Pacific/Auckland", "New Zealand",              720, "NZST"},
    { "new zealand",    "Pacific/Auckland", "New Zealand",              720, "NZST"},
    { "nzst",           "Pacific/Auckland", "New Zealand Standard Time",720, "NZST"},
    { "nzdt",           "Pacific/Auckland", "New Zealand Daylight Time",780, "NZDT"},

    /* Africa & South America */
    { "cairo",          "Africa/Cairo",     "Cairo, Egypt",             120, "EET" },
    { "johannesburg",   "Africa/Johannesburg","Johannesburg, South Africa",120,"SAST"},
    { "sast",           "Africa/Johannesburg","South Africa Standard",  120, "SAST"},
    { "nairobi",        "Africa/Nairobi",   "Nairobi, Kenya",           180, "EAT" },
    { "sao paulo",      "America/Sao_Paulo","São Paulo, Brazil",       -180, "BRT" },
    { "brazil",         "America/Sao_Paulo","Brazil",                  -180, "BRT" },
    { "buenos aires",   "America/Argentina/Buenos_Aires","Buenos Aires",-180,"ART" },

    { NULL, NULL, NULL, 0, NULL }
};

static void trim_lower(const char *src, char *dst, size_t dst_sz) {
    if (!src || !dst || dst_sz == 0) return;
    while (*src && isspace((unsigned char)*src)) src++;
    size_t i = 0;
    while (*src && i < dst_sz - 1) {
        dst[i++] = (char)tolower((unsigned char)*src++);
    }
    while (i > 0 && isspace((unsigned char)dst[i - 1])) {
        i--;
    }
    dst[i] = '\0';
}

/*
 * Parses numeric offsets like "utc+5:30", "utc-8", "gmt+2", "+05:30", "-4", "+5.5"
 */
static bool parse_numeric_offset(const char *query, int *out_minutes, char *out_abbrev, size_t abbrev_sz) {
    if (!query || !*query) return false;

    const char *p = query;
    while (*p && isspace((unsigned char)*p)) p++;

    if (strncasecmp(p, "utc", 3) == 0) p += 3;
    else if (strncasecmp(p, "gmt", 3) == 0) p += 3;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p != '+' && *p != '-') return false;
    int sign = (*p == '-') ? -1 : 1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;

    if (!isdigit((unsigned char)*p)) return false;

    double hours = 0.0;
    char *endptr = NULL;
    hours = strtod(p, &endptr);
    if (endptr == p) return false;

    int total_minutes = 0;
    if (*endptr == ':') {
        int h = (int)hours;
        endptr++;
        int m = (int)strtol(endptr, &endptr, 10);
        total_minutes = sign * (h * 60 + m);
    } else {
        total_minutes = (int)round(sign * hours * 60.0);
    }

    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return false;

    if (total_minutes < -840 || total_minutes > 840) return false; /* -14h to +14h */

    if (out_minutes) *out_minutes = total_minutes;
    if (out_abbrev && abbrev_sz > 0) {
        int ah = abs(total_minutes) / 60;
        int am = abs(total_minutes) % 60;
        if (am == 0) {
            snprintf(out_abbrev, abbrev_sz, "UTC%c%d", total_minutes >= 0 ? '+' : '-', ah);
        } else {
            snprintf(out_abbrev, abbrev_sz, "UTC%c%02d:%02d", total_minutes >= 0 ? '+' : '-', ah, am);
        }
    }
    return true;
}

bool is_valid_timezone_query(const char *tz_query) {
    if (!tz_query || !*tz_query) return false;
    char clean[64];
    trim_lower(tz_query, clean, sizeof(clean));

    int dummy = 0;
    if (parse_numeric_offset(clean, &dummy, NULL, 0)) return true;

    for (int i = 0; TZ_TABLE[i].query != NULL; i++) {
        if (strcmp(clean, TZ_TABLE[i].query) == 0) return true;
    }
    return false;
}

bool parse_source_timezone_offset(const char *tz_query, time_t ref_time, int *out_offset_minutes, char *out_abbrev, size_t abbrev_sz) {
    if (!tz_query || !*tz_query) return false;
    char clean[64];
    trim_lower(tz_query, clean, sizeof(clean));

    if (parse_numeric_offset(clean, out_offset_minutes, out_abbrev, abbrev_sz)) {
        return true;
    }

    for (int i = 0; TZ_TABLE[i].query != NULL; i++) {
        if (strcmp(clean, TZ_TABLE[i].query) == 0) {
            const TimezoneEntry *entry = &TZ_TABLE[i];

            /* Try system IANA tzset */
            char *orig_tz = getenv("TZ");
            char orig_tz_buf[128] = "";
            bool had_orig_tz = (orig_tz != NULL);
            if (had_orig_tz) {
                strncpy(orig_tz_buf, orig_tz, sizeof(orig_tz_buf) - 1);
            }

            setenv("TZ", entry->iana_name, 1);
            tzset();

            struct tm target_tm;
            localtime_r(&ref_time, &target_tm);

            if (had_orig_tz) {
                setenv("TZ", orig_tz_buf, 1);
            } else {
                unsetenv("TZ");
            }
            tzset();

            int off_min = (int)(target_tm.tm_gmtoff / 60);
            if (out_offset_minutes) *out_offset_minutes = off_min;
            if (out_abbrev && abbrev_sz > 0) {
                if (target_tm.tm_zone && target_tm.tm_zone[0]) {
                    strncpy(out_abbrev, target_tm.tm_zone, abbrev_sz - 1);
                    out_abbrev[abbrev_sz - 1] = '\0';
                } else {
                    strncpy(out_abbrev, entry->fallback_abbrev, abbrev_sz - 1);
                    out_abbrev[abbrev_sz - 1] = '\0';
                }
            }
            return true;
        }
    }

    return false;
}

bool convert_to_timezone(time_t epoch, const char *tz_query, TimezoneResult *res) {
    time_t cur_now = time(NULL);
    return convert_to_timezone_ex(epoch, cur_now, tz_query, res);
}

bool convert_to_timezone_ex(time_t epoch, time_t ref_time, const char *tz_query, TimezoneResult *res) {
    if (!tz_query || !res) return false;
    memset(res, 0, sizeof(*res));

    char clean[64];
    trim_lower(tz_query, clean, sizeof(clean));

    const TimezoneEntry *matched_entry = NULL;
    int numeric_offset = 0;
    char num_abbrev[16] = "";
    bool is_numeric = parse_numeric_offset(clean, &numeric_offset, num_abbrev, sizeof(num_abbrev));

    if (!is_numeric) {
        for (int i = 0; TZ_TABLE[i].query != NULL; i++) {
            if (strcmp(clean, TZ_TABLE[i].query) == 0) {
                matched_entry = &TZ_TABLE[i];
                break;
            }
        }
        if (!matched_entry) return false;
    }

    struct tm target_tm;
    memset(&target_tm, 0, sizeof(target_tm));

    if (is_numeric) {
        snprintf(res->name, sizeof(res->name), "%s", num_abbrev);
        snprintf(res->abbrev, sizeof(res->abbrev), "%s", num_abbrev);
        res->offset_minutes = numeric_offset;

        time_t shifted = epoch + (numeric_offset * 60);
        gmtime_r(&shifted, &target_tm);
    } else {
        snprintf(res->name, sizeof(res->name), "%s", matched_entry->display);

        char *orig_tz = getenv("TZ");
        char orig_tz_buf[128] = "";
        bool had_orig_tz = (orig_tz != NULL);
        if (had_orig_tz) {
            strncpy(orig_tz_buf, orig_tz, sizeof(orig_tz_buf) - 1);
        }

        setenv("TZ", matched_entry->iana_name, 1);
        tzset();

        localtime_r(&epoch, &target_tm);

        if (had_orig_tz) {
            setenv("TZ", orig_tz_buf, 1);
        } else {
            unsetenv("TZ");
        }
        tzset();

        res->offset_minutes = (int)(target_tm.tm_gmtoff / 60);
        if (target_tm.tm_zone && target_tm.tm_zone[0]) {
            strncpy(res->abbrev, target_tm.tm_zone, sizeof(res->abbrev) - 1);
        } else {
            strncpy(res->abbrev, matched_entry->fallback_abbrev, sizeof(res->abbrev) - 1);
        }
    }

    /* Reference local system time */
    struct tm local_tm;
    localtime_r(&ref_time, &local_tm);

    /* Exact calendar day difference calculation */
    long target_days = 0, local_days = 0;
    {
        int y1 = target_tm.tm_year + 1900, m1 = target_tm.tm_mon + 1;
        if (m1 <= 2) { y1 -= 1; m1 += 12; }
        long era1 = (y1 >= 0 ? y1 : y1 - 399) / 400;
        long yoe1 = y1 - era1 * 400;
        long doy1 = (153 * (m1 - 3) + 2) / 5 + target_tm.tm_mday - 1;
        long doe1 = yoe1 * 365 + yoe1 / 4 - yoe1 / 100 + doy1;
        target_days = era1 * 146097 + doe1 - 719468;

        int y2 = local_tm.tm_year + 1900, m2 = local_tm.tm_mon + 1;
        if (m2 <= 2) { y2 -= 1; m2 += 12; }
        long era2 = (y2 >= 0 ? y2 : y2 - 399) / 400;
        long yoe2 = y2 - era2 * 400;
        long doy2 = (153 * (m2 - 3) + 2) / 5 + local_tm.tm_mday - 1;
        long doe2 = yoe2 * 365 + yoe2 / 4 - yoe2 / 100 + doy2;
        local_days = era2 * 146097 + doe2 - 719468;
    }
    long day_diff = target_days - local_days;

    if (day_diff == 1) {
        res->day_shift = 1;
    } else if (day_diff == -1) {
        res->day_shift = -1;
    } else {
        res->day_shift = 0;
    }

    /* Format Date */
    static const char *m_shorts[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
    };
    const char *m_str = (target_tm.tm_mon >= 0 && target_tm.tm_mon < 12) ? m_shorts[target_tm.tm_mon] : "Jan";
    snprintf(res->date_str, sizeof(res->date_str), "%02d-%s-%04d",
             target_tm.tm_mday, m_str, target_tm.tm_year + 1900);

    /* Format Weekday */
    strftime(res->weekday_str, sizeof(res->weekday_str), "%A", &target_tm);

    /* Format Date & Time */
    int hour12 = target_tm.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *am_pm = target_tm.tm_hour >= 12 ? "PM" : "AM";

    if (res->day_shift > 0) {
        snprintf(res->time_str, sizeof(res->time_str), "Tomorrow, %s %02d:%02d:%02d %s",
                 res->date_str, hour12, target_tm.tm_min, target_tm.tm_sec, am_pm);
    } else if (res->day_shift < 0) {
        snprintf(res->time_str, sizeof(res->time_str), "Yesterday, %s %02d:%02d:%02d %s",
                 res->date_str, hour12, target_tm.tm_min, target_tm.tm_sec, am_pm);
    } else {
        snprintf(res->time_str, sizeof(res->time_str), "%s %02d:%02d:%02d %s",
                 res->date_str, hour12, target_tm.tm_min, target_tm.tm_sec, am_pm);
    }

    /* Format Offset */
    int off_h = abs(res->offset_minutes) / 60;
    int off_m = abs(res->offset_minutes) % 60;
    snprintf(res->offset_str, sizeof(res->offset_str), "UTC%c%02d:%02d",
             res->offset_minutes >= 0 ? '+' : '-', off_h, off_m);

    /* Relative difference from local */
    int local_off_min = (int)(local_tm.tm_gmtoff / 60);
    int diff_min = res->offset_minutes - local_off_min;
    if (diff_min == 0) {
        snprintf(res->diff_from_local, sizeof(res->diff_from_local), "Same time as local");
    } else {
        int dh = abs(diff_min) / 60;
        int dm = abs(diff_min) % 60;
        if (dm == 0) {
            snprintf(res->diff_from_local, sizeof(res->diff_from_local), "%c%dh %s local",
                     diff_min > 0 ? '+' : '-', dh, diff_min > 0 ? "ahead of" : "behind");
        } else {
            snprintf(res->diff_from_local, sizeof(res->diff_from_local), "%c%dh %02dm %s local",
                     diff_min > 0 ? '+' : '-', dh, dm, diff_min > 0 ? "ahead of" : "behind");
        }
    }

    return true;
}

void get_timezone_hints(char *hint_out, size_t hint_sz) {
    if (!hint_out || hint_sz == 0) return;
    snprintf(hint_out, hint_sz,
             "Timezones: est, pst, cst, gmt, utc, ist, jst, cet, tokyo, london, nyc, dubai, singapore, sydney, utc+5:30...");
}

bool get_matching_timezone_hints(const char *prefix, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return false;
    out[0] = '\0';

    if (!prefix || !*prefix) {
        snprintf(out, out_sz, "Timezones: est, pst, cst, gmt, utc, ist, jst, cet, tokyo, london, nyc, dubai, singapore, sydney...");
        return true;
    }

    char clean[64];
    trim_lower(prefix, clean, sizeof(clean));
    size_t plen = strlen(clean);

    char matches[256] = "";
    int match_count = 0;

    for (int i = 0; TZ_TABLE[i].query != NULL; i++) {
        if (strncasecmp(TZ_TABLE[i].query, clean, plen) == 0) {
            if (match_count > 0 && strlen(matches) + 2 < sizeof(matches)) {
                strcat(matches, ", ");
            }
            if (strlen(matches) + strlen(TZ_TABLE[i].query) < sizeof(matches)) {
                strcat(matches, TZ_TABLE[i].query);
                match_count++;
            }
            if (match_count >= 8) break;
        }
    }

    if (match_count > 0) {
        snprintf(out, out_sz, "Timezones: %s", matches);
        return true;
    }
    return false;
}

/*
 * Business / Working Days Calculations
 */
time_t add_business_days(time_t start, int count) {
    if (count == 0) return start;
    struct tm tm_info;
    localtime_r(&start, &tm_info);
    int step = (count > 0) ? 1 : -1;
    int remaining = abs(count);
    while (remaining > 0) {
        tm_info.tm_mday += step;
        tm_info.tm_isdst = -1;
        mktime(&tm_info);
        /* Sunday is 0, Saturday is 6 */
        if (tm_info.tm_wday != 0 && tm_info.tm_wday != 6) {
            remaining--;
        }
    }
    return mktime(&tm_info);
}

int count_business_days(time_t t1, time_t t2) {
    struct tm tm1, tm2;
    localtime_r(&t1, &tm1);
    localtime_r(&t2, &tm2);
    tm1.tm_hour = tm1.tm_min = tm1.tm_sec = 0; tm1.tm_isdst = -1;
    tm2.tm_hour = tm2.tm_min = tm2.tm_sec = 0; tm2.tm_isdst = -1;
    time_t start = mktime(&tm1);
    time_t end = mktime(&tm2);
    int sign = 1;
    if (start > end) {
        time_t tmp = start; start = end; end = tmp;
        sign = -1;
    }
    struct tm cur;
    localtime_r(&start, &cur);
    int bdays = 0;
    while (1) {
        cur.tm_mday += 1;
        cur.tm_isdst = -1;
        time_t cur_t = mktime(&cur);
        if (cur_t > end) break;
        if (cur.tm_wday != 0 && cur.tm_wday != 6) {
            bdays++;
        }
    }
    return sign * bdays;
}

