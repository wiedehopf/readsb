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
    if (a->baro_alt_valid.source == source)
        p = safe_snprintf(p, end, "\"altitude\",");
    if (a->geom_alt_valid.source == source)
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
    if (a->pos_reliable_valid.source == source)
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

void printACASInfoShort(uint32_t addr, unsigned char *MV, struct aircraft *a, struct modesMessage *mm, int64_t now) {
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

void logACASInfoShort(uint32_t addr, unsigned char *bytes, struct aircraft *a, struct modesMessage *mm, int64_t now) {

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

        p = sprintAircraftObject(p, end, a, now, 0, mm, false);
        p = safe_snprintf(p, end, "\n");

        if (p - buf >= (int) sizeof(buf) - 1) {
            fprintf(stderr, "logACAS json buffer insufficient!\n");
        } else {
            check_write(Modes.acasFD2, buf, p - buf, "acas.csv");
        }
    }
}

static char *sprintACASJson(char *p, char *end, unsigned char *bytes, struct modesMessage *mm, int64_t now) {
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

char *sprintACASInfoShort(char *p, char *end, uint32_t addr, unsigned char *bytes, struct aircraft *a, struct modesMessage *mm, int64_t now) {
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

    if (a && trackDataValid(&a->pos_reliable_valid))
        p = safe_snprintf(p, end, "%11.6f,", a->latReliable);
    else
        p = safe_snprintf(p, end, "           ,");

    if (a && trackDataValid(&a->pos_reliable_valid))
        p = safe_snprintf(p, end, "%11.6f,", a->lonReliable);
    else
        p = safe_snprintf(p, end, "           ,");

    if (a && altBaroReliable(a))
        p = safe_snprintf(p, end, "%5d,ft,", a->baro_alt);
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

char *sprintAircraftObject(char *p, char *end, struct aircraft *a, int64_t now, int printMode, struct modesMessage *mm, bool includeSeenByList) {

    // printMode == 0: aircraft.json / globe.json / apiBuffer
    // printMode == 1: trace.json
    // printMode == 2: jsonPositionOutput

    p = safe_snprintf(p, end, "{");
    if (printMode == 2)
        p = safe_snprintf(p, end, "\"now\" : %.3f,", now / 1000.0);
    if (printMode != 1)
        p = safe_snprintf(p, end, "\"hex\":\"%s%06x\",", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

    // Include all receivers that pushed an update in the last n seconds
    if (includeSeenByList && Modes.aircraft_json_seen_by_list) {
        p = safe_snprintf(p, end, "\"seenByReceiverIds\":[");

        struct seenByReceiverIdLlEntry *current = a->seenByReceiverIds;
        bool first = true;
        while(current) {
            // This entry is too old, ignore it
            if (Modes.aircraft_json_seen_by_list_timeout > 0 && current->lastTimestamp + Modes.aircraft_json_seen_by_list_timeout * 1000 < now) {
                current = current->next;
                continue;
            }

            char uuid[64];
            sprint_uuid(current->receiverId, current->receiverId2, uuid);
            p = safe_snprintf(p, end, "%s{\"receiverId\":\"%s\",\"lastTimestamp\":%ld}", first ? "" : ",", uuid, current->lastTimestamp);
            first = false;

            current = current->next;
        }
        p = safe_snprintf(p, end, "],");
    }

    p = safe_snprintf(p, end, "\"type\":\"%s\"", addrtype_enum_string(a->addrtype));
    if (trackDataValid(&a->callsign_valid)) {
        char buf[128];
        p = safe_snprintf(p, end, ",\"flight\":\"%s\"", jsonEscapeString(a->callsign, buf, sizeof(buf)));
    }
    if (printMode != 1) {

        if (Modes.db) {
            if (a->registration[0])
                p = safe_snprintf(p, end, ",\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
            if (a->typeCode[0])
                p = safe_snprintf(p, end, ",\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
            if (a->dbFlags) {
                uint32_t dbFlags = a->dbFlags;
                dbFlags &= ~(1 << 7);
                p = safe_snprintf(p, end, ",\"dbFlags\":%u", dbFlags);
            }

            if (Modes.jsonLongtype) {
                dbEntry *e = dbGet(a->addr, Modes.dbIndex);
                if (e) {
                    if (e->typeLong[0])
                        p = safe_snprintf(p, end, ",\"desc\":\"%.*s\"", (int) sizeof(e->typeLong), e->typeLong);
                    if (e->ownOp[0])
                        p = safe_snprintf(p, end, ",\n\"ownOp\":\"%.*s\"", (int) sizeof(e->ownOp), e->ownOp);
                    if (e->year[0])
                        p = safe_snprintf(p, end, ",\n\"year\":\"%.*s\"", (int) sizeof(e->year), e->year);
                }
            }
        }

        if (trackDataValid(&a->airground_valid) && a->airground == AG_GROUND) {
            if (0)
                p = safe_snprintf(p, end, ",\"ground\":true");
            else
                p = safe_snprintf(p, end, ",\"alt_baro\":\"ground\"");
        } else {
            if (altBaroReliable(a))
                p = safe_snprintf(p, end, ",\"alt_baro\":%d", a->baro_alt);
            if (0)
                p = safe_snprintf(p, end, ",\"ground\":false");
        }
    }
    if (trackDataValid(&a->geom_alt_valid))
        p = safe_snprintf(p, end, ",\"alt_geom\":%d", a->geom_alt);
    if (printMode != 1 && trackDataValid(&a->gs_valid))
        p = safe_snprintf(p, end, ",\"gs\":%.1f", a->gs);
    if (trackDataValid(&a->ias_valid))
        p = safe_snprintf(p, end, ",\"ias\":%u", a->ias);
    if (trackDataValid(&a->tas_valid))
        p = safe_snprintf(p, end, ",\"tas\":%u", a->tas);
    if (trackDataValid(&a->mach_valid))
        p = safe_snprintf(p, end, ",\"mach\":%.3f", a->mach);
    if (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->baro_alt) < 500) {
        p = safe_snprintf(p, end, ",\"wd\":%.0f", a->wind_direction);
        p = safe_snprintf(p, end, ",\"ws\":%.0f", a->wind_speed);
    }
    if (now < a->oat_updated + TRACK_EXPIRE) {
        p = safe_snprintf(p, end, ",\"oat\":%.0f", a->oat);
        p = safe_snprintf(p, end, ",\"tat\":%.0f", a->tat);
    }

    if (trackDataValid(&a->track_valid)) {
        p = safe_snprintf(p, end, ",\"track\":%.2f", a->track);
    } else if (printMode != 1 && trackDataValid(&a->pos_reliable_valid) && !(trackDataValid(&a->airground_valid) && a->airground == AG_GROUND)) {
        p = safe_snprintf(p, end, ",\"calc_track\":%.0f", a->calc_track);
    }

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
        if (trackDataValid(&a->pos_reliable_valid)) {
            p = safe_snprintf(p, end, ",\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.3f",
                    a->latReliable, a->lonReliable, a->pos_nic_reliable, a->pos_rc_reliable,
                (now < a->pos_reliable_valid.updated) ? 0 : ((now - a->pos_reliable_valid.updated) / 1000.0));
#if defined(TRACKS_UUID)
            char uuid[32]; // needs 18 chars and null byte
            sprint_uuid1(a->lastPosReceiverId, uuid);
            p = safe_snprintf(p, end, ",\"rId\":\"%s\"", uuid);
#endif
            if (Modes.userLocationValid) {
                p = safe_snprintf(p, end, ",\"r_dst\":%.3f,\"r_dir\":%.1f", a->receiver_distance / 1852.0, a->receiver_direction);
            }
        } else {
            if (now < a->rr_seen + 2 * MINUTES) {
                p = safe_snprintf(p, end, ",\"rr_lat\":%.1f,\"rr_lon\":%.1f", a->rr_lat, a->rr_lon);
            }
            if (now < a->seenPosReliable + 14 * 24 * HOURS) {
                p = safe_snprintf(p, end, ",\"lastPosition\":{\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.3f}",
                        a->latReliable, a->lonReliable, a->pos_nic_reliable, a->pos_rc_reliable,
                        (now < a->seenPosReliable) ? 0 : ((now - a->seenPosReliable) / 1000.0));
            }
        }
        if (nogps(now, a)) {
            p = safe_snprintf(p, end, ",\"gpsOkBefore\":%.1f", a->seenAdsbReliable / 1000.0);
            if (a->seenAdsbLat || a->seenAdsbLon) {
                p = safe_snprintf(p, end, ",\"gpsOkLat\":%f,\"gpsOkLon\":%f", a->seenAdsbLat, a->seenAdsbLon);
            }
        }
    }

    if (printMode == 1 && trackDataValid(&a->pos_reliable_valid)) {
        p = safe_snprintf(p, end, ",\"nic\":%u,\"rc\":%u",
                a->pos_nic_reliable, a->pos_rc_reliable);
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
    if (a->pos_reliable_valid.source == SOURCE_JAERO)
        p = safe_snprintf(p, end, ",\"jaero\": true");
    if (a->pos_reliable_valid.source == SOURCE_SBS)
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

char *sprintAircraftRecent(char *p, char *end, struct aircraft *a, int64_t now, int printMode, struct modesMessage *mm, int64_t recent) {
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
    if (recent > trackDataAge(now, &a->baro_alt_valid))
        p = safe_snprintf(p, end, ",\"alt_baro\":%d", a->baro_alt);
    if (recent > trackDataAge(now, &a->geom_alt_valid))
        p = safe_snprintf(p, end, ",\"alt_geom\":%d", a->geom_alt);
    if (recent > trackDataAge(now, &a->gs_valid))
        p = safe_snprintf(p, end, ",\"gs\":%.1f", a->gs);
    if (recent > trackDataAge(now, &a->ias_valid))
        p = safe_snprintf(p, end, ",\"ias\":%u", a->ias);
    if (recent > trackDataAge(now, &a->tas_valid))
        p = safe_snprintf(p, end, ",\"tas\":%u", a->tas);
    if (recent > trackDataAge(now, &a->mach_valid))
        p = safe_snprintf(p, end, ",\"mach\":%.3f", a->mach);
    if (now < a->wind_updated + recent && abs(a->wind_altitude - a->baro_alt) < 500) {
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
    if (recent > trackDataAge(now, &a->pos_reliable_valid)) {
        p = safe_snprintf(p, end, ",\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.3f",
                a->latReliable, a->lonReliable, a->pos_nic_reliable, a->pos_rc_reliable,
                (now < a->pos_reliable_valid.updated) ? 0 : ((now - a->pos_reliable_valid.updated) / 1000.0));
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

size_t calculateSeenByListJsonSize(struct aircraft *a, int64_t now)
{
    if (!Modes.aircraft_json_seen_by_list || !Modes.json_dir)
        return 0;

    // Base property
    size_t size = 23; // The empty array
    struct seenByReceiverIdLlEntry *current = a->seenByReceiverIds;
    while(current) {
        if (Modes.aircraft_json_seen_by_list_timeout > 0 && current->lastTimestamp + Modes.aircraft_json_seen_by_list_timeout * 1000 < now) {
            current = current->next;
            continue;
        }

        size += 90; // Length of one entry object with INT64_MAX as timestamp and trailing comma
        current = current->next;
    }

    return size;
}

int includeAircraftJson(int64_t now, struct aircraft *a) {
    if (unlikely(a == NULL)) {
        fprintf(stderr, "includeAircraftJson: got NULL pointer\n");
        return 0;
    }
    if (a->messages < 2) {
        return 0;
    }

    if (a->nogpsCounter >= NOGPS_SHOW && now - a->seenAdsbReliable < NOGPS_DWELL) {
        return 1;
    }

    // include all aircraft with valid position
    if (a->pos_reliable_valid.source != SOURCE_INVALID) {
        return 1;
    }

    // include active aircraft
    if (now < a->seen + TRACK_EXPIRE) {
        return 1;
    }

    return 0;
}

struct char_buffer generateAircraftBin(threadpool_buffer_t *pbuffer) {
    struct char_buffer cb;
    int64_t now = mstime();
    struct aircraft *a;

    struct craftArray *ca = &Modes.aircraftActive;
    ca_lock_read(ca);
    size_t alloc = 4096 + ca->len * sizeof(struct binCraft);

    char *buf = check_grow_threadpool_buffer_t(pbuffer, alloc);
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

    int32_t receiver_lat = 0;
    int32_t receiver_lon = 0;
    if (Modes.userLocationValid) {
        if (Modes.json_location_accuracy == 1) {
            receiver_lat = (int32_t) (1E4 * nearbyint(Modes.fUserLat * 1E2));
            receiver_lon = (int32_t) (1E4 * nearbyint(Modes.fUserLon * 1E2));
        } else if (Modes.json_location_accuracy == 2) {
            receiver_lat = (int32_t) nearbyint(Modes.fUserLat * 1E6);
            receiver_lon = (int32_t) nearbyint(Modes.fUserLon * 1E6);
        }
    }
    memWrite(p, receiver_lat);
    memWrite(p, receiver_lon);

    memWrite(p, Modes.binCraftVersion);

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
            fprintf(stderr, "buffer overrun aircraftBin\n");
            break;
        }

        toBinCraft(a, (struct binCraft *) p, now);
        p += sizeof(struct binCraft);

    }

    ca_unlock_read(ca);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;

#undef memWrite
}

struct char_buffer generateGlobeBin(int globe_index, int mil, threadpool_buffer_t *pbuffer) {
    struct char_buffer cb = { 0 };
    int64_t now = mstime();
    struct aircraft *a;
    ssize_t alloc = 4096 + 4 * sizeof(struct binCraft);

    struct craftArray *ca = NULL;

    if (globe_index == -1) {
        ca = &Modes.aircraftActive;
    } else if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
    }

    if (!ca) {
        fprintf(stderr, "generateGlobeBin: bad globe_index: %d\n", globe_index);
        return cb;
    }

    ca_lock_read(ca);

    alloc += ca->len * sizeof(struct binCraft);

    char *buf = check_grow_threadpool_buffer_t(pbuffer, alloc);
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

    int32_t dummy1 = 0;
    memWrite(p, dummy1);

    int32_t dummy2 = 0;
    memWrite(p, dummy2);

    memWrite(p, Modes.binCraftVersion);

    if (p - buf > (int) elementSize)
        fprintf(stderr, "buffer overrun globeBin\n");

    p = buf + elementSize;

    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];
        if (a == NULL) continue;

        if (!includeAircraftJson(now, a))
            continue;

        if (mil && !(a->dbFlags & 1))
            continue;
        // check if we have enough space
        if ((p + 2 * sizeof(struct binCraft)) >= end) {
            fprintf(stderr, "buffer insufficient globeBin\n");
            break;
        }

        toBinCraft(a, (struct binCraft *) p, now);
        p += sizeof(struct binCraft);

    }

    ca_unlock_read(ca);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;

#undef memWrite
}

struct char_buffer generateGlobeJson(int globe_index, threadpool_buffer_t *pbuffer) {
    struct char_buffer cb = { 0 };
    int64_t now = mstime();
    struct aircraft *a;
    ssize_t alloc = 4096; // The initial buffer is resized as needed

    struct craftArray *ca = NULL;
    if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
    }
    if (!ca) {
        fprintf(stderr, "generateAircraftJson: bad globe_index: %d\n", globe_index);
        return cb;
    }
    ca_lock_read(ca);

    alloc += ca->len * 2048; // 2048 bytes per potential aircraft object

    char *buf = check_grow_threadpool_buffer_t(pbuffer, alloc);

    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.3f,\n"
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

    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];
        if (a == NULL) continue;

        if (!includeAircraftJson(now, a))
            continue;

        // check if we have enough space
        // disable this due to passbuffer .... this would be a memleak
        if (0 && (p + 2000) >= end) {
            int used = p - buf;
            alloc *= 2;
            buf = (char *) realloc(buf, alloc);
            p = buf + used;
            end = buf + alloc;
        }

        p = safe_snprintf(p, end, "\n");
        p = sprintAircraftObject(p, end, a, now, 0, NULL, false);
        p = safe_snprintf(p, end, ",");

        if (p >= end) {
            fprintf(stderr, "buffer overrun aircraft json\n");
            break;
        }
    }

    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    ca_unlock_read(ca);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateAircraftJson(int64_t onlyRecent){
    struct char_buffer cb;
    int64_t now = mstime();
    struct aircraft *a;

    struct craftArray *ca = &Modes.aircraftActive;

    ca_lock_read(ca);

    size_t alloc = 4096 + ca->len * 2048; // The initial buffer is resized as needed

    char *buf = cmalloc(alloc);
    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.3f,\n"
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
            p = sprintAircraftObject(p, end, a, now, 0, NULL, false);
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

    ca_unlock_read(ca);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

static char *sprintTracePoint(char *p, char *end, struct state *state, struct state_all *state_all, int64_t referenceTs, int64_t now, struct aircraft *a) {
    int baro_alt = state->baro_alt / _alt_factor;
    int baro_rate = state->baro_rate / _rate_factor;

    int geom_alt = state->geom_alt / _alt_factor;
    int geom_rate = state->geom_rate / _rate_factor;

    int altitude = baro_alt;
    int altitude_valid = state->baro_alt_valid;
    int altitude_geom = 0;

    if (!altitude_valid && state->geom_alt_valid) {
        altitude = geom_alt;
        altitude_valid = 1;
        altitude_geom = 1;
    }

    int rate = baro_rate;
    int rate_valid = state->baro_rate_valid;
    int rate_geom = 0;

    if (!rate_valid && state->geom_rate_valid) {
        rate = geom_rate;
        rate_valid = 1;
        rate_geom = 1;
    }

    // in the air
    p = safe_snprintf(p, end, "\n[%.2f,%f,%f",
            (state->timestamp - referenceTs) / 1000.0, state->lat / 1E6, state->lon / 1E6);

    if (state->timestamp > now) {
        fprintf(stderr, "%06x WAT? trace timestamp in the future: %.3f\n", a->addr, state->timestamp / 1000.0);
    }

    if (state->on_ground)
        p = safe_snprintf(p, end, ",\"ground\"");
    else if (altitude_valid)
        p = safe_snprintf(p, end, ",%d", altitude);
    else
        p = safe_snprintf(p, end, ",null");

    if (state->gs_valid)
        p = safe_snprintf(p, end, ",%.1f", state->gs / _gs_factor);
    else
        p = safe_snprintf(p, end, ",null");

    if (state->track_valid)
        p = safe_snprintf(p, end, ",%.1f", state->track / _track_factor);
    else
        p = safe_snprintf(p, end, ",null");

    int bitfield = (altitude_geom << 3) | (rate_geom << 2) | (state->leg_marker << 1) | (state->stale << 0);
    p = safe_snprintf(p, end, ",%d", bitfield);

    if (rate_valid)
        p = safe_snprintf(p, end, ",%d", rate);
    else
        p = safe_snprintf(p, end, ",null");

    if (state_all) {
        int64_t now = state->timestamp;
        struct aircraft b;
        memset(&b, 0, sizeof(struct aircraft));
        struct aircraft *ac = &b;
        from_state_all(state_all, state, ac, now);

        p = safe_snprintf(p, end, ",");
        p = sprintAircraftObject(p, end, ac, now, 1, NULL, false);
    } else {
        p = safe_snprintf(p, end, ",null");
    }

    p = safe_snprintf(p, end, ",\"%s\"", addrtype_enum_string(state->addrtype));

    if (state->geom_alt_valid)
        p = safe_snprintf(p, end, ",%d", geom_alt);
    else
        p = safe_snprintf(p, end, ",null");

    if (state->geom_rate_valid)
        p = safe_snprintf(p, end, ",%d", geom_rate);
    else
        p = safe_snprintf(p, end, ",null");

    if (state->ias_valid)
        p = safe_snprintf(p, end, ",%d", state->ias);
    else
        p = safe_snprintf(p, end, ",null");

    if (state->roll_valid)
        p = safe_snprintf(p, end, ",%.1f", state->roll / _roll_factor);
    else
        p = safe_snprintf(p, end, ",null");

#if defined(TRACKS_UUID)
    char uuid[32]; // needs 8 chars and null byte
    sprint_uuid1_partial(state->receiverId, uuid);
    p = safe_snprintf(p, end, ",\"%s\"", uuid);
#endif

    p = safe_snprintf(p, end, "],");

    return p;
}

static void checkTraceCache(struct aircraft *a, traceBuffer tb, int64_t now) {
    struct traceCache *cache = &a->traceCache;
    if (!cache->entries || !cache->json || !cache->json_max) {
        if (Modes.trace_hist_only & 8) {
            return; // no cache in this special case
        }
        int64_t elapsedReliable = now - a->seenPosReliable;
        if (elapsedReliable > TRACE_CACHE_LIFETIME / 2 || tb.len == 0) {
            //fprintf(stderr, "elapsedReliable: %.3f\n", elapsedReliable / 1000.0);
            return;
        }
        if (cache->entries || cache->json || cache->json_max) {
            fprintf(stderr, "%06x wtf Eijo0eep\n", a->addr);
            sfree(cache->entries);
            sfree(cache->json);
        }

        // reset cache for good measure
        memset(cache, 0x0, sizeof(struct traceCache));

        ssize_t size_entries = Modes.traceCachePoints * sizeof(struct traceCacheEntry);
        cache->json_max = Modes.traceCachePoints * 35 * 8; // 280 per entry

        // allocate memory
        cache->totalAlloc = size_entries + cache->json_max;
        cache->entries = cmalloc(cache->totalAlloc);
        cache->json = ((char *) cache->entries) + size_entries;

        if (!cache->entries || !cache->json) {
            fprintf(stderr, "%06x wtf quae3OhG\n", a->addr);
        }

        memset(cache->entries, 0x0, size_entries);
        memset(cache->json, 0x0, cache->json_max);
    }

    char *p;
    char *end = cache->json + cache->json_max;
    int firstRecent = imax(0, tb.len - Modes.traceRecentPoints);
    int firstRecentCache = 0;
    int64_t firstRecentTs = getState(tb.trace, firstRecent)->timestamp;

    struct traceCacheEntry *entries = cache->entries;
    int cacheIndex = 0;
    int found = 0;
    while (cacheIndex < cache->entriesLen) {
        if (entries[cacheIndex].ts == firstRecentTs) {
            found = 1;
            firstRecentCache = cacheIndex;
            break;
        }
        cacheIndex++;
    }
    int resetCache = 1; // by default reset the cache instead of updating it

    if (a->addr == TRACE_FOCUS) {
        if (found) {
            fprintf(stderr, "%06x firstRecentTs found %d, entriesLen: %d\n", a->addr, firstRecentCache, cache->entriesLen);
        } else {
            fprintf(stderr, "%06x firstRecentTs not found, entriesLen: %d\n", a->addr, cache->entriesLen);
        }
    }

    if (found) {
        resetCache = 0;
        int recentPoints = tb.len - firstRecent;
        int usableCachePoints = cache->entriesLen - firstRecentCache;
        int newEntryCount = recentPoints - usableCachePoints;
        if (cache->entriesLen + newEntryCount > Modes.traceCachePoints) {

            // if the cache would get over capacity, do memmove fun!
            // first move the bookkeeping structs (struct traceCacheEntry)
            // second move the cached json, offsets and length of it are stored in the bookkeeping structs

            // remove indexes before firstRecentCache using memmove
            int moveIndexes = firstRecentCache;


            // remove json belonging to entries before firstRecentCache using memmove
            int moveDist = entries[moveIndexes].offset;
            struct traceCacheEntry *last = &entries[cache->entriesLen - 1];
            int moveBytes = (last->offset + last->len) - moveDist;

            if (cache->entriesLen > Modes.traceCachePoints || moveIndexes <= 0 || moveIndexes > cache->entriesLen) {
                fprintf(stderr, "%06x unexpected value moveIndexes: %ld firstRecentCache: %ld newEntryCount: %ld cache->entriesLen: %ld Modes.traceCachePoints: %ld\n",
                        a->addr, (long) moveIndexes, (long) firstRecentCache, (long) newEntryCount, (long) cache->entriesLen, (long) Modes.traceCachePoints);
                resetCache = 1;
            }


            if (moveDist + moveBytes > cache->json_max || moveBytes <= 0 || moveDist <= 0) {
                fprintf(stderr, "%06x in checkTraceCache: prevented illegal memmove: firstRecentCache: %ld moveIndexes: %ld newEntryCount: %ld moveBytes: %ld moveDist: %ld json_max: %ld cache->entriesLen: %ld\n",
                        a->addr, (long) firstRecentCache, (long) moveIndexes, (long) newEntryCount, (long) moveBytes, (long) moveDist, (long) cache->json_max, (long) cache->entriesLen);
                resetCache = 1;
            }

            if (!resetCache) {
                cache->entriesLen -= moveIndexes;
                firstRecentCache -= moveIndexes;

                memmove(entries, entries + moveIndexes, cache->entriesLen * sizeof(struct traceCacheEntry));
                memmove(cache->json, cache->json + moveDist, moveBytes);
                for (int x = 0; x < cache->entriesLen; x++) {
                    entries[x].offset -= moveDist;
                }

                if (a->addr == TRACE_FOCUS) {
                    fprintf(stderr, "%06x moveIndexes: %ld firstRecentCache: %ld newEntryCount: %ld cache->entriesLen: %ld Modes.traceCachePoints: %ld\n",
                            a->addr, (long) moveIndexes, (long) firstRecentCache, (long) newEntryCount, (long) cache->entriesLen, (long) Modes.traceCachePoints);
                }
            }
        }
    }

    if (cache->referenceTs && firstRecentTs > cache->referenceTs + 8 * HOURS) {
        if (a->addr == TRACE_FOCUS) {
            fprintf(stderr, "%06x referenceTs diff: %.1f h\n", a->addr, (getState(tb.trace, firstRecent)->timestamp - cache->referenceTs) / (double) (1 * HOURS));
        }
        // rebuild cache if referenceTs is too old to avoid very large numbers for the relative time
        resetCache = 1;
    }

    if (resetCache) {
        // reset / initialize stuff / rebuild cache
        cache->referenceTs = getState(tb.trace, firstRecent)->timestamp;
        firstRecentCache = 0;
        cache->entriesLen = 0;
        if (a->addr == TRACE_FOCUS) {
            fprintf(stderr, "%06x resetting traceCache\n", a->addr);
        }
    }

    cache->firstRecentCache = firstRecentCache;

    int sprintCount = 0;
    if (0 && a->addr == TRACE_FOCUS) {
        fprintf(stderr, "%06x sprintCache: %d points firstRecent starting %d (firstRecentCache starting %d, max %d)\n", a->addr, tb.len - firstRecent, firstRecent, firstRecentCache, Modes.traceCachePoints);
    }

    struct traceCacheEntry *entry = NULL;
    struct state *state = NULL;
    int64_t lastTs = 0;

    if (!cache->entries) {
        fprintf(stderr, "%06x wtf null pointer ?!?! aeV3Geih\n", a->addr);
    }
    if (!cache->json) {
        fprintf(stderr, "%06x wtf null pointer ?!?! ing5umuS\n", a->addr);
    }
    if (!cache->entries || !cache->json || !cache->json_max) {
        fprintf(stderr, "%06x wtf ANgo9Joo\n", a->addr);
    }

    for (int i = firstRecent, k = firstRecentCache; i < tb.len && k < Modes.traceCachePoints; i++, k++) {
        state = getState(tb.trace, i);
        entry = &entries[k];

        if (k < cache->entriesLen && entry->ts == state->timestamp && state->leg_marker == entry->leg_marker) {
            // cache is up to date, advance
            continue;
        }

        // cache needs updating:
        cache->entriesLen = k;

        if (k < 0) {
            fprintf(stderr, "%06x wtf null pointer ?!?! oonae2OJ\n", a->addr);
        }
        if (k == 0) {
            p = cache->json;
        } else {
            struct traceCacheEntry *prev = &entries[k - 1];
            p = cache->json + prev->offset + prev->len;
            if (p < cache->json) {
                fprintf(stderr, "%06x wtf null pointer ?!?! Ge2thiap\n", a->addr);
            }
        }

        struct state_all *state_all = getStateAll(tb.trace, i);

        if (!p) {
            fprintf(stderr, "%06x wtf null pointer ?!?! Mai9eice\n", a->addr);
            break;
        }
        if (!cache->entries || !cache->json) {
            fprintf(stderr, "%06x wtf null pointer ?!?! ohj4Ohbi\n", a->addr);
            break;
        }

        char *stringStart = p;
        p = sprintTracePoint(p, end, state, state_all, cache->referenceTs, now, a);
        if (p + 1 >= end) {
            fprintf(stderr, "traceCache full, not an issue but fix it! p + 1 - end: %lld json_max: %lld p - cache-json: %lld\n",
                    (long long) (p + 1 - end), (long long) cache->json_max, (long long) (p - cache->json));
            // not enough space to safely write another cache
            break;
        }

        sprintCount++;

        entry->ts = state->timestamp;
        entry->offset = stringStart - cache->json;
        entry->len = p - stringStart;
        entry->leg_marker = state->leg_marker;

        cache->entriesLen = k + 1;
        if (cache->entriesLen > Modes.traceCachePoints) {
            fprintf(stderr, "%06x wtf phooTie1\n", a->addr);
            cache->entriesLen = Modes.traceCachePoints;
            break;
        }

        *p = '\0';
        if (0 && state->timestamp < lastTs) {
            fprintf(stderr, "%06x trace timestamps wrong order: %.3f %.3f i: %d k: %d %s\n", a->addr, lastTs / 1000.0, state->timestamp / 1000.0, i, k, stringStart);
        }
        lastTs = state->timestamp;
    }

    if (cache->entriesLen < tb.len - firstRecent) {
        fprintf(stderr, "%06x traceCache FAIL, entriesLen %d recent points %d\n", a->addr, cache->entriesLen, tb.len - firstRecent);
    } else {
        if (a->addr == TRACE_FOCUS) {
            fprintf(stderr, "%06x traceCache succeeded, entriesLen %d recent points %d\n", a->addr, cache->entriesLen, tb.len - firstRecent);
        }
    }
}

struct char_buffer generateTraceJson(struct aircraft *a, traceBuffer tb, int start, int last, threadpool_buffer_t *buffer, int64_t referenceTs) {
    struct char_buffer cb = { 0 };
    if (!Modes.json_globe_index) {
        return cb;
    }
    int64_t now = mstime();

    int recent = (last == -2) ? 1 : 0;
    if (last < 0) {
        last = tb.len - 1;
    }
    if (recent) {
        start = imax(0, tb.len - Modes.traceRecentPoints);
    }
    if (start < 0) {
        fprintf(stderr, "WTF chu0Uub8\n");
        start = 0;
    }

    int traceCount = imax(last - start + 1, 0);
    ssize_t alloc = traceCount * 300 + 1024;

    char *buf = check_grow_threadpool_buffer_t(buffer, alloc);
    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end, "{\"icao\":\"%s%06x\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

    if (Modes.db) {
        char *regInfo = p;
        if (a->registration[0]) {
            p = safe_snprintf(p, end, ",\n\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
        }
        if (a->typeCode[0]) {
            p = safe_snprintf(p, end, ",\n\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
        }
        if (a->typeCode[0] || a->registration[0] || a->dbFlags) {
            uint32_t dbFlags = a->dbFlags;
            dbFlags &= ~(1 << 7);
            p = safe_snprintf(p, end, ",\n\"dbFlags\":%u", dbFlags);
        }
        dbEntry *e = dbGet(a->addr, Modes.dbIndex);
        if (e) {
            if (e->typeLong[0])
                p = safe_snprintf(p, end, ",\n\"desc\":\"%.*s\"", (int) sizeof(e->typeLong), e->typeLong);
            if (e->ownOp[0])
                p = safe_snprintf(p, end, ",\n\"ownOp\":\"%.*s\"", (int) sizeof(e->ownOp), e->ownOp);
            if (e->year[0])
                p = safe_snprintf(p, end, ",\n\"year\":\"%.*s\"", (int) sizeof(e->year), e->year);
        }
        if (p == regInfo)
            p = safe_snprintf(p, end, ",\n\"noRegData\":true");
    }

    int64_t firstStamp = 0;
    if (start >= 0 && start < tb.len) {
        firstStamp = getState(tb.trace, start)->timestamp;
        if (!referenceTs || firstStamp < referenceTs) {
            referenceTs = firstStamp;
        }
    } else {
        referenceTs = now;
    }

    struct traceCache *tCache = NULL;
    struct traceCacheEntry *entries = NULL;
    // due to timestamping, only use trace cache for recent trace jsons
    if (recent && firstStamp != 0) {
        checkTraceCache(a, tb, now);
        tCache = &a->traceCache;
        if (tCache->entries && tCache->entriesLen > 0) {
            entries = tCache->entries;
            referenceTs = tCache->referenceTs;
        } else {
            tCache = NULL;
        }
    }

    p = safe_snprintf(p, end, ",\n\"timestamp\": %.3f", referenceTs / 1000.0);

    p = safe_snprintf(p, end, ",\n\"trace\":[ ");

    if (start >= 0) {
        if (tCache) {
            if (0 && a->addr == TRACE_FOCUS) {
                fprintf(stderr, "%06x using tCache starting with tCache->firstRecentCache %d stateIndex %d\n", a->addr, tCache->firstRecentCache, start);
            }

            struct traceCacheEntry *first = &entries[tCache->firstRecentCache];
            struct traceCacheEntry *last = &entries[tCache->entriesLen - 1];
            int bytes = last->offset - first->offset + last->len;

            memcpy(p, tCache->json + first->offset, bytes);
            p += bytes;

        } else {
            for (int i = start; i <= last && i < tb.len; i++) {
                struct state *state = getState(tb.trace, i);
                struct state_all *state_all = getStateAll(tb.trace, i);
                p = sprintTracePoint(p, end, state, state_all, referenceTs, now, a);
            }
        }

        if (*(p-1) == ',') {
            p--; // remove last comma
        }
    }

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
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

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
    p = safe_snprintf(p, end, ", \"readsb\": true"); // for tar1090 so it can tell it's not dump1090-fa


    if (Modes.db || Modes.db2) {
        p = safe_snprintf(p, end, ", \"dbServer\": true");
    }

    if (Modes.json_globe_index) {

        p = safe_snprintf(p, end, ", \"json_trace_interval\": %.1f", ((double) Modes.json_trace_interval) / (1 * SECONDS));

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

    if (Modes.tar1090_use_api) {
        p = safe_snprintf(p, end, ", \"reapi\": true");
    }
    p = safe_snprintf(p, end, ", \"binCraft\": true");
    p = safe_snprintf(p, end, ", \"zstd\": true");

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
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

    // check for maximum over last 24 ivals and current ival
    struct distCoords record[RANGEDIRS_BUCKETS];
    memset(record, 0, sizeof(record));
    for (int hour = 0; hour < RANGEDIRS_IVALS; hour++) {
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
static inline __attribute__((always_inline)) struct char_buffer writeJsonTo (const char* dir, const char *file, struct char_buffer cb, int gzip, int gzip_level) {

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

    int firstOpenFail = 1;
open:
    fd = open(tmppath, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        if (firstOpenFail) {
            unlink(tmppath);
            firstOpenFail = 0;
            goto open;
        }
        fprintf(stderr, "writeJsonTo open(): ");
        perror(tmppath);
        goto error_2;
    }


    if (gzip) {
        gzFile gzfp = gzdopen(fd, "wb");
        if (!gzfp)
            goto error_1;

        int gBufSize = 128 * 1024;
        if (len < 16 * 1024) {
            gBufSize = 16 * 1024;
        }
        gzbuffer(gzfp, gBufSize);

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

    goto out;

error_1:
    close(fd);
error_2:
    unlink(tmppath);

out:
    return cb;
}

struct char_buffer writeJsonToFile (const char* dir, const char *file, struct char_buffer cb) {
    return writeJsonTo(dir, file, cb, 0, 0);
}

struct char_buffer writeJsonToGzip (const char* dir, const char *file, struct char_buffer cb, int gzip) {
    return writeJsonTo(dir, file, cb, 1, gzip);
}

struct char_buffer generateVRS(int part, int n_parts, int reduced_data) {
    struct char_buffer cb;
    int64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 256*1024; // The initial buffer is resized as needed
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;
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


            if (trackDataValid(&a->pos_reliable_valid)) {
                p = safe_snprintf(p, end, ",\"Lat\":%f,\"Long\":%f", a->latReliable, a->lonReliable);
                //p = safe_snprintf(p, end, ",\"PosTime\":%"PRIu64, a->pos_reliable_valid.updated);
            }

            if (altBaroReliable(a))
                p = safe_snprintf(p, end, ",\"Alt\":%d", a->baro_alt);

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

            if (trackDataValid(&a->geom_alt_valid))
                p = safe_snprintf(p, end, ",\"GAlt\":%d", a->geom_alt);

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

            if (a->pos_reliable_valid.source != SOURCE_INVALID) {
                if (a->pos_reliable_valid.source == SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":true");
                else if (a->pos_reliable_valid.source == SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":true");
                else if (a->pos_reliable_valid.source == SOURCE_JAERO)
                    p = safe_snprintf(p, end, ",\"Sat\":true");
            }

            if (reduced_data && a->addrtype != ADDR_JAERO && a->pos_reliable_valid.source != SOURCE_JAERO)
                goto skip_fields;

            if (trackDataAge(now, &a->callsign_valid) < 5 * MINUTES
                    || (a->pos_reliable_valid.source == SOURCE_JAERO && trackDataAge(now, &a->callsign_valid) < 8 * HOURS)
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


            if (a->pos_reliable_valid.source != SOURCE_INVALID) {
                if (a->pos_reliable_valid.source != SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":false");
                if (a->pos_reliable_valid.source != SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":false");
                if (a->pos_reliable_valid.source != SOURCE_JAERO)
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
    int64_t now = mstime();

    size_t buflen = 1*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{ \"now\" : %.3f,\n", now / 1000.0);
    p = safe_snprintf(p, end, "  \"format\" : "
            "[ \"receiverId\", \"host:port\", \"avg. kbit/s\", \"conn time(s)\","
            " \"messages/s\", \"positions/s\", \"reduce_signal\", \"recent_rtt(ms)\", \"positions\" ],\n");

    p = safe_snprintf(p, end, "  \"clients\" : [\n");

    for (struct net_service *service = Modes.services_in.services; service->descr; service++) {
        if (!service->read_handler) {
            continue;
        }
        for (struct client *c = service->clients; c; c = c->next) {
            if (!c->service)
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
            p = safe_snprintf(p, end, "[\"%s\",\"%49s\",%6.2f,%6.0f,%8.3f,%7.3f, %d,%5.0f, %10lld],\n",
                    uuid,
                    c->proxy_string,
                    c->bytesReceived / 128.0 / elapsed,
                    elapsed,
                    (double) c->messageCounter / elapsed,
                    (double) c->positionCounter / elapsed,
                    reduceSignaled,
                    c->recent_rtt,
                    (long long) c->positionCounter);



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
