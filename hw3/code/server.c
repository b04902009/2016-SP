/*B04902009 蕭千惠*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TIMEOUT_SEC 5		// timeout in seconds for wait for a connection 
#define MAXBUFSIZE  1024
#define NO_USE      0		// status of a http request
#define ERROR	    -1	
#define READING     1		
#define WRITING     2		
#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];		// hostname
    unsigned short port;	// port to listen
    int listen_fd;		// fd to wait for a new connection
} http_server;

typedef struct {
    int conn_fd;		// fd to talk with client
    int status;			// not used, error, reading (from client)
                                // writing (to client)
    char file[MAXBUFSIZE];	// requested file
    char query[MAXBUFSIZE];	// requested query
    char host[MAXBUFSIZE];	// client host
    char* buf;			// data sent by/to client
    size_t buf_len;		// bytes used by buf
    size_t buf_size; 		// bytes allocated for buf
    size_t buf_idx; 		// offset for reading and writing
} http_request;

static char* logfilenameP;	// log file name


// Forwards
//
static void init_http_server(http_server *svrP,  unsigned short port);
// initailize a http_request instance, exit for error

static void init_request(http_request* reqP);
// initailize a http_request instance

static void free_request(http_request* reqP);
// free resources used by a http_request instance

static int read_header_and_file(http_request* reqP, int *errP);
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: continue to it until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected

static void status(int s, int fd);
// 200 OK, 404 NOT FOUND, 400 BAD REQUEST

static void catch_sigchld(int sig);
// When SIGCHLD is caught, execute this finction
pid_t cpid[MAXBUFSIZE];
int child = 0, deadchild = 0;

static void catch_sigusr1(int sig);
// When SIGUSR is caught, execute this finction
int sigusr_connfd;
http_request* requestP = NULL;// pointer to http requests from client

typedef struct {
    char filename[100];
    char c_time_string[100];
} TimeInfo;
TimeInfo *p_map;

int main(int argc, char** argv) {
    http_server server;		// http server
    int maxfd;                  // size of open file descriptor table
    struct sockaddr_in cliaddr; // used by accept()
    int clilen;
    int conn_fd;		// fd for a new connection with client
    int err;			// used by read_header_and_file()
    int i, ret, nwritten;

    // Parse args. 
    if(argc != 3) {
        (void) fprintf(stderr, "usage:  %s port# logfile\n", argv[0]);
        exit(1);
    }

    logfilenameP = argv[2];

    // Initialize http server
    init_http_server(&server, (unsigned short) atoi(argv[1]));

    maxfd = getdtablesize();
    requestP = (http_request*) malloc(sizeof(http_request) * maxfd);
    if(requestP == (http_request*) 0) {
    	fprintf(stderr, "out of memory allocating all http requests\n");
    	exit(1);
    }
    for(i = 0; i < maxfd; i ++)
        init_request(&requestP[i]);
    requestP[ server.listen_fd ].conn_fd = server.listen_fd;
    requestP[ server.listen_fd ].status = READING;

    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d, logfile %s...\n",
            server.hostname, server.port, server.listen_fd, maxfd, logfilenameP);
    
    signal(SIGCHLD, catch_sigchld);
    signal(SIGUSR1, catch_sigusr1);

    fd_set master_set, working_set;
    FD_ZERO(&master_set);
    FD_ZERO(&working_set);
    FD_SET(server.listen_fd, &master_set);

    int cgi_read[maxfd][2], cgi_write[maxfd][2];
    int fd_list[MAXBUFSIZE];

    // Main loop. 
    while(1) {
    // Multiplexing
        memcpy(&working_set, &master_set, sizeof(master_set));
        int k = select(maxfd+1, &working_set, NULL, NULL, NULL);
        if(k == -1 && errno == 4)  continue; // when select is interrupted by signals, do it again.

        for(int i = 0; i < maxfd; i++){
            if(!FD_ISSET(i, &working_set))  continue;
        	if(i == server.listen_fd){
                clilen = sizeof(cliaddr);
            	conn_fd = accept(server.listen_fd, (struct sockaddr *) &cliaddr, (socklen_t *) &clilen);
            	if(conn_fd < 0) {
            	    if(errno == EINTR || errno == EAGAIN)  continue; // try again 
            	    if(errno == ENFILE) {
                        (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
            	        continue;
                    }	
            	    ERR_EXIT("accept")
            	}
                requestP[conn_fd].conn_fd = conn_fd;
                requestP[conn_fd].status = READING;
                strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);

        	    ret = read_header_and_file(&requestP[conn_fd], &err);
        	    if(ret > 0)  continue;
        	    else if(ret < 0) {
        	        // error for reading http header or requested file
                    fprintf(stderr, "error on fd %d, code %d\n", 
                    requestP[conn_fd].conn_fd, err);
                    requestP[conn_fd].status = ERROR;
        	        close(requestP[conn_fd].conn_fd);
        	        free_request(&requestP[conn_fd]);
                    break;
        	    }
                else{
                    int cgi_length = strlen(requestP[conn_fd].file), bad_request = 0;
                    for(int i = 0; i < cgi_length; i++){
                        char c = requestP[conn_fd].file[i];
                        if(!(isdigit(c) || isalpha(c) || c == '_')){
                            status(400, conn_fd);
                            write(conn_fd, "CGI BAD REQUEST!", 16);
                            fprintf(stderr, "CGI BAD REQUEST!\n");
                            close(conn_fd);
                            free_request(&requestP[conn_fd]);
                            bad_request++;
                            break;
                        }
                    }
                    if(bad_request)  continue;
                    if(strcmp(requestP[conn_fd].file, "info") == 0) {
                        sigusr_connfd = conn_fd;
                        if((cpid[child] = fork()) == 0){
                            kill(getppid(), SIGUSR1);
                            exit(0);
                        }
                        continue;
                    }
                    if(!(strcmp(requestP[conn_fd].file, "file_reader") == 0 || strcmp(requestP[conn_fd].file, "slow_file_reader") == 0)) {
                        status(404, conn_fd);
                        write(conn_fd, "CGI NOT FOUND!", 14);
                        close(conn_fd);
                        free_request(&requestP[conn_fd]);
                        continue;
                    }
                    pipe(cgi_read[conn_fd]);
                    pipe(cgi_write[conn_fd]);
                    // child
                    if((cpid[child++] = fork()) == 0){
                        dup2(cgi_read[conn_fd][0], STDIN_FILENO);
                        dup2(cgi_write[conn_fd][1], STDOUT_FILENO);
                        close(cgi_read[conn_fd][0]);
                        close(cgi_read[conn_fd][1]);
                        close(cgi_write[conn_fd][0]);
                        close(cgi_write[conn_fd][1]);
                        char buf[MAXBUFSIZE];
                        sprintf(buf, "%s", requestP[conn_fd].file);
                        if(execl(buf, buf, (char*)0) < 0)
                            perror("exec error!!!");
                        _exit(0);
                    }
                    // parent
                    close(cgi_read[conn_fd][0]);
                    close(cgi_write[conn_fd][1]);
                    write(cgi_read[conn_fd][1], requestP[conn_fd].query, strlen(requestP[conn_fd].query));
                    close(cgi_read[conn_fd][1]);
                    fd_list[cgi_write[conn_fd][0]] = conn_fd;
                    FD_SET(cgi_write[conn_fd][0], &master_set);

                    int fd;
                    const char *file = "time_test";
                    fd = open(file, O_RDWR);
                    p_map = (TimeInfo*) mmap(0, sizeof(TimeInfo),  PROT_READ,  MAP_SHARED, fd, 0);
        	    }
            }
            else{
                conn_fd = i;
                char buf[MAXBUFSIZE];
                if((ret = read(conn_fd, buf, sizeof(buf))) > 0)
                    write(fd_list[conn_fd], buf, ret);
                if(!ret){
                    FD_CLR(conn_fd, &master_set);
                    close(conn_fd);
                    close(fd_list[conn_fd]);
                    free_request(&requestP[fd_list[conn_fd]]);
                }
            }
        }
    }
    free(requestP);
    return 0;
}
static void status(int s, int fd){
    char buf[MAXBUFSIZE];
    if(s == 400)
        sprintf(buf, "HTTP/1.0 400 Bad Request\r\n");
    else if(s == 404)
        sprintf(buf, "HTTP/1.0 404 Not Found\r\n");
    else if(s == 200)
        sprintf(buf, "HTTP/1.0 200 OK\r\n");
    write(fd, buf, strlen(buf));
    sprintf(buf, "\r\n");
    write(fd, buf, strlen(buf));
}
static void catch_sigchld(int sig){
    signal(sig, catch_sigchld);
    siginfo_t info;
    waitid(P_ALL, 0, &info, WEXITED);
    
    for(int i = 0; i < child; i++){
        if(cpid[i] == info.si_pid){
            for(int j = i; j < child-1; j++)
                cpid[j] = cpid[j+1];
            child--;
            deadchild++;
        }
    }
}
static void catch_sigusr1(int sig){
    signal(sig, catch_sigusr1);
    status(200, sigusr_connfd);
    char buf[MAXBUFSIZE];
    sprintf(buf, "%d processes died previously.\n", deadchild);
    write(sigusr_connfd, buf, strlen(buf));
    sprintf(buf, "PIDs of Running Processes:");
    write(sigusr_connfd, buf, strlen(buf));
    for(int i = 0; i < child; i++){
        sprintf(buf, " %d%c", cpid[i], i == child-1 ? '\n' : ',');
        write(sigusr_connfd, buf, strlen(buf));
    }
    if(!child)  write(sigusr_connfd, "\n", 1);
    if(deadchild){
        sprintf(buf, "Last Exit CGI: %s, Filename: %s", p_map->c_time_string, p_map->filename);
        write(sigusr_connfd, buf, strlen(buf));
        child++;
        deadchild--;
    }
    close(sigusr_connfd);
    free_request(&requestP[sigusr_connfd]);
}


// ======================================================================================================
// You don't need to know how the following codes are working

#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

static void add_to_buf(http_request *reqP, char* str, size_t len);
static void strdecode(char* to, char* from);
static int hexit(char c);
static char* get_request_line(http_request *reqP);
static void* e_malloc(size_t size);
static void* e_realloc(void* optr, size_t size);

static void init_request(http_request* reqP) {
    reqP->conn_fd = -1;
    reqP->status = 0;		// not used
    reqP->file[0] = (char) 0;
    reqP->query[0] = (char) 0;
    reqP->host[0] = (char) 0;
    reqP->buf = NULL;
    reqP->buf_size = 0;
    reqP->buf_len = 0;
    reqP->buf_idx = 0;
}

static void free_request(http_request* reqP) {
    if(reqP->buf != NULL) {
	   free(reqP->buf);
	   reqP->buf = NULL;
    }
    init_request(reqP);
}


#define ERR_RET(error) { *errP = error; return -1; }
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: read more, continue until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected
//
static int read_header_and_file(http_request* reqP, int *errP) {
    // Request variables
    char* file = (char *) 0;
    char* path = (char *) 0;
    char* query = (char *) 0;
    char* protocol = (char *) 0;
    char* method_str = (char *) 0;
    int r, fd;
    struct stat sb;
    char timebuf[100];
    int buflen;
    char buf[10000];
    time_t now;
    void *ptr;

    // Read in request from client
    while(1) {
    	r = read(reqP->conn_fd, buf, sizeof(buf));
    	if(r < 0 && (errno == EINTR || errno == EAGAIN)) return 1;
    	if(r <= 0) ERR_RET(1)
    	add_to_buf(reqP, buf, r);
    	if(strstr(reqP->buf, "\015\012\015\012") != (char*) 0 ||
    	     strstr(reqP->buf, "\012\012") != (char*) 0) break;
    }
    // fprintf(stderr, "header: %s\n", reqP->buf);

    // Parse the first line of the request.
    method_str = get_request_line(reqP);
    if(method_str == (char*) 0)  ERR_RET(2)
    path = strpbrk(method_str, " \t\012\015");
    if(path == (char*) 0)  ERR_RET(2)
    *path++ = '\0';
    path += strspn(path, " \t\012\015");
    protocol = strpbrk(path, " \t\012\015");
    if(protocol == (char*) 0)  ERR_RET(2)
    *protocol++ = '\0';
    protocol += strspn(protocol, " \t\012\015");
    query = strchr(path, '?');
    if(query == (char*) 0)  query = "";
    else  *query++ = '\0';

    if(strcasecmp(method_str, "GET") != 0)  ERR_RET(3)
    else {
        strdecode(path, path);
        if(path[0] != '/')  ERR_RET(4)
        else file = &(path[1]);
    }

    if(strlen(file) >= MAXBUFSIZE-1)  ERR_RET(4)
    if(strlen(query) >= MAXBUFSIZE-1)  ERR_RET(5)
	  
    strcpy(reqP->file, file);
    strcpy(reqP->query, query);

    return 0;
}


static void add_to_buf(http_request *reqP, char* str, size_t len) { 
    char** bufP = &(reqP->buf);
    size_t* bufsizeP = &(reqP->buf_size);
    size_t* buflenP = &(reqP->buf_len);

    if(*bufsizeP == 0) {
    	*bufsizeP = len + 500;
    	*buflenP = 0;
    	*bufP = (char*) e_malloc(*bufsizeP);
    } 
    else if(*buflenP + len >= *bufsizeP) {
    	*bufsizeP = *buflenP + len + 500;
    	*bufP = (char*) e_realloc((void*) *bufP, *bufsizeP);
    }
    (void) memmove(&((*bufP)[*buflenP]), str, len);
    *buflenP += len;
    (*bufP)[*buflenP] = '\0';
}

static char* get_request_line(http_request *reqP) { 
    int begin;
    char c;

    char *bufP = reqP->buf;
    int buf_len = reqP->buf_len;

    for(begin = reqP->buf_idx ; reqP->buf_idx < buf_len; ++reqP->buf_idx) {
    	c = bufP[ reqP->buf_idx ];
    	if(c == '\012' || c == '\015') {
    	    bufP[reqP->buf_idx] = '\0';
    	    ++reqP->buf_idx;
    	    if(c == '\015' && reqP->buf_idx < buf_len && 
                bufP[reqP->buf_idx] == '\012') {
                bufP[reqP->buf_idx] = '\0';
                ++reqP->buf_idx;
    	    }
    	    return &(bufP[begin]);
    	}
    }
    fprintf(stderr, "http request format error\n");
    exit(1);
}



static void init_http_server(http_server *svrP, unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svrP->hostname, sizeof(svrP->hostname));
    svrP->port = port;
   
    svrP->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(svrP->listen_fd < 0) ERR_EXIT("socket")

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if(setsockopt(svrP->listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &tmp, sizeof(tmp)) < 0) 
        ERR_EXIT ("setsockopt ")
    if(bind(svrP->listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) 
        ERR_EXIT("bind")
    if(listen(svrP->listen_fd, 1024) < 0) ERR_EXIT("listen")
}

static void strdecode(char* to, char* from) {
    for(; *from != '\0'; ++to, ++from) {
    	if(from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
    	    *to = hexit(from[1]) * 16 + hexit(from[2]);
    	    from += 2;
    	} 
        else {
    	    *to = *from;
        }
    }
    *to = '\0';
}


static int hexit(char c) {
    if(c >= '0' && c <= '9')
	   return c - '0';
    if(c >= 'a' && c <= 'f')
	   return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
	   return c - 'A' + 10;
    return 0;           // shouldn't happen
}


static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if(ptr == (void*) 0) {
    	(void) fprintf(stderr, "out of memory\n");
    	exit(1);
    }
    return ptr;
}


static void* e_realloc(void* optr, size_t size) {
    void* ptr;

    ptr = realloc(optr, size);
    if(ptr == (void*) 0) {
    	(void) fprintf(stderr, "out of memory\n");
    	exit(1);
    }
    return ptr;
}
