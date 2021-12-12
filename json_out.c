#include "readsb.h"

/*
__attribute__ ((format(printf, 3, 0))) static char *safe_vsnprintf(char *p, char *end, const char *format, va_list ap) {
    p += vsnprintf(p < end ? p : NULL, p < end ? (size_t) (end - p) : 0, format, ap);
    return p;
}
*/

static const char *trimSpace(const char *in, char *out, int len) {

    out[len] = '\0';
    int found = 0;

    for (int i = len - 1; i >= 0; i--) {
        if (!found && in[i] == ' ') {
            out[i] = '\0';
        } else if (in[i] == '\0') {
            out[i] = '\0';
        } else {
            out[i] = in[i];
            found = 1; // found non space character
        }
    }

    return out;
}
//
//=========================================================================
//
// Return a description of planes in json. No metric conversion
//
static const char *jsonEscapeString(const char *str, char *buf, int len) {
    const char *in = str;
    char *out = buf, *end = buf + len - 10;

    for (; *in && out < end; ++in) {
        unsigned char ch = *in;
        if (ch == '"' || ch == '\\') {
            *out++ = '\\';
            *out++ = ch;
        } else if (ch < 32 || ch > 126) {
            out = safe_snprintf(out, end, "\\u%04x", ch);
        } else {
            *out++ = ch;
        }
    }

    *out++ = 0;
    return buf;
}

static inline double getSignal(struct aircraft *a) {
    double sum;
    if (likely(a->signalNext >= 8)) {
        sum = a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
            a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7];
    } else if (a->signalNext >= 4) {
        sum = 0;
        for (uint32_t i = 0; i < a->signalNext; i++) {
            sum += a->signalLevel[i];
        }
    } else {
        sum = 0;
    }

    return 10 * log10(sum / 8 + 1.125e-5);
}

static char *append_flags(char *p, char *end, struct aircraft *a, datasource_t source) {
    p = safe_snprintf(p, end, "[");

    char *start = p;
    if (a->callsign_valid.source == source)
        p = safe_snprintf(p, end, "\"callsign\",");
    if (a->altitude_baro_valid.source == source)
        p = safe_snprintf(p, end, "\"altitude\",");
    if (a->altitude_geom_valid.source == source)
        p = safe_snprintf(p, end, "\"alt_geom\",");
    if (a->gs_valid.source == source)
        p = safe_snprintf(p, end, "\"gs\",");
    if (a->ias_valid.source == source)
        p = safe_snprintf(p, end, "\"ias\",");
    if (a->tas_valid.source == source)
        p = safe_snprintf(p, end, "\"tas\",");
    if (a->mach_valid.source == source)
        p = safe_snprintf(p, end, "\"mach\",");
    if (a->track_valid.source == source)
        p = safe_snprintf(p, end, "\"track\",");
    if (a->track_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"track_rate\",");
    if (a->roll_valid.source == source)
        p = safe_snprintf(p, end, "\"roll\",");
    if (a->mag_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"mag_heading\",");
    if (a->true_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"true_heading\",");
    if (a->baro_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"baro_rate\",");
    if (a->geom_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"geom_rate\",");
    if (a->squawk_valid.source == source)
        p = safe_snprintf(p, end, "\"squawk\",");
    if (a->emergency_valid.source == source)
        p = safe_snprintf(p, end, "\"emergency\",");
    if (a->nav_qnh_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_qnh\",");
    if (a->nav_altitude_mcp_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_altitude_mcp\",");
    if (a->nav_altitude_fms_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_altitude_fms\",");
    if (a->nav_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_heading\",");
    if (a->nav_modes_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_modes\",");
    if (a->position_valid.source == source)
        p = safe_snprintf(p, end, "\"lat\",\"lon\",\"nic\",\"rc\",");
    if (a->nic_baro_valid.source == source)
        p = safe_snprintf(p, end, "\"nic_baro\",");
    if (a->nac_p_valid.source == source)
        p = safe_snprintf(p, end, "\"nac_p\",");
    if (a->nac_v_valid.source == source)
        p = safe_snprintf(p, end, "\"nac_v\",");
    if (a->sil_valid.source == source)
        p = safe_snprintf(p, end, "\"sil\",\"sil_type\",");
    if (a->gva_valid.source == source)
        p = safe_snprintf(p, end, "\"gva\",");
    if (a->sda_valid.source == source)
        p = safe_snprintf(p, end, "\"sda\",");
    if (p != start)
        --p;
    p = safe_snprintf(p, end, "]");
    return p;
}

static struct {
    nav_modes_t flag;
    const char *name;
} nav_modes_names[] = {
    { NAV_MODE_AUTOPILOT, "autopilot"},
    { NAV_MODE_VNAV, "vnav"},
    { NAV_MODE_ALT_HOLD, "althold"},
    { NAV_MODE_APPROACH, "approach"},
    { NAV_MODE_LNAV, "lnav"},
    { NAV_MODE_TCAS, "tcas"},
    { 0, NULL}
};

static char *append_nav_modes(char *p, char *end, nav_modes_t flags, const char *quote, const char *sep) {
    int first = 1;
    for (int i = 0; nav_modes_names[i].name; ++i) {
        if (!(flags & nav_modes_names[i].flag)) {
            continue;
        }

        if (!first) {
            p = safe_snprintf(p, end, "%s", sep);
        }

        first = 0;
        p = safe_snprintf(p, end, "%s%s%s", quote, nav_modes_names[i].name, quote);
    }

    return p;
}

const char *nav_modes_flags_string(nav_modes_t flags) {
    static char buf[256];
    buf[0] = 0;
    append_nav_modes(buf, buf + sizeof (buf), flags, "", " ");
    return buf;

}

void printACASInfoShort(uint32_t addr, unsigned char *MV, struct aircraft *a, struct modesMessage *mm, uint64_t now) {
    char buf[512];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = sprintACASInfoShort(p, end, addr, MV, a, mm, now);

    if (p == buf) // nothing written
        return;
    if (p - buf >= (int) sizeof(buf)) {
        fprintf(stderr, "printACAS buffer insufficient!\n");
        return;
    }

    printf("%s\n", buf);
    fflush(stdout); // FLUSH
}

void logACASInfoShort(uint32_t addr, unsigned char *bytes, struct aircraft *a, struct modesMessage *mm, uint64_t now) {

    static int64_t lastLogTimestamp1, lastLogTimestamp2;
    static uint32_t lastLogAddr1, lastLogAddr2;
    static char lastLogBytes1[7], lastLogBytes2[7];
    bool rat = getbit(bytes, 27); // clear of conflict / RA terminated
    int deduplicationInterval = rat ? 5000 : 200; // in ms
    if (lastLogAddr1 == addr && (int64_t) now - lastLogTimestamp1 < deduplicationInterval && !memcmp(lastLogBytes1, bytes, 7)) {
        return;
    }
    if (lastLogAddr2 == addr && (int64_t) now - lastLogTimestamp2 < deduplicationInterval && !memcmp(lastLogBytes2, bytes, 7)) {
        return;
    }

    if (addr == lastLogAddr1 || (addr != lastLogAddr2 && lastLogTimestamp2 > lastLogTimestamp1)) {
        lastLogAddr1 = addr;
        lastLogTimestamp1 = now;
        memcpy(lastLogBytes1, bytes, 7);
    } else {
        lastLogAddr2 = addr;
        lastLogTimestamp2 = now;
        memcpy(lastLogBytes2, bytes, 7);
    }


    if (Modes.acasFD1 > 0) {

        char buf[512];
        char *p = buf;
        char *end = buf + sizeof(buf);
        p = sprintACASInfoShort(p, end, addr, bytes, a, mm, now);

        p = safe_snprintf(p, end, "\n");
        if (p - buf >= (int) sizeof(buf) - 1) {
            fprintf(stderr, "logACAS csv buffer insufficient!\n");
        } else {
            check_write(Modes.acasFD1, buf, p - buf, "acas.csv");
        }
    }
    if (Modes.acasFD2 > 0) {

        char buf[2048];
        char *p = buf;
        char *end = buf + sizeof(buf);

        p = sprintAircraftObject(p, end, a, now, 0, mm);
        p = safe_snprintf(p, end, "\n");

        if (p - buf >= (int) sizeof(buf) - 1) {
            fprintf(stderr, "logACAS json buffer insufficient!\n");
        } else {
            check_write(Modes.acasFD2, buf, p - buf, "acas.csv");
        }
    }
}

static char *sprintACASJson(char *p, char *end, unsigned char *bytes, struct modesMessage *mm, uint64_t now) {
    bool ara = getbit(bytes, 9);
    bool rat = getbit(bytes, 27);
    bool mte = getbit(bytes, 28);

    char timebuf[128];
    struct tm utc;

    time_t time = now / 1000;
    gmtime_r(&time, &utc);
    strftime(timebuf, 128, "%F %T", &utc);
    timebuf[127] = 0;

    p = safe_snprintf(p, end, "{\"utc\":\"%s.%d\"", timebuf, (int)((now % 1000) / 100));

    p = safe_snprintf(p, end, ",\"unix_timestamp\":%.2f", now / 1000.0);


    if (mm && mm->acas_ra_valid) {
        if (Modes.debug_ACAS && !checkAcasRaValid(bytes, mm, 0)) {
            p = safe_snprintf(p, end, ",\"debug\":true");
        }
        p = safe_snprintf(p, end, ",\"df_type\":%d", mm->msgtype);
        p = safe_snprintf(p, end, ",\"full_bytes\":\"");
        for (int i = 0; i < mm->msgbits / 8; ++i) {
            p = safe_snprintf(p, end, "%02X", (unsigned) mm->msg[i]);
        }
        p = safe_snprintf(p, end, "\"");
    }

    p = safe_snprintf(p, end, ",\"bytes\":\"");
    for (int i = 0; i < 7; ++i) {
        p = safe_snprintf(p, end, "%02X", (unsigned) bytes[i]);
    }
    p = safe_snprintf(p, end, "\"");

    p = safe_snprintf(p, end, ",\"ARA\":\"");
    for (int i = 9; i <= 15; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));
    p = safe_snprintf(p, end, "\"");
    p = safe_snprintf(p, end, ",\"RAT\":\"%u\"", getbit(bytes, 27));
    p = safe_snprintf(p, end, ",\"MTE\":\"%u\"", getbit(bytes, 28));
    p = safe_snprintf(p, end, ",\"RAC\":\"");
    for (int i = 23; i <= 26; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));
    p = safe_snprintf(p, end, "\"");


    p = safe_snprintf(p, end, ",\"advisory_complement\":\"");
    if (getbits(bytes, 23, 26)) {
        bool notfirst = false;
        char *racs[4] = { "Do not pass below", "Do not pass above", "Do not turn left", "Do not turn right" };
        for (int i = 23; i <= 26; i++) {
            if (getbit(bytes, i)) {
                if (notfirst)
                    p = safe_snprintf(p, end, "; ");
                p = safe_snprintf(p, end, "%s", racs[i-23]);
                notfirst = true;
            }
        }
    }
    p = safe_snprintf(p, end, "\"");


    // https://mode-s.org/decode/book-the_1090mhz_riddle-junzi_sun.pdf
    //
    // https://www.faa.gov/documentlibrary/media/advisory_circular/tcas%20ii%20v7.1%20intro%20booklet.pdf
    /* RAs can be classified as positive (e.g.,
       climb, descend) or negative (e.g., limit climb
       to 0 fpm, limit descend to 500 fpm). The
       term "Vertical Speed Limit" (VSL) is
       equivalent to "negative." RAs can also be
       classified as preventive or corrective,
       depending on whether own aircraft is, or is
       not, in conformance with the RA target
       altitude rate. Corrective RAs require a
       change in vertical speed; preventive RAs do
       not require a change in vertical speed
       */
    p = safe_snprintf(p, end, ",\"advisory\":\"");
    if (rat) {
        p = safe_snprintf(p, end, "Clear of Conflict");
    } else if (ara) {
        bool corr = getbit(bytes, 10); // corrective / preventive
        bool down = getbit(bytes, 11); // downward sense / upward sense
        bool increase = getbit(bytes, 12); // increase rate
        bool reversal = getbit(bytes, 13); // sense reversal
        bool crossing = getbit(bytes, 14); // altitude crossing
        bool positive = getbit(bytes, 15);
        // positive: (Maintain climb / descent) / (Climb / descend): requires more than 1500 fpm vertical rate
        // !positive: (Do not / reduce) (climb / descend)

        if (corr && positive) {
            if (!reversal) {
                // reversal has priority and comes later
            } else if (increase) {
                p = safe_snprintf(p, end, "Increase ");
            }

            if (down)
                p = safe_snprintf(p, end, "Descend");
            else
                p = safe_snprintf(p, end, "Climb");

            if (reversal) {
                if (down)
                    p = safe_snprintf(p, end, "; Descend");
                else
                    p = safe_snprintf(p, end, "; Climb");
                p = safe_snprintf(p, end, " NOW");
            }

            if (crossing) {
                p = safe_snprintf(p, end, "; Crossing");
                if (down)
                    p = safe_snprintf(p, end, " Descend");
                else
                    p = safe_snprintf(p, end, " Climb");
            }
        }

        if (corr && !positive) {
            p = safe_snprintf(p, end, "Level Off");
        }

        if (!corr && positive) {
            p = safe_snprintf(p, end, "Maintain vertical Speed");
            if (crossing) {
                p = safe_snprintf(p, end, "; Crossing Maintain");
            }
        }

        if (!corr && !positive) {
            p = safe_snprintf(p, end, "Monitor vertical Speed");
        }

    } else if (!ara && mte) {
        if (getbit(bytes, 10))
            p = safe_snprintf(p, end, " Correct upwards;");
        if (getbit(bytes, 11))
            p = safe_snprintf(p, end, " Climb required;");
        if (getbit(bytes, 12))
            p = safe_snprintf(p, end, " Correct downwards;");
        if (getbit(bytes, 13))
            p = safe_snprintf(p, end, " Descent required;");
        if (getbit(bytes, 14))
            p = safe_snprintf(p, end, " Crossing;");
        if (getbit(bytes, 15))
            p = safe_snprintf(p, end, " Increase / Maintain vertical rate");
        else
            p = safe_snprintf(p, end, " Reduce / Limit vertical rate");
    }
    p = safe_snprintf(p, end, "\"");

    int tti = getbits(bytes, 29, 30);
    p = safe_snprintf(p, end, ",\"TTI\":\"");
    for (int i = 29; i <= 30; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));
    p = safe_snprintf(p, end, "\"");
    if (tti == 1) {
        uint32_t threatAddr = getbits(bytes, 31, 54);
        p = safe_snprintf(p, end, ",\"threat_id_hex\":\"%06x\"", threatAddr);
    }

    p = safe_snprintf(p, end, "}");

    return p;
}

char *sprintACASInfoShort(char *p, char *end, uint32_t addr, unsigned char *bytes, struct aircraft *a, struct modesMessage *mm, uint64_t now) {
    bool ara = getbit(bytes, 9);
    bool rat = getbit(bytes, 27);
    bool mte = getbit(bytes, 28);

    char timebuf[128];
    struct tm utc;

    time_t time = now / 1000;
    gmtime_r(&time, &utc);
    strftime(timebuf, 128, "%F", &utc);
    timebuf[127] = 0;

    int debug = 0;

    if (Modes.debug_ACAS && mm && !checkAcasRaValid(bytes, mm, 0)) {
        debug = 1;
        p = safe_snprintf(p, end, "DEBUG     ");
    } else {
        p = safe_snprintf(p, end, "%s", timebuf);
    }

    p = safe_snprintf(p, end, ",");

    strftime(timebuf, 128, "%T", &utc);
    timebuf[127] = 0;
    p = safe_snprintf(p, end, "%s.%d, %06x,DF:,", timebuf, (int)((now % 1000) / 100), addr);
    if (mm)
        p = safe_snprintf(p, end, "%2u", mm->msgtype);
    else
        p = safe_snprintf(p, end, "  ");

    p = safe_snprintf(p, end, ",bytes:,");
    for (int i = 0; i < 7; ++i) {
        p = safe_snprintf(p, end, "%02X", (unsigned) bytes[i]);
    }
    p = safe_snprintf(p, end, ",");

    if (a && posReliable(a))
        p = safe_snprintf(p, end, "%11.6f,", a->lat);
    else
        p = safe_snprintf(p, end, "           ,");

    if (a && posReliable(a))
        p = safe_snprintf(p, end, "%11.6f,", a->lon);
    else
        p = safe_snprintf(p, end, "           ,");

    if (a && altReliable(a))
        p = safe_snprintf(p, end, "%5d,ft,", a->altitude_baro);
    else
        p = safe_snprintf(p, end, "     ,ft,");

    if (a && trackDataValid(&a->geom_rate_valid)) {
        p = safe_snprintf(p, end, "%5d", a->geom_rate);
    } else if (a && trackDataValid(&a->baro_rate_valid)) {
        p = safe_snprintf(p, end, "%5d", a->baro_rate);
    } else {
        p = safe_snprintf(p, end, "     ");
    }
    p = safe_snprintf(p, end, ",fpm,");

    p = safe_snprintf(p, end, "ARA:,");
    for (int i = 9; i <= 15; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));
    p = safe_snprintf(p, end, ",RAT:,%u", getbit(bytes, 27));
    p = safe_snprintf(p, end, ",MTE:,%u", getbit(bytes, 28));
    p = safe_snprintf(p, end, ",RAC:,");
    for (int i = 23; i <= 26; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));

    p = safe_snprintf(p, end, ", ");

    if (getbits(bytes, 23, 26)) {
        char *racs[4] = { "not below", "not above", "not left ", "not right" };
        for (int i = 23; i <= 26; i++) {
            if (getbit(bytes, i))
                p = safe_snprintf(p, end, "%s", racs[i-23]);
        }
    } else {
        p = safe_snprintf(p, end, "         ");
    }

    p = safe_snprintf(p, end, ", ");

    // https://mode-s.org/decode/book-the_1090mhz_riddle-junzi_sun.pdf
    //
    // https://www.faa.gov/documentlibrary/media/advisory_circular/tcas%20ii%20v7.1%20intro%20booklet.pdf
    /* RAs can be classified as positive (e.g.,
       climb, descend) or negative (e.g., limit climb
       to 0 fpm, limit descend to 500 fpm). The
       term "Vertical Speed Limit" (VSL) is
       equivalent to "negative." RAs can also be
       classified as preventive or corrective,
       depending on whether own aircraft is, or is
       not, in conformance with the RA target
       altitude rate. Corrective RAs require a
       change in vertical speed; preventive RAs do
       not require a change in vertical speed
       */
    if (rat) {
        p = safe_snprintf(p, end, "Clear of Conflict");
    } else if (ara) {
        p = safe_snprintf(p, end, "RA:");
        bool corr = getbit(bytes, 10); // corrective / preventive
        bool down = getbit(bytes, 11); // downward sense / upward sense
        bool increase = getbit(bytes, 12); // increase rate
        bool reversal = getbit(bytes, 13); // sense reversal
        bool crossing = getbit(bytes, 14); // altitude crossing
        bool positive = getbit(bytes, 15);
        // positive: (Maintain climb / descent) / (Climb / descend): requires more than 1500 fpm vertical rate
        // !positive: (Do not / reduce) (climb / descend)

        if (corr && positive) {
            if (!reversal) {
                // reversal has priority and comes later
            } else if (increase) {
                p = safe_snprintf(p, end, " Increase");
            }

            if (down)
                p = safe_snprintf(p, end, " Descend");
            else
                p = safe_snprintf(p, end, " Climb");

            if (reversal) {
                if (down)
                    p = safe_snprintf(p, end, "; Descend");
                else
                    p = safe_snprintf(p, end, "; Climb");
                p = safe_snprintf(p, end, " NOW");
            }

            if (crossing) {
                p = safe_snprintf(p, end, "; Crossing");
                if (down)
                    p = safe_snprintf(p, end, " Descend");
                else
                    p = safe_snprintf(p, end, " Climb");
            }
        }

        if (corr && !positive) {
            p = safe_snprintf(p, end, " Level Off");
        }

        if (!corr && positive) {
            p = safe_snprintf(p, end, " Maintain vertical Speed");
            if (crossing) {
                p = safe_snprintf(p, end, "; Crossing Maintain");
            }
        }

        if (!corr && !positive) {
            p = safe_snprintf(p, end, " Monitor vertical Speed");
        }

    } else if (!ara && mte) {
        p = safe_snprintf(p, end, "RA multithreat:");
        if (getbit(bytes, 10))
            p = safe_snprintf(p, end, " correct upwards;");
        if (getbit(bytes, 11))
            p = safe_snprintf(p, end, " climb required;");
        if (getbit(bytes, 12))
            p = safe_snprintf(p, end, " correct downwards;");
        if (getbit(bytes, 13))
            p = safe_snprintf(p, end, " descent required;");
        if (getbit(bytes, 14))
            p = safe_snprintf(p, end, " crossing;");
        if (getbit(bytes, 15))
            p = safe_snprintf(p, end, " increase/maintain vertical rate");
        else
            p = safe_snprintf(p, end, "      reduce/limit vertical rate");
    }

    int tti = getbits(bytes, 29, 30);
    uint32_t threatAddr = getbits(bytes, 31, 54);
    if (tti == 1)
        p = safe_snprintf(p, end, "; TIDh: %06x", threatAddr);

    if (debug) {
        p = safe_snprintf(p, end, "; TTI: ");
        for (int i = 29; i <= 30; i++) p = safe_snprintf(p, end, "%u", getbit(bytes, i));
    }

    p = safe_snprintf(p, end, ",");

    return p;
}

char *sprintAircraftObject(char *p, char *end, struct aircraft *a, uint64_t now, int printMode, struct modesMessage *mm) {

    // printMode == 0: aircraft.json / globe.json / apiBuffer
    // printMode == 1: trace.json
    // printMode == 2: jsonPositionOutput

    p = safe_snprintf(p, end, "{");
    if (printMode == 2)
        p = safe_snprintf(p, end, "\"now\" : %.1f,", now / 1000.0);
    if (printMode != 1)
        p = safe_snprintf(p, end, "\"hex\":\"%s%06x\",", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
    p = safe_snprintf(p, end, "\"type\":\"%s\"", addrtype_enum_string(a->addrtype));
    if (trackDataValid(&a->callsign_valid)) {
        char buf[128];
        p = safe_snprintf(p, end, ",\"flight\":\"%s\"", jsonEscapeString(a->callsign, buf, sizeof(buf)));
    }
    if (Modes.db) {
        if (printMode != 1) {
            if (a->registration[0])
                p = safe_snprintf(p, end, ",\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
            if (a->typeCode[0])
                p = safe_snprintf(p, end, ",\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
            if (a->dbFlags)
                p = safe_snprintf(p, end, ",\"dbFlags\":%u", a->dbFlags);

            if (Modes.jsonLongtype && a->typeLong[0])
                p = safe_snprintf(p, end, ",\"desc\":\"%.*s\"", (int) sizeof(a->typeLong), a->typeLong);
        }
    }
    if (printMode != 1) {
        if (trackDataValid(&a->airground_valid) && a->airground == AG_GROUND)
            if (printMode == 2)
                p = safe_snprintf(p, end, ",\"ground\":true");
            else
                p = safe_snprintf(p, end, ",\"alt_baro\":\"ground\"");
        else {
            if (altReliable(a))
                p = safe_snprintf(p, end, ",\"alt_baro\":%d", a->altitude_baro);
            if (printMode == 2)
                p = safe_snprintf(p, end, ",\"ground\":false");
        }
    }
    if (trackDataValid(&a->altitude_geom_valid))
        p = safe_snprintf(p, end, ",\"alt_geom\":%d", a->altitude_geom);
    if (printMode != 1 && trackDataValid(&a->gs_valid))
        p = safe_snprintf(p, end, ",\"gs\":%.1f", a->gs);
    if (trackDataValid(&a->ias_valid))
        p = safe_snprintf(p, end, ",\"ias\":%u", a->ias);
    if (trackDataValid(&a->tas_valid))
        p = safe_snprintf(p, end, ",\"tas\":%u", a->tas);
    if (trackDataValid(&a->mach_valid))
        p = safe_snprintf(p, end, ",\"mach\":%.3f", a->mach);
    if (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->altitude_baro) < 500) {
        p = safe_snprintf(p, end, ",\"wd\":%.0f", a->wind_direction);
        p = safe_snprintf(p, end, ",\"ws\":%.0f", a->wind_speed);
    }
    if (now < a->oat_updated + TRACK_EXPIRE) {
        p = safe_snprintf(p, end, ",\"oat\":%.0f", a->oat);
        p = safe_snprintf(p, end, ",\"tat\":%.0f", a->tat);
    }

    if (trackDataValid(&a->track_valid))
        p = safe_snprintf(p, end, ",\"track\":%.2f", a->track);
    else if (printMode != 1 && trackDataValid(&a->position_valid) &&
        !(trackDataValid(&a->airground_valid) && a->airground == AG_GROUND))
        p = safe_snprintf(p, end, ",\"calc_track\":%.0f", a->calc_track);

    if (trackDataValid(&a->track_rate_valid))
        p = safe_snprintf(p, end, ",\"track_rate\":%.2f", a->track_rate);
    if (trackDataValid(&a->roll_valid))
        p = safe_snprintf(p, end, ",\"roll\":%.2f", a->roll);
    if (trackDataValid(&a->mag_heading_valid))
        p = safe_snprintf(p, end, ",\"mag_heading\":%.2f", a->mag_heading);
    if (trackDataValid(&a->true_heading_valid))
        p = safe_snprintf(p, end, ",\"true_heading\":%.2f", a->true_heading);
    if (trackDataValid(&a->baro_rate_valid))
        p = safe_snprintf(p, end, ",\"baro_rate\":%d", a->baro_rate);
    if (trackDataValid(&a->geom_rate_valid))
        p = safe_snprintf(p, end, ",\"geom_rate\":%d", a->geom_rate);
    if (trackDataValid(&a->squawk_valid))
        p = safe_snprintf(p, end, ",\"squawk\":\"%04x\"", a->squawk);
    if (trackDataValid(&a->emergency_valid))
        p = safe_snprintf(p, end, ",\"emergency\":\"%s\"", emergency_enum_string(a->emergency));
    if (a->category != 0)
        p = safe_snprintf(p, end, ",\"category\":\"%02X\"", a->category);
    if (trackDataValid(&a->nav_qnh_valid))
        p = safe_snprintf(p, end, ",\"nav_qnh\":%.1f", a->nav_qnh);
    if (trackDataValid(&a->nav_altitude_mcp_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_mcp\":%d", a->nav_altitude_mcp);
    if (trackDataValid(&a->nav_altitude_fms_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_fms\":%d", a->nav_altitude_fms);
    if (trackDataValid(&a->nav_heading_valid))
        p = safe_snprintf(p, end, ",\"nav_heading\":%.2f", a->nav_heading);
    if (trackDataValid(&a->nav_modes_valid)) {
        p = safe_snprintf(p, end, ",\"nav_modes\":[");
        p = append_nav_modes(p, end, a->nav_modes, "\"", ",");
        p = safe_snprintf(p, end, "]");
    }
    if (printMode != 1) {
        if (posReliable(a)) {
            p = safe_snprintf(p, end, ",\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.1f",
                    a->lat, a->lon, a->pos_nic, a->pos_rc,
                    (now < a->position_valid.updated) ? 0 : ((now - a->position_valid.updated) / 1000.0));
#if defined(TRACKS_UUID)
            char uuid[32]; // needs 18 chars and null byte
            sprint_uuid1(a->lastPosReceiverId, uuid);
            p = safe_snprintf(p, end, ",\"rId\":\"%s\"", uuid);
#endif
        } else {
            if (now < a->rr_seen + 2 * MINUTES) {
                p = safe_snprintf(p, end, ",\"rr_lat\":%.1f,\"rr_lon\":%.1f", a->rr_lat, a->rr_lon);
            }
            if (now < a->seenPosReliable + 14 * 24 * HOURS) {
                p = safe_snprintf(p, end, ",\"lastPosition\":{\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.1f}",
                        a->latReliable, a->lonReliable, a->pos_nic_reliable, a->pos_rc_reliable,
                        (now < a->seenPosReliable) ? 0 : ((now - a->seenPosReliable) / 1000.0));
            }
        }
        if (a->nogpsCounter >= NOGPS_SHOW && now < a->seenAdsbReliable + NOGPS_DWELL && now > a->seenAdsbReliable + 15 * SECONDS) {
            p = safe_snprintf(p, end, ",\"gpsOkBefore\":%.1f", a->seenAdsbReliable / 1000.0);
        }
    }

    if (printMode == 1 && trackDataValid(&a->position_valid)) {
        p = safe_snprintf(p, end, ",\"nic\":%u,\"rc\":%u",
                a->pos_nic, a->pos_rc);
    }
    if (a->adsb_version >= 0)
        p = safe_snprintf(p, end, ",\"version\":%d", a->adsb_version);
    if (trackDataValid(&a->nic_baro_valid))
        p = safe_snprintf(p, end, ",\"nic_baro\":%u", a->nic_baro);
    if (trackDataValid(&a->nac_p_valid))
        p = safe_snprintf(p, end, ",\"nac_p\":%u", a->nac_p);
    if (trackDataValid(&a->nac_v_valid))
        p = safe_snprintf(p, end, ",\"nac_v\":%u", a->nac_v);
    if (trackDataValid(&a->sil_valid))
        p = safe_snprintf(p, end, ",\"sil\":%u", a->sil);
    if (a->sil_type != SIL_INVALID)
        p = safe_snprintf(p, end, ",\"sil_type\":\"%s\"", sil_type_enum_string(a->sil_type));
    if (trackDataValid(&a->gva_valid))
        p = safe_snprintf(p, end, ",\"gva\":%u", a->gva);
    if (trackDataValid(&a->sda_valid))
        p = safe_snprintf(p, end, ",\"sda\":%u", a->sda);
    if (trackDataValid(&a->alert_valid))
        p = safe_snprintf(p, end, ",\"alert\":%u", a->alert);
    if (trackDataValid(&a->spi_valid))
        p = safe_snprintf(p, end, ",\"spi\":%u", a->spi);

    /*
    if (a->position_valid.source == SOURCE_JAERO)
        p = safe_snprintf(p, end, ",\"jaero\": true");
    if (a->position_valid.source == SOURCE_SBS)
        p = safe_snprintf(p, end, ",\"sbs_other\": true");
    */
    if (Modes.netReceiverIdPrint) {
        char uuid[32]; // needs 18 chars and null byte
        sprint_uuid1(a->lastPosReceiverId, uuid);
        p = safe_snprintf(p, end, ",\"rId\":%s", uuid);
    }

    if (printMode != 1) {
        p = safe_snprintf(p, end, ",\"mlat\":");
        p = append_flags(p, end, a, SOURCE_MLAT);
        p = safe_snprintf(p, end, ",\"tisb\":");
        p = append_flags(p, end, a, SOURCE_TISB);

        p = safe_snprintf(p, end, ",\"messages\":%u,\"seen\":%.1f,\"rssi\":%.1f",
                a->messages, (now < a->seen) ? 0 : ((now - a->seen) / 1000.0),
                getSignal(a));
    }

    if (trackDataAge(now, &a->acas_ra_valid) < 15 * SECONDS || (mm && mm->acas_ra_valid)) {
        p = safe_snprintf(p, end, ",\"acas_ra\":");
        p = sprintACASJson(p, end, a->acas_ra,
                (mm && mm->acas_ra_valid) ? mm : NULL,
                (mm && mm->acas_ra_valid) ? now : a->acas_ra_valid.updated);
    }

    p = safe_snprintf(p, end, "}");

    return p;
}

char *sprintAircraftRecent(char *p, char *end, struct aircraft *a, uint64_t now, int printMode, struct modesMessage *mm, uint64_t recent) {
    if (printMode == 1) {
    }
    char *start = p;

    p = safe_snprintf(p, end, "{");
    //p = safe_snprintf(p, end, "\"now\" : %.0f,", now / 1000.0);
    p = safe_snprintf(p, end, "\"hex\":\"%s%06x\",", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
    p = safe_snprintf(p, end, "\"type\":\"%s\"", addrtype_enum_string(a->addrtype));

    char *startRecent = p;

    if (recent > trackDataAge(now, &a->callsign_valid)) {
        char buf[128];
        p = safe_snprintf(p, end, ",\"flight\":\"%s\"", jsonEscapeString(a->callsign, buf, sizeof(buf)));
    }
    if (recent > trackDataAge(now, &a->airground_valid)) {
        if (a->airground == AG_GROUND) {
            p = safe_snprintf(p, end, ",\"ground\":true");
        } else if (a->airground == AG_AIRBORNE ) {
            p = safe_snprintf(p, end, ",\"ground\":false");
        }
    }
    if (recent > trackDataAge(now, &a->altitude_baro_valid))
        p = safe_snprintf(p, end, ",\"alt_baro\":%d", a->altitude_baro);
    if (recent > trackDataAge(now, &a->altitude_geom_valid))
        p = safe_snprintf(p, end, ",\"alt_geom\":%d", a->altitude_geom);
    if (recent > trackDataAge(now, &a->gs_valid))
        p = safe_snprintf(p, end, ",\"gs\":%.1f", a->gs);
    if (recent > trackDataAge(now, &a->ias_valid))
        p = safe_snprintf(p, end, ",\"ias\":%u", a->ias);
    if (recent > trackDataAge(now, &a->tas_valid))
        p = safe_snprintf(p, end, ",\"tas\":%u", a->tas);
    if (recent > trackDataAge(now, &a->mach_valid))
        p = safe_snprintf(p, end, ",\"mach\":%.3f", a->mach);
    if (now < a->wind_updated + recent && abs(a->wind_altitude - a->altitude_baro) < 500) {
        p = safe_snprintf(p, end, ",\"wd\":%.0f", a->wind_direction);
        p = safe_snprintf(p, end, ",\"ws\":%.0f", a->wind_speed);
    }
    if (now < a->oat_updated + recent) {
        p = safe_snprintf(p, end, ",\"oat\":%.0f", a->oat);
        p = safe_snprintf(p, end, ",\"tat\":%.0f", a->tat);
    }

    if (recent > trackDataAge(now, &a->track_valid))
        p = safe_snprintf(p, end, ",\"track\":%.2f", a->track);
    if (recent > trackDataAge(now, &a->track_rate_valid))
        p = safe_snprintf(p, end, ",\"track_rate\":%.2f", a->track_rate);
    if (recent > trackDataAge(now, &a->roll_valid))
        p = safe_snprintf(p, end, ",\"roll\":%.2f", a->roll);
    if (recent > trackDataAge(now, &a->mag_heading_valid))
        p = safe_snprintf(p, end, ",\"mag_heading\":%.2f", a->mag_heading);
    if (recent > trackDataAge(now, &a->true_heading_valid))
        p = safe_snprintf(p, end, ",\"true_heading\":%.2f", a->true_heading);
    if (recent > trackDataAge(now, &a->baro_rate_valid))
        p = safe_snprintf(p, end, ",\"baro_rate\":%d", a->baro_rate);
    if (recent > trackDataAge(now, &a->geom_rate_valid))
        p = safe_snprintf(p, end, ",\"geom_rate\":%d", a->geom_rate);
    if (recent > trackDataAge(now, &a->squawk_valid))
        p = safe_snprintf(p, end, ",\"squawk\":\"%04x\"", a->squawk);
    if (recent > trackDataAge(now, &a->emergency_valid))
        p = safe_snprintf(p, end, ",\"emergency\":\"%s\"", emergency_enum_string(a->emergency));
    if (recent > trackDataAge(now, &a->nav_qnh_valid))
        p = safe_snprintf(p, end, ",\"nav_qnh\":%.1f", a->nav_qnh);
    if (recent > trackDataAge(now, &a->nav_altitude_mcp_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_mcp\":%d", a->nav_altitude_mcp);
    if (recent > trackDataAge(now, &a->nav_altitude_fms_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_fms\":%d", a->nav_altitude_fms);
    if (recent > trackDataAge(now, &a->nav_heading_valid))
        p = safe_snprintf(p, end, ",\"nav_heading\":%.2f", a->nav_heading);
    if (recent > trackDataAge(now, &a->nav_modes_valid)) {
        p = safe_snprintf(p, end, ",\"nav_modes\":[");
        p = append_nav_modes(p, end, a->nav_modes, "\"", ",");
        p = safe_snprintf(p, end, "]");
    }
    if (recent > trackDataAge(now, &a->position_valid)) {
        p = safe_snprintf(p, end, ",\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.1f",
                a->lat, a->lon, a->pos_nic, a->pos_rc,
                (now < a->position_valid.updated) ? 0 : ((now - a->position_valid.updated) / 1000.0));
        if (a->adsb_version >= 0)
            p = safe_snprintf(p, end, ",\"version\":%d", a->adsb_version);
        if (a->category != 0)
            p = safe_snprintf(p, end, ",\"category\":\"%02X\"", a->category);
        if (Modes.netReceiverIdPrint) {
            char uuid[32]; // needs 18 chars and null byte
            sprint_uuid1(a->lastPosReceiverId, uuid);
            p = safe_snprintf(p, end, ",\"rId\":%s", uuid);
        }
    }

    if (recent > trackDataAge(now, &a->nic_baro_valid))
        p = safe_snprintf(p, end, ",\"nic_baro\":%u", a->nic_baro);
    if (recent > trackDataAge(now, &a->nac_p_valid))
        p = safe_snprintf(p, end, ",\"nac_p\":%u", a->nac_p);
    if (recent > trackDataAge(now, &a->nac_v_valid))
        p = safe_snprintf(p, end, ",\"nac_v\":%u", a->nac_v);
    if (recent > trackDataAge(now, &a->sil_valid)) {
        p = safe_snprintf(p, end, ",\"sil\":%u", a->sil);
        if (a->sil_type != SIL_INVALID)
            p = safe_snprintf(p, end, ",\"sil_type\":\"%s\"", sil_type_enum_string(a->sil_type));
    }
    if (recent > trackDataAge(now, &a->gva_valid))
        p = safe_snprintf(p, end, ",\"gva\":%u", a->gva);
    if (recent > trackDataAge(now, &a->sda_valid))
        p = safe_snprintf(p, end, ",\"sda\":%u", a->sda);
    if (recent > trackDataAge(now, &a->alert_valid))
        p = safe_snprintf(p, end, ",\"alert\":%u", a->alert);
    if (recent > trackDataAge(now, &a->spi_valid))
        p = safe_snprintf(p, end, ",\"spi\":%u", a->spi);

    // nothing recent, print nothing
    if (startRecent == p) {
        return start;
    }

    /*
    p = safe_snprintf(p, end, ",\"mlat\":");
    p = append_flags(p, end, a, SOURCE_MLAT);
    p = safe_snprintf(p, end, ",\"tisb\":");
    p = append_flags(p, end, a, SOURCE_TISB);

    p = safe_snprintf(p, end, ",\"messages\":%u,\"seen\":%.1f,\"rssi\":%.1f",
            a->messages, (now < a->seen) ? 0 : ((now - a->seen) / 1000.0),
            10 * log10((a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
                    a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7]) / 8 + 1.125e-5));
    */

    if (trackDataAge(now, &a->acas_ra_valid) < recent) {
        p = safe_snprintf(p, end, ",\"acas_ra_timestamp\":%.2f", now / 1000.0);
        if (mm && mm->acas_ra_valid)
            p = safe_snprintf(p, end, ",\"acas_ra_df_type\":%d", mm->msgtype);
        p = safe_snprintf(p, end, ",\"acas_ra_mv_mb_bytes_hex\":\"");
        for (int i = 0; i < 7; ++i) {
            p = safe_snprintf(p, end, "%02X", (unsigned) a->acas_ra[i]);
        }
        p = safe_snprintf(p, end, "\"");
        p = safe_snprintf(p, end, ",\"acas_ra_csvline\":\"");
        p = sprintACASInfoShort(p, end, a->addr, a->acas_ra, a, (mm && mm->acas_ra_valid) ? mm : NULL, a->acas_ra_valid.updated);
        p = safe_snprintf(p, end, "\"");
    }

    p = safe_snprintf(p, end, "}");

    return p;
}

/*
static void check_state_all(struct aircraft *test, uint64_t now) {
    size_t buflen = 4096;
    char buffer1[buflen];
    char buffer2[buflen];
    char *buf, *p, *end;

    struct aircraft abuf = *test;
    struct aircraft *a = &abuf;

    buf = buffer1;
    p = buf;
    end = buf + buflen;
    p = sprintAircraftObject(p, end, a, now, 1, NULL);

    buf = buffer2;
    p = buf;
    end = buf + buflen;


    struct state_all state_buf;
    memset(&state_buf, 0, sizeof(struct state_all));
    struct state_all *new_all = &state_buf;
    to_state_all(a, new_all, now);

    struct aircraft bbuf;
    memset(&bbuf, 0, sizeof(struct aircraft));
    struct aircraft *b = &bbuf;

    from_state_all(new_all, b, now);

    p = sprintAircraftObject(p, end, b, now, 1, NULL);

    if (strncmp(buffer1, buffer2, buflen)) {
        fprintf(stderr, "%s\n%s\n", buffer1, buffer2);
    }
}
*/

static inline __attribute__((always_inline)) int includeGlobeJson(uint64_t now, struct aircraft *a) {
    if (a == NULL)
        return 0;
    if (a->messages < 2)
        return 0;

    if (a->nogpsCounter >= NOGPS_SHOW && now < a->seenAdsbReliable + NOGPS_DWELL && now > a->seenAdsbReliable + 15 * SECONDS)
        return 1;
    // check aircraft without position:
    if (a->position_valid.source == SOURCE_INVALID) {
        // don't include stale aircraft
        if (now > a->seen + TRACK_EXPIRE / 2 && now > a->seenPosReliable + TRACK_EXPIRE) {
            return 0;
        }
        // don't include aircraft with very outdated positions in globe files
        if (now > a->seenPosReliable + 30 * MINUTES) {
            return 0;
        }
    }

    return 1;
}

static inline __attribute__((always_inline)) int includeAircraftJson(uint64_t now, struct aircraft *a) {
    if (a == NULL)
        return 0;
    if (a->messages < 2)
        return 0;

    if (a->nogpsCounter >= NOGPS_SHOW && now < a->seenAdsbReliable + NOGPS_DWELL && now > a->seenAdsbReliable + 15 * SECONDS)
        return 1;
    // check aircraft without position:
    if (a->position_valid.source == SOURCE_INVALID) {
        // don't include stale aircraft
        if (now > a->seen + TRACK_EXPIRE / 2 && now > a->seenPosReliable + TRACK_EXPIRE) {
            return 0;
        }
    }

    return 1;
}

struct char_buffer generateAircraftBin() {
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;

    struct craftArray *ca = &Modes.aircraftActive;
    size_t alloc = 4096 + ca->len * sizeof(struct binCraft); // The initial buffer is resized as needed

    char *buf = aligned_malloc(alloc);
    char *p = buf;
    char *end = buf + alloc;

    uint32_t elementSize = sizeof(struct binCraft);
    memset(p, 0, elementSize);

#define memWrite(p, var) do { memcpy(p, &var, sizeof(var)); p += sizeof(var); } while(0)

    memWrite(p, now);

    memWrite(p, elementSize);

    uint32_t ac_count_pos = Modes.globalStatsCount.readsb_aircraft_with_position;
    memWrite(p, ac_count_pos);

    uint32_t index = 314159; // unnecessary
    memWrite(p, index);

    int16_t south = -90;
    int16_t west = -180;
    int16_t north = 90;
    int16_t east = 180;

    memWrite(p, south);
    memWrite(p, west);
    memWrite(p, north);
    memWrite(p, east);

    uint32_t messageCount = Modes.stats_current.messages_total + Modes.stats_alltime.messages_total;
    memWrite(p, messageCount);

    if (p - buf > (int) elementSize)
        fprintf(stderr, "buffer overrun aircrafBin\n");

    p = buf + elementSize;

    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];
        if (a == NULL) continue;

        if (!includeAircraftJson(now, a)) {
            continue;
        }
        // check if we have enough space
        if ((p + 2 * sizeof(struct binCraft)) >= end) {
            int used = p - buf;
            alloc *= 2;
            buf = (char *) realloc(buf, alloc);
            p = buf + used;
            end = buf + alloc;
        }

        struct binCraft bin;
        toBinCraft(a, &bin, now);

        memWrite(p, bin);

        if (p >= end)
            fprintf(stderr, "buffer overrun aircraftBin\n");
    }

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;

#undef memWrite
}

struct char_buffer generateGlobeBin(int globe_index, int mil) {
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t alloc = 4096; // The initial buffer is resized as needed

    struct craftArray *ca = NULL;
    int good;
    if (globe_index == -1) {
        ca = &Modes.aircraftActive;
        good = 1;
    } else if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
        good = 1;
    } else {
        fprintf(stderr, "generateGlobeBin: bad globe_index: %d\n", globe_index);
        good = 0;
    }
    if (good && ca)
        alloc += ca->len * sizeof(struct binCraft);

    char *buf = aligned_malloc(alloc);
    char *p = buf;
    char *end = buf + alloc;

    uint32_t elementSize = sizeof(struct binCraft);
    memset(p, 0, elementSize);

#define memWrite(p, var) do { memcpy(p, &var, sizeof(var)); p += sizeof(var); } while(0)

    memWrite(p, now);

    memWrite(p, elementSize);

    uint32_t ac_count_pos = Modes.globalStatsCount.readsb_aircraft_with_position;
    memWrite(p, ac_count_pos);

    uint32_t index = globe_index < 0 ? 42777 : globe_index;
    memWrite(p, index);

    int16_t south = -90;
    int16_t west = -180;
    int16_t north = 90;
    int16_t east = 180;

    if (globe_index >= GLOBE_MIN_INDEX) {
        int grid = GLOBE_INDEX_GRID;
        south = ((globe_index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * grid - 90;
        west = ((globe_index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * grid - 180;
        north = south + grid;
        east = west + grid;
    } else if (globe_index >= 0) {
        struct tile *tiles = Modes.json_globe_special_tiles;
        struct tile tile = tiles[globe_index];
        south = tile.south;
        west = tile.west;
        north = tile.north;
        east = tile.east;
    }

    memWrite(p, south);
    memWrite(p, west);
    memWrite(p, north);
    memWrite(p, east);

    uint32_t messageCount = Modes.stats_current.messages_total + Modes.stats_alltime.messages_total;
    memWrite(p, messageCount);

    if (p - buf > (int) elementSize)
        fprintf(stderr, "buffer overrun globeBin\n");

    p = buf + elementSize;

    if (good && ca->list) {
        for (int i = 0; i < ca->len; i++) {
            a = ca->list[i];
            if (a == NULL) continue;

            if (!includeGlobeJson(now, a))
                continue;

            if (mil && !(a->dbFlags & 1))
                continue;
                        // check if we have enough space
            if ((p + 2 * sizeof(struct binCraft)) >= end) {
                int used = p - buf;
                alloc *= 2;
                buf = (char *) realloc(buf, alloc);
                p = buf + used;
                end = buf + alloc;
            }

            struct binCraft bin;
            toBinCraft(a, &bin, now);

            memWrite(p, bin);

            if (p >= end)
                fprintf(stderr, "buffer overrun globeBin\n");
        }
    }

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;

#undef memWrite
}

struct char_buffer generateGlobeJson(int globe_index){
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t alloc = 4096; // The initial buffer is resized as needed

    struct craftArray *ca = NULL;
    int good;
    if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
        good = 1;
        alloc += ca->len * 1024; // 1024 bytes per potential aircraft object
    } else {
        fprintf(stderr, "generateAircraftJson: bad globe_index: %d\n", globe_index);
        good = 0;
    }

    char *buf = aligned_malloc(alloc);
    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
            "  \"messages\" : %u,\n",
            now / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    p = safe_snprintf(p, end,
            "  \"global_ac_count_withpos\" : %d,\n",
            Modes.globalStatsCount.readsb_aircraft_with_position
            );

    p = safe_snprintf(p, end, "  \"globeIndex\" : %d, ", globe_index);
    if (globe_index >= GLOBE_MIN_INDEX) {
        int grid = GLOBE_INDEX_GRID;
        int lat = ((globe_index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * grid - 90;
        int lon = ((globe_index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * grid - 180;
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                lat,
                lon,
                lat + grid,
                lon + grid);
    } else {
        struct tile *tiles = Modes.json_globe_special_tiles;
        struct tile tile = tiles[globe_index];
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                tile.south,
                tile.west,
                tile.north,
                tile.east);
    }

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    if (good && ca->list) {
        for (int i = 0; i < ca->len; i++) {
            a = ca->list[i];
            if (a == NULL) continue;

            if (!includeGlobeJson(now, a))
                continue;

            // don't include aircraft with very outdated positions in globe files
            if (a->position_valid.source == SOURCE_INVALID && now > a->seenPosReliable + 5 * MINUTES) {
                continue;
            }

            // check if we have enough space
            if ((p + 2000) >= end) {
                int used = p - buf;
                alloc *= 2;
                buf = (char *) realloc(buf, alloc);
                p = buf + used;
                end = buf + alloc;
            }

            p = safe_snprintf(p, end, "\n");
            p = sprintAircraftObject(p, end, a, now, 0, NULL);
            p = safe_snprintf(p, end, ",");

            if (p >= end)
                fprintf(stderr, "buffer overrun aircraft json\n");
        }
    }
    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateAircraftJson(uint64_t onlyRecent){
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;

    struct craftArray *ca = &Modes.aircraftActive;
    size_t alloc = 4096 + ca->len * sizeof(struct binCraft); // The initial buffer is resized as needed

    char *buf = aligned_malloc(alloc);
    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
            "  \"messages\" : %u,\n",
            now / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];
        if (a == NULL) continue;

        //fprintf(stderr, "a: %05x\n", a->addr);
        if (!includeAircraftJson(now, a))
            continue;

        // check if we have enough space
        if ((p + 2000) >= end) {
            int used = p - buf;
            alloc *= 2;
            buf = (char *) realloc(buf, alloc);
            p = buf + used;
            end = buf + alloc;
        }

        char *beforeSprint = p;

        p = safe_snprintf(p, end, "\n");
        if (onlyRecent) {
            p = sprintAircraftRecent(p, end, a, now, 0, NULL, onlyRecent);
        } else {
            p = sprintAircraftObject(p, end, a, now, 0, NULL);
        }

        if (p - beforeSprint < 5) {
            p = beforeSprint;
        } else {
            p = safe_snprintf(p, end, ",");
        }


        if (p >= end)
            fprintf(stderr, "buffer overrun aircraft json\n");
    }

    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    //    fprintf(stderr, "%u\n", ac_counter);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

static char *sprintTracePoint(char *p, char *end, struct aircraft *a, int i, uint64_t startStamp) {
    struct state *trace = &a->trace[i];

    int32_t altitude = trace->altitude * 25;
    int32_t rate = trace->rate * 32;
    int rate_valid = trace->flags.rate_valid;
    int rate_geom = trace->flags.rate_geom;
    int stale = trace->flags.stale;
    int on_ground = trace->flags.on_ground;
    int altitude_valid = trace->flags.altitude_valid;
    int gs_valid = trace->flags.gs_valid;
    int track_valid = trace->flags.track_valid;
    int leg_marker = trace->flags.leg_marker;
    int altitude_geom = trace->flags.altitude_geom;

    // in the air
    p = safe_snprintf(p, end, "\n[%.1f,%f,%f",
            (trace->timestamp - startStamp) / 1000.0, trace->lat / 1E6, trace->lon / 1E6);

    if (on_ground)
        p = safe_snprintf(p, end, ",\"ground\"");
    else if (altitude_valid)
        p = safe_snprintf(p, end, ",%d", altitude);
    else
        p = safe_snprintf(p, end, ",null");

    if (gs_valid)
        p = safe_snprintf(p, end, ",%.1f", trace->gs / 10.0);
    else
        p = safe_snprintf(p, end, ",null");

    if (track_valid)
        p = safe_snprintf(p, end, ",%.1f", trace->track / 10.0);
    else
        p = safe_snprintf(p, end, ",null");

    int bitfield = (altitude_geom << 3) | (rate_geom << 2) | (leg_marker << 1) | (stale << 0);
    p = safe_snprintf(p, end, ",%d", bitfield);

    if (rate_valid)
        p = safe_snprintf(p, end, ",%d", rate);
    else
        p = safe_snprintf(p, end, ",null");

    if (i % 4 == 0) {
        uint64_t now = trace->timestamp;
        struct state_all *all = &(a->trace_all[i/4]);
        struct aircraft b;
        memset(&b, 0, sizeof(struct aircraft));
        struct aircraft *ac = &b;
        from_state_all(all, ac, now);

        p = safe_snprintf(p, end, ",");
        p = sprintAircraftObject(p, end, ac, now, 1, NULL);
    } else {
        p = safe_snprintf(p, end, ",null");
    }
#if defined(TRACKS_UUID)
    char uuid[32]; // needs 18 chars and null byte
    sprint_uuid1(trace->receiverId, uuid);
    p = safe_snprintf(p, end, ",\"%s\"", uuid);
#endif
    p = safe_snprintf(p, end, "]");

    return p;
}
static void checkTraceCache(struct aircraft *a, uint64_t now) {
    if (!a->traceCache) {
        if (now > a->seen_pos + TRACE_CACHE_LIFETIME / 2 || !a->trace) {
            return;
        }
        a->traceCache = aligned_malloc(sizeof(struct traceCache));
        if (!a->traceCache) {
            fprintf(stderr, "malloc error code point ohB6yeeg\n");
            return;
        }
        memset(a->traceCache, 0x0, sizeof(struct traceCache));
    }
    struct traceCache *c = a->traceCache;
    char *p;
    char *end = c->json + sizeof(c->json);
    int firstRecent = max(0, a->trace_len - TRACE_RECENT_POINTS);

    struct traceCacheEntry *e = c->entries;
    int k = 0;
    int stateIndex = firstRecent;
    int found = 0;
    while (k < c->entriesLen) {
        if (e[k].stateIndex == firstRecent) {
            found = 1;
            break;
        }
        k++;
    }
    int updateCache = 0; // by default rebuild the cache instead of updating it


    if (!found && a->addr == TRACE_FOCUS)
        fprintf(stderr, "%06x firstRecent not found, entriesLen: %d\n", a->addr, c->entriesLen);

    if (found) {
        int newEntryCount = a->trace_len - 1 - e[c->entriesLen - 1].stateIndex;
        if (newEntryCount > TRACE_CACHE_EXTRA) {
            // make it a bit simpler, just rebuild the cache in this case
            updateCache = 0;
            if (a->addr == TRACE_FOCUS)
                fprintf(stderr, "%06x newEntryCount: %d\n", a->addr, newEntryCount);
            if (Modes.debug_traceCount)
                fprintf(stderr, "%06x ", a->addr);
        } else if (newEntryCount + c->entriesLen > TRACE_CACHE_POINTS) {
            // if the cache would get full, do memmove fun!
            int moveIndexes = min(k, TRACE_CACHE_EXTRA);
            c->entriesLen -= moveIndexes;
            k -= moveIndexes;
            memmove(e, e + TRACE_CACHE_EXTRA, c->entriesLen * sizeof(struct traceCacheEntry));

            int moveDist = e[0].offset;
            struct traceCacheEntry *last = &e[c->entriesLen - 1];
            int jsonLen = last->offset + last->len;

            memmove(c->json, c->json + moveDist, jsonLen);
            for (int x = 0; x < c->entriesLen; x++) {
                e[x].offset -= moveDist;
            }
            updateCache = 1;
            //if (a->addr == TRACE_FOCUS)
            //    fprintf(stderr, "%06x k: %d moveIndexes: %d newEntryCount: %d\n", a->addr, k, moveIndexes, newEntryCount);
        } else {
            updateCache = 1;
        }
    }

    if (c->startStamp && a->trace[firstRecent].timestamp > c->startStamp + 8 * HOURS) {
        if (a->addr == TRACE_FOCUS)
            fprintf(stderr, "%06x startStamp diff: %.1f h\n", a->addr, (a->trace[firstRecent].timestamp - c->startStamp) / (double) (1 * HOURS));
        // rebuild cache if startStamp is too old to avoid very large numbers for the relative time
        updateCache = 0;
    }

    if (updateCache) {
        // step to last cached entry
        k = c->entriesLen - 1;
        // set p to write after json corresponding to last entry
        p = c->json + e[k].offset + e[k].len;
        // set stateIndex to the correct trace index to get data for the next cache entry
        stateIndex = e[k].stateIndex + 1;
        // set k to the index we will write to in the cache
        k++;
    } else {
        // reset / initialize stuff / rebuild cache
        c->startStamp = a->trace[firstRecent].timestamp;
        k = 0;
        c->entriesLen = 0;
        p = c->json;
        if (a->addr == TRACE_FOCUS)
            fprintf(stderr, "%06x resetting traceCache\n", a->addr);
    }

    int sprintCount = 0;
    for (int i = stateIndex; i < a->trace_len && k < TRACE_CACHE_POINTS; i++) {
        e[k].stateIndex = i;
        e[k].offset = p - c->json;

        p = sprintTracePoint(p, end, a, i, c->startStamp);
        if (p >= end) {
            fprintf(stderr, "traceCache full, not an issue but fix it!\n");
            // not enough space to safely write another cache
            break;
        }
        sprintCount++;

        e[k].len = p - c->json - e[k].offset;
        e[k].leg_marker = a->trace[i].flags.leg_marker;

        k++;
        c->entriesLen++;
    }
    if (a->addr == TRACE_FOCUS && sprintCount > 3) {
        fprintf(stderr, "%06x sprintCount: %d\n", a->addr, sprintCount);
    }
}

struct char_buffer generateTraceJson(struct aircraft *a, int start, int last) {
    struct char_buffer cb = { 0 };
    if (!Modes.json_globe_index) {
        return cb;
    }
    uint64_t now = mstime();

    int limit = a->trace_len + (a->tracePosBuffered ? 1 : 0);
    int recent = (last == -2) ? 1 : 0;
    if (last < 0) {
        last = limit - 1;
    }

    int traceCount = max(last - start + 1, 0);
    size_t alloc = traceCount * 300 + 1024;

    char *buf = (char *) aligned_malloc(alloc), *p = buf, *end = buf + alloc;

    if (!buf) {
        fprintf(stderr, "malloc error code point Loi1ahwe\n");
        return cb;
    }

    p = safe_snprintf(p, end, "{\"icao\":\"%s%06x\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

    if (Modes.db) {
        char *regInfo = p;
        if (a->registration[0])
            p = safe_snprintf(p, end, ",\n\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
        if (a->typeCode[0])
            p = safe_snprintf(p, end, ",\n\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
        if (a->typeLong[0])
            p = safe_snprintf(p, end, ",\n\"desc\":\"%.*s\"", (int) sizeof(a->typeLong), a->typeLong);
        if (a->dbFlags)
            p = safe_snprintf(p, end, ",\n\"dbFlags\":%u", a->dbFlags);
        dbEntry *e = dbGet(a->addr, Modes.dbIndex);
        if (e) {
            if (e->ownOp[0])
                p = safe_snprintf(p, end, ",\n\"ownOp\":\"%.*s\"", (int) sizeof(e->ownOp), e->ownOp);
            if (e->year[0])
                p = safe_snprintf(p, end, ",\n\"year\":\"%.*s\"", (int) sizeof(e->year), e->year);
        }
        if (p == regInfo)
            p = safe_snprintf(p, end, ",\n\"noRegData\":true");
    }

    uint64_t startStamp = a->trace[start].timestamp;

    if (recent) {
        checkTraceCache(a, now);
    }
    struct traceCache *tCache = NULL;
    struct traceCacheEntry *entries = NULL;
    int k = 0;
    if (a->traceCache && recent) {
        tCache = a->traceCache;
        startStamp = tCache->startStamp;
        entries = tCache->entries;
        int found = 0;
        while (k < tCache->entriesLen) {
            if (entries[k].stateIndex == start) {
                found = 1;
                break;
            }
            k++;
        }
        if (!found)
            tCache = NULL;
    }

    p = safe_snprintf(p, end, ",\n\"timestamp\": %.3f", startStamp / 1000.0);

    p = safe_snprintf(p, end, ",\n\"trace\":[ ");

    if (tCache) {
        int sprintCount = 0;
        for (int i = start; i <= last && i < limit; i++) {
            if (k < tCache->entriesLen && entries[k].stateIndex == i
                    && a->trace[i].flags.leg_marker == entries[k].leg_marker) {
                memcpy(p, tCache->json + entries[k].offset, entries[k].len);
                p += entries[k].len;
            } else {
                p = sprintTracePoint(p, end, a, i, startStamp);
                sprintCount++;
            }
            if (p < end)
                *p++ = ',';
            k++;
        }
        if (a->addr == TRACE_FOCUS && sprintCount > 1) {
            fprintf(stderr, "%06x sprintCount2: %d\n", a->addr, sprintCount);
        }
    } else {
        for (int i = start; i <= last && i < limit; i++) {
            p = sprintTracePoint(p, end, a, i, startStamp);
            if (p < end)
                *p++ = ',';
        }
    }

    if (*(p-1) == ',')
        p--; // remove last comma

    p = safe_snprintf(p, end, " ]\n");

    p = safe_snprintf(p, end, " }\n");

    cb.len = p - buf;
    cb.buffer = buf;

    if (p >= end) {
        fprintf(stderr, "buffer overrun trace json %zu %zu\n", cb.len, alloc);
    }

    return cb;
}

//
// Return a description of the receiver in json.
//
struct char_buffer generateReceiverJson() {
    struct char_buffer cb;
    size_t buflen = 8192;
    char *buf = (char *) aligned_malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{ "
            "\"refresh\": %.0f, "
            "\"history\": %d",
            1.0 * Modes.json_interval, Modes.json_aircraft_history_next + 1);


    if (Modes.json_location_accuracy && Modes.userLocationValid) {
        if (Modes.json_location_accuracy == 1) {
            p = safe_snprintf(p, end, ", "
                    "\"lat\": %.2f, "
                    "\"lon\": %.2f",
                    Modes.fUserLat, Modes.fUserLon); // round to 2dp - about 0.5-1km accuracy - for privacy reasons
        } else {
            p = safe_snprintf(p, end, ", "
                    "\"lat\": %.6f, "
                    "\"lon\": %.6f",
                    Modes.fUserLat, Modes.fUserLon); // exact location
        }
    }

    p = safe_snprintf(p, end, ", \"jaeroTimeout\": %.1f", ((double) Modes.trackExpireJaero) / (60 * SECONDS));

    if (Modes.json_globe_index) {
        if (Modes.db || Modes.db2)
            p = safe_snprintf(p, end, ", \"dbServer\": true");

        p = safe_snprintf(p, end, ", \"binCraft\": true");
        p = safe_snprintf(p, end, ", \"globeIndexGrid\": %d", GLOBE_INDEX_GRID);

        p = safe_snprintf(p, end, ", \"globeIndexSpecialTiles\": [ ");
        struct tile *tiles = Modes.json_globe_special_tiles;

        for (int i = 0; tiles[i].south != 0 || tiles[i].north != 0; i++) {
            struct tile tile = tiles[i];
            p = safe_snprintf(p, end, "{\"south\":%d,", tile.south);
            p = safe_snprintf(p, end, "\"east\":%d,", tile.east);
            p = safe_snprintf(p, end, "\"north\":%d,", tile.north);
            p = safe_snprintf(p, end, "\"west\":%d},", tile.west);
        }
        p -= 1; // get rid of comma at the end
        p = safe_snprintf(p, end, " ]");
    }


    p = safe_snprintf(p, end, ", \"aircraft_binCraft\": true");
    if (Modes.outline_json) {
        p = safe_snprintf(p, end, ", \"outlineJson\": true");
    }
    if (Modes.trace_hist_only) {
        p = safe_snprintf(p, end, ", \"trace_hist_only\": true");
    }
    p = safe_snprintf(p, end, ", \"version\": \"%s\" }\n", MODES_READSB_VERSION);

    if (p >= end)
        fprintf(stderr, "buffer overrun receiver json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
struct char_buffer generateOutlineJson() {
    struct char_buffer cb;
    size_t buflen = 1024 + RANGEDIRS_BUCKETS * 64;
    char *buf = (char *) aligned_malloc(buflen), *p = buf, *end = buf + buflen;

    // check for maximum over last 24 full and current hour
    struct distCoords record[RANGEDIRS_BUCKETS];
    memset(record, 0, sizeof(record));
    for (int hour = 0; hour < RANGEDIRS_HOURS; hour++) {
        for (int i = 0; i < RANGEDIRS_BUCKETS; i++) {
            struct distCoords curr = Modes.rangeDirs[hour][i];
            if (curr.distance > record[i].distance) {
                record[i] = curr;
            }
        }
    }

    // print the records in each direction
    p = safe_snprintf(p, end, "{ \"actualRange\": { \"last24h\": { \"points\": [");
    for (int i = 0; i < RANGEDIRS_BUCKETS; i++) {
        if (record[i].lat || record[i].lon) {
            p = safe_snprintf(p, end, "\n[%.4f,%.4f,%d],",
                    record[i].lat,
                    record[i].lon,
                    record[i].alt);
        }
    }
    if (*(p-1) == ',')
        p--; // remove last comma if it exists
    p = safe_snprintf(p, end, "\n]}}}\n");

    if (p >= end)
        fprintf(stderr, "buffer overrun outline json\n");
    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

// Write JSON to file
static inline __attribute__((always_inline)) int writeJsonTo (const char* dir, const char *file, struct char_buffer cb, int gzip, int gzip_level) {

    char pathbuf[PATH_MAX];
    char tmppath[PATH_MAX];
    int fd;
    int len = cb.len;
    char *content = cb.buffer;

    if (dir) {
        snprintf(pathbuf, PATH_MAX, "%s/%s", dir, file);
    } else {
        snprintf(pathbuf, PATH_MAX, "%s", file);
    }
    snprintf(tmppath, PATH_MAX, "%s.readsb_tmp", pathbuf);

    fd = open(tmppath, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        fprintf(stderr, "writeJsonTo open(): ");
        perror(tmppath);
        if (!gzip)
            free(content);
        return -1;
    }


    if (gzip) {
        gzFile gzfp = gzdopen(fd, "wb");
        if (!gzfp)
            goto error_1;

        gzbuffer(gzfp, 1024 * 1024);
        int name_len = strlen(file);
        if (name_len > 8 && strcmp("binCraft", file + (name_len - 8)) == 0) {
            gzsetparams(gzfp, gzip_level, Z_FILTERED);
        } else {
            gzsetparams(gzfp, gzip_level, Z_DEFAULT_STRATEGY);
        }

        int res = gzwrite(gzfp, content, len);
        if (res != len) {
            int error;
            fprintf(stderr, "%s: gzwrite of length %d failed: %s (res == %d)\n", pathbuf, len, gzerror(gzfp, &error), res);
        }

        if (gzclose(gzfp) < 0)
            goto error_2;
    } else {
        if (write(fd, content, len) != len) {
            fprintf(stderr, "writeJsonTo write(): ");
            perror(tmppath);
            goto error_1;
        }

        if (close(fd) < 0)
            goto error_2;
    }

    if (rename(tmppath, pathbuf) == -1) {
        fprintf(stderr, "writeJsonTo rename(): %s -> %s", tmppath, pathbuf);
        perror("");
        goto error_2;
    }
    if (!gzip)
        free(content);
    return 0;

error_1:
    close(fd);
error_2:
    unlink(tmppath);
    if (!gzip)
        free(content);
    return -1;
}

int writeJsonToFile (const char* dir, const char *file, struct char_buffer cb) {
    return writeJsonTo(dir, file, cb, 0, 0);
}

int writeJsonToGzip (const char* dir, const char *file, struct char_buffer cb, int gzip) {
    return writeJsonTo(dir, file, cb, 1, gzip);
}

struct char_buffer generateVRS(int part, int n_parts, int reduced_data) {
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 256*1024; // The initial buffer is resized as needed
    char *buf = (char *) aligned_malloc(buflen), *p = buf, *end = buf + buflen;
    int first = 1;
    int part_len = AIRCRAFT_BUCKETS / n_parts;
    int part_start = part * part_len;

    //fprintf(stderr, "%02d/%02d reduced_data: %d\n", part, n_parts, reduced_data);

    p = safe_snprintf(p, end,
            "{\"acList\":[");

    for (int j = part_start; j < part_start + part_len; j++) {
        for (a = Modes.aircraft[j]; a; a = a->next) {
            if (a->messages < 2) { // basic filter for bad decodes
                continue;
            }
            if (now > a->seen + 10 * SECONDS) // don't include stale aircraft in the JSON
                continue;

            // For now, suppress non-ICAO addresses
            if (a->addr & MODES_NON_ICAO_ADDRESS)
                continue;


            if ((p + 2048) >= end) {
                int used = p - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
                //fprintf(stderr, "realloc at %s, line %d.\n", __FILE__, __LINE__);
            }

            if (first)
                first = 0;
            else
                *p++ = ',';

            p = safe_snprintf(p, end, "{\"Icao\":\"%s%06X\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);


            if (trackDataValid(&a->position_valid)) {
                p = safe_snprintf(p, end, ",\"Lat\":%f,\"Long\":%f", a->lat, a->lon);
                //p = safe_snprintf(p, end, ",\"PosTime\":%"PRIu64, a->position_valid.updated);
            }

            if (altReliable(a))
                p = safe_snprintf(p, end, ",\"Alt\":%d", a->altitude_baro);

            if (trackDataValid(&a->geom_rate_valid)) {
                p = safe_snprintf(p, end, ",\"Vsi\":%d", a->geom_rate);
            } else if (trackDataValid(&a->baro_rate_valid)) {
                p = safe_snprintf(p, end, ",\"Vsi\":%d", a->baro_rate);
            }

            if (trackDataValid(&a->track_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->track);
            } else if (trackDataValid(&a->mag_heading_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->mag_heading);
            } else if (trackDataValid(&a->true_heading_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->true_heading);
            }

            if (trackDataValid(&a->gs_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%.1f", a->gs);
            } else if (trackDataValid(&a->ias_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%u", a->ias);
            } else if (trackDataValid(&a->tas_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%u", a->tas);
            }

            if (trackDataValid(&a->altitude_geom_valid))
                p = safe_snprintf(p, end, ",\"GAlt\":%d", a->altitude_geom);

            if (trackDataValid(&a->airground_valid) && a->airground == AG_GROUND)
                p = safe_snprintf(p, end, ",\"Gnd\":true");
            else
                p = safe_snprintf(p, end, ",\"Gnd\":false");

            if (trackDataValid(&a->squawk_valid))
                p = safe_snprintf(p, end, ",\"Sqk\":\"%04x\"", a->squawk);

            if (trackDataValid(&a->nav_altitude_mcp_valid)) {
                p = safe_snprintf(p, end, ",\"TAlt\":%d", a->nav_altitude_mcp);
            } else if (trackDataValid(&a->nav_altitude_fms_valid)) {
                p = safe_snprintf(p, end, ",\"TAlt\":%d", a->nav_altitude_fms);
            }

            if (a->position_valid.source != SOURCE_INVALID) {
                if (a->position_valid.source == SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":true");
                else if (a->position_valid.source == SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":true");
                else if (a->position_valid.source == SOURCE_JAERO)
                    p = safe_snprintf(p, end, ",\"Sat\":true");
            }

            if (reduced_data && a->addrtype != ADDR_JAERO && a->position_valid.source != SOURCE_JAERO)
                goto skip_fields;

            if (trackDataAge(now, &a->callsign_valid) < 5 * MINUTES
                    || (a->position_valid.source == SOURCE_JAERO && trackDataAge(now, &a->callsign_valid) < 8 * HOURS)
               ) {
                char buf[128];
                char buf2[16];
                const char *trimmed = trimSpace(a->callsign, buf2, 8);
                if (trimmed[0] != 0) {
                    p = safe_snprintf(p, end, ",\"Call\":\"%s\"", jsonEscapeString(trimmed, buf, sizeof(buf)));
                    p = safe_snprintf(p, end, ",\"CallSus\":false");
                }
            }

            if (trackDataValid(&a->nav_heading_valid))
                p = safe_snprintf(p, end, ",\"TTrk\":%.1f", a->nav_heading);


            if (trackDataValid(&a->geom_rate_valid)) {
                p = safe_snprintf(p, end, ",\"VsiT\":1");
            } else if (trackDataValid(&a->baro_rate_valid)) {
                p = safe_snprintf(p, end, ",\"VsiT\":0");
            }


            if (trackDataValid(&a->track_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":false");
            } else if (trackDataValid(&a->mag_heading_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":true");
            } else if (trackDataValid(&a->true_heading_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":true");
            }

            p = safe_snprintf(p, end, ",\"Sig\":%d", get8bitSignal(a));

            if (trackDataValid(&a->nav_qnh_valid))
                p = safe_snprintf(p, end, ",\"InHg\":%.2f", a->nav_qnh * 0.02952998307);

            p = safe_snprintf(p, end, ",\"AltT\":%d", 0);


            if (a->position_valid.source != SOURCE_INVALID) {
                if (a->position_valid.source != SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":false");
                if (a->position_valid.source != SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":false");
                if (a->position_valid.source != SOURCE_JAERO)
                    p = safe_snprintf(p, end, ",\"Sat\":false");
            }


            if (trackDataValid(&a->gs_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":0");
            } else if (trackDataValid(&a->ias_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":2");
            } else if (trackDataValid(&a->tas_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":3");
            }

            if (a->adsb_version >= 0)
                p = safe_snprintf(p, end, ",\"Trt\":%d", a->adsb_version + 3);
            else
                p = safe_snprintf(p, end, ",\"Trt\":%d", 1);


            //p = safe_snprintf(p, end, ",\"Cmsgs\":%ld", a->messages);


skip_fields:

            p = safe_snprintf(p, end, "}");
        }
    }

    p = safe_snprintf(p, end, "]}\n");

    if (p >= end)
        fprintf(stderr, "buffer overrun vrs json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateClientsJson() {
    struct char_buffer cb;
    uint64_t now = mstime();

    size_t buflen = 1*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) aligned_malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{ \"now\" : %.1f,\n", now / 1000.0);
    p = safe_snprintf(p, end, "  \"format\" : "
            "[ \"receiverId\", \"host:port\", \"avg. kbit/s\", \"conn time(s)\","
            " \"messages/s\", \"positions/s\", \"reduce_signal\", \"recent_rtt(ms)\" ],\n");

    p = safe_snprintf(p, end, "  \"clients\" : [\n");

    for (struct net_service *s = Modes.services; s; s = s->next) {
        for (struct client *c = s->clients; c; c = c->next) {
            if (!c->service)
                continue;
            if (!s->read_handler)
                continue;

            // check if we have enough space
            if ((p + 1000) >= end) {
                int used = p - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
            }

            char uuid[64]; // needs 36 chars and null byte
            sprint_uuid(c->receiverId, c->receiverId2, uuid);
            //fprintf(stderr, "printing rId %016"PRIx64"%016"PRIx64" %s\n", c->receiverId, c->receiverId2, uuid);

            double elapsed = (now - c->connectedSince) / 1000.0;
            int reduceSignaled = c->service->writer == &Modes.beast_in
                && c->pingReceived + 120 * SECONDS > now;
            p = safe_snprintf(p, end, "[\"%s\",\"%49s\",%6.2f,%6.0f,%8.3f,%7.3f, %d,%5.0f],\n",
                    uuid,
                    c->proxy_string,
                    c->bytesReceived / 128.0 / elapsed,
                    elapsed,
                    (double) c->messageCounter / elapsed,
                    (double) c->positionCounter / elapsed,
                    reduceSignaled,
                    c->recent_rtt);


            if (p >= end)
                fprintf(stderr, "buffer overrun client json\n");
        }
    }

    if (*(p-2) == ',')
        *(p-2) = ' ';

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
