#define VERSION "0.1.0"
#define COPYRIGHT "Copyright (C) 2023 Tokuyama Coooration, Easy Mejor Inc., and toor Inc. All rights reserved."

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <yaml.h>
#include <sndfile.h>
#include <time.h>
#include <math.h>
#include "debug.h"

#define BUF_SIZE 1024
#define NUM_BLOCKS 5
#define NUM_CHANNELS 4
#define NUM_SENSORS 20
#define DATA_SIZE 1026
#define NUM_DATA_PER_PACKET 128 // 128 data per packet
#define EPSILON 1.0e-9

// Sensor data structure
typedef struct {
    char *label;
    char *block;
    char *channel;
    int gain;
} Sensor;

// Config data structure
typedef struct {
    char *afe_ip;
    int afe_port;
    Sensor sensors[NUM_SENSORS];
    int sampling_rate;
} Config;

// map: block data <-> send data
typedef struct {
    char *block;
    uint8_t data;
} BlockData;
const BlockData block_data_map[] = {
    {"A", 0x01},
    {"B", 0x02},
    {"C", 0x03},
    {"D", 0x04},
    {"E", 0x05},
};

// map: gain <-> send data
typedef struct {
    int gain;
    uint8_t data;
} GainData;
const GainData gain_data_map[] = {
    {0, 0x00},
    {1, 0x01},
    {2, 0x02},
    {5, 0x03},
    {10, 0x04},
    {20, 0x05},
    {50, 0x06},
    {100, 0x07},
};

void error_handling(char *message);
void read_config(const char *filename, Config *config);
void getdata(int socket, Config *config, double duration, const char *block_to_record, const char *sensor_to_record);
int send_start_command_of_block(int sock, struct sockaddr_in *serv_addr, Config *config, int block_count);
int send_stop_command_of_block(int sock, struct sockaddr_in *serv_addr);


void usage() {
    DEBUG_PRINT("Usage: emgetdata [-f config_file] [-t duration] [-s sensor]\n");
    DEBUG_PRINT("  -f config_file: config file path. default: config.yml\n");
    DEBUG_PRINT("  -t duration: duration in sec. default: 10 sec.\n");
    DEBUG_PRINT("  -s sensor: specify a sensor label to record. otherwise, all sensors are recorded.\n");
    DEBUG_PRINT("  -h: show this help\n");
    DEBUG_PRINT("  -v: show version\n");
    DEBUG_PRINT("%s\n", COPYRIGHT);
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in serv_addr;

    // 引数処理
    // -f config_file: configファイル指定
    // -t: duration in sec.
    // -s: specify a sensor label to record. otherwise, all sensors are recorded.
    // -h: show this help
    // -v: show version
    Config config;
    const char *config_filename = "config.yml";
    int opt;
    double duration = 10.0; // default: 10 sec.
    const char *sensor_to_record = "";
    while ((opt = getopt(argc, argv, "f:t:s:hv")) != -1) {
        switch (opt) {
            case 'f':
                config_filename = optarg;
                break;
            case 't':
                if (optarg != NULL) {
                    duration = atof(optarg);
                    DEBUG_PRINT("duration: %f\n", duration);
                } else {
                    printf("Error: Duration argument is missing or invalid.\n");
                    exit(1);
                }
                break;
            case 's':
                sensor_to_record = optarg;
                break;
            case 'h':
                usage();
                exit(0);
            case 'v':
                DEBUG_PRINT("emgetdata version %s\n", VERSION);
                exit(0);
            default:
                usage();
                exit(1);
        }
    }
    read_config(config_filename, &config);

    // 引数で特定のセンサーが指定された場合、configファイルに当該センサーの定義があるかどうかを確認する
    if (strcmp(sensor_to_record, "") != 0) {
        int found = 0;
        for (int j = 0; j < NUM_SENSORS; j++) {
            if (strcmp(config.sensors[j].label, sensor_to_record) == 0) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            // センサーが見つからない場合は終了
            error_handling("Sensor not found in config file.");
        }
    }   

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        error_handling("socket");
    
    // socket通信のtimeout時間を5秒に設定
    struct timeval timeout;
    timeout.tv_sec = 5; // 5秒
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(config.afe_ip);
    serv_addr.sin_port = htons(config.afe_port);

    DEBUG_PRINT("AFE IP: %s\n", config.afe_ip);
    DEBUG_PRINT("AFE Port: %d\n", config.afe_port);
    // block毎にデータを取得
    for (int block_count = 0; block_count < NUM_BLOCKS; block_count++) {
        // 特定のセンサーのみ記録する場合は、このブロックに当該センサーラベルがあるかチェック
        if (strcmp(sensor_to_record, "") != 0) {
            int found = 0;
            for (int j = 0; j < NUM_SENSORS; j++) {
                if ((strcmp(config.sensors[j].block, block_data_map[block_count].block) == 0) && (strcmp(config.sensors[j].label, sensor_to_record)) == 0) {
                    found = 1;
                    break;
                }
            }
            if (found == 0) {
                continue;
            }
        }   
        DEBUG_PRINT("block: %s\n", block_data_map[block_count].block);

        if (send_start_command_of_block(sock, &serv_addr, &config, block_count)) {
            // Process received data and save to WAV files
            // wait 100ms
            //usleep(100000);
            DEBUG_PRINT("Start recording for block %s...\n", block_data_map[block_count].block);
            getdata(sock, &config, duration, block_data_map[block_count].block, sensor_to_record);
            DEBUG_PRINT("done\n");
        }

        send_stop_command_of_block(sock, &serv_addr);
    } // end of for (int block_count = 0; block_count < NUM_BLOCKS; block_count++)

    close(sock);
    return 0;
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void read_config(const char *filename, Config *config) {
    FILE *file = fopen(filename, "r");
    yaml_parser_t parser;
    yaml_event_t event;
    int done = 0;
    int seq_level = 0;
    int sensor_index = 0;

    if (!file) {
        DEBUG_PRINT("Failed to open config file: %s\n", filename);
        exit(1);
    }

    if (!yaml_parser_initialize(&parser)) {
        DEBUG_PRINT("Failed to initialize the YAML parser\n");
        exit(1);
    }

    yaml_parser_set_input_file(&parser, file);

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            DEBUG_PRINT("Parser error: %d\n", parser.error);
            exit(1);
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;

            if (strcmp(key, "afe_ip") == 0) {
                yaml_event_delete(&event);
                yaml_parser_parse(&parser, &event);
                config->afe_ip = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "afe_port") == 0) {
                yaml_event_delete(&event);
                yaml_parser_parse(&parser, &event);
                config->afe_port = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "sampling_rate") == 0) {
                yaml_event_delete(&event);
                yaml_parser_parse(&parser, &event);
                config->sampling_rate = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "sensors") == 0) {
                seq_level++;
            } else if (seq_level > 0) {
                if (strcmp(key, "label") == 0) {
                    yaml_event_delete(&event);
                    yaml_parser_parse(&parser, &event);
                    config->sensors[sensor_index].label = strdup((char *)event.data.scalar.value);
                } else if (strcmp(key, "block") == 0) {
                    yaml_event_delete(&event);
                    yaml_parser_parse(&parser, &event);
                    config->sensors[sensor_index].block = strdup((char *)event.data.scalar.value);
                } else if (strcmp(key, "channel") == 0) {
                    yaml_event_delete(&event);
                    yaml_parser_parse(&parser, &event);
                    config->sensors[sensor_index].channel = strdup((char *)event.data.scalar.value);
                } else if (strcmp(key, "gain") == 0) {
                    yaml_event_delete(&event);
                    yaml_parser_parse(&parser, &event);
                    config->sensors[sensor_index].gain = atoi((char *)event.data.scalar.value);
                    sensor_index++;
                }
            }
        } else if (event.type == YAML_SEQUENCE_END_EVENT) {
            seq_level--;
        }

        done = (event.type == YAML_STREAM_END_EVENT);

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(file);
}

void getdata(int socket, Config *config, double duration, const char *block_to_record, const char *sensor_to_record) {
    char recv_buf[DATA_SIZE];
    int recv_len;
    short samples[NUM_CHANNELS];
    double data_period = 1.0 / config->sampling_rate;
    char filename[BUF_SIZE * 3];

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    char host_name[BUF_SIZE];
    gethostname(host_name, BUF_SIZE);
    size_t host_name_len = strlen(host_name);

    // set filesuffix from current time
    char filesuffix[BUF_SIZE];
    snprintf(filesuffix, sizeof(filesuffix), "%d%02d%02d%02d%02d%02d.wav", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    size_t filesuffix_len = strlen(filesuffix);

    SF_INFO sfinfo;
    sfinfo.samplerate = config->sampling_rate;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    // Create and write headers for WAV files
    SNDFILE *wav_files[NUM_SENSORS] = { NULL };

    int sensor_to_record_idx = -1;
    for (int i = 0; i < NUM_SENSORS; i++) {
        size_t label_len = strlen(config->sensors[i].label);
        if (strcmp(config->sensors[i].block, block_to_record) == 0) {
            if (strcmp(sensor_to_record, "") != 0) {
                if (strcmp(config->sensors[i].label, sensor_to_record) == 0) {
                    sensor_to_record_idx = i;
                    if (host_name_len + label_len + filesuffix_len + 3 < sizeof(filename)) {
                        snprintf(filename, sizeof(filename), "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                    } else {
                        // エラー処理（例: ログ出力や戻り値でエラーを通知）
                    }

                    fprintf(stderr, "creating wav file [%s] for the sensor [%s]\n", filename, config->sensors[i].label);
                    wav_files[i] = sf_open(filename, SFM_WRITE, &sfinfo);
                    if (!wav_files[i]) {
                        fprintf(stderr, "Error: %s\n", sf_strerror(NULL));
                        exit(1);
                    }
                    break;
                }
            } else { // record all sensors
                if (host_name_len + label_len + filesuffix_len + 3 < sizeof(filename)) {
                    snprintf(filename, sizeof(filename), "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                } else {
                    fprintf(stderr, "Error: filename is too long.: ");
                    fprintf(stderr, "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                    exit(1);
                }
                fprintf(stderr, "creating wav file [%s] for the sensor [%s]\n", filename, config->sensors[i].label);
                wav_files[i] = sf_open(filename, SFM_WRITE, &sfinfo);
                if (!wav_files[i]) {
                    fprintf(stderr, "Error: %s\n", sf_strerror(NULL));
                    exit(1);
                }
            }
        }
    }

    if (strcmp(sensor_to_record, "") != 0 && sensor_to_record_idx == -1) {
        fprintf(stderr, "Error: Sensor label '%s' not found in the configuration.\n", sensor_to_record);
        exit(1);
    }

    double data_duration = 0.0;
    int packet_number = 0;
    int prev_packet_number = 0;
    while (data_duration < duration) {
        if (fabs(data_duration - duration) < EPSILON || data_duration > duration)
            break;

        recv_len = recvfrom(socket, recv_buf, DATA_SIZE, 0, NULL, NULL);

        // packet連番のチェック
        packet_number = (recv_buf[0] << 8) | recv_buf[1];
        if (prev_packet_number == 0 && packet_number > 256) {
            continue;
        }
        if (packet_number > 255 && (packet_number - prev_packet_number) > 0 && (packet_number - prev_packet_number) != 256) {
            fprintf(stderr, "Packet Loss is observed at packet: %d\n", packet_number);
        }
        prev_packet_number = packet_number;

        for (int byte_idx = 2; byte_idx < recv_len; byte_idx += NUM_CHANNELS * sizeof(short)) {
            for (int channel_idx = 0; channel_idx < NUM_CHANNELS; channel_idx++) {
                samples[channel_idx] = (recv_buf[byte_idx + channel_idx * 2] << 8) | recv_buf[byte_idx + channel_idx * 2 + 1];
            }

            if (sensor_to_record_idx != -1) {
                sf_write_short(wav_files[sensor_to_record_idx], &samples[sensor_to_record_idx], 1);
            } else {
                for (int i = 0; i < NUM_SENSORS; i++) {
                    if (strcmp(config->sensors[i].block, block_to_record) == 0) {
                        sf_write_short(wav_files[i], &samples[i], 1);
                    }
                }
            }
            data_duration += data_period;
            if (fabs(data_duration - duration) < EPSILON || data_duration > duration)
                break;
        }
    }
    DEBUG_PRINT("data_duration: %f\n", data_duration);
    DEBUG_PRINT("data_period: %f\n", data_period);

    for (int i = 0; i < NUM_SENSORS; i++) {
        if (strcmp(config->sensors[i].block, block_to_record) == 0) {
            if (sensor_to_record_idx != -1) {
                sf_close(wav_files[sensor_to_record_idx]);
                break;
            } else {
                sf_close(wav_files[i]);
            }
        }
    }
}



int send_start_command_of_block(int sock, struct sockaddr_in *serv_addr, Config *config, int block_count) {
    char start_command[32];
    char channel[BUF_SIZE];
    char response[32];
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // start command packet
    start_command[0] = 'O';
    start_command[1] = 'S';
    // Set block
    start_command[2] = block_data_map[block_count].data;
    // Set gain for 1-4 channels of the block
    for (int j = 0; j < NUM_CHANNELS; j++) {
        snprintf(channel, BUF_SIZE, "%d", j + 1);
        start_command[3 + j] = 0x00;
        for (int k = 0; k < NUM_SENSORS; k++) {
            if ((strcmp(config->sensors[k].block, block_data_map[block_count].block) == 0) && (strcmp(config->sensors[k].channel, channel) == 0)) {
                for (unsigned int m = 0; m < sizeof(gain_data_map) / sizeof(GainData); m++) {
                    if (config->sensors[k].gain == gain_data_map[m].gain) {
                        start_command[3 + j] = gain_data_map[m].data;
                        break;
                    }
                }
            }
        }
    }

    // Send the start command packet with the current block and channel
    if (sendto(sock, start_command, 32, 0, (struct sockaddr *)serv_addr, addr_len) == -1)
        error_handling("fail: sendto (start_command)");
    DEBUG_PRINT("Sent start command to AFE: ");
    for (int k = 0; k < 2; k++)
        DEBUG_PRINT("%c ", start_command[k]);
    for (int k = 2; k < 7; k++)
        DEBUG_PRINT("0x%x ", start_command[k]);
    DEBUG_PRINT("\n");

    if (recvfrom(sock, response, 32, 0, NULL, NULL) == -1)
        error_handling("fail: recvfrom (response of start_command)");
    
    if (response[0] == 'O' && response[1] == 'S' && response[2] == 0xA5) {
        DEBUG_PRINT("Start command is accepted successfully by AFE: %c %c 0x%X\n", response[0], response[1], response[2]);
        return 1;
    } else {
        fprintf(stderr, "Start command is failed: %c %c 0x%X\n", response[0], response[1], response[2]);
        return 0;
    }

}

int send_stop_command_of_block(int sock, struct sockaddr_in *serv_addr) {
    char stop_command[32];
    char response[32];
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // Prepare stop command packet
    stop_command[0] = 'O';
    stop_command[1] = 'Q';
    stop_command[3] = 0;

    if (sendto(sock, stop_command, 32, 0, (struct sockaddr *)serv_addr, addr_len) == -1)
        error_handling("fail: sendto (stop_command)");
    DEBUG_PRINT("Sent stop command to AFE\n");

    int valid_response_received = 0;
    while (!valid_response_received) {
        if (recvfrom(sock, response, 32, 0, NULL, NULL) == -1)
            error_handling("fail: recvfrom (response of stop_command)");

        if (response[0] == 'O' && response[1] == 'Q' && response[2] == 0xA5) {
            DEBUG_PRINT("Stop command is accepted successfully by AFE: %c %c 0x%X\n", response[0], response[1], response[2]);
            valid_response_received = 1;
        } else {
            fprintf(stderr, "Invalid response received: %c %c 0x%X\n", response[0], response[1], response[2]);
        }
    }
    return 1;
}