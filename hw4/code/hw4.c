#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#define N 25150
#define M 25008
#define F 33
#define TreeSize 64
#define ThreadNum 4
#define TreeNum 1024
typedef struct People{
	int id, bad; // bad = 0 => GOOD , bad = 1 => BAD
	double f[33]; //feature
} People;
People train[N+1], test[M+10];
typedef struct Node{
	struct Node *l, *r;
	int dim, g, b, s, e, result;
	double threshold;
} Node;
Node *root[TreeNum] = {NULL};
typedef struct Gini{
	int g, b;
	double impurity;
} Gini;
typedef struct Job{
	int s, e;
} Job;
int t[TreeNum][TreeSize];
Job jobs[ThreadNum];
FILE *fpr, *fpw;
int feature;

int cmp(const void *a, const void *b){
	return (train[*(int*)a].f[feature] > train[*(int*)b].f[feature]);
}
double count_gini(Gini *gi, double n){
	if(n == 0)  return 0;
	double f0 = gi->g / n, f1 = gi->b / n;
	return (f0 * (1-f0) + f1 * (1-f1));
}
void build(Node *node, int* t){
	int s = node->s, e = node->e;
	int n = e - s + 1, cut;
	if(n == 1){
		node->result = train[t[s]].bad;
		return;
	}
	double min = 1;
	node->b = node->g = 0;
	for(int j = s; j <= e; j++){
		if(train[t[j]].bad)  node->b++;
		else  node->g++;
	}
	for(int i = 0; i < F; i++){
		feature = i;
		qsort(t+s, n, sizeof(int), cmp);
		Gini *left = (Gini*)malloc(sizeof(Gini)), *right = (Gini*)malloc(sizeof(Gini));
		left->g = left->b = 0;
		right->g = node->g;
		right->b = node->b;
		for(int j = s; j < e; j++){
			if(train[t[j]].bad){
				left->b++;
				right->b--;
			}
			else{
				left->g++;
				right->g--;
			}
			double ln = left->g + left->b, rn = right->b + right->g;
			double tmp = (ln * count_gini(left, ln) +  rn * count_gini(right, rn)) / (ln + rn);
			if(tmp < min){
				min = tmp;
				node->dim = i;
				cut = j;
			}
		}
		free(left);
		free(right);
	}
	feature = node->dim;
	qsort(t+s, n, sizeof(int), cmp);
	node->threshold = (train[t[cut]].f[node->dim] + train[t[cut+1]].f[node->dim]) / 2;
	
	node->l = (Node*)calloc(sizeof(Node), sizeof(Node));
	node->l->s = s;
	node->l->e = cut;
	node->r = (Node*)calloc(sizeof(Node), sizeof(Node));
	node->r->s = cut+1;
	node->r->e = e;
	if(min == 0){
		node->l->result = train[t[cut]].bad;
		node->r->result = train[t[cut+1]].bad;
		return;
	}
	build(node->l, t);
	build(node->r, t);
}
int check(Node *node, int idx){
	if(node->l == NULL || node->r == NULL){
		return node->result;
	}
	if(test[idx].f[node->dim] < node->threshold) return check(node->l, idx);
	else return check(node->r, idx);
}
void* thread_build(void* ptr){
	Job *job = (Job*)ptr;
	for(int i = job->s; i <= job->e; i++){
		build(root[i], t[i]);
	}
	pthread_exit((void *)1);
}
void* thread_check(void* ptr){
	Job *job = (Job*)ptr;
	for(int i = job->s; i <= job->e; i++){
		int tb = 0, tg = 0;
		for(int j = 0; j < TreeNum; j++)	
			if(check(root[j], i))  tb++;
			else  tg++;
		test[i].bad = (tb > tg) ? 1 : 0;
	}
	pthread_exit((void *)1);
}
int main(){
	if((fpr = freopen("../data/training_data", "r", stdin)) == NULL) 
		printf("training_data open error!");
	for(int i = 0; i < N; i++){
		scanf("%d", &train[i].id);
		for(int j = 0; j < 33; j++)
			scanf("%lf", &train[i].f[j]);
		scanf("%d",& train[i].bad);
	}
	fclose(fpr);

	pthread_t thread[ThreadNum];
	for(int i = 0; i < TreeNum; i++){
		srand(i);
		for(int j = 0; j < TreeSize; j++)
			t[i][j] = rand()% N;
		root[i] = (Node*)malloc(sizeof(Node));
		root[i]->s = 0;
		root[i]->e = TreeSize-1;
	}

	for(int i = 0; i < ThreadNum; i++){
		int n = TreeNum/ThreadNum;
		jobs[i].s = i * n;
		jobs[i].e = (i+1) * n - 1;
		pthread_create(&thread[i], NULL, thread_build, (void*)(&jobs[i]));
	}

	void *ret;
	for(int i = 0; i < ThreadNum; i++)
		pthread_join(thread[i], &ret);

	if((fpr = freopen("../data/testing_data", "r", stdin)) == NULL) 
		printf("test_data open error!");
	if((fpw = freopen("submission.csv", "w", stdout)) == NULL) 
		printf("submission.csv create error!");

	printf("id,label\n");
	for(int i = 0; i < M; i++){
		scanf("%d", &test[i].id);
		for(int j = 0; j < 33; j++)
			scanf("%lf", &test[i].f[j]);
	}
	for(int i = 0; i < ThreadNum; i++){
		int n = M/ThreadNum;
		jobs[i].s = i * n;
		jobs[i].e = (i+1) * n - 1;
		pthread_create(&thread[i], NULL, thread_check, (void*)(&jobs[i]));
	}
	for(int i = 0; i < ThreadNum; i++)
		pthread_join(thread[i], &ret);
	for(int i = 0; i < M; i++)	
		printf("%d,%d\n", test[i].id, test[i].bad);

	fclose(fpr);
	fclose(fpw);
	return 0;
}
