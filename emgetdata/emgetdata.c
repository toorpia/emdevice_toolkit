#define VERSION "0.1.0"
#define COPYRIGHT "Copyright (C) 2023 Tokuyama Coorporation, Easy Measure Inc., and toor Inc. All rights reserved."

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
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include "debug.h"

#define BUF_SIZE 1024
#define NUM_BLOCKS 5
#define NUM_CHANNELS 4
#define NUM_SENSORS 20
#define DATA_SIZE 1026
#define NUM_DATA_PER_PACKET 128 // 128 data per packet
#define TIMEOUT_SEC 1
#define TIMEOUT_USEC 500000 // total timeout length: 1500 msec
#define EPSILON 1.0e-9
#define SAMPLING_RATE 20000 // Sampling Rate of AFE

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

void error_handling(char *message, int sock, struct sockaddr_in *serv_addr);
void read_config(const char *filename, Config *config);
int getdata(int sock, Config *config, double duration, const char *block_to_record, const char *sensor_to_record);
int send_start_command_of_block(int sock, struct sockaddr_in *serv_addr, Config *config, const char *block);
int send_stop_command_of_block(int sock, struct sockaddr_in *serv_addr);
void clear_remaining_buffer(int sock);
void set_timeout(int sock);
int check_response(int sock, char *command);
int16_t** create_data_buffer(double duration, int sampling_rate);
void free_data_buffer(int16_t** data_buffer);
void downsample(int16_t *original_data, int16_t *reduced_data, int reduced_length, int original_rate, int new_rate);
void write_wav_files(SNDFILE **wav_files, int16_t **data_buffer, int data_idx, int sensor_to_record_idx, const char *block_to_record, Config *config, int *channel_of_sensor);

void usage() {
    fprintf(stderr, "Usage: emgetdata [-f config_file] [-t duration] [-s sensor]\n");
    fprintf(stderr, "  -f config_file: config file path. default: config.yml\n");
    fprintf(stderr, "  -t duration: duration in sec. default: 10 sec.\n");
    fprintf(stderr, "  -s sensor: specify a sensor label to record. otherwise, all sensors are recorded.\n");
    fprintf(stderr, "  -h: show this help\n");
    fprintf(stderr, "  -v: show version\n");
    fprintf(stderr, "%s\n", COPYRIGHT);
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
                    fprintf(stderr, "Error: Duration argument is missing or invalid.\n");
                    exit(1);
                }
                break;
            case 's':
                if (optarg != NULL) {
                    sensor_to_record = optarg;
                    DEBUG_PRINT("sensor to record: %s\n", sensor_to_record);
                } else {
                    fprintf(stderr, "Error: Sensor argument is missing or invalid.\n");
                    exit(1);
                }
                break;
            case 'h':
                usage();
                exit(0);
            case 'v':
                fprintf(stderr, "emgetdata version %s\n", VERSION);
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
            fputs("Sensor not found in config file.", stderr);
            fputc('\n', stderr);
            exit(1);
        }
    }   

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        error_handling("socket", sock, &serv_addr);
    
    // タイムアウトの設定
    set_timeout(sock);

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

        int retry_count_getdata = 0;
        int retry_limit = 3;
        retry:

        // 計測開始コマンドの送信
        if (send_start_command_of_block(sock, &serv_addr, &config, block_data_map[block_count].block) < 0) {
            fprintf(stderr, "Error: send_start_command_of_block() failed.\n");
            exit(1);
        }
        usleep(1000000);

        // データ取得
        DEBUG_PRINT("Start recording for block %s...\n", block_data_map[block_count].block);
        if (getdata(sock, &config, duration, block_data_map[block_count].block, sensor_to_record) < 0) {
            // getdata()が失敗した場合は、stopコマンドを送信してからリトライする。ただし、3回まで。
            retry_count_getdata++;
            if (retry_count_getdata > retry_limit) {
                fprintf(stderr, "Error: getdata() failed. Retry count exceeded.\n");
                exit(1);
            }
            fprintf(stderr, "Error: getdata() failed. Retry...\n");

            // 計測終了コマンドの送信
            if (send_stop_command_of_block(sock, &serv_addr) < 0) {
                fprintf(stderr, "Error: send_stop_command_of_block() failed.\n");
                exit(1);
            }

            goto retry;
        }
        DEBUG_PRINT("done\n");

        // 計測終了コマンドの送信
        if (send_stop_command_of_block(sock, &serv_addr) < 0) {
            fprintf(stderr, "Error: send_stop_command_of_block() failed.\n");
            exit(1);
        }
        usleep(1000000);
    } // end of for (int block_count = 0; block_count < NUM_BLOCKS; block_count++)

    close(sock);
    return 0;
}

void error_handling(char *message, int sock, struct sockaddr_in *serv_addr) {
    // If the socket and serv_addr are valid, send stop command
    if (sock >= 0 && serv_addr != NULL) {
        send_stop_command_of_block(sock, serv_addr);
    }

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

int getdata(int sock, Config *config, double duration, const char *block_to_record, const char *sensor_to_record) {
    uint8_t recv_buf[DATA_SIZE];
    int recv_len;
    double data_period = 1.0 / SAMPLING_RATE;

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

    // filenameを格納する配列を生成
    char filenames[NUM_SENSORS][BUF_SIZE * 3] = { "" };

    // sensor番号とblockにおけるchannel番号との対応を格納する配列を生成
    int channel_of_sensor[NUM_SENSORS] = { -1 };
    int channel_idx = 0; // 0, 1, 2, 3

    int sensor_to_record_idx = -1;
    for (int i = 0; i < NUM_SENSORS; i++) {
        size_t label_len = strlen(config->sensors[i].label);
        if (strcmp(config->sensors[i].block, block_to_record) == 0) {
            channel_of_sensor[i] = channel_idx;
            channel_idx++;
            // sensor_to_recordが指定されている場合は、そのセンサーのみwavファイルを作成する
            if (strcmp(sensor_to_record, "") != 0) {
                if (strcmp(config->sensors[i].label, sensor_to_record) == 0) {
                    sensor_to_record_idx = i;
                    if (host_name_len + label_len + filesuffix_len + 3 < sizeof(filenames[i])) {
                        snprintf(filenames[i], sizeof(filenames[i]), "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                    } else {
                        // エラー処理（例: ログ出力や戻り値でエラーを通知）
                    }

                    fprintf(stderr, "creating wav file [%s] for the sensor [%s]\n", filenames[i], config->sensors[i].label);
                    wav_files[i] = sf_open(filenames[i], SFM_WRITE, &sfinfo);
                    if (!wav_files[i]) {
                        fprintf(stderr, "Error: %s\n", sf_strerror(NULL));
                        exit(1);
                    }
                    break;
                }
            } else { // record all sensors
                if (host_name_len + label_len + filesuffix_len + 3 < sizeof(filenames[i])) {
                    snprintf(filenames[i], sizeof(filenames[i]), "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                } else {
                    fprintf(stderr, "Error: filename is too long.: ");
                    fprintf(stderr, "%s_%s_%s", host_name, config->sensors[i].label, filesuffix);
                    exit(1);
                }
                fprintf(stderr, "creating wav file [%s] for the sensor [%s]\n", filenames[i], config->sensors[i].label);
                wav_files[i] = sf_open(filenames[i], SFM_WRITE, &sfinfo);
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

    // データ受信用のdata_buffer[NUM_CHANNEL][配列を初期化
    int16_t **data_buffer = create_data_buffer(duration, SAMPLING_RATE); // AFEのサンプリングレートは20kHz固定なので、まずはそれを受信して、後でconfig->sampling_rateへdownsampleする

    // データ受信
    data_duration = 0.0;
    int data_idx = 0;
    DEBUG_PRINT("start recording\n");
    //DEBUG_PRINT("packet_number: ");
    while (data_duration < duration) {
        if (fabs(data_duration - duration) < EPSILON || data_duration > duration)
            break;

        recv_len = recvfrom(sock, recv_buf, DATA_SIZE, 0, NULL, NULL);
        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout occurred, continue with next iteration
                printf("Timeout, no data received\n");

                // close & remove files
                if (sensor_to_record_idx != -1) {
                    sf_close(wav_files[sensor_to_record_idx]);
                    remove(filenames[sensor_to_record_idx]);
                } else {
                    for (int i = 0; i < NUM_SENSORS; i++) {
                        if (strcmp(config->sensors[i].block, block_to_record) == 0) {
                            sf_close(wav_files[i]);
                            remove(filenames[i]);
                        }
                    }
                }
                return -1; // -1で返すことによって、呼び出し位置(main関数内)でretryする
            } else {
                perror("recvfrom");
                exit(1);
            }
        }
        if (recv_len < DATA_SIZE) {
            fprintf(stderr, "Error: recvfrom() returned %d\n", recv_len);
            perror("recvfrom");
            //break;
            continue;
        }

        // packet連番のチェック
        packet_number = recv_buf[0] | (recv_buf[1] <<8);
        if ((packet_number - prev_packet_number) > 1) {
            fprintf(stderr, "Packet Loss is observed at packet: %d\n", packet_number);
        }
        prev_packet_number = packet_number;

        for (int byte_idx = 2; byte_idx < recv_len; byte_idx += NUM_CHANNELS * sizeof(short)) {
            for (int channel_idx = 0; channel_idx < NUM_CHANNELS; channel_idx++) {
                data_buffer[channel_idx][data_idx] = (int16_t)(recv_buf[byte_idx + channel_idx * 2] | (recv_buf[byte_idx + channel_idx * 2 + 1] << 8));
                data_buffer[channel_idx][data_idx] -= 0x7FFF;
            }
            data_idx += 1;
            data_duration += data_period;
            if (fabs(data_duration - duration) < EPSILON || data_duration > duration)
                break;
        }
    }
    DEBUG_PRINT("data_duration: %f\n", data_duration);
    DEBUG_PRINT("data_idx: %d\n", data_idx);
    DEBUG_PRINT("duration_in_samples: %d\n", (int)(duration * SAMPLING_RATE));

    if (config->sampling_rate < SAMPLING_RATE) {
        DEBUG_PRINT("downsampling from 20kHz to %dHz\n", config->sampling_rate);
        // AFEで20kHzで取得されたデータを config->sampling_rate にdownsample する
        int reduced_length = (data_idx * config->sampling_rate) / SAMPLING_RATE;
        int16_t** reduced_data_buffer = malloc(NUM_CHANNELS * sizeof(int16_t*));
        for (int i = 0; i < NUM_CHANNELS; i++) {
            reduced_data_buffer[i] = calloc(reduced_length, sizeof(int16_t));
            downsample(data_buffer[i], reduced_data_buffer[i], reduced_length, SAMPLING_RATE, config->sampling_rate);
        }
        write_wav_files(wav_files, reduced_data_buffer, reduced_length, sensor_to_record_idx, block_to_record, config, channel_of_sensor);
        free_data_buffer(reduced_data_buffer);
    } else {
        // AFEで20kHzで取得されたデータをそのまま書き込む
        write_wav_files(wav_files, data_buffer, data_idx, sensor_to_record_idx, block_to_record, config, channel_of_sensor);
    }

    free_data_buffer(data_buffer);

    return 0;
}

void write_wav_files(SNDFILE **wav_files, int16_t **data_buffer, int data_idx, int sensor_to_record_idx, const char *block_to_record, Config *config, int *channel_of_sensor) {
    if (sensor_to_record_idx != -1) { // write specified sensor
        if (sf_write_short(wav_files[sensor_to_record_idx], data_buffer[channel_of_sensor[sensor_to_record_idx]], data_idx) != data_idx) {
            fprintf(stderr, "Error: sf_write_short() failed\n");
            exit(1);
        }
        sf_write_sync(wav_files[sensor_to_record_idx]);
        sf_close(wav_files[sensor_to_record_idx]);
    } else { // write all sensors
        for (int i = 0; i < NUM_SENSORS; i++) {
            if (strcmp(config->sensors[i].block, block_to_record) == 0) {
                if (sf_write_short(wav_files[i], data_buffer[channel_of_sensor[i]], data_idx) != data_idx) {
                    fprintf(stderr, "Error: sf_write_short() failed\n");
                    exit(1);
                }
                sf_write_sync(wav_files[i]);
                sf_close(wav_files[i]);
            }
        } // for (int i = 0; i < NUM_SENSORS; i++)
    }
}

int send_start_command_of_block(int sock, struct sockaddr_in *serv_addr, Config *config, const char *block) {
    char start_command[32];
    char channel[BUF_SIZE];
    socklen_t addr_len = sizeof(struct sockaddr_in);

    clear_remaining_buffer(sock);

    // start command packet
    start_command[0] = 'O';
    start_command[1] = 'S';
    // Set block
    for (unsigned int i = 0; i < sizeof(block_data_map) / sizeof(BlockData); i++) {
        if (strcmp(block, block_data_map[i].block) == 0) {
            start_command[2] = block_data_map[i].data;
            break;
        }
    }

    // Set gain for 1-4 channels of the block
    for (int j = 0; j < NUM_CHANNELS; j++) {
        snprintf(channel, BUF_SIZE, "%d", j + 1);
        start_command[3 + j] = 0x00;
        for (int k = 0; k < NUM_SENSORS; k++) {
            if ((strcmp(config->sensors[k].block, block) == 0) && (strcmp(config->sensors[k].channel, channel) == 0)) {
                for (unsigned int m = 0; m < sizeof(gain_data_map) / sizeof(GainData); m++) {
                    if (config->sensors[k].gain == gain_data_map[m].gain) {
                        start_command[3 + j] = gain_data_map[m].data;
                        break;
                    }
                }
            }
        }
    }

    int retry_count = 0;
    int retry_max = 3;
    retry_start_command:
    // Send the start command packet with the current block and channel
    if (sendto(sock, start_command, 32, 0, (struct sockaddr *)serv_addr, addr_len) == -1)
        error_handling("fail: sendto (start_command)", sock, serv_addr);

    DEBUG_PRINT("Sent start command to AFE: ");
    for (int k = 0; k < 2; k++)
        DEBUG_PRINT("%c ", start_command[k]);
    for (int k = 2; k < 7; k++)
        DEBUG_PRINT("0x%x ", start_command[k]);
    DEBUG_PRINT("\n");

    if (check_response(sock, start_command) < 0) {
        retry_count++;
        if (retry_count > retry_max) {
            fprintf(stderr, "Error: Failed to send start command to AFE\n");
            return -1;
        }
        goto retry_start_command;
    }
    return 1;
}

int send_stop_command_of_block(int sock, struct sockaddr_in *serv_addr) {
    char stop_command[32];
    socklen_t addr_len = sizeof(struct sockaddr_in);

    clear_remaining_buffer(sock);

    // Prepare stop command packet
    stop_command[0] = 'O';
    stop_command[1] = 'Q';
    stop_command[2] = '\0';

    int retry_count = 0;
    int retry_max = 3;
    retry_stop_command:
    if (sendto(sock, stop_command, 32, 0, (struct sockaddr *)serv_addr, addr_len) == -1) {
        fputs("fail: sendto (stop_command)", stderr);
        fputc('\n', stderr);
        exit(1);
    }
    DEBUG_PRINT("Sent stop command to AFE\n");

    if (check_response(sock, stop_command) < 0) {
        retry_count++;
        if (retry_count > retry_max) {
            fprintf(stderr, "Error: Failed to send stop command to AFE\n");
            return -1;
        }
        goto retry_stop_command;
    }
    return 1;
}

int check_response(int sock, char *command) {
    char response[32];
    int retry_count = 0;
    int retry_limit = 5;
    while (1) {
        if (recvfrom(sock, response, 32, 0, NULL, NULL) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout occurred, continue with next iteration
                printf("Timeout, no data received\n");
                retry_count++;
                if (retry_count > retry_limit) {
                    fprintf(stderr, "Command is failed by timeout.\n");
                    return -1;
                }
            } else {
                perror("recvfrom");
                exit(1);
            }
        }

        if (response[0] == command[0] && response[1] == command[1] && response[2] == (char)0xA5) {
            DEBUG_PRINT("Command is accepted successfully by AFE: %c %c 0x%X\n", response[0], response[1], response[2]);
            return 1;
        } else {
            fprintf(stderr, "Command is not accepted yet: %c %c 0x%X\n", response[0], response[1], response[2]);
            return -1;
        }
    }
}

// clear remaining buffer
void clear_remaining_buffer(int sock) {
    // change socket to non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl (F_GETFL)");
        exit(1);
    }

    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // clear remaining buffer
    char tmp_buffer[1026];
    while (recvfrom(sock, tmp_buffer, sizeof(tmp_buffer), 0, NULL, NULL) > 0) {
    }

    // change socket back to blocking
    flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl (F_GETFL)");
        exit(1);
    }
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);

    // set timeout
    set_timeout(sock);
}

// set timeout
void set_timeout(int sock) {
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        exit(1);
    }
}

int16_t** create_data_buffer(double duration, int sampling_rate) {
    int duration_in_samples = (int)(duration * sampling_rate);
    int16_t** data_buffer = malloc(NUM_CHANNELS * sizeof(int16_t*));
    for (int i = 0; i < NUM_CHANNELS; i++) {
        data_buffer[i] = calloc(duration_in_samples, sizeof(int16_t));
    }
    return data_buffer;
}

void free_data_buffer(int16_t** data_buffer) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        free(data_buffer[i]);
    }
    free(data_buffer);
}

void downsample(int16_t *original_data, int16_t *reduced_data, int reduced_length, int original_rate, int new_rate) {
    int gcd = 1;
    for (int i = 1; i <= original_rate && i <= new_rate; ++i) {
        if (original_rate % i == 0 && new_rate % i == 0) {
            gcd = i;
        }
    }

    int step = original_rate / gcd;
    for (int i = 0; i < reduced_length; i++) {
        int index = i * step;
        reduced_data[i] = original_data[index];
    }
}
