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
#define N 100
int score[25], comb_num = 0;
char player_list[5000][20];
void comb(int pool, int need, unsigned long chosen, int at){
	if(pool < need + at)  return; // not enough bits left 
	if(!need){
		int player[5], cnt = 0;
		for(at = 0; at < pool; at++)
			if(chosen & ((unsigned long)1 << at))  player[cnt++] = at+1;
		sprintf(player_list[comb_num++], "%d %d %d %d\n", player[0], player[1], player[2], player[3]);
		//printf("%d:%s", comb_num, player_list[comb_num-1]);
		return;
	}
	comb(pool, need-1, chosen|((unsigned long)1 << at), at+1);  //choose the current item
	comb(pool, need, chosen, at+1);  //don't choose it, go to next
}
int cmp(const void *a, const void *b){
	return *((int*)a+1) < *((int*)b+1);
}
int main(int argc, char* argv[]){
	int judge_num = atoi(argv[1]);
	int player_num = atoi(argv[2]);
	int fd_parent_write[N], fd_parent_read[N], fd_child_write[N], fd_child_read[N];
	int fd_read[15][2], fd_write[15][2], pid[15];

	for(int i = 1; i <= judge_num; i++){
		pipe(fd_read[i]);
		pipe(fd_write[i]);
		fd_child_read[i] = fd_write[i][0];
		fd_child_write[i] = fd_read[i][1];
		fd_parent_read[i] = fd_read[i][0];
		fd_parent_write[i] = fd_write[i][1];

		if((pid[i] = fork()) == 0){
			dup2(fd_child_read[i], STDIN_FILENO);
			dup2(fd_child_write[i], STDOUT_FILENO);
			close(fd_read[i][0]);
			close(fd_write[i][1]);
			close(fd_read[i][1]);
			close(fd_write[i][0]);

			char judge_id[N];
			sprintf(judge_id, "%d", i);
			if(execl("judge", "judge", judge_id, (char*)0) < 0)
				perror("FIFO open error!!!\n");
			_exit(0);
		}
	}
	comb(player_num, 4, 0, 0);
	int read_cnt = 0, write_cnt = 0, available[N];
	char buffer[N];
	for(int i = 1; i <= judge_num; i++)
		available[i] = 1;

	while(read_cnt < comb_num){
		//printf("read_num:%d\n", read_cnt);
		for(int i = 1; i <= judge_num; i++){
			if(write_cnt == comb_num)  break;
			if(available[i]){
				write(fd_parent_write[i], player_list[write_cnt], strlen(player_list[write_cnt]));
				//printf("write:%s\n", player_list[write_cnt]);
				available[i] = 0;
				write_cnt++;
			}
		}

		fd_set judge_set;
		FD_ZERO(&judge_set);
		for(int i = 1; i <= judge_num; i++)
			FD_SET(fd_parent_read[i], &judge_set);

		select(fd_parent_read[judge_num]+1, &judge_set, NULL, NULL, NULL);
		for(int i = 1; i <= judge_num; i++)
			if(FD_ISSET(fd_parent_read[i], &judge_set)){
				int p[5], r[5];
				read(fd_parent_read[i], buffer, N);
				//printf("read:%s\n", buffer);
				sscanf(buffer, "%d%d%d%d%d%d%d%d", &p[0], &r[0], &p[1], &r[1], &p[2], &r[2], &p[3], &r[3]);
				for(int j = 0; j < 4; j++)
					score[p[j]] += 4 - r[j];
				available[i] = 1;
				read_cnt++;
				break;
			}

		//for(int j = 1; j <= player_num; j++)
			//printf("score[%d]:%d\n", j, score[j]);
	}
	for(int i = 1; i <= judge_num; i++)
		write(fd_parent_write[i],  "-1 -1 -1 -1\n", 12);

	int rank[25][2];
	for(int i = 0; i < player_num; i++){
		rank[i][0] = i+1;
		rank[i][1] = score[i+1];
	}
	qsort(rank, player_num, sizeof(int[2]), cmp);
	for(int i = 0; i < player_num; i++)
		printf("%d %d\n", rank[i][0], rank[i][1]);

	for(int i = 1; i <= judge_num; i++)
		waitpid(pid[i], NULL, 0);

	return 0;
}