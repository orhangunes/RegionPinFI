/*
 ============================================================================
 Name        : HelloWorld.c
 Author      : Orhan
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void orhanMessage(int params1, int params2);
void orhanIndegree();

int main(void) {
	printf("!!!Hello World!!!");

	for (int i = 0; i < 10; i++) {
		orhanMessage(5,6);
	}

	//orhanMessage for inDegree
	orhanIndegree();

	return EXIT_SUCCESS;
}

void orhanMessage(int params1, int params2) {
	// Alu instruction
	int add = 2+5;
	int sub = 5+3;
	printf("\nOrhan message pinfi");

	// Branch instruction IF-ELSE structure
	if(add == 7) {
		printf("\nOrhan message pinfi - branch instruction");
	}

	//Floating Point (FP) instruction
    float decimal1 = 10.5f;
    float decimal2 = 2.3f;
	float resultFloat = decimal1 + decimal2;

	//Loop iteration count instruction
	for (int i = 0; i < 5; i++) {
	}

	//orhanMessage for outDegree
	double process = (double)(params1 * params2); 
    double outDegree = sqrt(process);
	printf("\nSqrt result: %f", outDegree);
}

void orhanIndegree() {
	int a = 1;
	orhanMessage(5,6);
}
