#include <stdio.h>
#include <stdlib.h>
#include "/home/iot122/Downloads/cJSON-master/cJSON.h"
#include "/home/iot122/Downloads/cJSON-master/cJSON.c"

// JSON 파일을 읽어 문자열로 변환하는 함수
char *read_json_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("파일을 열 수 없습니다: %s\n", filename);
        return NULL;
    }

    // 파일 크기 확인
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    // 메모리 할당 및 파일 읽기
    char *json_data = (char *)malloc(file_size + 1);
    if (!json_data) {
        printf("메모리 할당 실패\n");
        fclose(file);
        return NULL;
    }

    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0';  // 문자열 종료 문자 추가

    fclose(file);
    return json_data;
}

int main() {
    // JSON 파일 읽기
    char *json_string = read_json_file("data.json");

    // printf ("%s",json_string);
    if (!json_string) {
        return 1;
    }
    
    // JSON 문자열을 cJSON 객체로 변환
    cJSON *json = cJSON_Parse(json_string);
    if (!json) {
        printf("JSON 파싱 실패\n");
        free(json_string);
        return 1;
    }


    // 개별 데이터 추출
    cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "No");
    cJSON *age = cJSON_GetObjectItemCaseSensitive(json, "제목");
    cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "저자");
    cJSON *skills = cJSON_GetObjectItemCaseSensitive(json, "출판사");

    printf("%s\n",json_string);
    

    // JSON 데이터 출력
    if (cJSON_IsNumber(name) && name->valueint) {
        printf("No: %d\n", name->valueint);
    }
    if (cJSON_IsString(age)&& age->valuestring) {
        printf("제목: %d\n", age->valuestring);
    }
    if (cJSON_IsString(email) && email->valuestring) {
        printf("저자: %s\n", email->valuestring);
    }

    // // JSON 배열 (skills) 출력
    // if (cJSON_IsArray(skills)) {
    //     int skill_count = cJSON_GetArraySize(skills);
    //     printf("기술 스택: ");
    //     for (int i = 0; i < skill_count; i++) {
    //         cJSON *skill = cJSON_GetArrayItem(skills, i);
    //         if (cJSON_IsString(skill) && skill->valuestring) {
    //             printf("%s ", skill->valuestring);
    //         }
    //     }
    //     printf("\n");
    // }

    // 메모리 해제
    cJSON_Delete(json);
    free(json_string);

    return 0;
}

