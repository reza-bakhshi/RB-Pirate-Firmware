#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "lib/minmea/minmea.h"
#include "lib/minmea/gps.h"

static const char* gga_quality_str(int q) {
    switch (q) {
        case 0: return "invalid/no fix";
        case 1: return "GPS SPS fix";
        case 2: return "DGPS fix";
        case 3: return "PPS fix";
        case 4: return "RTK fix";
        case 5: return "RTK float";
        case 6: return "dead reckoning";
        case 7: return "manual input";
        case 8: return "simulation";
        default: return "unknown";
    }
}

static const char* gsa_fix_str(int f) {
    switch (f) {
        case 1: return "no fix";
        case 2: return "2D fix";
        case 3: return "3D fix";
        default: return "unknown";
    }
}

static const char* gsa_system_str(int sys) {
    switch (sys) {
        case 1: return "GPS";
        case 2: return "GLONASS";
        case 3: return "Galileo";
        case 4: return "BeiDou";
        case 5: return "QZSS";
        case 6: return "NavIC";
        default: return NULL;
    }
}

void process_gps(char *line)
{			
    switch (minmea_sentence_id(line, false)) {
        case MINMEA_SENTENCE_RMC: {
            struct minmea_sentence_rmc frame;
            if (minmea_parse_rmc(&frame, line)) {
                int year = (frame.date.year < 80 ? 2000 + frame.date.year : (frame.date.year < 1900 ? 1900 + frame.date.year : frame.date.year));
                if (frame.valid) {
                    printf("$%.2sRMC: valid fix (lat: %f, lon: %f) speed: %.2f kn, course: %.1f deg, %02d/%02d/%04d %02d:%02d:%02d\r\n",
                           frame.type.talker_id,
                           minmea_tocoord(&frame.latitude),
                           minmea_tocoord(&frame.longitude),
                           minmea_tofloat(&frame.speed),
                           minmea_tofloat(&frame.course),
                           frame.date.day, frame.date.month, year,
                           frame.time.hours, frame.time.minutes, frame.time.seconds);
                } else {
                    printf("$%.2sRMC: status: void (no fix), date: %02d/%02d/%04d, time: %02d:%02d:%02d\r\n",
                           frame.type.talker_id,
                           frame.date.day, frame.date.month, year,
                           frame.time.hours, frame.time.minutes, frame.time.seconds);
                }
            } else {
                printf("$%.2sRMC: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_GGA: {
            struct minmea_sentence_gga frame;
            if (minmea_parse_gga(&frame, line)) {
                if (frame.fix_quality > 0) {
                    printf("$%.2sGGA: fix: %s, sats: %d, hdop: %.2f, alt: %.1f%c\r\n",
                           frame.type.talker_id,
                           gga_quality_str(frame.fix_quality),
                           frame.satellites_tracked,
                           minmea_tofloat(&frame.hdop),
                           minmea_tofloat(&frame.altitude),
                           frame.altitude_units ? frame.altitude_units : 'M');
                } else {
                    printf("$%.2sGGA: fix: %s, sats: %d\r\n",
                           frame.type.talker_id,
                           gga_quality_str(frame.fix_quality),
                           frame.satellites_tracked);
                }
            } else {
                printf("$%.2sGGA: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_GSA: {
            struct minmea_sentence_gsa frame;
            if (minmea_parse_gsa(&frame, line)) {
                int active_sats = 0;
                for (int i = 0; i < 12; i++) {
                    if (frame.sats[i] > 0) {
                        active_sats++;
                    }
                }
                const char* sys_name = gsa_system_str(frame.system_id);
                if (sys_name) {
                    printf("$%.2sGSA: mode: %c, fix: %s, active sats: %d, sys: %s (id %d)",
                           frame.type.talker_id, frame.mode, gsa_fix_str(frame.fix_type), active_sats,
                           sys_name, frame.system_id);
                } else {
                    printf("$%.2sGSA: mode: %c, fix: %s, active sats: %d",
                           frame.type.talker_id, frame.mode, gsa_fix_str(frame.fix_type), active_sats);
                }
                if (frame.pdop.scale > 0 || frame.hdop.scale > 0 || frame.vdop.scale > 0) {
                    printf(", pdop: %.2f, hdop: %.2f, vdop: %.2f\r\n",
                           minmea_tofloat(&frame.pdop),
                           minmea_tofloat(&frame.hdop),
                           minmea_tofloat(&frame.vdop));
                } else {
                    printf("\r\n");
                }
            } else {
                printf("$%.2sGSA: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_GLL: {
            struct minmea_sentence_gll frame;
            if (minmea_parse_gll(&frame, line)) {
                if (frame.status == 'A') {
                    printf("$%.2sGLL: valid pos (lat: %f, lon: %f) time: %02d:%02d:%02d\r\n",
                           frame.type.talker_id,
                           minmea_tocoord(&frame.latitude),
                           minmea_tocoord(&frame.longitude),
                           frame.time.hours, frame.time.minutes, frame.time.seconds);
                } else {
                    printf("$%.2sGLL: status: void (no fix), time: %02d:%02d:%02d\r\n",
                           frame.type.talker_id,
                           frame.time.hours, frame.time.minutes, frame.time.seconds);
                }
            } else {
                printf("$%.2sGLL: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_GSV: {
            struct minmea_sentence_gsv frame;
            if (minmea_parse_gsv(&frame, line)) {
                printf("$%.2sGSV: msg %d/%d, sats in view: %d\r\n",
                       frame.type.talker_id, frame.msg_nr, frame.total_msgs, frame.total_sats);
                for (int i = 0; i < 4; i++) {
                    if (frame.sats[i].nr > 0) {
                        printf("  sat #%02d: elev: %d deg, azim: %d deg, snr: %d dB-Hz\r\n",
                               frame.sats[i].nr,
                               frame.sats[i].elevation,
                               frame.sats[i].azimuth,
                               frame.sats[i].snr);
                    }
                }
            } else {
                printf("$%.2sGSV: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_VTG: {
            struct minmea_sentence_vtg frame;
            if (minmea_parse_vtg(&frame, line)) {
                if (frame.magnetic_track_degrees.scale > 0) {
                    printf("$%.2sVTG: track: %.1f deg true, %.1f deg mag, speed: %.2f kn (%.2f km/h)\r\n",
                           frame.type.talker_id,
                           minmea_tofloat(&frame.true_track_degrees),
                           minmea_tofloat(&frame.magnetic_track_degrees),
                           minmea_tofloat(&frame.speed_knots),
                           minmea_tofloat(&frame.speed_kph));
                } else {
                    printf("$%.2sVTG: track: %.1f deg true, speed: %.2f kn (%.2f km/h)\r\n",
                           frame.type.talker_id,
                           minmea_tofloat(&frame.true_track_degrees),
                           minmea_tofloat(&frame.speed_knots),
                           minmea_tofloat(&frame.speed_kph));
                }
            } else {
                printf("$%.2sVTG: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_TXT: {
            struct minmea_sentence_txt frame;
            if (minmea_parse_txt(&frame, line)) {
                printf("$%.2sTXT: msg %d/%d (id %02d): %s\r\n",
                       frame.type.talker_id,
                       frame.msg_nr, frame.total_msgs, frame.text_id,
                       frame.text);
            } else {
                printf("$%.2sTXT: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_GST: {
            struct minmea_sentence_gst frame;
            if (minmea_parse_gst(&frame, line)) {
                printf("$%.2sGST: rms: %.2f, lat err: %.2f, lon err: %.2f, alt err: %.2f\r\n",
                       frame.type.talker_id,
                       minmea_tofloat(&frame.rms_deviation),
                       minmea_tofloat(&frame.latitude_error_deviation),
                       minmea_tofloat(&frame.longitude_error_deviation),
                       minmea_tofloat(&frame.altitude_error_deviation));
            } else {
                printf("$%.2sGST: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_SENTENCE_ZDA: {
            struct minmea_sentence_zda frame;
            if (minmea_parse_zda(&frame, line)) {
                int year = (frame.date.year < 80 ? 2000 + frame.date.year : (frame.date.year < 1900 ? 1900 + frame.date.year : frame.date.year));
                printf("$%.2sZDA: %02d:%02d:%02d %02d/%02d/%04d UTC%+03d:%02d\r\n",
                       frame.type.talker_id,
                       frame.time.hours, frame.time.minutes, frame.time.seconds,
                       frame.date.day, frame.date.month, year,
                       frame.hour_offset, frame.minute_offset);
            } else {
                printf("$%.2sZDA: sentence could not be parsed\r\n", line + 1);
            }
        } break;

        case MINMEA_INVALID: {
            printf("Invalid NMEA sentence checksum\r\n");
        } break;

        default: {
            printf("$%.5s sentence not parsed\r\n", line + 1);
        } break;
    }
}
