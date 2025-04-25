/*
 *  Copyright 2014-2022 The GmSSL Project. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the License); you may
 *  not use this file except in compliance with the License.
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 */
/*************************
 * 
在 SM4 加密算法中，ROUND_SBOX 和 ROUND_TBOX 是两种不同的实现方式，它们都用于完成 SM4 的一轮加密操作，但在实现细节和性能上有所不同。以下是它们的区别和具体说明：
1. ROUND_SBOX
ROUND_SBOX 是基于 S 盒（Substitution Box）的实现方式。S 盒是一种非线性变换，用于将输入的 8 位数据替换为另一个 8 位数据。SM4 算法中使用了 4 个 S 盒，每个 S 盒对应一个 8 位的输入和输出。
工作原理
    将 32 位输入数据拆分为 4 个 8 位的数据块。
    每个 8 位的数据块通过 S 盒进行替换。
    将替换后的 4 个 8 位数据块重新组合为 32 位输出数据。
    进行线性变换（如移位和异或操作）。
优点
    安全性高：S 盒是非线性变换，增加了加密过程的复杂性和安全性。
    灵活性高：S 盒的实现可以灵活调整，便于优化和改进。
缺点
    性能较低：每次加密都需要多次查找 S 盒，计算开销较大。

2. ROUND_TBOX
ROUND_TBOX 是基于 T 盒（Tweak Box）的实现方式。T 盒是一种优化技术，通过将 S 盒的非线性变换和后续的线性变换合并为一个预计算的表（T 盒），从而减少计算开销。
工作原理
    预计算一个 T 盒，将 S 盒的输出和线性变换的结果预先存储在表中。
    在加密过程中，直接通过 T 盒查找结果，避免了多次 S 盒查找和线性变换的计算。
    通过 T 盒直接得到 32 位的输出数据。
优点
    性能高：通过预计算和表查找，减少了计算开销，提高了加密速度。
    适合硬件实现：T 盒的表查找操作适合硬件实现，可以进一步提高性能。
缺点
    内存占用大：需要预计算并存储 T 盒，增加了内存占用。
    灵活性较低：T 盒的实现依赖于预计算，调整和优化相对复杂。

3. 性能对比
    速度：ROUND_TBOX 通常比 ROUND_SBOX 更快，因为它减少了 S 盒查找和线性变换的计算开销。
    内存占用：ROUND_TBOX 需要更多的内存来存储预计算的 T 盒，而 ROUND_SBOX 只需要存储 S 盒。
    安全性：两者在安全性上没有本质区别，因为它们都是基于 SM4 算法的实现。

4. 应用场景
    ROUND_SBOX：
        适用于对内存占用敏感的场景，例如嵌入式设备。
        适用于需要灵活调整加密算法的场景。
    ROUND_TBOX：
        适用于对性能要求较高的场景，例如服务器端加密。
        适用于硬件实现，可以进一步提高加密速度。


ROUND_TBOX技术论文：https://html.rhhz.net/ZGKXYDXXB/20180205.htm

**************************/

#include <gmssl/sm4.h>
#include <gmssl/endian.h>
#include "sm4_lcl.h"


#define L32(x)							\
	((x) ^							\
	ROL32((x),  2) ^					\
	ROL32((x), 10) ^					\
	ROL32((x), 18) ^					\
	ROL32((x), 24))

#define ROUND_SBOX(x0, x1, x2, x3, x4, i)			\
	x4 = x1 ^ x2 ^ x3 ^ *(rk + i);				\
	x4 = S32(x4);						\
	x4 = x0 ^ L32(x4)

#define ROUND_TBOX(x0, x1, x2, x3, x4, i)			\
	x4 = x1 ^ x2 ^ x3 ^ *(rk + i);				\
	t0 = ROL32(SM4_T[(uint8_t)x4], 8);			\
	x4 >>= 8;						\
	x0 ^= t0;						\
	t0 = ROL32(SM4_T[(uint8_t)x4], 16);			\
	x4 >>= 8;						\
	x0 ^= t0;						\
	t0 = ROL32(SM4_T[(uint8_t)x4], 24);			\
	x4 >>= 8;						\
	x0 ^= t0;						\
	t1 = SM4_T[x4];					\
	x4 = x0 ^ t1

#define ROUND ROUND_TBOX


void sm4_encrypt(const SM4_KEY *key, const unsigned char in[16], unsigned char out[16])
{
	const uint32_t *rk = key->rk;
	uint32_t x0, x1, x2, x3, x4;
	uint32_t t0, t1;

	x0 = GETU32(in     );
	x1 = GETU32(in +  4);
	x2 = GETU32(in +  8);
	x3 = GETU32(in + 12);
	ROUNDS(x0, x1, x2, x3, x4);
	PUTU32(out     , x0);
	PUTU32(out +  4, x4);
	PUTU32(out +  8, x3);
	PUTU32(out + 12, x2);
}

/* caller make sure counter not overflow */
void sm4_ctr32_encrypt_blocks(const unsigned char *in, unsigned char *out,
	size_t blocks, const SM4_KEY *key, const unsigned char iv[16])
{
	const uint32_t *rk = key->rk;
	unsigned int c0 = GETU32(iv     );
	unsigned int c1 = GETU32(iv +  4);
	unsigned int c2 = GETU32(iv +  8);
	unsigned int c3 = GETU32(iv + 12);
	uint32_t x0, x1, x2, x3, x4;
	uint32_t t0, t1;

	while (blocks--) {
		x0 = c0;
		x1 = c1;
		x2 = c2;
		x3 = c3;
		ROUNDS(x0, x1, x2, x3, x4);
		PUTU32(out     , GETU32(in     ) ^ x0);
		PUTU32(out +  4, GETU32(in +  4) ^ x4);
		PUTU32(out +  8, GETU32(in +  8) ^ x3);
		PUTU32(out + 12, GETU32(in + 12) ^ x2);
		in += 16;
		out += 16;
		c3++;
	}
}
