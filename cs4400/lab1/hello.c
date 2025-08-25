# include <stdio.h>

int main(void) {
	printf("Hello, world!\n");
	unsigned int bits = 0xAABBCCDD;
	unsigned char MSB = bits >> 24;
	unsigned char shifted = bits >> 20;
	printf("%x\n", MSB);
	return 0;
}
