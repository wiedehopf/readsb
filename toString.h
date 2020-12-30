#ifndef DUMP1090_TOSTRING_H
#define DUMP1090_TOSTRING_H

static inline const char *cpr_type_string(cpr_type_t type) {
    switch (type) {
        case CPR_SURFACE:
            return "Surface";
        case CPR_AIRBORNE:
            return "Airborne";
        case CPR_COARSE:
            return "TIS-B Coarse";
        default:
            return "unknown CPR type";
    }
}

static inline const char *addrtype_enum_string(addrtype_t type) {
    switch (type) {
        case ADDR_ADSB_ICAO:
            return "adsb_icao";
        case ADDR_ADSB_ICAO_NT:
            return "adsb_icao_nt";
        case ADDR_ADSR_ICAO:
            return "adsr_icao";
        case ADDR_TISB_ICAO:
            return "tisb_icao";

        case ADDR_JAERO:
            return "adsc";
        case ADDR_MLAT:
            return "mlat";
        case ADDR_OTHER:
            return "other";
        case ADDR_MODE_S:
            return "mode_s";

        case ADDR_ADSB_OTHER:
            return "adsb_other";
        case ADDR_ADSR_OTHER:
            return "adsr_other";
        case ADDR_TISB_TRACKFILE:
            return "tisb_trackfile";
        case ADDR_TISB_OTHER:
            return "tisb_other";


        case ADDR_MODE_A:
            return "mode_ac";

        default:
            return "unknown";
    }
}

static inline const char *emergency_enum_string(emergency_t emergency) {
    switch (emergency) {
        case EMERGENCY_NONE: return "none";
        case EMERGENCY_GENERAL: return "general";
        case EMERGENCY_LIFEGUARD: return "lifeguard";
        case EMERGENCY_MINFUEL: return "minfuel";
        case EMERGENCY_NORDO: return "nordo";
        case EMERGENCY_UNLAWFUL: return "unlawful";
        case EMERGENCY_DOWNED: return "downed";
        default: return "reserved";
    }
}

static inline const char *sil_type_enum_string(sil_type_t type) {
    switch (type) {
        case SIL_UNKNOWN: return "unknown";
        case SIL_PER_HOUR: return "perhour";
        case SIL_PER_SAMPLE: return "persample";
        default: return "invalid";
    }
}

static inline const char *source_enum_string(datasource_t src) {
    switch (src) {
        case SOURCE_INVALID: return "SOURCE_INVALID";
        case SOURCE_INDIRECT: return "SOURCE_INDIRECT";
        case SOURCE_MODE_AC: return "SOURCE_MODE_AC";
        case SOURCE_SBS: return "SOURCE_SBS";
        case SOURCE_MLAT: return "SOURCE_MLAT";
        case SOURCE_MODE_S: return "SOURCE_MODE_S";
        case SOURCE_JAERO: return "SOURCE_JAERO";
        case SOURCE_MODE_S_CHECKED: return "SOURCE_MODE_S_CHECKED";
        case SOURCE_TISB: return "SOURCE_TISB";
        case SOURCE_ADSR: return "SOURCE_ADSR";
        case SOURCE_ADSB: return "SOURCE_ADSB";
        case SOURCE_PRIO: return "SOURCE_PRIO";
        default: return "SOURCE_WTF";
    }
}

static inline const char *nav_altitude_source_enum_string(nav_altitude_source_t src) {
    switch (src) {
        case NAV_ALT_INVALID: return "invalid";
        case NAV_ALT_UNKNOWN: return "unknown";
        case NAV_ALT_AIRCRAFT: return "aircraft";
        case NAV_ALT_MCP: return "mcp";
        case NAV_ALT_FMS: return "fms";
        default: return "invalid";
    }
}

static inline const char *airground_to_string(airground_t airground) {
    switch (airground) {
        case AG_GROUND:
            return "ground";
        case AG_AIRBORNE:
            return "airborne";
        case AG_INVALID:
            return "invalid";
        case AG_UNCERTAIN:
            return "airborne?";
        default:
            return "(unknown airground state)";
    }
}

#endif
