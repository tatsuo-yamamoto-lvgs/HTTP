#include <sys/socket.h>
#include <netinet/in.h>	
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define SIZE (5*1024)

int httpServer(int, char*);
int recvRequestMessage(int, char*, unsigned int);
int parseRequestMessage(char**, char**, char*, char*, int);
int getProcessing(char**, char*);
void savePostData(int,char*,int);
int createResponseMessage(char**, int, char*, char*, unsigned int, char*);
int sendResponseMessage(int, char*, unsigned int);
unsigned int getFileSize(const char*);
void setHeaderFiled(char**,char*,unsigned int);
void *handle_request(void*);

typedef struct {
    int c_sock;
    char *root_path;
} thread_args;

void *handle_request(void *arg) {
    thread_args *args = (thread_args *) arg;

    /* 接続済のソケットでデータのやり取り */
    httpServer(args->c_sock, args->root_path);

    /* ソケット通信をクローズ */
    close(args->c_sock);
    free(arg);

    pthread_exit(NULL);
}

/* ファイルサイズを取得する */
unsigned int getFileSize(const char *path) {
    int size, read_size;
    char read_buf[SIZE];
    FILE *f;

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }

    size = 0;
    do {
        read_size = fread(read_buf, 1, SIZE, f);
        size += read_size;
    } while(read_size != 0);

    fclose(f);

    return size;
}

/*
 * リクエストメッセージを受信する
 * sock：接続済のソケット
 * request_message：リクエストメッセージを格納するバッファへのアドレス
 * buf_size：そのバッファのサイズ
 * 戻り値：受信したデータサイズ（バイト長）
 */
int recvRequestMessage(int sock, char *request_message, unsigned int buf_size) {
    int recv_size;
    int total_recv_size = 0;
    
    while(1){
        recv_size = recv(sock, request_message + total_recv_size, buf_size, 0);
        total_recv_size += recv_size;
        request_message[total_recv_size] = '\0';
        if (strstr(request_message, "\r\n\r\n")){
            break;
        }
    }


    return total_recv_size;
}

/*
 * リクエストメッセージを解析する（今回はリクエスト行のみ）
 * method：メソッドを格納するバッファへのアドレス
 * target：リクエストターゲットを格納するバッファへのアドレス
 * request_message：解析するリクエストメッセージが格納されたバッファへのアドレス
 * 戻り値：成功時は0、失敗時は-1
 */
int parseRequestMessage(char **method, char **target, char *request_message, char *root_path, int request_size) {

    char *line;
    char *tmp_method;
    char *tmp_target;
    char tmp_request_message[SIZE];//request_messageが固定長なので、ここでも固定長
    
    /* リクエストメッセージの１行目のみ取得 */
    memcpy(tmp_request_message, request_message, request_size);

    line = strtok(tmp_request_message, "\n");

    /* " "までの文字列を取得しmethodにコピー */
    tmp_method = strtok(line, " ");
    if (tmp_method == NULL) {
        printf("get method error\n");
        return -1;
    }
    *method = malloc(strlen(tmp_method));
    strcpy(*method, tmp_method);

    /* 次の" "までの文字列を取得しtargetにコピー */
    tmp_target = strtok(NULL, " ");
    // if (tmp_target == NULL) {
    //     printf("get target error\n");
    //     return -1;
    // }
    if(strcmp(tmp_target, "/") == 0) {
        strcpy(tmp_target, "/index.html");
    }
    *target = malloc(strlen(root_path)+strlen(tmp_target));
    strcpy(*target, root_path);
    strcat(*target,tmp_target);
    printf("--------------------target:%s\n",*target);

    return 0;
}

/*
 * リクエストに対する処理を行う（今回はGETのみ）
 * body：ボディを格納するバッファへのアドレス
 * file_path：リクエストターゲットに対応するファイルへのパス
 * 戻り値：ステータスコード（ファイルがない場合は404）
 */
int getProcessing(char **body, char *file_path) {

    FILE *f;
    int file_size;

    file_size = getFileSize(file_path);
    *body = malloc(file_size);
    f = fopen(file_path, "rb");
    if (access(file_path, F_OK) != 0) {
        printf("File do not exist\n");
        return 404;
    } else if (access(file_path, R_OK) != 0){
        fclose(f);
        return 403;
    } else {
        fread(*body, 1, file_size, f);
        fclose(f);
        return 200;
    }
}

/* POST通信で送られてきたデータを保存する */
void savePostData(int sock, char *request_header, int recv_size){
    char *boundary;
    char *delimiter = "\r\n\r\n";
    char *request_message;
    char *request_body;
    char *file_name;

    /* 読み取れていないデータがある場合、request_messageにデータを追加する */
    char *content_length_pos = strstr(request_header, "Content-Length: ");
    char *boundary_pos = strstr(request_header, "Content-Type: multipart/form-data; boundary=");
    if (boundary_pos) {
        char *boundary_value_first = boundary_pos + strlen("Content-Type: multipart/form-data; boundary=");
        char *boundary_value_last = strstr(boundary_pos,"\r\n\r\n");
        int boundary_len = boundary_value_last - boundary_value_first;
        boundary = malloc(boundary_len);
        sscanf(boundary_value_first, "%s", boundary);
    } else {
        printf("Cannot find boundary.\n");
    }
    char *content_length_str;
    content_length_str = malloc(boundary_pos - content_length_pos -2);
    strncpy(content_length_str, content_length_pos + strlen("Content-Length: "), boundary_pos - content_length_pos -2);
    int content_length = atoi(content_length_str);
    char *blank_line_pos = strstr(request_header, "\r\n\r\n");
    size_t header_length = blank_line_pos - request_header;
    request_message = malloc(content_length + header_length + strlen("\r\n\r\n") + 1);

    memcpy(request_message, request_header, recv_size);
    int recieve_more_content_length = 0;
    int re_recv_size;
    while(1){
        if (content_length + header_length + strlen("\r\n\r\n")> SIZE){
            re_recv_size = recv(sock, request_message + recv_size + recieve_more_content_length, content_length + header_length + strlen("\r\n\r\n") - recv_size, 0);
        }
        recieve_more_content_length += re_recv_size;
        if (recieve_more_content_length == content_length + header_length + strlen("\r\n\r\n") - recv_size){
            break;
        }
    }
    char *start_boundary_pos = strstr(strstr(request_message, boundary) + strlen(boundary), boundary);
    char *end_boundary_pos = strstr(start_boundary_pos + strlen(boundary), boundary);
    char *request_file_name_pos = strstr(start_boundary_pos + strlen(boundary), "filename=");
    char *request_content_type_pos = strstr(start_boundary_pos + strlen(boundary), "Content-Type: ");
    char *delimiter_pos = strstr(start_boundary_pos + strlen(boundary), delimiter);
    size_t request_body_length = end_boundary_pos - delimiter_pos - strlen(delimiter) - 2;
    size_t file_name_length = request_content_type_pos - request_file_name_pos - strlen("filename=") - 2;
    file_name = malloc(file_name_length-2);
    strncpy(file_name, request_file_name_pos + strlen("filename=") + 1, file_name_length - 2); 
    file_name[file_name_length - 2] = '\0';
    request_body = delimiter_pos + strlen(delimiter);
    /* ここにfile nameを決める関数をかく */
    if (strstr(file_name, ".html") != NULL || strstr(file_name, ".css") != NULL || strstr(file_name, ".js") != NULL){
        FILE *file = fopen(file_name, "w");
        fwrite(request_body, sizeof(char), request_body_length, file);
        fclose(file);
    } else if(strstr(file_name, "jpg") != NULL || strstr(file_name, "png") != NULL){
        FILE *file = fopen(file_name, "wb");
        fwrite(request_body, sizeof(char), request_body_length, file);
        fclose(file);
    } else {
        printf("post message content-type error\n");
    }

    free(request_message);
    free(content_length_str);
    free(boundary);
    free(file_name);
}

/*
 * レスポンスメッセージを作成する
 * response_message：レスポンスメッセージを格納するバッファへのアドレス
 * status：ステータスコード
 * header：ヘッダーフィールドを格納したバッファへのアドレス
 * body：ボディを格納したバッファへのアドレス
 * body_size：ボディのサイズ
 * 戻り値：レスポンスメッセージのデータサイズ（バイト長）
 */
int createResponseMessage(char **response_message, int status, char *header, char *body, unsigned int body_size, char *method) {

    char *status_message = NULL;
    unsigned int header_len = strlen(header);
    unsigned int body_len = 0;

    switch (status) {
        case 200:
            status_message = "200 OK";
            if(strcmp(method, "HEAD") != 0) {
                if(strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0) {
                    body_len = body_size;
                }
            }
            break;
        case 404:
            status_message = "404 Not Found";
            break;
        case 403:
            status_message = "403 Forbidden";
            break;
        default:
            printf("Not support status(%d)\n", status);
            return -1;
    }

    unsigned int response_message_len = header_len + body_len + strlen("HTTP/1.1 ") + strlen(status_message) + strlen("\r\n") + strlen("\r\n") + 1;
    *response_message = (char *) malloc(response_message_len * sizeof(char));
    if(*response_message == NULL) {
        fprintf(stderr, "Failed to allocate memory.\n");
        exit(EXIT_FAILURE);
    }

    /* レスポンス行とヘッダーフィールドの文字列を作成 */
    sprintf(*response_message, "HTTP/1.1 %s\r\n%s\r\n", status_message, header);

    /* ヘッダーフィールドの後ろにボディをコピー */
    memcpy(*response_message + strlen(*response_message), body, body_len);

    return response_message_len;
}

/*
 * レスポンスメッセージを送信する
 * sock：接続済のソケット
 * response_message：送信するレスポンスメッセージへのアドレス
 * message_size：送信するメッセージのサイズ
 * 戻り値：送信したデータサイズ（バイト長）
 */
int sendResponseMessage(int sock, char *response_message, unsigned int message_size) {

    int send_size;
    
    send_size = send(sock, response_message, message_size, 0);

    return send_size;
}

void showMessage(char *message, unsigned int size) {
    
    unsigned int i;

    printf("Show Message\n\n");

    for (i = 0; i < size; i++) {
        putchar(message[i]);
    }
    printf("\n\n");
    
}

/*
 * クライアント側からのリクエストを元にヘッダーフィールドを作成する関数
 * 
 */
void setHeaderFiled(char **header_field, char *target, unsigned int file_size){
    
    char* type = NULL;
    struct ContentType {
        char *extension;
        char *mime_type;
    };
    struct ContentType content_types[] = {
        {"html", "text/html"},
        {"css", "text/css"},
        {"javascript", "text/javascript"},
        {"png", "image/png"},
        {"jpeg", "image/jpeg"}
    };
    size_t content_num = sizeof(content_types) / sizeof(content_types[0]);

    for(size_t i = 0; i < content_num; i++) {
        if(strstr(target, content_types[i].extension) != NULL) {
            type = content_types[i].mime_type;
            break;
        }
    }
    if(type == NULL) {
        type = "image/jpeg";
    }

    int required_size = snprintf(NULL, 0, "Content-Type: %s\r\nContent-Length: %u\r\n", type, file_size) + 1;
    *header_field = malloc(required_size * sizeof(char));
    snprintf(*header_field, required_size, "Content-Type: %s\r\nContent-Length: %u\r\n", type, file_size);
}

/*
 * HTTPサーバーの処理を行う関数
 * sock：接続済のソケット
 * 戻り値：0
 */
int httpServer(int sock, char *root_path) {

    int request_size, response_size;
    char request_message[SIZE];//固定長で良い。
    char *response_message;
    char *method;
    char *target;
    char *header_field;
    char *response_body;
    int status;
    unsigned int file_size;
    
    while (1) {

        /* リクエストメッセージを受信 */
        request_size = recvRequestMessage(sock, request_message, SIZE);
        if (request_size == -1) {
            printf("recvRequestMessage error\n");
            break;
        }
        if (request_size == 0) {
            /* 受信サイズが0の場合は相手が接続閉じていると判断 */
            printf("connection ended\n");
            break;
        }

        /* 受信した文字列を表示 */
        showMessage(request_message, request_size);
        
        /* 受信した文字列を解析してメソッドやリクエストターゲットを取得 */
        if (parseRequestMessage(&method, &target, request_message, root_path, request_size) == -1) {
            printf("parseRequestMessage error\n");
            break;
        }
        
        /* メソッドがGETまたはPOSTまたはHEAD以外はステータスコードを404にする */
        if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 || strcmp(method, "HEAD") == 0) {
            /* GETの応答をするために必要な処理を行う */
            status = getProcessing(&response_body, &target[1]);
        } else {
            status = 405;
        }
        
        /* POST通信時の受信ファイルを保存*/
        if (strcmp(method, "POST") == 0) {
            savePostData(sock, request_message, request_size);
        }

        /* ヘッダーフィールド作成*/
        file_size = getFileSize(&target[1]);
        setHeaderFiled(&header_field, target, file_size);

        /* レスポンスメッセージを作成 */
        response_size = createResponseMessage(&response_message, status, header_field, response_body, file_size, method);
        if (response_size == -1) {
            printf("createResponseMessage error\n");
            break;
        }

        /* 送信するメッセージを表示 */
        showMessage(response_message, response_size);

        /* レスポンスメッセージを送信する */
        sendResponseMessage(sock, response_message, response_size);
        
    }
    free(method);
    free(target);
    free(header_field);
    free(response_body);
    free(response_message);
    return 0;
}

int main(int argc, char *argv[]) {
    if(argc != 4 && argc != 3){
        printf("command line error\n");
        return -1;
    }
    
    char *server_addr = argv[1];
    int server_port = atoi(argv[2]);
    char *root_path;

    if(argc == 4){
        root_path = argv[3];
    }else{
        root_path = "";
    }
    
    int w_addr, c_sock;
    struct sockaddr_in a_addr;

    /* ソケットを作成 */
    w_addr = socket(AF_INET, SOCK_STREAM, 0);
    if (w_addr == -1) {
        printf("socket error\n");
        return -1;
    }
    int optval = 1;
    setsockopt(w_addr, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    /* 構造体を全て0にセット */
    memset(&a_addr, 0, sizeof(struct sockaddr_in));

    /* サーバーのIPアドレスとポートの情報を設定 */
    a_addr.sin_family = AF_INET;
    a_addr.sin_port = htons((unsigned short)server_port);
    a_addr.sin_addr.s_addr = inet_addr(server_addr);

    /* ソケットに情報を設定 */
    if (bind(w_addr, (const struct sockaddr *)&a_addr, sizeof(a_addr)) == -1) {
        printf("bind error\n");
        close(w_addr);
        return -1;
    }

    /* ソケットを接続待ちに設定 */
    if (listen(w_addr, 3) == -1) {
        printf("listen error\n");
        close(w_addr);
        return -1;
    }

    while (1) {
        /* 接続要求の受け付け（接続要求くるまで待ち） */
        printf("Waiting connect...\n");
        c_sock = accept(w_addr, NULL, NULL);
        if (c_sock == -1) {
            printf("accept error\n");
            close(w_addr);
            return -1;
        }
        printf("Connected!!\n");

        pthread_t thread;
        thread_args *args = malloc(sizeof(thread_args));
        args->c_sock = c_sock;
        args->root_path = root_path;
        pthread_create(&thread, NULL, handle_request, (void *)args);
    }

    /* 接続待ちソケットをクローズ */
    close(w_addr);

    return 0;
}
