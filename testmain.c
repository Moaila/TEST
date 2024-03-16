#include "video_decoder.h"
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h> 
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>

#define GRAYSCALE_CHARS " .:-=+*#%@"

void set_nonblocking_input() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

int kbhit() {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

void print_frame_as_grayscale(Frame frame) {
    int char_len = strlen(GRAYSCALE_CHARS) - 1;
    char *output = malloc(frame.width * frame.height + frame.height + 1);
    char *p = output;

    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            int index = y * frame.linesize + x * 3;
            unsigned char r = frame.data[index];
            unsigned char g = frame.data[index + 1];
            unsigned char b = frame.data[index + 2];
            double gray = 0.299 * r + 0.587 * g + 0.114 * b;
            int val = (int)(char_len * gray / 255);
            *p++ = GRAYSCALE_CHARS[val];
        }
        *p++ = '\n';
    }
    *p = '\0';

    printf("%s", output);
    free(output);
}

void print_frame_as_rgb(Frame frame) {
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            int index = y * frame.linesize + x * 3;
            unsigned char r = frame.data[index];
            unsigned char g = frame.data[index + 1];
            unsigned char b = frame.data[index + 2];
            
            // 使用 ANSI 转义字符来设置前景色
            printf("\x1b[38;2;%d;%d;%dm█", r, g, b);
        }
        printf("\x1b[0m\n"); // 重置颜色并换行
    }
}

// ANSI 转义字符，用于设置终端颜色
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_GRAY    "\x1b[90m"
Frame resize_with_average_pooling(Frame frame, int target_width, int target_height) {
    Frame resized;
    resized.width = target_width;
    resized.height = target_height;
    resized.linesize = target_width * 3; // 假设RGB格式
    resized.data = malloc(resized.linesize * resized.height);

    int horizontal_step = frame.width / target_width;
    int vertical_step = frame.height / target_height;

    for (int y = 0; y < target_height; y++) {
        for (int x = 0; x < target_width; x++) {
            long sum_r = 0, sum_g = 0, sum_b = 0;
            for (int dy = 0; dy < vertical_step; dy++) {
                for (int dx = 0; dx < horizontal_step; dx++) {
                    int index = ((y * vertical_step + dy) * frame.linesize + (x * horizontal_step + dx) * 3);
                    sum_r += frame.data[index];
                    sum_g += frame.data[index + 1];
                    sum_b += frame.data[index + 2];
                }
            }
            int count = vertical_step * horizontal_step;
            resized.data[(y * resized.linesize) + x * 3]     = sum_r / count;
            resized.data[(y * resized.linesize) + x * 3 + 1] = sum_g / count;
            resized.data[(y * resized.linesize) + x * 3 + 2] = sum_b / count;
        }
    }

    return resized;
}

void show_help() {
    // 显示帮助信息
    printf("Usage: ./player [OPTIONS]\n");
    printf("Options:\n");
    printf("  -h        Display this help message and open document\n");
    printf("  -v        Output version information\n");

    // 使用系统调用打开指定位置的文档
    if (access("/home/tom/example.txt", F_OK) != -1) {
        system("xdg-open /home/tom/example.txt");
    } else {
        printf("无法打开文档。\n");
    }
}

void show_version() {
    // 显示版本信息
    printf("版本信息：Dian团队工程开发测试\n");
}

int main(int argc, char *argv[]) {
    bool color = false;
    int target_width = 0;
    int target_height = 0;
    int custom_delay = -1; // 用户可以通过命令行参数指定自定义延迟
    const char *filename = NULL; // 通过 -f 参数指定的文件名

    if (argc == 1) {
        printf("Missing options. Use -h for usage information.\n");
        return -1;
    } else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0) {
                show_help();
                return 0;
            } else if (strcmp(argv[i], "-v") == 0) {
                show_version();
                return 0;
            } else if (strcmp(argv[i], "-c") == 0) {
                color = true;
            } else if (strcmp(argv[i], "-r") == 0 && i + 2 < argc) {
                target_width = atoi(argv[i + 1]);
                target_height = atoi(argv[i + 2]);
                i += 2; // 跳过接下来的两个参数
            } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
                custom_delay = atoi(argv[i + 1]);
                i++; // 跳过接下来的一个参数
            } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
                filename = argv[i + 1];
                i++; // 跳过文件路径参数
            }
        }

        if (!filename || target_width <= 0 || target_height <= 0) {
            printf("Invalid or missing parameters. Please check your input and use -h for help.\n");
            return -1;
        }

        if (decoder_init(filename) != 0) {
            fprintf(stderr, "Failed to initialize decoder with file %s\n", filename);
            return -1;
        }

        double fps = get_fps();
        int delay = custom_delay > 0 ? custom_delay * 1000 : (int)(1000000.0 / fps);
        int playback_speed = 1; // 播放速度，默认为1

        bool running = true;
        // 设置非阻塞输入
        set_nonblocking_input();

        bool paused = false; // 标记是否暂停

        while (running) {
            // 检测键盘输入
            if (kbhit()) {
                char ch = getchar();
                if (ch == ' ') {
                    paused = !paused; // 暂停/继续切换
                } else if (ch == 'a') {
                    playback_speed *= 2; // 播放速度加倍
                }
            }

            if (!paused) {
                system("clear"); // 或者system("cls"); 用于Windows
                Frame frame = decoder_get_frame();
                if (frame.data == NULL) {
                    running = false;
                    break;
                }

                Frame resized_frame = resize_with_average_pooling(frame, target_width, target_height);

                if (color) {
                    print_frame_as_rgb(resized_frame);
                } else {
                    print_frame_as_grayscale(resized_frame);
                }

                usleep(delay / playback_speed); // 根据播放速度调整等待时间
                free(resized_frame.data); // 释放调整大小后的帧数据
            }
        }

        decoder_close();
        return 0;
    }
}

