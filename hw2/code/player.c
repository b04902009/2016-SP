/*b04902009蕭千惠*/
#include <stdio.h> 
#include <unistd.h> 
#include <string.h>
#include <stdlib.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <fcntl.h>
#include <time.h>
#define N 1024
int main(int argc, char* argv[]){
	char FIFO[2][100];
	sprintf(FIFO[0], "%s%s%s", "judge", argv[1], ".FIFO");
	sprintf(FIFO[1], "%s%s%s%s%s", "judge", argv[1], "_", argv[2], ".FIFO");

	int fd[2], n;
	char buffer[N], buf[N]={'\0'};

	fd[0] = open(FIFO[0], O_WRONLY);
	fd[1] = open(FIFO[1], O_RDONLY);

	srand((unsigned)time(NULL)+atoi(argv[3]));
	for(int r = 0; r < 20; r++){
		//if(argv[2][0] == 'A')  sleep(4);
		int number_choose = (rand() % 3) * 2 + 1;
		sprintf(buffer, "%s %s %d", argv[2], argv[3], number_choose);
		//printf("player_buffer:%s\n", buffer);
		write(fd[0], buffer, strlen(buffer));
		if(r != 19)  read(fd[1], buf, N);
		//printf("round:%d_%s: %s\n", r+1, argv[2], buf);
	}
	return 0;
}