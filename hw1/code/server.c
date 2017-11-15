#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    char* filename;  // filename set in header, end with '\0'.
    int header_done;  // used by handle_read to know if the header is read or not.
    int first_in;
    int write_filefd;
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_header = "ACCEPT\n";
const char* reject_header = "REJECT\n";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int lock_reg(int, int, int, off_t, int, off_t);
static pid_t lock_test(int, int, off_t, int, off_t);
int conn_fd_name[10000]={0};
int main(int argc, char** argv) {
    int i, ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
	
	fd_set working_set, master_set;
	FD_ZERO(&master_set);
	FD_ZERO(&working_set);
	FD_SET(svr.listen_fd, &master_set);
	int n_fd = 4;
	struct timeval restrict_time;
	restrict_time.tv_sec = 5;
	restrict_time.tv_usec = 0;

	
    while (1) {
    	memcpy(&working_set, &master_set, sizeof(master_set));
        select(n_fd, &working_set, NULL, NULL, &restrict_time);
        for(int i = 3; i < n_fd; i++) {
            if(FD_ISSET(i, &working_set)) {
                if(i == svr.listen_fd) {
                    clilen = sizeof(cliaddr);
                    conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                    if (conn_fd < 0) {
                        if (errno == EINTR || errno == EAGAIN) continue;  // try again
                        if (errno == ENFILE) {
                            (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                            continue;
                        }
                        ERR_EXIT("accept")
                    }
                    requestP[conn_fd].conn_fd = conn_fd;
                    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                    FD_SET(requestP[conn_fd].conn_fd, &master_set);
                    n_fd++;
                }
                else{
                    file_fd = -1;
                    conn_fd = i;
   
#ifdef READ_SERVER
			        ret = handle_read(&requestP[conn_fd]);
		            if (ret < 0) {
		                fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
		                continue;
		            }
		            if (ret == 0){
		            	continue;
					}
					// open the file here.
					fprintf(stderr, "Opening file [%s]\n", requestP[conn_fd].filename);
		            file_fd = open(requestP[conn_fd].filename, O_RDONLY, 0);
		            // requestP[conn_fd]->filename is guaranteed to be successfully set.
		        
					// TODO: check if the request should be rejected.
	                if(!lock_test(file_fd, F_WRLCK, 0, SEEK_SET, 0)){
			    		write(requestP[conn_fd].conn_fd, accept_header, sizeof(accept_header));
						// TODO: Add lock
			          	lock_reg(file_fd, F_SETLK, F_RDLCK, 0, SEEK_SET, 0);
				        while (1) {
				            ret = read(file_fd, buf, sizeof(buf));
				            if (ret < 0) {
				                fprintf(stderr, "Error when reading file %s\n", requestP[conn_fd].filename);
				                break;
				            } else if (ret == 0) break;
				            write(requestP[conn_fd].conn_fd, buf, ret);
				        }
				        lock_reg(file_fd, F_SETLK, F_UNLCK, 0, SEEK_SET, 0);
				        fprintf(stderr, "Done reading file [%s]\n", requestP[conn_fd].filename);   	
					}
	                else
						write(requestP[conn_fd].conn_fd, reject_header, sizeof(reject_header)); 
			        FD_CLR(i, &master_set);
			        if (file_fd >= 0) close(file_fd);
		        	close(requestP[conn_fd].conn_fd);
		        	free_request(&requestP[conn_fd]);
#endif

#ifndef READ_SERVER
			        ret = handle_read(&requestP[conn_fd]);
			        if (ret < 0) {
			            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
			            continue;
			        }
			        // requestP[conn_fd]->filename is guaranteed to be successfully set.
			        if (ret > 0){
						if(requestP[conn_fd].first_in) {
				            // open the file here.
				            fprintf(stderr, "Opening file [%s]\n", requestP[conn_fd].filename);
				            file_fd = open(requestP[conn_fd].filename, O_WRONLY | O_CREAT | O_TRUNC,
				                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
				            int flag = 1;
							for(int i = 0; i < maxfd; i++){
				            	if(conn_fd_name[i]){
				            		if(!strcmp(requestP[conn_fd].filename, requestP[i].filename))
										flag = 0;
								}
							}
				            // TODO: check if the request should be rejected.
				            if(flag && !lock_test(file_fd, F_RDLCK, 0, SEEK_SET, 0) && !lock_test(file_fd, F_WRLCK, 0, SEEK_SET, 0)){
			                    // TODO: Add lock
                            	lock_reg(file_fd, F_SETLK, F_WRLCK, 0, SEEK_SET, 0);
                                requestP[conn_fd].first_in = 0;
                                write(requestP[conn_fd].conn_fd, accept_header, sizeof(accept_header));
                                conn_fd_name[conn_fd] = 1;
                                requestP[conn_fd].write_filefd = file_fd;
							}
                            else{
                                write(requestP[conn_fd].conn_fd, reject_header, sizeof(reject_header));
                                fprintf(stderr, "Done writing file [%s]\n", requestP[conn_fd].filename);
                                FD_CLR(i, &master_set);
                                close(file_fd);
                                close(requestP[conn_fd].conn_fd);
                                free_request(&requestP[conn_fd]);
                            }
						}
						write(requestP[conn_fd].write_filefd, requestP[conn_fd].buf, requestP[conn_fd].buf_len);
					}
			        if (ret == 0){
				 	    lock_reg(requestP[conn_fd].write_filefd, F_SETLK, F_UNLCK, 0, SEEK_SET, 0);
                        conn_fd_name[conn_fd] = 0;
					    fprintf(stderr, "Done writing file [%s]\n", requestP[conn_fd].filename);
					    FD_CLR(i, &master_set);
                        close(file_fd);
				        close(requestP[conn_fd].write_filefd);
			        	close(requestP[conn_fd].conn_fd);
			        	free_request(&requestP[conn_fd]);
			        }    				    
#endif	        
        		} 
        	}
		} 
    }

    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->filename = NULL;
    reqP->header_done = 0;
    reqP->first_in = 1;
}

static void free_request(request* reqP) {
    if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (reqP->header_done == 0) {
        char* p1 = strstr(buf, "\015\012");
        int newline_len = 2;
        // be careful that in Windows, line ends with \015\012
        if (p1 == NULL) {
            p1 = strstr(buf, "\012");
            newline_len = 1;
            if (p1 == NULL) {
                // This would not happen in testing, but you can fix this if you want.
                ERR_EXIT("header not complete in first read...");
            }
        }
        size_t len = p1 - buf + 1;
        reqP->filename = (char*)e_malloc(len);
        memmove(reqP->filename, buf, len);
        reqP->filename[len - 1] = '\0';
        p1 += newline_len;
        reqP->buf_len = r - (p1 - buf);
        memmove(reqP->buf, p1, reqP->buf_len);
        reqP->header_done = 1;
    } else {
        reqP->buf_len = r;
        memmove(reqP->buf, buf, r);
    }
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}
static int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len) {
    struct flock lock;
    lock.l_type = type;     /* F_RDLCK, F_WRLCK, F_UNLCK */
    lock.l_start = offset;  /* byte offset relative to l_whence */
    lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
    lock.l_len = len;       /* #bytes (0 means to EOF) */
    return (fcntl(fd, cmd, &lock));
}
static pid_t lock_test(int fd, int type, off_t offset, int whence, off_t len) {
    struct flock lock;
    lock.l_type = type;     /* F_RDLCK or F_WRLCK */
    lock.l_start = offset;  /* byte offset relative to l_whence */
    lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
    lock.l_len = len;       /* #bytes (0 means to EOF) */
    if(fcntl(fd,F_GETLK,&lock) < 0)  perror("fcntl");
    if(lock.l_type == F_UNLCK)
        return (0);      /* false, region is not locked by another process */
    return (lock.l_pid); /* true, return pid of lock owner */
}
