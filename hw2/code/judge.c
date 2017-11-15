/*b04902009蕭千惠*/
#include <stdio.h> 
#include <unistd.h> 
#include <string.h>
#include <stdlib.h> 
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h> 
#include <sys/types.h> 
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>

#define N 256
int main(int argc, char* argv[]){
	char FIFO[5][100], chr[5][3] = {"", "A", "B", "C", "D"};
	int p[5], fd[5], pid[5], key[5], score[5] = {0};
	sprintf(FIFO[0], "%s%s%s", "judge", argv[1], ".FIFO");
	for(int i = 1; i < 5; i++)
		sprintf(FIFO[i], "%s%s%s%s%s", "judge", argv[1], "_", chr[i], ".FIFO");
	for(int i = 0; i < 5; i++)
		mkfifo(FIFO[i], 0666);
	
	fd[0] = open(FIFO[0], O_RDWR);
	for(int i = 1; i < 5; i++)
		fd[i] = open(FIFO[i], O_RDWR);
	
	while(1) {
		scanf("%d%d%d%d", &p[1], &p[2], &p[3], &p[4]);
		if(p[1] < 0)  break;
		for(int i = 1; i < 5; i++)
			key[i] = rand() % 65536;

		for(int i = 1; i < 5; i++){
			char temp_key[100];
			sprintf(temp_key, "%d", key[i]);
			if((pid[i] = fork()) == 0){  //child -> player
				if(execl("player", "player", argv[1], chr[i], temp_key, (char*)0) < 0)
					perror("FIFO open error!!!\n");
				_exit(0);
			}
		}
		char buffer[N], buf[N];
		int ban[5] = {0};

		fd_set fifo_set;
		struct timeval timeout;
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;

		for(int r = 0; r < 20; r++){
			memset( buffer, '\0', sizeof(buffer));
			FD_ZERO(&fifo_set);
			FD_SET(fd[0], &fifo_set);
			int cnt = 4, n, random_key, number_choose, choose[5] = {0};
			char player_index;

			struct timeval end_time, now_time;
			gettimeofday(&end_time, NULL);
			end_time.tv_sec += 3;

			while(1){
				select(fd[0]+1, &fifo_set, NULL, NULL, &timeout);
				gettimeofday(&now_time, NULL);
				timeout.tv_sec = end_time.tv_sec - now_time.tv_sec;
				timeout.tv_usec = end_time.tv_usec - now_time.tv_usec;
				if(timeout.tv_usec < 0){
					timeout.tv_sec--;
					timeout.tv_usec += 1000000;
				}

				if(FD_ISSET(fd[0], &fifo_set)){
					n = read(fd[0], buffer, N);
					buffer[n] = '\0';
					//printf("buffer:%s  n:%d\n", buffer, n);
					for(int i = 0; i < n; i++){
						if(buffer[i] >= 'A' && buffer[i] <= 'D'){
							sscanf(buffer+i, "%c %d %d", &player_index, &random_key, &number_choose);
							int player_id = (player_index - 'A') + 1;
							if(key[player_id] != random_key)  continue;
							choose[player_id] = number_choose;
							cnt--;
						}
					}
				}
				else{
					//printf("time out\n");
					for(int i = 1; i < 5; i++)
						if(!choose[i])  ban[i] = 1;
					break;
				}
				if(!cnt)  break;
			}
			for(int i = 1; i < 5; i++)
				if(ban[i])  choose[i] = 0;
			for(int i = 1; i < 5; i++){
				if(ban[i])  continue;
				int flag = 1;
				for(int j = 1; j < 5; j++)
					if(i != j && choose[i] == choose[j]){
						flag = 0;
						break;
					}
				if(flag)  score[i] += choose[i];
				//printf("round:%d  choose[%d]:%d  score:%d\n", r+1, i, choose[i], score[i]);
			}

			for(int i = 1; i < 5; i++){
				memset(buf, '\0', sizeof(buf));
				if(r != 19){
					sprintf(buf, "%d %d %d %d\n", choose[1], choose[2], choose[3], choose[4]);
					write(fd[i], buf, strlen(buf));
				}
			}
		}
		int rank[5] = {4, 4, 4, 4, 4};
		for(int i = 1; i < 5; i++){
			if(ban[i])  kill(pid[i], SIGKILL);
			waitpid(pid[i], NULL, 0);
			for(int j = 1; j < 5; j++)
				if(score[i] > score[j])  rank[i]--;
		}
		char tmp[N];
		sprintf(tmp, "%d %d\n%d %d\n%d %d\n%d %d\n", p[1], rank[1], p[2], rank[2], p[3], rank[3], p[4], rank[4]);
		printf("%s", tmp);
		fflush(stdout);
	}
	for(int i = 0; i < 5; i++){
		unlink(FIFO[i]);
	}

	return 0;
}