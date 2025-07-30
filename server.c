#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include <time.h>
#include <wiringPiI2C.h>
#include <wiringPi.h>

// ======== DB 정보 ========
#define DB_HOST "localhost"
#define DB_USER "admin"
#define DB_PASS "1234"
#define DB_NAME "your_DB"

// ======== LCD 주소 ========
#define LCD_ADDR 0x27

// ======== 전역 변수 ========
int lcd_fd;
volatile int current_id = 0;

// ======== LCD 출력 함수 (테스트용 콘솔 출력) ========
void lcdPrint(const char* line1, const char* line2) {
    // 실제 LCD 라이브러리에 맞게 수정 가능
    printf("[LCD] %s | %s\n", line1, line2);
}

// ======== DB에 데이터 삽입 ========
void insertData(float temp, float humidity, float lux, float level, int ir) {
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "DB Connect Error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    char query[256];
    snprintf(query, sizeof(query),
        "INSERT INTO sensor_data (temp,humidity,lux,level,ir,timestamp) "
        "VALUES (%.2f,%.2f,%.2f,%.2f,%s,NOW())",
        temp, humidity, lux, level, ir ? "TRUE" : "FALSE");

    if (mysql_query(conn, query)) {
        fprintf(stderr, "DB Insert Error: %s\n", mysql_error(conn));
    }

    mysql_close(conn);
}

// ======== DB에서 특정 id 조회 후 LCD 출력 ========
void fetchAndDisplay(int id) {
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "DB Connect Error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    char query[128];
    snprintf(query, sizeof(query),
             "SELECT temp,humidity,lux,level,ir FROM sensor_data WHERE id=%d", id);

    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            char line1[32], line2[32];
            snprintf(line1, sizeof(line1), "T:%.1f H:%.1f",
                     atof(row[0]), atof(row[1]));
            snprintf(line2, sizeof(line2), "L:%.0f Lv:%.0f IR:%s",
                     atof(row[2]), atof(row[3]), atoi(row[4]) ? "Y" : "N");
            lcdPrint(line1, line2);
        }
        mysql_free_result(res);
    } else {
        fprintf(stderr, "DB Select Error: %s\n", mysql_error(conn));
    }

    mysql_close(conn);
}

// ======== 조이스틱 스레드 ========
void* joystickThread(void* arg) {
    pinMode(0, INPUT); // UP
    pinMode(1, INPUT); // DOWN
    while (1) {
        if (digitalRead(0) == HIGH) { // UP
            current_id++;
            fetchAndDisplay(current_id);
            delay(300);
        }
        if (digitalRead(1) == HIGH) { // DOWN
            if (current_id > 1) current_id--;
            fetchAndDisplay(current_id);
            delay(300);
        }
        delay(50);
    }
    return NULL;
}

// ======== 클라이언트 핸들러 스레드 ========
void* clientHandler(void* arg) {
    int clientSock = *(int*)arg;
    free(arg);

    char buf[256];
    int len = read(clientSock, buf, sizeof(buf)-1);
    if (len > 0) {
        buf[len] = '\0';

        float t,h,l,lv;
        int ir;
        if (sscanf(buf, "%f,%f,%f,%f,%d", &t,&h,&l,&lv,&ir) == 5) {
            insertData(t,h,l,lv,ir);

            // 최신 ID 구하기
            MYSQL *conn = mysql_init(NULL);
            if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
                if (mysql_query(conn, "SELECT MAX(id) FROM sensor_data")==0) {
                    MYSQL_RES *res = mysql_store_result(conn);
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0]) {
                        current_id = atoi(row[0]);
                    }
                    mysql_free_result(res);
                }
                mysql_close(conn);
            }

            fetchAndDisplay(current_id);
        } else {
            fprintf(stderr, "Invalid data received: %s\n", buf);
        }
    }
    close(clientSock);
    return NULL;
}

// ======== 메인 함수 ========
int main() {
    // WiringPi 초기화
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi setup failed!\n");
        return 1;
    }

    // LCD 초기화
    lcd_fd = wiringPiI2CSetup(LCD_ADDR);
    if (lcd_fd < 0) {
        fprintf(stderr, "LCD I2C setup failed!\n");
        // LCD가 없어도 서버는 계속 동작할 수 있게 진행
    }

    // 조이스틱 스레드
    pthread_t joyThread;
    if (pthread_create(&joyThread, NULL, joystickThread, NULL) != 0) {
        fprintf(stderr, "Failed to create joystick thread\n");
    }

    // TCP 서버 소켓
    int servSock = socket(PF_INET, SOCK_STREAM, 0);
    if (servSock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in servAddr, clntAddr;
    socklen_t clntAddrSize = sizeof(clntAddr);
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(5000);

    if (bind(servSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        perror("bind");
        close(servSock);
        return 1;
    }

    if (listen(servSock, 5) < 0) {
        perror("listen");
        close(servSock);
        return 1;
    }

    printf("port 5000 로 진행 중...\n");

    while (1) {
        int *clntSock = malloc(sizeof(int));
        if (!clntSock) {
            fprintf(stderr, "Memory alloc error\n");
            continue;
        }

        *clntSock = accept(servSock, (struct sockaddr*)&clntAddr, &clntAddrSize);
        if (*clntSock < 0) {
            perror("accept");
            free(clntSock);
            continue;
        }

        pthread_t t;
        if (pthread_create(&t, NULL, clientHandler, (void*)clntSock) != 0) {
            fprintf(stderr, "Failed to create client handler thread\n");
            close(*clntSock);
            free(clntSock);
        } else {
            pthread_detach(t);
        }
    }

    close(servSock);
    return 0;
}