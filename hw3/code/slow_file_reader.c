/*B04902009 蕭千惠*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define N 1024
typedef struct {
	char filename[100];
    char c_time_string[100];
} TimeInfo;
void status(int s){
	char buf[N];
	if(s == 400){
		printf("FILENAME BAD REQUEST!");
		sprintf(buf, "HTTP/1.0 400 Bad Request\r\n");
	}
	else if(s == 404){
		sprintf(buf, "HTTP/1.0 404 Not Found\r\n");
		printf("FILE NOT FOUND!");
	}
	else
		sprintf(buf, "HTTP/1.0 200 OK\r\n");
    write(1, buf, strlen(buf));
    sprintf(buf, "\r\n");
    write(1, buf, strlen(buf));
}
int main(){
	// Deal with filename
	char filename[N], buffer[N];
	scanf("%s", filename);
	sleep(5);
	if(strncmp(filename, "filename=", 9)){
		status(400);
		return -1;
	}
	else
		for(int i = 9; i <= strlen(filename); i++)
			filename[i-9] = filename[i];

	// mmap
	int fd;
    time_t current_time;
    char c_time_string[100];
    TimeInfo *p_map;
    const char  *file ="time_test";
    
    fd = open(file, O_RDWR | O_TRUNC | O_CREAT, 0777); 
    if(fd < 0) {
        perror("open");
        exit(-1);
    }
    lseek(fd, sizeof(TimeInfo), SEEK_SET);
    write(fd, "", 1);

    p_map = (TimeInfo*) mmap(0, sizeof(TimeInfo), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    current_time = time(NULL);
    strcpy(c_time_string, ctime(&current_time));
    memcpy(p_map->c_time_string, &c_time_string , strlen(c_time_string)-1);
    memcpy(p_map->filename, &filename , strlen(filename));   
    munmap(p_map, sizeof(TimeInfo));

    fd = open(file, O_RDWR);
    p_map = (TimeInfo*)mmap(0, sizeof(TimeInfo),  PROT_READ,  MAP_SHARED, fd, 0);

    // CGI program
	int filename_length = strlen(filename), bad_request = 0;
    for(int i = 0; i < filename_length; i++){
        char c = filename[i];
        if(!(isdigit(c) || isalpha(c) || c == '_')){
            bad_request++;
            break;
        }
    }
    if(bad_request){
		status(400);
		return -1;
    }
	else{
		int fd, n;
		if((fd = open(filename, O_RDONLY)) < 0){
			status(404);
			return -1;
		}
		status(200);
		while((n = read(fd, buffer, N)) > 0)
			write(1, buffer, strlen(buffer));
	}
	return 0;
}