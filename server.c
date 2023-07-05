#include <sys/socket.h>
#include <netinet/in.h>	
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SIZE (5*1024)

int httpServer(int, char*);
int recvRequestMessage(int, char*, unsigned int);
int parseRequestMessage(char*, char*, char*, char*);
int getProcessing(char*, char*);
int createResponseMessage(char*, int, char*, char*, unsigned int);
int sendResponseMessage(int, char*, unsigned int);
unsigned int getFileSize(const char*);
void setHeaderFiled(char*,char*,unsigned int);

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
    
    recv_size = recv(sock, request_message, buf_size, 0);
            printf("-------------------------------------------------------\n");

    return recv_size;
}

/*
 * リクエストメッセージを解析する（今回はリクエスト行のみ）
 * method：メソッドを格納するバッファへのアドレス
 * target：リクエストターゲットを格納するバッファへのアドレス
 * request_message：解析するリクエストメッセージが格納されたバッファへのアドレス
 * 戻り値：成功時は0、失敗時は-1
 */
int parseRequestMessage(char *method, char *target, char *request_message, char *root_path) {

    char *line;
    char *tmp_method;
    char *tmp_target;
    
    /* リクエストメッセージの１行目のみ取得 */
    line = strtok(request_message, "\n");

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
    printf("target;%s-----------------------------------------\n",target);
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

    /* ファイルサイズを取得 */
    file_size = getFileSize(file_path);
    if (file_size == 0) {
        /* ファイルサイズが0やファイルが存在しない場合は404を返す */
        printf("getFileSize error\n");
        return 404;
    }
    /* ファイルを読み込んでボディとする */
    f = fopen(file_path, "rb");
    if (strcmp(file_path, "secret.html") == 0){
        fclose(f);
        return 403;
    }else{
        fread(body, 1, file_size, f);
        fclose(f);
        return 200;
    }
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
int createResponseMessage(char *response_message, int status, char *header, char *body, unsigned int body_size) {

    unsigned int no_body_len;
    unsigned int body_len;

    response_message[0] = '\0';

    if (status == 200) {
        /* レスポンス行とヘッダーフィールドの文字列を作成 */
        sprintf(response_message, "HTTP/1.1 200 OK\r\n%s\r\n", header);

        no_body_len = strlen(response_message);
        body_len = body_size;

        /* ヘッダーフィールドの後ろにボディをコピー */
        memcpy(&response_message[no_body_len], body, body_len);
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
        showMessage(request_message, request_size);

        /* 受信した文字列を解析してメソッドやリクエストターゲットを取得 */
        if (parseRequestMessage(method, target, request_message, root_path) == -1) {
            printf("parseRequestMessage error\n");
            break;
        }

        /* メソッドがGET以外はステータスコードを404にする */
        if (strcmp(method, "GET") == 0) {
            // if (strcmp(target, "/") == 0) {
            //     /* /が指定された時は/index.htmlに置き換える */
            //     strcpy(target, "/index.html");
            // }

            /* GETの応答をするために必要な処理を行う */
            status = getProcessing(body, &target[1]);
        } else {
            status = 404;
        }

        /* ヘッダーフィールド作成*/
        file_size = getFileSize(&target[1]);
        setHeaderFiled(header_field, target, file_size);

        /* レスポンスメッセージを作成 */
        response_size = createResponseMessage(response_message, status, header_field, body, file_size);
        if (response_size == -1) {
            printf("createResponseMessage error\n");
            break;
        }

        /* 送信するメッセージを表示 */
        showMessage(response_message, response_size);

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

        /* 接続済のソケットでデータのやり取り */
        httpServer(c_sock,root_path);

        /* ソケット通信をクローズ */
        close(c_sock);

        /* 次の接続要求の受け付けに移る */
    }

    /* 接続待ちソケットをクローズ */
    close(w_addr);

    return 0;
}
