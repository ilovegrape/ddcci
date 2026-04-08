#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <ctype.h>


#include "i2c-dev.h"

#define TAG "DDC/CI"

/* ddc/ci defines */
#define DDCCI_COMMAND_READ      0x01    /* read ctrl value */
#define DDCCI_REPLY_READ        0x02    /* read ctrl value reply */
#define DDCCI_COMMAND_WRITE     0x03    /* write ctrl value */

#define DDCCI_COMMAND_SAVE      0x0c    /* save current settings */

#define DDCCI_REPLY_CAPS        0xe3    /* get monitor caps reply */
#define DDCCI_COMMAND_CAPS      0xf3    /* get monitor caps */
#define DDCCI_COMMAND_PRESENCE  0xf7    /* ACCESS.bus presence check */

/* control numbers */
#define DDCCI_CTRL_BRIGHTNESS   0x10

/* samsung specific, magictune starts with writing 1 to this register */
#define DDCCI_CTRL              0xf5
#define DDCCI_CTRL_ENABLE       0x0001
#define DDCCI_CTRL_DISABLE      0x0000

/* ddc/ci iface tunables */
#define DEFAULT_DDCCI_ADDR      0x37    /* samsung ddc/ci logic sits at 0x37 */
#define MAX_BYTES               127     /* max message length */
#define DELAY                   50 * 1000       /* uS to wait after write */
#define RETRYS                  3       /* number of retry */

/* magic numbers */
#define MAGIC_1 0x51    /* first byte to send, host address */
#define MAGIC_2 0x80    /* second byte to send, ored with length */
#define MAGIC_XOR 0x50  /* initial xor for received frame */

static char C9High[8];
static char Scalar[8];
static char SIcode[8];
static char FWStage_H[8];
static char FWStage_L[8];

static char *model_result = "DUMMY_MODEL";
static char *serial_result = "DUMMY_SERIAL";

static char *
getScalar(unsigned int C8_value)
{
    unsigned int code = C8_value & 0x000000FF;
    switch (code) {
        case 0x0d: Scalar[0] = '1'; break;
        case 0x05: Scalar[0] = '2'; break;
        case 0x09: Scalar[0] = '3'; break;
        case 0x12: Scalar[0] = '4'; break;
        default: Scalar[0] = '\0';
    }
    return Scalar;
}

static char *
getSICode(unsigned int FD_value)
{
    unsigned int code = FD_value & 0x000000FF;
    code -= 0x20;
    snprintf(SIcode, sizeof(SIcode) - 1, "%c", code);
    SIcode[1] = '\0';
    return SIcode;
}

static char * 
getFWStage_H(unsigned int C9_value)
{
    FWStage_H[0] = '\0';
    int code = C9_value >> 8;
    char tmp[8] = "";
    snprintf(tmp, sizeof(tmp) - 1, "%02X", code);
    C9High[0] = tmp[1];
    switch (code & 0x70) {
        case 0x10:
            FWStage_H[0] = '1';
            break;
        case 0x20:
            FWStage_H[0] = '2';
            break;
        case 0x30:
            FWStage_H[0] = '3';
            break;
        case 0x40:
            FWStage_H[0] = 'M';
            break;
        default:
            FWStage_H[0] = '\0';
            break;
    }
    return FWStage_H;
}


bool
isBCD(char* model)
{
    int non_digit_count = 0;
    char tmp[16];
    int value;

    if (strcmp(model, "U3423WE") == 0) {
        return true;
    }

    for (int i = 0; i < (int)strlen(model); ++i) {
        if (model[i] <= '/' || model[i] > '9') {
            non_digit_count++;
        }
    }

    memcpy(tmp, model + non_digit_count + 2, 2);
    tmp[2] = '\0';
    value = atoi(tmp);
    return value > 0x17;
}

static char *
getFWStage_L(unsigned int C9_value, unsigned int FD_value)
{
    unsigned int c9 = C9_value & 0x000000FF;
    unsigned int fd = FD_value & 0x000000FF;

    if (fd == 0x62 && isBCD(model_result) == false) {
        snprintf(FWStage_L, sizeof(FWStage_L) - 1, "%02d", c9);
    } else {
        snprintf(FWStage_L, sizeof(FWStage_L) - 1, "%02X", c9);
    }
    return FWStage_L;
}

/* debugging */
void
dumphex(unsigned char *buf, unsigned char len)
{
    int i, j;
    char msg[128];
    int offset = 0;
    for (j = 0; j < len; j +=16) {
        offset = 0;
        if (len > 16) {
            offset += snprintf(msg + offset, sizeof(msg) - offset, "%04x: ", j);
        }

        for (i = 0; i < 16; i++) {
            if (i + j < len)
                offset += snprintf(msg + offset, sizeof(msg) - offset, "%02x ", buf[i + j]);
            else
                offset += snprintf(msg + offset, sizeof(msg) - offset, "   ");
        }

        offset += snprintf(msg + offset, sizeof(msg) - offset, "| ");

        for (i = 0; i < 16; i++) {
            if (i + j < len)
                offset += snprintf(msg + offset, sizeof(msg) - offset, "%c",
                    buf[i + j] >= ' ' && buf[i + j] < 127 ? buf[i + j] : '.');
            else
                offset += snprintf(msg + offset, sizeof(msg) - offset, " ");
        }

        offset += snprintf(msg + offset, sizeof(msg) - offset, "\n");
        msg[offset] = '\0';
        printf("%s", msg);
    }
}

/* write len bytes (stored in buf) to i2c address addr */
/* return 0 on success, -1 on failure */
int i2c_write(int fd, unsigned int addr, unsigned char *buf, unsigned char len)
{
    int i;
    struct i2c_rdwr_ioctl_data msg_rdwr;
    struct i2c_msg             i2cmsg;

    /* done, prepare message */
    msg_rdwr.msgs = &i2cmsg;
    msg_rdwr.nmsgs = 1;

    i2cmsg.addr  = addr;
    i2cmsg.flags = 0;
    i2cmsg.len   = len;
    i2cmsg.buf   = buf;

    if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0 )
    {
        printf("i2c_write: ioctl returned %d, ioctl() error %s\n", i, strerror(errno));
        return -1;
    }

    return i;
}

/* read at most len bytes from i2c address addr, to buf */
/* return -1 on failure */
int i2c_read(int fd, unsigned int addr, unsigned char *buf, unsigned char len)
{
    struct i2c_rdwr_ioctl_data msg_rdwr;
    struct i2c_msg             i2cmsg;
    int i;

    msg_rdwr.msgs = &i2cmsg;
    msg_rdwr.nmsgs = 1;

    i2cmsg.addr  = addr;
    i2cmsg.flags = I2C_M_RD;
    i2cmsg.len   = len;
    i2cmsg.buf   = buf;

    if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0)
    {
        printf("i2c_read:ioctl returned %d ioctl() error: %s\n", i, strerror(errno));
        return -1;
    }

    return i;
}


/* write len bytes (stored in buf) to ddc/ci at address addr */
/* return 0 on success, -1 on failure */
int ddcci_write(int fd, unsigned int addr, unsigned char *buf, unsigned char len)
{
    int i = 0;
    unsigned char _buf[MAX_BYTES + 3];
    unsigned xor = ((unsigned char)addr << 1);      /* initial xor value */

    // fprintf(stderr,"Send: "); dumphex(buf, len);

    /* put first magic */
    xor ^= (_buf[i++] = MAGIC_1);

    /* second magic includes message size */
    xor ^= (_buf[i++] = MAGIC_2 | len);

    while (len--) /* bytes to send */
        xor ^= (_buf[i++] = *buf++);

    /* finally put checksum */
    _buf[i++] = xor;

    return i2c_write(fd, addr, _buf, i);
}

/* read ddc/ci formatted frame from ddc/ci at address addr, to buf */
int ddcci_read(int fd, unsigned int addr, unsigned char *buf, unsigned char len)
{
    unsigned char _buf[MAX_BYTES];
    unsigned char xor = MAGIC_XOR;
    int i, _len;

    if (i2c_read(fd, addr, _buf, len + 3) <= 0 ||
        _buf[0] == 0x51 || _buf[0] == 0xff) // busy ???
    {
        return -1;
    }

    /* validate answer */
    if (_buf[0] != addr * 2) {
        //dumphex(_buf, sizeof(_buf));
        printf("Invalid response, first byte is 0x%02x, should be 0x%02x\n",
                _buf[0], addr * 2);
        return -1;
    }

    if ((_buf[1] & MAGIC_2) == 0) {
        printf("Invalid response, magic is 0x%02x\n", _buf[1]);
        return -1;
    }

    _len = _buf[1] & ~MAGIC_2;
    if (_len > len || _len > sizeof(_buf)) {
        printf("Invalid response, length is %d, should be %d at most\n",
                _len, len);
        return -1;
    }

    /* get the xor value */
    for (i = 0; i < _len + 3; i++) {
        xor ^= _buf[i];
    }

    if (xor != 0) {
        printf("Invalid response, corrupted data - xor is 0x%02x, length 0x%02x\n", xor, _len);
        dumphex(_buf, _len + 3);
        return -1;
    }

    /* copy payload data */
    memcpy(buf, _buf + 2, _len);

    //fprintf(stderr,"Recv: "); dumphex(buf, _len);

    return _len;
}

/* read register ctrl raw data of ddc/ci at address addr */
int ddcci_readctrl(int fd, unsigned int addr, unsigned char ctrl, unsigned char *buf, unsigned char len)
{
    unsigned char _buf[2];

    _buf[0] = DDCCI_COMMAND_READ;
    _buf[1] = ctrl;

    if (ddcci_write(fd, addr, _buf, sizeof(_buf)) < 0)
    {
        return -1;
    }

    usleep(DELAY);
    return ddcci_read(fd, addr, buf, len);
}


static int 
ddcci_dumpctrl(int fd, unsigned int addr, unsigned char ctrl, int force, int* value) 
{
    unsigned char buf[8];
    *value = 0;
    int len = ddcci_readctrl(fd, addr, ctrl, buf, sizeof(buf));

    if (len == sizeof(buf) && buf[0] == DDCCI_REPLY_READ &&
        buf[2] == ctrl && (force || !buf[1])) /* buf[1] is validity (0 - valid, 1 - invalid) */
    {
        int current = buf[6] * 256 + buf[7];
        int maximum = buf[4] * 256 + buf[5];

        printf("Control 0x%02x: %c/%d/%d\n", ctrl,
                buf[1] ? '-' : '+',  current, maximum);
        *value = current;
    }

    return len;
}


/* save current settings */
static int 
ddcci_command(int fd, unsigned int addr, unsigned char cmd)
{
    unsigned char _buf[1];
    _buf[0] = cmd;
    return ddcci_write(fd, addr, _buf, sizeof(_buf));
}

static char *
getTagValue(unsigned char tag, unsigned char* buf, int len)
{
    int i;
    char *value = "N/A";
    /*
    EDID data example
    0000: 00 ff ff ff ff ff ff 00 10 ac bd 42 4c 45 5a 41 | ...........BLEZA
    0010: 1e 22 01 04 a5 35 1e 78 3a 56 25 ab 53 4f 9d 25 | ."...5.x:V%.SO.%
    0020: 10 50 54 a5 4b 00 71 4f 81 80 a9 c0 d1 c0 01 01 | .PT.K.qO........
    0030: 01 01 01 01 01 01 02 3a 80 18 71 38 2d 40 58 2c | .......:..q8-@X,
    0040: 45 00 0f 28 21 00 00 1e 00 00 00 ff 00 35 4b 38 | E..(!........5K8
    0050: 43 48 33 34 0a 20 20 20 20 20 00 00 00 fc 00 44 | CH34.     .....D
    0060: 45 4c 4c 20 50 32 34 32 34 48 45 42 00 00 00 fd | ELL P2424HEB....
    0070: 00 30 4b 1e 5a 13 01 0a 20 20 20 20 20 20 01 cc | .0K.Z...      ..
    */

    /*
    See EDID spec Table 3.23 - Display Descriptor Summary
    The remaining three 18-byte descriptors (at addresses 48h → 59h, 5Ah → 6Bh and 6Ch → 7Dh)

    offset  count
    0       2 (00 00)h Indicates that this 18 byte descriptor is a Display Descriptor.
    2       1 00h Reserved: Set to 00h when 18 byte descriptor is used as a Display Descriptor
    3       1 FFh Display Product Serial Number
              FCh Display Product Name
    4       1 00h Reserved: Set to 00h when 18 byte descriptor is used as a Display Descriptor
    5 - 17    Stored data dependant on Display Descriptor Definition
    */

    for (i = 0x48; i < len - 18; i += 18) {
        if (buf[i + 0] == 0x00 && buf[i + 1] == 0x00 && buf[i + 3] == tag) {
            value = buf + i;
            break;
        }
    }

    if (i > len - 18)
        return value;

    i = 0; // handle the current 18 bytes descriptor
    while (value[i] != 0x0A && i < 18)
        i++;

    value[i] = '\0';
    return value + 5; // skip first 5 bytes
}

static char *
getSerial(unsigned char* buf, int len)
{
    // FFh Display Product Serial Number
    return getTagValue(0xFF, buf, len);
}

static char*
getModel(unsigned char* buf, int len)
{
    // FCh Display Product Name
    return getTagValue(0xFC, buf, len);
}

static bool
isDellDevice(unsigned char* buf)
{
    unsigned char id[4];
    id[0] = ((buf[8] >> 2) & 31) + 'A' - 1;
    id[1] = ((buf[8] & 3) << 3) + (buf[9] >> 5) + 'A' - 1;
    id[2] = (buf[9] & 31) + 'A' - 1;

    printf("isDellDevice: %c%c%c\n", id[0], id[1], id[2]);
    // "DEL"
    return (id[0] == 'D' && id[1] == 'E' && id[2] == 'L');
}

bool getMonitorInfo(char *path, char* model, char* serial, char* version)
{
    unsigned int C8_value = 4361, FD_value = 99, C9_value = 256;
    char *pModel = NULL;
    bool is_dell = false;
    unsigned int ddc = 0x37;
    unsigned int edid = 0x50;
    unsigned char ctrls[] = { 0xC8, 0xC9, 0xFD };
    unsigned char buf[128];
    unsigned int * values[] = { &C8_value, &C9_value, &FD_value };
    bool ret =  true;

    if (path == NULL || model == NULL || serial == NULL || version == NULL) {
        return false;
    }

    snprintf(model, 64 - 1, "N/A");
    model[63] = '\0';
    snprintf(serial, 64 - 1, "N/A");
    serial[63] = '\0';
    snprintf(version, 64 - 1, "N/A");
    version[63] = '\0';

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("failed to open device %s\n", path);
        return false;
    }

    memset(buf, 0, sizeof(buf));
    if (i2c_write(fd, edid, buf, 1) > 0 &&
        i2c_read(fd, edid, buf, sizeof(buf)) > 0)
    {
        dumphex(buf, sizeof(buf));
    } else {
        printf("Reading EDID 0x%02x@%s failed.\n", edid, path);
        ret = false;
        goto end;
    }

    serial_result = getSerial(buf, sizeof(buf));
    model_result =  getModel(buf, sizeof(buf));
    is_dell = isDellDevice(buf);

    if (is_dell) {
        if (ddcci_command(fd, ddc, DDCCI_COMMAND_PRESENCE) < 0) {
            printf("DDC/CI at 0x%02x@%s is unusable.\n", ddc, path);
            goto finalize;
        }

        usleep(DELAY);
        for (int i = 0; i < 3 ; i++)
        {
            int len;
            int force = 0;
            for (int retry = RETRYS; retry &&
                    (len = (ddcci_dumpctrl(fd, ddc, ctrls[i], force, values[i])) < 0); retry--)
                usleep(DELAY);
            //printf("0x%02X: %d (len = %d)\n\n", ctrls[i], *values[i], len);
        }
        printf("Fetched value: C8_value = %d, C9_value = %d, FD_value = %d\n\n",
                C8_value, C9_value, FD_value);

        if (C8_value || C9_value || FD_value) {
            getScalar(C8_value);
            getSICode(FD_value);
            getFWStage_H(C9_value);
            getFWStage_L(C9_value, FD_value);

            printf("getScalar(C8_value = %d) = \"%s\"\n", C8_value, Scalar);
            printf("getSICode(FD_value = %d) = \"%s\"\n", FD_value, SIcode);
            printf("getFWStage_H(C9_value = %d) = \"%s\"\n", C9_value, FWStage_H);
            printf("   C9High = \"%s\"\n", C9High);
            printf("getFWStage_L(C9_value = %d, FD_value = %d) = \"%s\"\n", C9_value, FD_value, FWStage_L);

            snprintf(version, 64 - 1, "%s%s%s%s%s", FWStage_H, Scalar, SIcode, C9High, FWStage_L);
            version[63] = '\0';
        } else {
            printf("failed to get ctrl values\n");
        }
    } else {
        printf("Not a Dell monitor, EDID info only.\n");
    }

finalize:
    printf("Model = %s\n", model_result);
    printf("Serial = %s\n", serial_result);
    printf("Firmware Version = %s\n\n", version);

    if (is_dell) {
        pModel = strrchr(model_result, ' '); // remove prefix 'DELL '
        pModel = pModel ? pModel + 1 : model_result;
    } else {
        pModel = model_result;
    }
    strncpy(model, pModel, 64 - 1);
    model[63] = '\0';
    strncpy(serial, serial_result, 64 - 1);
    serial[63] = '\0';
end:
    close(fd);
    return ret;
}

int main(int argc, char** argv)
{
    char device[512];
    char model[64], serial[64], version[64];
    FILE *fp;

    fp = popen("ls /dev/iic* /dev/i2c* 2>/dev/null", "r");
    if (!fp) {
        printf("popen() error %s\n", strerror(errno));
        return -1;
    }

    while (fgets(device, sizeof(device), fp)) {
        char *p = strrchr(device, '\n');
        if (p) *p = '\0';

        if (getMonitorInfo(device, model, serial, version)) {
            printf("%s: Model = %s, Serial = %s, Firmware Version = %s\n", device, model, serial, version);
        } else {
            printf("Failed to get monitor info from %s\n", device);
        }
    }
    pclose(fp);
    return 0;
}
