#include <stdio.h>

void generateDecoder(int bitWidth) {
	printf("byte* decompressPFoR_%d(byte *compressed, offset startValue, offset *uncompressed) {\n",
	       bitWidth);
	printf("  uint32_t buffer = *((uint32_t*)compressed);\n");
	printf("  compressed += 4;\n");
	int bitsInBuffer = 32;
	for (int i = 0; i < 32; i++) {
		if (bitsInBuffer == 0) {
			// If the buffer is empty, refill it.
			printf("  buffer = *((uint32_t*)compressed);\n");
			printf("  compressed += 4;\n");
			bitsInBuffer = 32;
		}
		if (bitsInBuffer >= bitWidth) {
			// This is the easy case. Just take it out of the buffer.
			printf("  startValue += (buffer & 0x%X);\n", (1LL << bitWidth) - 1);
			printf("  *uncompressed++ = startValue;\n");
			if ((i < 31) && (bitWidth != 32))
				printf("  buffer >>= %d;\n", bitWidth);
			bitsInBuffer -= bitWidth;
		} else {
			// This is the case where the current delta is spread across two
			// different 32-bit words.
			int bitsRemaining = bitWidth - bitsInBuffer;
			printf("  startValue += buffer;\n");
			printf("  buffer = *((uint32_t*)compressed);\n");
			printf("  compressed += 4;\n");
			printf("  startValue += (buffer & 0x%X) << %d;\n",
			       (1LL << bitsRemaining) - 1, bitsInBuffer);
			printf("  *uncompressed++ = startValue;\n");
			printf("  buffer >>= %d;\n", bitsRemaining);
			bitsInBuffer = 32 - bitsRemaining;			
		}
	}
	printf("  return compressed;\n");
	printf("}\n\n");
}

int main() {
	for (int i = 1; i <= 32; i++)
		generateDecoder(i);
	return 0;
}

