#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
	FILE *fp1 = fopen(argv[1], "rb");
	FILE *fp2 = fopen(argv[2], "rb");
	int n = 25008, t = 0;
	fscanf(fp1, "%*s");
	fscanf(fp2, "%*s");
	for (int i = 0; i < n; i++) {
		int x, y;
		fscanf(fp1, "%*d%*c%d", &x);
		fscanf(fp2, "%*d%*c%d", &y);
		if(x == y) t++;
	}
	printf("%f\n", (double)t/n);
	fclose(fp1);
	fclose(fp2);
}
//./judge submission.csv ../ans.csv