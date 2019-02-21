#include "miner.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sha3/sph_groestl.h"

// Convert uint8_t to uint64_t
uint64_t u8tou64(uint8_t const *u8){
	uint64_t u64;
	memcpy(&u64, u8, sizeof(u64));
	return u64;
}

// Any arbitrary data can be used as salt
void getsalt(const void* input, unsigned char* salt) {
	// Copy previous block hash from block header
	memcpy(salt, input + 4, 32 * sizeof(*input));
}

// Return pointer to the block by index
void *block_index(uint8_t *blocks, size_t i) {
	return blocks + (64 * i);
}

void balloon_hash(void *output, const void *input)
{
	const uint64_t s_cost = 16;
	const uint64_t t_cost = 2;
	const int delta = 3;
	
	sph_groestl512_context context;
	uint8_t blocks[s_cost * 64];
	uint64_t cnt = 0;

	const int slength = 32;
	uint8_t salt[slength];
	getsalt(input, salt);
	
	// Step 1: Expand input into buffer
	sph_groestl512_init(&context);
	sph_groestl512(&context, (uint8_t *)&cnt, sizeof((uint8_t *)&cnt));
	sph_groestl512(&context, input, 80);
	sph_groestl512(&context, salt, slength);
	sph_groestl512_close(&context, block_index(blocks, 0));
	cnt++;

	for (int m = 1; m < s_cost; m++) {
		sph_groestl512(&context, (uint8_t *)&cnt, sizeof((uint8_t *)&cnt));
		sph_groestl512(&context, block_index(blocks, m - 1), 64);
		sph_groestl512_close(&context, block_index(blocks, m));
		cnt++;
	}

	// Step 2: Mix buffer contents
	for (uint64_t t = 0; t < t_cost; t++) {
		for (uint64_t m = 0; m < s_cost; m++) {
			// Step 2a: Hash last and current blocks
			sph_groestl512(&context, (uint8_t *)&cnt, sizeof((uint8_t *)&cnt));
			sph_groestl512(&context, block_index(blocks, (m - 1) % s_cost), 64);
			sph_groestl512(&context, block_index(blocks, m), 64);
			sph_groestl512_close(&context, block_index(blocks, m));
			cnt++;

			for (uint64_t i = 0; i < delta; i++) {
				// Step 2b: Hash in pseudorandomly chosen blocks
				uint8_t index[64];
				sph_groestl512(&context, (uint8_t *)&cnt, sizeof((uint8_t *)&cnt));
				sph_groestl512(&context, (uint8_t *)&t, sizeof((uint8_t *)&t));
				sph_groestl512(&context, (uint8_t *)&m, sizeof((uint8_t *)&m));
				sph_groestl512(&context, (uint8_t *)&i, sizeof((uint8_t *)&i));
				sph_groestl512(&context, salt, slength);
				sph_groestl512_close(&context, index);
				cnt++;

				uint64_t other = u8tou64(index) % s_cost;
				sph_groestl512(&context, (uint8_t *)&cnt, sizeof((uint8_t *)&cnt));
				sph_groestl512(&context, block_index(blocks, m), 64);
				sph_groestl512(&context, block_index(blocks, other), 64);
				sph_groestl512_close(&context, block_index(blocks, m));
				cnt++;
			}
		}
	}
	
	memcpy(output, block_index(blocks, s_cost - 1), 32);
}

int scanhash_balloon(int thr_id, struct work *work, uint32_t max_nonce, uint64_t *hashes_done)
{
	uint32_t _ALIGN(128) hash[8];
	uint32_t _ALIGN(128) endiandata[20];
	uint32_t *pdata = work->data;
	uint32_t *ptarget = work->target;

	const uint32_t Htarg = ptarget[7];
	const uint32_t first_nonce = pdata[19];
	uint32_t nonce = first_nonce;

	// if (opt_benchmark)
	// 	ptarget[7] = 0x00ff;

	for (int k=0; k < 19; k++)
		be32enc(&endiandata[k], pdata[k]);

	do {
		be32enc(&endiandata[19], nonce);
		balloon_hash(hash, endiandata);

		// applog_hex((void *) hash, 32); 

		if (hash[7] <= Htarg && fulltest(hash, ptarget)) {
			work_set_target_ratio(work, hash);
			pdata[19] = nonce;
			*hashes_done = pdata[19] - first_nonce;
			return 1;
		}
		nonce++;

	} while (nonce < max_nonce && !work_restart[thr_id].restart);

	pdata[19] = nonce;
	*hashes_done = pdata[19] - first_nonce + 1;
	return 0;
}
