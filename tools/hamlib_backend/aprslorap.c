/*
 * Hamlib backend for the APRS LoRa+ tracker (EA7JRS) -- talks the CAT-over-WiFi protocol of the firmware
 * (docs/CAT_HAMLIB.md: Hamlib NET rigctl text protocol, TCP port 4532).
 *
 * The tracker is NOT an audio radio: it has frequency, mode (APRS LoRa / APRS FSK 9600), RSSI, RF power and
 * "PTT = send one APRS beacon" (optional, enabled by the owner in the WebUI).
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hamlib/rig.h>
#include <iofunc.h>
#include <misc.h>
#include <idx_builtin.h>

#include "dummy.h"

#define APRSLORAP_DEFAULT_PATH "aprs-lora-plus.local:4532"
#define APRSLORAP_LINE         96

#define APRSLORAP_MODES  (RIG_MODE_PKTFM | RIG_MODE_PKTFMN)    /* PKTFM = APRS FSK 9600, PKTFMN = APRS LoRa */
#define APRSLORAP_LEVELS (RIG_LEVEL_RFPOWER | RIG_LEVEL_STRENGTH)

/* One line out, one line in (the firmware answers every command with a single line, or "RPRT n") */
static int aprslorap_transaction(RIG *rig, const char *cmd, char *reply, int reply_len)
{
    hamlib_port_t *rp = RIGPORT(rig);
    int retval;

    ENTERFUNC;
    rig_flush(rp);
    retval = write_block(rp, (const unsigned char *) cmd, strlen(cmd));

    if (retval != RIG_OK) { RETURNFUNC(-RIG_EIO); }

    retval = read_string(rp, (unsigned char *) reply, reply_len, "\n", 1, 0, 1);

    if (retval <= 0) { RETURNFUNC(retval < 0 ? retval : -RIG_ETIMEOUT); }

    reply[retval] = 0;
    RETURNFUNC(RIG_OK);
}

/* Commands that answer "RPRT n": 0 = ok, otherwise the Hamlib error is -n */
static int aprslorap_rprt(RIG *rig, const char *cmd)
{
    char reply[APRSLORAP_LINE];
    int code = 0;
    int retval = aprslorap_transaction(rig, cmd, reply, sizeof(reply));

    if (retval != RIG_OK) { return retval; }

    if (sscanf(reply, "RPRT %d", &code) != 1) { return -RIG_EPROTO; }

    return code;
}

static int aprslorap_init(RIG *rig)
{
    ENTERFUNC;
    strncpy(RIGPORT(rig)->pathname, APRSLORAP_DEFAULT_PATH, sizeof(RIGPORT(rig)->pathname) - 1);
    STATE(rig)->current_vfo = RIG_VFO_A;
    RETURNFUNC(RIG_OK);
}

static int aprslorap_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
    char reply[APRSLORAP_LINE];
    int retval = aprslorap_transaction(rig, "f\n", reply, sizeof(reply));

    ENTERFUNC;

    if (retval != RIG_OK) { RETURNFUNC(retval); }

    *freq = 0;

    if (sscanf(reply, "%lf", freq) != 1 || *freq <= 0) { RETURNFUNC(-RIG_EPROTO); }

    RETURNFUNC(RIG_OK);
}

static int aprslorap_open(RIG *rig)
{
    freq_t freq;

    ENTERFUNC;
    /* the tracker must answer a frequency query, otherwise the port is not a tracker with CAT enabled */
    RETURNFUNC(aprslorap_get_freq(rig, RIG_VFO_A, &freq) == RIG_OK ? RIG_OK : -RIG_EPROTO);
}

static int aprslorap_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    char cmd[APRSLORAP_LINE];

    ENTERFUNC;
    SNPRINTF(cmd, sizeof(cmd), "F %.0f\n", freq);
    RETURNFUNC(aprslorap_rprt(rig, cmd));       /* RIG_EINVAL / -9 if outside the tuning window set by the owner */
}

static int aprslorap_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
    char reply[APRSLORAP_LINE * 2];
    char name[32];
    int bw = 0;
    int retval = aprslorap_transaction(rig, "m\n", reply, sizeof(reply));

    ENTERFUNC;

    if (retval != RIG_OK) { RETURNFUNC(retval); }

    /* "PKTFM\n<passband>\n": the second line is read separately */
    if (sscanf(reply, "%31s", name) != 1) { RETURNFUNC(-RIG_EPROTO); }

    if (!strcmp(name, "PKTFM"))       { *mode = RIG_MODE_PKTFM; }
    else if (!strcmp(name, "PKTFMN")) { *mode = RIG_MODE_PKTFMN; }
    else                              { RETURNFUNC(-RIG_EPROTO); }

    retval = read_string(RIGPORT(rig), (unsigned char *) reply, sizeof(reply), "\n", 1, 0, 1);

    if (retval <= 0) { RETURNFUNC(-RIG_ETIMEOUT); }

    reply[retval] = 0;
    sscanf(reply, "%d", &bw);
    *width = bw;
    RETURNFUNC(RIG_OK);
}

static int aprslorap_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    char cmd[APRSLORAP_LINE];
    const char *name;

    ENTERFUNC;

    if (mode == RIG_MODE_PKTFM)       { name = "PKTFM"; }
    else if (mode == RIG_MODE_PKTFMN) { name = "PKTFMN"; }
    else                              { RETURNFUNC(-RIG_EINVAL); }

    SNPRINTF(cmd, sizeof(cmd), "M %s 0\n", name);
    RETURNFUNC(aprslorap_rprt(rig, cmd));       /* accepted only if it is the mode the tracker started in */
}

static int aprslorap_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
    char reply[APRSLORAP_LINE];
    const char *cmd;
    int retval;

    ENTERFUNC;

    if (level == RIG_LEVEL_STRENGTH)      { cmd = "l STRENGTH\n"; }
    else if (level == RIG_LEVEL_RFPOWER)  { cmd = "l RFPOWER\n"; }
    else                                  { RETURNFUNC(-RIG_ENAVAIL); }

    retval = aprslorap_transaction(rig, cmd, reply, sizeof(reply));

    if (retval != RIG_OK) { RETURNFUNC(retval); }

    if (!strncmp(reply, "RPRT", 4)) { RETURNFUNC(-RIG_ENAVAIL); }

    if (level == RIG_LEVEL_STRENGTH) { val->i = atoi(reply); }
    else                             { val->f = (float) atof(reply); }

    RETURNFUNC(RIG_OK);
}

static int aprslorap_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
    char cmd[APRSLORAP_LINE];

    ENTERFUNC;

    if (level != RIG_LEVEL_RFPOWER) { RETURNFUNC(-RIG_ENAVAIL); }

    SNPRINTF(cmd, sizeof(cmd), "L RFPOWER %f\n", val.f);
    RETURNFUNC(aprslorap_rprt(rig, cmd));
}

/* PTT = the tracker sends one APRS beacon (only if the owner enabled it; otherwise the tracker answers "not available") */
static int aprslorap_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
    ENTERFUNC;
    RETURNFUNC(aprslorap_rprt(rig, ptt == RIG_PTT_OFF ? "T 0\n" : "T 1\n"));
}

static int aprslorap_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
    ENTERFUNC;
    *ptt = RIG_PTT_OFF;                          /* the beacon is a single packet, never a held transmission */
    RETURNFUNC(RIG_OK);
}

static const char *aprslorap_get_info(RIG *rig)
{
    static char info[APRSLORAP_LINE];

    if (aprslorap_transaction(rig, "_\n", info, sizeof(info)) != RIG_OK) { return NULL; }

    info[strcspn(info, "\r\n")] = 0;
    return info;
}

struct rig_caps aprslorap_caps =
{
    RIG_MODEL(RIG_MODEL_APRSLORAP),
    .model_name     = "APRS LoRa+ tracker",
    .mfg_name       = "EA7JRS",
    .version        = "20261105.0",
    .copyright      = "LGPL",
    .status         = RIG_STATUS_BETA,
    .rig_type       = RIG_TYPE_OTHER,
    .ptt_type       = RIG_PTT_RIG,
    .port_type      = RIG_PORT_NETWORK,
    .write_delay    = 0,
    .post_write_delay = 0,
    .timeout        = 2000,
    .retry          = 2,

    .has_get_level  = APRSLORAP_LEVELS,
    .has_set_level  = RIG_LEVEL_RFPOWER,
    .level_gran     = {
        [LVL_RFPOWER] = { .min = { .f = 0 }, .max = { .f = 1 }, .step = { .f = 0.01f } },
        [LVL_STRENGTH] = { .min = { .i = -60 }, .max = { .i = 60 }, .step = { .i = 1 } },
    },

    .filters        = { {APRSLORAP_MODES, RIG_FLT_ANY}, RIG_FLT_END },

    /* ISM bands used by LoRa APRS (EU 433 / 868, Americas 915); the owner's tuning window is enforced by the tracker */
    .rx_range_list1 = {
        { .startf = MHz(433), .endf = MHz(435), .modes = APRSLORAP_MODES, .low_power = -1, .high_power = -1, .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        { .startf = MHz(863), .endf = MHz(870), .modes = APRSLORAP_MODES, .low_power = -1, .high_power = -1, .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        RIG_FRNG_END,
    },
    .tx_range_list1 = {
        { .startf = MHz(433), .endf = MHz(435), .modes = APRSLORAP_MODES, .low_power = mW(1), .high_power = W(1), .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        { .startf = MHz(863), .endf = MHz(870), .modes = APRSLORAP_MODES, .low_power = mW(1), .high_power = W(1), .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        RIG_FRNG_END,
    },
    .rx_range_list2 = {
        { .startf = MHz(902), .endf = MHz(928), .modes = APRSLORAP_MODES, .low_power = -1, .high_power = -1, .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        RIG_FRNG_END,
    },
    .tx_range_list2 = {
        { .startf = MHz(902), .endf = MHz(928), .modes = APRSLORAP_MODES, .low_power = mW(1), .high_power = W(1), .vfo = RIG_VFO_A, .ant = RIG_ANT_1 },
        RIG_FRNG_END,
    },
    .tuning_steps   = { {APRSLORAP_MODES, 1}, RIG_TS_END },
    .priv           = NULL,

    .rig_init       = aprslorap_init,
    .rig_open       = aprslorap_open,
    .get_freq       = aprslorap_get_freq,
    .set_freq       = aprslorap_set_freq,
    .get_mode       = aprslorap_get_mode,
    .set_mode       = aprslorap_set_mode,
    .get_level      = aprslorap_get_level,
    .set_level      = aprslorap_set_level,
    .get_ptt        = aprslorap_get_ptt,
    .set_ptt        = aprslorap_set_ptt,
    .get_info       = aprslorap_get_info,
    .hamlib_check_rig_caps = HAMLIB_CHECK_RIG_CAPS
};
