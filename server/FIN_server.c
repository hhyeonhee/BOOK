// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "/home/iot122/Downloads/cJSON-master/cJSON.c"
#include "/home/iot122/Downloads/cJSON-master/cJSON.h"

//— 매크로 정의 —————————————————————————————————————————————
#define SERVER_PORT   9999               // 서버 고정 포트
#define SERVER_IP     "10.10.20.122"     // 서버 허용 IP
#define RESET         0                  // 초기화 값
#define BUF_SIZE      1024               // 버퍼 크기

//— 요청 코드 ———————————————————————————————————————————————
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
    char data[512];      // 요청 부가 데이터
} Request;

typedef struct {
    int status;          // 응답 상태 (RES_OK/RES_ERR)
    char data[512];      // 응답 메시지 또는 JSON
} Response;

//— 도메인 구조체 —————————————————————————————————————————————
typedef struct {
    int no;
    char id[50];
    char pw[50];
    int birth;
    char name[100];
    char phone[20];
    char address[200];
    int exist;
    int fault;
    int  role;    // 0=관리자, 1=사서, 2=일반사용자
} User;

typedef struct {
    int no;
    char isbn[50];
    char title[200];
    char author[200];
    char publisher[200];
    int year;
    int count;
    int exist;
    int loanable; // 1=대출가능,0=불가
} Book;

typedef struct {
    int no;
    char isbn[50];
    char user_id[50];
    time_t start_time;
    time_t deadline;
    int status;    // 0=진행중,1=반납,2=연체,3=신청보류
    int type;      // 0=현장,1=온라인
} Rent;

typedef struct {
    int start_hour;
    int close_hour;
    char holiday[20];
} TimeCfg;

//— 함수 프로토타입 ———————————————————————————————————————————
// 서버 초기화 & 클라이언트 수락
int  init_server_socket(void);
int  accept_client(int serv_sock);
// 요청/응답 입출력
int  recv_request(int sock, Request* req);
int  send_response(int sock, const Response* res);
// JSON I/O: *.json 파일 ↔ 구조체 배열
int  load_users(User** users, int* cnt);
void save_users(const User* users, int cnt);
int  load_books(Book** books, int* cnt);
void save_books(const Book* books, int cnt);
int  load_rents(Rent** rents, int* cnt);
void save_rents(const Rent* rents, int cnt);
int  load_timecfg(TimeCfg* cfg);
void save_timecfg(const TimeCfg* cfg);
// 클라이언트 쓰레드 핸들러
void* client_thread(void* arg);
// 역할별 처리 함수
void  handle_admin(int sock);
void  handle_librarian(int sock);
void  handle_user(int sock);

//— main —————————————————————————————————————————————————————————
int main(void) {
    int serv_sock = init_server_socket();
    if (serv_sock < RESET) {
        perror("서버 소켓 생성 실패");
        exit(EXIT_FAILURE);
    }
    printf("서버 실행: IP=%s, PORT=%d\n", SERVER_IP, SERVER_PORT);

    // 무한 수락 루프
    while (1) {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept_client(serv_sock);
        if (*client_sock < RESET) {
            perror("클라이언트 수락 실패");
            free(client_sock);
            continue;
        }
        // 각 클라이언트를 별도 스레드로 처리
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, client_sock) != RESET) {
            perror("스레드 생성 실패");
            free(client_sock);
            continue;
        }
        pthread_detach(tid);
    }
    return RESET;
}

//— 서버 소켓 초기화 —————————————————————————————————————————————
int init_server_socket(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < RESET) {
        perror("socket() 실패");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, RESET, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < RESET) {
        perror("bind() 실패");
        close(sock);
        return -1;
    }
    if (listen(sock, 5) < RESET) {
        perror("listen() 실패");
        close(sock);
        return -1;
    }
    return sock;
}

//— 클라이언트 연결 수락 ———————————————————————————————————————————
int accept_client(int serv_sock) {
    struct sockaddr_in cli_addr;
    socklen_t len = sizeof(cli_addr);
    int sock = accept(serv_sock, (struct sockaddr*)&cli_addr, &len);
    if (sock < RESET) return -1;
    return sock;
}

//— 요청 수신 —————————————————————————————————————————————————————
int recv_request(int sock, Request* req) {
    int r = read(sock, req, sizeof(Request));
    if (r < sizeof(Request)) return -1;
    return 0;
}

//— 응답 전송 —————————————————————————————————————————————————————
int send_response(int sock, const Response* res) {
    int w = write(sock, res, sizeof(Response));
    if (w < sizeof(Response)) return -1;
    return 0;
}

//— 클라이언트 쓰레드 핸들러 ———————————————————————————————————————
void* client_thread(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    Request req; Response res;

    // 1) 로그인 처리
    if (recv_request(sock, &req) < RESET || req.code != REQ_LOGIN) {
        close(sock);
        return NULL;
    }
    char id[50], pw[50];
    sscanf(req.data, "%49[^:]:%49s", id, pw);

    // 2) 사용자 인증
    User *users = NULL; int ucnt = RESET;
    if (load_users(&users, &ucnt) < RESET) {
        res.status = RES_ERR;
        strcpy(res.data, "내부 오류: 사용자 로드 실패");
        send_response(sock, &res);
        close(sock);
        return NULL;
    }
    int is_admin = RESET, is_lib = RESET, found = RESET;
    for (int i = 0; i < ucnt; i++) {
        if (users[i].exist == 1 &&
            strcmp(users[i].id, id) == 0 &&
            strcmp(users[i].pw, pw) == 0)
        {
            found = 1;
            if (strcmp(users[i].id, "admin") == 0) {
                is_admin = 1;
            }
            else if (strcmp(users[i].id, "librarian") == 0) {
                is_lib = 1;
            }
            break;
        }
    }
    free(users);

    // 로그인 실패
    if (found == RESET) {
        res.status = RES_ERR;
        strcpy(res.data, "로그인 실패: 아이디/비밀번호 확인");
        send_response(sock, &res);
        close(sock);
        return NULL;
    }

    // 관리자라면 IP 검사
    if (is_admin == 1) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        getpeername(sock, (struct sockaddr*)&peer, &plen);
        char *client_ip = inet_ntoa(peer.sin_addr);
        if (strcmp(client_ip, "127.0.0.1") != 0 &&
            strcmp(client_ip, SERVER_IP)   != 0)
        {
            res.status = RES_ERR;
            strcpy(res.data, "관리자 계정은 서버에서만 접속 가능");
            send_response(sock, &res);
            close(sock);
            return NULL;
        }
    }

    // 3) 역할 통보
    res.status = RES_OK;
    if (is_admin == 1)       strcpy(res.data, "admin");
    else if (is_lib == 1)    strcpy(res.data, "librarian");
    else                      strcpy(res.data, "user");
    send_response(sock, &res);

    // 4) 역할별 처리
    if (is_admin == 1)           handle_admin(sock);
    else if (is_lib == 1)        handle_librarian(sock);
    else                          handle_user(sock);

    close(sock);
    return NULL;
}

//— 관리자 기능 처리 —————————————————————————————————————————————
void handle_admin(int sock) {
    Request req; Response res;
    while (recv_request(sock, &req) == 0) {
        // 요청 코드 분기
        switch (req.code) {
            case REQ_ADMIN_ADD_USER: {
                // JSON → 구조체 → 저장
                cJSON *o = cJSON_Parse(req.data);
                User u = {RESET};
                strcpy(u.id,      cJSON_GetObjectItem(o,"ID")->valuestring);
                strcpy(u.pw,      cJSON_GetObjectItem(o,"PW")->valuestring);
                u.birth   = cJSON_GetObjectItem(o,"BIRTH")->valueint;
                strcpy(u.name,    cJSON_GetObjectItem(o,"NAME")->valuestring);
                strcpy(u.phone,   cJSON_GetObjectItem(o,"PHONE")->valuestring);
                strcpy(u.address, cJSON_GetObjectItem(o,"ADDRESS")->valuestring);
                u.exist   = 1; u.fault = 0;
                cJSON_Delete(o);

                User *arr=NULL; int cnt=RESET;
                load_users(&arr, &cnt);
                arr = realloc(arr, sizeof(User)*(cnt+1));
                u.no = cnt; arr[cnt] = u; cnt++;
                save_users(arr, cnt);
                free(arr);

                res.status = RES_OK;
                strcpy(res.data, "사용자 추가 완료");
                break;
            }
            case REQ_ADMIN_DEL_USER: {
                int no = atoi(req.data);
                User *arr=NULL; int cnt=RESET;
                load_users(&arr, &cnt);
                int ok=RESET;
                for (int i=0; i<cnt; i++){
                    if (arr[i].no==no){ arr[i].exist=0; ok=1; break; }
                }
                if (ok==1){ save_users(arr,cnt); res.status=RES_OK; strcpy(res.data,"사용자 삭제 완료"); }
                else      { res.status=RES_ERR; strcpy(res.data,"해당 사용자 없음"); }
                free(arr);
                break;
            }
            case REQ_ADMIN_LIST_USERS: {
                User *arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                cJSON *a=cJSON_CreateArray();
                for (int i=0; i<cnt; i++){
                    if (arr[i].exist==1){
                        cJSON *o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"ID",arr[i].id);
                        cJSON_AddStringToObject(o,"NAME",arr[i].name);
                        cJSON_AddNumberToObject(o,"FAULT",arr[i].fault);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char *s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_ADMIN_ADD_BOOK: {
                cJSON *o=cJSON_Parse(req.data);
                Book b={RESET};
                strcpy(b.isbn,     cJSON_GetObjectItem(o,"ISBN")->valuestring);
                strcpy(b.title,    cJSON_GetObjectItem(o,"TITLE")->valuestring);
                strcpy(b.author,   cJSON_GetObjectItem(o,"AUTHOR")->valuestring);
                strcpy(b.publisher,cJSON_GetObjectItem(o,"PUBLISHER")->valuestring);
                b.year     = cJSON_GetObjectItem(o,"YEAR")->valueint;
                b.count    = cJSON_GetObjectItem(o,"COUNT")->valueint;
                b.exist    = 1; b.loanable=1;
                cJSON_Delete(o);

                Book *arr=NULL; int cnt=RESET;
                load_books(&arr,&cnt);
                arr=realloc(arr,sizeof(Book)*(cnt+1));
                b.no=cnt; arr[cnt]=b; cnt++;
                save_books(arr,cnt);
                free(arr);

                res.status=RES_OK;
                strcpy(res.data,"도서 추가 완료");
                break;
            }
            case REQ_ADMIN_DEL_BOOK: {
                int no=atoi(req.data);
                Book *arr=NULL; int cnt=RESET;
                load_books(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no){ arr[i].exist=0; ok=1; break; }
                }
                if(ok==1){ save_books(arr,cnt); res.status=RES_OK; strcpy(res.data,"도서 삭제 완료"); }
                else    { res.status=RES_ERR; strcpy(res.data,"해당 도서 없음"); }
                free(arr);
                break;
            }
            case REQ_ADMIN_LIST_BOOKS: {
                Book *arr=NULL; int cnt=RESET;
                load_books(&arr,&cnt);
                cJSON *a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(arr[i].exist==1){
                        cJSON *o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"ISBN",arr[i].isbn);
                        cJSON_AddStringToObject(o,"TITLE",arr[i].title);
                        cJSON_AddNumberToObject(o,"COUNT",arr[i].count);
                        cJSON_AddNumberToObject(o,"LOANABLE",arr[i].loanable);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_ADMIN_SET_TIME: {
                cJSON *o=cJSON_Parse(req.data);
                TimeCfg cfg={RESET};
                cfg.start_hour=cJSON_GetObjectItem(o,"START")->valueint;
                cfg.close_hour=cJSON_GetObjectItem(o,"CLOSE")->valueint;
                strcpy(cfg.holiday,cJSON_GetObjectItem(o,"HOLIDAY")->valuestring);
                cJSON_Delete(o);
                save_timecfg(&cfg);
                res.status=RES_OK;
                strcpy(res.data,"운영 시간 설정 완료");
                break;
            }
            case REQ_ADMIN_LIST_FAULTY: {
                User *arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                cJSON *a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(arr[i].exist==1 && arr[i].fault==1){
                        cJSON*o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"ID",arr[i].id);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_ADMIN_CLEAR_FAULTY: {
                int no=atoi(req.data);
                User*arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no){ arr[i].fault=0; ok=1; break; }
                }
                if(ok==1){ save_users(arr,cnt); res.status=RES_OK; strcpy(res.data,"불량대출자 해제 완료"); }
                else     { res.status=RES_ERR; strcpy(res.data,"해당 대출자 없음"); }
                free(arr);
                break;
            }
            default:
                res.status=RES_ERR;
                strcpy(res.data,"알 수 없는 관리자 요청");
        }
        send_response(sock, &res);
    }
}

//— 사서 기능 처리 —————————————————————————————————————————————
void handle_librarian(int sock) {
    Request req; Response res;
    while (recv_request(sock, &req) == 0) {
        switch(req.code) {
            case REQ_LIB_LIST_PENDING: {
                Rent*arr=NULL; int cnt=RESET;
                load_rents(&arr,&cnt);
                cJSON*a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(arr[i].status==3){
                        cJSON*o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"ISBN",arr[i].isbn);
                        cJSON_AddStringToObject(o,"USER",arr[i].user_id);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_LIB_APPROVE_RENT: {
                int no = atoi(req.data);
                Rent* rarr; int rcnt;
                load_rents(&rarr, &rcnt);
                int idx = -1;
                for(int i=0; i<rcnt; i++) {
                    if(rarr[i].no==no && rarr[i].status==3) {
                        rarr[i].status     = 0;
                        rarr[i].start_time = time(NULL);
                        rarr[i].deadline   = rarr[i].start_time + 10*24*3600;
                        idx = i;
                        break;
                    }
                }
                if(idx >= 0) {
                    save_rents(rarr, rcnt);
                    // 도서 수량 차감 및 loanable 업데이트
                    Book* barr; int bcnt;
                    load_books(&barr, &bcnt);
                    for(int j=0; j<bcnt; j++) {
                        if(strcmp(barr[j].isbn, rarr[idx].isbn)==0) {
                            barr[j].count--;
                            if(barr[j].count <= 0) barr[j].loanable = 0;
                            break;
                        }
                    }
                    save_books(barr, bcnt);
                    free(barr);
                    res.status = RES_OK;
                    strcpy(res.data, "대출 승인 완료");
                } else {
                    res.status = RES_ERR;
                    strcpy(res.data, "승인할 신청 없음");
                }
                free(rarr);
                break;
            }
            case REQ_LIB_REJECT_RENT: {
                int no=atoi(req.data);
                Rent*arr=NULL; int cnt=RESET;
                load_rents(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no && arr[i].status==3){
                        arr[i].status=2;
                        ok=1; break;
                    }
                }
                if(ok==1){ save_rents(arr,cnt); res.status=RES_OK; strcpy(res.data,"대출 거절 완료"); }
                else    { res.status=RES_ERR; strcpy(res.data,"거절할 신청 없음"); }
                free(arr);
                break;
            }
            case REQ_LIB_SET_LOANABLE: {
                int no,flag;
                sscanf(req.data,"%d:%d",&no,&flag);
                Book*arr=NULL; int cnt=RESET;
                load_books(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no){
                        arr[i].loanable=flag;
                        ok=1; break;
                    }
                }
                if(ok==1){ save_books(arr,cnt); res.status=RES_OK; strcpy(res.data,"대출 가능 상태 변경 완료"); }
                else    { res.status=RES_ERR; strcpy(res.data,"해당 도서 없음"); }
                free(arr);
                break;
            }
            case REQ_LIB_LIST_FAULTY: {
                User*arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                cJSON*a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(arr[i].exist==1 && arr[i].fault==1){
                        cJSON*o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"ID",arr[i].id);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_LIB_CLEAR_FAULTY: {
                int no=atoi(req.data);
                User*arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no){ arr[i].fault=0; ok=1; break; }
                }
                if(ok==1){ save_users(arr,cnt); res.status=RES_OK; strcpy(res.data,"불량대출자 해제 완료"); }
                else    { res.status=RES_ERR; strcpy(res.data,"해당 대출자 없음"); }
                free(arr);
                break;
            }
            default:
                res.status=RES_ERR;
                strcpy(res.data,"알 수 없는 사서 요청");
        }
        send_response(sock, &res);
    }
}

//— 일반 사용자 기능 처리 —————————————————————————————————————————————
void handle_user(int sock) {
    Request req; Response res;
    while (recv_request(sock, &req) == 0) {
        switch (req.code) {
            case REQ_USER_SEARCH_BOOK: {
                char keyword[512];
                strcpy(keyword, req.data);
                Book*arr=NULL; int cnt=RESET;
                load_books(&arr,&cnt);
                cJSON*a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(arr[i].exist==1 && strstr(arr[i].title,keyword)){
                        cJSON*o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddStringToObject(o,"TITLE",arr[i].title);
                        cJSON_AddNumberToObject(o,"COUNT",arr[i].count);
                        cJSON_AddNumberToObject(o,"LOANABLE",arr[i].loanable);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_USER_REQUEST_RENT: {
                int bno,type; char uid[50];
                sscanf(req.data,"%d:%49[^:]:%d",&bno,uid,&type);
                Book*barr=NULL; int bcnt=RESET;
                load_books(&barr,&bcnt);
                int ok=RESET;
                for(int i=0;i<bcnt;i++){
                    if(barr[i].no==bno){
                        if(barr[i].loanable==0) ok=-1;
                        else ok=1;
                        break;
                    }
                }
                free(barr);
                if(ok<0){ res.status=RES_ERR; strcpy(res.data,"도서 대출 불가"); break; }
                if(ok==0){ res.status=RES_ERR; strcpy(res.data,"도서 없음"); break; }

                Rent r={RESET};
                strcpy(r.user_id, uid);
                r.type=type; r.status=3;
                r.start_time=RESET; r.deadline=RESET;
                strcpy(r.isbn,""); // 실제 ISBN 채우려면 추가 로직 필요

                Rent*arr=NULL; int cnt=RESET;
                load_rents(&arr,&cnt);
                r.no=cnt;
                arr=realloc(arr,sizeof(Rent)*(cnt+1));
                arr[cnt++]=r;
                save_rents(arr,cnt);
                free(arr);

                res.status=RES_OK;
                strcpy(res.data,"대출 신청 접수됨");
                break;
            }
            case REQ_USER_LIST_RENTS: {
                char uid[50]; strcpy(uid, req.data);
                Rent*arr=NULL; int cnt=RESET;
                load_rents(&arr,&cnt);
                cJSON*a=cJSON_CreateArray();
                for(int i=0;i<cnt;i++){
                    if(strcmp(arr[i].user_id,uid)==0){
                        cJSON*o=cJSON_CreateObject();
                        cJSON_AddNumberToObject(o,"NO",arr[i].no);
                        cJSON_AddNumberToObject(o,"STATUS",arr[i].status);
                        cJSON_AddNumberToObject(o,"TYPE",arr[i].type);
                        cJSON_AddItemToArray(a,o);
                    }
                }
                free(arr);
                char*s=cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                res.status=RES_OK;
                strncpy(res.data,s,sizeof(res.data)-1);
                free(s);
                break;
            }
            case REQ_USER_RETURN_BOOK: {
                int no=atoi(req.data);
                Rent*arr=NULL; int cnt=RESET;
                load_rents(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(arr[i].no==no && arr[i].status!=1){
                        arr[i].status=1; ok=1; break;
                    }
                }
                if(ok==1){ save_rents(arr,cnt); res.status=RES_OK; strcpy(res.data,"반납 처리 완료"); }
                else     { res.status=RES_ERR; strcpy(res.data,"반납할 기록 없음"); }
                free(arr);
                break;
            }
            case REQ_USER_EDIT_PROFILE: {
                cJSON*o=cJSON_Parse(req.data);
                char uid[50],ph[50],ad[200];
                strcpy(uid, cJSON_GetObjectItem(o,"ID")->valuestring);
                strcpy(ph,  cJSON_GetObjectItem(o,"PHONE")->valuestring);
                strcpy(ad,  cJSON_GetObjectItem(o,"ADDRESS")->valuestring);
                cJSON_Delete(o);

                User*arr=NULL; int cnt=RESET;
                load_users(&arr,&cnt);
                int ok=RESET;
                for(int i=0;i<cnt;i++){
                    if(strcmp(arr[i].id,uid)==0){
                        strcpy(arr[i].phone,ph);
                        strcpy(arr[i].address,ad);
                        ok=1; break;
                    }
                }
                if(ok==1){ save_users(arr,cnt); res.status=RES_OK; strcpy(res.data,"정보 수정 완료"); }
                else     { res.status=RES_ERR; strcpy(res.data,"사용자 없음"); }
                free(arr);
                break;
            }
            default:
                res.status=RES_ERR;
                strcpy(res.data,"알 수 없는 요청");
        }
        send_response(sock, &res);
    }
}

//— JSON I/O 구현 —————————————————————————————————————————————
// user.json ↔ User[]
int load_users(User** users, int* cnt) {
    FILE* f = fopen("user.json","r");
    if (!f) return -1;
    fseek(f,0,SEEK_END); long len = ftell(f); rewind(f);
    char *js = malloc(len+1); fread(js,1,len,f); js[len]=0; fclose(f);
    cJSON *a = cJSON_Parse(js); free(js);
    if (!a || !cJSON_IsArray(a)) return -1;
    int n = cJSON_GetArraySize(a);
    *users = malloc(sizeof(User)*n); *cnt = n;
    for(int i=0;i<n;i++){
        cJSON *o = cJSON_GetArrayItem(a,i);
        User u = {RESET};
        u.no    = cJSON_GetObjectItem(o,"NO")->valueint;
        strcpy(u.id, cJSON_GetObjectItem(o,"ID")->valuestring);
        strcpy(u.pw, cJSON_GetObjectItem(o,"PW")->valuestring);
        u.birth = cJSON_GetObjectItem(o,"BIRTH")->valueint;
        strcpy(u.name,    cJSON_GetObjectItem(o,"NAME")->valuestring);
        strcpy(u.phone,   cJSON_GetObjectItem(o,"PHONE")->valuestring);
        strcpy(u.address, cJSON_GetObjectItem(o,"ADDRESS")->valuestring);
        u.exist = cJSON_GetObjectItem(o,"EXIST")->valueint;
        u.fault = cJSON_GetObjectItem(o,"FAUL")->valueint;
        (*users)[i] = u;
    }
    cJSON_Delete(a);
    return 0;
}

void save_users(const User* users, int cnt) {
    cJSON *a=cJSON_CreateArray();
    for(int i=0;i<cnt;i++){
        cJSON *o=cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"NO",   users[i].no);
        cJSON_AddStringToObject(o,"ID",   users[i].id);
        cJSON_AddStringToObject(o,"PW",   users[i].pw);
        cJSON_AddNumberToObject(o,"BIRTH",users[i].birth);
        cJSON_AddStringToObject(o,"NAME", users[i].name);
        cJSON_AddStringToObject(o,"PHONE",users[i].phone);
        cJSON_AddStringToObject(o,"ADDRESS",users[i].address);
        cJSON_AddNumberToObject(o,"EXIST",users[i].exist);
        cJSON_AddNumberToObject(o,"FAUL", users[i].fault);
        cJSON_AddItemToArray(a,o);
    }
    char *s=cJSON_PrintUnformatted(a);
    FILE* f=fopen("user.json","w");
    if(f){ fwrite(s,1,strlen(s),f); fclose(f); }
    free(s); cJSON_Delete(a);
}

// book.json ↔ Book[]
int load_books(Book** books, int* cnt) {
    FILE* f=fopen("book.json","r"); if(!f) return -1;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
    char*js=malloc(len+1); fread(js,1,len,f); js[len]=0; fclose(f);
    cJSON*a=cJSON_Parse(js); free(js);
    if(!a||!cJSON_IsArray(a))return -1;
    int n=cJSON_GetArraySize(a);
    *books=malloc(sizeof(Book)*n); *cnt=n;
    for(int i=0;i<n;i++){
        cJSON*o=cJSON_GetArrayItem(a,i);
        Book b={RESET};
        b.no       = cJSON_GetObjectItem(o,"NO")->valueint;
        strcpy(b.isbn,     cJSON_GetObjectItem(o,"ISBN")->valuestring);
        strcpy(b.title,    cJSON_GetObjectItem(o,"TITLE")->valuestring);
        strcpy(b.author,   cJSON_GetObjectItem(o,"AUTHOR")->valuestring);
        strcpy(b.publisher,cJSON_GetObjectItem(o,"PUBLISHER")->valuestring);
        b.year     = cJSON_GetObjectItem(o,"YEAR")->valueint;
        b.count    = cJSON_GetObjectItem(o,"COUNT")->valueint;
        b.exist    = cJSON_GetObjectItem(o,"EXIST")->valueint;
        b.loanable = cJSON_GetObjectItem(o,"LOANABLE")->valueint;
        (*books)[i]=b;
    }
    cJSON_Delete(a); return 0;
}

void save_books(const Book* books, int cnt){
    cJSON*a=cJSON_CreateArray();
    for(int i=0;i<cnt;i++){
        cJSON*o=cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"NO",books[i].no);
        cJSON_AddStringToObject(o,"ISBN",books[i].isbn);
        cJSON_AddStringToObject(o,"TITLE",books[i].title);
        cJSON_AddStringToObject(o,"AUTHOR",books[i].author);
        cJSON_AddStringToObject(o,"PUBLISHER",books[i].publisher);
        cJSON_AddNumberToObject(o,"YEAR",books[i].year);
        cJSON_AddNumberToObject(o,"COUNT",books[i].count);
        cJSON_AddNumberToObject(o,"EXIST",books[i].exist);
        cJSON_AddNumberToObject(o,"LOANABLE",books[i].loanable);
        cJSON_AddItemToArray(a,o);
    }
    char*s=cJSON_PrintUnformatted(a);
    FILE*f=fopen("book.json","w");
    if(f){ fwrite(s,1,strlen(s),f); fclose(f); }
    free(s); cJSON_Delete(a);
}

// rent.json ↔ Rent[]
int load_rents(Rent** rents, int* cnt) {
    FILE* f = fopen("rent.json","r"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long len = ftell(f); rewind(f);
    char* js = malloc(len+1); fread(js,1,len,f); js[len]=0; fclose(f);
    cJSON* a = cJSON_Parse(js); free(js);
    if (!a || !cJSON_IsArray(a)) return -1;
    int n = cJSON_GetArraySize(a);
    *rents = malloc(sizeof(Rent)*n); *cnt = n;
    for(int i=0;i<n;i++){
        cJSON* o = cJSON_GetArrayItem(a,i);
        Rent r = {RESET};
        r.no         = cJSON_GetObjectItem(o,"NO")->valueint;
        strcpy(r.isbn,     cJSON_GetObjectItem(o,"ISBN")->valuestring);
        strcpy(r.user_id,  cJSON_GetObjectItem(o,"ID")->valuestring);
        r.deadline   = cJSON_GetObjectItem(o,"DEADLINE")->valueint;
        r.status     = cJSON_GetObjectItem(o,"STATUS")->valueint;
        r.type       = cJSON_GetObjectItem(o,"ONOFF")->valueint;
        (*rents)[i]  = r;
    }
    cJSON_Delete(a);
    return 0;
}


void save_rents(const Rent* rents, int cnt) {
    cJSON* a = cJSON_CreateArray();
    for(int i= RESET; i<cnt; i++) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"NO",        rents[i].no);
        cJSON_AddStringToObject(o,"ISBN",     rents[i].isbn);
        cJSON_AddStringToObject(o,"ID",       rents[i].user_id);
        cJSON_AddNumberToObject(o,"DEADLINE", rents[i].deadline);
        cJSON_AddNumberToObject(o,"STATUS",   rents[i].status);
        cJSON_AddNumberToObject(o,"ONOFF",    rents[i].type);
        cJSON_AddItemToArray(a, o);
    }
    char* s = cJSON_PrintUnformatted(a);
    FILE* f = fopen("rent.json","w");
    if(f) { fwrite(s,1,strlen(s),f); fclose(f); }
    free(s);
    cJSON_Delete(a);
}

// time.json ↔ TimeCfg 읽기
int load_timecfg(TimeCfg* cfg){
    FILE* f = fopen("time.json","r");
    if (!f) return -1;
    char buf[BUF_SIZE];
    fread(buf,1,BUF_SIZE,f);
    fclose(f);
    cJSON* o = cJSON_Parse(buf);
    if (!o) return -1;
    cfg->start_hour = cJSON_GetObjectItem(o,"start_time")->valueint;
    cfg->close_hour = cJSON_GetObjectItem(o,"close_time")->valueint;
    strcpy(cfg->holiday, cJSON_GetObjectItem(o,"holiday")->valuestring);
    cJSON_Delete(o);
    return 0;
}

// TimeCfg 구조체를 time.json 에 쓰기
void save_timecfg(const TimeCfg* cfg){
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o,"start_time", cfg->start_hour);
    cJSON_AddNumberToObject(o,"close_time", cfg->close_hour);
    cJSON_AddStringToObject(o,"holiday", cfg->holiday);
    char* s = cJSON_PrintUnformatted(o);
    FILE* f = fopen("time.json","w");
    if (f) { fwrite(s,1,strlen(s),f); fclose(f); }
    free(s);
    cJSON_Delete(o);
}
