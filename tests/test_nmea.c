#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "src/lib/minmea/minmea.h"
#include "src/lib/minmea/gps.h"

static void test_nmea_checksums(void) {
    const char *sentences[] = {
        "$GPTXT,01,01,02,ANTSTATUS=OPEN*2B\r\n",
        "$GNRMC,000357.096,V,,,,,0.00,0.00,060180,,,N,V*28\r\n",
        "$GNVTG,0.00,T,,M,0.00,N,0.00,K,N*2C\r\n",
        "$GNGGA,000357.096,,,,,0,0,,,M,,M,,*58\r\n",
        "$GNGSA,A,1,,,,,,,,,,,,,,,,1*1D\r\n",
        "$GNGSA,A,1,,,,,,,,,,,,,,,,2*1E\r\n",
        "$GPGSV,1,1,00,1*64\r\n",
        "$GLGSV,1,1,00,1*78\r\n",
        "$GNGLL,,,,,000357.096,V,N*6A\r\n",
        NULL
    };

    for (int i = 0; sentences[i] != NULL; i++) {
        assert(minmea_check(sentences[i], true));
    }

    // Invalid checksum test
    assert(!minmea_check("$GPTXT,01,01,02,ANTSTATUS=OPEN*00\r\n", true));
    printf("PASS: test_nmea_checksums\n");
}

static void test_nmea_sentence_ids(void) {
    assert(minmea_sentence_id("$GPTXT,01,01,02,ANTSTATUS=OPEN*2B\r\n", false) == MINMEA_SENTENCE_TXT);
    assert(minmea_sentence_id("$GNRMC,000357.096,V,,,,,0.00,0.00,060180,,,N,V*28\r\n", false) == MINMEA_SENTENCE_RMC);
    assert(minmea_sentence_id("$GNVTG,0.00,T,,M,0.00,N,0.00,K,N*2C\r\n", false) == MINMEA_SENTENCE_VTG);
    assert(minmea_sentence_id("$GNGGA,000357.096,,,,,0,0,,,M,,M,,*58\r\n", false) == MINMEA_SENTENCE_GGA);
    assert(minmea_sentence_id("$GNGSA,A,1,,,,,,,,,,,,,,,,1*1D\r\n", false) == MINMEA_SENTENCE_GSA);
    assert(minmea_sentence_id("$GPGSV,1,1,00,1*64\r\n", false) == MINMEA_SENTENCE_GSV);
    assert(minmea_sentence_id("$GLGSV,1,1,00,1*78\r\n", false) == MINMEA_SENTENCE_GSV);
    assert(minmea_sentence_id("$GNGLL,,,,,000357.096,V,N*6A\r\n", false) == MINMEA_SENTENCE_GLL);

    assert(strcmp(minmea_sentence(MINMEA_SENTENCE_TXT), "TXT") == 0);
    assert(strcmp(minmea_sentence(MINMEA_SENTENCE_RMC), "RMC") == 0);
    assert(strcmp(minmea_sentence(MINMEA_UNKNOWN), "UNKNOWN") == 0);
    assert(strcmp(minmea_sentence(MINMEA_INVALID), "INVALID") == 0);

    printf("PASS: test_nmea_sentence_ids\n");
}

static void test_nmea_parsers(void) {
    // 1. TXT
    struct minmea_sentence_txt frame_txt;
    assert(minmea_parse_txt(&frame_txt, "$GPTXT,01,01,02,ANTSTATUS=OPEN*2B\r\n"));
    assert(frame_txt.total_msgs == 1);
    assert(frame_txt.msg_nr == 1);
    assert(frame_txt.text_id == 2);
    assert(strcmp(frame_txt.text, "ANTSTATUS=OPEN") == 0);

    // 2. RMC
    struct minmea_sentence_rmc frame_rmc;
    assert(minmea_parse_rmc(&frame_rmc, "$GNRMC,000357.096,V,,,,,0.00,0.00,060180,,,N,V*28\r\n"));
    assert(frame_rmc.valid == false);
    assert(frame_rmc.time.hours == 0);
    assert(frame_rmc.time.minutes == 3);
    assert(frame_rmc.time.seconds == 57);
    assert(frame_rmc.date.day == 6);
    assert(frame_rmc.date.month == 1);
    assert(frame_rmc.date.year == 80);

    // 3. VTG
    struct minmea_sentence_vtg frame_vtg;
    assert(minmea_parse_vtg(&frame_vtg, "$GNVTG,0.00,T,,M,0.00,N,0.00,K,N*2C\r\n"));
    assert(minmea_tofloat(&frame_vtg.true_track_degrees) == 0.0f);
    assert(frame_vtg.magnetic_track_degrees.scale == 0); // empty in sentence

    // 4. GGA
    struct minmea_sentence_gga frame_gga;
    assert(minmea_parse_gga(&frame_gga, "$GNGGA,000357.096,,,,,0,0,,,M,,M,,*58\r\n"));
    assert(frame_gga.fix_quality == 0);
    assert(frame_gga.satellites_tracked == 0);

    // 5. GSA with system ID
    struct minmea_sentence_gsa frame_gsa1;
    assert(minmea_parse_gsa(&frame_gsa1, "$GNGSA,A,1,,,,,,,,,,,,,,,,1*1D\r\n"));
    assert(frame_gsa1.mode == 'A');
    assert(frame_gsa1.fix_type == 1);
    assert(frame_gsa1.system_id == 1);

    struct minmea_sentence_gsa frame_gsa2;
    assert(minmea_parse_gsa(&frame_gsa2, "$GNGSA,A,1,,,,,,,,,,,,,,,,2*1E\r\n"));
    assert(frame_gsa2.system_id == 2);

    // 6. GSV with 0 satellites
    struct minmea_sentence_gsv frame_gsv1;
    assert(minmea_parse_gsv(&frame_gsv1, "$GPGSV,1,1,00,1*64\r\n"));
    assert(frame_gsv1.total_sats == 0);
    assert(frame_gsv1.sats[0].nr == 0);

    // 7. GLL
    struct minmea_sentence_gll frame_gll;
    assert(minmea_parse_gll(&frame_gll, "$GNGLL,,,,,000357.096,V,N*6A\r\n"));
    assert(frame_gll.status == 'V');
    assert(frame_gll.time.hours == 0);
    assert(frame_gll.time.minutes == 3);
    assert(frame_gll.time.seconds == 57);

    printf("PASS: test_nmea_parsers\n");
}

static void test_process_gps_output(void) {
    char buf[128];
    strcpy(buf, "$GPTXT,01,01,02,ANTSTATUS=OPEN*2B\r\n");
    process_gps(buf);

    strcpy(buf, "$GNRMC,000357.096,V,,,,,0.00,0.00,060180,,,N,V*28\r\n");
    process_gps(buf);

    strcpy(buf, "$GNGSA,A,1,,,,,,,,,,,,,,,,1*1D\r\n");
    process_gps(buf);

    printf("PASS: test_process_gps_output\n");
}

int main(void) {
    printf("Running NMEA / GPS unit tests...\n");
    test_nmea_checksums();
    test_nmea_sentence_ids();
    test_nmea_parsers();
    test_process_gps_output();
    printf("All NMEA tests passed successfully!\n");
    return 0;
}
