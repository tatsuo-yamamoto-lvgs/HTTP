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
int parseRequestMessage(char*, char*, char*, char*, int);
int getProcessing(char*, char*);
void savePostData(int,char*,int);
int createResponseMessage(char*, int, char*, char*, unsigned int, char*);
int sendResponseMessage(int, char*, unsigned int);
unsigned int getFileSize(const char*);
void setHeaderFiled(char*,char*,unsigned int);
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
int parseRequestMessage(char *method, char *target, char *request_message, char *root_path, int request_size) {

    char *line;
    char *tmp_method;
    char *tmp_target;
    char tmp_request_message[SIZE];
    
    /* リクエストメッセージの１行目のみ取得 */
    memcpy(tmp_request_message, request_message, request_size);

    line = strtok(tmp_request_message, "\n");

    /* " "までの文字列を取得しmethodにコピー */
    tmp_method = strtok(line, " ");
    if (tmp_method == NULL) {
        printf("get method error\n");
        return -1;
    }
    strcpy(method, tmp_method);

    /* 次の" "までの文字列を取得しtargetにコピー */
    tmp_target = strtok(NULL, " ");
    if (tmp_target == NULL) {
        printf("get target error\n");
        return -1;
    }
    if(strcmp(tmp_target, "/") == 0) {
        strcpy(target, root_path);
        strcat(target, "/index.html");
    }else{
        strcpy(target, root_path);
        strcat(target,tmp_target);
    }

    return 0;
}

/*
 * リクエストに対する処理を行う（今回はGETのみ）
 * body：ボディを格納するバッファへのアドレス
 * file_path：リクエストターゲットに対応するファイルへのパス
 * 戻り値：ステータスコード（ファイルがない場合は404）
 */
int getProcessing(char *body, char *file_path) {

    FILE *f;
    int file_size;

    file_size = getFileSize(file_path);
    f = fopen(file_path, "rb");
    if (access(file_path, F_OK) != 0) {
        printf("File do not exist\n");
        return 404;
    } else if (access(file_path, R_OK) != 0){
        fclose(f);
        return 403;
    } else {
        fread(body, 1, file_size, f);
        fclose(f);
        return 200;
    }
}

/* POST通信で送られてきたデータを保存する */
void savePostData(int sock, char *request_header, int recv_size){
    char boundary[SIZE];
    char request_file_name[SIZE];
    char *delimiter = "\r\n\r\n";
    char *request_message;
    char *request_body;
    char file_name[SIZE];

    /* 読み取れていないデータがある場合、request_messageにデータを追加する */
    char *content_length_pos = strstr(request_header, "Content-Length: ");
    char *boundary_pos = strstr(request_header, "Content-Type: multipart/form-data; boundary=");
    if (boundary_pos) {
        char *boundary_value = boundary_pos + strlen("Content-Type: multipart/form-data; boundary=");
        sscanf(boundary_value, "%s", boundary);
    } else {
        printf("Cannot find boundary.\n");
    }
    char content_length_str[SIZE];
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
    strncpy(file_name, request_file_name_pos + strlen("filename=") + 1, file_name_length - 2); 
    file_name[file_name_length - 2] = '\0';
    request_body = delimiter_pos + strlen(delimiter);
    /* ここにfile nameを決める関数をかく */
    char preserve_name[SIZE];
    if (strstr(file_name, ".html") != NULL || strstr(file_name, ".css") != NULL || strstr(file_name, ".js") != NULL){
        strcpy(preserve_name, file_name);
        FILE *file = fopen(preserve_name, "w");
        fwrite(request_body, sizeof(char), request_body_length, file);
        fclose(file);
    } else if(strstr(file_name, "jpg") != NULL || strstr(file_name, "png") != NULL){
        strcpy(preserve_name,file_name);
        FILE *file = fopen(preserve_name, "wb");
        fwrite(request_body, sizeof(char), request_body_length, file);
        fclose(file);
    } else {
        printf("post message content-type error\n");
    }

    free(request_message);
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
int createResponseMessage(char *response_message, int status, char *header, char *body, unsigned int body_size, char *method) {

    unsigned int no_body_len;
    unsigned int body_len;

    response_message[0] = '\0';

    if (status == 200) {
        /* レスポンス行とヘッダーフィールドの文字列を作成 */
        sprintf(response_message, "HTTP/1.1 200 OK\r\n%s\r\n", header);

        no_body_len = strlen(response_message);
        if(strcmp(method, "HEAD") == 0){
            body_len = 0;
        } else if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0){
            body_len = body_size;
            /* ヘッダーフィールドの後ろにボディをコピー */
            memcpy(&response_message[no_body_len], body, body_len);
        }
    } else if (status == 404) {
        /* レスポンス行とヘッダーフィールドの文字列を作成 */
        sprintf(response_message, "HTTP/1.1 404 Not Found\r\n%s\r\n", header);

        no_body_len = strlen(response_message);
        body_len = 0;
    } else if (status == 403) {
        /* レスポンス行とヘッダーフィールドの文字列を作成 */
        sprintf(response_message, "HTTP/1.1 403 Forbidden\r\n%s\r\n", header);
        no_body_len = strlen(response_message);
        body_len = 0;
    } else {
        /* statusが200・404以外はこのプログラムで非サポート */
        printf("Not support status(%d)\n", status);
        return -1;
    }

    return no_body_len + body_len;
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
void setHeaderFiled(char *header_field, char *target, unsigned int file_size){
    
    if(strstr(target,"html") != NULL){
        sprintf(header_field, "Content-Type: text/html\r\n");
    }else if(strstr(target,"css") != NULL){
        sprintf(header_field, "Content-Type: text/css\r\n");
    }else if(strstr(target,"javascript") != NULL){
        sprintf(header_field, "Content-Type: text/javascript\r\n");
    }else if(strstr(target,"png") != NULL){
        sprintf(header_field, "Content-Type: image/png\r\n");
    }else{
        sprintf(header_field, "Content-Type: image/jpeg\r\n");
    }
    sprintf(header_field+strlen(header_field), "Content-Length: %u\r\n", file_size);

}

/*
 * HTTPサーバーの処理を行う関数
 * sock：接続済のソケット
 * 戻り値：0
 */
int httpServer(int sock, char *root_path) {

    int request_size, response_size;
    char request_message[SIZE];
    char response_message[SIZE];
    char method[SIZE];
    char target[SIZE];
    char request_body[SIZE];
    char header_field[SIZE];
    char body[SIZE];
    int status;
    char file_type[SIZE];
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
        // showMessage(request_message, request_size);
        
        /* 受信した文字列を解析してメソッドやリクエストターゲットを取得 */
        if (parseRequestMessage(method, target, request_message, root_path, request_size) == -1) {
            printf("parseRequestMessage error\n");
            break;
        }
        
        /* メソッドがGETまたはPOSTまたはHEAD以外はステータスコードを404にする */
        if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 || strcmp(method, "HEAD") == 0) {
            /* GETの応答をするために必要な処理を行う */
            status = getProcessing(body, &target[1]);
        } else {
            status = 405;
        }
        
        /* POST通信時の受信ファイルを保存*/
        if (strcmp(method, "POST") == 0) {
            savePostData(sock, request_message, request_size);
        }

        /* ヘッダーフィールド作成*/
        file_size = getFileSize(&target[1]);
        setHeaderFiled(header_field, target, file_size);

        /* レスポンスメッセージを作成 */
        response_size = createResponseMessage(response_message, status, header_field, body, file_size, method);
        if (response_size == -1) {
            printf("createResponseMessage error\n");
            break;
        }

        /* 送信するメッセージを表示 */
        // showMessage(response_message, response_size);

        /* レスポンスメッセージを送信する */
        sendResponseMessage(sock, response_message, response_size);
        
    }

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
