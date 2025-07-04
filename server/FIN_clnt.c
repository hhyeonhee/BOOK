// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "/home/iot122/Downloads/cJSON-master/cJSON.c"
#include "/home/iot122/Downloads/cJSON-master/cJSON.h"

//— 매크로 정의 —————————————————————————————————————————————
#define SERVER_PORT   9999
#define SERVER_IP     "10.10.20.122"
#define RESET         0
#define BUF_SIZE      1024

//— 요청 코드 (서버와 동일하게 선언) —————————————————————————————
enum RequestCode {
    REQ_LOGIN            = 1,
    // 관리자
    REQ_ADMIN_ADD_USER   = 10,
    REQ_ADMIN_DEL_USER   = 11,
    REQ_ADMIN_LIST_USERS = 12,
    REQ_ADMIN_ADD_BOOK   = 20,
    REQ_ADMIN_DEL_BOOK   = 21,
    REQ_ADMIN_LIST_BOOKS = 22,
    REQ_ADMIN_SET_TIME   = 30,
    REQ_ADMIN_LIST_FAULTY= 40,
    REQ_ADMIN_CLEAR_FAULTY=41,
    // 사서
    REQ_LIB_LIST_PENDING = 50,
    REQ_LIB_APPROVE_RENT = 51,
    REQ_LIB_REJECT_RENT  = 52,
    REQ_LIB_SET_LOANABLE = 53,
    REQ_LIB_LIST_FAULTY  = 54,
    REQ_LIB_CLEAR_FAULTY = 55,
    // 일반 사용자
    REQ_USER_SEARCH_BOOK =100,
    REQ_USER_REQUEST_RENT=101,
    REQ_USER_LIST_RENTS  =102,
    REQ_USER_RETURN_BOOK =103,
    REQ_USER_EDIT_PROFILE=104
};

//— 응답 상태 ———————————————————————————————————————————————
enum ResponseStatus {
    RES_OK  = 0,
    RES_ERR = 1
};

//— 패킷 구조체 —————————————————————————————————————————————
typedef struct {
    int code;            // 요청 코드
    char data[512];      // 요청 데이터(JSON 또는 기타)
} Request;

typedef struct {
    int status;          // 응답 상태 (RES_OK/RES_ERR)
    char data[512];      // 응답 메시지 또는 JSON
} Response;

//— 프로토타입 (한글 설명 추가) ——————————————————————————————————
int     connect_server(void);                              // 서버 소켓에 연결
void    send_req(int sock, const Request* req);            // 패킷 전송
int     recv_res(int sock, Response* res);                 // 패킷 수신
void    admin_menu(int sock, const char* uid);             // 관리자 메뉴
void    librarian_menu(int sock, const char* uid);         // 사서 메뉴
void    user_menu(int sock, const char* uid);              // 일반 사용자 메뉴

//— main —————————————————————————————————————————————————————
int main(void) {
    // 1) 서버에 연결
    int sock = connect_server();
    if (sock < RESET) return EXIT_FAILURE;

    // 2) 로그인
    Request req = {RESET};
    Response res;
    char uid[50], pw[50];
    printf("아이디: ");    scanf("%49s", uid);
    printf("비밀번호: ");  scanf("%49s", pw);

    req.code = REQ_LOGIN;
    snprintf(req.data, sizeof(req.data), "%s:%s", uid, pw);
    send_req(sock, &req);

    if (recv_res(sock, &res) < RESET || res.status == RES_ERR) {
        printf("로그인 실패: %s\n", res.data);
        close(sock);
        return EXIT_FAILURE;
    }

    // 3) 역할 분기
    if      (strcmp(res.data, "admin")     == 0) admin_menu(sock, uid);
    else if (strcmp(res.data, "librarian") == 0) librarian_menu(sock, uid);
    else                                        user_menu(sock, uid);

    close(sock);
    return RESET;
}

//— 서버 연결 함수 —————————————————————————————————————————————————
int connect_server(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < RESET) {
        perror("소켓 생성 실패");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, RESET, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < RESET) {
        perror("서버 연결 실패");
        close(sock);
        return -1;
    }
    return sock;
}

//— 요청 전송 함수 —————————————————————————————————————————————————
void send_req(int sock, const Request* req) {
    write(sock, req, sizeof(Request));
}

//— 응답 수신 함수 —————————————————————————————————————————————————
int recv_res(int sock, Response* res) {
    int r = read(sock, res, sizeof(Response));
    if (r < sizeof(Response)) {
        printf("서버 응답 수신 오류\n");
        return -1;
    }
    return 0;
}

//— 관리자 메뉴 구현 —————————————————————————————————————————————————
void admin_menu(int sock, const char* uid) {
    int cmd = RESET;
    Request req = {RESET};
    Response res;
    while (1) {
        printf(
            "\n[관리자 메뉴]\n"
            "1)사용자 추가 2)사용자 삭제 3)사용자 목록\n"
            "4)도서 추가 5)도서 삭제 6)도서 목록\n"
            "7)운영시간 설정 8)불량대출자 목록 9)불량 해제\n"
            "0)로그아웃 > "
        );
        scanf("%d", &cmd);

        if (cmd == 1) {
            // 사용자 추가
            cJSON* o = cJSON_CreateObject();
            char buf[200];
            printf("추가할 ID: ");    scanf("%199s", buf); cJSON_AddStringToObject(o,"ID",buf);
            printf("비밀번호: ");    scanf("%199s", buf); cJSON_AddStringToObject(o,"PW",buf);
            int birth; printf("생년: "); scanf("%d", &birth); cJSON_AddNumberToObject(o,"BIRTH", birth);
            printf("이름: ");        scanf("%199s", buf); cJSON_AddStringToObject(o,"NAME",buf);
            printf("전화번호: ");    scanf("%199s", buf); cJSON_AddStringToObject(o,"PHONE",buf);
            printf("주소: ");        scanf("%199s", buf); cJSON_AddStringToObject(o,"ADDRESS",buf);
            char* s = cJSON_PrintUnformatted(o);
            req.code = REQ_ADMIN_ADD_USER;
            strcpy(req.data, s);
            free(s); cJSON_Delete(o);
        }
        else if (cmd == 2) {
            // 사용자 삭제
            int no; printf("삭제할 NO: "); scanf("%d", &no);
            req.code = REQ_ADMIN_DEL_USER;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 3) {
            // 사용자 목록
            req.code = REQ_ADMIN_LIST_USERS;
        }
        else if (cmd == 4) {
            // 도서 추가
            cJSON* o = cJSON_CreateObject();
            char buf[200];
            printf("ISBN: ");      scanf("%199s", buf); cJSON_AddStringToObject(o,"ISBN",buf);
            printf("제목: ");      scanf("%199s", buf); cJSON_AddStringToObject(o,"TITLE",buf);
            printf("저자: ");      scanf("%199s", buf); cJSON_AddStringToObject(o,"AUTHOR",buf);
            printf("출판사: ");    scanf("%199s", buf); cJSON_AddStringToObject(o,"PUBLISHER",buf);
            int yr, ct;
            printf("출판연도: "); scanf("%d", &yr); cJSON_AddNumberToObject(o,"YEAR",yr);
            printf("권수: ");     scanf("%d", &ct); cJSON_AddNumberToObject(o,"COUNT",ct);
            char* s = cJSON_PrintUnformatted(o);
            req.code = REQ_ADMIN_ADD_BOOK;
            strcpy(req.data, s);
            free(s); cJSON_Delete(o);
        }
        else if (cmd == 5) {
            // 도서 삭제
            int no; printf("삭제할 도서 NO: "); scanf("%d", &no);
            req.code = REQ_ADMIN_DEL_BOOK;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 6) {
            // 도서 목록
            req.code = REQ_ADMIN_LIST_BOOKS;
        }
        else if (cmd == 7) {
            // 운영시간 설정
            cJSON* o = cJSON_CreateObject();
            int sh, ch;
            char buf[50];
            printf("시작 시각(0~23): "); scanf("%d", &sh); cJSON_AddNumberToObject(o,"START",sh);
            printf("종료 시각(0~23): "); scanf("%d", &ch); cJSON_AddNumberToObject(o,"CLOSE",ch);
            printf("휴일(예: Sunday): "); scanf("%49s", buf); cJSON_AddStringToObject(o,"HOLIDAY",buf);
            char* s = cJSON_PrintUnformatted(o);
            req.code = REQ_ADMIN_SET_TIME;
            strcpy(req.data, s);
            free(s); cJSON_Delete(o);
        }
        else if (cmd == 8) {
            // 불량대출자 목록
            req.code = REQ_ADMIN_LIST_FAULTY;
        }
        else if (cmd == 9) {
            // 불량 해제
            int no; printf("해제할 NO: "); scanf("%d", &no);
            req.code = REQ_ADMIN_CLEAR_FAULTY;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 0) {
            // 로그아웃
            break;
        }
        else {
            printf("잘못된 명령입니다\n");
            continue;
        }

        // 요청 전송 & 응답 수신
        send_req(sock, &req);
        recv_res(sock, &res);
        if (res.status == RES_OK)    printf("[성공] %s\n", res.data);
        else                          printf("[오류] %s\n", res.data);
    }
}

//— 사서 메뉴 —————————————————————————————————————————————————
void librarian_menu(int sock, const char* uid) {
    int cmd = RESET;
    Request req = {RESET};
    Response res;
    while (1) {
        printf(
            "\n[사서 메뉴]\n"
            "1)대출 신청 목록 2)승인 3)거절\n"
            "4)대출 가능 설정 5)불량대출자 목록 6)불량 해제\n"
            "0)로그아웃 > "
        );
        scanf("%d", &cmd);

        if (cmd == 1) {
            req.code = REQ_LIB_LIST_PENDING;
        }
        else if (cmd == 2) {
            int no; printf("승인할 NO: "); scanf("%d", &no);
            req.code = REQ_LIB_APPROVE_RENT;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 3) {
            int no; printf("거절할 NO: "); scanf("%d", &no);
            req.code = REQ_LIB_REJECT_RENT;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 4) {
            int bno, flag;
            printf("도서 NO: "); scanf("%d", &bno);
            printf("1=가능,0=불가: "); scanf("%d", &flag);
            req.code = REQ_LIB_SET_LOANABLE;
            snprintf(req.data, sizeof(req.data), "%d:%d", bno, flag);
        }
        else if (cmd == 5) {
            req.code = REQ_LIB_LIST_FAULTY;
        }
        else if (cmd == 6) {
            int no; printf("해제할 NO: "); scanf("%d", &no);
            req.code = REQ_LIB_CLEAR_FAULTY;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 0) {
            break;
        }
        else {
            printf("잘못된 명령입니다\n");
            continue;
        }

        send_req(sock, &req);
        recv_res(sock, &res);
        if (res.status == RES_OK)    printf("[성공] %s\n", res.data);
        else                         printf("[오류] %s\n", res.data);
    }
}

//— 일반 사용자 메뉴 —————————————————————————————————————————————
void user_menu(int sock, const char* uid) {
    int cmd = RESET;
    Request req = {RESET};
    Response res;
    while (1) {
        printf(
            "\n[사용자 메뉴]\n"
            "1)도서 검색 2)대출 신청 3)내 대출 목록\n"
            "4)반납 5)내 정보 수정 0)종료 > "
        );
        scanf("%d", &cmd);

        if (cmd == 1) {
            // 도서 검색
            char kw[200];
            printf("검색어: "); scanf("%199s", kw);
            req.code = REQ_USER_SEARCH_BOOK;
            strcpy(req.data, kw);
        }
        else if (cmd == 2) {
            // 대출 신청
            int bno, type;
            printf("도서 NO: "); scanf("%d", &bno);
            printf("0=현장,1=온라인: "); scanf("%d", &type);
            req.code = REQ_USER_REQUEST_RENT;
            snprintf(req.data, sizeof(req.data), "%d:%s:%d", bno, uid, type);
        }
        else if (cmd == 3) {
            // 내 대출 목록
            req.code = REQ_USER_LIST_RENTS;
            strcpy(req.data, uid);
        }
        else if (cmd == 4) {
            // 반납
            int no; printf("반납할 NO: "); scanf("%d", &no);
            req.code = REQ_USER_RETURN_BOOK;
            snprintf(req.data, sizeof(req.data), "%d", no);
        }
        else if (cmd == 5) {
            // 내 정보 수정
            cJSON* o = cJSON_CreateObject();
            char buf[200];
            cJSON_AddStringToObject(o, "ID", uid);
            printf("새 전화번호: "); scanf("%199s", buf); cJSON_AddStringToObject(o,"PHONE",buf);
            printf("새 주소: ");     scanf("%199s", buf); cJSON_AddStringToObject(o,"ADDRESS",buf);
            char* s = cJSON_PrintUnformatted(o);
            req.code = REQ_USER_EDIT_PROFILE;
            strcpy(req.data, s);
            free(s); cJSON_Delete(o);
        }
        else if (cmd == 0) {
            printf("종료");
            break;
        }
        else {
            printf("잘못된 명령입니다\n");
            continue;
        }

        // 요청 전송 & 응답 수신
        send_req(sock, &req);
        recv_res(sock, &res);
        if (res.status == RES_OK)    
        printf("[성공] %s\n", res.data);
        else                          
        printf("[오류] %s\n", res.data);
    }
}

