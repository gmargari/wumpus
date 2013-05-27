#include <stdio.h>
#include <math.h>

int main() {
	double L, R, I, N;
	printf("N = "); scanf("%lf", &N);
	printf("L = "); scanf("%lf", &L);
	printf("R = "); scanf("%lf", &R);
	printf("I = "); scanf("%lf", &I);

	double oldCost = L * log(N/L+1) / log(2);
	printf("Normal compression: %.0lf bits (%.2lf bits per posting)\n", oldCost, oldCost / L);

	double newCost = (L-I) * log(N/L+1) / log(2)
	               + I * log(R/L+1) / log(2)
	               + (L-I) * log(L/(L-I)) / log(2)
	               + I * log(L/I) / log(2);
	printf("New compression: %.0lf bits (%.2lf bits per posting)\n", newCost, newCost / L);

	double oldCost2 =  L * log(N/L+1) / log(2) + R * log(N/R+1) / log(2);
	printf("Total cost (old compression: %.0lf bits (%.2lf bits per posting)\n",
			oldCost2, oldCost2 / (L + R));

	double newCost2 =
		I * log(N/I+1) / log(2) + (L-I) * log(N/(L-I)+1) / log(2) + (R-I) * log(N/(R-I)+1) / log(2);
	printf("Factoring out the intersection: %.0lf bits (%.2lf bits per posting)\n",
			newCost2, newCost2 / (L + R));

	return 0;
}


