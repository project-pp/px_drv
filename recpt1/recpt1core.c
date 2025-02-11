#include <stdio.h>
#include <stdlib.h>
#include "recpt1core.h"
#include "version.h"
#include "pt1_dev.h"

#define ISDB_T_NODE_LIMIT 24 // 32:ARIB limit 24:program maximum
#define ISDB_T_SLOT_LIMIT 8

// globals
boolean f_exit = FALSE;
char  bs_channel_buf[20];
ISDB_T_FREQ_CONV_TABLE isdb_t_conv_set = { 0, CHTYPE_SATELLITE, 0, bs_channel_buf };

// lookup frequency conversion table
ISDB_T_FREQ_CONV_TABLE* searchrecoff(char* channel)
{
    int lp;

    if (channel[0] == 'B' && channel[1] == 'S')
    {
        int node = 0;
        int slot = 0;
        char* bs_ch;
        bs_ch = channel + 2;

        while (isdigit(*bs_ch))
        {
            node *= 10;
            node += *bs_ch++ - '0';
        }

        if (*bs_ch == '_' && (node & 0x01) && node < ISDB_T_NODE_LIMIT)
        {
            if (isdigit(*++bs_ch))
            {
                slot = *bs_ch - '0';

                if (*++bs_ch == '\0' && slot < ISDB_T_SLOT_LIMIT)
                {
                    isdb_t_conv_set.set_freq = node / 2;
                    isdb_t_conv_set.add_freq = slot;
                    sprintf(bs_channel_buf, "BS%d_%d", node, slot);
                    return &isdb_t_conv_set;
                }
            }
        }

        return NULL;
    }

    for (lp = 0; isdb_t_conv_table[lp].parm_freq != NULL; lp++)
    {
        // return entry number in the table when strings match and lengths are same.
        if ((memcmp(isdb_t_conv_table[lp].parm_freq, channel, strlen(channel)) == 0)
            && (strlen(channel) == strlen(isdb_t_conv_table[lp].parm_freq)))
        {
            return &isdb_t_conv_table[lp];
        }
    }

    return NULL;
}

int close_tuner(thread_data* tdata)
{
    int rv = 0;

    if (tdata->tfd == -1)
    {
        return rv;
    }

    if (tdata->table->type == CHTYPE_SATELLITE)
    {
        if (ioctl(tdata->tfd, LNB_DISABLE, 0) < 0)
        {
            rv = 1;
        }
    }

    close(tdata->tfd);
    tdata->tfd = -1;
    return rv;
}

float getsignal_isdb_s(int signal)
{
    // apply linear interpolation
    static const float afLevelTable[] =
    {
        24.07f,    // 00    00    0        24.07dB
        24.07f,    // 10    00    4096     24.07dB
        18.61f,    // 20    00    8192     18.61dB
        15.21f,    // 30    00    12288    15.21dB
        12.50f,    // 40    00    16384    12.50dB
        10.19f,    // 50    00    20480    10.19dB
        8.140f,    // 60    00    24576    8.140dB
        6.270f,    // 70    00    28672    6.270dB
        4.550f,    // 80    00    32768    4.550dB
        3.730f,    // 88    00    34816    3.730dB
        3.630f,    // 88    FF    35071    3.630dB
        2.940f,    // 90    00    36864    2.940dB
        1.420f,    // A0    00    40960    1.420dB
        0.000f     // B0    00    45056    -0.01dB
    };
    unsigned char sigbuf[4];
    memset(sigbuf, '\0', sizeof(sigbuf));
    sigbuf[0] = (((signal & 0xFF00) >> 8) & 0XFF);
    sigbuf[1] = (signal & 0xFF);

    // calculate signal level
    if (sigbuf[0] <= 0x10U)
    {
        // clipped maximum
        return 24.07f;
    }
    else if (sigbuf[0] >= 0xB0U)
    {
        // clipped minimum
        return 0.0f;
    }
    else
    {
        // linear interpolation
        const float fMixRate = (float)(((unsigned short)(sigbuf[0] & 0x0FU) << 8)
            | (unsigned short)sigbuf[0]) / 4096.0f;
        return afLevelTable[sigbuf[0] >> 4] * (1.0f - fMixRate)
            + afLevelTable[(sigbuf[0] >> 4) + 0x01U] * fMixRate;
    }
}

void calc_cn(int fd, int type, boolean use_bell)
{
    int rc;
    int bell = 0;
    double P;
    double CNR;

    if (ioctl(fd, GET_SIGNAL_STRENGTH, &rc) < 0)
    {
        fprintf(stderr, "Tuner Select Error\n");
        return;
    }

    if (type == CHTYPE_GROUND)
    {
        P = log10(5505024 / (double)rc) * 10;
        CNR = (0.000024 * P * P * P * P) - (0.0016 * P * P * P)
            + (0.0398 * P * P) + (0.5491 * P) + 3.0965;
    }
    else
    {
        CNR = getsignal_isdb_s(rc);
    }

    if (use_bell)
    {
        if (CNR >= 30.0)
        {
            bell = 3;
        }
        else if (CNR >= 15.0 && CNR < 30.0)
        {
            bell = 2;
        }
        else if (CNR < 15.0)
        {
            bell = 1;
        }

        fprintf(stderr, "\rC/N = %fdB ", CNR);
        do_bell(bell);
    }
    else
    {
        fprintf(stderr, "\rC/N = %fdB", CNR);
    }
}

void show_channels(void)
{
    FILE* f;
    char* home;
    char buf[255];
    char filename[255];
    fprintf(stderr, "Available Channels:\n");
    home = getenv("HOME");
    sprintf(filename, "%s/.recpt1-channels", home);
    f = fopen(filename, "r");

    if (f)
    {
        while (fgets(buf, 255, f))
        {
            fprintf(stderr, "%s", buf);
        }

        fclose(f);
    }
    else
    {
        fprintf(stderr, "13-62: Terrestrial Channels\n");
    }

    fprintf(stderr, "101ch: NHK BS1\n");
    fprintf(stderr, "103ch: NHK BS Premium\n");
    fprintf(stderr, "141ch: BS Nittele\n");
    fprintf(stderr, "151ch: BS Asahi\n");
    fprintf(stderr, "161ch: BS-TBS\n");
    fprintf(stderr, "171ch: BS Japan\n");
    fprintf(stderr, "181ch: BS Fuji\n");
    fprintf(stderr, "191ch: WOWOW Prime\n");
    fprintf(stderr, "192ch: WOWOW Live\n");
    fprintf(stderr, "193ch: WOWOW Cinema\n");
    fprintf(stderr, "200ch: Star Channel1\n");
    fprintf(stderr, "201ch: Star Channel2\n");
    fprintf(stderr, "202ch: Star Channel3\n");
    fprintf(stderr, "211ch: BS11 Digital\n");
    fprintf(stderr, "222ch: TwellV\n");
    fprintf(stderr, "231ch: Housou Daigaku 1\n");
    fprintf(stderr, "232ch: Housou Daigaku 2\n");
    fprintf(stderr, "233ch: Housou Daigaku 3\n");
    fprintf(stderr, "234ch: Green Channel\n");
    fprintf(stderr, "236ch: BS Animax\n");
    fprintf(stderr, "238ch: FOX bs238\n");
    fprintf(stderr, "241ch: BS SkyPer!\n");
    fprintf(stderr, "242ch: J SPORTS 1\n");
    fprintf(stderr, "243ch: J SPORTS 2\n");
    fprintf(stderr, "244ch: J SPORTS 3\n");
    fprintf(stderr, "245ch: J SPORTS 4\n");
    fprintf(stderr, "251ch: BS Tsuri Vision\n");
    fprintf(stderr, "252ch: IMAGICA BS\n");
    fprintf(stderr, "255ch: Nihon Eiga Senmon Channel\n");
    fprintf(stderr, "256ch: Disney Channel\n");
    fprintf(stderr, "258ch: Dlife\n");
    fprintf(stderr, "C13-C63: CATV Channels\n");
    fprintf(stderr, "CS2-CS24: CS Channels\n");
}

int parse_time(char* rectimestr, int* recsec)
{
    // indefinite
    if (!strcmp("-", rectimestr))
    {
        *recsec = -1;
        return 0;
    }
    // colon
    else if (strchr(rectimestr, ':'))
    {
        int n1, n2, n3;

        if (sscanf(rectimestr, "%d:%d:%d", &n1, &n2, &n3) == 3)
        {
            *recsec = n1 * 3600 + n2 * 60 + n3;
        }
        else if (sscanf(rectimestr, "%d:%d", &n1, &n2) == 2)
        {
            *recsec = n1 * 3600 + n2 * 60;
        }
        else
        {
            // unsuccessful
            return 1;
        }

        return 0;
    }
    // HMS
    else
    {
        char* tmpstr;
        char* p1;
        char* p2;
        int  flag;

        if (*rectimestr == '-')
        {
            rectimestr++;
            flag = 1;
        }
        else
        {
            flag = 0;
        }

        tmpstr = strdup(rectimestr);
        p1 = tmpstr;

        while (*p1 && !isdigit(*p1))
        {
            p1++;
        }

        // hour
        if ((p2 = strchr(p1, 'H')) || (p2 = strchr(p1, 'h')))
        {
            *p2 = '\0';
            *recsec += atoi(p1) * 3600;
            p1 = p2 + 1;

            while (*p1 && !isdigit(*p1))
            {
                p1++;
            }
        }

        // minute
        if ((p2 = strchr(p1, 'M')) || (p2 = strchr(p1, 'm')))
        {
            *p2 = '\0';
            *recsec += atoi(p1) * 60;
            p1 = p2 + 1;

            while (*p1 && !isdigit(*p1))
            {
                p1++;
            }
        }

        // second
        *recsec += atoi(p1);

        if (flag)
        {
            *recsec *= -1;
        }

        free(tmpstr);
        return 0;
    }

    // unsuccessful
    return 1;
}

void do_bell(int bell)
{
    int i;

    for (i = 0; i < bell; i++)
    {
        fprintf(stderr, "\a");
        usleep(400000);
    }
}

// from checksignal.c
int tune(char* channel, thread_data* tdata, char* device)
{
    char** tuner;
    int num_devs;
    int lp;
    FREQUENCY freq;
    unsigned char EncAPKey[16];

    // Just use a dummy key to test
    unsigned char EncPCKey[16] =
    {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 16
    };
    // use PLEX key1[0]
    unsigned char EncAPKey1[16] =
    {
        0x8b, 0x59, 0x82, 0xe7,
        0x98, 0xdc, 0x40, 0xef,
        0x8e, 0x43, 0x21, 0x6f,
        0xeb, 0x92, 0x80, 0x8c
    };
    // use PLEX key2[0]
    unsigned char EncAPKey2[16] =
    {
        0xf0, 0xf1, 0x33, 0x84,
        0xa1, 0x1d, 0x46, 0x25,
        0x95, 0x1a, 0xce, 0x09,
        0xdd, 0x86, 0x78, 0xa4
    };

    // get channel
    tdata->table = searchrecoff(channel);

    if (tdata->table == NULL)
    {
        fprintf(stderr, "Invalid Channel: %s\n", channel);
        return 1;
    }

    freq.frequencyno = tdata->table->set_freq;
    freq.slot = tdata->table->add_freq;

    // open tuner
    // case 1: specified tuner device
    if (device)
    {
        tdata->tfd = open(device, O_RDONLY);

        if (tdata->tfd < 0)
        {
            fprintf(stderr, "Cannot open tuner device: %s\n", device);
            return 1;
        }

#ifdef ASV5220_USE_APKEY1
        memcpy(EncAPKey, EncAPKey1, 16);
        DTV_SetEncrypKey(EncAPKey, 16, EncPCKey, 16, tdata->tfd);
#else
        memcpy(EncAPKey, EncAPKey2, 16);
        DTV_SetEncrypKey(EncAPKey, 16, EncPCKey, 16, tdata->tfd);
#endif

        // power on LNB
        if (tdata->table->type == CHTYPE_SATELLITE)
        {
            if (ioctl(tdata->tfd, LNB_ENABLE, tdata->lnb) < 0)
            {
                fprintf(stderr, "Power on LNB failed: %s\n", device);
            }
        }

        // tune to specified channel
        while (ioctl(tdata->tfd, SET_CHANNEL, &freq) < 0)
        {
            if (tdata->tune_persistent)
            {
                if (f_exit)
                {
                    close_tuner(tdata);
                    return 1;
                }

                fprintf(stderr, "No signal. Still trying: %s\n", device);
            }
            else
            {
                close(tdata->tfd);
                fprintf(stderr, "Cannot tune to the specified channel: %s\n", device);
                return 1;
            }
        }

        fprintf(stderr, "device = %s\n", device);
    }
    else
    {
        // case 2: loop around available devices
        if (tdata->table->type == CHTYPE_SATELLITE)
        {
            tuner = bsdev;
            num_devs = NUM_BSDEV;
        }
        else
        {
            tuner = isdb_t_dev;
            num_devs = NUM_ISDB_T_DEV;
        }

        for (lp = 0; lp < num_devs; lp++)
        {
            int count = 0;
            tdata->tfd = open(tuner[lp], O_RDONLY);

            if (tdata->tfd >= 0)
            {
#ifdef ASV5220_USE_APKEY1
                memcpy(EncAPKey, EncAPKey1, 16);
                DTV_SetEncrypKey(EncAPKey, 16, EncPCKey, 16, tdata->tfd);
#else
                memcpy(EncAPKey, EncAPKey2, 16);
                DTV_SetEncrypKey(EncAPKey, 16, EncPCKey, 16, tdata->tfd);
#endif

                // power on LNB
                if (tdata->table->type == CHTYPE_SATELLITE)
                {
                    if (ioctl(tdata->tfd, LNB_ENABLE, tdata->lnb) < 0)
                    {
                        fprintf(stderr, "Warning: Power on LNB failed: %s\n", tuner[lp]);
                    }
                }

                // tune to specified channel
                if (tdata->tune_persistent)
                {
                    while (ioctl(tdata->tfd, SET_CHANNEL, &freq) < 0 && count < MAX_RETRY)
                    {
                        if (f_exit)
                        {
                            close_tuner(tdata);
                            return 1;
                        }

                        fprintf(stderr, "No signal. Still trying: %s\n", tuner[lp]);
                        count++;
                    }

                    if (count >= MAX_RETRY)
                    {
                        close_tuner(tdata);
                        count = 0;
                        continue;
                    }
                }
                else
                {
                    if (ioctl(tdata->tfd, SET_CHANNEL, &freq) < 0)
                    {
                        close(tdata->tfd);
                        tdata->tfd = -1;
                        continue;
                    }
                }

                if (tdata->tune_persistent)
                {
                    fprintf(stderr, "device = %s\n", tuner[lp]);
                }

                // found suitable tuner
                break;
            }
        }

        // all tuners cannot be used
        if (tdata->tfd < 0)
        {
            fprintf(stderr, "Cannot tune to the specified channel\n");
            return 1;
        }
    }

    if (!tdata->tune_persistent)
    {
        // show signal strength
        calc_cn(tdata->tfd, tdata->table->type, FALSE);
    }

    // success
    return 0;
}
