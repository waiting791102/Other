#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define IN
#define OUT

#define ATA_PASS_THROUGH_12_SIZE 12
#define ATA_PASS_THROUGH_16_SIZE 16
#define ATA_COMMAND_DATA_SIZE 512
#define ATA_SENSE_DATA_SIZE 32
#define ATA_SENSE_ERROR_FEILD 11
#define ATA_SENSE_STATUS_FEILD 21
#define ATA_SENSE_ERROR_SUCCESS 0x00
#define ATA_SENSE_STATUS_SUCCESS 0x50
#define SECURITY_USER_PASSWORD "QNAP"
#define SECURITY_STATUS_SUPPORTED 0x01
#define SECURITY_STATUS_ENABLED 0x02
#define SECURITY_STATUS_LOCKED 0x04
#define NOMINAL_MEDIA_ROTATION_RATE_SSD 0x0001

/**
 * @struct    IDENTIFY_DEVICE
 * @brief    ATA SPEC. data content
 */
typedef struct _IDENTIFY_DEVICE
{
    unsigned short int general_configuration;
    unsigned short int reserved_1_127[127];
    unsigned short int security_status;
    unsigned short int reserved_129_216[88];
    unsigned short int nominal_media_rotation_rate;
    unsigned short int reserved_218_255[38];
} __attribute__((packed)) IDENTIFY_DEVICE;
//typedef struct _IDENTIFY_DEVICE IDENTIFY_DEVICE;

/**
 * @struct    SECURITY_SET_PASSWORD_NEW
 * @brief    ATA SPEC. data content
 */
typedef struct _SECURITY_SET_PASSWORD_NEW
{
    unsigned short int control_word;
    char new_password[32];
    unsigned short int new_master_password_identifier;
    unsigned short int reserved_18_255[238];
} __attribute__((packed)) SECURITY_SET_PASSWORD_NEW;
//typedef struct _SECURITY_SET_PASSWORD_NEW SECURITY_SET_PASSWORD_NEW;

/**
 * @struct    SECURITY_PASSWORD
 * @brief    ATA SPEC. data content
 */
typedef struct _SECURITY_PASSWORD
{
    unsigned short int control_word;
    char password[32];
    unsigned short int reserved_17_255[239];
} __attribute__((packed)) SECURITY_PASSWORD;
//typedef struct _SECURITY_PASSWORD SECURITY_PASSWORD;

static int security_set_password(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    SECURITY_SET_PASSWORD_NEW data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));
    snprintf(data.new_password, sizeof(data.new_password), SECURITY_USER_PASSWORD);

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (5 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x26; // bit 3-> T_DIR 0:transfer from application client
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xf1; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int security_erase_prepare(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
     sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (3 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x20; // bit 3-> T_DIR 0:transfer from application client
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x00; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xf3; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_NONE;
    io_hdr.dxfer_len = 0;
    io_hdr.dxferp = NULL;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int security_erase_unit(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    SECURITY_PASSWORD data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));
    snprintf(data.password, sizeof(data.password), SECURITY_USER_PASSWORD);

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (5 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x26; // bit 3-> T_DIR 0:transfer from application client
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xf4; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = 32;
    io_hdr.sbp = sense;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;   io_hdr.timeout = ((508 + 90) * 60 * 1000); // SPEC. > 508min (seconds*1000)

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int security_unlock(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    SECURITY_PASSWORD data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));
    snprintf(data.password, sizeof(data.password), SECURITY_USER_PASSWORD);

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (5 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x26; // bit 3-> T_DIR 0:transfer from application client
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xf2; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int security_disable_password(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    SECURITY_PASSWORD data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));
    snprintf(data.password, sizeof(data.password), SECURITY_USER_PASSWORD);

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (5 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x26; // bit 3-> T_DIR 0:transfer from application client
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xf6; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int identify_device(IN char *sys_name, OUT IDENTIFY_DEVICE *dev_info)
{
    int ret = 0;
    int fd = 0;
    IDENTIFY_DEVICE data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (4 << 1); // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x2e; // bit 3-> T_DIR 1:transfer from ata device
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0xec; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    *dev_info = data;
    close(fd);
    return ret;
}

static int read_log_ext(IN char *sys_name)
{
    int ret = 0;
    int fd = 0;
    IDENTIFY_DEVICE data = {0};
    unsigned char cdb[ATA_PASS_THROUGH_16_SIZE] = {0};
    unsigned char sense[ATA_SENSE_DATA_SIZE] = {0};
    sg_io_hdr_t io_hdr = {0};

    fd = open(sys_name, O_RDWR);
    if (fd < 0)
        return -1;

    // set Data
    memset(&data, 0, sizeof(data));

    // set Command Descriptor Block
    memset(&cdb, 0, sizeof(cdb));
    cdb[0] = 0x85; // 16 bytes ATA pass-through
    cdb[1] = (4 << 1)  ; // 3-> Non-data, 4-> PIO Data-IN, 5-> PIO Data-Out
    cdb[2] = 0x2e; // bit 3-> T_DIR 1:transfer from ata device
                   // bit 2-> BYT_BLOK 1:T_LENGTH is sector unit
                   // bit 1,0 -> T_LENGTH 02h:transfer length is in SECTOR_COUNT
    cdb[4] = 0x00;  // Features(0:7)
    cdb[6] = 0x01; // SECTOR_COUNT(0:7)
    cdb[7] = 0x00; // LBA_LOW(8:15)
    cdb[8] = 0x30; // LBA_LOW(0:7)
    cdb[9] = 0x00; // LBA_MID(8:15)
    cdb[10] = 0x06; // LBA_MID(0:7)
    cdb[11] = 0x00; // LBA_HIGH(8:15)
    cdb[12] = 0x00; // LBA_HIGH(0:7)
    cdb[13] = 0x40; // DEVICE
    cdb[14] = 0x2f; // COMMAND

    // set sense
    memset(&sense, 0,sizeof(sense));

    // set SG I/O Header
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = ATA_COMMAND_DATA_SIZE;
    io_hdr.dxferp = &data;
    io_hdr.cmd_len = ATA_PASS_THROUGH_16_SIZE;
    io_hdr.cmdp = cdb;
    io_hdr.mx_sb_len = ATA_SENSE_DATA_SIZE;
    io_hdr.sbp = sense;

    if ((ret = ioctl(fd, SG_IO, &io_hdr)) < 0 ||
        sense[ATA_SENSE_ERROR_FEILD] != ATA_SENSE_ERROR_SUCCESS ||
        sense[ATA_SENSE_STATUS_FEILD] != ATA_SENSE_STATUS_SUCCESS)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return ret;
}

static int pd_security_erase_data(IN char *sys_name)
{
    int ret = 0;

    if ((ret = open(sys_name, O_WRONLY)) < 0)
        goto pd_security_erase_data_end;
    else
    {
        // prepare
//        pd_wipe_out_set_conf(sys_name, 0, WIPE_OUT_PROCESS);
        if ((ret = security_set_password(sys_name)) < 0)
            goto pd_security_erase_data_end;        
        if ((ret = security_erase_prepare(sys_name)) < 0)
            goto pd_security_erase_data_end;
        if ((ret = security_erase_unit(sys_name)) < 0)
            goto pd_security_erase_data_end;
        // complete
//        pd_wipe_out_set_conf(sys_name, 100, WIPE_OUT_COMPLETED);
//        Ini_Conf_Remove_Field(OVERWRITE_CONF_FILE, sys_name, OVERWRITE_CONF_PID_KEY);
    }
pd_security_erase_data_end:
    return ret;
}

int main(int argc, char *argv[])
{
    read_log_ext(argv[1]);
    IDENTIFY_DEVICE data = {0};
    identify_device(argv[1], &data);
//  pd_security_erase_data(argv[1]);
//  security_unlock(argv[1]);
//  security_disable_password(argv[1]);
    return 0;
}

